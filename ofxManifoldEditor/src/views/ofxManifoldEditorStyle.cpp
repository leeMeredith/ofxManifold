#include "ofxManifoldEditorStyle.h"

namespace ofxManifold {
namespace editor {

// One colour per output, by CHANNEL rather than by position in the list, so
// an output keeps its colour when another is removed and the ids shift.
// Drawn from Okabe and Ito's colour-blind-safe set, lifted for a dark
// background, leaving out the yellow already meaning "selected" and the
// purple already meaning "derived". Colour is an added cue: the lines, the
// plugs and sockets, and the labels carry the meaning without it. More than
// six outputs and colours repeat; names still tell them apart.
ofColor outputColour(const Mapping& mapping, TargetID t) {
    static const ofColor palette[] = {
        ofColor(86, 180, 233),     // sky blue
        ofColor(230, 159, 0),      // orange
        ofColor(40, 190, 140),     // bluish green
        ofColor(235, 110, 70),     // vermilion
        ofColor(110, 150, 235),    // blue
        ofColor(225, 135, 180),    // pink
    };
    if (t == InvalidTarget || t >= mapping.targetCount()) {
        return ofColor(150, 156, 172);
    }
    const int ch = std::max(0, mapping.targetChannel(t));
    return palette[ch % 6];
}

} // namespace editor
} // namespace ofxManifold
