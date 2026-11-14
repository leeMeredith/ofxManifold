#pragma once

// ofxManifold -- movement (round 3B-2).
//
// A Motion answers one question: where is the point at a given count -- x and
// y on the map, and a height. That is the whole shared idea, and it is earned:
// a recording, a random jump and a random glide all fit it. Nothing else is
// shared until a second kind needs it.
//
// THE COUNT IS THE CLOCK'S, NOT SECONDS. Seconds by default; bars when
// SuperCollider's phase drives it (ofxManifoldPlayer's PhaseClock, speaking
// ofxScPhaseBridge's /phase); a fader if someone wires one. Every setting is in
// the same unit -- the step interval, the travel, the speed -- so a walk set
// to a step every 1/16 steps on SuperCollider's sixteenths and stretches with
// its tempo. PointSource said it first: the general form of a source is
// deliberately not time.
//
// ASKED, NOT STEPPED. A motion is asked a count; it does not advance a frame.
// So it is the same at any frame rate, can be paused, scrubbed or run
// backwards, and is tested as the trajectory is: positions at given counts.
//
// IT KNOWS NOTHING OF MAPS OR SOUND. It gives positions in the map's own
// coordinates. Where a walk must stay on the map, the player hands it a yes/no
// test (setInside); the walk never sees the map.
//
// NOT A PointSource: a PointSource is a finite extent asked at 0..1, and a
// random walk has no end. A finite motion is one by at(t * duration()).
//
// Worked in double: a float second loses milliseconds after a few hours, and
// the Python reference (tests/ref/reference_motion.py) works in doubles too, so
// the two make the same choices.
//
// Left room for, not built: circles, spirals, an orbit whose centre is itself
// a Motion; edges for shapes that can leave the map. Nothing below assumes a
// position is a constant beyond the plain data a later slice can turn into a
// Motion. Flocking does not fit "where at a given count" -- objects steering by
// each other must be simulated forward together -- and gets a stepped form of
// its own.

#include "ofxManifoldTrajectory.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace ofxManifold {

class Motion {
public:
    virtual ~Motion() = default;
    // x, y on the map; z the height, in the map's units.
    virtual glm::vec3 at(double t) const = 0;
    // How long it lasts, in its count; infinity when it does not end.
    virtual double duration() const { return std::numeric_limits<double>::infinity(); }
};

// ---- the generator ----------------------------------------------------------

// Sebastiano Vigna's splitmix64. Our own, a few lines: the standard library's
// distributions differ between compilers, and the Python reference must draw
// exactly the same numbers. Checked against its published outputs.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed = 0) : state_(seed) {}
    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // 0 <= u < 1: the top 24 bits over 2^24, exact in a float.
    double unit() { return double(next() >> 40) * (1.0 / 16777216.0); }
    std::uint64_t state() const { return state_; }

private:
    std::uint64_t state_;
};

// ---- a recording as a motion ------------------------------------------------

// The trajectory at t of its own duration, with its height. Before 0, its
// start; after its end, held (the default) or looped. A recording is in the
// units of the clock it was recorded against: recorded in bars, it plays in
// bars, in time at any tempo. Unknown duration counts as one unit, as
// pointAtSeconds already does.
class RecordedMotion : public Motion {
public:
    enum class End { Hold, Loop };

    RecordedMotion() = default;
    explicit RecordedMotion(Trajectory path, End end = End::Hold)
        : path_(std::move(path)), end_(end) {}

    glm::vec3 at(double t) const override {
        const double d = length();
        double s = t;
        if (s < 0.0) s = 0.0;
        else if (end_ == End::Loop) s = std::fmod(s, d);
        else if (s > d) s = d;
        return path_.point3At(static_cast<float>(s / d));
    }
    double duration() const override {
        return end_ == End::Loop ? std::numeric_limits<double>::infinity() : length();
    }

    const Trajectory& path() const { return path_; }
    End end() const { return end_; }
    void setEnd(End e) { end_ = e; }

private:
    double length() const {
        return path_.duration() > 0.0f ? double(path_.duration()) : 1.0;
    }
    Trajectory path_;
    End end_ = End::Hold;
};

// ---- a random walk: jump and glide ------------------------------------------

// When steps happen.
struct StepTiming {
    enum class Kind { Every, At, Pattern };
    Kind kind = Kind::Every;
    double interval = 0.5;           // Every: the count between steps
    double jitter = 0.0;             // Every: each interval x (1 +/- jitter), 0..0.9
    std::vector<double> at;          // At: the counts (clicks); Pattern: offsets
    double cycle = 1.0;              // Pattern: the cycle the offsets repeat in

