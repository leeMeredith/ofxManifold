#pragma once

// ofxManifold — point sources (architecture doc section 11).
//
// Everything that produces a point for a manifold to evaluate.
//
// The general form is deliberately NOT time. In SpaceMap's KA production a
// bubble effect's position was bound to a console fader so the operator could
// track a performer swimming onstage; in Soldaat van Oranje, rotational
// coordinates came from the audience's rotating platform. Time is one
// parameterization among several.
//
//     glm::vec2 pointAt(float t)
//
// where t is time, a fader, a normalized phase, a platform angle, or anything
// else a consumer wants to drive with. The source does not know which.
//
// This is a SIBLING MODULE, not kernel. The only thing it requires of the
// kernel is that evaluation be cheap and stateless enough to drive from a
// playback engine, and that a point be pure data. Both were true already.

#include "../core/ofxManifoldTypes.h"

namespace ofxManifold {

class PointSource {
public:
    virtual ~PointSource() = default;

    // t is normalized to [0, 1] across the source's own extent.
    //
    // Normalized rather than seconds because a source outlives the tempo it
    // was recorded at: the same path can be replayed in half the time, driven
    // from a fader, or scrubbed backwards, and none of those should require
    // rewriting it. A consumer that wants seconds divides by its own duration.
    virtual glm::vec2 pointAt(float t) const = 0;
};

} // namespace ofxManifold
