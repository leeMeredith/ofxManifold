#pragma once

// ofxManifold — several maps, one room (PLAN-round3.md decisions B, C, E).
//
// A RIG is the room: every channel that leaves it. It is a Mapping with
// outputs and no bindings (rigOf() in ofxManifoldMapping.h makes one from a
// map), so nothing here needs a second kind of object. What lives here is
// what happens when SEVERAL maps play in one room:
//
//   blendMaps()       any number of maps, each with its weights and a gain,
//                     blended by output NAME -- blendByName() for N maps
//   rigChannels()     the blend turned into the rig's channel vector: trims,
//   rigLevels()       the derived outputs fed by outputs computed ONCE from
//                     the blend, the channels maps fill taken as blended
//   checkAgreement()  where maps meant for one room disagree about it
//   useRig()          a map moved onto a rig: the rig's outputs, channels and
//                     trims, bindings following by name
//
// No meaning lives here (Boundaries, PLAN-round3.md). Gains are the caller's,
// so is the curve, and the default curve is linear, as blend()'s is. A player
// driving audio chooses equal power; the kernel does not choose for it.

#include "ofxManifoldMapping.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {

// One map in a blend: the mapping, the weight vector its manifold gave at the
// listening point, and its gain -- from a slider, a map of maps, or a 3D
// map's levels.
struct MapInput {
    const Mapping* mapping = nullptr;
    WeightVector   weights;
    float          gain = 0.0f;
};

// The blend, by name, before any trim.
//
//   shares   what reached each OUTPUT
//   filled   the value of each channel a map FILLS -- a derived output fed by
//            nodes. Blended like a share: the rig lists the channel (decided
//            in 3A-1), each map supplies its value
//   silence  what reached no output, blended -- the chart's silence bar
//
// A name absent from a list has no share. Zeros are dropped, as blend() and
// blendByName() drop them.
struct Blended {
    std::vector<NamedWeight> shares;
    std::vector<NamedWeight> filled;
    float                    silence = 0.0f;
};

namespace detail {

inline void addNamed(std::vector<NamedWeight>& v, const std::string& name,
                     float w) {
    for (auto& nw : v) {
        if (nw.target == name) { nw.weight += w; return; }
    }
    v.push_back(NamedWeight{name, w});
}

inline void dropZeros(std::vector<NamedWeight>& v) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const NamedWeight& o) {
                               return std::fabs(o.weight) < 1e-9f;
                           }),
            v.end());
}

inline float namedValue(const std::vector<NamedWeight>& v,
                        const std::string& name) {
    for (const auto& nw : v) if (nw.target == name) return nw.weight;
    return 0.0f;
}

} // namespace detail

// Blend any number of maps by output name.
//
// Each gain is clamped to [0, 1] and passed through `fn`, exactly as
// blendByName() treats 1 - t and t -- so two maps at gains 1 - t and t give
// what blendByName() gives, checked by vector: the old function is the
// two-map case of this one. Gains need not sum to one; that is the caller's.
//
// Merging is by NAME because TargetIDs, like NodeIDs, are per-map indices
// (D-013). Whether the maps agree about what a name means is a separate
// question -- checkAgreement() -- asked once, when maps are loaded together,
// not on every frame.
inline Blended blendMaps(const std::vector<MapInput>& in,
                         curve::Fn fn = curve::linear) {
    Blended b;
    for (const MapInput& mi : in) {
        if (!mi.mapping) continue;
        float g = mi.gain;
        if (g < 0.0f) g = 0.0f;
        if (g > 1.0f) g = 1.0f;
        g = fn(g);
        const Mapping& m = *mi.mapping;
        const Resolved r = m.resolve(mi.weights);
        for (const auto& wt : r.targets) {
            detail::addNamed(b.shares, m.targetName(wt.id), wt.weight * g);
        }
        const auto& ags = m.aggregators();
        for (std::size_t a = 0; a < ags.size(); ++a) {
            if (ags[a].feed != Feed::Nodes) continue;
            detail::addNamed(b.filled, ags[a].name, r.aggregates[a] * g);
        }
        b.silence += m.silenceShare(mi.weights) * g;
    }
    detail::dropZeros(b.shares);
    detail::dropZeros(b.filled);
    return b;
}

