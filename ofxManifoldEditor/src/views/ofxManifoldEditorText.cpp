#include "ofxManifoldEditorText.h"

namespace ofxManifold {
namespace editor {

// Loaded at 72 dots per inch, so a size in points is a size in pixels, as
// on the rest of the Mac; openFrameworks' default of 96 made every size a
// third larger than asked. Reloaded only when the size changes. Character
// width is measured over ten characters, since a single one can round.
void Text::fit() {
    if (size_ == loaded_) return;
    loaded_ = size_;
    if (font_.load(OF_TTF_MONO, loaded_, true, true, false, 0.3f, 72)) {
        cw_ = font_.stringWidth("0000000000") / 10.0f;
        lh_ = font_.getLineHeight();
    } else {
        cw_ = 8.0f;
        lh_ = 13.0f;
    }
}

void Text::draw(const std::string& s, float x, float y) const {
    if (font_.isLoaded()) font_.drawString(s, x, y);
    else ofDrawBitmapString(s, x, y);
}

} // namespace editor
} // namespace ofxManifold
