#pragma once

// ofxManifold — what a 3D map drives (PLAN-round3.md decisions D and F).
//
// Levels (core) is the 3D map's geometry: a point (x, y, z) becomes weights
// over (node, level). Map3D is the next layer, as Mapping is over Manifold2D:
// each level's bindings, and each level's node values, read through those
// weights.
//
//   - OUTPUTS belong to every level at once: the room does not change with
//     height. Every output operation here applies to every level, so two
//     levels can never disagree about a channel, a name or a trim.
//   - BINDINGS belong to one level: the canopy can feed the ceiling speakers
//     and the ground the floor ones. Every binding operation takes a level.
//   - VALUES belong to one level, from one value set for the whole map: the
//     canopy can be bright and the ground dark.
//
// Reading is linear in the weights, as a 2D map's is: each level is resolved
// with its own share of the weights and the results summed. A derived output
// fed by outputs is computed ONCE, from that sum; one fed by nodes reads the
// floor plan's weight for each node -- the same on every level, so height
// does not change it.
//
// One door, like the editor's: the levels' Mappings are reachable only as
// const. Everything that changes them goes through here, which is what keeps
// them in step. A Map3D with one level and no values is today's map exactly,
// checked by vector against Mapping itself.

#include "../core/ofxManifoldLevels.h"
#include "ofxManifoldMapping.h"
#include "ofxManifoldRig.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {

// A named number a node carries besides its share (decision D). The range
// guides an editor's stepping and display; the kernel never clamps to it.
struct ValueDef {
    std::string name;
    float       fallback = 0.0f;   // a node's value until it is given one
    float       lo       = 0.0f;
    float       hi       = 1.0f;
};

class Map3D {
public:
    Map3D() : routing_(1), values_(1) {}

    // ---- reading ---------------------------------------------------------

    const Levels&  levels() const { return levels_; }
    std::size_t    count() const { return routing_.size(); }
    const Mapping& level(LevelID k) const { return routing_[k]; }

    const std::vector<ValueDef>& valueSet() const { return defs_; }
    std::size_t findValue(const std::string& name) const {
        for (std::size_t i = 0; i < defs_.size(); ++i) {
            if (defs_[i].name == name) return i;
        }
        return static_cast<std::size_t>(-1);
    }

    // A node's value on a level: its own if given one, else the fallback.
    float value(LevelID k, NodeID n, std::size_t v) const {
        if (k >= values_.size() || v >= defs_.size()) return 0.0f;
        if (v >= values_[k].size() || n >= values_[k][v].size()) {
            return defs_[v].fallback;
        }
        return values_[k][v][n];
    }

    // ---- the geometry, kept in step ----------------------------------------

