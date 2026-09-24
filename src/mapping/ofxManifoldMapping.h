#pragma once

// ofxManifold — mapping.
//
// Resolves node identity onto external targets. This is the ONLY layer that
// knows what anything means (architecture doc section 10).
//
// The relation is many-to-many, and that is not a generalization for its own
// sake -- it is what the source material requires:
//
//   * A node may bind to zero targets. MIAP calls this a silent node; SpaceMap
//     added them because sound dragged off the map edge cut out abruptly, and
//     ringing the map with them makes fade-to-nothing ordinary barycentric
//     behaviour rather than a special case.
//
//   * A node may bind to many targets. MIAP calls this a virtual node. It was
//     introduced when it became clear not every rig has an overhead speaker:
//     remove the overhead node, put a virtual one there, and dragging sound
//     overhead feeds the four corners instead.
//
//   * Several nodes may bind to the SAME target. MIAP states this directly, and
//     it follows from the map not being a projection of real space: two nodes
//     adjacent on the map may be the two most distant outputs in the room.
//
// Because those three collapse to "node -> weighted target set" with cardinality
// zero, one, or many, the kernel needs no node type field at all. Manifold2D
// cannot tell a terminal node from a null one, and does not need to.

#include "../interpretation/ofxManifoldCurves.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace ofxManifold {

using TargetID = std::uint32_t;
static constexpr TargetID InvalidTarget =
    std::numeric_limits<TargetID>::max();

struct WeightedTarget {
    TargetID id     = InvalidTarget;
    float    weight = 0.0f;
};

// Where a binding sends its share (PLAN-outputs.md decision B).
//
// Outputs, silence and -- later -- layers all answer one question: where does
// a node's share go? The kind is recorded from the start so adding layers does
// not change the binding structure, or the file format, under maps already
// saved in it. Layer is reserved and not accepted yet.
enum class DestKind : std::uint8_t {
    Output,
    Silence
};

struct Link {
    DestKind kind   = DestKind::Output;
    TargetID id     = InvalidTarget;   // unused for silence
    float    weight = 0.0f;
};

// A node's role, COMPUTED from its bindings and never stored (decision F). A
// stored type that could disagree with the actual bindings would be the
// D-018/019/020 pattern a fourth time.
enum class NodeKind {
    Null,        // bound to nothing, or to silence alone
    Terminal,    // one output
    Composite,   // several outputs
    Partial      // at least one output, and a share to silence
};

// No channel. A derived output made the old way, with addAggregator(), has
// none and does not appear in the channel vector -- exactly as before.
static constexpr int NoChannel = -1;

// How an aggregator combines the node weights it watches.
enum class SumMode {
    Linear,          // straight sum
    PowerPreserving  // root of the sum of squares
};

// A named reducer over a subset of the weight vector, evaluated AFTER the
// manifold (architecture doc 6.2).
//
// This is SpaceMap's derived node, and it runs the routing logic backwards:
// rather than distributing a signal among linked nodes, it receives their sum.
// It is not part of any region, and its position on a map exists only for the
// author's visual convenience.
//
// Which is exactly why it is not a node type. Modelling it as one would put a
// non-participating object into the geometry and force every region operation
// to filter it out. It lives here and reads the manifold's output.
//
// Each source carries a weight, as MIAP's derived links do -- a sub fed three
// parts front to one part rear. Source weights are GAINS on the sources, not
// shares, so they are not normalized. An aggregator made with addAggregator()
// has every weight 1 and resolves exactly as it always did.
struct Aggregator {
    std::string         name;
    std::vector<NodeID> sources;
    SumMode             mode = SumMode::Linear;
    std::vector<float>  weights;               // parallel to sources
    int                 channel = NoChannel;
    float               trim    = 1.0f;
};

struct Resolved {
    std::vector<WeightedTarget> targets;
    std::vector<float>          aggregates;   // parallel to aggregators()
};

class Mapping {
public:
    // ---- targets --------------------------------------------------------

