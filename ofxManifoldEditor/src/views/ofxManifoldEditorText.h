#pragma once

// ofxManifoldEditor — the text the views are drawn in.
//
// A VIEW: it draws, and only reads. Needs openFrameworks, so it is kept out of
// the logic headers (ofxManifoldEditor.h) and reached through
// ofxManifoldEditorViews.h. Lifted from example-editor's ofApp unchanged, and
// checked unchanged: the recorded drawing calls of a scripted session match
// before and after the move (tests/drawlog).

#include "ofMain.h"

#include <string>

namespace ofxManifold {
namespace editor {

// The system's monospaced font at a size you choose -- a fixed reading size,
// NOT one following the window: a bigger window is for more room, not bigger
// letters. If the font cannot be loaded, openFrameworks' bitmap font is used,
// and charWidth() and lineHeight() say so.
class Text {
public:
    void  setSize(int points) { size_ = points; }
    int   size() const { return size_; }

    // Load the font if the size has changed since it was last loaded.
    void  fit();

    float charWidth() const { return cw_; }
    float lineHeight() const { return lh_; }
    void  draw(const std::string& s, float x, float y) const;

private:
    ofTrueTypeFont font_;
    int   size_   = 12;      // what was chosen
    int   loaded_ = 0;       // what is loaded
    float cw_ = 8.0f;
    float lh_ = 13.0f;
};

} // namespace editor
} // namespace ofxManifold
