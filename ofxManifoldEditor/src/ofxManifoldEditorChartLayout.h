#pragma once

// ofxManifoldEditor — where the faders go.
//
// The output chart's GEOMETRY, as data: one bar per channel in channel order,
// which output or derived output owns it, and where the silence bar sits.
// Drawing it is a view; laying it out is arithmetic, so it lives here, without
// openFrameworks, and is tested with vectors.
//
// Round 1 moves the layout as it stands. Round 2 changes it on purpose --
// narrower faders as outputs are added, labels that fit, slivers for unused
// channels -- and its vectors change with it.

#include "mapping/ofxManifoldMapping.h"
#include "ofxManifoldEditorModel.h"
#include "ofxManifoldEditorSelection.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ofxManifold {
namespace editor {

struct ChartBar {
    enum class Kind { Unused, Output, Derived };

    Kind        kind    = Kind::Unused;
    int         channel = 0;
    float       x       = 0.0f;              // left edge
    TargetID    output  = InvalidTarget;     // when kind == Output
    std::string name;                        // empty when Unused
};

struct ChartLayout {
    bool  empty    = true;    // no outputs and no derived outputs at all
    float x0       = 0.0f;
    float top      = 0.0f;
    float width    = 0.0f;
    float height   = 0.0f;
    float barArea  = 0.0f;    // the height bars rise within
    float base     = 0.0f;    // the y bars rise from
    float gap      = 0.0f;
    float barWidth = 0.0f;
    float silenceX = 0.0f;
    float totalX   = 0.0f;    // where the running total is written
    std::vector<ChartBar> bars;

    // The output whose bar contains (x, y), or InvalidTarget. Unused channels
    // and derived outputs are not clickable -- only an output can be current.
    TargetID outputAt(float x, float y) const;
};

ChartLayout layoutChart(const Mapping& mapping, float x0, float top,
                        float width, float height);

// The fader line: for each output a selected node feeds, that node's fill --
// the same percentage its shape shows, so the line on the fader and the fill
// in the node move together when q or w is pressed. One mark per node per
// output, in selection order then binding order.
struct FillMark {
    TargetID output = InvalidTarget;
    NodeID   node   = InvalidNode;
    float    fill   = 0.0f;
};

std::vector<FillMark> fillMarks(const Model& model, const Selection& sel);

} // namespace editor
} // namespace ofxManifold
