#include "ofxManifoldEditorKeyPanel.h"

#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

namespace ofxManifold {
namespace editor {

float keyGroupHeight(const KeyGroup& g, float lh) {
    return (1.0f + float(g.entries.size())) * (lh + 1.0f) + lh * 0.5f;
}

// ---------------------------------------------------------------------------
// the panel
// ---------------------------------------------------------------------------
// The panel in FIXED zones, so nothing pushes anything else: status lines
// that are always in the same places, one line for the message, a few lines
// for the output you are acting on, then the keys. The full list of outputs
// used to sit in the middle and grow with every output, pushing the keys off
// the bottom of the window; the faders already show every output, and hover
// gives any one's details.
void drawPanel(const Scene& s, const GridLook& look, const PanelStatus& st,
               const KeyContext& keys) {
    const GridKind kind = look.kind;
    const float spacing = look.spacing;
    const float rotation = look.rotation;
    const int spokes = look.spokes;
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

    // Beside the map, or -- with the 3D view open -- beneath the views, the
    // status in a block on the left and the keys across in columns.
    const float x = s.panelLeft >= 0.0f ? s.panelLeft
                                        : renderer->viewport().getRight() + 30.0f;
    const float room = float(ofGetWidth()) - x - 16.0f;
    const std::size_t width = std::size_t(std::max(12.0f, room / cw));
    // Any line longer than the panel is shortened rather than run off it.
    auto fit = [width](std::string s) {
        if (s.size() > width) s = s.substr(0, width - 3) + "...";
        return s;
    };
    const float step = lh + 2.0f;
    float y = 24.0f + lh;
    static const char* names[] = {"free", "square", "triangular", "polar"};

    // ---- status: seven lines, each always in its place ----
    ofSetColor(s.auditioning ? ofColor(120, 200, 160) : ofColor(235, 238, 245));
    text(fit(s.auditioning
        ? (st.auditionLocked ? "AUDITION  (locked -- v to edit)"
                          : "AUDITION  (release space to edit)")
        : "EDIT"), x, y);
    ofSetColor(180, 186, 200);
    text(fit(st.name
             + (st.unsaved ? "   (unsaved)" : "")), x, y + step);
    std::string g = std::string("grid ") + names[int(kind)];
    if (kind != GridKind::Free) {
        g += "  " + ofToString(spacing, 3) + "  "
           + ofToString(ofRadToDeg(rotation), 0) + " deg";
        if (kind == GridKind::Polar) g += "  " + ofToString(spokes) + " spokes";
    }
    text(fit(g), x, y + 2.0f * step);
    text(fit(std::string("auto-output ") + (model.autoOutput() ? "on" : "off")
             + "  (p)"), x, y + 3.0f * step);
    text(fit("nodes " + ofToString(manifold.nodeCount())
             + "   regions " + ofToString(manifold.regionCount())
             + "   selected " + ofToString(selection.size())), x, y + 4.0f * step);
    // The selection's fill, so a q or w press can be read before and after.
    if (!selection.empty()) {
        float lo = 2.0f, hi = -1.0f;
        for (NodeID id : selection.nodes()) {
            if (model.outputCount(id) == 0) continue;
            lo = std::min(lo, mapping.outputFraction(id));
            hi = std::max(hi, mapping.outputFraction(id));
        }
        const std::string fill = hi < 0.0f ? std::string("- (no output)")
            : ofToString(int(std::lround(lo * 100))) + "%"
              + (std::lround(lo * 100) == std::lround(hi * 100) ? std::string()
                 : " to " + ofToString(int(std::lround(hi * 100))) + "%");
        text(fit("fill " + fill), x, y + 5.0f * step);
    }
    if (!model.topology().orphans.empty()) {
        ofSetColor(140, 146, 160);
        text(fit("orphans " + ofToString(model.topology().orphans.size())
                 + "  (in no region yet)"), x, y + 6.0f * step);
    }
    y += 7.0f * step + lh * 0.5f;

    // ---- the message: one line, always here ----
    if (st.messageShown) {
        ofSetColor(st.warning ? ofColor(232, 96, 88)
                                       : ofColor(120, 200, 160));
        text(fit(st.message), x, y);
    }
    y += step + lh * 0.5f;

    // ---- the output you are acting on: three lines, never more ----
    const auto& sel = model.selectedOutputs();
    if (mapping.targetCount() == 0) {
        ofSetColor(140, 146, 160);
        text(fit("no outputs yet -- o makes one"), x, y);
    } else if (sel.size() == 1) {
        const TargetID t = sel.front();
        const float tr = mapping.targetTrim(t);
        std::size_t feeders = 0;
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            if (model.feeds(NodeID(i), t)) ++feeders;
        }
        ofSetColor(outputColour(mapping, t));
        ofFill();
        ofDrawRectangle(x - 12.0f, y - lh * 0.7f, 7.0f, lh * 0.7f);
        ofSetColor(255, 214, 90);
        text(fit("> " + mapping.targetName(t) + "  ch "
                 + ofToString(mapping.targetChannel(t)) + "  "
                 + (tr > 0.0f ? ofToString(20.0f * std::log10(tr), 1) + " dB"
                              : std::string("off"))), x, y);
        ofSetColor(180, 186, 200);
        text(fit("fed by " + ofToString(feeders)
                 + (feeders == 1 ? " node" : " nodes")), x, y + step);
    } else {
        std::string list;
        for (TargetID t : sel) {
            list += (list.empty() ? "" : ", ") + mapping.targetName(t);
        }
        ofSetColor(255, 214, 90);
        text(fit("+ " + ofToString(sel.size()) + " outputs: " + list), x, y);
        ofSetColor(180, 186, 200);
        text(fit("b and t act on all " + ofToString(sel.size())), x, y + step);
    }
    ofSetColor(140, 146, 160);
    const std::size_t derived = mapping.aggregators().size();
    text(fit(ofToString(mapping.targetCount()) + " outputs"
             + (derived ? ", " + ofToString(derived) + " derived" : std::string())
             + " -- hover a fader for details"), x, y + 2.0f * step);
    y += 3.0f * step + lh * 0.5f;

