#pragma once

// ofxManifold — Manifold2D.
//
// Owns nodes and regions, finds the region containing a point, and reports
// topology faults. No openFrameworks dependency; glm only.
//
// What this class does NOT do, deliberately:
//   - cache anything derived from node positions (architecture doc 8.6)
//   - hold per-source evaluation state; that is Evaluator's job (8.1)
//   - interpret weights in any way; that is the interpretation layer (9)

#include "ofxManifoldRegion.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ofxManifold {

// Distance from a node to an edge, below which the node is considered to lie
// ON that edge for T-junction purposes.
//
// Distance is |cross| / edgeLength. For normalized coordinates the cross
// product carries float rounding noise near 1e-8 (see DECISIONS.md D-001), so
// a tolerance three orders above that is meaningful rather than noise. In
// normalized space 1e-5 is one hundred-thousandth of the control space: a node
// that close to an edge is on it for every practical purpose.
static constexpr float kCollinearEpsilon = 1e-5f;

// Below this, a per-node bias is treated as annihilating its component: the
// forward map destroyed the information and positionOf() cannot recover it.
static constexpr float kBiasEpsilon = 1e-6f;

// How far a weight vector may stray from summing to one before the inverse
// stops calling itself well posed. Looser than kEdgeEpsilon because these
// weights have been through a normalization and a bias, each rounding.
static constexpr float kWeightSumEpsilon = 1e-4f;

// A node that lies on the interior of an edge it is not a vertex of. Weights
// jump discontinuously as the point crosses such an edge, because the edge is
// shared by position but not by topology.
struct TJunction {
    NodeID   node = InvalidNode;   // the offending node
    RegionID region = InvalidRegion;
    NodeID   edgeA = InvalidNode;  // the edge it sits on
    NodeID   edgeB = InvalidNode;
};

struct TopologyReport {
    std::vector<TJunction> tJunctions;
    std::vector<NodeID>    orphans;      // in no region at all
    std::vector<RegionID>  duplicates;   // same nodes as an earlier region

    // INFORMATION, not a fault, and deliberately ignored by clean(). A star is
    // a legitimate control surface and MVC handles it (DECISIONS.md D-016b (D-C)).
    // Listed because weights CAN go negative inside a non-convex region, and
    // an author is better off knowing which parts of a map can do that before
    // a performance than after.
    std::vector<RegionID>  nonConvex;

    bool clean() const {
        return tJunctions.empty() && orphans.empty() && duplicates.empty();
    }
};

class Manifold2D {
public:
    // ---- construction ---------------------------------------------------

    NodeID addNode(const std::string& name, glm::vec2 position,
                   float weight = 1.0f) {
        const NodeID id = static_cast<NodeID>(nodes_.size());
        nodes_.push_back(Node{name, position, weight});
        nodeRegions_.emplace_back();
        byName_[name] = id;
        return id;
    }

    // A region is an ordered ring of N >= 3 nodes. Returns InvalidRegion if
    // the ring is not constructible; lastRegionError() says why.
    //
    // Non-convex rings are ACCEPTED. A star is a legitimate control surface
    // and mean-value coordinates handle it (DECISIONS.md D-016b (D-C)).
    RegionID addRegion(std::vector<NodeID> ids) {
        lastError_.clear();
        for (NodeID id : ids) {
            if (!validNode(id)) {
                lastError_ = "unknown node";
                return InvalidRegion;
            }
        }
        std::vector<glm::vec2> p;
        p.reserve(ids.size());
        for (NodeID id : ids) p.push_back(nodes_[id].position);

        lastError_ = Region::invalidReason(ids, p);
        if (!lastError_.empty()) return InvalidRegion;

        Region r;
        if (!Region::make(ids, p, r)) {
            if (lastError_.empty()) lastError_ = "region rejected";
            return InvalidRegion;
        }
        regions_.push_back(std::move(r));
        const RegionID id = static_cast<RegionID>(regions_.size() - 1);

        // Incidence is derived from TOPOLOGY, not from positions, so caching
        // it does not violate the 8.6 rule against position-derived caches.
        // It is invalidated by addRegion alone, which is where it is built.
        for (NodeID n : regions_.back().ids()) nodeRegions_[n].push_back(id);
        return id;
    }

    // Kept for the common case, and because it reads better than a braced
    // list of three. Forwards to addRegion.
    RegionID addTriangle(NodeID a, NodeID b, NodeID c) {
        return addRegion({a, b, c});
    }

    const std::string& lastRegionError() const { return lastError_; }

    // ---- access ---------------------------------------------------------

    std::size_t nodeCount()   const { return nodes_.size(); }
    std::size_t regionCount() const { return regions_.size(); }

    const Node& node(NodeID id) const { return nodes_[id]; }
    const Region& region(RegionID id) const { return regions_[id]; }

    NodeID findNode(const std::string& name) const {
        auto it = byName_.find(name);
        return (it == byName_.end()) ? InvalidNode : it->second;
    }

    // ---- evaluation -----------------------------------------------------