    static StepTiming every(double interval, double jitter = 0.0) {
        StepTiming s; s.kind = Kind::Every; s.interval = interval; s.jitter = jitter; return s;
    }
    static StepTiming clicks(std::vector<double> counts = {}) {
        StepTiming s; s.kind = Kind::At; s.at = std::move(counts); return s;
    }
    static StepTiming pattern(double cycle, std::vector<double> offsets) {
        StepTiming s; s.kind = Kind::Pattern; s.cycle = cycle; s.at = std::move(offsets); return s;
    }
};

enum class Easing { Linear, Smooth };

struct WalkSettings {
    std::uint64_t seed = 1;
    glm::dvec3    start{0.5, 0.5, 0.0};
    StepTiming    steps;
    // Travel: the count a trip takes (0: a jump), or with bySpeed, map units
    // per count, so a long trip takes longer.
    bool          bySpeed = false;
    double        travel = 0.4;
    Easing        easing = Easing::Smooth;
    // Bounds: a rectangle and a height range (low = high keeps one height).
    double        x0 = 0.0, y0 = 0.0, x1 = 1.0, y1 = 1.0;
    double        zLow = 0.0, zHigh = 0.0;
    // How far one step may go from where it starts; 0: anywhere.
    double        step = 0.0;
    // The file's "inside": "map" -- the player is to supply the map's test.
    bool          insideMap = false;
    // With an inside test: a destination is refused if any of 16 points on
    // the straight way there is off the map.
    bool          straight = true;
};

// Jump and glide are one thing: a jump is a glide that takes no time to
// arrive. Step k happens at count t_k; it starts from WHERE THE POINT IS at
// t_k -- mid-trip if a step comes before the last arrived -- picks a
// destination inside the bounds, travels there, eased, and holds.
//
// The same seed, settings and steps give the same walk, always: choices draw
// from splitmix64(seed); jitter draws from a stream of its own (seed ^
// kTimeStream), so jitter never changes where the steps go.
//
// Memory stays small: step k depends on step k - 1, so asking for an hour in
// walks the hour. It keeps where it last got to (playing forward is one step
// at a time) and a checkpoint every kCheckpoint steps, about 48 bytes each; an
// earlier count starts from the checkpoint before it. ONE THREAD: the cache is
// mutable inside the const at().
class RandomWalk : public Motion {
public:
    static constexpr int           kTries = 64;
    static constexpr int           kAlong = 16;
    static constexpr std::size_t   kCheckpoint = 256;
    static constexpr std::uint64_t kTimeStream = 0x5851F42D4C957F2DULL;

    explicit RandomWalk(WalkSettings s = {}) : s_(std::move(s)) { restart(); }

    const WalkSettings& settings() const { return s_; }

    // The map's yes/no, from the player; empty: the rectangle alone.
    void setInside(std::function<bool(glm::dvec2)> inside) {
        inside_ = std::move(inside);
        restart();
    }
    bool hasInside() const { return bool(inside_); }

    // A click: one more step, at this count. Only with StepTiming::At, and
    // only at or after the last; anything else is refused (false).
    bool addStep(double t) {
        if (s_.steps.kind != StepTiming::Kind::At) return false;
        if (!s_.steps.at.empty() && t < s_.steps.at.back()) return false;
        s_.steps.at.push_back(t);
        return true;
    }

    glm::vec3 at(double t) const override { return glm::vec3(atPrecise(t)); }

    glm::dvec3 atPrecise(double t) const {
        const State& st = walkTo(t);
        return position(st, t);
    }

    // How many steps have happened by t.
    std::size_t stepsBy(double t) const { return walkTo(t).k; }

    // When step k (from 1) happens; infinity if it never does.
    double stepTime(std::size_t k) const {
        if (k == 0) return -std::numeric_limits<double>::infinity();
        State st = initial();
        for (std::size_t i = 1; i < k; ++i) {
            if (!(nextTime(st).first < std::numeric_limits<double>::infinity()))
                return std::numeric_limits<double>::infinity();
            st = advance(st);
        }
        return nextTime(st).first;
    }

    // Checkpoints kept so far (for the tests: memory stays small).
    std::size_t checkpoints() const { return checkpoints_.size(); }

private:
    struct State {
        std::size_t k = 0;                       // steps taken
        double      tk = -std::numeric_limits<double>::infinity();
        glm::dvec3  from{0.0}, dest{0.0};
        double      dur = 0.0;
        SplitMix64  place, time;
    };

    void restart() {
        checkpoints_.clear();
        checkpoints_.push_back(initial());
        cur_ = checkpoints_.front();
    }

    State initial() const {
        State st;
        st.from = st.dest = s_.start;
        st.place = SplitMix64(s_.seed);
        st.time = SplitMix64(s_.seed ^ kTimeStream);
        return st;
    }

