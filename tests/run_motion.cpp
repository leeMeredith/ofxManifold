// ofxManifold -- movement vector runner (round 3B-2).
//
// Reads tests/vectors/motion.vec (from tests/ref/reference_motion.py) and
// checks, against the C++:
//   RNG / UNIT   splitmix64's integers exactly; its units exactly
//   WALK         a random walk asked at counts: position within 1e-9, the
//                number of steps taken exactly; step times; checkpoints kept
//   REC          a recording held or looped, within 1e-6 (the path is float)
//   PHASE / SECS the clocks, within 1e-12, and whether the phase is heard
//   FILE         motion files: loaded, saved byte for byte, walked; or refused
//
// Walks are asked in the order the vectors give, which is not always forward
// in time: "out_of_order" asks far ahead, back, and across checkpoints, and
// must get the answers the reference got walking from the start each time.

#include "../src/sources/ofxManifoldMotion.h"
#include "../src/io/ofxManifoldSerializeMotion.h"
#include "../ofxManifoldPlayer/src/ofxManifoldClocks.h"

#include <cmath>
#include <cstdlib>
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

bool square(glm::dvec2 p) { return 0.2 <= p.x && p.x <= 0.8 && 0.2 <= p.y && p.y <= 0.8; }
bool lshape(glm::dvec2 p) {
    return (0.1 <= p.x && p.x <= 0.9 && 0.1 <= p.y && p.y <= 0.4)
        || (0.1 <= p.x && p.x <= 0.4 && 0.1 <= p.y && p.y <= 0.9);
}

std::vector<double> splitNumbers(const std::string& s) {
    std::vector<double> v;
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')) v.push_back(std::strtod(part.c_str(), nullptr));
    return v;
}

// WALK name CLASS key=value ... -- the settings as reference_motion.py wrote them.
RandomWalk walkFrom(std::istringstream& in) {
    WalkSettings s;
    std::string inside = "none", kv;
    while (in >> kv) {
        const auto eq = kv.find('=');
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        if (k == "seed") s.seed = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "start") { auto n = splitNumbers(v); s.start = glm::dvec3(n[0], n[1], n[2]); }
        else if (k == "travel") {
            s.bySpeed = v.rfind("speed:", 0) == 0;
            s.travel = std::strtod(v.substr(v.find(':') + 1).c_str(), nullptr);
        }
        else if (k == "easing") s.easing = v == "smooth" ? Easing::Smooth : Easing::Linear;
        else if (k == "rect") { auto n = splitNumbers(v); s.x0 = n[0]; s.y0 = n[1]; s.x1 = n[2]; s.y1 = n[3]; }
        else if (k == "z") { auto n = splitNumbers(v); s.zLow = n[0]; s.zHigh = n[1]; }
        else if (k == "step") s.step = std::strtod(v.c_str(), nullptr);
        else if (k == "inside") inside = v;
        else if (k == "straight") s.straight = v == "1";
        else if (k == "clock") {
            const std::string kind = v.substr(0, v.find(','));
            auto n = splitNumbers(v.substr(v.find(',') + 1));
            if (kind == "every") s.steps = StepTiming::every(n[0], n[1]);
            else if (kind == "at") s.steps = StepTiming::clicks(std::vector<double>(n.begin() + 1, n.end()));
            else s.steps = StepTiming::pattern(n[0], std::vector<double>(n.begin() + 2, n.end()));
        }
    }
    RandomWalk w(s);
    if (inside == "square") w.setInside(square);
    if (inside == "lshape") w.setInside(lshape);
    return w;
}

std::string unescape(const std::string& s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { r += '\n'; ++i; }
        else r += s[i];
    }
    return r;
}

