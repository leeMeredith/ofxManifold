#include "ofxManifoldOutputSmoother.h"

#include <algorithm>
#include <cmath>

namespace ofxManifold {
namespace player {

void OutputSmoother::setHalfLife(float seconds) { halfLife_ = std::max(0.0f, seconds); }

void OutputSmoother::setMaxRate(float perSecond) { maxRate_ = std::max(0.0f, perSecond); }

void OutputSmoother::snap(const std::vector<float>& v) {
    state_ = v;
    settled_ = true;
}

void OutputSmoother::reset() {
    state_.clear();
    settled_ = true;
}

const std::vector<float>& OutputSmoother::update(const std::vector<float>& target, float dt) {
    if (target.size() != state_.size() || !enabled()) {
        snap(target);
        return state_;
    }
    if (dt <= 0.0f) {
        settled_ = state_ == target;
        return state_;
    }
    // The kernel's step (WeightSmoother::step): the share of the remaining
    // distance closed in dt, then the speed limit.
    const float alpha = halfLife_ > 0.0f ? 1.0f - std::pow(0.5f, dt / halfLife_) : 1.0f;
    const float maxStep = maxRate_ > 0.0f ? maxRate_ * dt : 0.0f;
    settled_ = true;
    for (std::size_t k = 0; k < state_.size(); ++k) {
        const float from = state_[k], to = target[k];
        float v = from + (to - from) * alpha;
        if (maxStep > 0.0f) {
            const float d = v - from;
            if (d > maxStep)  v = from + maxStep;
            if (d < -maxStep) v = from - maxStep;
        }
        if (std::fabs(to - v) < kArrived) v = to;
        state_[k] = v;
        if (v != to) settled_ = false;
    }
    return state_;
}

} // namespace player
} // namespace ofxManifold
