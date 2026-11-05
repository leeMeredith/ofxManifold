#pragma once

// ofxManifoldEditor — the faders.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofxManifoldEditorChartLayout.h"
#include "ofxManifoldEditorScene.h"

namespace ofxManifold {
namespace editor {

// The faders, drawn from the layout editor::layoutChart computed and the
// levels it is HANDED (round 3A-5): it computes no level of its own, so a
// player can hand it a blend of several maps. The editor hands it
// chartLevels(). Trims, fill lines and the hover label as before.
void drawChart(const Scene& s, const ChartLayout& chart,
               const ChartLevels& lv);

} // namespace editor
} // namespace ofxManifold
