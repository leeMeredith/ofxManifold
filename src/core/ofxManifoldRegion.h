#pragma once

// ofxManifold — generalized regions.
//
// A region is an ordered ring of N >= 3 nodes. Three nodes take the barycentric
// solve in ofxManifoldTriangle.h; more take mean-value coordinates, after
// Hormann & Floater (2006), "Mean value coordinates for arbitrary planar
// polygons", ACM TOG 25(4), 1424-1441.
//
// WHY A VALUE TYPE RATHER THAN AN INTERFACE (PLAN-regions.md D-A).
//
// The architecture document committed to `Region` as an interface with
// `Triangle` implementing it, as an escape hatch for exactly this moment. Now
// that the moment is here, one value type holding a vector of NodeID does the
// same job with less machinery: Manifold2D stays copyable, there is no heap
// allocation per region, no virtual call, and no unique_ptr in a class that
// gets moved by loadManifold.
//
// The argument that settles it: an interface here would be a
// one-implementation abstraction that stays that way. A tetrahedron belongs to
// a Manifold3D taking a vec3 point, not here. An abstraction with two members
// and no prospect of a third is a struct wearing a costume.
//
// WHY THE TRIANGLE PATH SURVIVES.
//
// MVC reduces to barycentric coordinates at N = 3 exactly -- measured at
// 3.33e-16 over a full interior sweep -- so one path would suffice. It is kept
// because the triangle solve has 31 vectors and four mutation gates behind it,
// and because it is four multiplies and a subtract against MVC's divisions and
// square roots. Fewer operations is fewer places for D-001 to happen again.
//
// NON-CONVEX REGIONS ARE ACCEPTED (PLAN-regions.md D-C).
//
// A star is a legitimate control surface and MVC handles it. Weights can go
// negative at some interior points of a non-convex region -- measured at
// -0.0004 on a deep star and -0.013 on an L-shape -- and that is the algorithm
// working rather than a fault. A negative weight is a relationship; whether it
// is acceptable is interpretation, and clampNegative() in that layer is there
// for consumers who need it.

#include "ofxManifoldTriangle.h"

#include <cmath>
#include <string>
#include <cstddef>
#include <vector>

namespace ofxManifold {

// A point closer than this to a vertex is treated as coincident with it, and
// an area smaller than this as zero. Chosen the way kAreaEpsilon was
// (DECISIONS.md D-001): above the float noise floor, far below anything an
// author would author.
static constexpr float kRingEpsilon = 1e-9f;

// Twice the signed area of a ring. Positive for counter-clockwise winding.
inline float ringArea2(const std::vector<glm::vec2>& p) {
    float a = 0.0f;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2& u = p[i];
        const glm::vec2& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return a;
}

// Every turn the same way round.
//
// Reported as INFORMATION by validate(), never as a reason to refuse a region.
// An author who meant a star sees a note; one who mistyped a vertex order sees
// the same note and looks.
inline bool ringIsConvex(const std::vector<glm::vec2>& p,
                         float eps = kRingEpsilon) {
    const std::size_t n = p.size();
    int sign = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2& a = p[i];
        const glm::vec2& b = p[(i + 1) % n];
        const glm::vec2& c = p[(i + 2) % n];
        const float z = (b.x - a.x) * (c.y - b.y) - (c.x - b.x) * (b.y - a.y);
        if (std::fabs(z) < eps) continue;
        const int s = (z > 0.0f) ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

// Do two open segments properly cross, sharing no endpoint?
inline bool segmentsCross(glm::vec2 p1, glm::vec2 p2,
                          glm::vec2 p3, glm::vec2 p4,
                          float eps = kRingEpsilon) {
    auto orient = [eps](glm::vec2 a, glm::vec2 b, glm::vec2 c) {
        const float z = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        return (std::fabs(z) < eps) ? 0 : (z > 0.0f ? 1 : -1);
    };
    const int o1 = orient(p1, p2, p3), o2 = orient(p1, p2, p4);
    const int o3 = orient(p3, p4, p1), o4 = orient(p3, p4, p2);
    return o1 != o2 && o3 != o4 && o1 != 0 && o2 != 0 && o3 != 0 && o4 != 0;
}

// A bowtie has no coherent inside, so its vertex ordering is meaningless
// rather than merely unusual. The one shape refused at construction besides
// the genuinely degenerate.
inline bool ringSelfIntersects(const std::vector<glm::vec2>& p) {
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if ((j + 1) % n == i || (i + 1) % n == j) continue;
            if (segmentsCross(p[i], p[(i + 1) % n],
                              p[j], p[(j + 1) % n])) {
                return true;
            }
        }
    }
    return false;
}

// ---- mean-value coordinates ----------------------------------------------

struct RingResult {
    std::vector<float> w;
    bool valid = false;
};