    // An output made the old way: a name, nothing else. It gets channel =
    // its id, so for every existing mapping the channel vector is IDENTICAL
    // to toDenseVector(). If that channel is already taken by an explicit
    // output, the next free one above it.
    TargetID addTarget(const std::string& name) {
        auto it = targetsByName_.find(name);
        if (it != targetsByName_.end()) return it->second;
        const TargetID id = static_cast<TargetID>(targetNames_.size());
        int ch = static_cast<int>(id);
        while (channelInUse(ch)) ++ch;
        targetNames_.push_back(name);
        channels_.push_back(ch);
        trims_.push_back(1.0f);
        targetsByName_[name] = id;
        return id;
    }

    // An output with an explicit channel and trim (decision A).
    //
    // Refused -- InvalidTarget -- if the name exists, or the channel is
    // negative or already used by any output or derived output. Two outputs on
    // one channel would make the channel vector ambiguous: the kernel
    // accepting a state a consumer trips over.
    TargetID addOutput(const std::string& name, int channel,
                       float trim = 1.0f) {
        if (targetsByName_.count(name) != 0) return InvalidTarget;
        if (channel < 0 || channelInUse(channel)) return InvalidTarget;
        const TargetID id = static_cast<TargetID>(targetNames_.size());
        targetNames_.push_back(name);
        channels_.push_back(channel);
        trims_.push_back(trim);
        targetsByName_[name] = id;
        return id;
    }

    int   targetChannel(TargetID id) const { return channels_[id]; }
    float targetTrim(TargetID id) const { return trims_[id]; }
    void  setTargetTrim(TargetID id, float trim) {
        if (id < trims_.size()) trims_[id] = trim;
    }

    bool channelInUse(int ch) const {
        for (int c : channels_) if (c == ch) return true;
        for (const auto& a : aggregators_) if (a.channel == ch) return true;
        return false;
    }

    TargetID findTarget(const std::string& name) const {
        auto it = targetsByName_.find(name);
        return (it == targetsByName_.end()) ? InvalidTarget : it->second;
    }

    std::size_t targetCount() const { return targetNames_.size(); }
    const std::string& targetName(TargetID id) const {
        return targetNames_[id];
    }

    // ---- bindings -------------------------------------------------------

    // Repeatable. Zero calls for a node makes it null, one makes it terminal,
    // several make it composite. No type switch anywhere.
    //
    // Binding the same node to the same target twice accumulates the link
    // weight rather than replacing it, so a caller building bindings from two
    // sources does not silently lose one.
    void bind(NodeID node, TargetID target, float weight = 1.0f) {
        auto& links = links_[node];
        for (auto& l : links) {
            if (l.kind == DestKind::Output && l.id == target) {
                l.weight += weight;
                return;
            }
        }
        links.push_back(Link{DestKind::Output, target, weight});
    }

    // Send part of a node's share to silence (decision B).
    //
    // A silence link takes part in the within-node normalization and
    // contributes nowhere. Bound 0.7 to an output and 0.3 to silence, a node
    // sends 70% of its share and discards 30% -- MIAP's virtual-to-silent
    // link, the partial fade ofxManifold could not express. Repeated calls
    // accumulate, as bind() does.
    void bindSilence(NodeID node, float weight) {
        auto& links = links_[node];
        for (auto& l : links) {
            if (l.kind == DestKind::Silence) { l.weight += weight; return; }
        }
        links.push_back(Link{DestKind::Silence, InvalidTarget, weight});
    }