    // Finds the region containing p and returns identity-retaining weights.
    //
    // Overlapping regions are permitted by design in an arbitrary node network
    // (architecture doc 8.3), so containment is not unique. First hit wins, in
    // insertion order, with the hint tested first. That is deterministic, and
    // in the hands of an Evaluator it is also hysteretic: a point stays in the
    // region it was already in.
    //
    // Outside the hull returns inside == false and an empty weight vector.
    // There is no clamping policy: the authoring convention is to ring the map
    // with null nodes so that fade-to-nothing is ordinary barycentric
    // behaviour (8.2).
    Evaluation evaluate(glm::vec2 p, RegionID hint = InvalidRegion) const {
        Evaluation e;

        if (hint < regions_.size() && containsRegion(hint, p)) {
            fill(e, hint, p);
            return e;
        }
        for (RegionID r = 0; r < regions_.size(); ++r) {
            if (r == hint) continue;          // already tested
            if (containsRegion(r, p)) {
                fill(e, r, p);
                return e;
            }
        }
        return e;                              // inside == false, empty
    }

    // ---- inverse evaluation ---------------------------------------------

    // A position recovered from a weight vector, plus whether the recovery was
    // meaningful (architecture doc 8.5).
    struct InversePosition {
        glm::vec2 position{0.0f, 0.0f};
        bool      wellPosed = false;
    };

    // Weights -> position. The inverse of evaluate().
    //
    // NOT simply the weighted sum of node positions. Forward evaluation applies
    // per-node bias and renormalizes, so the weights that come out are no
    // longer the barycentric coordinates of the point. Summing them directly
    // would land somewhere else entirely on any manifold with a bias other
    // than 1, and would do so silently.
    //
    // The bias is exactly invertible, because
    //
    //     b_i = raw_i * nw_i / SUM_j (raw_j * nw_j)
    //
    // so raw_i is proportional to b_i / nw_i, and renormalizing recovers it.
    // That inversion is undone here before the positions are combined, which
    // is what makes the round trip hold on a biased manifold.
    //
    // wellPosed is false when the recovery is not trustworthy:
    //   - the weights do not sum to one
    //   - a contributing node has a bias at or near zero, in which case the
    //     forward map genuinely destroyed information and no inverse exists
    //   - the weighted nodes do not all belong to one common region, so the
    //     combination does not describe a point in any single region
    //
    // The position is still returned in those cases. The caller decides.
    InversePosition positionOf(const std::vector<WeightedNode>& w) const {
        InversePosition out;
        if (w.empty()) return out;

        float sum = 0.0f;
        for (const auto& n : w) {
            if (n.id >= nodes_.size()) return out;
            sum += n.weight;
        }

        // Un-bias, then renormalize.
        std::vector<float> raw(w.size(), 0.0f);
        float rawTotal = 0.0f;
        bool  invertible = true;
        for (std::size_t i = 0; i < w.size(); ++i) {
            const float nw = nodes_[w[i].id].weight;
            if (std::fabs(nw) < kBiasEpsilon) {
                invertible = false;   // forward map destroyed this component
                break;
            }
            raw[i] = w[i].weight / nw;
            rawTotal += raw[i];
        }

        if (invertible && std::fabs(rawTotal) >= kBiasEpsilon) {
            for (std::size_t i = 0; i < w.size(); ++i) {
                out.position += (raw[i] / rawTotal) * nodes_[w[i].id].position;
            }
        } else {
            // Fall back to the naive combination so the caller still gets a
            // position, but never claim it is well posed.
            for (const auto& n : w) {
                out.position += n.weight * nodes_[n.id].position;
            }
            return out;
        }

        out.wellPosed = std::fabs(sum - 1.0f) <= kWeightSumEpsilon
                     && sharesRegion(w);
        return out;
    }

    // ---- mutation -------------------------------------------------------

    // Move a node, refusing the move if it would invert or flatten any region
    // the node belongs to (architecture doc 8.6).
    //
    // Weights vary continuously as nodes move, with exactly one failure mode:
    // a node travelling far enough to turn a region inside out drives the
    // signed area through zero, and the weights diverge on the way. Comparing
    // against the winding sign recorded at construction makes that checkable
    // instead of discovered.
    //
    // Returns false and leaves the node where it was if the move is refused.
    bool setNodePosition(NodeID id, glm::vec2 to) {
        if (id >= nodes_.size()) return false;

        const glm::vec2 from = nodes_[id].position;
        nodes_[id].position = to;

        for (RegionID r : nodeRegions_[id]) {
            const auto& ids = regions_[r].ids();
            std::vector<glm::vec2> ring;
            ring.reserve(ids.size());
            for (NodeID q : ids) ring.push_back(nodes_[q].position);
            const float area2 = ringArea2(ring);
            const int sign = (area2 > 0.0f) ? 1 : -1;
            if (std::fabs(area2) < kAreaEpsilon
                || sign != regions_[r].constructionSign()) {
                nodes_[id].position = from;   // refuse, restore
                return false;
            }
        }
        return true;
    }

    const std::vector<RegionID>& regionsAt(NodeID id) const {
        return nodeRegions_[id];
    }

    // ---- validation -----------------------------------------------------

