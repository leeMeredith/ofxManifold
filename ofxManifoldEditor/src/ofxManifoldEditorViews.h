#pragma once

// ofxManifoldEditor — the views: drawing, which needs openFrameworks.
//
// Kept apart from ofxManifoldEditor.h, the logic, so that anything wanting
// only the logic -- the tests, a headless tool -- never pulls in
// openFrameworks. An app that draws includes this; it includes the logic too.

#include "ofxManifoldEditor.h"

#include "views/ofxManifoldEditorScene.h"
#include "views/ofxManifoldEditorText.h"
#include "views/ofxManifoldEditorStyle.h"
#include "views/ofxManifoldEditorNodeGlyph.h"
#include "views/ofxManifoldEditorMapView.h"
#include "views/ofxManifoldEditorOutputChart.h"
#include "views/ofxManifoldEditorKeyPanel.h"
