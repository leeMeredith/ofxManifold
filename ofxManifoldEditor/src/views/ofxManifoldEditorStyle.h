#pragma once

// ofxManifoldEditor — the colours the views share.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofMain.h"
#include "ofxManifold.h"

namespace ofxManifold {
namespace editor {

// The colour of an output: its fader, its swatch, the lines into it, and the
// name of the node it belongs to.
ofColor outputColour(const Mapping& mapping, TargetID t);

} // namespace editor
} // namespace ofxManifold
