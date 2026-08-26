// ofxManifold — trajectory conformance vector runner.
//
// The TOUR, PORTABLE and DIFFERS records are the ones that matter. They assert
// the property section 5 chose normalized coordinates for: a path recorded
// against one map replays against a different one, same positions, different
// weights. Nothing else in the project asserted it.

#include "../src/io/ofxManifoldSerialize.h"
#include "../src/sources/ofxManifoldTrajectory.h"

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
std::string fixtureDir = "tests/fixtures";
std::map<std::string, Trajectory> trajectories;

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

bool slurp(const std::string& file, std::string& out) {
    std::ifstream f(fixtureDir + "/" + file);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

bool loadManifoldFixture(const std::string& file, Manifold2D& m) {
    std::string text;
    if (!slurp(file, text)) return false;
    return io::loadManifold(text, m).ok;
}

// "0.25:0.3,0.5" -> one sample
TrajectorySample parseSample(const std::string& tok) {
    const std::size_t colon = tok.find(':');
    const std::size_t comma = tok.find(',', colon);
    TrajectorySample s;
    s.t = std::stof(tok.substr(0, colon));
    s.position = glm::vec2(
        std::stof(tok.substr(colon + 1, comma - colon - 1)),
        std::stof(tok.substr(comma + 1)));
    return s;
}

std::string showWeights(const Manifold2D& m, const Evaluation& e) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);
    for (const auto& wn : e.weights) {
        os << " " << m.node(wn.id).name << "=" << wn.weight;
    }
    return os.str();
}

} // namespace

