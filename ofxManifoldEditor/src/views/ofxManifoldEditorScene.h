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
    // Its height (round 3A-6): on the current level unless audition has
    // moved it up or down.
    float     listeningZ = 0.0f;
    bool      auditioning = false;
    // Where the key panel starts; negative: 30 to the right of the map, as
    // it always was. The column of dots and the 3D view push it right.
    float     panelLeft = -1.0f;
    // With the 3D view open the panel sits BENEATH the views instead, from
    // panelTop to panelBottom, its keys spread across in columns; negative:
    // beside the map.
    float     panelTop    = -1.0f;
    float     panelBottom = -1.0f;
    // Beneath: is the status block beside the views (at panelLeft, as
    // always) with only the keys beneath, or beneath with them?
    bool      statusBeside = false;
    // A level's dot being dragged (round 3A-6b): which level, and where its
    // start would land if let go now. -1: none.
    int       levelDrag     = -1;
    float     levelDragBase = 0.0f;
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
