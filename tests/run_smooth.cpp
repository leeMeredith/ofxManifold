// ofxManifoldPlayer -- output smoother vector runner (round 3B-1).
//
// Sequences, like the kernel's smoother suite: each script snaps or resets,
// then ticks, and every tick's list, its length and whether it has settled
// are checked against tests/vectors/smooth.vec (from
// tests/ref/reference_smooth.py), within 1e-6.
//
// Scripts marked kernel are also run through the kernel's WeightSmoother, on
// one node holding the same number: the two must agree within 1e-6 until the
// player's smoother first arrives (sets a number to its target from within
// kArrived), and within kArrived after -- the one way they are meant to
// differ.

#include "../src/interpretation/ofxManifoldSmoother.h"
#include "../ofxManifoldPlayer/src/ofxManifoldOutputSmoother.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxManifold;
using namespace ofxManifold::player;

namespace {

constexpr float kTol = 1e-6f;

std::vector<float> readList(std::istringstream& in) {
    std::size_t n = 0;
    in >> n;
    std::vector<float> v(n);
    for (float& x : v) in >> x;
    return v;
}

std::string show(const std::vector<float>& v) {
    std::ostringstream o;
    o << std::setprecision(9);
    for (float x : v) o << " " << x;
    return o.str();
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/vectors/smooth.vec";
    std::ifstream file(path);
    if (!file) { std::cerr << "cannot open " << path << "\n"; return 2; }
    std::cout << "ofxManifoldPlayer output smoother\nvectors: " << path << "\n\n";

    std::map<std::string, std::pair<int, int>> tally;   // class -> pass, fail
    std::string line, name, cls, why;
    OutputSmoother s;
    WeightSmoother kernel;
    bool useKernel = false, arrived = false, ok = true;
    int tick = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kw;
        in >> kw;
        if (kw == "SMOOTH") {
            float h = 0, r = 0;
            int k = 0;
            in >> name >> cls >> h >> r >> k;
            s = OutputSmoother();
            s.setHalfLife(h);
            s.setMaxRate(r);
            kernel = WeightSmoother();
            kernel.setHalfLife(h);
            kernel.setMaxRate(r);
            useKernel = k != 0;
            arrived = false;
            ok = true;
            why.clear();
            tick = 0;
        } else if (kw == "SNAP") {
            const std::vector<float> v = readList(in);
            s.snap(v);
            if (useKernel) kernel.snap(WeightVector{WeightedNode{0, v.at(0)}});
        } else if (kw == "RESET") {
            s.reset();
        } else if (kw == "TICK") {
            ++tick;
            float dt = 0;
            in >> dt;
            const std::vector<float> target = readList(in);
            std::string expectKw;
            in >> expectKw;
            const std::vector<float> want = readList(in);
            int settled = 0;
            in >> settled;
            const std::vector<float> got = s.update(target, dt);
            if (!ok) continue;
            if (got.size() != want.size()) {
                ok = false;
                why = "tick " + std::to_string(tick) + ": " + std::to_string(got.size())
                    + " numbers, want " + std::to_string(want.size());
                continue;
            }
            for (std::size_t k = 0; k < got.size() && ok; ++k) {
                if (std::fabs(got[k] - want[k]) > kTol) {
                    ok = false;
                    why = "tick " + std::to_string(tick) + ":" + show(got) + " -- want" + show(want);
                }
            }
            if (ok && int(s.settled()) != settled) {
                ok = false;
                why = "tick " + std::to_string(tick) + ": settled " + std::to_string(s.settled())
                    + ", want " + std::to_string(settled);
            }
            if (ok && useKernel) {
                const WeightVector& kv = kernel.update(WeightVector{WeightedNode{0, target.at(0)}}, dt);
                const float kw0 = kv.empty() ? 0.0f : kv[0].weight;
                if (got[0] == target[0] && std::fabs(kw0 - target[0]) > 0.0f) arrived = true;
                const float allowed = arrived ? OutputSmoother::kArrived : kTol;
                if (std::fabs(got[0] - kw0) > allowed) {
                    ok = false;
                    std::ostringstream o;
                    o << std::setprecision(9) << "tick " << tick << ": " << got[0]
                      << ", the kernel's smoother " << kw0;
                    why = o.str();
                }
            }
        } else if (kw == "END") {
            auto& t = tally[cls];
            (ok ? t.first : t.second)++;
            std::cout << (ok ? "  pass  " : "  FAIL  ") << std::left << std::setw(10) << cls
                      << name << "\n";
            if (!ok) std::cout << "        " << why << "\n";
        }
    }

    int pass = 0, fail = 0;
    std::cout << "\n";
    for (const auto& t : tally) {
        std::cout << "  " << std::left << std::setw(9) << t.first << " " << t.second.first
                  << "/" << (t.second.first + t.second.second) << "\n";
        pass += t.second.first;
        fail += t.second.second;
    }
    std::cout << "\n" << pass << "/" << (pass + fail) << (fail ? "  RED" : "  GREEN") << "\n";
    return fail ? 1 : 0;
}
