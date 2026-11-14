#pragma once

// ofxManifold -- motion files (round 3B-2).
//
//     {"motion": 1, "kind": "walk", "seed": 7, "units": "seconds",
//      "start": [0.5, 0.5, 0],
//      "clock": {"every": 0.5, "jitter": 0}      or {"at": [0.8, 1.9, 2.4]}
//                                                or {"cycle": 1, "at": [0, 0.1875, 0.5]},
//      "travel": {"duration": 0.4}               or {"speed": 0.25},
//      "easing": "smooth",
//      "bounds": {"rect": [0, 0, 1, 1], "z": [0, 0.5], "step": 0,
//                 "inside": "map", "straight": true}}
//
// "units" -- "seconds" or "bars" -- says what the numbers count, so a walk
// written for bars is not played in seconds without anyone knowing. "inside":
// "map" says the player is to supply the map's test; "none", the rectangle
// alone. A recording keeps its path file.
//
// Numbers are written as the shorter of %.15g and %.17g that reads back
// exactly, so save -> load -> save is byte-stable and a loaded walk is the
// walk that was saved, step for step. The Python reference writes the same
// text (tests/ref/reference_motion.py, save()).

#include "../sources/ofxManifoldMotion.h"
#include "ofxManifoldJSON.h"
#include "ofxManifoldSerialize.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace ofxManifold {
namespace io {

namespace detail {

inline std::string motionNumber(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.15g", v);
    if (std::strtod(buf, nullptr) != v) std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

inline std::string motionList(const std::vector<double>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += ", ";
        s += motionNumber(v[i]);
    }
    return s;
}

} // namespace detail

// `units`: "seconds" or "bars".
inline std::string saveMotion(const RandomWalk& w, const std::string& units = "seconds") {
    using detail::motionNumber;
    using detail::motionList;
    const WalkSettings& s = w.settings();
    std::ostringstream o;
    o << "{\"motion\": 1, \"kind\": \"walk\", \"seed\": " << s.seed
      << ", \"units\": \"" << units << "\",\n";
    o << " \"start\": [" << motionList({s.start.x, s.start.y, s.start.z}) << "],\n";
    const StepTiming& c = s.steps;
    switch (c.kind) {
    case StepTiming::Kind::Every:
        o << " \"clock\": {\"every\": " << motionNumber(c.interval)
          << ", \"jitter\": " << motionNumber(c.jitter) << "},\n";
        break;
    case StepTiming::Kind::At:
        o << " \"clock\": {\"at\": [" << motionList(c.at) << "]},\n";
        break;
    case StepTiming::Kind::Pattern:
        o << " \"clock\": {\"cycle\": " << motionNumber(c.cycle)
          << ", \"at\": [" << motionList(c.at) << "]},\n";
        break;
    }
    o << " \"travel\": {\"" << (s.bySpeed ? "speed" : "duration") << "\": "
      << motionNumber(s.travel) << "},\n";
    o << " \"easing\": \"" << (s.easing == Easing::Smooth ? "smooth" : "linear") << "\",\n";
    o << " \"bounds\": {\"rect\": [" << motionList({s.x0, s.y0, s.x1, s.y1})
      << "], \"z\": [" << motionList({s.zLow, s.zHigh})
      << "], \"step\": " << motionNumber(s.step)
      << ", \"inside\": \"" << (s.insideMap ? "map" : "none")
      << "\", \"straight\": " << (s.straight ? "true" : "false") << "}}\n";
    return o.str();
}

// Refused: unknown kind, version above 1, unknown units, an interval at or
// below 0, jitter outside 0..0.9, clicks going backwards, a pattern's offsets
// out of order or outside its cycle, travel below 0, speed at or below 0, an
// unknown easing, a rectangle inside out, z low above high, a step distance
// below 0, inside neither "map" nor "none".
inline LoadResult loadMotion(const std::string& text, RandomWalk& out,
                             std::string* unitsOut = nullptr) {
    LoadResult res;
    const json::ParseResult pr = json::parse(text);
    if (!pr.ok) { res.error = "json: " + pr.error; return res; }
    const json::Value& d = pr.value;
    auto fail = [&](const char* why) { res.error = why; return res; };
    if (!d.isObject()) return fail("root is not an object");
    if (!d["motion"].isNumber() || d["motion"].asNumber() != 1.0)
        return fail("missing or unsupported motion version");
    if (d["kind"].asString() != "walk") return fail("unknown kind");
    const std::string units = d.has("units") ? d["units"].asString() : "seconds";
    if (units != "seconds" && units != "bars") return fail("unknown units");

    WalkSettings s;
    s.seed = static_cast<std::uint64_t>(d["seed"].asNumber());
    if (d.has("start")) {
        const json::Array& a = d["start"].asArray();
        if (a.size() != 3) return fail("start needs three numbers");
        s.start = glm::dvec3(a[0].asNumber(), a[1].asNumber(), a[2].asNumber());
    }

    const json::Value& c = d["clock"];
    auto numbers = [](const json::Value& v) {
        std::vector<double> r;
        for (const json::Value& x : v.asArray()) r.push_back(x.asNumber());
        return r;
    };
    if (c.has("every")) {
        const double i = c["every"].asNumber();
        const double j = c.has("jitter") ? c["jitter"].asNumber() : 0.0;
        if (!(i > 0.0)) return fail("the interval must be above 0");
        if (!(j >= 0.0 && j <= 0.9)) return fail("jitter must be 0 to 0.9");
        s.steps = StepTiming::every(i, j);
    } else if (c.has("cycle")) {
        const double cyc = c["cycle"].asNumber();
        const std::vector<double> offs = numbers(c["at"]);
        if (!(cyc > 0.0) || offs.empty()) return fail("a pattern needs a cycle above 0 and offsets");
        for (std::size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] < 0.0 || offs[i] >= cyc || (i && !(offs[i] > offs[i - 1])))
                return fail("a pattern's offsets must rise within its cycle");
        }
        s.steps = StepTiming::pattern(cyc, offs);
    } else if (c.has("at")) {
        const std::vector<double> at = numbers(c["at"]);
        for (std::size_t i = 1; i < at.size(); ++i)
            if (at[i] < at[i - 1]) return fail("clicks must not go backwards");
        s.steps = StepTiming::clicks(at);
    } else {
        return fail("missing clock");
    }

    const json::Value& tr = d.has("travel") ? d["travel"] : json::Value();
    if (!d.has("travel")) {
        s.bySpeed = false;
        s.travel = 0.0;
    } else if (tr.has("duration")) {
        s.bySpeed = false;
        s.travel = tr["duration"].asNumber();
        if (s.travel < 0.0) return fail("travel must not be below 0");
    } else if (tr.has("speed")) {
        s.bySpeed = true;
        s.travel = tr["speed"].asNumber();
        if (!(s.travel > 0.0)) return fail("speed must be above 0");
    } else {
        return fail("missing travel");
    }

    const std::string easing = d.has("easing") ? d["easing"].asString() : "linear";
    if (easing == "smooth") s.easing = Easing::Smooth;
    else if (easing == "linear") s.easing = Easing::Linear;
    else return fail("unknown easing");

    const json::Value& b = d["bounds"];
    if (b.has("rect")) {
        const std::vector<double> r = numbers(b["rect"]);
        if (r.size() != 4) return fail("rect needs four numbers");
        s.x0 = r[0]; s.y0 = r[1]; s.x1 = r[2]; s.y1 = r[3];
    }
    if (s.x0 > s.x1 || s.y0 > s.y1) return fail("the rectangle is inside out");
    if (b.has("z")) {
        const std::vector<double> z = numbers(b["z"]);
        if (z.size() != 2) return fail("z needs two numbers");
        s.zLow = z[0]; s.zHigh = z[1];
    }
    if (s.zLow > s.zHigh) return fail("z low is above z high");
    s.step = b.has("step") ? b["step"].asNumber() : 0.0;
    if (s.step < 0.0) return fail("the step distance must not be below 0");
    const std::string inside = b.has("inside") ? b["inside"].asString() : "none";
    if (inside != "none" && inside != "map") return fail("inside is map or none");
    s.insideMap = inside == "map";
    s.straight = b.has("straight") ? b["straight"].asBool() : true;

    out = RandomWalk(s);
    if (unitsOut) *unitsOut = units;
    res.ok = true;
    return res;
}

} // namespace io
} // namespace ofxManifold
