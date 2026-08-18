#pragma once

// ofxManifold — serialization (architecture doc section 15).
//
// Built last, deliberately. A format settled before the thing it describes is
// finished becomes a migration the first time the thing changes, and migrations
// on a format nobody has used yet are pure cost.
//
// Two files, not one. A MANIFOLD is portable; a MAPPING is installation
// specific. That split is the whole reason a SpaceMap trajectory survives a
// change of venue: the map is rebuilt for the new rig, the cues are not
// touched. Keeping bindings in the manifold file would weld the two together
// and lose exactly the property that makes the format worth having.
//
// Mapping refers to nodes BY NAME, never by NodeID. IDs are runtime handles
// whose values depend on insertion order; writing them to disk would make a
// mapping file silently wrong the first time someone reorders the nodes in
// their manifold.

#include "ofxManifoldJSON.h"
#include "../core/ofxManifold2D.h"
#include "../mapping/ofxManifoldMapping.h"

#include <cstddef>
#include <sstream>
#include <string>

namespace ofxManifold {
namespace io {

static constexpr int kFormatVersion = 1;

struct LoadResult {
    bool        ok = false;
    std::string error;
};

// ---- manifold ------------------------------------------------------------

inline std::string saveManifold(const Manifold2D& m) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << kFormatVersion << ",\n";
    // Recorded so a future non-normalized variant is DETECTABLE rather than
    // silently misread as normalized. A loader that finds an unknown space
    // refuses rather than guessing.
    o << "  \"space\": \"normalized\",\n";

    o << "  \"nodes\": [\n";
    for (std::size_t i = 0; i < m.nodeCount(); ++i) {
        const Node& n = m.node(static_cast<NodeID>(i));
        o << "    { \"id\": " << json::quote(n.name)
          << ", \"position\": [" << json::number(n.position.x) << ", "
          << json::number(n.position.y) << "]"
          << ", \"weight\": " << json::number(n.weight) << " }";
        if (i + 1 < m.nodeCount()) o << ",";
        o << "\n";
    }
    o << "  ],\n";

