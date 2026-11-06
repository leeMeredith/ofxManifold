#pragma once

// ofxManifold — the 3D map's geometry (PLAN-round3.md decision F).
//
// A 3D map is one FLOOR PLAN -- a Manifold2D, unchanged -- seen at several
// LEVELS. A level is the floor plan copied up or down in z: every node has a
// copy directly above or below itself, so each column is one node at several
// heights. A level starts flat, at its starting height, and bends when some
// of its nodes are raised or lowered.
//
// This header is geometry and nothing else. It turns a point (x, y, z) into
// weights over (node, level) pairs, the way Manifold2D turns (x, y) into
// weights over nodes. What those weights drive -- outputs, values -- is the
// next layer, as Mapping is the next layer over Manifold2D.
//
// How a point is weighted:
//   1. The floor plan gives weights at (x, y). Levels takes that Evaluation,
//      so it never repeats the 2D work and cannot disagree with it.
//   2. Each level's height at (x, y) is its node heights blended by those
//      weights.
//   3. z falls between two levels' heights; t says how far up from the lower.
//   4. Node i on the lower level weighs w_i (1 - t); on the upper, w_i t.
//
// The weights reproduce the point: sum over (node, level) of weight times
// (x_i, y_i, height) is (x, y, z) exactly, bent levels included -- the floor
// plan's weights reproduce (x, y), and the vertical blend is linear between
// the two heights at (x, y). Checked by vector.
//
// Shape. Every level has the same floor plan, so every horizontal slice of the
// volume is the same shape: it cannot narrow to a waist or a point -- no
// hourglass, no double pyramid. Levels may not cross or touch: each node is
// strictly higher on each level than on the one below. Every operation that
// could break that refuses instead, changing nothing, and says which nodes.
//
// Nodes. Levels never needs to be told about a new node: a node with no height
// of its own sits at each level's starting height. Removing nodes renumbers
// them; pass Manifold2D::removeNodes()'s table to remapNodes(), as Mapping
// already requires.
//
// One level is today's map: z is ignored, and the weights are the floor
// plan's, on level 0.

#include "ofxManifoldTypes.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {

using LevelID = std::size_t;

// A node on a level, and its weight.
struct LevelWeight {
    NodeID  node   = InvalidNode;
    LevelID level  = 0;
    float   weight = 0.0f;
};

struct Evaluation3D {
    bool    inside = false;
    LevelID below  = 0;      // the level at or below z
    float   t      = 0.0f;   // 0 at `below`, 1 at the level above it
    std::vector<LevelWeight> weights;
};

class Levels {
public:
    // One level, flat, at height 0: today's map.
    Levels() { levels_.push_back(Level{0.0f, {}}); }

    std::size_t count() const { return levels_.size(); }

    // Where a level starts, before any bend.
    float base(LevelID k) const {
        return k < levels_.size() ? levels_[k].base : 0.0f;
    }

    // A node's height on a level: its own if it has been raised or lowered,
    // otherwise the level's starting height.
    float height(LevelID k, NodeID n) const {
        if (k >= levels_.size()) return 0.0f;
        const Level& l = levels_[k];
        return n < l.heights.size() ? l.heights[n] : l.base;
    }

    // Does this node have a height of its own on this level? A file writes
    // only these.
    bool bent(LevelID k, NodeID n) const {
        if (k >= levels_.size()) return false;
        const Level& l = levels_[k];
        return n < l.heights.size() && l.heights[n] != l.base;
    }

    // The level's height under a point, from the floor plan's weights there.
    float heightAt(LevelID k, const std::vector<WeightedNode>& w) const {
        float h = 0.0f;
        for (const auto& wn : w) h += wn.weight * height(k, wn.id);
        return h;
    }

    // ---- making levels ----------------------------------------------------

    // Copy level `from` to a new level starting at `newBase`: every node's
    // height shifted by newBase - base(from), bends included. The new level
    // goes wherever its starting height puts it.
    //
    // Refused, changing nothing, if another level already starts there, or if
    // any node's copy would not be strictly between that node on the levels
    // around it. `nodeCount` is the floor plan's, so nodes with no height of
    // their own are checked too. Returns the new level's id, or count() on
    // refusal; `blocked` receives the nodes that refused it.
    LevelID copyLevel(LevelID from, float newBase, std::size_t nodeCount,
                      std::vector<NodeID>* blocked = nullptr) {
        if (blocked) blocked->clear();
        if (from >= levels_.size()) return levels_.size();
        for (const Level& l : levels_) {
            if (l.base == newBase) return levels_.size();
        }
        LevelID at = 0;
        while (at < levels_.size() && levels_[at].base < newBase) ++at;

        const float shift = newBase - levels_[from].base;
        Level made{newBase, {}};
        const std::size_t n = std::max(nodeCount, levels_[from].heights.size());
        made.heights.resize(n);
        for (NodeID i = 0; i < n; ++i) made.heights[i] = height(from, i) + shift;

        std::vector<NodeID> bad;
        for (NodeID i = 0; i < n; ++i) {
            const float h = made.heights[i];
            const bool overLower = (at == 0) || h > height(at - 1, i);
            const bool underUpper = (at == levels_.size()) || h < height(at, i);
            if (!overLower || !underUpper) bad.push_back(i);
        }
        if (!bad.empty()) {
            if (blocked) *blocked = bad;
            return levels_.size();
        }
        levels_.insert(levels_.begin() + static_cast<std::ptrdiff_t>(at),
                       std::move(made));
        return at;
    }

