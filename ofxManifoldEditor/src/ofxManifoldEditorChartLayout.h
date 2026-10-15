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
    float       w       = 0.0f;              // width: a sliver when Unused
    TargetID    output  = InvalidTarget;     // when kind == Output
    std::string name;                        // empty when Unused

    // What is written under the bar. Decided for the WHOLE ROW at once, so
    // labels never appear on some bars and not others at random:
    //
    //   every name fits             names, each with its channel beneath
    //                               (if every channel number fits too)
    //   every channel number fits   channel numbers on every bar
    //   otherwise                   every 2nd, 5th, 10th... channel, the
    //                               smallest step whose numbers fit, like
    //                               the ticks on a ruler -- unused channels
    //                               included, so the count stays even
    //
    // Hover gives the full name of any bar.
    std::string label;
    std::string sub;
};

// The size of the text the chart is drawn with, so labels can be fitted. The
// defaults are openFrameworks' bitmap font.
struct ChartText {
    float charWidth  = 8.0f;
    float lineHeight = 13.0f;
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
    float barWidth = 0.0f;    // a full bar; unused channels are slivers
    int   labelStep = 0;      // ruler step when labels are thinned; 0 if not
    float silenceX = 0.0f;
    std::string silenceLabel;
    // A column at the right for the running total, reserved before the bars
    // are fitted, so however many outputs there are, the total stays on
    // screen. (With thirty outputs it used to be drawn past the window edge.)
    float totalX   = 0.0f;
    std::vector<ChartBar> bars;

    // The output whose bar is under (x, y), or InvalidTarget. Only an output
    // can be picked -- not a derived output, not an unused channel.
    TargetID outputAt(float x, float y) const;

    // What is under (x, y), for the hover label: a bar's index, kSilence, or
    // kNothing. A bar's area takes in half the gap either side, so a narrow
    // bar or a sliver is still easy to point at; clicking uses the same
    // area, so what the hover names is what a click picks.
    static constexpr int kNothing = -1;
    static constexpr int kSilence = -2;
    int hoverAt(float x, float y) const;
};

ChartLayout layoutChart(const Mapping& mapping, float x0, float top,
                        float width, float height,
                        ChartText text = ChartText{});

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

// How much of each output's level comes from the selected nodes, at the
// listening point described by `weights`, indexed by channel like
// Mapping::toChannels. Drawn as a segment inside each fader: "this much of
// front's bar is this node".
std::vector<float> contributions(const Model& model, const Selection& sel,
                                 const WeightVector& weights);

// The derived outputs a selected node is a source of -- a tap, a copy the
// node loses nothing to -- by aggregator index.
std::vector<std::size_t> tapsOf(const Model& model, const Selection& sel);

} // namespace editor
} // namespace ofxManifold
