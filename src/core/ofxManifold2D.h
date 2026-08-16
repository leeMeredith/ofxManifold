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
        return static_cast<RegionID>(regions_.size() - 1);
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
    std::unordered_map<std::string, NodeID> byName_;
};

} // namespace ofxManifold
