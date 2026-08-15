#pragma once

// ofxManifold — triangle barycentric evaluation.
//
// Formulation: signed sub-triangle areas via 2D cross products. For point P in
// triangle ABC the three sub-triangles PBC, PCA, PAB are formed; each signed
// area becomes the coordinate of the vertex NOT contained in it.
//
//                  A
//                 . .
//                .  |  .
//               .   P   .
//              .  .   .  .
//             B - - - - - C
//
//         area(PBC) -> wA
//         area(PCA) -> wB
//         area(PAB) -> wC
//
// The Python reference in tests/ref uses Cramer's rule on the linear system
// instead. The two are algebraically equivalent and structurally unrelated, so
// agreement between them is evidence rather than tautology.
//
// solveRaw() is deliberately a free function of three positions and a point,
// with no object state and no container access. That keeps the arithmetic
// transcribable to GLSL unchanged, should the lookup-texture bake in §21.5
// ever be built.

#include "ofxManifoldTypes.h"

#include <array>
#include <cmath>

namespace ofxManifold {

// Twice the signed area of triangle (u, v, w). Positive for counter-clockwise
// winding. Retained per region at construction so the animated-position sign
// flip test (architecture doc §8.6) has something to compare against.
inline float signedArea2(const glm::vec2& u,
                         const glm::vec2& v,
                         const glm::vec2& w) {
    return (v.x - u.x) * (w.y - u.y) - (w.x - u.x) * (v.y - u.y);
}

struct BarycentricResult {
    std::array<float, 3> w{0.0f, 0.0f, 0.0f};
    bool valid = false;   // false only if the triangle is degenerate
};

// Raw barycentric coordinates, no per-node bias.
//
// Normalization is by the SUM of the signed sub-areas rather than by the parent
// area. Algebraically these are identical; using the sum keeps the biased path
// below a strict extension of this one rather than a separate formula.
inline BarycentricResult solveRaw(const glm::vec2& a,
                                  const glm::vec2& b,
                                  const glm::vec2& c,
                                  const glm::vec2& p) {
    BarycentricResult r;

    const float sA = signedArea2(p, b, c);
    const float sB = signedArea2(p, c, a);
    const float sC = signedArea2(p, a, b);

    const float total = sA + sB + sC;   // == signedArea2(a, b, c)

    if (std::fabs(total) < kAreaEpsilon) {
        return r;                        // degenerate; valid stays false
    }

    const float inv = 1.0f / total;
    r.w = {sA * inv, sB * inv, sC * inv};
    r.valid = true;
    return r;
}

// Barycentric coordinates with per-node bias applied before renormalization.
//
// SPEC rule, architecture doc §6.3. SpaceMap biases silent nodes and does not
// publish the value; MIAP exposes a multiplier defaulting to 1. Neither
// documents multiply-then-renormalize specifically. This is our decision, and
// the vectors covering it are classed SPEC for exactly that reason.
inline BarycentricResult solveBiased(const glm::vec2& a,
                                     const glm::vec2& b,
                                     const glm::vec2& c,
                                     const glm::vec2& p,
                                     float wa, float wb, float wc) {
    BarycentricResult r = solveRaw(a, b, c, p);
    if (!r.valid) {
        return r;
    }

    const std::array<float, 3> bias{wa, wb, wc};
    std::array<float, 3> scaled{};
    float total = 0.0f;
    for (int i = 0; i < 3; ++i) {
        scaled[i] = r.w[i] * bias[i];
        total += scaled[i];
    }

    // A bias vector that annihilates every contributing coordinate leaves
    // nothing to normalize. Report invalid rather than emitting NaN — the
    // failure mode this guards against is a corrupted weight vector that looks
    // plausible downstream.
    if (std::fabs(total) < kAreaEpsilon) {
        r.valid = false;
        return r;
    }

    const float inv = 1.0f / total;
    for (int i = 0; i < 3; ++i) {
        r.w[i] = scaled[i] * inv;
    }
    return r;
}

// A triangle references nodes; it does not own them.
class Triangle {
public:
    Triangle() = default;

    // Construction validates. A zero- or near-zero-area triangle divides by
    // zero in the solve, and that is a construction error rather than a runtime
    // condition — so it is rejected here and never checked per-evaluate.
    static bool make(NodeID ia, NodeID ib, NodeID ic,
                     const glm::vec2& pa, const glm::vec2& pb,
                     const glm::vec2& pc,
                     Triangle& out) {
        const float area2 = signedArea2(pa, pb, pc);
        if (std::fabs(area2) < kAreaEpsilon) {
            return false;
        }
        out.ids_ = {ia, ib, ic};
        out.constructionSign_ = (area2 > 0.0f) ? 1 : -1;
        return true;
    }

    const std::array<NodeID, 3>& ids() const { return ids_; }

    // Winding sign recorded at construction. §8.6: a node animated far enough
    // to invert the triangle drives the signed area through zero, and weights
    // blow up on the way. Comparing against this makes that checkable rather
    // than discovered.
    int constructionSign() const { return constructionSign_; }

    bool contains(const glm::vec2& pa, const glm::vec2& pb,
                  const glm::vec2& pc, const glm::vec2& p,
                  float eps = kEdgeEpsilon) const {
        const BarycentricResult r = solveRaw(pa, pb, pc, p);
        if (!r.valid) {
            return false;
        }
        return r.w[0] >= -eps && r.w[1] >= -eps && r.w[2] >= -eps;
    }

private:
    std::array<NodeID, 3> ids_{InvalidNode, InvalidNode, InvalidNode};
    int constructionSign_ = 0;
};

} // namespace ofxManifold
