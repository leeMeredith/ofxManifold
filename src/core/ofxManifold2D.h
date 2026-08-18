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

#include "ofxManifoldTriangle.h"

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
    std::vector<RegionID>  duplicates;   // same three nodes as an earlier region

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

    // Returns InvalidRegion if the triangle is degenerate or references a node
    // that does not exist. Degeneracy is a construction error, not a runtime
    // condition, so it is caught here and the solve never sees it.
    RegionID addTriangle(NodeID a, NodeID b, NodeID c) {
        if (!validNode(a) || !validNode(b) || !validNode(c)) {
            return InvalidRegion;
        }
        if (a == b || b == c || a == c) {
            return InvalidRegion;
        }
        Triangle t;
        if (!Triangle::make(a, b, c,
                            nodes_[a].position,
                            nodes_[b].position,
                            nodes_[c].position, t)) {
            return InvalidRegion;
        }
        regions_.push_back(t);
        const RegionID id = static_cast<RegionID>(regions_.size() - 1);

        // Incidence is derived from TOPOLOGY, not from positions, so caching
        // it does not violate the 8.6 rule against position-derived caches.
        // It is invalidated by addTriangle alone, which is where it is built.
        nodeRegions_[a].push_back(id);
        nodeRegions_[b].push_back(id);
        nodeRegions_[c].push_back(id);
        return id;
    }

    // ---- access ---------------------------------------------------------

    std::size_t nodeCount()   const { return nodes_.size(); }
    std::size_t regionCount() const { return regions_.size(); }

    const Node& node(NodeID id) const { return nodes_[id]; }
    const Triangle& region(RegionID id) const { return regions_[id]; }

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
            const float area2 = signedArea2(nodes_[ids[0]].position,
                                            nodes_[ids[1]].position,
                                            nodes_[ids[2]].position);
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
            for (int e = 0; e < 3; ++e) {
                const NodeID ia = ids[e];
                const NodeID ib = ids[(e + 1) % 3];
                for (NodeID n = 0; n < nodes_.size(); ++n) {
                    if (n == ids[0] || n == ids[1] || n == ids[2]) continue;
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
            std::array<NodeID, 3> a = regions_[r].ids();
            std::sort(a.begin(), a.end());
            for (RegionID q = 0; q < r; ++q) {
                std::array<NodeID, 3> b = regions_[q].ids();
                std::sort(b.begin(), b.end());
                if (a == b) { rep.duplicates.push_back(r); break; }
            }
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
                if (n.id != ids[0] && n.id != ids[1] && n.id != ids[2]) {
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
        return regions_[r].contains(nodes_[ids[0]].position,
                                    nodes_[ids[1]].position,
                                    nodes_[ids[2]].position, p);
    }

    void fill(Evaluation& e, RegionID r, glm::vec2 p) const {
        const auto& ids = regions_[r].ids();
        const BarycentricResult br =
            solveBiased(nodes_[ids[0]].position,
                        nodes_[ids[1]].position,
                        nodes_[ids[2]].position, p,
                        nodes_[ids[0]].weight,
                        nodes_[ids[1]].weight,
                        nodes_[ids[2]].weight);
        if (!br.valid) return;                 // leaves inside == false

        e.regionID = r;
        e.inside   = true;
        e.weights.reserve(3);
        for (int i = 0; i < 3; ++i) {
            e.weights.push_back(WeightedNode{ids[i], br.w[i]});
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
    std::vector<Triangle> regions_;
    std::vector<std::vector<RegionID>> nodeRegions_;   // topology-derived
    std::unordered_map<std::string, NodeID> byName_;
};

} // namespace ofxManifold