    // Remove a level. Refused for the last one: a map has at least one level.
    bool removeLevel(LevelID k) {
        if (levels_.size() <= 1 || k >= levels_.size()) return false;
        levels_.erase(levels_.begin() + static_cast<std::ptrdiff_t>(k));
        return true;
    }

    // ---- bending ------------------------------------------------------------

    // Raise (dz > 0) or lower (dz < 0) nodes on one level. All or nothing:
    // refused, changing nothing, if any of them would reach the level above
    // or below; `blocked` receives those nodes.
    bool raise(LevelID k, const std::vector<NodeID>& nodes, float dz,
               std::vector<NodeID>* blocked = nullptr) {
        if (blocked) blocked->clear();
        if (k >= levels_.size()) return false;
        std::vector<NodeID> bad;
        for (NodeID n : nodes) {
            if (n == InvalidNode) { bad.push_back(n); continue; }
            if (!fits(k, n, height(k, n) + dz)) bad.push_back(n);
        }
        if (!bad.empty()) {
            if (blocked) *blocked = bad;
            return false;
        }
        for (NodeID n : nodes) set(k, n, height(k, n) + dz);
        return true;
    }

    // Move a whole level up (dz > 0) or down: its starting height and every
    // node on it, bends kept (round 3A-6b). All or nothing: refused, changing
    // nothing, if any node -- of `nodeCount`, or any bent one -- would reach
    // the level above or below; `blocked` receives those nodes. Refused too,
    // with `blocked` empty, if its start would reach a neighbour's start.
    bool moveLevel(LevelID k, float dz, std::size_t nodeCount,
                   std::vector<NodeID>* blocked = nullptr) {
        if (blocked) blocked->clear();
        if (k >= levels_.size()) return false;
        const float nb = levels_[k].base + dz;
        const std::size_t n = std::max(nodeCount, levels_[k].heights.size());
        std::vector<NodeID> bad;
        for (NodeID i = 0; i < n; ++i) {
            if (!fits(k, i, height(k, i) + dz)) bad.push_back(i);
        }
        const bool basesFit =
            (k == 0 || nb > levels_[k - 1].base)
            && (k + 1 >= levels_.size() || nb < levels_[k + 1].base);
        if (!bad.empty() || !basesFit) {
            if (blocked) *blocked = bad;
            return false;
        }
        Level& l = levels_[k];
        for (float& h : l.heights) h += dz;   // unbent stay equal to the base
        l.base = nb;
        return true;
    }

    // How far a whole level can move before it would reach a neighbour:
    // {down, up}, each the smallest gap over its nodes and its start; a move
    // must stay strictly inside. Infinite where there is no neighbour.
    std::pair<float, float> room(LevelID k, std::size_t nodeCount) const {
        const float inf = std::numeric_limits<float>::infinity();
        float down = inf, up = inf;
        if (k >= levels_.size()) return {0.0f, 0.0f};
        const std::size_t n = std::max(nodeCount, levels_[k].heights.size());
        if (k > 0) {
            down = levels_[k].base - levels_[k - 1].base;
            for (NodeID i = 0; i < n; ++i) {
                down = std::min(down, height(k, i) - height(k - 1, i));
            }
        }
        if (k + 1 < levels_.size()) {
            up = levels_[k + 1].base - levels_[k].base;
            for (NodeID i = 0; i < n; ++i) {
                up = std::min(up, height(k + 1, i) - height(k, i));
            }
        }
        return {down, up};
    }

    // Put one node at a height on one level -- what a file does on load.
    // Refused, changing nothing, if that would reach the level above or below.
    bool setHeight(LevelID k, NodeID n, float h) {
        if (k >= levels_.size() || n == InvalidNode || !fits(k, n, h)) {
            return false;
        }
        set(k, n, h);
        return true;
    }

    // ---- rebuilding, as a file does -------------------------------------------

