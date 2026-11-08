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
    // With the 3D view open (3A-6c) the levels keys sit beneath it, from
    // here, this wide; the rest of the panel stays beside, as always. x < 0:
    // the levels keys are in the panel with the rest.
    glm::vec2 levelsKeysAt{-1.0f, -1.0f};
    float     levelsKeysWidth = 0.0f;
    // A level's dot being dragged (round 3A-6b): which level, and where its
    // start would land if let go now. -1: none.
    // The typing line (round 3A-7): open while a value is named; what it is
    // for ("new value", "value") and what has been typed.
    bool        typing = false;
    std::string typingPrompt;
    std::string typingLine;
    int       levelDrag     = -1;
    float     levelDragBase = 0.0f;
    glm::vec2 mouse{-1.0f, -1.0f};       // for hover
    // What l would lay out at the mouse (round 3A-9), shown faintly after
    // [ or ] until l or another key. nullptr: nothing shown.
    const LayoutPlan* layoutPreview = nullptr;
    // The panel's bottom: no line of it below this (3A-10). < 0: the
    // window's bottom.
    float panelBottom = -1.0f;
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
    // Round 3A-11: the grid is drawn as its whole cells only, so no cut-off
    // tile shows at the map's edge -- gridCells(), kept by the app as the
    // grid changes; and on the polar grid its whole rings.
    const std::vector<std::vector<GridAddress>>* cells = nullptr;
    int         rings    = 0;
    // The grid lines through this point, brighter, and the nodes on them
    // ringed: where a node being placed or moved lines up (3A-11).
    bool                lineOn = false;
    GridAddress         lineAt;
    std::vector<NodeID> lined;
};

} // namespace editor
} // namespace ofxManifold
