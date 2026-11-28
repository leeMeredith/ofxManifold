#pragma once

// ofxManifold — files for the 3D map (PLAN-round3.md decision G, round 3A-4c).
//
// A 3D map is saved in the usual pair of files, nothing more:
//
//   the map file (name.json)       the floor plan, as always; and, when the
//                                  map has them, "values" (the value set) and
//                                  "levels": each level's starting height,
//                                  the heights of its bent nodes, its node
//                                  values. That makes it version 3 --
//                                  version 4 when levels touch (3B-2d),
//                                  version 5 when any node is off its
//                                  column on a level (3B-5c): each such
//                                  level then lists "places", a node and
//                                  how far off its column it is there.
//   the outputs file               outputs and derived outputs, as always;
//   (name-outputs.json)            each binding on a level other than the
//                                  first carries "level": k. That makes it
//                                  version 3.
//
// The rule of D-022 holds: the lowest version that reloads exactly. A map
// with one flat level at 0 and no values -- every map made before 3D -- saves
// byte-identical to saveManifold() and saveMapping(), checked by vector.
//
// Levels that touch (round 3B-2d) are written at version 4, and only they:
// an ofxManifold from before then reads levels as strictly apart, and would
// refuse a touch as a crossing -- by version it refuses plainly instead. A
// version 3 file with a touch is refused here for the same reason, as levels
// in a version 2 file are.
//
// A node off its column (round 3B-5c) is written at version 5, and only
// then, for the same reason: an older reader would open the map with every
// level the floor plan's own shape, without a word. A file whose places
// leave the map unsound -- a region flattened, turned over or crossing
// between two levels, or the volume folded -- is refused, as crossing
// heights are.
//
// Each file refuses the other's reader rather than being half-read:
// loadManifold() refuses a version 3 map file, loadMapping() a binding with a
// level. loadMap3D() opens both, and checks that the outputs file names no
// level the map file does not have.

#include "../mapping/ofxManifoldMap3D.h"
#include "ofxManifoldSerialize.h"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace io {

namespace detail {

// Is there anything only version 3 can say? More than one level, a level not
// starting at 0, a bent node, a node off its column, or a declared value.
inline bool needs3D(const Map3D& map, std::size_t nodeCount) {
    const Levels& lv = map.levels();
    if (lv.count() > 1 || !map.valueSet().empty()) return true;
    if (lv.shaped()) return true;              // a node off its column (3B-5c)
    if (lv.base(0) != 0.0f) return true;
    for (NodeID n = 0; n < nodeCount; ++n) {
        if (lv.bent(0, n)) return true;
    }
    return false;
}

// The map file's version for a 3D map: 5 only where a node is off its
// column, 4 only where levels touch.
inline int map3Version(const Map3D& map, std::size_t nodeCount) {
    if (map.levels().shaped()) return 5;
    return map.levels().touches(nodeCount) ? 4 : 3;
}

// What broke, in a file's words.
inline std::string faultText(const Levels::SliceFault& f) {
    const char* what = f.kind == Levels::SliceFault::Flat ? "flattened"
                     : f.kind == Levels::SliceFault::TurnedOver ? "turned over"
                     : f.kind == Levels::SliceFault::Crossing ? "crossing itself"
                     : "folded";
    return "region " + std::to_string(f.region) + " is " + what
         + " between levels " + std::to_string(f.below) + " and "
         + std::to_string(f.below + 1);
}

} // namespace detail