    // Structural faults that make evaluation misbehave without making it fail.
    // This is not called per-evaluate; it is an authoring-time check.
    TopologyReport validate() const {
        TopologyReport rep;

        // T-junctions. For every region edge, every node that is not a vertex
        // of that region and lies on the edge's interior is a fault.
        for (RegionID r = 0; r < regions_.size(); ++r) {
            const auto& ids = regions_[r].ids();
            const std::size_t m = ids.size();
            for (std::size_t e = 0; e < m; ++e) {
                const NodeID ia = ids[e];
                const NodeID ib = ids[(e + 1) % m];
                for (NodeID n = 0; n < nodes_.size(); ++n) {
                    if (std::find(ids.begin(), ids.end(), n) != ids.end()) {
                        continue;
                    }
                    if (onSegmentInterior(nodes_[ia].position,
                                          nodes_[ib].position,
                                          nodes_[n].position)) {
                        rep.tJunctions.push_back(TJunction{n, r, ia, ib});
                    }
                }
            }
        }

        // Orphans. A node in no region contributes to nothing and is almost
        // always an authoring slip rather than an intention.
        std::vector<bool> used(nodes_.size(), false);
        for (const auto& t : regions_) {
            for (NodeID id : t.ids()) used[id] = true;
        }
        for (NodeID n = 0; n < nodes_.size(); ++n) {
            if (!used[n]) rep.orphans.push_back(n);
        }

        // Duplicates. Two regions over the same three nodes make first-hit
        // order the only thing distinguishing them, which is a coin flip
        // dressed as a decision.
        for (RegionID r = 0; r < regions_.size(); ++r) {
            std::vector<NodeID> a = regions_[r].ids();
            std::sort(a.begin(), a.end());
            for (RegionID q = 0; q < r; ++q) {
                std::vector<NodeID> b = regions_[q].ids();
                std::sort(b.begin(), b.end());
                if (a == b) { rep.duplicates.push_back(r); break; }
            }
        }

        // Non-convex regions, as INFORMATION. Not a fault and not counted by
        // clean(): a star is a legitimate control surface. Listed because
        // weights can go negative inside one, and an author is better off
        // knowing where before a performance than after.
        for (RegionID r = 0; r < regions_.size(); ++r) {
            if (!regions_[r].convex()) rep.nonConvex.push_back(r);
        }

        return rep;
    }

private:
    bool validNode(NodeID id) const { return id < nodes_.size(); }

    // Do all of these nodes belong to one common region? If not, the weights
    // describe a blend across disjoint parts of the manifold, and the position
    // recovered from them lies in no single region.
    bool sharesRegion(const std::vector<WeightedNode>& w) const {
        if (w.empty()) return false;
        for (RegionID r : nodeRegions_[w[0].id]) {
            const auto& ids = regions_[r].ids();
            bool all = true;
            for (const auto& n : w) {
                if (std::find(ids.begin(), ids.end(), n.id) == ids.end()) {
                    all = false;
                    break;
                }
            }
            if (all) return true;
        }
        return false;
    }

    bool containsRegion(RegionID r, glm::vec2 p) const {
        const auto& ids = regions_[r].ids();
        std::vector<glm::vec2> pos;
        pos.reserve(ids.size());
        for (NodeID id : ids) pos.push_back(nodes_[id].position);
        return regions_[r].contains(pos, p);
    }

    void fill(Evaluation& e, RegionID r, glm::vec2 p) const {
        const auto& ids = regions_[r].ids();
        std::vector<glm::vec2> pos;
        std::vector<float>     bias;
        pos.reserve(ids.size());
        bias.reserve(ids.size());
        for (NodeID id : ids) {
            pos.push_back(nodes_[id].position);
            bias.push_back(nodes_[id].weight);
        }

        const RingResult rr = regions_[r].evaluate(pos, p, bias);
        if (!rr.valid) return;                 // leaves inside == false

        e.regionID = r;
        e.inside   = true;
        e.weights.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            e.weights.push_back(WeightedNode{ids[i], rr.w[i]});
        }
    }

    // Is n strictly between a and b, and on the line through them?
    //
    // Distance is taken perpendicular to the segment and normalised by its
    // length, so the tolerance means the same thing for a long edge and a
    // short one. The endpoints are excluded: a node coincident with a vertex
    // is a different fault, not a T-junction.
    static bool onSegmentInterior(glm::vec2 a, glm::vec2 b, glm::vec2 n) {
        const glm::vec2 ab = b - a;
        const glm::vec2 an = n - a;
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        if (len2 < kAreaEpsilon) return false;

        const float cross = ab.x * an.y - ab.y * an.x;
        const float dist  = std::fabs(cross) / std::sqrt(len2);
        if (dist > kCollinearEpsilon) return false;

        const float t = (ab.x * an.x + ab.y * an.y) / len2;
        return t > kCollinearEpsilon && t < 1.0f - kCollinearEpsilon;
    }

    std::vector<Node>     nodes_;
    std::vector<Region>   regions_;
    std::string           lastError_;
    std::vector<std::vector<RegionID>> nodeRegions_;   // topology-derived
    std::unordered_map<std::string, NodeID> byName_;
};

} // namespace ofxManifold
