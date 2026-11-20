#pragma once

// ofxManifold -- following, and keeping apart (round 3B-2i).
//
// A point following goes, on each step, to where the point before it was at
// the step before: point 2 after point 1, point 3 after point 2 -- a chain,
// each one step behind the one ahead. It glides there over its travel.
//
// Keeping apart is a rule of its own, so shapes later use the same one: a
// point moving itself never ends a step within the gap of another point on
// about the same level. Too close, it goes as far along its way as it can
// and waits at the gap; already within it, it waits where it is; stopping
// short off the map, it waits instead.
//
// Plain functions on numbers, no openFrameworks: checked against
// tests/ref/reference_follow.py (make test-follow).

#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace player {

// Where a point is: on the map (x, y) and its level position u (0 on the
// bottom level, 1 on the next ...).
struct Place {
    float x = 0.5f;
    float y = 0.5f;
    float u = 0.0f;
};

// Points this far apart in level position, or more, are on other levels:
// never too close, wherever they are on the map.
constexpr float kLevelApart = 1.0f;

// The way from a to b, t of it: 0 at a, 1 at b.
inline Place along(const Place& a, const Place& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.u + (b.u - a.u) * t};
}

// How far along from -> to a step may go, 0 to 1: the farthest it can end
// and be no nearer than the gap to any of the others on about the same
// level -- less than kLevelApart from it in level position. 1: all the way.
// 0: it waits -- nowhere along its way is clear. A gap of 0 is off.
//
// For each other, the stretch of the way where the end would be too close
// is an open interval of t: on the map, where |from + t v - other| < gap (a
// quadratic); on the level, where |u(t) - other.u| < kLevelApart (linear);
// both at once. The answer is the largest t in [0, 1] in none of them.
inline float keepApart(const Place& from, const Place& to,
                       const std::vector<Place>& others, float gap) {
    if (!(gap > 0.0f)) return 1.0f;
    const double inf = std::numeric_limits<double>::infinity();
    const double vx = double(to.x) - from.x, vy = double(to.y) - from.y;
    const double vu = double(to.u) - from.u;
    const double g = gap, apart = kLevelApart;
    std::vector<std::pair<double, double>> blocked;
    for (const Place& o : others) {
        const double px = double(from.x) - o.x, py = double(from.y) - o.y;
        const double a = vx * vx + vy * vy, b = 2.0 * (px * vx + py * vy);
        const double c = px * px + py * py - g * g;
        double lo, hi;
        if (!(a > 0.0)) {                        // not moving on the map
            if (!(c < 0.0)) continue;
            lo = -inf;
            hi = inf;
        } else {
            const double disc = b * b - 4.0 * a * c;
            if (!(disc > 0.0)) continue;
            const double r = std::sqrt(disc);
            lo = (-b - r) / (2.0 * a);
            hi = (-b + r) / (2.0 * a);
        }
        const double pu = double(from.u) - o.u;
        double llo, lhi;
        if (vu == 0.0) {
            if (!(std::fabs(pu) < apart)) continue;
            llo = -inf;
            lhi = inf;
        } else {
            const double t1 = (-apart - pu) / vu, t2 = (apart - pu) / vu;
            llo = t1 < t2 ? t1 : t2;
            lhi = t1 < t2 ? t2 : t1;
        }
        if (llo > lo) lo = llo;
        if (lhi < hi) hi = lhi;
        if (lo < hi) blocked.emplace_back(lo, hi);
    }
    // From the far end back: inside a stretch, back to where it starts.
    double t = 1.0;
    for (bool moved = true; moved;) {
        moved = false;
        for (const auto& s : blocked) {
            if (s.first < t && t < s.second) {
                t = s.first;
                moved = true;
            }
        }
    }
    return t > 0.0 ? float(t) : 0.0f;
}

// Where a glide from a to b is, `since` into it, over `travel` -- smooth at
// both ends, as the walks' glides. Travel 0: there at once.
inline Place glideAt(const Place& a, const Place& b, double since, double travel) {
    if (!(travel > 0.0) || since >= travel) return b;
    if (since <= 0.0) return a;
    const double s = since / travel;
    return along(a, b, float(s * s * (3.0 - 2.0 * s)));
}

// A follower's step: point (from 1), where it is, where it ends, and
// whether it was held -- stopped short at the gap, or waiting.
struct FollowMove {
    int   point = 2;
    Place from;
    Place to;
    bool  held = false;
};

// The chain: the places noted at each step, and the followers' moves.
class FollowChain {
public:
    // On the map: false off it. Empty: everywhere is on it.
    using Inside = std::function<bool(float, float)>;

    // A step. `now`: every point's place, point 1 first. `follows`: which of
    // them follow (point 1's is ignored: no one is ahead of it). Each
    // follower goes to where the point before it was at the last step, kept
    // apart from every other point where it is now -- if both were there at
    // the last step; one added since waits this step. Then every place is
    // noted for the next.
    std::vector<FollowMove> step(const std::vector<Place>& now,
                                 const std::vector<bool>& follows, float gap,
                                 const Inside& inside = Inside{}) {
        std::vector<FollowMove> moves;
        for (std::size_t k = 1; k < now.size(); ++k) {
            if (k >= follows.size() || !follows[k]) continue;
            if (k >= noted_.size()) continue;            // added since: it waits
            const Place& was = noted_[k - 1];
            std::vector<Place> others;
            for (std::size_t j = 0; j < now.size(); ++j) {
                if (j != k) others.push_back(now[j]);
            }
            const float t = keepApart(now[k], was, others, gap);
            FollowMove m;
            m.point = int(k) + 1;
            m.from = now[k];
            m.to = t >= 1.0f ? was : along(now[k], was, t);
            m.held = t < 1.0f;
            // Stopping short off the map: it waits where it is instead.
            if (m.held && t > 0.0f && inside && !inside(m.to.x, m.to.y)) m.to = now[k];
            moves.push_back(m);
        }
        noted_ = now;
        return moves;
    }

    // A new map, or the steps started again: nothing noted.
    void clear() { noted_.clear(); }
    const std::vector<Place>& noted() const { return noted_; }

private:
    std::vector<Place> noted_;
};

} // namespace player
} // namespace ofxManifold