// MVC over a ring, written as a free function of positions and a point so the
// arithmetic stays transcribable, as solveRaw() is.
//
// Two special cases, both of which the naive formula divides by zero on, and
// neither of which is rare -- a control point dragged along an edge sits in
// the second one continuously.
inline RingResult solveRing(const std::vector<glm::vec2>& p,
                            const glm::vec2& v) {
    RingResult out;
    const std::size_t n = p.size();
    if (n < 3) return out;

    std::vector<glm::vec2> s(n);
    std::vector<float> r(n);
    for (std::size_t i = 0; i < n; ++i) {
        s[i] = p[i] - v;
        r[i] = std::sqrt(s[i].x * s[i].x + s[i].y * s[i].y);
    }

    // Case 1: on a vertex. That vertex takes everything.
    for (std::size_t i = 0; i < n; ++i) {
        if (r[i] < kRingEpsilon) {
            out.w.assign(n, 0.0f);
            out.w[i] = 1.0f;
            out.valid = true;
            return out;
        }
    }

    std::vector<float> A(n), D(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        A[i] = 0.5f * (s[i].x * s[j].y - s[j].x * s[i].y);
        D[i] = s[i].x * s[j].x + s[i].y * s[j].y;
    }

    // Case 2: on an edge. Zero area with the endpoints on opposite sides,
    // which the negative dot product detects. The two endpoints split the
    // weight by distance and every other vertex is exactly zero.
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        if (std::fabs(A[i]) < kRingEpsilon && D[i] < 0.0f) {
            out.w.assign(n, 0.0f);
            const float total = r[i] + r[j];
            if (total < kRingEpsilon) return out;
            out.w[i] = r[j] / total;
            out.w[j] = r[i] / total;
            out.valid = true;
            return out;
        }
    }

    out.w.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t h = (i + n - 1) % n;
        const std::size_t j = (i + 1) % n;
        if (std::fabs(A[h]) > kRingEpsilon) {
            out.w[i] += (r[h] - D[h] / r[i]) / A[h];
        }
        if (std::fabs(A[i]) > kRingEpsilon) {
            out.w[i] += (r[j] - D[i] / r[i]) / A[i];
        }
    }

    float total = 0.0f;
    for (float x : out.w) total += x;
    if (std::fabs(total) < 1e-12f) return out;

    const float inv = 1.0f / total;
    for (float& x : out.w) x *= inv;
    out.valid = true;
    return out;
}

// ---- the region value type ------------------------------------------------

class Region {
public:
    Region() = default;

    // Why a ring is not constructible. Empty string when it is.
    //
    // ORDER MATTERS. Self-intersection is tested before area, because a
    // bowtie's lobes have opposite signed area and cancel to zero -- an area
    // test placed first reports "degenerate", which is the right verdict for
    // the wrong reason and sends the author looking for a collapsed region
    // instead of a swapped vertex. Same failure as DECISIONS.md D-007.
    static std::string invalidReason(const std::vector<NodeID>& ids,
                                     const std::vector<glm::vec2>& p) {
        if (ids.size() < 3) return "fewer than three nodes";
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (std::size_t j = i + 1; j < ids.size(); ++j) {
                if (ids[i] == ids[j]) return "repeated node";
            }
        }
        if (ringSelfIntersects(p)) return "self-intersecting";
        if (std::fabs(ringArea2(p)) < kAreaEpsilon) {
            return "degenerate: area below epsilon";
        }
        return "";
    }

    static bool make(std::vector<NodeID> ids,
                     const std::vector<glm::vec2>& p,
                     Region& out) {
        if (!invalidReason(ids, p).empty()) return false;
        out.ids_ = std::move(ids);
        const float a2 = ringArea2(p);
        out.constructionSign_ = (a2 > 0.0f) ? 1 : -1;
        out.convex_ = ringIsConvex(p);
        return true;
    }

    const std::vector<NodeID>& ids() const { return ids_; }
    std::size_t size() const { return ids_.size(); }

    // Winding recorded at construction, so the animated-position flip test
    // (architecture doc 8.6) has a comparand at any arity.
    int  constructionSign() const { return constructionSign_; }

    // Information, not a verdict. See PLAN-regions.md D-C.
    bool convex() const { return convex_; }

    // Weights for a point, dispatching on arity.
    //
    // Three nodes take the barycentric path; the rest take MVC. The two agree
    // at N = 3 -- asserted by the N3AGREE vectors rather than assumed -- which
    // is what makes this an extension rather than a replacement.
    RingResult evaluate(const std::vector<glm::vec2>& p,
                        const glm::vec2& v,
                        const std::vector<float>& bias) const {
        RingResult out;
        if (ids_.size() == 3) {
            const BarycentricResult b =
                solveBiased(p[0], p[1], p[2], v,
                            bias[0], bias[1], bias[2]);
            if (!b.valid) return out;
            out.w.assign(b.w.begin(), b.w.end());
            out.valid = true;
            return out;
        }

        out = solveRing(p, v);
        if (!out.valid) return out;

        // Per-node bias, applied before renormalization exactly as it is for
        // triangles (architecture doc 6.3). The same rule at any arity.
        float total = 0.0f;
        for (std::size_t i = 0; i < out.w.size(); ++i) {
            out.w[i] *= bias[i];
            total += out.w[i];
        }
        if (std::fabs(total) < kAreaEpsilon) {
            out.valid = false;
            return out;
        }
        const float inv = 1.0f / total;
        for (float& x : out.w) x *= inv;
        return out;
    }

    // Inside when no weight is negative.
    //
    // For a convex region this is the ordinary containment test. For a
    // non-convex one it is STRICTER than geometric containment: a point inside
    // an L-shape can carry a small negative weight and be reported outside.
    // That is deliberate, and it is why containsRing() exists separately for
    // callers who want the geometric answer.
    bool contains(const std::vector<glm::vec2>& p, const glm::vec2& v,
                  float eps = kEdgeEpsilon) const {
        const RingResult r = (ids_.size() == 3)
            ? [&] {
                RingResult t;
                const BarycentricResult b = solveRaw(p[0], p[1], p[2], v);
                if (b.valid) { t.w.assign(b.w.begin(), b.w.end()); t.valid = true; }
                return t;
              }()
            : solveRing(p, v);
        if (!r.valid) return false;
        for (float x : r.w) if (x < -eps) return false;
        return true;
    }

private:
    std::vector<NodeID> ids_;
    int  constructionSign_ = 0;
    bool convex_ = true;
};

} // namespace ofxManifold
