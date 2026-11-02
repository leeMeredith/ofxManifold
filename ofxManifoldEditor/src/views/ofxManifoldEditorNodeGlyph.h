#pragma once

// ofxManifoldEditor — one node: its shape, fill and halo.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofxManifoldEditorScene.h"

namespace ofxManifold {
namespace editor {

// Drawn radius of a node: its bias weight, as the renderer shows it.
float nodeRadius(const Manifold2D& manifold, NodeID id);

// Shape (what the node does with its share), fill (how much reaches an
// output) and name.
void drawNode(const Scene& s, NodeID id);

// Its live share at the listening point, behind it.
void drawHalo(const Scene& s, NodeID id, float weight);

} // namespace editor
} // namespace ofxManifold