    // Replace every level at once: flat levels starting at `bases`, then the
    // `bends` -- per level, (node, height) pairs. Checked WHOLE, after all of
    // it is in place: set one by one, a valid file could be refused part way
    // (a node raised on the ground before the level above it is raised too).
    // Refused, changing nothing, if the bases are not strictly increasing, or
    // any node -- of `nodeCount`, or any bent one -- would not be strictly
    // between its levels. `why` says which.
    bool rebuild(const std::vector<float>& bases,
                 const std::vector<std::vector<std::pair<NodeID, float>>>& bends,
                 std::size_t nodeCount, std::string* why = nullptr) {
        auto fail = [&](const std::string& w) {
            if (why) *why = w;
            return false;
        };
        if (bases.empty()) return fail("a map has at least one level");
        for (std::size_t k = 1; k < bases.size(); ++k) {
            if (!(bases[k] > bases[k - 1])) {
                return fail("level starting heights are not strictly "
                            "increasing at level " + std::to_string(k));
            }
        }
        std::vector<Level> made;
        for (float b : bases) made.push_back(Level{b, {}});
        std::size_t n = nodeCount;
        for (std::size_t k = 0; k < bends.size() && k < made.size(); ++k) {
            for (const auto& nh : bends[k]) {
                if (nh.first == InvalidNode) return fail("bend on an unknown node");
                Level& l = made[k];
                if (l.heights.size() <= nh.first) {
                    l.heights.resize(nh.first + 1, l.base);
                }
                l.heights[nh.first] = nh.second;
                n = std::max(n, static_cast<std::size_t>(nh.first) + 1);
            }
        }
        if (bends.size() > made.size()) {
            return fail("bends for " + std::to_string(bends.size())
                        + " levels, but there are "
                        + std::to_string(made.size()));
        }
        auto at = [&](std::size_t k, NodeID i) {
            const Level& l = made[k];
            return i < l.heights.size() ? l.heights[i] : l.base;
        };
        for (std::size_t k = 1; k < made.size(); ++k) {
            for (NodeID i = 0; i < n; ++i) {
                if (!(at(k, i) > at(k - 1, i))) {
                    return fail("levels cross or touch at node "
                                + std::to_string(i) + " between levels "
                                + std::to_string(k - 1) + " and "
                                + std::to_string(k));
                }
            }
        }
        levels_ = std::move(made);
        return true;
    }

    // ---- nodes removed from the floor plan -----------------------------------

    // `table[old]` is the node's new id, or InvalidNode if it was removed:
    // what Manifold2D::removeNodes() hands back.
    void remapNodes(const std::vector<NodeID>& table) {
        for (Level& l : levels_) {
            std::vector<float> moved;
            for (NodeID old = 0; old < l.heights.size(); ++old) {
                if (old >= table.size() || table[old] == InvalidNode) continue;
                const NodeID now = table[old];
                if (moved.size() <= now) moved.resize(now + 1, l.base);
                moved[now] = l.heights[old];
            }
            l.heights = std::move(moved);
        }
    }

    // ---- weights ------------------------------------------------------------

    // The point (x, y, z), given the floor plan's evaluation at (x, y).
    //
    // Outside the floor plan, outside. With one level, z is ignored. Otherwise
    // z must lie between the bottom and top levels' heights at (x, y) --
    // above or below them is OUTSIDE, exactly as off the edge of a 2D map.
    // Nothing is clamped: clamping would invent an owner for a place no level
    // covers. A soft edge is built, as a level of null nodes, not assumed.
    Evaluation3D evaluate(const Evaluation& floor, float z) const {
        Evaluation3D out;
        if (!floor.inside) return out;
        const std::size_t n = levels_.size();
        if (n == 1) {
            out.inside = true;
            for (const auto& wn : floor.weights) {
                out.weights.push_back(LevelWeight{wn.id, 0, wn.weight});
            }
            return out;
        }

        // A level's height at (x, y) is a weighted sum, so a point placed
        // exactly on a level can land a rounding error to either side of it.
        // Within this of the bottom or top, the point is ON that level.
        const float onLevel = 1e-6f;

        std::vector<float> h(n);
        for (LevelID k = 0; k < n; ++k) h[k] = heightAt(k, floor.weights);
        if (z < h[0] - onLevel || z > h[n - 1] + onLevel) return out;

        // The highest level at or below z, never the top one.
        LevelID k = 0;
        while (k + 2 < n && h[k + 1] <= z) ++k;
        float t = (z - h[k]) / (h[k + 1] - h[k]);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        out.inside = true;
        out.below = k;
        out.t = t;
        for (const auto& wn : floor.weights) {
            if (t < 1.0f) {
                out.weights.push_back(LevelWeight{wn.id, k,
                                                  wn.weight * (1.0f - t)});
            }
            if (t > 0.0f) {
                out.weights.push_back(LevelWeight{wn.id, k + 1,
                                                  wn.weight * t});
            }
        }
        return out;
    }

private:
    struct Level {
        float              base = 0.0f;
        std::vector<float> heights;   // by NodeID; past the end, `base`
    };
    std::vector<Level> levels_;

    // Strictly above the level below and strictly below the level above.
    bool fits(LevelID k, NodeID n, float h) const {
        if (k > 0 && !(h > height(k - 1, n))) return false;
        if (k + 1 < levels_.size() && !(h < height(k + 1, n))) return false;
        return true;
    }

    void set(LevelID k, NodeID n, float h) {
        Level& l = levels_[k];
        if (l.heights.size() <= n) l.heights.resize(n + 1, l.base);
        l.heights[n] = h;
    }
};

} // namespace ofxManifold