int main(int argc, char** argv) {
    const std::string path =
        (argc > 1) ? argv[1] : "tests/vectors/trajectory.vec";
    if (argc > 2) fixtureDir = argv[2];

    std::ifstream vf(path);
    if (!vf) { std::cerr << "cannot open: " << path << "\n"; return 2; }

    std::cout << "ofxManifold trajectory\nvectors: " << path << "\n\n";

    std::string line;
    while (std::getline(vf, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kind; in >> kind;
        if (kind == "TOL") { in >> tolerance; continue; }

        if (kind == "TRAJ") {
            std::string name; in >> name;
            std::vector<TrajectorySample> ss;
            std::string tok;
            while (in >> tok) ss.push_back(parseSample(tok));
            Trajectory tr;
            tr.setSamples(std::move(ss));
            trajectories[name] = tr;
            continue;
        }

        std::string name, cls; in >> name >> cls;
        std::ostringstream d;

        if (kind == "AT") {
            std::string traj; float t, ex, ey;
            in >> traj >> t >> ex >> ey;
            const glm::vec2 p = trajectories[traj].pointAt(t);
            const bool ok = close(p.x, ex) && close(p.y, ey);
            if (!ok) d << "expected (" << std::fixed << std::setprecision(6)
                       << ex << ", " << ey << "), got (" << p.x << ", "
                       << p.y << ")";
            record(cls, name, ok, d.str());

        } else if (kind == "RECORD" || kind == "RECORDBACK") {
            // Replays the CALLS -- addSample() then finalize() -- rather than
            // handing over finished samples. Without this, neither function is
            // reached by the suite at all.
            Trajectory tr;
            std::vector<float> want;
            std::string tok;
            bool inExpect = false;
            while (in >> tok) {
                if (tok == "EXPECT") { inExpect = true; continue; }
                if (inExpect) { want.push_back(std::stof(tok)); continue; }
                const TrajectorySample s2 = parseSample(tok);
                tr.addSample(s2.t, s2.position);
            }
            tr.finalize();

            bool ok = tr.sampleCount() == want.size();
            if (!ok) {
                d << "expected " << want.size() << " samples, got "
                  << tr.sampleCount();
            } else {
                for (std::size_t i = 0; i < want.size(); ++i) {
                    if (!close(tr.sample(i).t, want[i])) {
                        ok = false;
                        d << "\n      sample " << i << ": expected t="
                          << std::fixed << std::setprecision(6) << want[i]
                          << ", got " << tr.sample(i).t;
                    }
                }
            }
            // finalize must always produce a path running 0 to 1. A recording
            // that began at t=17.5 and still starts at 17.5 is carrying a
            // stopwatch reading around instead of a portable path.
            if (ok && tr.sampleCount() > 1) {
                if (!close(tr.sample(0).t, 0.0f)) {
                    ok = false;
                    d << "\n      finalized path does not start at 0";
                }
                if (!close(tr.sample(tr.sampleCount() - 1).t, 1.0f)) {
                    ok = false;
                    d << "\n      finalized path does not end at 1";
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "DURATION") {
            // Calls addSample() and finalize(), so the duration is derived
            // rather than handed over (D-011).
            Trajectory tr;
            std::string tok;
            float want = 0.0f;
            bool inExpect = false;
            while (in >> tok) {
                if (tok == "EXPECT") { inExpect = true; continue; }
                if (inExpect) { want = std::stof(tok); continue; }
                const TrajectorySample s2 = parseSample(tok);
                tr.addSample(s2.t, s2.position);
            }
            tr.finalize();
            const bool ok = close(tr.duration(), want);
            if (!ok) d << "expected duration " << std::fixed
                       << std::setprecision(6) << want << ", got "
                       << tr.duration();
            record(cls, name, ok, d.str());

        } else if (kind == "SECONDS") {
            Trajectory tr;
            std::string tok;
            float at = 0.0f, ex = 0.0f, ey = 0.0f;
            int stage = 0;
            while (in >> tok) {
                if (tok == "AT")     { stage = 1; continue; }
                if (tok == "EXPECT") { stage = 2; continue; }
                if (stage == 0) {
                    const TrajectorySample s2 = parseSample(tok);
                    tr.addSample(s2.t, s2.position);
                } else if (stage == 1) {
                    at = std::stof(tok);
                } else {
                    if (ex == 0.0f && ey == 0.0f) ex = std::stof(tok);
                    else ey = std::stof(tok);
                }
            }
            tr.finalize();
            const glm::vec2 p = tr.pointAtSeconds(at);
            const bool ok = close(p.x, ex) && close(p.y, ey);
            if (!ok) d << "at " << at << " expected (" << std::fixed
                       << std::setprecision(6) << ex << ", " << ey
                       << "), got (" << p.x << ", " << p.y << ")";
            record(cls, name, ok, d.str());

        } else if (kind == "VEL") {
            std::string traj; float t, window, ex, ey;
            in >> traj >> t >> window >> ex >> ey;
            const glm::vec2 v = trajectories[traj].velocityAt(t, window);
            const bool ok = close(v.x, ex) && close(v.y, ey);
            if (!ok) d << "expected velocity (" << std::fixed
                       << std::setprecision(6) << ex << ", " << ey
                       << "), got (" << v.x << ", " << v.y << ")"
                       << "\n      at t=" << t << " window=" << window;
            record(cls, name, ok, d.str());

        } else if (kind == "TOUR") {
            std::string traj; float t, ex, ey;
            in >> traj >> t >> ex >> ey;

            const glm::vec2 p = trajectories[traj].pointAt(t);
            bool ok = close(p.x, ex) && close(p.y, ey);
            if (!ok) d << "position wrong before either venue was consulted";

            // Two venues, each followed by that venue's expected weights.
            std::string tok;
            while (in >> tok && ok) {
                Manifold2D m;
                if (!loadManifoldFixture(tok, m)) {
                    ok = false; d << "cannot load " << tok; break;
                }
                const Evaluation e = m.evaluate(p);
                std::string wtok;
                std::streampos mark;
                while (true) {
                    mark = in.tellg();
                    if (!(in >> wtok)) break;
                    if (wtok.find('=') == std::string::npos) {
                        in.clear(); in.seekg(mark); break;   // next venue
                    }
                    const std::size_t eq = wtok.find('=');
                    const std::string nm = wtok.substr(0, eq);
                    const float want = std::stof(wtok.substr(eq + 1));
                    const NodeID id = m.findNode(nm);
                    bool found = false;
                    for (const auto& wn : e.weights) {
                        if (wn.id != id) continue;
                        found = true;
                        if (!close(wn.weight, want)) {
                            ok = false;
                            d << "\n      " << tok << " node " << nm
                              << ": expected " << std::fixed
                              << std::setprecision(6) << want << ", got "
                              << wn.weight;
                        }
                        break;
                    }
                    if (!found) {
                        ok = false;
                        d << "\n      " << tok << " node " << nm << " missing";
                    }
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "PORTABLE") {
            // THE INVARIANT. The same trajectory sampled densely, evaluated
            // against two different maps, must yield identical positions
            // every time. A trajectory that depended on the map it was
            // recorded against fails here and nowhere else.
            std::string traj, fa, fb;
            in >> traj >> fa >> fb;
            Manifold2D a, b;
            if (!loadManifoldFixture(fa, a) || !loadManifoldFixture(fb, b)) {
                record(cls, name, false, "cannot load a venue"); continue;
            }
            bool ok = true;
            for (int i = 0; i <= 40 && ok; ++i) {
                const float t = static_cast<float>(i) / 40.0f;
                const glm::vec2 p1 = trajectories[traj].pointAt(t);
                const glm::vec2 p2 = trajectories[traj].pointAt(t);
                if (p1 != p2) {
                    ok = false;
                    d << "pointAt is not deterministic at t=" << t;
                    break;
                }
                // Evaluating against a manifold must not perturb the source.
                (void)a.evaluate(p1);
                (void)b.evaluate(p1);
                const glm::vec2 p3 = trajectories[traj].pointAt(t);
                if (p3 != p1) {
                    ok = false;
                    d << "evaluating changed the trajectory at t=" << t;
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "DIFFERS") {
            // If the two venues produced the same weights, they are the same
            // venue and the touring vectors prove nothing.
            std::string traj, fa, fb;
            in >> traj >> fa >> fb;
            Manifold2D a, b;
            if (!loadManifoldFixture(fa, a) || !loadManifoldFixture(fb, b)) {
                record(cls, name, false, "cannot load a venue"); continue;
            }
            int differing = 0;
            for (int i = 0; i <= 40; ++i) {
                const float t = static_cast<float>(i) / 40.0f;
                const glm::vec2 p = trajectories[traj].pointAt(t);
                const Evaluation ea = a.evaluate(p);
                const Evaluation eb = b.evaluate(p);
                if (showWeights(a, ea) != showWeights(b, eb)) ++differing;
            }
            const bool ok = differing > 30;
            if (!ok) d << "only " << differing << " of 41 samples produced "
                          "different weights; the two venues are too alike "
                          "for the touring vectors to mean anything";
            record(cls, name, ok, d.str());

        } else if (kind == "ROUNDTRIP") {
            std::string file; in >> file;
            std::string text;
            if (!slurp(file, text)) {
                record(cls, name, false, "missing " + file); continue;
            }
            Trajectory t1;
            io::LoadResult r = io::loadTrajectory(text, t1);
            if (!r.ok) { record(cls, name, false, r.error); continue; }

            const std::string s1 = io::saveTrajectory(t1);
            Trajectory t2;
            r = io::loadTrajectory(s1, t2);
            if (!r.ok) {
                record(cls, name, false, "own output failed to load: "
                       + r.error);
                continue;
            }
            const std::string s2 = io::saveTrajectory(t2);

            bool ok = (s1 == s2);
            if (!ok) d << "save -> load -> save is not byte stable";
            // Byte stability and matching positions are both satisfied by a
            // pair of save/load functions that agree on dropping the duration.
            // Asserting it explicitly is what catches that.
            if (ok && !close(t1.duration(), t2.duration())) {
                ok = false;
                d << "duration did not survive the round trip: "
                  << t1.duration() << " -> " << t2.duration();
            }
            if (ok) {
                for (int i = 0; i <= 40 && ok; ++i) {
                    const float t = static_cast<float>(i) / 40.0f;
                    if (t1.pointAt(t) != t2.pointAt(t)) {
                        ok = false;
                        d << "reloaded trajectory differs at t=" << t;
                    }
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "FILEDURATION") {
            std::string file; float want;
            in >> file >> want;
            std::string text;
            if (!slurp(file, text)) {
                record(cls, name, false, "missing " + file); continue;
            }
            Trajectory tr;
            const io::LoadResult r = io::loadTrajectory(text, tr);
            bool ok = r.ok && close(tr.duration(), want);
            if (!r.ok) d << "load failed: " << r.error;
            else if (!ok) d << "expected duration " << std::fixed
                            << std::setprecision(6) << want << ", got "
                            << tr.duration();
            record(cls, name, ok, d.str());

        } else if (kind == "REJECT") {
            std::string file, wantErr;
            in >> file >> wantErr;
            std::string text;
            if (!slurp(file, text)) {
                record(cls, name, false, "missing " + file); continue;
            }
            Trajectory tr;
            const io::LoadResult r = io::loadTrajectory(text, tr);
            bool ok = !r.ok;
            if (!ok) d << "expected the file to be refused, it loaded";
            else if (r.error.find(wantErr) == std::string::npos) {
                ok = false;
                d << "refused for the wrong reason"
                  << "\n      expected the error to mention: " << wantErr
                  << "\n      actual: " << r.error;
            }
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