    void unbind(NodeID node, TargetID target) {
        auto it = links_.find(node);
        if (it == links_.end()) return;
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const Link& l) {
                                   return l.kind == DestKind::Output
                                       && l.id == target;
                               }),
                v.end());
    }

    std::size_t linkCount(NodeID node) const {
        auto it = links_.find(node);
        return (it == links_.end()) ? 0 : it->second.size();
    }

    // Ordered access to a node's links. Serialization needs a deterministic
    // order so that save -> load -> save is byte-stable; the insertion order
    // held in the vector provides it, where iterating the map would not.
    const Link& link(NodeID node, std::size_t k) const {
        return links_.find(node)->second[k];
    }

    // The role a node plays, and how much of its share reaches an output --
    // what its shape and its fill show in the editor.
    NodeKind nodeKind(NodeID node) const {
        auto it = links_.find(node);
        if (it == links_.end()) return NodeKind::Null;
        std::size_t outs = 0;
        float total = 0.0f, silent = 0.0f;
        for (const auto& l : it->second) {
            total += l.weight;
            if (l.kind == DestKind::Output) ++outs;
            else silent += l.weight;
        }
        if (outs == 0 || std::fabs(total) < 1e-9f) return NodeKind::Null;
        if (silent > 1e-9f) return NodeKind::Partial;
        return outs == 1 ? NodeKind::Terminal : NodeKind::Composite;
    }

    float outputFraction(NodeID node) const {
        auto it = links_.find(node);
        if (it == links_.end()) return 0.0f;
        float total = 0.0f, silent = 0.0f;
        bool anyOut = false;
        for (const auto& l : it->second) {
            total += l.weight;
            if (l.kind == DestKind::Silence) silent += l.weight;
            else anyOut = true;
        }
        if (!anyOut || std::fabs(total) < 1e-9f) return 0.0f;
        return 1.0f - silent / total;
    }

    // ---- aggregators ----------------------------------------------------

    std::size_t addAggregator(const std::string& name,
                              std::vector<NodeID> sources,
                              SumMode mode = SumMode::Linear) {
        Aggregator a;
        a.name = name;
        a.weights.assign(sources.size(), 1.0f);
        a.sources = std::move(sources);
        a.mode = mode;
        aggregators_.push_back(std::move(a));
        return aggregators_.size() - 1;
    }

    // A derived output: weighted sources, a channel and a trim (decision C).
    // It appears in the channel vector at its channel. `channel` may be
    // NoChannel; any other negative channel, or one already in use, is
    // refused and returns SIZE_MAX.
    std::size_t addDerived(const std::string& name, int channel, float trim,
                           SumMode mode,
                           const std::vector<std::pair<NodeID, float>>& srcs) {
        if (channel != NoChannel && (channel < 0 || channelInUse(channel))) {
            return static_cast<std::size_t>(-1);
        }
        Aggregator a;
        a.name = name;
        a.mode = mode;
        a.channel = channel;
        a.trim = trim;
        for (const auto& sw : srcs) {
            a.sources.push_back(sw.first);
            a.weights.push_back(sw.second);
        }
        aggregators_.push_back(std::move(a));
        return aggregators_.size() - 1;
    }

    const std::vector<Aggregator>& aggregators() const { return aggregators_; }

    // ---- resolution -----------------------------------------------------

    // Weight vector -> target weights and aggregate values.
    //
    // A node's share is divided among its links in proportion to their link
    // weights, normalized within the node. So a composite node bound to four
    // targets with equal links sends a quarter of its weight to each, and the
    // node's total contribution is unchanged by how many targets it feeds.
    //
    // The division is LINEAR, not power-preserving. Same layer split as
    // spread(): power is the curve layer's business, and a caller who wants
    // constant power applies a curve. Doing it here would leave a caller who
    // wants linear behaviour no way back.
    //
    // Note what a null node does to the total: its weight is discarded, so the
    // resolved target weights sum to LESS than one whenever the point is near
    // one. That shortfall is the fade, and it is the intended behaviour rather
    // than a leak.
    Resolved resolve(const WeightVector& w) const {
        Resolved out;
        std::vector<float> acc(targetNames_.size(), 0.0f);

        for (const auto& wn : w) {
            auto it = links_.find(wn.id);
            if (it == links_.end() || it->second.empty()) {
                continue;                   // null node: weight discarded
            }
            float linkTotal = 0.0f;
            for (const auto& l : it->second) linkTotal += l.weight;
            if (std::fabs(linkTotal) < 1e-9f) continue;

            for (const auto& l : it->second) {
                // Silence counted in linkTotal above, and sent nowhere.
                if (l.kind == DestKind::Output && l.id < acc.size()) {
                    acc[l.id] += wn.weight * (l.weight / linkTotal);
                }
            }
        }

        for (TargetID t = 0; t < acc.size(); ++t) {
            if (std::fabs(acc[t]) >= 1e-9f) {
                out.targets.push_back(WeightedTarget{t, acc[t]});
            }
        }

        out.aggregates.reserve(aggregators_.size());
        for (const auto& ag : aggregators_) {
            // Source weight applied to the AMPLITUDE, then summed or
            // power-summed. With every weight 1 this is exactly the old sum.
            float v = 0.0f;
            for (std::size_t s = 0; s < ag.sources.size(); ++s) {
                const NodeID src = ag.sources[s];
                const float g = (s < ag.weights.size()) ? ag.weights[s] : 1.0f;
                for (const auto& wn : w) {
                    if (wn.id != src) continue;
                    const float a = g * wn.weight;
                    if (ag.mode == SumMode::Linear) v += a;
                    else                            v += a * a;
                    break;
                }
            }
            if (ag.mode == SumMode::PowerPreserving) {
                v = (v <= 0.0f) ? 0.0f : std::sqrt(v);
            }
            out.aggregates.push_back(v);
        }
        return out;
    }

    // Fixed-arity output, indexed by TargetID, including targets with no
    // contribution this frame.
    //
    // OSC consumers need this. A sparse vector whose length changes as the
    // point moves would shift every array index downstream, so a receiver
    // reading argument 3 would be reading a different output from one frame to
    // the next. Arity is a property of the mapping, not of the point.
    std::vector<float> toDenseVector(const WeightVector& w) const {
        std::vector<float> dense(targetNames_.size(), 0.0f);
        const Resolved r = resolve(w);
        for (const auto& t : r.targets) {
            if (t.id < dense.size()) dense[t.id] = t.weight;
        }
        return dense;
    }

    // ---- channels (decisions A and D) ---------------------------------

    // Indexed by channel number EXACTLY: length highest channel + 1, zeros in
    // the gaps, derived outputs at their channels. Routed share, BEFORE trim.
    // For a mapping built only with addTarget(), identical to toDenseVector().
    std::vector<float> toChannels(const WeightVector& w) const {
        return channelVector(w, false);
    }

    // The same, AFTER trim: what actually leaves. Trim is a gain on an output
    // after routing, unlike a node's weight, which biases the geometry before
    // renormalization. Keeping the two readings apart keeps outputs plus
    // silence summing to exactly one up to the last possible moment.
    std::vector<float> toChannelLevels(const WeightVector& w) const {
        return channelVector(w, true);
    }

    // The share that reached no output: null nodes whole, plus every node's
    // silence links. Routed share plus this is exactly the weight vector's
    // total -- the bar chart's silence bar.
    float silenceShare(const WeightVector& w) const {
        float s = 0.0f;
        for (const auto& wn : w) {
            auto it = links_.find(wn.id);
            if (it == links_.end() || it->second.empty()) {
                s += wn.weight;
                continue;
            }
            float total = 0.0f, silent = 0.0f;
            for (const auto& l : it->second) {
                total += l.weight;
                if (l.kind == DestKind::Silence) silent += l.weight;
            }
            if (std::fabs(total) < 1e-9f) { s += wn.weight; continue; }
            s += wn.weight * (silent / total);
        }
        return s;
    }

    // Follow a node renumbering (decision E).
    //
    // `remap` is the table Manifold2D::removeNodes() returns: for every OLD
    // NodeID, the new one, or InvalidNode if it was removed. Bindings of
    // removed nodes are dropped; every other binding moves to its node's new
    // id; aggregator sources likewise, with their weights. Without this,
    // deleting a node would silently re-route every binding after it to the
    // wrong node -- D-020's rule, applied to the first thing that holds
    // NodeIDs across a removal.
    void remapNodes(const std::vector<NodeID>& remap) {
        std::unordered_map<NodeID, std::vector<Link>> moved;
        for (auto& kv : links_) {
            if (kv.first >= remap.size()) continue;
            const NodeID to = remap[kv.first];
            if (to == InvalidNode) continue;
            moved[to] = std::move(kv.second);
        }
        links_ = std::move(moved);
        for (auto& ag : aggregators_) {
            std::vector<NodeID> s;
            std::vector<float>  g;
            for (std::size_t i = 0; i < ag.sources.size(); ++i) {
                const NodeID old = ag.sources[i];
                if (old >= remap.size() || remap[old] == InvalidNode) continue;
                s.push_back(remap[old]);
                g.push_back(i < ag.weights.size() ? ag.weights[i] : 1.0f);
            }
            ag.sources = std::move(s);
            ag.weights = std::move(g);
        }
    }

    // All nodes with bindings, in ascending order: what a serializer walks.
    std::vector<NodeID> boundNodes() const {
        std::vector<NodeID> n;
        for (const auto& kv : links_) n.push_back(kv.first);
        std::sort(n.begin(), n.end());
        return n;
    }

