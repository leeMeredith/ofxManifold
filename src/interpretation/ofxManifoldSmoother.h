#pragma once

// ofxManifold — weight smoothing (architecture doc section 12).
//
// The first stateful object in this project, and the only one below src/ofx.
// Everything else is a pure function of its arguments; this remembers what it
// said last frame.
//
// WHAT IT IS FOR, precisely.
//
// It is NOT for ordinary region crossings. On a conforming mesh the weights
// are already continuous there: walk a point across a shared edge and the
// departing node decays to exactly zero as the arriving one rises from zero.
// The node SET changes, the numbers do not jump.
//
// It is for the cases where they genuinely do:
//
//   * a jittery source -- a tracker, a network message, a fader read at frame
//     rate. The manifold faithfully reports the jitter, because that is what
//     it was given.
//   * leaving or entering the hull, where the weight vector goes from three
//     entries to none in one frame.
//   * overlapping regions, where the Evaluator's hysteresis means entering
//     from a different side gives a different answer for the same point.
//   * a T-junction, where the weights really do step (example-basic, key 2).
//   * a point that jumps -- a cue fired, a trajectory scrubbed, a map swapped.
//
// A weight vector that steps is zipper noise in audio and popping in visuals.
//
// WHY WEIGHTS RATHER THAN THE POINT.
//
// Smoothing the point before evaluating would handle jitter, and nothing else.
// A hull exit, an overlap pop and a map swap are all discontinuities in the
// OUTPUT that a perfectly smooth input still produces. Smoothing where the
// discontinuity is means one component handles all of them.

#include "../interpretation/ofxManifoldCurves.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ofxManifold {

// Below this a decaying node is dropped rather than tracked forever. Without
// it, a source that wandered the whole map would accumulate an entry per node
// it ever touched.
static constexpr float kSmootherPrune = 1e-4f;

class WeightSmoother {
public:
    // ---- configuration ---------------------------------------------------

    // Time for the remaining distance to halve, in the same units as the dt
    // passed to update(). Zero or less disables smoothing entirely.
    //
    // Expressed as a half-life rather than a per-frame coefficient so the
    // result is FRAME-RATE INDEPENDENT. A coefficient of 0.2 per frame means
    // something different at 30fps and at 120fps, and a smoother tuned in
    // rehearsal on one machine would behave differently on another.
    void setHalfLife(float seconds) { halfLife_ = std::max(0.0f, seconds); }
    float halfLife() const { return halfLife_; }

    // Optional hard limit on how fast any single weight may change, in weight
    // units per unit of time. Zero disables it.
    //
    // Different in character from the half-life. Exponential smoothing
    // approaches asymptotically and never quite arrives; a slew limit
    // approaches linearly and arrives exactly, at a bounded rate. Both can be
    // on, in which case the exponential result is clamped afterwards.
    void setMaxRate(float perSecond) { maxRate_ = std::max(0.0f, perSecond); }
    float maxRate() const { return maxRate_; }

    // ---- use -------------------------------------------------------------

    // Advance toward `target` by `dt` and return the smoothed vector.
    //
    // Nodes present in the target but not in the current state enter from
    // zero. Nodes present in the state but not in the target decay toward
    // zero and are dropped once they fall below kSmootherPrune. That union
    // handling is most of what this class does: the two vectors rarely name
    // the same nodes, because the point has usually moved.
    const WeightVector& update(const WeightVector& target, float dt) {
        if (halfLife_ <= 0.0f && maxRate_ <= 0.0f) {
            state_ = target;
            return state_;
        }
        if (dt <= 0.0f) return state_;

        // Exponential factor from the half-life. alpha is how much of the
        // remaining distance is closed this step.
        const float alpha = (halfLife_ > 0.0f)
                          ? 1.0f - std::pow(0.5f, dt / halfLife_)
                          : 1.0f;
        const float maxStep = (maxRate_ > 0.0f) ? maxRate_ * dt : 0.0f;

        WeightVector next;
        next.reserve(state_.size() + target.size());

        // Existing entries move toward their target, which is zero when the
        // node has left the target vector.
        for (const auto& cur : state_) {
            float want = 0.0f;
            for (const auto& t : target) {
                if (t.id == cur.id) { want = t.weight; break; }
            }
            next.push_back(WeightedNode{cur.id, step(cur.weight, want,
                                                     alpha, maxStep)});
        }

        // Nodes the target has and the state does not, entering from zero.
        for (const auto& t : target) {
            const bool known = std::any_of(
                state_.begin(), state_.end(),
                [&](const WeightedNode& c) { return c.id == t.id; });
            if (!known) {
                next.push_back(WeightedNode{t.id, step(0.0f, t.weight,
                                                       alpha, maxStep)});
            }
        }

        // A node that has decayed below the prune threshold AND is not wanted
        // is dropped. The second condition matters: a node genuinely held at a
        // very small weight should stay, not flicker out.
        next.erase(std::remove_if(next.begin(), next.end(),
                       [&](const WeightedNode& w) {
                           if (std::fabs(w.weight) >= kSmootherPrune) {
                               return false;
                           }
                           return !std::any_of(target.begin(), target.end(),
                                   [&](const WeightedNode& t) {
                                       return t.id == w.id;
                                   });
                       }),
                   next.end());

        state_ = std::move(next);
        return state_;
    }

    // Jump to a vector with no smoothing. For a cue fired, a map swapped, a
    // trajectory scrubbed -- anywhere the discontinuity is intended and
    // gliding through it would be wrong.
    void snap(const WeightVector& v) { state_ = v; }

    void reset() { state_.clear(); }

    const WeightVector& current() const { return state_; }
    bool settled(const WeightVector& target, float eps = 1e-3f) const;

private:
    // One component. Exponential first, then clamped by the slew limit if one
    // is set.
    static float step(float from, float to, float alpha, float maxStep) {
        float v = from + (to - from) * alpha;
        if (maxStep > 0.0f) {
            const float d = v - from;
            if (d >  maxStep) v = from + maxStep;
            if (d < -maxStep) v = from - maxStep;
        }
        return v;
    }

    WeightVector state_;
    float halfLife_ = 0.05f;
    float maxRate_  = 0.0f;
};

// Has the smoother arrived, within tolerance, at this target?
//
// Useful because exponential smoothing never exactly arrives: a consumer that
// wants to know when a transition is over needs a threshold, and picking it
// once here is better than every caller picking a different one.
inline bool WeightSmoother::settled(const WeightVector& target,
                                    float eps) const {
    for (const auto& t : target) {
        float have = 0.0f;
        for (const auto& c : state_) {
            if (c.id == t.id) { have = c.weight; break; }
        }
        if (std::fabs(have - t.weight) > eps) return false;
    }
    for (const auto& c : state_) {
        const bool wanted = std::any_of(target.begin(), target.end(),
                [&](const WeightedNode& t) { return t.id == c.id; });
        if (!wanted && std::fabs(c.weight) > eps) return false;
    }
    return true;
}

} // namespace ofxManifold