    // ---- the keys: always starting here ----
    // Grouped by intention; lit when the key would act, dim when it would
    // not -- drawn from the same key map that keyPressed() asks, so lit and
    // working cannot drift apart. b's line says what it WOULD do now.
    const KeyContext& ctx = keys;
    std::vector<KeyGroup> groups = keyMap(ctx);

    // With the 3D view open, the levels keys go beneath it, the rest here.
    if (s.levelsKeysAt.x >= 0.0f) {
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (groups[i].title != "levels") continue;
            const std::size_t chars =
                std::size_t(std::max(12.0f, s.levelsKeysWidth / cw));
            float yy = s.levelsKeysAt.y;
            ofSetColor(235, 238, 245);
            text(groups[i].title, s.levelsKeysAt.x, yy);
            yy += lh + 1.0f;
            for (const KeyEntry& e : groups[i].entries) {
                std::string k = e.keys;
                if (k.size() < 12) k.resize(12, ' ');
                std::string t = k + " " + e.action;
                if (t.size() > chars) t = t.substr(0, chars - 3) + "...";
                ofSetColor(e.available ? ofColor(200, 205, 215)
                                       : ofColor(82, 88, 100));
                text(t, s.levelsKeysAt.x, yy);
                yy += lh + 1.0f;
            }
            groups.erase(groups.begin() + std::ptrdiff_t(i));
            break;
        }
    }
    const bool twoColumns = room >= 64.0f * cw;
    const std::size_t colChars = twoColumns ? 31 : width;
    auto keyLine = [&](const KeyEntry& e) {
        std::string k = e.keys;
        if (k.size() < 12) k.resize(12, ' ');
        std::string t = k + " " + e.action;
        if (t.size() > colChars) t = t.substr(0, colChars - 3) + "...";
        return t;
    };
    // Two columns: place, shape, grid and listen on the left; outputs and
    // file on the right. One column: in order.
    const char* leftTitles[] = {"place and select", "shape the map", "grid",
                                "listen"};
    auto onLeft = [&](const std::string& title) {
        for (const char* l : leftTitles) if (title == l) return true;
        return false;
    };
    float yl = y, yr = y;
    for (const KeyGroup& g : groups) {
        const bool left = !twoColumns || onLeft(g.title);
        float& yy = left ? yl : yr;
        const float xx = left ? x : x + 33.0f * cw;
        ofSetColor(235, 238, 245);
        text(g.title, xx, yy);
        yy += lh + 1.0f;
        for (const KeyEntry& e : g.entries) {
            ofSetColor(e.available ? ofColor(200, 205, 215)
                                   : ofColor(82, 88, 100));
            text(keyLine(e), xx, yy);
            yy += lh + 1.0f;
        }
        yy += lh * 0.5f;
    }
}

} // namespace editor
} // namespace ofxManifold
