#pragma once

// ofxManifoldEditor — the 3D view (PLAN-round3.md, round 3A-6).
//
// For VIEWING only: editing stays in the plan view, with heights set by keys,
// so there is no picking in 3D. Each level is drawn as its regions' outlines
// at its nodes' heights, the current level brighter; each node has a line up
// to itself on the level above; selected nodes are highlighted; the listening
// point is a point with a line dropped to the level below it -- the way the
// player will show a source.
//
// A VIEW: it draws, and only reads. The camera is the caller's, since a
// camera is dragged and so has state of its own; the view only begins and
// ends it in the area given.

#include "ofxManifoldEditorScene.h"

namespace ofxManifold {
namespace editor {

// Point the camera at the map from above and to the front, at a distance
// that fits `area`. Once, when the view is first shown; after that the
// camera is wherever it was dragged.
void aimLevelView(ofEasyCam& cam, const ofRectangle& area);

void drawLevelView(const Scene& s, ofEasyCam& cam, const ofRectangle& area);

} // namespace editor
} // namespace ofxManifold
