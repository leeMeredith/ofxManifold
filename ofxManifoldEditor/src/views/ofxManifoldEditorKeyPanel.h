#pragma once

// ofxManifoldEditor — the side panel: status, the output being acted on, the keys.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofxManifoldEditorKeys.h"
#include "ofxManifoldEditorScene.h"

#include <string>
#include <vector>

namespace ofxManifold {
namespace editor {

// What the panel shows beyond the model: what the app knows.
struct PanelStatus {
    bool        auditionLocked = false;
    std::string name = "untitled";        // the open pair
    bool        unsaved = false;
    std::string message;
    bool        messageShown = false;
    bool        warning = false;           // the message is a refusal
};

// In fixed zones, so nothing pushes anything else: status, the message, the
// output being acted on, then the keys -- grouped, lit or dim from `keys`,
// the same KeyContext the app asks before acting.
void drawPanel(const Scene& s, const GridLook& look, const PanelStatus& st,
               const KeyContext& keys);

// How tall a group of keys is drawn, title and rows: for an app placing one.
float keyGroupHeight(const KeyGroup& g, float lh);

// The key panel's width, at most, when it may use two columns.
constexpr float kPanelChars = 66.0f;

} // namespace editor
} // namespace ofxManifold
