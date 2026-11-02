#pragma once

// ofxManifoldEditor — what every view reads.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofxManifoldEditorModel.h"
#include "ofxManifoldEditorSelection.h"

#include "ofMain.h"
#include "ofxManifold.h"

namespace ofxManifold {
namespace editor {

class Text;

// Everything a view reads, gathered so each draw call takes one argument
// rather than a dozen. All of it const: a view cannot change what it draws.
struct Scene {
    const Model*               model      = nullptr;
    const Selection*           selection  = nullptr;
    const ofxManifoldRenderer* renderer   = nullptr;
    const Evaluation*          evaluation = nullptr;
    const Text*                text       = nullptr;
    glm::vec2 listeningPoint{0.5f, 0.5f};
    bool      auditioning = false;
    glm::vec2 mouse{-1.0f, -1.0f};       // for hover
};

// The snapping grid, as it is drawn. A preference of the editing session,
// never written into a map file.
enum class GridKind { Free, Square, Triangular, Polar };

struct GridLook {
    GridKind    kind     = GridKind::Square;
    const Grid* grid     = nullptr;
    float       spacing  = 0.05f;
    float       rotation = 0.0f;          // radians
    int         spokes   = 8;
};

} // namespace editor
} // namespace ofxManifold