    // When step k+1 happens, and the time stream after drawing for it.
    std::pair<double, SplitMix64> nextTime(const State& st) const {
        const StepTiming& c = s_.steps;
        SplitMix64 gen = st.time;
        switch (c.kind) {
        case StepTiming::Kind::Every: {
            double i = c.interval;
            if (c.jitter > 0.0) i = c.interval * (1.0 + c.jitter * (2.0 * gen.unit() - 1.0));
            const double base = st.k == 0 ? 0.0 : st.tk;
            return {base + i, gen};
        }
        case StepTiming::Kind::At:
            return {st.k < c.at.size() ? c.at[st.k] : std::numeric_limits<double>::infinity(), gen};
        case StepTiming::Kind::Pattern: {
            if (c.at.empty()) return {std::numeric_limits<double>::infinity(), gen};
            const std::size_t m = c.at.size();
            return {double(st.k / m) * c.cycle + c.at[st.k % m], gen};
        }
        }
        return {std::numeric_limits<double>::infinity(), gen};
    }

    glm::dvec3 position(const State& st, double t) const {
        if (st.k == 0) return s_.start;
        if (t < st.tk) return st.from;
        if (st.dur <= 0.0 || t >= st.tk + st.dur) return st.dest;
        const double u = (t - st.tk) / st.dur;
        const double e = s_.easing == Easing::Smooth ? u * u * (3.0 - 2.0 * u) : u;
        return glm::dvec3(st.from.x + (st.dest.x - st.from.x) * e,
                          st.from.y + (st.dest.y - st.from.y) * e,
                          st.from.z + (st.dest.z - st.from.z) * e);
    }

    bool ok(const glm::dvec3& p, const glm::dvec3& c, bool within) const {
        if (!within) return false;
        if (!(s_.x0 <= c.x && c.x <= s_.x1 && s_.y0 <= c.y && c.y <= s_.y1)) return false;
        if (!inside_) return true;
        if (!inside_(glm::dvec2(c.x, c.y))) return false;
        if (!s_.straight) return true;
        for (int i = 1; i <= kAlong; ++i) {
            const double f = double(i) / double(kAlong);
            const glm::dvec2 q(p.x + (c.x - p.x) * f, p.y + (c.y - p.y) * f);
            if (!inside_(q)) return false;
        }
        return true;
    }

    // Up to kTries candidates, three draws each whether it passes or not, so
    // the sequence never depends on which tests ran. Within the step distance:
    // a square of that half-width, a draw outside the circle a failed try (no
    // sine or cosine, so C++ and Python agree to the last bit). None passing:
    // it stays where it is.
    glm::dvec3 choose(const glm::dvec3& p, SplitMix64& gen) const {
        for (int n = 0; n < kTries; ++n) {
            const double ux = gen.unit(), uy = gen.unit(), uz = gen.unit();
            glm::dvec3 c;
            bool within = true;
            if (s_.step > 0.0) {
                const double dx = (2.0 * ux - 1.0) * s_.step;
                const double dy = (2.0 * uy - 1.0) * s_.step;
                within = dx * dx + dy * dy <= s_.step * s_.step;
                c.x = p.x + dx;
                c.y = p.y + dy;
            } else {
                c.x = s_.x0 + ux * (s_.x1 - s_.x0);
                c.y = s_.y0 + uy * (s_.y1 - s_.y0);
            }
            c.z = s_.zLow + uz * (s_.zHigh - s_.zLow);
            if (ok(p, c, within)) return c;
        }
        return p;
    }

    State advance(const State& st) const {
        const auto nt = nextTime(st);
        State n;
        n.k = st.k + 1;
        n.tk = nt.first;
        n.time = nt.second;
        n.from = position(st, n.tk);
        n.place = st.place;
        n.dest = choose(n.from, n.place);
        if (s_.bySpeed) {
            const glm::dvec3 d = n.dest - n.from;
            n.dur = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) / s_.travel;
        } else {
            n.dur = s_.travel;
        }
        return n;
    }

    // The state of the last step at or before t: from where it last got to
    // when that is not past t, else from the checkpoint before t.
    const State& walkTo(double t) const {
        if (!(cur_.k == 0 || cur_.tk <= t)) {
            std::size_t i = checkpoints_.size();
            while (i > 1 && !(checkpoints_[i - 1].tk <= t)) --i;
            cur_ = checkpoints_[i - 1];
        }
        for (;;) {
            if (!(nextTime(cur_).first <= t)) break;
            cur_ = advance(cur_);
            if (cur_.k % kCheckpoint == 0 && checkpoints_.size() == cur_.k / kCheckpoint)
                checkpoints_.push_back(cur_);
        }
        return cur_;
    }

    WalkSettings s_;
    std::function<bool(glm::dvec2)> inside_;
    mutable State cur_;
    mutable std::vector<State> checkpoints_;
};

} // namespace ofxManifold
