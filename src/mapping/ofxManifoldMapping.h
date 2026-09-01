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

namespace ofxManifold {

using TargetID = std::uint32_t;
static constexpr TargetID InvalidTarget =
    std::numeric_limits<TargetID>::max();

struct WeightedTarget {
    TargetID id     = InvalidTarget;
    float    weight = 0.0f;
};

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
struct Aggregator {
    std::string         name;
    std::vector<NodeID> sources;
    SumMode             mode = SumMode::Linear;
};

struct Resolved {
    std::vector<WeightedTarget> targets;
    std::vector<float>          aggregates;   // parallel to aggregators()
};

class Mapping {
public:
    // ---- targets --------------------------------------------------------

    TargetID addTarget(const std::string& name) {
        auto it = targetsByName_.find(name);
        if (it != targetsByName_.end()) return it->second;
        const TargetID id = static_cast<TargetID>(targetNames_.size());
        targetNames_.push_back(name);
        targetsByName_[name] = id;
        return id;
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
            if (l.id == target) { l.weight += weight; return; }
        }
        links.push_back(WeightedTarget{target, weight});
    }

    void unbind(NodeID node, TargetID target) {
        auto it = links_.find(node);
        if (it == links_.end()) return;
        auto& v = it->second;
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const WeightedTarget& l) {
                                   return l.id == target;
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
    const WeightedTarget& link(NodeID node, std::size_t k) const {
        return links_.find(node)->second[k];
    }

    // ---- aggregators ----------------------------------------------------

    std::size_t addAggregator(const std::string& name,
                              std::vector<NodeID> sources,
                              SumMode mode = SumMode::Linear) {
        aggregators_.push_back(Aggregator{name, std::move(sources), mode});
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
                if (l.id < acc.size()) {
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
            float v = 0.0f;
            for (NodeID src : ag.sources) {
                for (const auto& wn : w) {
                    if (wn.id != src) continue;
                    if (ag.mode == SumMode::Linear) v += wn.weight;
                    else                            v += wn.weight * wn.weight;
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

private:
    std::vector<std::string>                 targetNames_;
    std::unordered_map<std::string, TargetID> targetsByName_;
    std::unordered_map<NodeID, std::vector<WeightedTarget>> links_;
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