    // Copy level `from` to a new starting height: heights and bends (as
    // Levels::copyLevel), and the level's bindings and values with them.
    // Refused exactly when Levels refuses it; nothing changes then.
    LevelID copyLevel(LevelID from, float newBase, std::size_t nodeCount,
                      std::vector<NodeID>* blocked = nullptr) {
        if (from >= routing_.size()) return routing_.size();
        const Mapping bindings = routing_[from];
        const auto vals = values_[from];
        const LevelID at = levels_.copyLevel(from, newBase, nodeCount, blocked);
        if (at >= levels_.count()) return routing_.size();
        routing_.insert(routing_.begin() + static_cast<std::ptrdiff_t>(at),
                        bindings);
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(at), vals);
        return at;
    }

    bool removeLevel(LevelID k) {
        if (!levels_.removeLevel(k)) return false;
        routing_.erase(routing_.begin() + static_cast<std::ptrdiff_t>(k));
        values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(k));
        return true;
    }

    bool raise(LevelID k, const std::vector<NodeID>& nodes, float dz,
               std::vector<NodeID>* blocked = nullptr) {
        return levels_.raise(k, nodes, dz, blocked);
    }

    bool setHeight(LevelID k, NodeID n, float h) {
        return levels_.setHeight(k, n, h);
    }

    // Nodes removed from the floor plan: heights, every level's bindings and
    // every level's values follow, with Manifold2D::removeNodes()'s table.
    void remapNodes(const std::vector<NodeID>& table) {
        levels_.remapNodes(table);
        for (Mapping& m : routing_) m.remapNodes(table);
        for (auto& lv : values_) {
            for (std::size_t v = 0; v < lv.size(); ++v) {
                const std::vector<float>& col = lv[v];
                std::vector<float> moved;
                for (NodeID old = 0; old < col.size(); ++old) {
                    if (old >= table.size() || table[old] == InvalidNode) continue;
                    const NodeID now = table[old];
                    // a gap the move leaves is a node with no value of its own
                    if (moved.size() <= now) {
                        moved.resize(now + 1, defs_[v].fallback);
                    }
                    moved[now] = col[old];
                }
                lv[v] = std::move(moved);
            }
        }
    }

    // Take levels and per-level routing built elsewhere -- by a file loader.
    // Checked at the door: one Mapping per level, and every level agreeing
    // about the room (checkAgreement, notes included). Refused, changing
    // nothing, otherwise. Values are cleared; declare and set them after.
    bool adopt(Levels geometry, std::vector<Mapping> routing,
               std::string* why = nullptr) {
        if (routing.size() != geometry.count()) {
            if (why) {
                *why = std::to_string(routing.size()) + " bindings tables for "
                     + std::to_string(geometry.count()) + " levels";
            }
            return false;
        }
        std::vector<const Mapping*> all;
        for (const Mapping& m : routing) all.push_back(&m);
        const auto dis = checkAgreement(all);
        if (!dis.empty()) {
            if (why) *why = "levels disagree about the room: " + dis[0].message;
            return false;
        }
        levels_ = std::move(geometry);
        routing_ = std::move(routing);
        defs_.clear();
        values_.assign(routing_.size(), {});
        return true;
    }

    // ---- outputs: every level at once --------------------------------------

    TargetID addOutput(const std::string& name, int channel, float trim = 1.0f) {
        TargetID id = InvalidTarget;
        for (Mapping& m : routing_) id = m.addOutput(name, channel, trim);
        return id;
    }

    bool removeOutput(TargetID id) {
        bool ok = true;
        for (Mapping& m : routing_) ok = m.removeOutput(id) && ok;
        return ok;
    }

    void setTrim(TargetID id, float trim) {
        for (Mapping& m : routing_) m.setTargetTrim(id, trim);
    }

    std::size_t addDerived(const std::string& name, int channel, float trim,
                           SumMode mode,
                           const std::vector<std::pair<NodeID, float>>& srcs) {
        std::size_t made = static_cast<std::size_t>(-1);
        for (Mapping& m : routing_) {
            made = m.addDerived(name, channel, trim, mode, srcs);
        }
        return made;
    }

    std::size_t addDerivedFromOutputs(
            const std::string& name, int channel, float trim, SumMode mode,
            const std::vector<std::pair<TargetID, float>>& srcs) {
        std::size_t made = static_cast<std::size_t>(-1);
        for (Mapping& m : routing_) {
            made = m.addDerivedFromOutputs(name, channel, trim, mode, srcs);
        }
        return made;
    }

    // Move a whole level, its start and every node on it (Levels::moveLevel).
    // Bindings and values are untouched: they belong to the level, not to a
    // height.
    bool moveLevel(LevelID k, float dz, std::size_t nodeCount,
                   std::vector<NodeID>* blocked = nullptr) {
        return levels_.moveLevel(k, dz, nodeCount, blocked);
    }

    // Move every level onto a rig at once (the editor's cmd-r, round 3A-6):
    // each level as useRig() moves one map, its bindings following by name.
    // All or nothing: refused, changing nothing, if any level sends what the
    // rig lacks or has as another kind; `refused` receives those names,
    // sorted. The levels and the node values are not touched.
    bool useRig(const Mapping& rig, std::vector<std::string>* refused = nullptr) {
        if (refused) refused->clear();
        std::vector<Mapping> moved;
        std::vector<std::string> bad;
        for (const Mapping& m : routing_) {
            RigUse u = ofxManifold::useRig(m, rig);
            if (!u.ok) {
                bad.insert(bad.end(), u.refused.begin(), u.refused.end());
                continue;
            }
            moved.push_back(std::move(u.result));
        }
        if (!bad.empty()) {
            std::sort(bad.begin(), bad.end());
            bad.erase(std::unique(bad.begin(), bad.end()), bad.end());
            if (refused) *refused = bad;
            return false;
        }
        routing_ = std::move(moved);
        return true;
    }

    // ---- bindings: one level -----------------------------------------------

    // A target that is not an output is ignored, as an unknown level is:
    // Mapping::bind() does not check, and a link to an id past the end would
    // be read out of bounds on every evaluation.
    void bind(LevelID k, NodeID n, TargetID t, float w = 1.0f) {
        if (k < routing_.size() && t < routing_[k].targetCount()) {
            routing_[k].bind(n, t, w);
        }
    }
    void bindSilence(LevelID k, NodeID n, float w) {
        if (k < routing_.size()) routing_[k].bindSilence(n, w);
    }
    void unbind(LevelID k, NodeID n, TargetID t) {
        if (k < routing_.size()) routing_[k].unbind(n, t);
    }
    void clearBindings(LevelID k, NodeID n) {
        if (k < routing_.size()) routing_[k].clearBindings(n);
    }
    bool setOutputFraction(LevelID k, NodeID n, float f) {
        return k < routing_.size() && routing_[k].setOutputFraction(n, f);
    }

    // ---- values ------------------------------------------------------------

    // Declare a value for the whole map. Refused -- SIZE_MAX -- for a name
    // already declared.
    std::size_t addValue(const std::string& name, float fallback,
                         float lo = 0.0f, float hi = 1.0f) {
        if (findValue(name) != static_cast<std::size_t>(-1)) {
            return static_cast<std::size_t>(-1);
        }
        defs_.push_back(ValueDef{name, fallback, lo, hi});
        return defs_.size() - 1;
    }

    // A value's range changed after it was made (round 3A-7): every number
    // it holds on every level, and its fallback, brought inside the new
    // range. Refused unless lo < hi, or for a value that is not there.
    bool setValueRange(std::size_t v, float lo, float hi) {
        if (v >= defs_.size() || !(lo < hi)) return false;
        auto clamp = [lo, hi](float x) { return std::min(hi, std::max(lo, x)); };
        defs_[v].lo = lo;
        defs_[v].hi = hi;
        defs_[v].fallback = clamp(defs_[v].fallback);
        for (auto& level : values_) {
            if (v < level.size()) {
                for (float& x : level[v]) x = clamp(x);
            }
        }
        return true;
    }

    // A value renamed, keeping its numbers. Refused for a name another value
    // has, or a value that is not there.
    bool renameValue(std::size_t v, const std::string& name) {
        if (v >= defs_.size()) return false;
        const std::size_t other = findValue(name);
        if (other != static_cast<std::size_t>(-1) && other != v) return false;
        defs_[v].name = name;
        return true;
    }

    // A value removed from the map: its declaration and its numbers on every
    // level. The values after it move down one.
    bool removeValue(std::size_t v) {
        if (v >= defs_.size()) return false;
        defs_.erase(defs_.begin() + static_cast<std::ptrdiff_t>(v));
        for (auto& level : values_) {
            if (v < level.size()) {
                level.erase(level.begin() + static_cast<std::ptrdiff_t>(v));
            }
        }
        return true;
    }

    // One node's value on one level. Not clamped: the range is for editors.
    bool setValue(LevelID k, NodeID n, std::size_t v, float x) {
        if (k >= values_.size() || v >= defs_.size() || n == InvalidNode) {
            return false;
        }
        auto& lv = values_[k];
        if (lv.size() <= v) lv.resize(v + 1);
        auto& col = lv[v];
        if (col.size() <= n) col.resize(n + 1, defs_[v].fallback);
        col[n] = x;
        return true;
    }

    // A value at a point: the nodes' values on their levels, by the 3D
    // weights. Always linear -- values are not power. Outside, the fallback:
    // a point no level covers has no node to carry anything.
    float valueAt(std::size_t v, const Evaluation3D& e) const {
        if (v >= defs_.size()) return 0.0f;
        if (!e.inside) return defs_[v].fallback;
        float x = 0.0f;
        for (const auto& lw : e.weights) {
            x += lw.weight * value(lw.level, lw.node, v);
        }
        return x;
    }

    // ---- what reaches the room -----------------------------------------------

    // The 3D weights as a blend, by name: each level resolved with its own
    // weights, summed; the channels the map fills read from the floor plan's
    // weight for each node -- the sum of its weights over every level.
    Blended resolve(const Evaluation3D& e) const {
        Blended b;
        if (!e.inside || routing_.empty()) return b;
        std::vector<WeightVector> per(routing_.size());
        WeightVector floor;
        for (const auto& lw : e.weights) {
            if (lw.level >= routing_.size()) continue;
            per[lw.level].push_back(WeightedNode{lw.node, lw.weight});
            bool merged = false;
            for (auto& f : floor) {
                if (f.id == lw.node) { f.weight += lw.weight; merged = true; }
            }
            if (!merged) floor.push_back(WeightedNode{lw.node, lw.weight});
        }
        std::vector<float> acc(routing_[0].targetCount(), 0.0f);
        for (LevelID k = 0; k < routing_.size(); ++k) {
            if (per[k].empty()) continue;
            const Resolved r = routing_[k].resolve(per[k]);
            for (const auto& wt : r.targets) {
                if (wt.id < acc.size()) acc[wt.id] += wt.weight;
            }
            b.silence += routing_[k].silenceShare(per[k]);
        }
        const Mapping& room = routing_[0];
        for (TargetID t = 0; t < acc.size(); ++t) {
            if (acc[t] != 0.0f) {
                b.shares.push_back(NamedWeight{room.targetName(t), acc[t]});
            }
        }
        const Resolved onFloor = room.resolve(floor);
        const auto& ags = room.aggregators();
        for (std::size_t a = 0; a < ags.size(); ++a) {
            if (ags[a].feed != Feed::Nodes) continue;
            if (onFloor.aggregates[a] != 0.0f) {
                b.filled.push_back(NamedWeight{ags[a].name,
                                               onFloor.aggregates[a]});
            }
        }
        return b;
    }

    // Indexed by channel, before each channel's trim -- as
    // Mapping::toChannels() -- and after it, as toChannelLevels().
    std::vector<float> toChannels(const Evaluation3D& e) const {
        return rigChannels(rigOf(routing_[0]), resolve(e));
    }
    std::vector<float> toChannelLevels(const Evaluation3D& e) const {
        return rigLevels(rigOf(routing_[0]), resolve(e));
    }
    float silenceShare(const Evaluation3D& e) const {
        return resolve(e).silence;
    }

private:
    Levels               levels_;
    std::vector<Mapping> routing_;                  // one per level
    std::vector<ValueDef> defs_;
    // values_[level][value][node]; past the end, the value's fallback
    std::vector<std::vector<std::vector<float>>> values_;
};

} // namespace ofxManifold
