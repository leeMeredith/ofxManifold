#pragma once

// ofxManifold -- the control file (round 3B-2f): a map's points as the
// player takes them from outside, beside the map pair as <map>-control.json.
// A primer, never a requirement: without it a map has point 1, taking every
// inlet. The player writes it; editing it by hand narrows -- a point's
// inlets, its name, the range a controller sends, how long a silent holder
// keeps it -- and never widens: at most kMaxPoints, known inlets only.
//
//   {
//     "control": 1,
//     "points": [
//       { "number": 1, "name": "lead", "inlets": ["xy", "z"],
//         "range": { "x": [0, 127], "y": [0, 127] }, "silence": 5 }
//     ]
//   }
//
// Read strictly, as map and motion files are: a refused file says why and
// changes nothing.

#include "ofxManifoldPointControl.h"
#include "io/ofxManifoldJSON.h"
#include "io/ofxManifoldSerialize.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ofxManifold {
namespace player {

namespace detail {

inline std::string controlNumber(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", double(v));
    return buf;
}

// A letter, then letters, digits, - or _; up to 32.
inline bool controlName(const std::string& n) {
    if (n.empty() || n.size() > 32) return false;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (!letter(n[0])) return false;
    for (char c : n) {
        if (!(letter(c) || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

} // namespace detail

inline std::string saveControl(const ControlSpec& spec) {
    using detail::controlNumber;
    std::string o = "{\n  \"control\": 1,\n  \"points\": [\n";
    for (std::size_t k = 0; k < spec.points.size(); ++k) {
        const PointSpec& p = spec.points[k];
        o += "    { \"number\": " + std::to_string(k + 1);
        if (!p.name.empty()) o += ", \"name\": \"" + p.name + "\"";
        o += ", \"inlets\": [";
        bool first = true;
        for (const char* n : {"xy", "z", "u", "xyz"}) {
            const unsigned bit = std::string(n) == "xy" ? InletXY : std::string(n) == "z" ? InletZ
                               : std::string(n) == "u" ? InletU : InletXYZ;
            if (!(p.inlets & bit)) continue;
            o += std::string(first ? "" : ", ") + "\"" + n + "\"";
            first = false;
        }
        o += "]";
        std::string ranges;
        const std::pair<const char*, const ControlRange*> axes[] = {
            {"x", &p.x}, {"y", &p.y}, {"z", &p.z}, {"u", &p.u}};
        for (const auto& a : axes) {
            if (!a.second->set) continue;
            ranges += std::string(ranges.empty() ? "" : ", ") + "\"" + a.first + "\": ["
                    + controlNumber(a.second->lo) + ", " + controlNumber(a.second->hi) + "]";
        }
        if (!ranges.empty()) o += ", \"range\": { " + ranges + " }";
        o += ", \"silence\": " + controlNumber(p.silence) + " }";
        o += k + 1 < spec.points.size() ? ",\n" : "\n";
    }
    o += "  ]\n}\n";
    return o;
}

// Reads `text` into `out`, or refuses, leaving `out` as it was. `points`: how
// many points this player has -- a file declaring more is refused.
inline io::LoadResult loadControl(const std::string& text, ControlSpec& out,
                                  int points = kMaxPoints) {
    io::LoadResult res;
    const json::ParseResult pr = json::parse(text);
    if (!pr.ok) { res.error = "json: " + pr.error; return res; }
    const json::Value& d = pr.value;
    auto fail = [&](const std::string& why) { res.error = why; return res; };
    if (!d.isObject()) return fail("root is not an object");
    for (const auto& kv : d.asObject()) {
        if (kv.first != "control" && kv.first != "points") return fail("unknown key: " + kv.first);
    }
    if (!d["control"].isNumber() || d["control"].asNumber() != 1.0)
        return fail("missing or unsupported control version");
    if (!d["points"].isArray() || d["points"].asArray().empty())
        return fail("points must be a list of at least one");
    const json::Array& ps = d["points"].asArray();
    if (int(ps.size()) > kMaxPoints)
        return fail("at most " + std::to_string(kMaxPoints) + " points");
    if (int(ps.size()) > points)
        return fail("this player has " + std::to_string(points) + " point"
                    + (points == 1 ? "" : "s"));

    ControlSpec spec;
    spec.points.clear();
    for (std::size_t k = 0; k < ps.size(); ++k) {
        const json::Value& p = ps[k];
        const std::string at = "point " + std::to_string(k + 1) + ": ";
        if (!p.isObject()) return fail(at + "not an object");
        for (const auto& kv : p.asObject()) {
            const std::string& key = kv.first;
            if (key != "number" && key != "name" && key != "inlets" && key != "range"
                && key != "silence") return fail(at + "unknown key: " + key);
        }
        if (!p["number"].isNumber() || p["number"].asNumber() != double(k + 1))
            return fail(at + "numbered out of order");
        PointSpec s;
        if (p.has("name")) {
            if (!p["name"].isString() || !detail::controlName(p["name"].asString()))
                return fail(at + "a name is a letter, then letters, digits, - or _, up to 32");
            s.name = p["name"].asString();
            for (const PointSpec& o : spec.points) {
                if (o.name == s.name) return fail(at + "the name " + s.name + " is taken");
            }
        }
        if (p.has("inlets")) {
            if (!p["inlets"].isArray()) return fail(at + "inlets must be a list");
            s.inlets = 0;
            for (const json::Value& n : p["inlets"].asArray()) {
                const std::string name = n.isString() ? n.asString() : "";
                const unsigned bit = name == "xy" ? InletXY : name == "z" ? InletZ
                                   : name == "u" ? InletU : name == "xyz" ? InletXYZ : 0u;
                if (bit == 0) return fail(at + "unknown inlet: " + name);
                if (s.inlets & bit) return fail(at + "the inlet " + name + " twice");
                s.inlets |= bit;
            }
        }
        if (p.has("range")) {
            if (!p["range"].isObject()) return fail(at + "range must be an object");
            for (const auto& kv : p["range"].asObject()) {
                ControlRange* r = kv.first == "x" ? &s.x : kv.first == "y" ? &s.y
                                : kv.first == "z" ? &s.z : kv.first == "u" ? &s.u : nullptr;
                if (!r) return fail(at + "unknown range axis: " + kv.first);
                const json::Value& v = kv.second;
                if (!v.isArray() || v.asArray().size() != 2 || !v.asArray()[0].isNumber()
                    || !v.asArray()[1].isNumber())
                    return fail(at + "a range is two numbers, low and high");
                const double lo = v.asArray()[0].asNumber(), hi = v.asArray()[1].asNumber();
                if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo < hi))
                    return fail(at + "a range's low must be below its high");
                r->set = true;
                r->lo = float(lo);
                r->hi = float(hi);
            }
        }
        if (p.has("silence")) {
            if (!p["silence"].isNumber() || !(p["silence"].asNumber() >= 0.0)
                || !std::isfinite(p["silence"].asNumber()))
                return fail(at + "silence is 0 or more seconds");
            s.silence = float(p["silence"].asNumber());
        }
        spec.points.push_back(s);
    }
    out = spec;
    res.ok = true;
    return res;
}

} // namespace player
} // namespace ofxManifold
