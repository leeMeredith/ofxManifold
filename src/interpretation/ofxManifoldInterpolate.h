#pragma once

// ofxManifold — interpolation.
//
// Weights plus values give a blended value. This is the operation the addon's
// own one-line description promises -- "place your presets as nodes, drag a
// point, get a weighted blend" -- and it was missing from the first five
// layers, because the architecture treated mapping as the only consumer of a
// weight vector.
//
// There are three (architecture doc section 3):
//
//     RELATIONSHIP   point -> weights          the manifold itself
//     INTERPOLATION  weights + values -> value this file
//     MAPPING        weights -> targets        the mapping layer
//
// and a fourth that is deliberately outside the kernel:
//
//     MOTION         weights(t) -> d/dt        a temporal consumer
//
// The property that makes this work is partition of unity, and it is worth
// being precise about what that does and does not guarantee.
//
// Barycentric weights sum to one, so the combination below is AFFINE: the
// result lies in the affine hull of the values, and no arbitrary scale factor
// creeps in. At a vertex the result is exactly that vertex's value; at the
// centroid it is exactly the average.
//
// It does NOT guarantee constant power. Two weights of 0.5 sum to one, but
// their squares sum to 0.5. That is why equal-power conversion lives in the
// curve layer and not here, and why applying a curve BEFORE interpolating is
// almost always wrong -- see the warning on interpolate() below.

#include "ofxManifoldCurves.h"

#include <cstddef>
#include <vector>

namespace ofxManifold {

// Blend values indexed by NodeID.
//
//     result = SUM_i  w_i * values[w_i.id]
//
// T needs `T * float` and `T + T`, and a default constructor that means zero.
// That covers float, glm::vec2/3/4, and most colour and parameter types.
//
// WEIGHTS ARE USED AS GIVEN. There is no internal normalization, and that is a
// decision rather than an oversight:
//
//   * Straight from evaluate(), weights sum to one and the result is a true
//     affine combination. This is the ordinary case.
//
//   * Where a null node holds part of the weight, the sum is BELOW one and the
//     result scales toward the zero value. That is the fade a null node exists
//     to produce, and normalizing here would silently remove it.
//
//   * After a curve, the sum is above one and the result is scaled up. That is
//     almost never wanted: curves produce gains, not blend coefficients. Call
//     normalize() first, or better, do not curve weights you intend to
//     interpolate with.
//
// Nodes whose id falls outside `values` contribute nothing rather than reading
// past the end.
template <typename T>
T interpolate(const WeightVector& w, const std::vector<T>& values) {
    T acc{};
    for (const auto& wn : w) {
        if (wn.id < values.size()) {
            acc = acc + values[wn.id] * wn.weight;
        }
    }
    return acc;
}

// Blend values supplied by a callable, for cases where they are not a dense
// array indexed by NodeID -- a map, a lookup into someone else's structure, or
// a value computed on demand.
//
// `lookup` takes a NodeID and returns T.
template <typename T, typename Lookup>
T interpolateWith(const WeightVector& w, Lookup lookup) {
    T acc{};
    for (const auto& wn : w) {
        acc = acc + lookup(wn.id) * wn.weight;
    }
    return acc;
}

// How much of the weight actually landed on a value.
//
// One minus this is the shortfall: the share held by null nodes, or lost
// outside the hull. A consumer that wants to fade to silence rather than to
// the zero value can use it as a master gain, and a consumer that expects a
// full blend can use it to notice that it is not getting one.
inline float coverage(const WeightVector& w, std::size_t valueCount) {
    float total = 0.0f;
    for (const auto& wn : w) {
        if (wn.id < valueCount) total += wn.weight;
    }
    return total;
}

} // namespace ofxManifold