private:
    std::vector<float> channelVector(const WeightVector& w,
                                     bool levels) const {
        int top = -1;
        for (int c : channels_) top = std::max(top, c);
        for (const auto& a : aggregators_) top = std::max(top, a.channel);
        std::vector<float> out(static_cast<std::size_t>(top + 1), 0.0f);
        if (top < 0) return out;

        const Resolved r = resolve(w);
        for (const auto& t : r.targets) {
            if (t.id >= channels_.size()) continue;
            out[static_cast<std::size_t>(channels_[t.id])] =
                t.weight * (levels ? trims_[t.id] : 1.0f);
        }
        for (std::size_t a = 0; a < aggregators_.size(); ++a) {
            const int ch = aggregators_[a].channel;
            if (ch < 0) continue;
            out[static_cast<std::size_t>(ch)] =
                r.aggregates[a] * (levels ? aggregators_[a].trim : 1.0f);
        }
        return out;
    }

    std::vector<std::string>                 targetNames_;
    std::vector<int>                         channels_;
    std::vector<float>                       trims_;
    std::unordered_map<std::string, TargetID> targetsByName_;
    std::unordered_map<NodeID, std::vector<Link>> links_;
    std::vector<Aggregator>                  aggregators_;
};

// ---- blending across manifolds -------------------------------------------