namespace detail {

inline std::vector<float> rigVector(const Mapping& rig, const Blended& b,
                                    bool levels) {
    int top = -1;
    for (TargetID t = 0; t < rig.targetCount(); ++t) {
        top = std::max(top, rig.targetChannel(t));
    }
    for (const auto& ag : rig.aggregators()) top = std::max(top, ag.channel);
    std::vector<float> out(static_cast<std::size_t>(top + 1), 0.0f);
    if (top < 0) return out;

    // What each output SENDS: its blended share times the rig's trim. The
    // derived outputs fed by outputs read this, once, after the blend -- so a
    // sub follows the mix of the maps, not a mix of each map's own sub.
    std::vector<float> sent(rig.targetCount(), 0.0f);
    for (TargetID t = 0; t < rig.targetCount(); ++t) {
        const float s = namedValue(b.shares, rig.targetName(t));
        sent[t] = s * rig.targetTrim(t);
        out[static_cast<std::size_t>(rig.targetChannel(t))] =
            levels ? sent[t] : s;
    }
    for (const auto& ag : rig.aggregators()) {
        if (ag.channel < 0) continue;
        float v = 0.0f;
        if (ag.feed == Feed::Outputs) {
            for (std::size_t s = 0; s < ag.outputs.size(); ++s) {
                if (ag.outputs[s] >= sent.size()) continue;
                const float g = s < ag.weights.size() ? ag.weights[s] : 1.0f;
                const float x = g * sent[ag.outputs[s]];
                v += (ag.mode == SumMode::Linear) ? x : x * x;
            }
            if (ag.mode == SumMode::PowerPreserving) {
                v = (v > 0.0f) ? std::sqrt(v) : 0.0f;
            }
        } else {
            v = namedValue(b.filled, ag.name);
        }
        out[static_cast<std::size_t>(ag.channel)] =
            v * (levels ? ag.trim : 1.0f);
    }
    return out;
}

} // namespace detail

// The blend as the rig's channel vector, BEFORE each channel's own trim --
// the same reading Mapping::toChannels() gives for one map. Indexed by
// channel exactly, zeros in the gaps, the rig's arity.
inline std::vector<float> rigChannels(const Mapping& rig, const Blended& b) {
    return detail::rigVector(rig, b, false);
}

// The same AFTER trim: what actually leaves the room. For ONE map at gain 1
// through its own rig this is exactly that map's toChannelLevels(), checked
// by vector.
inline std::vector<float> rigLevels(const Mapping& rig, const Blended& b) {
    return detail::rigVector(rig, b, true);
}

// ---- agreement --------------------------------------------------------------

// Something maps meant for one room disagree about.
//
//   code "channel"  one name on two channels
//        "shared"   two names on one channel (name holds the channel number)
//        "feed"     one name and channel, computed from outputs in one and
//                   filled by the map in another -- or an output in one and a
//                   derived output in another
//        "missing"  a channel a map sends that the rig does not have
//        "trim"     a different trim: a NOTE, not a disagreement, since in a
//                   blend the rig's trim is used
//
// `message` names the maps by position -- "map 1", "map 2", "the rig" -- for
// a caller to show; whatever loads the maps knows their file names.
struct Disagreement {
    bool        note = false;
    std::string code;
    std::string name;
    std::string message;
};

