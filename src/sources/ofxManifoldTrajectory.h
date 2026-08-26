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

// A recorded path keeps TWO time representations, and it needs both.
//
// The normalized parameter is the portable one: t in [0, 1] across the path,
// which is what lets a gesture replay against a rebuilt map, at a different
// tempo, from a fader, or backwards.
//
// The recorded DURATION is metadata. Without it, "replay this at the speed it
// was performed" is unanswerable, because normalizing threw the answer away.
// An earlier version of this class did exactly that: the shape survived and
// the tempo did not.
//
// The shape of the fix is owed to Zachary Lieberman's timePointRecorder, from
// the drawingWithTime example (2010), which stores each sample's time in
// seconds and offers getPositionForTime() and getVelocityForTime() directly.
// Keeping the normalized parameter as the contract and the duration as
// metadata gets both: portable by default, replayable in real seconds when
// that is what is wanted.
class Trajectory : public PointSource {
public:
    // ---- recording -------------------------------------------------------

    void clear() { samples_.clear(); duration_ = 0.0f;
                   finalized_ = false; }

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

    // Rescale recorded times to [0, 1], remembering how long the recording
    // actually took. Idempotent.
    //
    // Keeping BOTH is the point. The normalized times are what make a path
    // portable between maps and replayable at any speed; the duration is what
    // lets it replay at the speed it was performed, which is the default a
    // performer wants and which normalizing alone would silently discard.
    //
    // Zach Lieberman's timePointRecorder (drawingWithTime, 2010) keeps seconds
    // throughout and exposes getDuration(). That is the right instinct and this
    // takes it; the difference is that seconds live alongside the normalized
    // parameter here rather than instead of it, because a path recorded in one
    // room has to replay in another.
    void finalize() {
        if (samples_.size() < 2) {
            if (samples_.size() == 1) samples_[0].t = 0.0f;
            duration_ = 0.0f;
            finalized_ = true;
            return;
        }
        const float t0 = samples_.front().t;
        const float span = samples_.back().t - t0;
        duration_ = (span > 0.0f) ? span : 0.0f;
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

    // How long the recording took, in whatever units addSample() was given --
    // seconds, if it was fed ofGetElapsedTimef(). Zero when unknown: a single
    // sample, coincident samples, or a path built directly from setSamples().
    float duration() const { return duration_; }
    void  setDuration(float d) { duration_ = (d > 0.0f) ? d : 0.0f; }

    // Position at a time in the ORIGINAL units. The obvious convenience, and
    // the one that makes "play it back the way I performed it" a single call.
    //
    // With no known duration this treats the path as one unit long, so it
    // degrades to pointAt() rather than returning nothing.
    glm::vec2 pointAtSeconds(float seconds) const {
        const float d = (duration_ > 0.0f) ? duration_ : 1.0f;
        return pointAt(seconds / d);
    }

    // ---- motion ----------------------------------------------------------
    //
    // Velocity along the path, as a central difference over a window.
    //
    // The architecture keeps MOTION outside the kernel (section 3.1) because
    // the evaluator is stateless and a derivative needs history. A trajectory
    // IS history -- a stored path, complete before anyone asks -- so a
    // derivative over it is a pure function of data already in hand, not a
    // stateful accumulator. That is why it lives here and d(weight)/dt does
    // not.
    //
    // Lieberman's version looks BACKWARD by a fixed 0.05 seconds, which is
    // what a performer feels: where did I just come from. Central difference
    // is more accurate for a stored path, where there is no reason to prefer
    // the past.
    //
    // At the ends the window is clipped, and the result is divided by the span
    // ACTUALLY used rather than the requested one. Dividing by the requested
    // window would halve the reported speed at t = 0 and t = 1 -- a stroke
    // that began fast would read as beginning slowly, which is precisely
    // backwards.
    glm::vec2 velocityAt(float t, float window = 0.02f) const {
        if (samples_.size() < 2 || window <= 0.0f) return glm::vec2(0.0f);

        const float a = std::max(0.0f, t - window);
        const float b = std::min(1.0f, t + window);
        const float span = b - a;
        if (span <= 0.0f) return glm::vec2(0.0f);

        return (pointAt(b) - pointAt(a)) / span;   // units per unit of t
    }

    // Velocity in original units per unit of time -- normalized distance per
    // second, if the recording was fed seconds.
    glm::vec2 velocityAtSeconds(float seconds,
                                float windowSeconds = 0.05f) const {
        const float d = (duration_ > 0.0f) ? duration_ : 1.0f;
        return velocityAt(seconds / d, windowSeconds / d) / d;
    }

    float speedAt(float t, float window = 0.02f) const {
        return glm::length(velocityAt(t, window));
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

    void setSamples(std::vector<TrajectorySample> s, float dur = 0.0f) {
        samples_ = std::move(s);
        duration_ = (dur > 0.0f) ? dur : 0.0f;
        finalized_ = true;
    }

private:
    std::vector<TrajectorySample> samples_;
    float duration_ = 0.0f;     // original units; 0 when unknown
    bool  finalized_ = false;
};

} // namespace ofxManifold
