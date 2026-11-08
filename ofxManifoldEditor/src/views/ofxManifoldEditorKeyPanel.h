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

// ? (3A-10): every key over the whole window, in columns, at the same text
// size -- for when the panel has had to leave some out. Any key or click
// closes it.
void drawKeySheet(const Scene& s, const KeyContext& keys);

// keyGroupHeight, panelTop, panelKeysTop and the window's layout are in
// ofxManifoldEditorKeys.h: arithmetic, checked without openFrameworks.

} // namespace editor
} // namespace ofxManifold
