// Primordial Particle System (Schmickl, Stefanec, Crailsheim, Sci. Rep. 6, 37969 (2016)).
//
// Every particle has a position p and a heading phi. Each time step it counts the other particles within
// radius r in its left (L) and right (R) half-plane, N = L + R, turns RIGHT by
//     delta_phi = alpha + beta * N * sign(R - L)
// and moves forward by v:  p <- p + v (cos phi, sin phi).
// Default parameters PPS = <r = 5, alpha = 180 deg, beta = 17 deg, v = 0.67>, toroidal world.
//
// Coordinates are mathematical (y up, angles counter-clockwise), so a right turn decreases phi.
// Header-only, no graphics dependency; the SFML viewer and the headless runner live in main.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace pps {

constexpr double kPi = 3.14159265358979323846;

struct Params {
    double alpha = 180.0;     // fixed rotation per step, degrees
    double beta = 17.0;       // rotation per neighbour, degrees
    double v = 0.67;          // distance moved per step
    double r = 5.0;           // neighbourhood radius
    double rClose = 1.3;      // radius of the close neighbourhood used for the magenta colour
    double width = 150.0;     // world size
    double height = 150.0;
    bool torus = true;        // false: reflecting walls
    bool asynchronous = true; // particles updated one after another (as in the paper's pseudo-code)
};

enum class Color : uint8_t { Green, Brown, Blue, Yellow, Magenta };

inline const char *colorName(Color c) {
    switch (c) {
        case Color::Green: return "green";
        case Color::Brown: return "brown";
        case Color::Blue: return "blue";
        case Color::Yellow: return "yellow";
        case Color::Magenta: return "magenta";
    }
    return "?";
}

// Colour rule of the paper: blue if 15 < N <= 35, yellow if N > 35, brown if 13 <= N <= 15,
// magenta if N(r = 1.3) > 15, else green.
inline Color classify(int n, int nClose) {
    if (n > 15 && n <= 35) return Color::Blue;
    if (n > 35) return Color::Yellow;
    if (n >= 13 && n <= 15) return Color::Brown;
    if (nClose > 15) return Color::Magenta;
    return Color::Green;
}

struct Neighbourhood {
    int left = 0, right = 0, close = 0;
    int total() const { return left + right; }
};

class Simulation {
  public:
    std::vector<double> x, y, phi;  // phi in degrees
    std::vector<Neighbourhood> nb;  // neighbourhood seen at the last update of each particle
    long long stepCount = 0;

    Simulation(const Params &p, std::size_t n, uint64_t seed) : par_(p), rng_(seed) {
        if (p.width <= 0 || p.height <= 0 || p.r <= 0) throw std::invalid_argument("world size and r must be > 0");
        std::uniform_real_distribution<double> ux(0, p.width), uy(0, p.height), ua(0, 360);
        x.resize(n), y.resize(n), phi.resize(n), nb.resize(n);
        for (std::size_t i = 0; i < n; i++) x[i] = ux(rng_), y[i] = uy(rng_), phi[i] = ua(rng_);
        initGrid();
    }

    static Simulation withDensity(const Params &p, double density, uint64_t seed) {
        return Simulation(p, (std::size_t)std::llround(density * p.width * p.height), seed);
    }

    const Params &params() const { return par_; }
    std::size_t size() const { return x.size(); }

    // Displacement from particle i to the point (px, py), using the nearest periodic image on a torus.
    void delta(std::size_t i, double px, double py, double &dx, double &dy) const {
        dx = px - x[i], dy = py - y[i];
        if (par_.torus) {
            if (dx > 0.5 * par_.width) dx -= par_.width; else if (dx < -0.5 * par_.width) dx += par_.width;
            if (dy > 0.5 * par_.height) dy -= par_.height; else if (dy < -0.5 * par_.height) dy += par_.height;
        }
    }

