// Round 3B-2f: points moved from outside -- ofxManifoldPlayer's PointControl
// and its control file, checked against tests/vectors/control.vec (from
// tests/ref/reference_control.py).
//
//   FILE name points OK|REFUSED reason   loadControl, for a player of so many points
//   ROUND name EXPECT-FILE file          read, written again: exactly that file
//   SCRIPT ... SPEC file PLAN p PREFIX p EXTENT zLow zHigh uTop
//   RECV t ip port address args OURS 0|1 EXPECT <result>
//   TICK t EXPECT <result>
//   HAND n EXPECT <result>
//   ADD|REMOVE 0|1 EXPECT <result>   3B-2g: a point added after the last, the last removed
//   END
//
// <result>: the moves, events and replies since the last, and the counts --
// compared word by word, numbers to within 1e-5.

#include "../ofxManifoldPlayer/src/ofxManifoldControlFile.h"
#include "../ofxManifoldPlayer/src/ofxManifoldPointControl.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxManifold::player;

namespace {

const std::string kFixtures = "tests/fixtures/control/";

bool slurp(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::istringstream in(s);
    for (std::string t; in >> t;) w.push_back(t);
    return w;
}

bool number(const std::string& s, double& v) {
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end && *end == '\0' && !s.empty();
}

// Word by word; two words that both read as numbers match within 1e-5 --
// "t:v" words compare their types exactly and their values so.
bool same(const std::string& want, const std::string& got) {
    const auto a = words(want), b = words(got);
    if (a.size() != b.size()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (a[k] == b[k]) continue;
        std::string x = a[k], y = b[k];
        if (x.size() > 2 && y.size() > 2 && x[1] == ':' && y[1] == ':') {
            if (x[0] != y[0]) return false;
            x = x.substr(2);
            y = y.substr(2);
        }
        double p, q;
        if (!number(x, p) || !number(y, q)) return false;
        if (std::isnan(p) && std::isnan(q)) continue;
        if (std::fabs(p - q) > 1e-5 * std::max(1.0, std::fabs(p))) return false;
    }
    return true;
}

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", double(v));
    return buf;
}

std::string spaced(std::string s) {
    for (char& c : s) if (c == ' ') c = '_';
    return s;
}

std::string result(PointControl& c) {
    std::string o = "MOVES";
    for (const PointMove& m : c.takeMoves()) {
        o += " " + std::to_string(m.point);
        if (m.hasXY) o += " xy " + num(m.x) + " " + num(m.y);
        if (m.hasZ) o += " z " + num(m.z);
        if (m.hasU) o += " u " + num(m.u);
        o += " ;";
    }
    o += " EVENTS";
    for (const PointEvent& e : c.takeEvents()) {
        o += std::string(" ") + (e.kind == PointEvent::Taken ? "Taken" : "Returned") + " "
           + std::to_string(e.point) + " " + (e.why.empty() ? "-" : e.why) + " " + e.by.str()
           + " ;";
    }
    o += " REPLIES";
    for (const PointReply& r : c.takeReplies()) {
        o += " " + r.to.str() + " " + r.message.address;
        for (const OscArg& a : r.message.args) {
            if (a.type == 'i') o += " i:" + std::to_string(a.i);
            else if (a.type == 'f') o += " f:" + num(a.f);
            else o += " s:" + spaced(a.s);
        }
        o += " ;";
    }
    o += " ACCEPTED " + std::to_string(c.accepted()) + " REFUSED "
       + std::to_string(c.refused()) + " LAST "
       + (c.lastRefusal().empty() ? "-" : spaced(c.lastRefusal()));
    return o;
}

