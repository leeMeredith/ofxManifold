// ofxManifold — serialization conformance vector runner.
//
// Fixtures live in tests/fixtures/ and are read by BOTH implementations: the
// C++ parser in src/io, and Python's json module via
// tests/ref/reference_serialize.py. That reference is an independent
// implementation of the format written by neither of us, which makes this the
// strongest cross-check in the project.

#include "../src/io/ofxManifoldSerialize.h"

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

} // namespace

int main(int argc, char** argv) {
    const std::string path =
        (argc > 1) ? argv[1] : "tests/vectors/serialize.vec";
    if (argc > 2) fixtureDir = argv[2];

    std::ifstream vf(path);
    if (!vf) { std::cerr << "cannot open vector file: " << path << "\n"; return 2; }

    std::cout << "ofxManifold serialization\nvectors: " << path << "\n\n";

    std::string line;
    while (std::getline(vf, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string kind; in >> kind;
        if (kind == "TOL") { in >> tolerance; continue; }

        std::string name, cls; in >> name >> cls;
        std::ostringstream d;

        if (kind == "MANIFOLD_LOAD") {
            std::string file, expect; in >> file >> expect;
            // Underscores stand in for spaces so the substring survives a
            // whitespace-delimited vector file.
            std::string wantErr; in >> wantErr;
            for (char& ch : wantErr) if (ch == '_') ch = ' ';
            std::string text;
            if (!slurp(file, text)) {
                record(cls, name, false, "missing fixture: " + file);
                continue;
            }
            Manifold2D m;
            const io::LoadResult r = io::loadManifold(text, m);
            const bool wantOk = (expect == "OK");
            bool ok = (r.ok == wantOk);
            if (!ok) {
                if (wantOk) d << "expected the file to load, it failed: "
                              << r.error;
                else        d << "expected the file to be REFUSED, it loaded."
                                 "\n      A loader that accepts a malformed"
                                 " file hands back a manifold that is wrong"
                                 " rather than absent, which is worse than"
                                 " failing.";
            }
            // Right verdict, wrong reason is still a bug: it is the message
            // the person debugging actually reads.
            if (ok && !wantOk && !wantErr.empty()
                && r.error.find(wantErr) == std::string::npos) {
                ok = false;
                d << "refused, but for the wrong reason"
                  << "\n      expected the error to mention: " << wantErr
                  << "\n      actual error: " << r.error;
            }
            record(cls, name, ok, d.str());

        } else if (kind == "COUNTS") {
            std::string file; std::size_t wantNodes, wantRegions;
            in >> file >> wantNodes >> wantRegions;
            std::string text; slurp(file, text);
            Manifold2D m;
            const io::LoadResult r = io::loadManifold(text, m);
            bool ok = r.ok && m.nodeCount() == wantNodes
                           && m.regionCount() == wantRegions;
            if (!ok) d << "expected " << wantNodes << " nodes and "
                       << wantRegions << " regions, got " << m.nodeCount()
                       << " and " << m.regionCount()
                       << (r.ok ? "" : (" (load failed: " + r.error + ")"));
            record(cls, name, ok, d.str());

        } else if (kind == "EVAL") {
            std::string file; float px, py;
            in >> file >> px >> py;
            std::vector<std::pair<std::string, float>> want;
            std::string tok;
            while (in >> tok) {
                const std::size_t eq = tok.find('=');
                if (eq == std::string::npos) continue;
                want.emplace_back(tok.substr(0, eq),
                                  std::stof(tok.substr(eq + 1)));
            }
            std::string text; slurp(file, text);
            Manifold2D m;
            const io::LoadResult r = io::loadManifold(text, m);
            if (!r.ok) { record(cls, name, false, "load failed: " + r.error); continue; }

            const Evaluation e = m.evaluate({px, py});
            bool ok = e.inside && e.weights.size() == want.size();
            if (!ok) {
                d << "expected " << want.size() << " weighted nodes, got "
                  << (e.inside ? e.weights.size() : 0)
                  << (e.inside ? "" : " (point reported outside)");
            } else {
                for (const auto& wnt : want) {
                    const NodeID id = m.findNode(wnt.first);
                    bool found = false;
                    for (const auto& g : e.weights) {
                        if (g.id != id) continue;
                        found = true;
                        if (!close(g.weight, wnt.second)) {
                            ok = false;
                            d << "\n      node " << wnt.first << ": expected "
                              << std::fixed << std::setprecision(9)
                              << wnt.second << ", got " << g.weight;
                        }
                        break;
                    }
                    if (!found) {
                        ok = false;
                        d << "\n      node " << wnt.first << " missing";
                    }
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "ROUNDTRIP") {
            std::string file; in >> file;
            std::string text; slurp(file, text);
            Manifold2D m1;
            io::LoadResult r = io::loadManifold(text, m1);
            if (!r.ok) { record(cls, name, false, "load failed: " + r.error); continue; }

            const std::string s1 = io::saveManifold(m1);
            Manifold2D m2;
            r = io::loadManifold(s1, m2);
            if (!r.ok) {
                record(cls, name, false,
                       "our own output failed to load: " + r.error);
                continue;
            }
            const std::string s2 = io::saveManifold(m2);

            bool ok = (s1 == s2);
            if (!ok) d << "save -> load -> save is not byte stable";

            // Stronger than byte stability: the reloaded manifold must
            // evaluate BIT-identically, which is what nine significant digits
            // on a 32-bit float buys.
            if (ok) {
                const glm::vec2 probes[] = {{0.45f, 0.40f}, {0.5f, 0.5f},
                                            {0.62f, 0.62f}, {0.3f, 0.3f}};
                for (const glm::vec2& p : probes) {
                    const Evaluation a = m1.evaluate(p);
                    const Evaluation b = m2.evaluate(p);
                    if (a.inside != b.inside
                        || a.weights.size() != b.weights.size()) {
                        ok = false;
                        d << "\n      reloaded manifold evaluates differently"
                             " at (" << p.x << ", " << p.y << ")";
                        break;
                    }
                    for (std::size_t i = 0; i < a.weights.size(); ++i) {
                        if (a.weights[i].id != b.weights[i].id
                            || a.weights[i].weight != b.weights[i].weight) {
                            ok = false;
                            d << "\n      reloaded weights differ at ("
                              << p.x << ", " << p.y << ")";
                            break;
                        }
                    }
                    if (!ok) break;
                }
            }
            record(cls, name, ok, d.str());

        } else if (kind == "MAPPING_LOAD" || kind == "MAPPING_TARGETS"
                   || kind == "MAPPING_ROUNDTRIP"
                   || kind == "MAPPING_NAMES") {
            std::string mfile, pfile; in >> mfile >> pfile;
            std::string mtext, ptext;
            if (!slurp(mfile, mtext) || !slurp(pfile, ptext)) {
                record(cls, name, false, "missing fixture");
                continue;
            }
            Manifold2D m;
            if (!io::loadManifold(mtext, m).ok) {
                record(cls, name, false, "manifold fixture failed to load");
                continue;
            }
            Mapping mp;
            const io::LoadResult r = io::loadMapping(ptext, m, mp);

            if (kind == "MAPPING_LOAD") {
                std::string expect; in >> expect;
                std::string wantErr; in >> wantErr;
                for (char& ch : wantErr) if (ch == '_') ch = ' ';
                const bool wantOk = (expect == "OK");
                bool ok = (r.ok == wantOk);
                if (!ok) d << (wantOk ? "expected load, failed: " + r.error
                                      : "expected refusal, it loaded");
                if (ok && !wantOk && !wantErr.empty()
                    && r.error.find(wantErr) == std::string::npos) {
                    ok = false;
                    d << "refused, but for the wrong reason"
                      << "\n      expected: " << wantErr
                      << "\n      actual:   " << r.error;
                }
                record(cls, name, ok, d.str());

            } else if (kind == "MAPPING_NAMES") {
                // Survives a save/load cycle, so this checks the WRITER as
                // well as the reader.
                const std::string s1 = io::saveMapping(mp, m);
                Mapping mp2;
                const io::LoadResult r2 = io::loadMapping(s1, m, mp2);
                std::vector<std::string> want;
                std::string t;
                while (in >> t) want.push_back(t);
                bool ok = r2.ok && mp2.targetCount() == want.size();
                if (!ok) {
                    d << "expected " << want.size() << " targets after a round"
                         " trip, got " << mp2.targetCount();
                } else {
                    for (std::size_t i = 0; i < want.size(); ++i) {
                        if (mp2.targetName(static_cast<TargetID>(i))
                            != want[i]) {
                            ok = false;
                            d << "\n      target " << i << ": expected \""
                              << want[i] << "\", got \""
                              << mp2.targetName(static_cast<TargetID>(i))
                              << "\"";
                        }
                    }
                }
                record(cls, name, ok, d.str());

            } else if (kind == "MAPPING_TARGETS") {
                std::size_t want; in >> want;
                const bool ok = r.ok && mp.targetCount() == want;
                if (!ok) d << "expected " << want << " targets, got "
                           << mp.targetCount();
                record(cls, name, ok, d.str());

            } else {
                if (!r.ok) { record(cls, name, false, "load failed: " + r.error); continue; }
                const std::string s1 = io::saveMapping(mp, m);
                Mapping mp2;
                const io::LoadResult r2 = io::loadMapping(s1, m, mp2);
                bool ok = r2.ok;
                if (!ok) d << "our own mapping output failed to load: "
                           << r2.error;
                else {
                    const std::string s2 = io::saveMapping(mp2, m);
                    ok = (s1 == s2);
                    if (!ok) d << "mapping save -> load -> save is not byte"
                                  " stable";
                }
                record(cls, name, ok, d.str());
            }

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