    // Count the neighbours of particle i within r: left / right of its heading, and within rClose.
    // A neighbour exactly ahead or behind (cross product 0) counts as left.
    Neighbourhood count(std::size_t i) const {
        Neighbourhood out;
        const double r2 = par_.r * par_.r, rc2 = par_.rClose * par_.rClose;
        const double hx = std::cos(phi[i] * kPi / 180), hy = std::sin(phi[i] * kPi / 180);
        const int cx = cellX(x[i]), cy = cellY(y[i]);
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                int gx = cx + ox, gy = cy + oy;
                if (par_.torus) gx = (gx + nx_) % nx_, gy = (gy + ny_) % ny_;
                else if (gx < 0 || gy < 0 || gx >= nx_ || gy >= ny_) continue;
                if (par_.torus && visitedDuplicate(ox, oy, gx, gy, cx, cy)) continue;
                for (int j = head_[gy * nx_ + gx]; j != -1; j = next_[j]) {
                    if ((std::size_t)j == i) continue;
                    double dx, dy;
                    delta(i, x[j], y[j], dx, dy);
                    const double d2 = dx * dx + dy * dy;
                    if (d2 > r2) continue;
                    if (hx * dy - hy * dx >= 0) out.left++; else out.right++;  // cross(heading, d) > 0: left
                    if (d2 <= rc2) out.close++;
                }
            }
        }
        return out;
    }

    // The same count by brute force (for tests).
    Neighbourhood countBrute(std::size_t i) const {
        Neighbourhood out;
        const double hx = std::cos(phi[i] * kPi / 180), hy = std::sin(phi[i] * kPi / 180);
        for (std::size_t j = 0; j < size(); j++) {
            if (j == i) continue;
            double dx, dy;
            delta(i, x[j], y[j], dx, dy);
            const double d2 = dx * dx + dy * dy;
            if (d2 > par_.r * par_.r) continue;
            if (hx * dy - hy * dx >= 0) out.left++; else out.right++;
            if (d2 <= par_.rClose * par_.rClose) out.close++;
        }
        return out;
    }

    // Turn of the motion law, in degrees (positive = to the right).
    double turn(const Neighbourhood &n) const {
        const int s = (n.right > n.left) - (n.right < n.left);
        return par_.alpha + par_.beta * n.total() * s;
    }

    void step() {
        const std::size_t n = size();
        if (par_.asynchronous) {
            for (std::size_t i = 0; i < n; i++) {
                nb[i] = count(i);
                rotateAndMove(i, turn(nb[i]));
                moveInGrid(i);
            }
        } else {
            for (std::size_t i = 0; i < n; i++) nb[i] = count(i);
            for (std::size_t i = 0; i < n; i++) rotateAndMove(i, turn(nb[i]));
            rebuildGrid();
        }
        stepCount++;
    }

    // Fresh neighbourhood of every particle (for colouring the current configuration).
    void refreshNeighbourhoods() {
        for (std::size_t i = 0; i < size(); i++) nb[i] = count(i);
    }

    Color color(std::size_t i) const { return classify(nb[i].total(), nb[i].close); }

    // Number of particles of each colour (Green, Brown, Blue, Yellow, Magenta).
    std::vector<std::size_t> colorCounts() const {
        std::vector<std::size_t> c(5, 0);
        for (std::size_t i = 0; i < size(); i++) c[(int)color(i)]++;
        return c;
    }

    // Change one particle's state (keeps the neighbour grid consistent).
    void place(std::size_t i, double px, double py, double heading) {
        x[i] = px, y[i] = py, phi[i] = heading;
        wrapOrReflect(i);
        moveInGrid(i);
    }

  private:
    Params par_;
    std::mt19937_64 rng_;
    int nx_ = 1, ny_ = 1;
    double cw_ = 1, ch_ = 1;
    std::vector<int> head_, next_, prev_, cellOf_;

    int cellX(double px) const { return std::min(nx_ - 1, std::max(0, (int)(px / cw_))); }
    int cellY(double py) const { return std::min(ny_ - 1, std::max(0, (int)(py / ch_))); }

    // With fewer than 3 cells along an axis the 3x3 stencil would visit a cell twice on a torus.
    bool visitedDuplicate(int ox, int oy, int gx, int gy, int cx, int cy) const {
        auto wrapped = [](int c, int o, int n) { return ((c + o) % n + n) % n; };
        for (int py = -1; py <= oy; py++) {
            for (int px = -1; px <= 1; px++) {
                if (py == oy && px >= ox) return false;
                if (wrapped(cx, px, nx_) == gx && wrapped(cy, py, ny_) == gy) return true;
            }
        }
        return false;
    }

    void initGrid() {
        nx_ = std::max(1, (int)std::floor(par_.width / par_.r));
        ny_ = std::max(1, (int)std::floor(par_.height / par_.r));
        cw_ = par_.width / nx_, ch_ = par_.height / ny_;  // cells at least r wide
        rebuildGrid();
    }

    void rebuildGrid() {
        head_.assign((std::size_t)nx_ * ny_, -1);
        next_.assign(size(), -1), prev_.assign(size(), -1), cellOf_.assign(size(), -1);
        for (std::size_t i = 0; i < size(); i++) insert(i);
    }

    void insert(std::size_t i) {
        const int c = cellY(y[i]) * nx_ + cellX(x[i]);
        cellOf_[i] = c;
        prev_[i] = -1;
        next_[i] = head_[c];
        if (head_[c] != -1) prev_[head_[c]] = (int)i;
        head_[c] = (int)i;
    }

    void remove(std::size_t i) {
        const int c = cellOf_[i];
        if (prev_[i] != -1) next_[prev_[i]] = next_[i]; else head_[c] = next_[i];
        if (next_[i] != -1) prev_[next_[i]] = prev_[i];
    }

    void moveInGrid(std::size_t i) {
        const int c = cellY(y[i]) * nx_ + cellX(x[i]);
        if (c != cellOf_[i]) remove(i), insert(i);
    }

    void rotateAndMove(std::size_t i, double turnDeg) {
        phi[i] = std::fmod(phi[i] - turnDeg, 360.0);  // right turn = clockwise = decreasing angle
        if (phi[i] < 0) phi[i] += 360.0;
        x[i] += par_.v * std::cos(phi[i] * kPi / 180);
        y[i] += par_.v * std::sin(phi[i] * kPi / 180);
        wrapOrReflect(i);
    }

    void wrapOrReflect(std::size_t i) {
        if (par_.torus) {
            x[i] = std::fmod(x[i], par_.width), y[i] = std::fmod(y[i], par_.height);
            if (x[i] < 0) x[i] += par_.width;
            if (y[i] < 0) y[i] += par_.height;
            return;
        }
        // reflecting walls: mirror the position back inside and the heading component
        if (x[i] < 0) x[i] = -x[i], phi[i] = 180.0 - phi[i];
        if (x[i] > par_.width) x[i] = 2 * par_.width - x[i], phi[i] = 180.0 - phi[i];
        if (y[i] < 0) y[i] = -y[i], phi[i] = -phi[i];
        if (y[i] > par_.height) y[i] = 2 * par_.height - y[i], phi[i] = -phi[i];
        x[i] = std::min(std::max(x[i], 0.0), par_.width);
        y[i] = std::min(std::max(y[i], 0.0), par_.height);
        phi[i] = std::fmod(phi[i] + 720.0, 360.0);
    }
};

}  // namespace pps
