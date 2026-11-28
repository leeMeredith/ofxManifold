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
// Shape. Every level has the same floor plan -- the same nodes and regions --
// but since round 3B-5a a node may sit off its column on a level, so a slice
// of the volume may be wider, narrower or another shape than the floor plan:
// a funnel, an hourglass, a stadium ("a node's own place on a level", below).
// With no node off its column every slice is the floor plan, as before.
// Levels may touch but never cross (round
// 3B-2d): each node is at least as high on each level as on the one below,
// and each level starts no lower than the one below it. Where two levels are
// one height, they PINCH: a closed shape -- a bipyramid, the octahedron --
// is a bottom and a top level meeting at its rim. Every operation that could
// make levels cross refuses instead, changing nothing, and says which nodes.
//
// Nodes. Levels never needs to be told about a new node: a node with no height
// of its own sits at each level's starting height. Removing nodes renumbers
// them; pass Manifold2D::removeNodes()'s table to remapNodes(), as Mapping
// already requires.
//
// One level is today's map: z is ignored, and the weights are the floor
// plan's, on level 0.

#include "ofxManifold2D.h"
#include "ofxManifoldTypes.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
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

    // ---- height measured in levels (round 3B-2c) ----------------------------
    //
    // A level position u: 0 on the bottom level's surface, 1 on the next
    // level's, 1.5 half way between the second and third -- at the point the
    // floor-plan weights `w` describe, so on bent levels it follows the bends.
    // Off the floor plan (no weights) the levels are taken flat, at their
    // bases. One level: u is 0, the height that level's.
    //
    // RESTRAINED (the default) holds u to 0..count()-1: the point never leaves
    // the volume between the bottom and top surfaces. EXTRAPOLATED lets u run
    // past the ends, the outermost surfaces' spacing carried on -- below the
    // bottom by the gap from it to the next level, above the top by the gap
    // below it -- so the bend is followed out there too. Outside is still
    // outside: evaluate() reads it as silent, as ever.

    // The height at level position u.
    float heightAtPosition(const std::vector<WeightedNode>& w, float u,
                           bool extrapolate = false) const {
        const std::size_t n = levels_.size();
        if (n == 1) return levelHeight(0, w);
        if (!extrapolate) u = std::min(std::max(u, 0.0f), float(n - 1));
        const LevelID k = cellBelow(u, n);
        const float lo = levelHeight(k, w), hi = levelHeight(k + 1, w);
        return lo + (u - float(k)) * (hi - lo);
    }

    // Its inverse: the level position of height z. The levels never cross,
    // so the surfaces are in order and the two agree both ways -- except at
    // a pinch (3B-2d), where levels touch and many positions have one height:
    // there, the lowest. Extrapolated through an end cell with no gap there
    // is no slope to carry on: the nearer level.
    float positionAt(const std::vector<WeightedNode>& w, float z,
                     bool extrapolate = false) const {
        const std::size_t n = levels_.size();
        if (n == 1) return 0.0f;
        std::vector<float> h(n);
        for (LevelID k = 0; k < n; ++k) h[k] = levelHeight(k, w);
        if (!extrapolate) {
            // On the top surface itself, the walk below: where the top
            // touches the level under it, the lowest (3B-2e found this).
            if (z <= h[0]) return 0.0f;
            if (z > h[n - 1]) return float(n - 1);
        }
        // The lowest cell whose top is at or above z, never past the top
        // one; below the bottom, the bottom cell, carried on.
        LevelID k = 0;
        while (k + 2 < n && z > h[k + 1]) ++k;
        const float gap = h[k + 1] - h[k];
        if (!(gap > 0.0f)) return float(z > h[k] ? k + 1 : k);
        return float(k) + (z - h[k]) / gap;
    }

    // The weights at level position u (3B-2d) -- what a point riding the
    // levels by position, as the player does, is heard as. The cell is
    // floor(u), held to the cells there are, and t is u's part of the way up
    // it: no division, so a pinch is no different from anywhere else.
    // Outside the floor plan, or with u outside 0..count()-1, outside. One
    // level: u is ignored, as z is by evaluate().
    Evaluation3D evaluateAtPosition(const Evaluation& floor, float u) const {
        Evaluation3D out;
        if (!floor.inside) return out;
        const std::size_t n = levels_.size();
        if (n == 1) return evaluate(floor, 0.0f);
        if (!(u >= 0.0f && u <= float(n - 1))) return out;
        const LevelID k = cellBelow(u, n);
        return share(floor, k, u - float(k));
    }

    // ---- touching (round 3B-2d) ----------------------------------------------
    //
    // A height within kTouch of the same node on the level next to it is ON
    // that level: it settles there exactly, a touch. Heights are sums and
    // differences of floats -- a node stepped down 0.05 at a time onto the
    // level below can land a rounding error past it, or short of it -- and a
    // touch is meant exactly, so that the levels are one height there and the
    // point is shared (evaluate()'s pinch). Files are read exactly: a file
    // says what it means.
    static constexpr float kTouch = 1e-6f;

    // Would node n at height h on level k keep to its levels -- at or above
    // the level below, at or below the level above, once settled? Touching
    // is allowed, passing is not.
    bool fits(LevelID k, NodeID n, float h) const {
        h = settle(k, n, h);
        if (k > 0 && !(h >= height(k - 1, n))) return false;
        if (k + 1 < levels_.size() && !(h <= height(k + 1, n))) return false;
        return true;
    }

    // h, settled onto the level below or above where within kTouch of it.
    float settle(LevelID k, NodeID n, float h) const {
        if (k > 0) h = settleOn(h, height(k - 1, n), true);
        if (k + 1 < levels_.size()) h = settleOn(h, height(k + 1, n), false);
        return h;
    }

    // Do any two neighbouring levels touch -- a node, or the starts? Only
    // then does a file need the version that can say so (3B-2d).
    bool touches(std::size_t nodeCount) const {
        for (LevelID k = 1; k < levels_.size(); ++k) {
            if (!(levels_[k].base > levels_[k - 1].base)) return true;
            const std::size_t n = std::max({nodeCount, levels_[k].heights.size(),
                                            levels_[k - 1].heights.size()});
            for (NodeID i = 0; i < n; ++i) {
                if (!(height(k, i) > height(k - 1, i))) return true;
            }
        }
        return false;
    }

    // ---- a node's own place on a level (round 3B-5a) ------------------------
    //
    // Each node on each level may sit OFF ITS COLUMN: an (x, y) offset from
    // its place on the floor plan, zero until set. A level's own plan is the
    // floor plan -- the same nodes and regions -- with every node at its
    // column plus its offset there: a stadium's tiers, a funnel, an
    // hourglass, or one node pushed out on one level. Moving a node on the
    // floor plan moves its column, offsets kept.
    //
    // Between two levels the SLICE at t has each node on the straight line
    // between its two places, and at the blend of its two heights. A point
    // (x, y, z) is weighted by the slice at its own height: the t whose
    // slice, weighted at (x, y) as a floor plan is, has height z under the
    // point. Node i on the lower level then weighs w_i (1 - t), on the upper
    // w_i t, as on an unshaped map -- and the weights still reproduce the
    // point exactly, because the slice's weights reproduce (x, y) and its
    // height, and every node's slice place is linear in t. Checked by vector.
    //
    // Inside and outside are the slice's: the map's edge is where it is at
    // that height.
    //
    // These take the floor plan, not its Evaluation, since the floor plan
    // must be asked again at each height. On a map with no offsets each one
    // gives exactly what its Evaluation-taking twin above gives -- by vector
    // -- so a caller may use these always.
    //
    // What is and is not found. A level position u needs no search: its
    // slice is known. A height z does: each cell is looked at kSliceSteps + 1
    // times, the map's edge between two looks is closed in on by halving,
    // and the height by halving again -- which takes the height under a
    // point to rise with t (never folding; refused by every operation from
    // round 3B-5b on). A part of the map thinner than a step in t, with the
    // point outside at the looks either side of it, is not found: outside.
    //
    // setOffset() checks nothing yet: round 3B-5b makes every change that
    // would cross, fold or overlap a slice refuse, as heights do.

    static constexpr int kSliceSteps = 8;

    // Node n's offset from its column on level k.
    glm::vec2 offset(LevelID k, NodeID n) const {
        if (k >= levels_.size()) return glm::vec2(0.0f);
        const Level& l = levels_[k];
        return n < l.places.size() ? l.places[n] : glm::vec2(0.0f);
    }

    // Is this node off its column on this level? A file writes only these.
    bool shaped(LevelID k, NodeID n) const {
        const glm::vec2 o = offset(k, n);
        return o.x != 0.0f || o.y != 0.0f;
    }

    // Is any node off its column on any level? If not, this is a map as it
    // was before round 3B-5.
    bool shaped() const {
        for (const Level& l : levels_) {
            for (const glm::vec2& o : l.places) {
                if (o.x != 0.0f || o.y != 0.0f) return true;
            }
        }
        return false;
    }

    // Put node n at `off` from its column on level k. Unchecked (see above).
    bool setOffset(LevelID k, NodeID n, glm::vec2 off) {
        if (k >= levels_.size() || n == InvalidNode) return false;
        Level& l = levels_[k];
        if (l.places.size() <= n) l.places.resize(n + 1, glm::vec2(0.0f));
        l.places[n] = off;
        return true;
    }

    // Where node n is on level k: its column plus its offset there.
    glm::vec2 place(const Manifold2D& plan, LevelID k, NodeID n) const {
        return plan.node(n).position + offset(k, n);
    }

    // Every node's offset in the slice t of the way from level k to k + 1.
    std::vector<glm::vec2> slice(LevelID k, float t) const {
        std::vector<glm::vec2> out;
        if (k >= levels_.size()) return out;
        const std::vector<glm::vec2>& lo = levels_[k].places;
        static const std::vector<glm::vec2> none;
        const std::vector<glm::vec2>& hi =
            k + 1 < levels_.size() ? levels_[k + 1].places : none;
        out.resize(std::max(lo.size(), hi.size()), glm::vec2(0.0f));
        for (std::size_t i = 0; i < out.size(); ++i) {
            const glm::vec2 a = i < lo.size() ? lo[i] : glm::vec2(0.0f);
            const glm::vec2 b = i < hi.size() ? hi[i] : glm::vec2(0.0f);
            out[i] = a + t * (b - a);
        }
        return out;
    }

    // The floor plan's evaluation of (x, y) in the slice at level position
    // u, held to the levels there are. One level: that level's own plan.
    Evaluation floorAtPosition(const Manifold2D& plan, glm::vec2 xy, float u,
                               RegionID hint = InvalidRegion) const {
        const std::size_t n = levels_.size();
        if (!shaped()) return plan.evaluate(xy, hint);
        if (n == 1) return plan.evaluateMoved(xy, slice(0, 0.0f), hint);
        u = std::min(std::max(u, 0.0f), float(n - 1));
        const LevelID k = cellBelow(u, n);
        return plan.evaluateMoved(xy, slice(k, u - float(k)), hint);
    }

    // The height at level position u under (x, y), restrained to the levels:
    // heightAtPosition() above, for a shaped map. Outside the slice there,
    // the levels are taken flat, at their bases, as above.
    float heightAtPosition(const Manifold2D& plan, glm::vec2 xy, float u,
                           RegionID hint = InvalidRegion) const {
        return heightAtPosition(floorAtPosition(plan, xy, u, hint).weights, u);
    }

    // The weights at level position u: evaluateAtPosition() above, for a
    // shaped map. No search: the slice at u is asked directly.
    Evaluation3D evaluateAtPosition(const Manifold2D& plan, glm::vec2 xy,
                                    float u,
                                    RegionID hint = InvalidRegion) const {
        // Past the ends the twin above says outside, whatever the slice.
        return evaluateAtPosition(floorAtPosition(plan, xy, u, hint), u);
    }

    // The point (x, y, z): evaluate() above, for a shaped map.
    Evaluation3D evaluate(const Manifold2D& plan, glm::vec3 p,
                          RegionID hint = InvalidRegion) const {
        const glm::vec2 xy(p.x, p.y);
        const std::size_t n = levels_.size();
        if (!shaped()) return evaluate(plan.evaluate(xy, hint), p.z);
        if (n == 1) return evaluate(plan.evaluateMoved(xy, slice(0, 0.0f), hint), p.z);
        Found f;
        if (!find(plan, xy, p.z, hint, f)) return Evaluation3D{};
        return share(f.floor, f.cell, f.t);
    }

    // The level position of height z under (x, y): positionAt() above, for
    // a shaped map, restrained. Below everything 0, above everything the top;
    // where the point is outside the map at that height, the levels are
    // taken flat, at their bases, as above.
    float positionAt(const Manifold2D& plan, glm::vec2 xy, float z,
                     RegionID hint = InvalidRegion) const {
        const std::size_t n = levels_.size();
        if (n == 1) return 0.0f;
        if (!shaped()) return positionAt(plan.evaluate(xy, hint).weights, z);
        Found f;
        if (find(plan, xy, z, hint, f)) return float(f.cell) + f.t;
        return positionAt(std::vector<WeightedNode>{}, z);
    }

    // ---- making levels ----------------------------------------------------

    // Copy level `from` to a new level starting at `newBase`: every node's
    // height shifted by newBase - base(from), bends included. The new level
    // goes wherever its starting height puts it.
    //
    // Refused, changing nothing, if another level already starts there, or if
    // any node's copy would pass that node on a level around it -- touching
    // is allowed. `nodeCount` is the floor plan's, so nodes with no height of
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
        made.places = levels_[from].places;      // its shape too (3B-5a)

        std::vector<NodeID> bad;
        for (NodeID i = 0; i < n; ++i) {
            float& h = made.heights[i];
            if (at > 0) h = settleOn(h, height(at - 1, i), true);
            if (at < levels_.size()) h = settleOn(h, height(at, i), false);
            const bool overLower = (at == 0) || h >= height(at - 1, i);
            const bool underUpper = (at == levels_.size()) || h <= height(at, i);
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
    // refused, changing nothing, if any of them would pass the level above
    // or below -- reaching it, touching, is allowed; `blocked` receives those
    // nodes.
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
        for (NodeID n : nodes) set(k, n, settle(k, n, height(k, n) + dz));
        return true;
    }

    // Move a whole level up (dz > 0) or down: its starting height and every
    // node on it, bends kept (round 3A-6b). All or nothing: refused, changing
    // nothing, if any node -- of `nodeCount`, or any bent one -- would pass
    // the level above or below; `blocked` receives those nodes. Refused too,
    // with `blocked` empty, if its start would pass a neighbour's start.
    // Reaching either is a touch, and allowed.
    bool moveLevel(LevelID k, float dz, std::size_t nodeCount,
                   std::vector<NodeID>* blocked = nullptr) {
        if (blocked) blocked->clear();
        if (k >= levels_.size()) return false;
        float nb = levels_[k].base + dz;
        if (k > 0) nb = settleOn(nb, levels_[k - 1].base, true);
        if (k + 1 < levels_.size()) nb = settleOn(nb, levels_[k + 1].base, false);
        const std::size_t n = std::max(nodeCount, levels_[k].heights.size());
        std::vector<NodeID> bad;
        for (NodeID i = 0; i < n; ++i) {
            if (!fits(k, i, height(k, i) + dz)) bad.push_back(i);
        }
        const bool basesFit =
            (k == 0 || nb >= levels_[k - 1].base)
            && (k + 1 >= levels_.size() || nb <= levels_[k + 1].base);
        if (!bad.empty() || !basesFit) {
            if (blocked) *blocked = bad;
            return false;
        }
        // Unbent nodes stay equal to the base; one that settles on a
        // neighbour, a float's width away from the base, takes a height of
        // its own.
        std::vector<float> moved(n);
        for (NodeID i = 0; i < n; ++i) moved[i] = settle(k, i, height(k, i) + dz);
        Level& l = levels_[k];
        l.base = nb;
        for (NodeID i = 0; i < n; ++i) {
            if (i < l.heights.size() || moved[i] != nb) set(k, i, moved[i]);
        }
        return true;
    }

    // How far a whole level can move before it would pass a neighbour:
    // {down, up}, each the smallest gap over its nodes and its start; a move
    // may go that far and touch. 0 where it already touches. Infinite where
    // there is no neighbour.
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
    // Refused, changing nothing, if that would pass the level above or below.
    bool setHeight(LevelID k, NodeID n, float h) {
        if (k >= levels_.size() || n == InvalidNode || !fits(k, n, h)) {
            return false;
        }
        set(k, n, settle(k, n, h));
        return true;
    }

    // ---- rebuilding, as a file does -------------------------------------------

    // Replace every level at once: flat levels starting at `bases`, then the
    // `bends` -- per level, (node, height) pairs. Checked WHOLE, after all of
    // it is in place: set one by one, a valid file could be refused part way
    // (a node raised on the ground before the level above it is raised too).
    // Refused, changing nothing, if a base is below the one before it, or
    // any node -- of `nodeCount`, or any bent one -- would be below itself on
    // the level before. Equal is a touch, and allowed. `why` says which.
    bool rebuild(const std::vector<float>& bases,
                 const std::vector<std::vector<std::pair<NodeID, float>>>& bends,
                 std::size_t nodeCount, std::string* why = nullptr) {
        auto fail = [&](const std::string& w) {
            if (why) *why = w;
            return false;
        };
        if (bases.empty()) return fail("a map has at least one level");
        for (std::size_t k = 1; k < bases.size(); ++k) {
            if (!(bases[k] >= bases[k - 1])) {
                return fail("level starting heights are not increasing at "
                            "level " + std::to_string(k));
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
                if (!(at(k, i) >= at(k - 1, i))) {
                    return fail("levels cross at node "
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
            std::vector<glm::vec2> placed;
            for (NodeID old = 0; old < l.places.size(); ++old) {
                if (old >= table.size() || table[old] == InvalidNode) continue;
                const NodeID to = table[old];
                if (placed.size() <= to) placed.resize(to + 1, glm::vec2(0.0f));
                placed[to] = l.places[old];
            }
            l.places = std::move(placed);
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

        // A PINCH (3B-2d): where two levels are one height at the point, a
        // point there is on both, and shared equally, t = 0.5. Either the
        // cell found has no gap, or z sits on its lower level and the level
        // below that is the same height. Where more than two meet, the top
        // two. A gap shrinking toward a pinch is still divided, never 0.
        const float gap = h[k + 1] - h[k];
        float t;
        if (!(gap > 0.0f)) {
            t = 0.5f;
        } else if (k > 0 && !(z > h[k]) && !(h[k] > h[k - 1])) {
            --k;
            t = 0.5f;
        } else {
            t = (z - h[k]) / gap;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
        }
        return share(floor, k, t);
    }

private:
    struct Level {
        float              base = 0.0f;
        std::vector<float> heights;   // by NodeID; past the end, `base`
        std::vector<glm::vec2> places = {};  // offsets by NodeID; past the end, none
    };
    std::vector<Level> levels_;

    // ---- finding a height in a shaped map (3B-5a) ----------------------------

    // Where a height was found: the cell, how far up it, and the slice's
    // evaluation there.
    struct Found {
        LevelID    cell = 0;
        float      t = 0.0f;
        Evaluation floor;
    };

    // One look at cell k, t of the way up: is (x, y) inside the slice, and
    // how far is its height there above z?
    struct Look {
        float      t = 0.0f;
        bool       inside = false;
        float      over = 0.0f;       // the slice's height at (x, y), less z
        float      gap = 0.0f;        // between the two levels there
        Evaluation floor;
    };
    Look look(const Manifold2D& plan, glm::vec2 xy, float z, RegionID hint,
              LevelID k, float t) const {
        Look l;
        l.t = t;
        l.floor = plan.evaluateMoved(xy, slice(k, t), hint);
        l.inside = l.floor.inside;
        if (!l.inside) return l;
        const float lo = heightAt(k, l.floor.weights);
        const float hi = heightAt(k + 1, l.floor.weights);
        l.gap = hi - lo;
        l.over = lo + t * (hi - lo) - z;
        return l;
    }

    // The look nearest `out` that is still inside, between an inside look
    // and an outside one: the map's edge at this point, closed in on.
    Look edge(const Manifold2D& plan, glm::vec2 xy, float z, RegionID hint,
              LevelID k, Look in, Look out) const {
        for (int i = 0; i < 24; ++i) {
            const Look mid = look(plan, xy, z, hint, k, 0.5f * (in.t + out.t));
            if (mid.inside) in = mid; else out = mid;
        }
        return in;
    }

    // The height z under (x, y): each cell from the bottom, looked at
    // kSliceSteps + 1 times; between two looks with the height at or below z
    // at the lower and at or above it at the upper, closed in on by halving.
    // Within `on` of a level, on it -- as evaluate() above -- and at a pinch,
    // shared equally, by the same rules.
    bool find(const Manifold2D& plan, glm::vec2 xy, float z, RegionID hint,
              Found& found) const {
        const float on = 1e-6f;
        const std::size_t n = levels_.size();
        for (LevelID k = 0; k + 1 < n; ++k) {
            Look a = look(plan, xy, z, hint, k, 0.0f);
            for (int s = 1; s <= kSliceSteps; ++s) {
                Look b = look(plan, xy, z, hint, k, float(s) / float(kSliceSteps));
                Look lo = a, hi = b;
                const Look next = b;
                bool any = a.inside || b.inside;
                if (a.inside && !b.inside) hi = edge(plan, xy, z, hint, k, a, b);
                if (!a.inside && b.inside) lo = edge(plan, xy, z, hint, k, b, a);
                a = next;
                if (!any || !(lo.over <= on && hi.over >= -on)) continue;
                for (int i = 0; i < 30 && hi.t - lo.t > 0.0f; ++i) {
                    const Look mid = look(plan, xy, z, hint, k, 0.5f * (lo.t + hi.t));
                    if (!mid.inside) return false;      // a fold: not ours
                    if (mid.over <= 0.0f) lo = mid; else hi = mid;
                }
                Look at = (std::fabs(lo.over) <= std::fabs(hi.over)) ? lo : hi;
                // On a level: exactly.
                if (at.t < on) at = look(plan, xy, z, hint, k, 0.0f);
                else if (at.t > 1.0f - on) at = look(plan, xy, z, hint, k, 1.0f);
                if (!at.inside) return false;
                found.cell = k;
                found.t = at.t;
                found.floor = at.floor;
                // A pinch: no gap here -- shared equally, t = 0.5.
                if (!(at.gap > 0.0f)) {
                    found.t = 0.5f;
                    return true;
                }
                // On the level above: its own cell, from the bottom -- and
                // where the level above THAT touches it here, that pinch.
                if (at.t == 1.0f && k + 2 < n) {
                    const Look up = look(plan, xy, z, hint, k + 1, 0.0f);
                    found.cell = k + 1;
                    found.t = (up.inside && !(up.gap > 0.0f)) ? 0.5f : 0.0f;
                }
                return true;
            }
        }
        return false;
    }

    // A level's height under the point; its base off the floor plan.
    float levelHeight(LevelID k, const std::vector<WeightedNode>& w) const {
        return w.empty() ? levels_[k].base : heightAt(k, w);
    }

    // The cell a level position falls in: the level below it, never the top.
    static LevelID cellBelow(float u, std::size_t n) {
        const float f = std::floor(u);
        if (!(f > 0.0f)) return 0;
        return std::min(LevelID(f), LevelID(n - 2));
    }

    // h past `edge` -- below it when `below`, above it otherwise -- by no
    // more than kTouch, is on it.
    static float settleOn(float h, float edge, bool below) {
        if (below ? (h < edge && h >= edge - kTouch)
                  : (h > edge && h <= edge + kTouch)) {
            return edge;
        }
        return h;
    }

    // The floor plan's weights split between level k, 1 - t, and the one
    // above it, t.
    static Evaluation3D share(const Evaluation& floor, LevelID k, float t) {
        Evaluation3D out;
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


    void set(LevelID k, NodeID n, float h) {
        Level& l = levels_[k];
        if (l.heights.size() <= n) l.heights.resize(n + 1, l.base);
        l.heights[n] = h;
    }
};

} // namespace ofxManifold
