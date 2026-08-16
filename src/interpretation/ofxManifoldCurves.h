#pragma once

// ofxManifold — response curves.
//
// A curve reshapes each weight independently. It is the layer where "these
// weights are gains" or "these weights are parameter positions" first gets
// decided, and it is deliberately NOT part of the evaluator (architecture doc
// section 7 in the original, section 9.1 in v2).
//
// MIAP offers cosine, square root, and linear. Seldess added the alternatives
// to SpaceMap's cosine specifically because the weights might be driving plugin
// parameters or lighting-board faders, where constant-power is meaningless or
// actively wrong. That argument is the whole reason this file exists separately
// from the solve.
//
// IMPORTANT: a curve does NOT preserve partition of unity, and must not
// renormalize to restore it.
//
// Barycentric weights sum to one. Equal-power gains do not: they preserve the
// sum of SQUARES instead. Renormalizing after a square-root curve would undo
// exactly the property the curve exists to create. If a caller wants weights
// that sum to one again, they call normalize() explicitly and take
// responsibility for having discarded the power property.

#include "../core/ofxManifoldTypes.h"

#include <cmath>
#include <vector>

namespace ofxManifold {

using WeightVector = std::vector<WeightedNode>;

namespace curve {

// Identity. The right choice whenever the weights are positions in a parameter
// space rather than amplitudes -- a filter cutoff blended 50/50 wants 0.5, not
// 0.707.
inline float linear(float w) { return w; }

// Equal power. Two weights summing to one produce gains whose squares sum to
// one, so a source panned between two outputs holds constant acoustic power
// across the move.
inline float equalPower(float w) {
    return (w <= 0.0f) ? 0.0f : std::sqrt(w);
}

// SpaceMap's original shape. Also constant-power for the two-node case, and it
// differs from equalPower for three or more -- which is the case a triangular
// manifold is in most of the time. Offered because the two feel different in
// the room, not because one is correct.
inline float cosine(float w) {
    if (w <= 0.0f) return 0.0f;
    if (w >= 1.0f) return 1.0f;
    return std::sin(w * 1.57079632679489661923f);   // sin(w * pi/2)
}

using Fn = float (*)(float);

// Apply a curve to every component. Node identity is untouched: this layer
// reshapes magnitudes and has no business reordering or dropping anything.
inline WeightVector apply(const WeightVector& in, Fn fn) {
    WeightVector out;
    out.reserve(in.size());
    for (const auto& wn : in) {
        out.push_back(WeightedNode{wn.id, fn(wn.weight)});
    }
    return out;
}

} // namespace curve

// Restore partition of unity. Explicit, never automatic.
//
// Returns the input unchanged if the sum is too near zero to divide by, rather
// than emitting NaN. A silently NaN weight vector downstream is the identity
// buffer failure again: everything looks structured, nothing is valid.
inline WeightVector normalize(const WeightVector& in) {
    float sum = 0.0f;
    for (const auto& wn : in) sum += wn.weight;
    if (std::fabs(sum) < 1e-9f) return in;

    WeightVector out;
    out.reserve(in.size());
    const float inv = 1.0f / sum;
    for (const auto& wn : in) {
        out.push_back(WeightedNode{wn.id, wn.weight * inv});
    }
    return out;
}

// Sum of squares. The quantity equalPower and cosine hold constant, and the
// thing worth asserting in a vector rather than trusting.
inline float power(const WeightVector& in) {
    float p = 0.0f;
    for (const auto& wn : in) p += wn.weight * wn.weight;
    return p;
}

inline float sum(const WeightVector& in) {
    float s = 0.0f;
    for (const auto& wn : in) s += wn.weight;
    return s;
}

} // namespace ofxManifold
