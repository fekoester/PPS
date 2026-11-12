// Primordial Particle System: SFML viewer and headless runner.
//
//   ./PPS                                   # window, paper parameters, 0.08 particles per unit area
//   ./PPS --size 150x150 --density 0.12 --seed 3
//   ./PPS --headless --steps 5000 --stats stats.csv --dump-every 50 --dump-dir frames
//
// See ./PPS --help for every option.
#include "pps.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef PPS_WITH_SFML
#include <SFML/Graphics.hpp>
#endif

namespace {

struct Options {
    pps::Params par;
    double density = 0.08;
    long long particles = -1;  // overrides density
    uint64_t seed = 1;
    bool headless = false;
    long long steps = 1000;
    std::string statsFile, dumpDir;
    long long dumpEvery = 0;
    double scale = 5.0;  // pixels per unit in the viewer
    int stepsPerFrame = 1;
};

void usage() {
    std::cout <<
        "Primordial Particle System (Schmickl et al., Sci. Rep. 6, 37969 (2016))\n\n"
        "  --alpha DEG        fixed turn per step (default 180)\n"
        "  --beta DEG         turn per neighbour (default 17)\n"
        "  --v V              step length (default 0.67)\n"
        "  --r R              neighbourhood radius (default 5)\n"
        "  --size WxH         world size (default 150x150)\n"
        "  --density D        particles per unit area (default 0.08)\n"
        "  --particles N      number of particles (overrides --density)\n"
        "  --walls            reflecting walls instead of a torus\n"
        "  --sync             update all particles simultaneously (default: one after another, as in the paper)\n"
        "  --seed S           random seed (default 1)\n"
        "  --headless         no window: run --steps steps and write --stats / --dump-dir\n"
        "  --steps N          steps for --headless (default 1000)\n"
        "  --stats FILE       CSV with the colour counts after every step\n"
        "  --dump-every K     with --dump-dir: write the particles every K steps\n"
        "  --dump-dir DIR     frame files DIR/frame_000000.csv (x, y, phi, n, color)\n"
        "  --scale PX         viewer: pixels per unit (default 5)\n"
        "  --speed K          viewer: steps per frame (default 1)\n\n"
        "Viewer keys: space pause, right arrow single step, +/- speed, R restart (new seed), S screenshot, Esc quit\n";
}

double num(const std::string &flag, const std::string &v) {
    char *end = nullptr;
    double x = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') throw std::invalid_argument("not a number after " + flag + ": " + v);
    return x;
}

Options parse(int argc, char **argv) {
    Options o;
    auto value = [&](int &i) {
        if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value after ") + argv[i]);
        return std::string(argv[++i]);
    };
    for (int i = 1; i < argc; i++) {
        const std::string f = argv[i];
        if (f == "-h" || f == "--help") { usage(); std::exit(0); }
        else if (f == "--alpha") o.par.alpha = num(f, value(i));
        else if (f == "--beta") o.par.beta = num(f, value(i));
        else if (f == "--v") o.par.v = num(f, value(i));
        else if (f == "--r") o.par.r = num(f, value(i));
        else if (f == "--density") o.density = num(f, value(i));
        else if (f == "--particles") o.particles = (long long)num(f, value(i));
        else if (f == "--walls") o.par.torus = false;
        else if (f == "--sync") o.par.asynchronous = false;
        else if (f == "--seed") o.seed = (uint64_t)num(f, value(i));
        else if (f == "--headless") o.headless = true;
        else if (f == "--steps") o.steps = (long long)num(f, value(i));
        else if (f == "--stats") o.statsFile = value(i);
        else if (f == "--dump-every") o.dumpEvery = (long long)num(f, value(i));
        else if (f == "--dump-dir") o.dumpDir = value(i);
        else if (f == "--scale") o.scale = num(f, value(i));
        else if (f == "--speed") o.stepsPerFrame = std::max(1, (int)num(f, value(i)));
        else if (f == "--size") {
            const std::string s = value(i);
            const auto xpos = s.find('x');
            if (xpos == std::string::npos) throw std::invalid_argument("--size expects WxH, e.g. 150x150");
            o.par.width = num(f, s.substr(0, xpos));
            o.par.height = num(f, s.substr(xpos + 1));
        } else throw std::invalid_argument("unknown option " + f + " (see --help)");
    }
    return o;
}

pps::Simulation makeSim(const Options &o) {
    if (o.particles >= 0) return pps::Simulation(o.par, (std::size_t)o.particles, o.seed);
    return pps::Simulation::withDensity(o.par, o.density, o.seed);
}

void dumpFrame(const pps::Simulation &sim, const std::string &dir) {
    char name[64];
    std::snprintf(name, sizeof name, "frame_%06lld.csv", sim.stepCount);
    std::ofstream f(std::filesystem::path(dir) / name);
    f << "x,y,phi,n,n_close,color\n";
    for (std::size_t i = 0; i < sim.size(); i++)
        f << sim.x[i] << "," << sim.y[i] << "," << sim.phi[i] << "," << sim.nb[i].total() << "," << sim.nb[i].close
          << "," << pps::colorName(sim.color(i)) << "\n";
}

int runHeadless(const Options &o) {
    pps::Simulation sim = makeSim(o);
    std::ofstream stats;
    if (!o.statsFile.empty()) {
        stats.open(o.statsFile);
        stats << "step,green,brown,blue,yellow,magenta\n";
    }
    if (!o.dumpDir.empty()) std::filesystem::create_directories(o.dumpDir);
    sim.refreshNeighbourhoods();
    auto record = [&] {
        if (stats.is_open()) {
            auto c = sim.colorCounts();
            stats << sim.stepCount << "," << c[0] << "," << c[1] << "," << c[2] << "," << c[3] << "," << c[4] << "\n";
        }
        if (!o.dumpDir.empty() && o.dumpEvery > 0 && sim.stepCount % o.dumpEvery == 0) dumpFrame(sim, o.dumpDir);
    };
    record();
    const auto t0 = std::chrono::steady_clock::now();
    for (long long s = 0; s < o.steps; s++) {
        sim.step();
        record();
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto c = sim.colorCounts();
    std::cout << sim.size() << " particles, " << o.steps << " steps in " << sec << " s ("
              << (sec > 0 ? o.steps * (double)sim.size() / sec / 1e6 : 0) << " M particle-steps/s)\n"
              << "final colours: green " << c[0] << ", brown " << c[1] << ", blue " << c[2] << ", yellow " << c[3]
              << ", magenta " << c[4] << "\n";
    return 0;
}

#ifdef PPS_WITH_SFML
sf::Color toSf(pps::Color c) {
    switch (c) {
        case pps::Color::Green: return sf::Color(60, 200, 60);
        case pps::Color::Brown: return sf::Color(150, 90, 40);
        case pps::Color::Blue: return sf::Color(60, 110, 255);
        case pps::Color::Yellow: return sf::Color(250, 220, 40);
        case pps::Color::Magenta: return sf::Color(230, 60, 230);
    }
    return sf::Color::White;
}

int runViewer(Options o) {
    pps::Simulation sim = makeSim(o);
    sim.refreshNeighbourhoods();
    const unsigned W = (unsigned)std::lround(o.par.width * o.scale), H = (unsigned)std::lround(o.par.height * o.scale);
    sf::RenderWindow window(sf::VideoMode(W, H), "Primordial Particle System");
    window.setFramerateLimit(60);
    sf::CircleShape dot(std::max(1.0f, (float)(0.5 * o.scale)));
    dot.setOrigin(dot.getRadius(), dot.getRadius());
    bool paused = false;
    while (window.isOpen()) {
        sf::Event e;
        bool single = false;
        while (window.pollEvent(e)) {
            if (e.type == sf::Event::Closed) window.close();
            if (e.type == sf::Event::KeyPressed) {
                switch (e.key.code) {
                    case sf::Keyboard::Escape: window.close(); break;
                    case sf::Keyboard::Space: paused = !paused; break;
                    case sf::Keyboard::Right: single = true; break;
                    case sf::Keyboard::Add: case sf::Keyboard::Equal: o.stepsPerFrame++; break;
                    case sf::Keyboard::Subtract: case sf::Keyboard::Hyphen:
                        o.stepsPerFrame = std::max(1, o.stepsPerFrame - 1); break;
                    case sf::Keyboard::R: o.seed++; sim = makeSim(o); sim.refreshNeighbourhoods(); break;
                    case sf::Keyboard::S: {
                        sf::Texture tex;
                        tex.create(window.getSize().x, window.getSize().y);
                        tex.update(window);
                        const std::string name = "pps_step" + std::to_string(sim.stepCount) + ".png";
                        tex.copyToImage().saveToFile(name);
                        std::cout << "saved " << name << "\n";
                        break;
                    }
                    default: break;
                }
            }
        }
        if (!paused || single)
            for (int s = 0; s < (single ? 1 : o.stepsPerFrame); s++) sim.step();
        window.clear(sf::Color(10, 10, 20));
        for (std::size_t i = 0; i < sim.size(); i++) {
            dot.setPosition((float)(sim.x[i] * o.scale), (float)(H - sim.y[i] * o.scale));  // y up
            dot.setFillColor(toSf(sim.color(i)));
            window.draw(dot);
        }
        window.display();
        window.setTitle("Primordial Particle System - step " + std::to_string(sim.stepCount) +
                        (paused ? " (paused)" : ""));
    }
    return 0;
}
#endif

}  // namespace

int main(int argc, char **argv) {
    try {
        Options o = parse(argc, argv);
        if (o.headless) return runHeadless(o);
#ifdef PPS_WITH_SFML
        return runViewer(o);
#else
        std::cerr << "built without SFML: use --headless (or rebuild with SFML)\n";
        return 1;
#endif
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
