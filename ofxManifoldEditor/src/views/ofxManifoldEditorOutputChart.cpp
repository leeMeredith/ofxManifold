#include "ofxManifoldEditorOutputChart.h"

#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

namespace ofxManifold {
namespace editor {

// One bar per OUTPUT, not per node, in channel order -- outputs are what is
// heard. Derived outputs in their own colour. A silence bar at the end, so
// outputs plus silence always total exactly 1. Trim as a tick: the bar is the
// routed share, the tick is the level that actually leaves.
void drawChart(const Scene& s, const ChartLayout& chart) {
    const Model&      model     = *s.model;
    const Selection&  selection = *s.selection;
    const Manifold2D& manifold  = model.manifold();
    const Mapping&    mapping   = model.mapping();
    const Evaluation& evaluation = *s.evaluation;
    const ofxManifoldRenderer* renderer = s.renderer;
    const float cw = s.text->charWidth(), lh = s.text->lineHeight();
    auto text = [&](const std::string& t, float x, float y) {
        s.text->draw(t, x, y);
    };
    (void)model; (void)selection; (void)manifold; (void)mapping;
    (void)evaluation; (void)renderer; (void)cw; (void)lh; (void)text;

    ofPushStyle();
    ofSetColor(40, 43, 52);
    ofFill();
    ofDrawRectangle(chart.x0, chart.top, chart.width, chart.height);

    if (chart.empty) {
        ofSetColor(140, 146, 160);
        text("no outputs yet -- press o to make one",
             chart.x0 + 12.0f, chart.top + lh + 8.0f);
        ofPopStyle();
        return;
    }

    const WeightVector none;
    const WeightVector& wv = evaluation.inside ? evaluation.weights : none;
    const std::vector<float> routed = mapping.toChannels(wv);
    const std::vector<float> levels = mapping.toChannelLevels(wv);
    const float silence = evaluation.inside ? mapping.silenceShare(wv) : 0.0f;
    // How much of each output's level comes from the selected nodes.
    const std::vector<float> fromSel = selection.empty()
        ? std::vector<float>() : contributions(model, selection, wv);
    const std::vector<std::size_t> taps = tapsOf(model, selection);
    const float rise = chart.barArea - 6.0f;
    const float row1 = chart.base + lh + 1.0f;      // label rows under bars
    const float row2 = row1 + lh;

    float outputsTotal = 0.0f;
    for (const ChartBar& b : chart.bars) {
        const std::size_t ch = std::size_t(b.channel);
        const float bw = b.w;
        if (b.kind == ChartBar::Kind::Unused) {
            // A sliver: a channel nothing is on. Hover names it; a ruler mark
            // may still number it, so the count along the row stays even.
            ofSetColor(52, 56, 66);
            ofDrawRectangle(b.x, chart.top + 6.0f, bw, rise);
            ofSetColor(140, 146, 160);
            if (!b.label.empty()) text(b.label, b.x, row1);
            continue;
        }
        ofSetColor(58, 62, 74);
        ofDrawRectangle(b.x, chart.top + 6.0f, bw, rise);
        const bool derived = (b.kind == ChartBar::Kind::Derived);
        if (!derived) outputsTotal += routed[ch];

        const float v = ofClamp(routed[ch], 0.0f, 1.0f);
        ofSetColor(derived ? ofColor(180, 140, 230) : outputColour(mapping, b.output));
        ofDrawRectangle(b.x, chart.base - v * rise, bw, v * rise);
        // The level after trim, as a tick across the bar.
        const float lv = ofClamp(levels[ch], 0.0f, 1.0f);
        ofSetColor(255, 255, 255, 200);
        const float ty = chart.base - lv * rise;
        ofDrawLine(b.x - 2.0f, ty, b.x + bw + 2.0f, ty);

        // A dot above each output a selected node feeds -- the other half of
        // seeing a binding: which faders this selection reaches.
        if (!derived) {
            bool fed = false;
            for (NodeID id : selection.nodes()) {
                if (model.feeds(id, b.output)) { fed = true; break; }
            }
            if (fed) {
                ofSetColor(255, 214, 90);
                ofDrawCircle(b.x + bw * 0.5f, chart.top + 1.0f, 3.0f);
            }
        }

        // The selected nodes' part of this output, as a brighter segment
        // from the bottom of the bar: "this much of it is them".
        if (!derived && ch < fromSel.size() && fromSel[ch] > 0.0f) {
            const float c = ofClamp(fromSel[ch], 0.0f, 1.0f);
            const float inset = std::min(2.0f, bw * 0.2f);
            ofSetColor(200, 245, 220);
            ofDrawRectangle(b.x + inset, chart.base - c * rise,
                            bw - 2.0f * inset, c * rise);
        }

        // A derived output that reads a selected node: a purple dot -- a
        // tap, a copy the node loses nothing to.
        if (derived) {
            for (std::size_t a : taps) {
                if (mapping.aggregators()[a].name == b.name) {
                    ofSetColor(180, 140, 230);
                    ofDrawCircle(b.x + bw * 0.5f, chart.top + 1.0f, 3.0f);
                }
            }
        }

        bool picked = false;
        for (TargetID t : model.selectedOutputs()) {
            if (!derived && b.output == t) picked = true;
        }
        if (picked) {
            ofNoFill();
            ofSetColor(255, 214, 90);
            ofSetLineWidth(b.output == model.currentOutput() ? 2.0f : 1.0f);
            ofDrawRectangle(b.x - 2.0f, chart.top + 4.0f, bw + 4.0f,
                            chart.barArea - 2.0f);
            ofFill();
            ofSetLineWidth(1.0f);
        }
        // Labels as the layout fitted them: the name over its channel, or
        // the channel alone, or nothing when even that will not fit.
        ofSetColor(derived ? ofColor(200, 205, 215) : outputColour(mapping, b.output));
        if (!b.label.empty()) text(b.label, b.x, row1);
        ofSetColor(140, 146, 160);
        if (!b.sub.empty()) text(b.sub, b.x, row2);
    }

    // The fader line: each selected node's fill, on each output it feeds --
    // the same percentage its shape shows, so pressing q or w moves the line
    // and the shape together. Yellow, as the selection is; the white tick is
    // the level after trim, a different thing.
    ofSetLineWidth(2.0f);
    ofSetColor(255, 214, 90);
    for (const FillMark& m : fillMarks(model, selection)) {
        for (const ChartBar& b : chart.bars) {
            if (b.kind != ChartBar::Kind::Output || b.output != m.output) continue;
            const float y = chart.base - ofClamp(m.fill, 0.0f, 1.0f) * rise;
            ofDrawLine(b.x, y, b.x + b.w, y);
        }
    }
    ofSetLineWidth(1.0f);

    // Silence, so the whole share is accounted for.
    ofSetColor(58, 62, 74);
    ofDrawRectangle(chart.silenceX, chart.top + 6.0f, chart.barWidth, rise);
    const float sv = ofClamp(silence, 0.0f, 1.0f);
    ofSetColor(120, 126, 140);
    ofDrawRectangle(chart.silenceX, chart.base - sv * rise, chart.barWidth,
                    sv * rise);
    ofSetColor(200, 205, 215);
    if (!chart.silenceLabel.empty()) text(chart.silenceLabel, chart.silenceX, row1);

    // The total, in the column kept for it, one figure a line, so it never
    // runs past the window however many outputs there are.
    ofSetColor(235, 238, 245);
    const float tx = chart.totalX;
    if (evaluation.inside) {
        text("outputs " + ofToString(outputsTotal, 3), tx, chart.top + lh + 6.0f);
        text("silence " + ofToString(silence, 3), tx, chart.top + 2.0f * lh + 6.0f);
        text("total   " + ofToString(outputsTotal + silence, 3), tx,
             chart.top + 3.0f * lh + 6.0f);
    } else {
        text("listening point", tx, chart.top + lh + 6.0f);
        text("outside the map", tx, chart.top + 2.0f * lh + 6.0f);
    }

    // Hover: the full name and level of whatever bar is under the mouse, so
    // nothing becomes unreadable when the bars get narrow.
    const int h = chart.hoverAt(s.mouse.x, s.mouse.y);
    if (h != ChartLayout::kNothing) {
        std::vector<std::string> lines;
        float x;
        if (h == ChartLayout::kSilence) {
            x = chart.silenceX;
            lines = {"silent", "share " + ofToString(silence, 3)};
        } else {
            const ChartBar& b = chart.bars[std::size_t(h)];
            const std::size_t ch = std::size_t(b.channel);
            x = b.x;
            if (b.kind == ChartBar::Kind::Unused) {
                lines = {"channel " + ofToString(ch), "unused"};
            } else if (b.kind == ChartBar::Kind::Derived) {
                lines = {b.name + "  ch " + ofToString(ch) + "  derived",
                         "level " + ofToString(levels[ch], 3)};
            } else {
                const float tr = mapping.targetTrim(b.output);
                lines = {b.name + "  ch " + ofToString(ch),
                         "share " + ofToString(routed[ch], 3) + "  level "
                         + ofToString(levels[ch], 3) + "  trim "
                         + (tr > 0.0f ? ofToString(20.0f * std::log10(tr), 1)
                                        + " dB"
                                      : std::string("off"))};
            }
        }
        std::size_t longest = 0;
        for (const std::string& l : lines) longest = std::max(longest, l.size());
        const float bwid = float(longest) * cw + 12.0f;
        const float bh = float(lines.size()) * lh + 8.0f;
        // Above the chart, kept inside the window.
        const float bx = std::min(x, float(ofGetWidth()) - bwid - 8.0f);
        const float by = chart.top - bh - 4.0f;
        ofSetColor(24, 26, 32, 235);
        ofDrawRectangle(bx, by, bwid, bh);
        ofNoFill();
        ofSetColor(140, 146, 160);
        ofDrawRectangle(bx, by, bwid, bh);
        ofFill();
        ofSetColor(235, 238, 245);
        for (std::size_t k = 0; k < lines.size(); ++k) {
            text(lines[k], bx + 6.0f, by + 4.0f + float(k + 1) * lh - 3.0f);
        }
    }
    ofPopStyle();
}

} // namespace editor
} // namespace ofxManifold
