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

// Beneath the views (the 3D view open): the status on the left, then the key
// groups flowing down and across in columns, each group whole, a new column
// when the next would pass `height`. Where each group goes, from (x0, y0);
// and how wide the columns come to. The app asks the width to choose a
// height that fits the window; the panel draws from the same positions.
std::vector<glm::vec2> flowKeyGroups(const std::vector<KeyGroup>& groups,
                                     float cw, float lh, float x0, float y0,
                                     float height);
float keyColumnsWidth(const std::vector<KeyGroup>& groups, float cw, float lh,
                      float height);

// The height the panel needs beneath the views to fit within `width`. With
// `withStatus`, the status block is beneath too, on the left: at least its
// height, and the keys take what width is left. Without, the status sits
// beside the views, in the space they leave, and the keys have the width.
float panelBelowHeight(const std::vector<KeyGroup>& groups, float cw, float lh,
                       float width, bool withStatus);

// Where the status block ends and the key columns begin, beneath the views.
constexpr float kStatusChars = 42.0f;

} // namespace editor
} // namespace ofxManifold