OscArg arg(const std::string& w) {
    const std::string v = w.substr(2);
    if (w[0] == 'i') return OscArg::integer(std::int32_t(std::stol(v)));
    if (w[0] == 'f') return OscArg::number(std::strtof(v.c_str(), nullptr));
    return OscArg::text(v);
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "tests/vectors/control.vec";
    std::ifstream in(path);
    if (!in) { std::cerr << "cannot open " << path << "\n"; return 2; }

    int total = 0, failed = 0;
    PointControl control;
    std::string script;
    auto check = [&](bool ok, const std::string& what, const std::string& why) {
        ++total;
        if (!ok) {
            ++failed;
            std::cout << "  FAIL  " << what << "\n        " << why << "\n";
        }
    };

    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        const auto w = words(line);
        const std::string& kind = w[0];

        if (kind == "FILE") {
            std::string text;
            const bool read = slurp(kFixtures + w[1], text);
            ControlSpec spec;
            const auto r = loadControl(text, spec, std::stoi(w[2]));
            std::string reason;
            for (std::size_t k = 4; k < w.size(); ++k) reason += (k > 4 ? " " : "") + w[k];
            const bool ok = read && (w[3] == "OK" ? r.ok
                                    : !r.ok && r.error.compare(0, reason.size(), reason) == 0);
            check(ok, "FILE " + w[1], r.ok ? "loaded" : "refused: " + r.error);
        } else if (kind == "ROUND") {
            std::string text, want;
            ControlSpec spec;
            const bool read = slurp(kFixtures + w[1], text) && slurp(kFixtures + w[3], want);
            const auto r = loadControl(text, spec);
            const std::string got = r.ok ? saveControl(spec) : "";
            check(read && r.ok && got == want, "ROUND " + w[1], "wrote:\n" + got);
        } else if (kind == "SCRIPT") {
            // SCRIPT name SPEC f PLAN p PREFIX p EXTENT a b c
            script = w[1];
            ControlSpec spec;
            if (w[3] != "-") {
                std::string text;
                slurp(kFixtures + w[3], text);
                const auto r = loadControl(text, spec);
                if (!r.ok) { check(false, "SCRIPT " + script, "its file: " + r.error); continue; }
            }
            control = PointControl();
            control.setSpec(spec);
            control.setPrefix(w[7]);
            if (w[5] == "square") {
                control.setInside([](float x, float y) {
                    return x >= 0.2f && x <= 0.8f && y >= 0.2f && y <= 0.8f;
                });
            }
            control.setExtent(std::strtof(w[9].c_str(), nullptr),
                              std::strtof(w[10].c_str(), nullptr),
                              std::strtof(w[11].c_str(), nullptr));
        } else if (kind == "RECV" || kind == "TICK" || kind == "HAND"
                   || kind == "ADD" || kind == "REMOVE") {
            const std::size_t e = line.find(" EXPECT ");
            const std::string want = line.substr(e + 8);
            std::string what = script + ": " + line.substr(0, e);
            bool ours = true, wantOurs = true;
            if (kind == "RECV") {
                // RECV t ip port address args... OURS n
                const double now = std::strtof(w[1].c_str(), nullptr);
                OscMessage m;
                m.address = w[4];
                std::size_t k = 5;
                for (; k < w.size() && w[k] != "OURS"; ++k) {
                    if (w[k] != "-") m.args.push_back(arg(w[k]));
                }
                wantOurs = w[k + 1] == "1";
                ours = control.receive(now, m, OscPeer{w[2], std::stoi(w[3])});
            } else if (kind == "TICK") {
                control.tick(std::strtof(w[1].c_str(), nullptr));
            } else if (kind == "ADD" || kind == "REMOVE") {
                // 3B-2g: ADD|REMOVE ok -- whether it was done
                const bool done = kind == "ADD" ? control.addPoint() : control.removeLast();
                ours = done;
                wantOurs = w[1] == "1";
            } else {
                control.handTook(std::stoi(w[1]));
            }
            const std::string got = result(control);
            check(ours == wantOurs && same(want, got), what,
                  (ours != wantOurs ? std::string("ours? ") : std::string()) + "want " + want
                  + "\n        got  " + got);
        }
    }
    std::cout << failed << " failed, " << total - failed << "/" << total
              << (failed ? "  RED" : "  GREEN") << "\n";
    return failed ? 1 : 0;
}