namespace detail {

enum class EntryKind { Output, After, Filled };

struct Entry {
    std::string name;
    int         channel = NoChannel;
    float       trim    = 1.0f;
    EntryKind   kind    = EntryKind::Output;
};

// Every channel a mapping sends into the room. A derived output with no
// channel never leaves the room, so it is the map's alone and is not here.
inline std::vector<Entry> entriesOf(const Mapping& m) {
    std::vector<Entry> e;
    for (TargetID t = 0; t < m.targetCount(); ++t) {
        e.push_back({m.targetName(t), m.targetChannel(t), m.targetTrim(t),
                     EntryKind::Output});
    }
    for (const auto& ag : m.aggregators()) {
        if (ag.channel == NoChannel) continue;
        e.push_back({ag.name, ag.channel, ag.trim,
                     ag.feed == Feed::Outputs ? EntryKind::After
                                              : EntryKind::Filled});
    }
    return e;
}

inline const char* kindWords(EntryKind k) {
    switch (k) {
    case EntryKind::Output: return "an output";
    case EntryKind::After:  return "computed from outputs";
    case EntryKind::Filled: return "filled by the map";
    }
    return "?";
}

inline void report(std::vector<Disagreement>& found, bool note,
                   const std::string& code, const std::string& name,
                   const std::string& message) {
    for (const auto& d : found) {
        if (d.note == note && d.code == code && d.name == name) return;
    }
    found.push_back(Disagreement{note, code, name, message});
}

} // namespace detail

// Where `maps`, and the rig if given, disagree about the room.
//
// Compared over EVERY PAIR, not each map against the first: maps 1 and 2 can
// agree while 2 and 3 do not, and a check against map 1 alone passes that.
// One report per (kind, code, name), the first found, so three maps that all
// move R produce one line about R rather than three. Disagreements first,
// then notes, each sorted by code and name, so the result does not depend on
// the order the maps were given in.
inline std::vector<Disagreement> checkAgreement(
        const std::vector<const Mapping*>& maps, const Mapping* rig = nullptr) {
    using detail::Entry;
    std::vector<std::vector<Entry>> parts;
    std::vector<std::string> label;
    for (std::size_t i = 0; i < maps.size(); ++i) {
        if (!maps[i]) continue;
        parts.push_back(detail::entriesOf(*maps[i]));
        label.push_back("map " + std::to_string(i + 1));
    }
    if (rig) {
        parts.push_back(detail::entriesOf(*rig));
        label.push_back("the rig");
    }

    std::vector<Disagreement> found;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        for (std::size_t j = i + 1; j < parts.size(); ++j) {
            for (const Entry& a : parts[i]) {
                for (const Entry& b : parts[j]) {
                    if (a.name == b.name) {
                        if (a.channel != b.channel) {
                            detail::report(found, false, "channel", a.name,
                                a.name + " is on channel "
                                + std::to_string(a.channel) + " in " + label[i]
                                + " and channel " + std::to_string(b.channel)
                                + " in " + label[j]);
                        } else if (a.kind != b.kind) {
                            detail::report(found, false, "feed", a.name,
                                a.name + " is " + detail::kindWords(a.kind)
                                + " in " + label[i] + " and "
                                + detail::kindWords(b.kind) + " in "
                                + label[j]);
                        } else if (std::fabs(a.trim - b.trim) > 1e-9f) {
                            detail::report(found, true, "trim", a.name,
                                a.name + " is trimmed differently in "
                                + label[i] + " and " + label[j]);
                        }
                    } else if (a.channel == b.channel) {
                        const std::string ch = std::to_string(a.channel);
                        detail::report(found, false, "shared", ch,
                            "channel " + ch + " is " + a.name + " in "
                            + label[i] + " and " + b.name + " in "
                            + label[j]);
                    }
                }
            }
        }
    }
    if (rig) {
        const std::vector<Entry>& inRig = parts.back();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            for (const Entry& a : parts[i]) {
                bool there = false;
                for (const Entry& r : inRig) {
                    if (r.name == a.name) { there = true; break; }
                }
                if (!there) {
                    detail::report(found, false, "missing", a.name,
                        a.name + " (" + label[i] + ") is not in the rig");
                }
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const Disagreement& x, const Disagreement& y) {
                  if (x.note != y.note) return !x.note;
                  if (x.code != y.code) return x.code < y.code;
                  return x.name < y.name;
              });
    return found;
}

// ---- using a rig --------------------------------------------------------------

