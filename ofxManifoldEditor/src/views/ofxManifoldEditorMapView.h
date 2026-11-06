#pragma once

// ofxManifoldEditor — the map: grid, regions, nodes, links, listening point.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofxManifoldEditorScene.h"

namespace ofxManifold {
namespace editor {

// What is in progress on the map, beyond the model.
struct MapOverlay {
    bool      refused = false;            // the last action was refused
    bool      boxing  = false;            // a box select is being dragged
    glm::vec2 boxFrom{0.0f, 0.0f};
    glm::vec2 boxTo{0.0f, 0.0f};
};

// The whole map, in order: grid, regions, halos, links, nodes, the rings
// marking what feeds the picked outputs, the selection, the drag box, and
// the listening point.
void drawMap(const Scene& s, const GridLook& look, const MapOverlay& o);

// The pieces, for an app that draws a map its own way.
void drawGrid(const Scene& s, const GridLook& look);
void drawLinks(const Scene& s);

// Round 3A-6. Each bent node's offset from its level's start, beside it, on
// the current level. And the column of dots just right of the map, one per
// level, the current one filled with its starting height beside it, and the
// listening mark: where the listening point is among the levels, greyed when
// it is outside them. Both draw nothing for a map with one flat level, so a
// 2D map looks exactly as it did. drawMap() draws both.
void drawOffsets(const Scene& s);
void drawLevelColumn(const Scene& s);

} // namespace editor
} // namespace ofxManifold
