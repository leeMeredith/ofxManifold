#pragma once

// ofxManifold — Evaluator.
//
// One per moving source. Holds the last containing region and offers it to the
// manifold as a hint on the next evaluation.
//
// Why this is a separate object rather than a member of Manifold2D
// (architecture doc 8):
//
// Installations run many concurrent sources through one map -- KA used more
// than thirty maps as purpose-built panners with multiple sources moving at
// once. A hint cached on the manifold would be shared by all of them and would
// thrash, giving worse behaviour than no cache at all.
//
// The hint does two jobs:
//
//   1. Performance. Testing the previous region first is the whole story for
//      realistic node counts: a point that has not left its region costs one
//      containment test instead of a linear scan.
//
//   2. Hysteresis. Regions may overlap by design in an arbitrary node network,
//      so containment is not unique. Testing the previous region first means a
//      source stays in the region it was already in, rather than popping to
//      whichever region happens to come first in insertion order.
//
// The second is not a side effect to be tolerated. It is the behaviour you
// want from a control surface, and it is why the hint belongs to the source
// rather than to the map.

#include "ofxManifold2D.h"

namespace ofxManifold {

class Evaluator {
public:
    // The manifold must outlive the evaluator. It is held by reference and is
    // never mutated here: many evaluators share one const manifold.
    explicit Evaluator(const Manifold2D& m) : manifold_(&m) {}

    Evaluation evaluate(glm::vec2 p) {
        const Evaluation e = manifold_->evaluate(p, lastRegion_);
        // Leaving the hull clears the hint rather than keeping a stale region.
        // Keeping it would make re-entry depend on where the source left,
        // which is a hidden dependency on history no author asked for.
        lastRegion_ = e.inside ? e.regionID : InvalidRegion;
        return e;
    }

    RegionID hint() const { return lastRegion_; }

    // Forget the previous region. Call after editing topology, and any time a
    // source jumps discontinuously and should not inherit its old region.
    void reset() { lastRegion_ = InvalidRegion; }

private:
    const Manifold2D* manifold_;
    RegionID lastRegion_ = InvalidRegion;
};

} // namespace ofxManifold
