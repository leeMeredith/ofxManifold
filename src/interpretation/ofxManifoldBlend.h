#pragma once

// ofxManifold — blending weight vectors.
//
// MIAP objects hold two maps and crossfade between them. This is why the
// interpretation layer is closed over weight vectors: the natural unit of
// composition is the weight vector, not the manifold (architecture doc 9.3).
//
// Two manifolds evaluated concurrently and blended downstream is the design,
// and it replaces the v1 proposal to reach three dimensions with tetrahedra.
//
// The curve here shapes the CROSSFADE, not the weights. That distinction
// matters: curve::apply reshapes each weight independently, while the gains
// below are computed from t once and applied to whole vectors. Passing
// equalPower gives a constant-power crossfade -- the two gains' squares sum to
// one -- which is what an audio consumer wants when moving between two maps.
// Passing linear gives a plain lerp, which is what a parameter consumer wants.

#include "ofxManifoldCurves.h"

#include <algorithm>

namespace ofxManifold {

// Crossfade a into b by t, with the crossfade shaped by `fn`.
//
// t == 0 yields a, t == 1 yields b, for every curve, because every curve here
// satisfies fn(0) == 0 and fn(1) == 1. That endpoint property is asserted by
// vector rather than assumed.
//
// Nodes present in only one input keep their contribution scaled by that
// input's gain alone. Merging is by NodeID, so the two vectors need not cover
// the same nodes -- which they will not, when they come from two different
// manifolds.
inline WeightVector blend(const WeightVector& a, const WeightVector& b,
                          float t, curve::Fn fn = curve::linear) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const float ga = fn(1.0f - t);
    const float gb = fn(t);

    WeightVector out;
    out.reserve(a.size() + b.size());

    for (const auto& wn : a) {
        out.push_back(WeightedNode{wn.id, wn.weight * ga});
    }
    for (const auto& wn : b) {
        auto it = std::find_if(out.begin(), out.end(),
                               [&](const WeightedNode& o) {
                                   return o.id == wn.id;
                               });
        if (it != out.end()) {
            it->weight += wn.weight * gb;
        } else {
            out.push_back(WeightedNode{wn.id, wn.weight * gb});
        }
    }

    // Emitting zero-weight entries would grow the vector every blend and hand
    // the mapping layer work that means nothing. A node with no share is
    // absent, which is the same convention evaluate() uses outside the hull.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const WeightedNode& o) {
                                 return std::fabs(o.weight) < 1e-9f;
                             }),
              out.end());
    return out;
}

} // namespace ofxManifold
