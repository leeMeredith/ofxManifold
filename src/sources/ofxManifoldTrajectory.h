#pragma once

// ofxManifold — recorded trajectories.
//
// A path through the control space, stored as samples and replayed against ANY
// manifold.
//
// That last part is the whole point, and it is why section 5 chose normalized
// coordinates. SpaceMap renders a move in real time during the performance, so
// a touring production builds a new map matching the new rig and replays its
// existing cues untouched. The map changes; the trajectory does not.
//
// A trajectory in screen pixels could not do that. A trajectory in the
// manifold's own normalized space can, and the vectors prove it: the same path
// evaluated against two different maps yields the same POSITIONS and different
// WEIGHTS, which is exactly the touring case.

#include "ofxManifoldPointSource.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ofxManifold {

struct TrajectorySample {
    float     t = 0.0f;          // normalized [0, 1] after finalize()
    glm::vec2 position{0.0f, 0.0f};
};

class Trajectory : public PointSource {
public:
    // ---- recording -------------------------------------------------------

    void clear() { samples_.clear(); finalized_ = false; }

    // Append a sample. `t` may be in any monotonically non-decreasing units --
    // seconds, frames, beats -- and finalize() rescales the whole path to
    // [0, 1] afterwards.
    //
    // Storing the ORIGINAL spacing rather than resampling uniformly is what
    // preserves performance: if the performer held still for two seconds, the
    // replay holds still for the same share of the path. Uniform resampling
    // would quietly turn a pause into a slow drift.
    void addSample(float t, glm::vec2 position) {
        if (!samples_.empty() && t < samples_.back().t) {
            t = samples_.back().t;      // never allow time to run backwards
        }
        samples_.push_back(TrajectorySample{t, position});
        finalized_ = false;
    }

    // Rescale recorded times to [0, 1]. Idempotent.
    void finalize() {
        if (samples_.size() < 2) {
            if (samples_.size() == 1) samples_[0].t = 0.0f;
            finalized_ = true;
            return;
        }
        const float t0 = samples_.front().t;
        const float span = samples_.back().t - t0;
        if (span <= 0.0f) {
            // Every sample at the same instant. Spread them evenly rather than
            // dividing by zero: the path still has shape even if its timing
            // was lost.
            for (std::size_t i = 0; i < samples_.size(); ++i) {
                samples_[i].t = static_cast<float>(i)
                              / static_cast<float>(samples_.size() - 1);
            }
        } else {
            for (auto& s : samples_) s.t = (s.t - t0) / span;
        }
        finalized_ = true;
    }

    // ---- playback --------------------------------------------------------

    // Linear interpolation between the bracketing samples.
    //
    // t outside [0, 1] CLAMPS to the endpoints rather than extrapolating. A
    // trajectory describes where a source went, and inventing where it would
    // have gone next is a different operation that a caller should ask for
    // explicitly.
    glm::vec2 pointAt(float t) const override {
        if (samples_.empty()) return glm::vec2(0.0f);
        if (samples_.size() == 1) return samples_[0].position;

        if (t <= samples_.front().t) return samples_.front().position;
        if (t >= samples_.back().t)  return samples_.back().position;

        // First sample with time strictly greater than t.
        const auto it = std::upper_bound(
            samples_.begin(), samples_.end(), t,
            [](float v, const TrajectorySample& s) { return v < s.t; });

        const TrajectorySample& b = *it;
        const TrajectorySample& a = *(it - 1);

        const float span = b.t - a.t;
        if (span <= 0.0f) return a.position;    // coincident samples
        const float u = (t - a.t) / span;
        return a.position + (b.position - a.position) * u;
    }

    // ---- access ----------------------------------------------------------

    std::size_t sampleCount() const { return samples_.size(); }
    bool        empty()       const { return samples_.empty(); }
    bool        finalized()   const { return finalized_; }
    const TrajectorySample& sample(std::size_t i) const { return samples_[i]; }
    const std::vector<TrajectorySample>& samples() const { return samples_; }

    // Total path length in normalized units. Useful for a consumer that wants
    // constant SPEED rather than constant time -- a path with a pause in it
    // covers ground unevenly, and sometimes that is wanted and sometimes not.
    float length() const {
        float d = 0.0f;
        for (std::size_t i = 1; i < samples_.size(); ++i) {
            d += glm::distance(samples_[i - 1].position, samples_[i].position);
        }
        return d;
    }

    void setSamples(std::vector<TrajectorySample> s) {
        samples_ = std::move(s);
        finalized_ = true;
    }

private:
    std::vector<TrajectorySample> samples_;
    bool finalized_ = false;
};

} // namespace ofxManifold