// A target weight carrying its NAME rather than an id.
//
// TargetIDs are per-Mapping indices, exactly as NodeIDs are per-manifold. Two
// mappings that declared their targets in different orders give the same name
// different ids, so anything crossing between them has to travel by name.
struct NamedWeight {
    std::string target;
    float       weight = 0.0f;
};

// Crossfade between two manifolds.
//
// This is the operation MIAP performs when it holds two maps and interpolates
// between them, and it is the historical answer to three dimensions: several
// concurrent 2D maps combined downstream, rather than tetrahedra (architecture
// doc 9.3).
//
// It lives here rather than beside blend() because a crossfade between two
// maps is really a crossfade between what they DRIVE. The maps have no nodes
// in common -- if they did they would be one map -- so there is nothing to
// merge at the node level. What they share is outputs, and outputs are named.
//
// Each side is resolved through its own Mapping first, so a node's share has
// already been distributed to targets before the two are combined. A composite
// node in map A and a terminal node in map B can therefore both feed "out.3"
// and their contributions add, which is correct and is not expressible at the
// node level at all.
inline std::vector<NamedWeight> blendByName(
        const Resolved& a, const Mapping& ma,
        const Resolved& b, const Mapping& mb,
        float t, curve::Fn fn = curve::linear) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const float ga = fn(1.0f - t);
    const float gb = fn(t);

    std::vector<NamedWeight> out;
    out.reserve(a.targets.size() + b.targets.size());

    for (const auto& wt : a.targets) {
        out.push_back(NamedWeight{ma.targetName(wt.id), wt.weight * ga});
    }
    for (const auto& wt : b.targets) {
        const std::string& name = mb.targetName(wt.id);
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const NamedWeight& o) {
                                   return o.target == name;
                               });
        if (it != out.end()) it->weight += wt.weight * gb;
        else out.push_back(NamedWeight{name, wt.weight * gb});
    }

    // Same convention as blend() and evaluate(): a target with no share is
    // absent rather than present at zero.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const NamedWeight& o) {
                                 return std::fabs(o.weight) < 1e-9f;
                             }),
              out.end());
    return out;
}

} // namespace ofxManifold
