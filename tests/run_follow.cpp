// Round 3B-2i: following, and keeping apart -- ofxManifoldPlayer's
// ofxManifoldFollow.h, checked against tests/vectors/follow.vec (from
// tests/ref/reference_follow.py).
//
//   APART name fx fy fu tx ty tu gap | ox oy ou ; ... EXPECT t
//   GLIDE name ax ay au bx by bu since travel EXPECT x y u
//   SCRIPT name MAP all | x,y x,y ...
//   STEP gap FOLLOWS flags | x y u ; ... EXPECT n:x,y,u:held ... | -
//   END
//
// Numbers to within 1e-5.

#include "../ofxManifoldPlayer/src/ofxManifoldFollow.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ofxManifold::player;

namespace {

bool near(double a, double b) { return std::fabs(a - b) <= 1e-5 * std::max(1.0, std::fabs(b)); }

std::vector<std::string> split(const std::string& s, char by) {
    std::vector<std::string> out;
    std::string part;
    std::stringstream ss(s);
    while (std::getline(ss, part, by)) out.push_back(part);
    return out;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

Place readPlace(const std::string& s) {
    std::istringstream in(s);
    Place p;
    in >> p.x >> p.y >> p.u;
    return p;
}

// The places between "|" and " EXPECT", ";"-separated; none if empty.
std::vector<Place> readPlaces(const std::string& s) {
    std::vector<Place> out;
    if (trim(s).empty()) return out;
    for (const std::string& part : split(s, ';')) out.push_back(readPlace(part));
    return out;
}

// Inside a polygon, by counting crossings; an empty one is everywhere.
bool insidePolygon(const std::vector<std::pair<float, float>>& poly, float x, float y) {
    if (poly.empty()) return true;
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const float xi = poly[i].first, yi = poly[i].second;
        const float xj = poly[j].first, yj = poly[j].second;
        if ((yi > y) != (yj > y) && x < xi + (y - yi) * (xj - xi) / (yj - yi)) in = !in;
    }
    return in;
}

} // namespace

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "tests/vectors/follow.vec";
    std::ifstream in(path);
    if (!in) { std::printf("cannot open %s\n", path); return 1; }
    int pass = 0, fail = 0;
    std::string line, script;
    FollowChain chain;
    std::vector<std::pair<float, float>> poly;
    auto report = [&](const std::string& name, bool ok, const std::string& why) {
        if (ok) { ++pass; return; }
        ++fail;
        std::printf("  FAIL  %s -- %s\n", name.c_str(), why.c_str());
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("APART ", 0) == 0) {
            const std::size_t bar = line.find(" | "), ex = line.find(" EXPECT ");
            std::istringstream head(line.substr(6, bar - 6));
            std::string name;
            Place from, to;
            float gap = 0.0f;
            head >> name >> from.x >> from.y >> from.u >> to.x >> to.y >> to.u >> gap;
            const std::vector<Place> others = readPlaces(line.substr(bar + 3, ex - bar - 3));
            const double want = std::stod(line.substr(ex + 8));
            const float got = keepApart(from, to, others, gap);
            report(name, near(got, want), "t " + std::to_string(got) + ", want " + std::to_string(want));
        } else if (line.rfind("GLIDE ", 0) == 0) {
            const std::size_t ex = line.find(" EXPECT ");
            std::istringstream head(line.substr(6, ex - 6));
            std::string name;
            Place a, b;
            double since = 0.0, travel = 0.0;
            head >> name >> a.x >> a.y >> a.u >> b.x >> b.y >> b.u >> since >> travel;
            const Place want = readPlace(line.substr(ex + 8));
            const Place got = glideAt(a, b, since, travel);
            report(name, near(got.x, want.x) && near(got.y, want.y) && near(got.u, want.u),
                   "at " + std::to_string(got.x) + " " + std::to_string(got.y) + " "
                       + std::to_string(got.u));
        } else if (line.rfind("SCRIPT ", 0) == 0) {
            std::istringstream head(line.substr(7));
            std::string word;
            head >> script >> word;          // MAP
            poly.clear();
            for (std::string xy; head >> xy;) {
                if (xy == "all") break;
                const auto c = split(xy, ',');
                poly.emplace_back(std::stof(c[0]), std::stof(c[1]));
            }
            chain.clear();
        } else if (line.rfind("STEP ", 0) == 0) {
            const std::size_t bar = line.find(" | "), ex = line.find(" EXPECT ");
            std::istringstream head(line.substr(5, bar - 5));
            float gap = 0.0f;
            std::string word, flags;
            head >> gap >> word >> flags;
            std::vector<bool> follows;
            for (char c : flags) follows.push_back(c == '1');
            const std::vector<Place> now = readPlaces(line.substr(bar + 3, ex - bar - 3));
            const auto moves = chain.step(now, follows, gap, [&](float x, float y) {
                return insidePolygon(poly, x, y);
            });
            std::vector<std::string> want = split(trim(line.substr(ex + 8)), ' ');
            if (want.size() == 1 && want[0] == "-") want.clear();
            std::string why;
            if (moves.size() != want.size()) {
                why = std::to_string(moves.size()) + " moves, want " + std::to_string(want.size());
            } else {
                for (std::size_t i = 0; i < moves.size() && why.empty(); ++i) {
                    const auto w = split(want[i], ':');
                    const auto xyz = split(w[1], ',');
                    const FollowMove& m = moves[i];
                    if (m.point != std::stoi(w[0]) || !near(m.to.x, std::stod(xyz[0]))
                        || !near(m.to.y, std::stod(xyz[1])) || !near(m.to.u, std::stod(xyz[2]))
                        || m.held != (w[2] == "1")) {
                        why = "point " + std::to_string(m.point) + " to " + std::to_string(m.to.x)
                            + "," + std::to_string(m.to.y) + "," + std::to_string(m.to.u)
                            + (m.held ? " held" : "") + " -- want " + want[i];
                    }
                }
            }
            report(script + " step", why.empty(), why);
        }
    }
    const int total = pass + fail;
    std::printf("%d/%d  %s\n", pass, total, fail == 0 ? "GREEN" : "RED");
    return fail == 0 ? 0 : 1;
}