inline std::string saveManifold3D(const Manifold2D& m, const Map3D& map) {
    if (!detail::needs3D(map, m.nodeCount())) return saveManifold(m);

    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << detail::map3Version(map, m.nodeCount()) << ",\n";
    detail::writeFloor(o, m, true);
    o << "  ],\n";

    const auto& defs = map.valueSet();
    o << "  \"values\": [";
    for (std::size_t v = 0; v < defs.size(); ++v) {
        o << (v ? ",\n" : "\n") << "    { \"name\": " << json::quote(defs[v].name)
          << ", \"fallback\": " << json::number(defs[v].fallback)
          << ", \"lo\": " << json::number(defs[v].lo)
          << ", \"hi\": " << json::number(defs[v].hi) << " }";
    }
    o << (defs.empty() ? "],\n" : "\n  ],\n");

    const Levels& lv = map.levels();
    o << "  \"levels\": [\n";
    for (LevelID k = 0; k < lv.count(); ++k) {
        o << "    { \"base\": " << json::number(lv.base(k)) << ",\n";
        o << "      \"heights\": [";
        bool first = true;
        for (NodeID n = 0; n < m.nodeCount(); ++n) {
            if (!lv.bent(k, n)) continue;
            o << (first ? "" : ", ") << "{ \"node\": "
              << json::quote(m.node(n).name) << ", \"height\": "
              << json::number(lv.height(k, n)) << " }";
            first = false;
        }
        o << "],\n";
        // Only in a shaped map, and only the nodes off their columns here.
        if (lv.shaped()) {
            o << "      \"places\": [";
            first = true;
            for (NodeID n = 0; n < m.nodeCount(); ++n) {
                if (!lv.shaped(k, n)) continue;
                const glm::vec2 off = lv.offset(k, n);
                o << (first ? "" : ", ") << "{ \"node\": "
                  << json::quote(m.node(n).name) << ", \"dx\": "
                  << json::number(off.x) << ", \"dy\": "
                  << json::number(off.y) << " }";
                first = false;
            }
            o << "],\n";
        }
        // Only values that differ from the fallback: a node at its fallback
        // reloads the same either way.
        o << "      \"values\": [";
        first = true;
        for (std::size_t v = 0; v < defs.size(); ++v) {
            for (NodeID n = 0; n < m.nodeCount(); ++n) {
                const float x = map.value(k, n, v);
                if (x == defs[v].fallback) continue;
                o << (first ? "" : ", ") << "{ \"value\": "
                  << json::quote(defs[v].name) << ", \"node\": "
                  << json::quote(m.node(n).name) << ", \"x\": "
                  << json::number(x) << " }";
                first = false;
            }
        }
        o << "] }";
        if (k + 1 < lv.count()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

inline std::string saveMapping3D(const Map3D& map, const Manifold2D& m) {
    if (map.count() == 1) return saveMapping(map.level(0), m);

    // Version 3: bindings carry their level. Outputs and derived outputs are
    // every level's, written once, from the first.
    const Mapping& room = map.level(0);
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": 3,\n";
    detail::writeOutputs(o, room);
    o << ",\n";
    o << "  \"bindings\": [\n";
    bool first = true;
    for (LevelID k = 0; k < map.count(); ++k) {
        const Mapping& mp = map.level(k);
        for (std::size_t n = 0; n < m.nodeCount(); ++n) {
            const NodeID id = static_cast<NodeID>(n);
            for (std::size_t i = 0; i < mp.linkCount(id); ++i) {
                const Link& l = mp.link(id, i);
                if (!first) o << ",\n";
                o << "    { \"node\": " << json::quote(m.node(id).name);
                if (k > 0) o << ", \"level\": " << k;
                if (l.kind == DestKind::Silence) {
                    o << ", \"kind\": \"silence\"";
                } else {
                    o << ", \"target\": " << json::quote(mp.targetName(l.id));
                }
                o << ", \"weight\": " << json::number(l.weight) << " }";
                first = false;
            }
        }
    }
    if (!first) o << "\n";
    o << "  ],\n";
    detail::writeAggregators(o, room, &m, false);
    o << "}\n";
    return o.str();
}

// Open a 3D map's pair of files -- or any map's: a version 1 or 2 map file
// and an outputs file with no levels open as a 3D map of one level.
//
// Refused, with the reason, and leaving `m` and `out` untouched, if either
// file is refused on its own, if the outputs file binds on a level the map
// file does not have, if levels cross -- or touch in a file older than
// version 4 --, if a node is off its column in a file older than version 5
// or the places leave the map unsound, if a value is set that was
// never declared or declared twice, or if a height or value names a node the
// floor plan does not have.
inline LoadResult loadMap3D(const std::string& mapText,
                            const std::string& outputsText,
                            Manifold2D& m, Map3D& out) {
    LoadResult res;
    Manifold2D floor;
    {
        const LoadResult fr = detail::loadFloor(mapText, floor, 5);
        if (!fr.ok) { res.error = fr.error; return res; }
    }
    const json::ParseResult pr = json::parse(mapText);
    const json::Value& root = pr.value;
    // Levels and values in an older file would be read here and dropped by
    // every older reader; only version 3 may carry them.
    if ((root.has("levels") || root.has("values"))
        && static_cast<int>(root["version"].asNumber()) < 3) {
        res.error = "levels and values need version 3";
        return res;
    }

    // ---- levels: starting heights and bends --------------------------------
    std::vector<float> bases{0.0f};
    std::vector<std::vector<std::pair<NodeID, float>>> bends(1);
    struct Placed { LevelID level; NodeID node; glm::vec2 off; };
    std::vector<Placed> placed;
    struct SetValue { LevelID level; NodeID node; std::string name; float x; };
    std::vector<SetValue> sets;
    if (root.has("levels")) {
        if (!root["levels"].isArray() || root["levels"].asArray().empty()) {
            res.error = "levels is not a non-empty array";
            return res;
        }
        bases.clear();
        bends.clear();
        LevelID k = 0;
        for (const json::Value& lv : root["levels"].asArray()) {
            if (!lv.isObject() || !lv.has("base")) {
                res.error = "level " + std::to_string(k) + " has no base";
                return res;
            }
            bases.push_back(lv["base"].asFloat());
            bends.emplace_back();
            if (lv.has("heights")) {
                for (const json::Value& h : lv["heights"].asArray()) {
                    const NodeID n = floor.findNode(h["node"].asString());
                    if (n == InvalidNode) {
                        res.error = "height on level " + std::to_string(k)
                                  + " for unknown node: "
                                  + h["node"].asString();
                        return res;
                    }
                    bends.back().emplace_back(n, h["height"].asFloat());
                }
            }
            if (lv.has("places")) {
                for (const json::Value& pl : lv["places"].asArray()) {
                    const NodeID n = floor.findNode(pl["node"].asString());
                    if (n == InvalidNode) {
                        res.error = "place on level " + std::to_string(k)
                                  + " for unknown node: "
                                  + pl["node"].asString();
                        return res;
                    }
                    placed.push_back({k, n, glm::vec2(pl["dx"].asFloat(),
                                                      pl["dy"].asFloat())});
                }
            }
            if (lv.has("values")) {
                for (const json::Value& x : lv["values"].asArray()) {
                    const NodeID n = floor.findNode(x["node"].asString());
                    if (n == InvalidNode) {
                        res.error = "value on level " + std::to_string(k)
                                  + " for unknown node: "
                                  + x["node"].asString();
                        return res;
                    }
                    sets.push_back({k, n, x["value"].asString(),
                                    x["x"].asFloat()});
                }
            }
            ++k;
        }
    }
    Levels geometry;
    {
        std::string why;
        if (!geometry.rebuild(bases, bends, floor.nodeCount(), &why)) {
            res.error = why;
            return res;
        }
    }
    if (static_cast<int>(root["version"].asNumber()) < 4
        && geometry.touches(floor.nodeCount())) {
        res.error = "levels that touch need version 4";
        return res;
    }

    // ---- places: nodes off their columns (3B-5c) -------------------------------
    for (const Placed& pl : placed) geometry.setOffset(pl.level, pl.node, pl.off);
    if (geometry.shaped()) {
        if (static_cast<int>(root["version"].asNumber()) < 5) {
            res.error = "a node off its column needs version 5";
            return res;
        }
        const Levels::SliceFault fault = geometry.unsound(floor);
        if (fault) {
            res.error = "unsound: " + detail::faultText(fault);
            return res;
        }
    }

    // ---- bindings, per level ---------------------------------------------------
    std::vector<Mapping> routing(geometry.count());
    {
        const json::ParseResult op = json::parse(outputsText);
        if (!op.ok) {
            res.error = "outputs json: " + op.error + " at byte "
                      + std::to_string(op.offset);
            return res;
        }
        int version = 0;
        if (!detail::readMappingHeader(op.value, version, res.error)) return res;
        if (!detail::readMappingLevels(op.value, version, floor, routing, true,
                                       res.error)) {
            return res;
        }
    }

    Map3D built;
    {
        std::string why;
        if (!built.adopt(std::move(geometry), std::move(routing), &why)) {
            res.error = why;
            return res;
        }
    }

    // ---- values -------------------------------------------------------------
    if (root.has("values")) {
        for (const json::Value& d : root["values"].asArray()) {
            const std::string name = d["name"].asString();
            if (built.addValue(name,
                               d.has("fallback") ? d["fallback"].asFloat() : 0.0f,
                               d.has("lo") ? d["lo"].asFloat() : 0.0f,
                               d.has("hi") ? d["hi"].asFloat() : 1.0f)
                == static_cast<std::size_t>(-1)) {
                res.error = "value declared twice: " + name;
                return res;
            }
        }
    }
    for (const SetValue& sv : sets) {
        const std::size_t v = built.findValue(sv.name);
        if (v == static_cast<std::size_t>(-1)) {
            res.error = "value set but never declared: " + sv.name;
            return res;
        }
        built.setValue(sv.level, sv.node, v, sv.x);
    }

    m = std::move(floor);
    out = std::move(built);
    res.ok = true;
    return res;
}

} // namespace io
} // namespace ofxManifold
