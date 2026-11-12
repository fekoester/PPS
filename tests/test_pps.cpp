#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#if __has_include(<doctest/doctest.h>)
#include <doctest/doctest.h>
#else
#include "doctest.h"
#endif

#include "pps.hpp"

using pps::Params;
using pps::Simulation;

static Simulation two(double hx, double hy, double heading, double nx, double ny, Params p = Params()) {
    Simulation s(p, 2, 1);
    s.place(0, hx, hy, heading);
    s.place(1, nx, ny, 0.0);
    return s;
}

TEST_CASE("left / right relative to the heading") {
    // heading 0 deg = +x; a neighbour at +y is on the left, at -y on the right
    auto s = two(50, 50, 0, 50, 52);
    CHECK(s.count(0).left == 1);
    CHECK(s.count(0).right == 0);
    s = two(50, 50, 0, 50, 48);
    CHECK(s.count(0).right == 1);
    // heading 90 deg = +y: a neighbour at +x is on the right
    s = two(50, 50, 90, 52, 50);
    CHECK(s.count(0).right == 1);
    // outside r = 5: not counted
    s = two(50, 50, 0, 50, 56);
    CHECK(s.count(0).total() == 0);
}

TEST_CASE("motion law: delta_phi = alpha + beta N sign(R - L), positive = turn right") {
    auto s = two(50, 50, 90, 52, 50);  // one neighbour on the right
    CHECK(s.turn(s.count(0)) == doctest::Approx(180 + 17));
    s = two(50, 50, 90, 48, 50);  // one neighbour on the left
    CHECK(s.turn(s.count(0)) == doctest::Approx(180 - 17));
    // turning right from heading 90 deg by 197 deg gives heading 90 - 197 = -107 = 253 deg
    Params p;
    p.asynchronous = false;
    auto t = two(50, 50, 90, 52, 50, p);
    t.step();
    CHECK(t.phi[0] == doctest::Approx(253.0));
}

TEST_CASE("an isolated particle with alpha = 180 deg holds its position every two steps") {
    Simulation s(Params(), 1, 7);
    const double x0 = s.x[0], y0 = s.y[0];
    for (int k = 0; k < 10; k++) s.step();
    CHECK(s.x[0] == doctest::Approx(x0).epsilon(1e-9));
    CHECK(s.y[0] == doctest::Approx(y0).epsilon(1e-9));
}

TEST_CASE("grid neighbour search equals brute force (torus, walls, tiny worlds)") {
    for (bool torus : {true, false}) {
        for (double size : {7.0, 12.0, 60.0}) {
            Params p;
            p.torus = torus;
            p.width = size;
            p.height = size * 1.3;
            Simulation s = Simulation::withDensity(p, 0.5, 11);
            for (int rep = 0; rep < 3; rep++) {
                for (std::size_t i = 0; i < s.size(); i++) {
                    auto a = s.count(i), b = s.countBrute(i);
                    REQUIRE(a.left == b.left);
                    REQUIRE(a.right == b.right);
                    REQUIRE(a.close == b.close);
                }
                for (int k = 0; k < 5; k++) s.step();  // the grid must stay consistent while particles move
            }
        }
    }
}

TEST_CASE("torus distances wrap around") {
    Params p;
    p.width = p.height = 100;
    auto s = two(1, 50, 180, 99, 50, p);  // 2 units apart across the edge
    CHECK(s.count(0).total() == 1);
    p.torus = false;
    auto w = two(1, 50, 180, 99, 50, p);
    CHECK(w.count(0).total() == 0);
}

TEST_CASE("positions stay inside the world") {
    for (bool torus : {true, false}) {
        Params p;
        p.torus = torus;
        p.width = 30;
        p.height = 20;
        Simulation s = Simulation::withDensity(p, 0.3, 5);
        for (int k = 0; k < 200; k++) s.step();
        for (std::size_t i = 0; i < s.size(); i++) {
            CHECK(s.x[i] >= 0);
            CHECK(s.x[i] <= p.width);
            CHECK(s.y[i] >= 0);
            CHECK(s.y[i] <= p.height);
            CHECK(s.phi[i] >= 0);
            CHECK(s.phi[i] < 360);
        }
    }
}

TEST_CASE("colour rule of the paper") {
    using pps::Color;
    CHECK(pps::classify(5, 0) == Color::Green);
    CHECK(pps::classify(13, 0) == Color::Brown);
    CHECK(pps::classify(15, 0) == Color::Brown);
    CHECK(pps::classify(16, 0) == Color::Blue);
    CHECK(pps::classify(35, 0) == Color::Blue);
    CHECK(pps::classify(36, 0) == Color::Yellow);
    CHECK(pps::classify(12, 16) == Color::Magenta);
    CHECK(pps::classify(12, 15) == Color::Green);
}

TEST_CASE("same seed, same run; sync and async differ") {
    Params p;
    p.width = p.height = 40;
    Simulation a = Simulation::withDensity(p, 0.1, 3), b = Simulation::withDensity(p, 0.1, 3);
    for (int k = 0; k < 50; k++) a.step(), b.step();
    CHECK(a.x == b.x);
    p.asynchronous = false;
    Simulation c = Simulation::withDensity(p, 0.1, 3);
    for (int k = 0; k < 50; k++) c.step();
    CHECK(a.x != c.x);
}

TEST_CASE("structures emerge at the paper's parameters") {
    // nucleation is stochastic: this world and seed form structures within ~600 steps (see docs/pps.gif)
    Params p;
    p.width = p.height = 120;
    Simulation s = Simulation::withDensity(p, 0.08, 4);
    s.refreshNeighbourhoods();
    const auto before = s.colorCounts();
    for (int k = 0; k < 1500; k++) s.step();
    const auto after = s.colorCounts();
    // dense (blue/yellow) particles: almost none in the random start, a sizeable fraction later
    const double dense0 = double(before[2] + before[3]) / s.size(), dense1 = double(after[2] + after[3]) / s.size();
    CHECK(dense0 < 0.02);
    CHECK(dense1 > 0.10);
}
