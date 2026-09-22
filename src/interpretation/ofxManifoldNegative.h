#pragma once

// ofxManifold — negative weights.
//
// Mean-value coordinates over a non-convex region produce negative weights at
// some interior points. That is the algorithm working, not a fault
// (DECISIONS.md D-016b (D-C)): Floater and Hormann's paper is titled "Mean value
// coordinates for arbitrary planar polygons", and arbitrary is the point.
//
// Measured over full interior sweeps: a moderate star never goes negative at
// all; a deep star reaches -0.0004; an L-shape -0.013. Small, and a backward
// pull from a vertex that is out of sight from where the point stands.
//
// Whether that is acceptable is INTERPRETATION, which is why this lives here
// and not in the evaluator. For parameter interpolation a negative weight is
// mild extrapolation beyond a node's value, often harmless. For amplitude
// panning it is a phase inversion, usually wrong. The manifold reports the
// relationship; the consumer decides.

#include "ofxManifoldCurves.h"

#include <cmath>

namespace ofxManifold {

// Does any entry fall below -eps?
//
// The tolerance matters. MVC's arithmetic leaves last-digit noise on weights
// that are mathematically zero -- a point on an edge gives the two far
// vertices something like -1e-8, not 0 -- and a diagnostic that fired on that
// would fire on every edge and be ignored within a day.
inline bool anyNegative(const WeightVector& w, float eps = 1e-6f) {
    for (const auto& wn : w) {
        if (wn.weight < -eps) return true;
    }
    return false;
}

// The most negative entry, or 0 if none is negative. How much a clamp would
// have to redistribute, which is the number a consumer deciding whether to
// care actually wants.
inline float mostNegative(const WeightVector& w) {
    float m = 0.0f;
    for (const auto& wn : w) {
        if (wn.weight < m) m = wn.weight;
    }
    return m;
}

// Zero every negative weight, then renormalize the rest to sum to one.
//
// THIS BREAKS AFFINITY, and that has to be said plainly. After clamping the
// result is no longer a true affine combination of the node values, so a blend
// of two parameter values will not land where the geometry says it should.
//
//     right for GAINS      a speaker should not receive a phase-inverted
//                          share, and redistributing ~1% of the vector to the
//                          nodes that are genuinely pulling is inaudible
//
//     wrong for POSITIONS  interpolating a filter cutoff through clamped
//                          weights gives a value the map did not describe
//
// Same distinction as curve::equalPower (DECISIONS.md D-003), and for the same
// reason it is never applied automatically. A consumer that wants it calls it.
//
// Nodes that clamp to zero are REMOVED rather than kept at zero, matching the
// convention everywhere else: a node with no share is absent.
//
// If every weight is negative or zero the input is returned unchanged rather
// than dividing by zero. That cannot happen for a vector that sums to one, but
// a caller may pass anything.
inline WeightVector clampNegative(const WeightVector& w) {
    float kept = 0.0f;
    for (const auto& wn : w) {
        if (wn.weight > 0.0f) kept += wn.weight;
    }
    if (kept <= 1e-12f) return w;

    WeightVector out;
    out.reserve(w.size());
    const float inv = 1.0f / kept;
    for (const auto& wn : w) {
        if (wn.weight > 0.0f) {
            out.push_back(WeightedNode{wn.id, wn.weight * inv});
        }
    }
    return out;
}

} // namespace ofxManifold
