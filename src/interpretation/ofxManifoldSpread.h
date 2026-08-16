#pragma once

// ofxManifold — spread.
//
// SpaceMap calls this divergence: power distributed to all nodes in a map by a
// controllable amount, rather than only to the region containing the point.
// Generalized here as an interpolation between the evaluated weight vector and
// a uniform vector over every node.
//
//     spread 0   the point is localized, weights unchanged
//     spread 1   the point is everywhere, 1/N at every node
//
// This is the mechanism by which several concurrent 2D maps produce
// three-dimensional behaviour -- the historical answer to 3D, in place of
// tetrahedra (architecture doc 9.3). It is also a useful morphing control in
// its own right: a single scalar that takes a control surface from pinpoint to
// wash.
//
// The interpolation is LINEAR in weight space, not power-preserving. That is a
// deliberate layer split: linear interpolation between two vectors that each
// sum to one produces a vector that sums to one, so spread composes with
// anything. Power is the curve layer's business, and applying a curve after
// spread gives the power-preserving version. Doing it here would mean a caller
// who wants linear behaviour has no way back.

#include "ofxManifoldCurves.h"

namespace ofxManifold {

// Spread `in` toward uniform coverage of `totalNodes` nodes.
//
// Assumes node IDs are contiguous from 0, which is what Manifold2D::addNode
// produces. The function takes a count rather than a manifold so this layer
// stays free of geometry: interpretation operates on weight vectors and knows
// nothing about where nodes are.
//
// The result is dense whenever amount > 0 -- every node acquires a share, which
// is the point. At amount == 0 the input is returned untouched, including its
// sparsity, so the common case costs nothing.
inline WeightVector spread(const WeightVector& in, float amount,
                           std::size_t totalNodes) {
    if (amount <= 0.0f || totalNodes == 0) return in;
    if (amount > 1.0f) amount = 1.0f;

    const float uniform = 1.0f / static_cast<float>(totalNodes);

    // Start from the uniform floor, then add the localized part on top.
    WeightVector out;
    out.reserve(totalNodes);
    for (std::size_t i = 0; i < totalNodes; ++i) {
        out.push_back(WeightedNode{static_cast<NodeID>(i),
                                   amount * uniform});
    }
    for (const auto& wn : in) {
        if (wn.id < totalNodes) {
            out[wn.id].weight += (1.0f - amount) * wn.weight;
        }
    }
    return out;
}

} // namespace ofxManifold
