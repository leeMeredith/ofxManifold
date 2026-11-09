#pragma once

// ofxManifoldPlayer -- smoothing what is sent (round 3B-1).
//
// The kernel's WeightSmoother (src/interpretation) smooths the weights of the
// map's NODES: its state is every node the point has lately been near, which
// grows with how fast the point moves and how fine the map is, is matched up
// by search every frame, and knows only the floor plan -- a node on the floor
// and the same node on the level above are one id to it.
//
// A player sends one level per OUTPUT, and how many outputs there are is
// fixed by the map. So this smooths those: a fixed list of numbers, one per
// output (or per node value), the same length every frame. Its size never
// grows with how many nodes a point passes, it needs no pruning and no
// search, and one per moving object costs objects x outputs, a plain loop.
// It covers what the kernel's covers -- a jump, leaving the map, a level
// crossed -- because each of those is a jump in the levels sent.
//
// The arithmetic of one number is the kernel's, step for step (the tests
// drive both): a half-life, so it is the same at any frame rate, then an
// optional limit on speed. One thing more: within kArrived of its target a
// number IS its target. Exponential smoothing never quite arrives, so without
// this a still point would go on sending changes too small to hear for about
// a second after every move; with it, per-output sending goes quiet.
//
// A different list -- another map's outputs, or a different count -- is not
// glided into: the smoother starts over at it, as the kernel's snap() does
// for a swapped map. Call reset() when the outputs change but keep their
// count (a map opened in place of another of the same size).

#include <cstddef>
#include <vector>

namespace ofxManifold {
namespace player {

class OutputSmoother {
public:
    // Within this of its target, a number is set to its target: 1e-4 of full
    // level is -80 dB.
    static constexpr float kArrived = 1e-4f;

    // Time for the remaining distance to halve, in the units of update()'s
    // dt. Zero or less: no exponential smoothing.
    void  setHalfLife(float seconds);
    float halfLife() const { return halfLife_; }

    // A limit on how fast any number may change, per unit of time. Zero: no
    // limit. With both, the exponential step is clamped by the limit.
    void  setMaxRate(float perSecond);
    float maxRate() const { return maxRate_; }

    // Neither half-life nor limit: update() returns its target.
    bool enabled() const { return halfLife_ > 0.0f || maxRate_ > 0.0f; }

    // Move toward `target` by `dt` and return where it is. A target of a
    // different length from where it is -- the first ever, the first after
    // reset(), another map's -- or any target while disabled, is taken at
    // once.
    const std::vector<float>& update(const std::vector<float>& target, float dt);

    // Take this list at once.
    void snap(const std::vector<float>& v);
    // Forget: the next update() takes its target at once (nothing is the
    // length of no list but the empty one, which there is nothing to glide).
    void reset();

    const std::vector<float>& current() const { return state_; }
    // Every number at its target after the last update().
    bool settled() const { return settled_; }

private:
    std::vector<float> state_;
    bool  settled_  = true;
    float halfLife_ = 0.05f;
    float maxRate_  = 0.0f;
};

} // namespace player
} // namespace ofxManifold
