// ofxManifold — smoother conformance vector runner.
//
// The only suite in this project that tests SEQUENCES. Every other runner
// evaluates a pure function once; a smoother has memory, so it is snapped to a
// state and then ticked, with the output asserted at every step.
//
// A smoother that was right on the first tick and wrong on the fourth would
// pass any single-shot check, which is why the vectors are shaped this way.

#include "../src/interpretation/ofxManifoldSmoother.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxManifold;

namespace {

struct Tally { int pass = 0; int fail = 0; };
std::map<std::string, Tally> tallies;
std::vector<std::string> failures;
float tolerance = 1e-6f;

void record(const std::string& cls, const std::string& name, bool ok,
            const std::string& detail) {
    Tally& t = tallies[cls];
    if (ok) ++t.pass; else {
        ++t.fail;
        failures.push_back(cls + "  " + name + "\n      " + detail);
    }
    std::cout << (ok ? "  pass  " : "  FAIL  ")
              << std::left << std::setw(10) << cls << name << "\n";
}

bool close(float a, float b) { return std::fabs(a - b) <= tolerance; }

WeightVector readWeights(std::istringstream& in) {
    WeightVector v;
    std::streampos mark;
    std::string tok;
    while (true) {
        mark = in.tellg();
        if (!(in >> tok)) break;
        const std::size_t eq = tok.find('=');
        if (eq == std::string::npos) { in.clear(); in.seekg(mark); break; }
        v.push_back(WeightedNode{
            static_cast<NodeID>(std::stoul(tok.substr(0, eq))),
            std::stof(tok.substr(eq + 1))});
    }
    return v;
}

void expectKeyword(std::istringstream& in, const char* kw) {
    std::string tok; in >> tok;
    if (tok != kw) {
        std::cerr << "malformed vector: expected " << kw << ", saw " << tok
                  << "\n";
        std::exit(2);
    }
}

std::string show(const WeightVector& v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(6);
    for (const auto& w : v) os << " " << w.id << "=" << w.weight;
    return os.str();
}

bool sameVector(const WeightVector& got, const WeightVector& want,
                std::ostringstream& d) {
    if (got.size() != want.size()) {
        d << "expected " << want.size() << " entries, got " << got.size()
          << "\n      expected" << show(want)
          << "\n      got     " << show(got);
        return false;
    }
    for (const auto& w : want) {
        bool found = false;
        for (const auto& g : got) {
            if (g.id != w.id) continue;
            found = true;
            if (!close(g.weight, w.weight)) {
                d << "\n      node " << w.id << ": expected " << std::fixed
                  << std::setprecision(6) << w.weight << ", got " << g.weight;
                return false;
            }
            break;
        }
        if (!found) { d << "\n      node " << w.id << " missing"; return false; }
    }
    return true;
}

WeightSmoother make(float halfLife, float maxRate) {
    WeightSmoother s;
    s.setHalfLife(halfLife);
    s.setMaxRate(maxRate);
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::string path =
        (argc > 1) ? argv[1] : "tests/vectors/smoother.vec";
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open: " << path << "\n"; return 2; }

    std::cout << "ofxManifold smoother\nvectors: " << path << "\n\n";

    // Block state, for SMOOTH ... ENDSMOOTH sequences.
    bool inBlock = false;
    std::string bName, bCls;
    float bDt = 0.0f;
    WeightSmoother sm;
    bool blockOk = true;
    std::ostringstream blockDetail;
    int tick = 0;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kind; in >> kind;
        if (kind == "TOL") { in >> tolerance; continue; }

        if (kind == "SMOOTH") {
            float hl, mr;
            in >> bName >> bCls;
            expectKeyword(in, "HALFLIFE"); in >> hl;
            expectKeyword(in, "MAXRATE");  in >> mr;
            expectKeyword(in, "DT");       in >> bDt;
            sm = make(hl, mr);
            inBlock = true; blockOk = true; tick = 0;
            blockDetail.str(""); blockDetail.clear();
            continue;
        }
        if (kind == "SNAP" && inBlock) { sm.snap(readWeights(in)); continue; }
        if (kind == "TICK") {
            const WeightVector target = readWeights(in);
            expectKeyword(in, "EXPECT");
            const WeightVector want = readWeights(in);
            const WeightVector& got = sm.update(target, bDt);
            std::ostringstream d;
            if (blockOk && !sameVector(got, want, d)) {
                blockOk = false;
                blockDetail << "tick " << tick << ": " << d.str();
            }
            ++tick;
            continue;
        }
        if (kind == "ENDSMOOTH") {
            record(bCls, bName, blockOk, blockDetail.str());
            inBlock = false;
            continue;
        }

        // ---- single-shot derived assertions ------------------------------
        std::string name, cls; in >> name >> cls;
        std::ostringstream d;

        if (kind == "RATEMATCH") {
            // The same elapsed time at two frame rates must land in the same
            // place. This is the whole reason the API takes a half-life and a
            // dt rather than a per-frame coefficient.
            float hl, dtA, dtB, want; int nA, nB;
            in >> hl >> dtA >> nA >> dtB >> nB;
            expectKeyword(in, "TARGET");
            const WeightVector target = readWeights(in);
            expectKeyword(in, "EXPECT"); in >> want;

            WeightSmoother a = make(hl, 0.0f), b = make(hl, 0.0f);
            a.snap({{0, 0.0f}}); b.snap({{0, 0.0f}});
            for (int i = 0; i < nA; ++i) a.update(target, dtA);
            for (int i = 0; i < nB; ++i) b.update(target, dtB);

            const float va = a.current().empty() ? 0.f : a.current()[0].weight;
            const float vb = b.current().empty() ? 0.f : b.current()[0].weight;
            bool ok = close(va, want) && close(vb, want);
            if (!ok) {
                d << "expected both rates to reach " << std::fixed
                  << std::setprecision(6) << want
                  << "\n      at dt=" << dtA << " x" << nA << ": " << va
                  << "\n      at dt=" << dtB << " x" << nB << ": " << vb
                  << "\n      a per-frame coefficient would differ here";
            }
            record(cls, name, ok, d.str());

        } else if (kind == "SUMAT") {
            float hl, mr, dt, want; int ticks;
            in >> hl >> mr >> dt;
            expectKeyword(in, "SNAP");
            const WeightVector start = readWeights(in);
            expectKeyword(in, "TARGET");
            const WeightVector target = readWeights(in);
            expectKeyword(in, "TICKS"); in >> ticks;
            expectKeyword(in, "SUM");   in >> want;

            WeightSmoother s = make(hl, mr);
            s.snap(start);
            for (int i = 0; i < ticks; ++i) s.update(target, dt);
            const float got = sum(s.current());
            const bool ok = close(got, want);
            if (!ok) d << "after " << ticks << " ticks the weights sum to "
                       << std::fixed << std::setprecision(6) << got
                       << ", expected " << want;
            record(cls, name, ok, d.str());

        } else if (kind == "PRUNE") {
            float hl, mr, dt; int ticks; std::size_t want;
            in >> hl >> mr >> dt;
            expectKeyword(in, "SNAP");
            const WeightVector start = readWeights(in);
            expectKeyword(in, "TARGET");
            const WeightVector target = readWeights(in);
            expectKeyword(in, "TICKS"); in >> ticks;
            expectKeyword(in, "FINALCOUNT"); in >> want;

            WeightSmoother s = make(hl, mr);
            s.snap(start);
            for (int i = 0; i < ticks; ++i) s.update(target, dt);
            const bool ok = s.current().size() == want;
            if (!ok) d << "expected " << want << " entries after " << ticks
                       << " ticks, got " << s.current().size()
                       << show(s.current());
            record(cls, name, ok, d.str());

        } else if (kind == "SETTLED") {
            float hl, mr, dt; int want;
            in >> hl >> mr >> dt;
            expectKeyword(in, "SNAP");
            const WeightVector start = readWeights(in);
            expectKeyword(in, "TARGET");
            const WeightVector target = readWeights(in);
            expectKeyword(in, "FIRSTSETTLED"); in >> want;

            WeightSmoother s = make(hl, mr);
            s.snap(start);
            int first = -1;
            for (int i = 1; i < 200; ++i) {
                s.update(target, dt);
                if (first < 0 && s.settled(target)) first = i;
            }
            const bool ok = (first == want);
            if (!ok) d << "expected settled() to become true at tick " << want
                       << ", got " << first;
            record(cls, name, ok, d.str());

        } else {
            std::cerr << "unknown record type: " << kind << "\n";
            return 2;
        }
    }

    int pass = 0, fail = 0;
    std::cout << "\n";
    for (const char* cls : {"ANALYTIC", "CROSS", "SPEC"}) {
        const Tally& t = tallies[cls];
        if (t.pass + t.fail == 0) continue;
        std::cout << "  " << std::left << std::setw(10) << cls
                  << t.pass << "/" << (t.pass + t.fail) << "\n";
        pass += t.pass; fail += t.fail;
    }
    if (!failures.empty()) {
        std::cout << "\nfailures:\n";
        for (const auto& s : failures) std::cout << "  " << s << "\n";
    }
    std::cout << "\n" << pass << "/" << (pass + fail)
              << (fail == 0 ? "  GREEN\n" : "  RED\n");
    return fail == 0 ? 0 : 1;
}
