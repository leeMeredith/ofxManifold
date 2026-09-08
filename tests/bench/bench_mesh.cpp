// ofxManifold — mesh scaling benchmark.
//
// Everything in the suite is five to nine nodes. This asks what happens at
// thirty, three hundred, and beyond -- BEFORE deciding whether anything needs
// optimizing. Measure, then decide.
//
// Not a conformance test. It asserts nothing and gates nothing; it prints
// numbers so a decision can be made from evidence.

#include "../../src/core/ofxManifoldEvaluator.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace ofxManifold;
using Clock = std::chrono::steady_clock;

// A triangulated n x n grid: n*n nodes, 2*(n-1)^2 regions, every interior edge
// shared whole, so the topology is clean by construction.
static Manifold2D grid(int n) {
    Manifold2D m;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            m.addNode("n" + std::to_string(y * n + x),
                      {static_cast<float>(x) / (n - 1),
                       static_cast<float>(y) / (n - 1)});
        }
    }
    for (int y = 0; y < n - 1; ++y) {
        for (int x = 0; x < n - 1; ++x) {
            const NodeID a = y * n + x, b = a + 1;
            const NodeID c = a + n,     d = c + 1;
            m.addTriangle(a, b, c);
            m.addTriangle(b, d, c);
        }
    }
    return m;
}

template <typename F>
static double timeMs(int iters, F f) {
    const auto t0 = Clock::now();
    for (int i = 0; i < iters; ++i) f(i);
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    std::printf("%6s %7s %8s  %10s %10s %10s  %9s\n",
                "grid", "nodes", "regions",
                "scan us", "hint us", "jump us", "valid ms");
    std::printf("%s\n", std::string(72, '-').c_str());

    for (int n : {4, 6, 10, 18, 32, 56, 100}) {
        const Manifold2D m = grid(n);
        const int nodes   = static_cast<int>(m.nodeCount());
        const int regions = static_cast<int>(m.regionCount());

        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> u(0.02f, 0.98f);
        std::vector<glm::vec2> pts;
        for (int i = 0; i < 2000; ++i) pts.push_back({u(rng), u(rng)});

        const int iters = 2000;

        // Cold scan: no hint, every call a linear search from region 0.
        volatile float sink = 0.0f;
        const double scan = timeMs(iters, [&](int i) {
            const Evaluation e = m.evaluate(pts[i % pts.size()]);
            if (!e.weights.empty()) sink = sink + e.weights[0].weight;
        });

        // Hinted, moving smoothly: the realistic case. A control point drifts,
        // so it is usually still in the region it was in last frame.
        Evaluator ev(m);
        glm::vec2 p(0.5f, 0.5f);
        const double hint = timeMs(iters, [&](int) {
            p += glm::vec2(0.0007f, 0.0005f);
            if (p.x > 0.97f) p.x = 0.03f;
            if (p.y > 0.97f) p.y = 0.03f;
            const Evaluation e = ev.evaluate(p);
            if (!e.weights.empty()) sink = sink + e.weights[0].weight;
        });

        // Hinted, jumping: the hint always misses, so it costs one wasted
        // containment test on top of the scan. This is the worst case.
        Evaluator ev2(m);
        const double jump = timeMs(iters, [&](int i) {
            const Evaluation e = ev2.evaluate(pts[i % pts.size()]);
            if (!e.weights.empty()) sink = sink + e.weights[0].weight;
        });

        // validate() is O(regions * 3 * nodes). Authoring-time only, but worth
        // knowing where that stops being true.
        const int vIters = (regions > 4000) ? 1 : 5;
        const double valid = timeMs(vIters, [&](int) {
            const TopologyReport r = m.validate();
            if (!r.clean()) sink = sink + 1.0f;
        }) / vIters;

        std::printf("%4dx%-2d %7d %8d  %10.3f %10.3f %10.3f  %9.2f\n",
                    n, n, nodes, regions,
                    scan * 1000.0 / iters,
                    hint * 1000.0 / iters,
                    jump * 1000.0 / iters,
                    valid);
        std::fflush(stdout);
    }
    return 0;
}