// The result of moving a map onto a rig.
struct RigUse {
    bool                     ok = false;
    std::vector<std::string> refused;   // sorted; empty when ok
    Mapping                  result;    // the map on the rig, when ok
};

// Move a map onto a rig (the editor's cmd-r).
//
// The result has the rig's outputs, in the rig's order, with its channels and
// trims, and the rig's derived outputs fed by outputs; bindings follow by
// name, silence shares included; each channel the map fills keeps the map's
// own sources and mode at the rig's channel and trim. A channel the rig lists
// as filled that this map does not fill is there, empty. A derived output
// with no channel is the map's alone and is kept as it was.
//
// REFUSED, naming them, for anything the map sends that the rig does not
// have, or has as a different kind -- an output the rig computes, a sub the
// map fills that the rig computes from outputs. Nothing is dropped without a
// word, and the map is not touched: the result is a new mapping.
inline RigUse useRig(const Mapping& map, const Mapping& rig) {
    RigUse u;
    const std::vector<detail::Entry> mine = detail::entriesOf(map);
    const std::vector<detail::Entry> room = detail::entriesOf(rig);
    for (const auto& e : mine) {
        bool match = false;
        for (const auto& r : room) {
            if (r.name == e.name && r.kind == e.kind) { match = true; break; }
        }
        if (!match) u.refused.push_back(e.name);
    }
    std::sort(u.refused.begin(), u.refused.end());
    u.refused.erase(std::unique(u.refused.begin(), u.refused.end()),
                    u.refused.end());
    if (!u.refused.empty()) return u;

    Mapping out;
    for (TargetID t = 0; t < rig.targetCount(); ++t) {
        out.addOutput(rig.targetName(t), rig.targetChannel(t),
                      rig.targetTrim(t));
    }
    for (NodeID n : map.boundNodes()) {
        for (std::size_t k = 0; k < map.linkCount(n); ++k) {
            const Link& l = map.link(n, k);
            if (l.kind == DestKind::Silence) {
                out.bindSilence(n, l.weight);
            } else {
                out.bind(n, out.findTarget(map.targetName(l.id)), l.weight);
            }
        }
    }
    for (const auto& ag : rig.aggregators()) {
        if (ag.feed == Feed::Outputs) {
            // The rig's TargetIDs are the result's: outputs went in in the
            // rig's order.
            std::vector<std::pair<TargetID, float>> srcs;
            for (std::size_t i = 0; i < ag.outputs.size(); ++i) {
                srcs.emplace_back(ag.outputs[i],
                                  i < ag.weights.size() ? ag.weights[i] : 1.0f);
            }
            out.addDerivedFromOutputs(ag.name, ag.channel, ag.trim, ag.mode,
                                      srcs);
            continue;
        }
        const Aggregator* fill = nullptr;
        for (const auto& m : map.aggregators()) {
            if (m.feed == Feed::Nodes && m.channel != NoChannel
                && m.name == ag.name) { fill = &m; break; }
        }
        std::vector<std::pair<NodeID, float>> srcs;
        if (fill) {
            for (std::size_t i = 0; i < fill->sources.size(); ++i) {
                srcs.emplace_back(fill->sources[i],
                                  i < fill->weights.size() ? fill->weights[i]
                                                           : 1.0f);
            }
        }
        out.addDerived(ag.name, ag.channel, ag.trim,
                       fill ? fill->mode : ag.mode, srcs);
    }
    for (const auto& m : map.aggregators()) {
        if (m.feed != Feed::Nodes || m.channel != NoChannel) continue;
        std::vector<std::pair<NodeID, float>> srcs;
        for (std::size_t i = 0; i < m.sources.size(); ++i) {
            srcs.emplace_back(m.sources[i],
                              i < m.weights.size() ? m.weights[i] : 1.0f);
        }
        out.addDerived(m.name, NoChannel, m.trim, m.mode, srcs);
    }
    u.ok = true;
    u.result = std::move(out);
    return u;
}

} // namespace ofxManifold