std::string fmt(double v) {
    std::ostringstream o;
    o << std::setprecision(17) << v;
    return o.str();
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/vectors/motion.vec";
    std::ifstream file(path);
    if (!file) { std::cerr << "cannot open " << path << "\n"; return 2; }
    std::cout << "ofxManifold movement\nvectors: " << path << "\n\n";

    std::map<std::string, std::pair<int, int>> tally;
    std::string line, kind, name, cls, why;
    bool ok = true;
    RandomWalk walk;
    RecordedMotion rec;
    PhaseClock phase;
    SecondsClock secs;
    double tol = 1e-9;
    bool fileOk = true, loaded = false;
    std::string fileJson, fileUnits, savedWant;

    auto failWith = [&](const std::string& w) { if (ok) { ok = false; why = w; } };
    auto finish = [&]() {
        auto& t = tally[cls];
        (ok ? t.first : t.second)++;
        std::cout << (ok ? "  pass  " : "  FAIL  ") << std::left << std::setw(10) << cls << name << "\n";
        if (!ok) std::cout << "        " << why << "\n";
    };

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kw;
        in >> kw;
        if (kw == "RNG" || kw == "UNIT") {
            std::uint64_t seed = 0;
            in >> name >> cls >> seed;
            ok = true;
            SplitMix64 g(seed);
            std::string want;
            int i = 0;
            while (in >> want) {
                if (kw == "RNG") {
                    const std::uint64_t got = g.next();
                    if (got != std::strtoull(want.c_str(), nullptr, 10))
                        failWith("draw " + std::to_string(i) + ": " + std::to_string(got) + ", want " + want);
                } else {
                    const double got = g.unit();
                    if (got != std::strtod(want.c_str(), nullptr))
                        failWith("unit " + std::to_string(i) + ": " + fmt(got) + ", want " + want);
                }
                ++i;
            }
            finish();
        } else if (kw == "WALK") {
            in >> name >> cls;
            ok = true;
            kind = "walk";
            tol = 1e-9;
            walk = walkFrom(in);
        } else if (kw == "REC") {
            std::string end;
            double dur = 0;
            std::size_t n = 0;
            in >> name >> cls >> end >> dur >> n;
            ok = true;
            kind = "rec";
            tol = 1e-6;
            std::vector<TrajectorySample> samples;
            for (std::size_t i = 0; i < n; ++i) {
                std::string s;
                in >> s;
                auto v = splitNumbers(s);
                TrajectorySample ts;
                ts.t = float(v[0]);
                ts.position = glm::vec2(float(v[1]), float(v[2]));
                ts.z = float(v[3]);
                samples.push_back(ts);
            }
            Trajectory tr;
            tr.setSamples(samples, float(dur));
            rec = RecordedMotion(tr, end == "loop" ? RecordedMotion::End::Loop
                                                   : RecordedMotion::End::Hold);
        } else if (kw == "ASK") {
            double t, x, y, z;
            std::size_t k;
            in >> t >> x >> y >> z >> k;
            glm::dvec3 got;
            std::size_t gotK = 0;
            if (kind == "rec") {
                got = glm::dvec3(rec.at(t));
            } else {
                if (kind == "file" && !loaded) continue;
                got = walk.atPrecise(t);
                gotK = walk.stepsBy(t);
            }
            if (std::fabs(got.x - x) > tol || std::fabs(got.y - y) > tol || std::fabs(got.z - z) > tol)
                failWith("at " + fmt(t) + ": " + fmt(got.x) + " " + fmt(got.y) + " " + fmt(got.z)
                         + " -- want " + fmt(x) + " " + fmt(y) + " " + fmt(z));
            if (kind != "rec" && gotK != k)
                failWith("at " + fmt(t) + ": " + std::to_string(gotK) + " steps, want " + std::to_string(k));
        } else if (kw == "STEP") {
            std::size_t k;
            double t;
            in >> k >> t;
            const double got = walk.stepTime(k);
            if (std::fabs(got - t) > 1e-12)
                failWith("step " + std::to_string(k) + " at " + fmt(got) + ", want " + fmt(t));
        } else if (kw == "CLICK") {
            double t;
            int took;
            in >> t >> took;
            if (int(walk.addStep(t)) != took)
                failWith("a click at " + fmt(t) + (took ? " refused" : " taken"));
        } else if (kw == "CHECKPOINTS") {
            std::size_t n;
            in >> n;
            if (walk.checkpoints() != n)
                failWith(std::to_string(walk.checkpoints()) + " checkpoints, want " + std::to_string(n));
        } else if (kw == "PHASE") {
            in >> name >> cls;
            ok = true;
            kind = "phase";
            phase = PhaseClock();
        } else if (kw == "SECS") {
            in >> name >> cls;
            ok = true;
            kind = "secs";
            secs = SecondsClock();
        } else if (kw == "FEED") {
            double now, ph;
            in >> now >> ph;
            int bar = PhaseClock::kNoBar;
            if (!(in >> bar)) bar = PhaseClock::kNoBar;
            phase.feed(ph, now, bar);
        } else if (kw == "PAUSE" || kw == "RESUME") {
            double now;
            in >> now;
            if (kw == "PAUSE") secs.pause(now);
            else secs.resume(now);
        } else if (kw == "COUNT") {
            double now, want;
            int heard;
            in >> now >> want >> heard;
            const double got = kind == "phase" ? phase.count() : secs.count(now);
            const int gotHeard = kind == "phase" ? int(phase.heard(now)) : 1;
            if (std::fabs(got - want) > 1e-12 || gotHeard != heard)
                failWith("at " + fmt(now) + ": count " + fmt(got) + " heard " + std::to_string(gotHeard)
                         + " -- want " + fmt(want) + " heard " + std::to_string(heard));
        } else if (kw == "FILE") {
            std::string verdict;
            in >> name >> cls >> verdict >> fileUnits;
            ok = true;
            kind = "file";
            fileOk = verdict == "ok";
            loaded = false;
            savedWant.clear();
        } else if (kw == "JSON") {
            fileJson = unescape(line.substr(5));
            std::string units;
            RandomWalk w;
            const io::LoadResult r = io::loadMotion(fileJson, w, &units);
            if (!fileOk) {
                if (r.ok) failWith("loaded; should be refused");
                continue;
            }
            if (!r.ok) { failWith("refused: " + r.error); continue; }
            if (units != fileUnits) failWith("units " + units + ", want " + fileUnits);
            walk = w;
            if (walk.settings().insideMap) walk.setInside(lshape);
            loaded = true;
            savedWant = fileJson;          // round trip, unless SAVED says otherwise
        } else if (kw == "SAVED") {
            savedWant = unescape(line.substr(6));
        } else if (kw == "END") {
            if (kind == "file" && loaded) {
                const std::string saved = io::saveMotion(walk, fileUnits);
                if (saved != savedWant) failWith("saved:\n" + saved + "        want:\n" + savedWant);
            }
            finish();
        }
    }

    int pass = 0, fail = 0;
    std::cout << "\n";
    for (const auto& t : tally) {
        std::cout << "  " << std::left << std::setw(9) << t.first << " " << t.second.first << "/"
                  << (t.second.first + t.second.second) << "\n";
        pass += t.second.first;
        fail += t.second.second;
    }
    std::cout << "\n" << pass << "/" << (pass + fail) << (fail ? "  RED" : "  GREEN") << "\n";
    return fail ? 1 : 0;
}