    o << "  \"triangles\": [\n";
    for (std::size_t r = 0; r < m.regionCount(); ++r) {
        const auto& ids = m.region(static_cast<RegionID>(r)).ids();
        o << "    [" << json::quote(m.node(ids[0]).name) << ", "
          << json::quote(m.node(ids[1]).name) << ", "
          << json::quote(m.node(ids[2]).name) << "]";
        if (r + 1 < m.regionCount()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

inline LoadResult loadManifold(const std::string& text, Manifold2D& out) {
    LoadResult res;
    const json::ParseResult pr = json::parse(text);
    if (!pr.ok) {
        res.error = "json: " + pr.error + " at byte "
                  + std::to_string(pr.offset);
        return res;
    }
    const json::Value& root = pr.value;
    if (!root.isObject()) { res.error = "root is not an object"; return res; }

    if (!root.has("version") || !root["version"].isNumber()) {
        res.error = "missing version";
        return res;
    }
    const int v = static_cast<int>(root["version"].asNumber());
    if (v != kFormatVersion) {
        res.error = "unsupported version " + std::to_string(v);
        return res;
    }
    if (root.has("space") && root["space"].asString() != "normalized") {
        res.error = "unsupported space: " + root["space"].asString();
        return res;
    }
    if (!root.has("nodes") || !root["nodes"].isArray()) {
        res.error = "missing nodes array";
        return res;
    }

    Manifold2D built;
    for (const json::Value& n : root["nodes"].asArray()) {
        if (!n.isObject() || !n["id"].isString()) {
            res.error = "node is not an object with a string id";
            return res;
        }
        const std::string name = n["id"].asString();
        if (built.findNode(name) != InvalidNode) {
            res.error = "duplicate node id: " + name;
            return res;
        }
        if (!n["position"].isArray() || n["position"].asArray().size() != 2) {
            res.error = "node " + name + " has no two-element position";
            return res;
        }
        const float x = n["position"].asArray()[0].asFloat();
        const float y = n["position"].asArray()[1].asFloat();
        // Absent weight defaults to 1, so a hand-written file need not carry
        // the field on every node.
        const float w = n.has("weight") ? n["weight"].asFloat() : 1.0f;
        built.addNode(name, {x, y}, w);
    }

    if (root.has("triangles")) {
        if (!root["triangles"].isArray()) {
            res.error = "triangles is not an array";
            return res;
        }
        for (const json::Value& t : root["triangles"].asArray()) {
            if (!t.isArray() || t.asArray().size() != 3) {
                res.error = "triangle is not an array of three node ids";
                return res;
            }
            NodeID ids[3];
            for (int k = 0; k < 3; ++k) {
                const std::string nm = t.asArray()[k].asString();
                ids[k] = built.findNode(nm);
                if (ids[k] == InvalidNode) {
                    res.error = "triangle references unknown node: " + nm;
                    return res;
                }
            }
            if (built.addTriangle(ids[0], ids[1], ids[2]) == InvalidRegion) {
                // Degenerate or repeated vertices. Refusing here rather than
                // loading a manifold whose regions divide by zero later.
                res.error = "triangle rejected: degenerate or repeated vertex";
                return res;
            }
        }
    }

    out = std::move(built);
    res.ok = true;
    return res;
}

// ---- mapping -------------------------------------------------------------

inline std::string saveMapping(const Mapping& mp, const Manifold2D& m) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << kFormatVersion << ",\n";

    o << "  \"bindings\": [\n";
    bool first = true;
    for (std::size_t n = 0; n < m.nodeCount(); ++n) {
        const NodeID id = static_cast<NodeID>(n);
        for (std::size_t k = 0; k < mp.linkCount(id); ++k) {
            const WeightedTarget& l = mp.link(id, k);
            if (!first) o << ",\n";
            o << "    { \"node\": " << json::quote(m.node(id).name)
              << ", \"target\": " << json::quote(mp.targetName(l.id))
              << ", \"weight\": " << json::number(l.weight) << " }";
            first = false;
        }
    }
    if (!first) o << "\n";
    o << "  ],\n";

    o << "  \"aggregators\": [\n";
    const auto& ags = mp.aggregators();
    for (std::size_t a = 0; a < ags.size(); ++a) {
        o << "    { \"name\": " << json::quote(ags[a].name)
          << ", \"mode\": \""
          << (ags[a].mode == SumMode::Linear ? "linear" : "power")
          << "\", \"sources\": [";
        for (std::size_t s = 0; s < ags[a].sources.size(); ++s) {
            o << json::quote(m.node(ags[a].sources[s]).name);
            if (s + 1 < ags[a].sources.size()) o << ", ";
        }
        o << "] }";
        if (a + 1 < ags.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

inline LoadResult loadMapping(const std::string& text, const Manifold2D& m,
                              Mapping& out) {
    LoadResult res;
    const json::ParseResult pr = json::parse(text);
    if (!pr.ok) {
        res.error = "json: " + pr.error + " at byte "
                  + std::to_string(pr.offset);
        return res;
    }
    const json::Value& root = pr.value;
    if (!root.isObject()) { res.error = "root is not an object"; return res; }
    if (!root.has("version") ||
        static_cast<int>(root["version"].asNumber()) != kFormatVersion) {
        res.error = "missing or unsupported version";
        return res;
    }

    Mapping built;
    if (root.has("bindings")) {
        if (!root["bindings"].isArray()) {
            res.error = "bindings is not an array";
            return res;
        }
        for (const json::Value& b : root["bindings"].asArray()) {
            if (!b.isObject() || !b["node"].isString()
                || !b["target"].isString()) {
                res.error = "binding needs string node and target";
                return res;
            }
            const std::string nn = b["node"].asString();
            const NodeID id = m.findNode(nn);
            if (id == InvalidNode) {
                // A mapping is only meaningful against a manifold, so a
                // dangling reference is a load error rather than a warning.
                res.error = "binding references unknown node: " + nn;
                return res;
            }
            const float w = b.has("weight") ? b["weight"].asFloat() : 1.0f;
            built.bind(id, built.addTarget(b["target"].asString()), w);
        }
    }

    if (root.has("aggregators")) {
        for (const json::Value& a : root["aggregators"].asArray()) {
            if (!a.isObject() || !a["name"].isString()) {
                res.error = "aggregator needs a string name";
                return res;
            }
            const std::string mode =
                a.has("mode") ? a["mode"].asString() : "linear";
            if (mode != "linear" && mode != "power") {
                res.error = "unknown aggregator mode: " + mode;
                return res;
            }
            std::vector<NodeID> srcs;
            if (a.has("sources")) {
                for (const json::Value& s : a["sources"].asArray()) {
                    const NodeID id = m.findNode(s.asString());
                    if (id == InvalidNode) {
                        res.error = "aggregator references unknown node: "
                                  + s.asString();
                        return res;
                    }
                    srcs.push_back(id);
                }
            }
            built.addAggregator(a["name"].asString(), srcs,
                                mode == "power" ? SumMode::PowerPreserving
                                                : SumMode::Linear);
        }
    }

    out = std::move(built);
    res.ok = true;
    return res;
}

} // namespace io
} // namespace ofxManifold
