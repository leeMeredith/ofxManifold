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
#include "../sources/ofxManifoldTrajectory.h"

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
    // A map made only of triangles is written EXACTLY as before: version 1,
    // a "triangles" key. Every file v1.0.0 could read, it still can.
    //
    // A map containing any region of four or more nodes is written as
    // version 2 under "regions". The version bump is what makes an older
    // reader FAIL CLEANLY -- "unsupported version 2" -- instead of finding no
    // "triangles" key and silently loading a map with no regions at all.
    bool allTriangles = true;
    for (std::size_t r = 0; r < m.regionCount(); ++r) {
        if (m.region(static_cast<RegionID>(r)).size() != 3) {
            allTriangles = false;
            break;
        }
    }
    const int version = allTriangles ? 1 : 2;

    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << version << ",\n";
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

    // Every region is written in full, at whatever arity it has. The
    // previous writer emitted exactly three names per region, so a quad
    // saved as a triangle, dropped its fourth node, and reloaded without
    // error as a different shape. See DECISIONS.md D-017.
    o << "  \"" << (allTriangles ? "triangles" : "regions") << "\": [\n";
    for (std::size_t r = 0; r < m.regionCount(); ++r) {
        const auto& ids = m.region(static_cast<RegionID>(r)).ids();
        o << "    [";
        for (std::size_t k = 0; k < ids.size(); ++k) {
            o << json::quote(m.node(ids[k]).name);
            if (k + 1 < ids.size()) o << ", ";
        }
        o << "]";
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
    if (v != 1 && v != 2) {
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

    // Both keys are read. "triangles" is the version 1 name and must hold
    // exactly three ids per entry; "regions" allows any ring of three or
    // more. A file carrying both is refused: which one wins would be a guess.
    if (root.has("triangles") && root.has("regions")) {
        res.error = "file has both triangles and regions";
        return res;
    }
    const bool legacy = root.has("triangles");
    const char* key = legacy ? "triangles" : "regions";

    if (root.has(key)) {
        if (!root[key].isArray()) {
            res.error = std::string(key) + " is not an array";
            return res;
        }
        for (const json::Value& t : root[key].asArray()) {
            if (!t.isArray()) {
                res.error = "region is not an array of node ids";
                return res;
            }
            if (legacy && t.asArray().size() != 3) {
                res.error = "triangle is not an array of three node ids";
                return res;
            }
            std::vector<NodeID> ids;
            for (const json::Value& nv : t.asArray()) {
                const std::string nm = nv.asString();
                const NodeID id = built.findNode(nm);
                if (id == InvalidNode) {
                    res.error = "region references unknown node: " + nm;
                    return res;
                }
                ids.push_back(id);
            }
            if (built.addRegion(ids) == InvalidRegion) {
                // Refused at load rather than loaded and wrong later. The
                // reason comes from the region itself, so a bowtie reads as
                // self-intersecting rather than as a generic rejection.
                res.error = "region rejected: " + built.lastRegionError();
                return res;
            }
        }
    }

    out = std::move(built);
    res.ok = true;
    return res;
}

// ---- mapping -------------------------------------------------------------

// Would a version 1 file reproduce this mapping EXACTLY on reload?
//
// Version 1 lists bindings only, and outputs are recreated as the bindings
// mention them. Measured before this function existed: an output bound to
// nothing was DROPPED, and outputs came back in the order bindings mention them
// rather than the order they were made -- L and R swapped indices, so an OSC
// receiver reading argument 1 as the right speaker got the left one after
// reopening the file (DECISIONS.md D-022).
//
// So version 1 is written only when it is safe: no new feature, every output
// bound, and outputs first mentioned in the order they were made. Those files
// stay byte-identical to what was written before. Anything else is version 2,
// which lists outputs explicitly, and which an older reader refuses cleanly
// rather than loading wrongly.
inline bool mappingNeedsV2(const Mapping& mp, const Manifold2D& m) {
    for (TargetID t = 0; t < mp.targetCount(); ++t) {
        if (mp.targetChannel(t) != static_cast<int>(t)) return true;
        if (mp.targetTrim(t) != 1.0f) return true;
    }
    for (const auto& a : mp.aggregators()) {
        if (a.channel != NoChannel || a.trim != 1.0f) return true;
        for (float g : a.weights) if (g != 1.0f) return true;
    }
    // Walk the bindings in the order version 1 would write them, recording
    // the order outputs are first mentioned.
    std::vector<bool> seen(mp.targetCount(), false);
    TargetID next = 0;
    for (std::size_t n = 0; n < m.nodeCount(); ++n) {
        const NodeID id = static_cast<NodeID>(n);
        for (std::size_t k = 0; k < mp.linkCount(id); ++k) {
            const Link& l = mp.link(id, k);
            if (l.kind != DestKind::Output) return true;      // silence
            if (l.id >= seen.size() || seen[l.id]) continue;
            if (l.id != next) return true;                    // reordered
            seen[l.id] = true;
            ++next;
        }
    }
    // Links to nodes the manifold does not have would be lost either way.
    for (NodeID n : mp.boundNodes()) {
        if (n >= m.nodeCount()) return true;
    }
    return next != mp.targetCount();                          // unbound
}

inline std::string saveMappingV2(const Mapping& mp, const Manifold2D& m);

inline std::string saveMapping(const Mapping& mp, const Manifold2D& m) {
    if (mappingNeedsV2(mp, m)) return saveMappingV2(mp, m);

    // ---- version 1: UNCHANGED, so existing files stay byte-identical ----
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << kFormatVersion << ",\n";

    o << "  \"bindings\": [\n";
    bool first = true;
    for (std::size_t n = 0; n < m.nodeCount(); ++n) {
        const NodeID id = static_cast<NodeID>(n);
        for (std::size_t k = 0; k < mp.linkCount(id); ++k) {
            const Link& l = mp.link(id, k);
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

// Version 2: outputs listed explicitly, in the order they were made, with
// channel and trim; a binding's kind written only when it is not an output;
// aggregator sources as objects carrying their weights.
inline std::string saveMappingV2(const Mapping& mp, const Manifold2D& m) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": 2,\n";

    o << "  \"outputs\": [\n";
    for (TargetID t = 0; t < mp.targetCount(); ++t) {
        o << "    { \"name\": " << json::quote(mp.targetName(t))
          << ", \"channel\": " << mp.targetChannel(t)
          << ", \"trim\": " << json::number(mp.targetTrim(t)) << " }";
        if (t + 1 < mp.targetCount()) o << ",";
        o << "\n";
    }
    o << "  ],\n";

    o << "  \"bindings\": [\n";
    bool first = true;
    for (std::size_t n = 0; n < m.nodeCount(); ++n) {
        const NodeID id = static_cast<NodeID>(n);
        for (std::size_t k = 0; k < mp.linkCount(id); ++k) {
            const Link& l = mp.link(id, k);
            if (!first) o << ",\n";
            o << "    { \"node\": " << json::quote(m.node(id).name);
            if (l.kind == DestKind::Silence) {
                o << ", \"kind\": \"silence\"";
            } else {
                o << ", \"target\": " << json::quote(mp.targetName(l.id));
            }
            o << ", \"weight\": " << json::number(l.weight) << " }";
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
          << (ags[a].mode == SumMode::Linear ? "linear" : "power") << "\"";
        if (ags[a].channel != NoChannel) {
            o << ", \"channel\": " << ags[a].channel
              << ", \"trim\": " << json::number(ags[a].trim);
        }
        o << ", \"sources\": [";
        for (std::size_t s2 = 0; s2 < ags[a].sources.size(); ++s2) {
            const float g = s2 < ags[a].weights.size() ? ags[a].weights[s2]
                                                       : 1.0f;
            o << "{ \"node\": " << json::quote(m.node(ags[a].sources[s2]).name)
              << ", \"weight\": " << json::number(g) << " }";
            if (s2 + 1 < ags[a].sources.size()) o << ", ";
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
    if (!root.has("version")) {
        res.error = "missing or unsupported version";
        return res;
    }
    const int version = static_cast<int>(root["version"].asNumber());
    if (version != 1 && version != 2) {
        res.error = "missing or unsupported version "
                  + std::to_string(version);
        return res;
    }

    Mapping built;

    // Version 2 declares outputs up front, in order, with channel and trim.
    if (version == 2 && root.has("outputs")) {
        if (!root["outputs"].isArray()) {
            res.error = "outputs is not an array";
            return res;
        }
        for (const json::Value& ov : root["outputs"].asArray()) {
            if (!ov.isObject() || !ov["name"].isString()
                || !ov.has("channel")) {
                res.error = "output needs a string name and a channel";
                return res;
            }
            const std::string nm = ov["name"].asString();
            const int ch = static_cast<int>(ov["channel"].asNumber());
            const float tr = ov.has("trim") ? ov["trim"].asFloat() : 1.0f;
            if (built.findTarget(nm) != InvalidTarget) {
                res.error = "duplicate output name: " + nm;
                return res;
            }
            if (ch < 0) {
                res.error = "negative channel for output " + nm;
                return res;
            }
            if (built.addOutput(nm, ch, tr) == InvalidTarget) {
                res.error = "duplicate channel " + std::to_string(ch)
                          + " for output " + nm;
                return res;
            }
        }
    }
    if (root.has("bindings")) {
        if (!root["bindings"].isArray()) {
            res.error = "bindings is not an array";
            return res;
        }
        for (const json::Value& b : root["bindings"].asArray()) {
            if (!b.isObject() || !b["node"].isString()) {
                res.error = "binding needs a string node";
                return res;
            }
            // Kind: absent means output, which is every version 1 binding.
            // "layer" is reserved and REFUSED until layers exist -- accepting
            // and ignoring it would silently drop a binding.
            const std::string kind =
                b.has("kind") ? b["kind"].asString() : "output";
            if (kind != "output" && kind != "silence") {
                res.error = "unknown binding kind: " + kind;
                return res;
            }
            if (kind == "silence" && b.has("target")) {
                res.error = "a silence binding must not name a target";
                return res;
            }
            if (kind == "output" && !b["target"].isString()) {
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
            if (kind == "silence") {
                built.bindSilence(id, w);
            } else {
                built.bind(id, built.addTarget(b["target"].asString()), w);
            }
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
            // Sources: names (version 1) or {node, weight} (version 2).
            std::vector<std::pair<NodeID, float>> srcs;
            if (a.has("sources")) {
                for (const json::Value& s : a["sources"].asArray()) {
                    const std::string nm =
                        s.isObject() ? s["node"].asString() : s.asString();
                    const float g = (s.isObject() && s.has("weight"))
                                  ? s["weight"].asFloat() : 1.0f;
                    const NodeID id = m.findNode(nm);
                    if (id == InvalidNode) {
                        res.error = "aggregator references unknown node: "
                                  + nm;
                        return res;
                    }
                    srcs.emplace_back(id, g);
                }
            }
            const int ch = a.has("channel")
                         ? static_cast<int>(a["channel"].asNumber())
                         : NoChannel;
            const float tr = a.has("trim") ? a["trim"].asFloat() : 1.0f;
            if (built.addDerived(a["name"].asString(), ch, tr,
                                 mode == "power" ? SumMode::PowerPreserving
                                                 : SumMode::Linear, srcs)
                    == static_cast<std::size_t>(-1)) {
                res.error = "aggregator " + a["name"].asString()
                          + ": channel " + std::to_string(ch)
                          + " is negative or already used";
                return res;
            }
        }
    }

    out = std::move(built);
    res.ok = true;
    return res;
}

// ---- trajectory ----------------------------------------------------------
//
// A THIRD file, and for the same reason there are already two: a trajectory
// outlives the map it was recorded against. Section 5 chose normalized
// coordinates precisely so a recorded path could be replayed against a rebuilt
// map, and welding the path into the manifold file would destroy that.
//
// Stored as sampled points rather than as a parametric curve. Sampling is what
// recording produces, it preserves the original timing exactly -- including
// pauses -- and it needs no decision about which curve family is right. A
// curve representation can be added later as a second format; going the other
// way, from a fitted curve back to what actually happened, cannot.
inline std::string saveTrajectory(const Trajectory& tr) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << kFormatVersion << ",\n";
    o << "  \"space\": \"normalized\",\n";
    // Optional. Absent means the original timing is unknown, which is what a
    // path assembled programmatically has. A reader from before this field
    // existed ignores it, and a file without it still loads.
    if (tr.duration() > 0.0f) {
        o << "  \"duration\": " << json::number(tr.duration()) << ",\n";
    }
    o << "  \"samples\": [\n";
    for (std::size_t i = 0; i < tr.sampleCount(); ++i) {
        const TrajectorySample& s = tr.sample(i);
        o << "    { \"t\": " << json::number(s.t)
          << ", \"p\": [" << json::number(s.position.x) << ", "
          << json::number(s.position.y) << "] }";
        if (i + 1 < tr.sampleCount()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

inline LoadResult loadTrajectory(const std::string& text, Trajectory& out) {
    LoadResult res;
    const json::ParseResult pr = json::parse(text);
    if (!pr.ok) {
        res.error = "json: " + pr.error + " at byte "
                  + std::to_string(pr.offset);
        return res;
    }
    const json::Value& root = pr.value;
    if (!root.isObject()) { res.error = "root is not an object"; return res; }
    if (!root.has("version")
        || static_cast<int>(root["version"].asNumber()) != kFormatVersion) {
        res.error = "missing or unsupported version";
        return res;
    }
    if (root.has("space") && root["space"].asString() != "normalized") {
        res.error = "unsupported space: " + root["space"].asString();
        return res;
    }
    if (!root.has("samples") || !root["samples"].isArray()) {
        res.error = "missing samples array";
        return res;
    }

    std::vector<TrajectorySample> samples;
    float last = -1.0f;
    for (const json::Value& s : root["samples"].asArray()) {
        if (!s.isObject() || !s["p"].isArray()
            || s["p"].asArray().size() != 2) {
            res.error = "sample needs a two-element p";
            return res;
        }
        const float t = s["t"].asFloat();
        // Non-monotonic time would make pointAt's binary search meaningless,
        // and the failure would look like a jittering playhead rather than a
        // bad file.
        if (t < last) {
            res.error = "samples are not in non-decreasing time order";
            return res;
        }
        last = t;
        samples.push_back(TrajectorySample{
            t, glm::vec2(s["p"].asArray()[0].asFloat(),
                         s["p"].asArray()[1].asFloat())});
    }

    const float dur = root.has("duration") ? root["duration"].asFloat() : 0.0f;
    if (dur < 0.0f) {
        res.error = "duration is negative";
        return res;
    }
    out.setSamples(std::move(samples), dur);
    res.ok = true;
    return res;
}

} // namespace io
} // namespace ofxManifold
