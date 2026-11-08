#include "ofxManifoldEditorKeyPanel.h"

#include "ofxManifoldEditorListening.h"

#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

namespace ofxManifold {
namespace editor {


// ---------------------------------------------------------------------------
// the panel
// ---------------------------------------------------------------------------
// The panel in FIXED zones, so nothing pushes anything else: status lines
// that are always in the same places, one line for the message, a few lines
// for the output you are acting on, then the keys. The full list of outputs
// used to sit in the middle and grow with every output, pushing the keys off
// the bottom of the window; the faders already show every output, and hover
// gives any one's details.
namespace {

// The keys where flowKeys put them: titles bright, keys lit when they would
// act and dim when not, each line cut to its column; and the note of what
// was left out.
void drawKeyLines(const Scene& s, const std::vector<KeyGroup>& groups,
                  const KeyFlow& flow) {
    const std::size_t width = flow.chars;
    auto fit = [width](std::string t) {
        if (t.size() > width) t = t.substr(0, width - 3) + "...";
        return t;
    };
    for (const KeyLine& kl : flow.lines) {
        const KeyGroup& grp = groups[kl.group];
        if (kl.entry < 0) {
            ofSetColor(235, 238, 245);
            s.text->draw(fit(grp.title + (kl.more ? " (more)" : "")), kl.x, kl.y);
            continue;
        }
        const KeyEntry& e = grp.entries[std::size_t(kl.entry)];
        std::string k = e.keys;
        if (k.size() < 12) k.resize(12, ' ');
        ofSetColor(e.available ? ofColor(200, 205, 215) : ofColor(82, 88, 100));
        s.text->draw(fit(k + " " + e.action), kl.x, kl.y);
    }
    if (flow.hidden > 0 || flow.dimLeftOut > 0) {
        ofSetColor(232, 170, 88);
        s.text->draw(fit(flow.hidden > 0
                     ? "+ " + ofToString(flow.hidden) + " more -- ? shows every key"
                     : std::string("dim keys left out -- ? shows every key")),
             flow.moreX, flow.moreY);
    }
}

} // namespace

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

    // Beside the views. The status at the top of the first column, the keys
    // flowing down it and on into the next columns, which start at the top
    // (3A-10).
    const float x = s.panelLeft >= 0.0f ? s.panelLeft
                                        : renderer->viewport().getRight() + 30.0f;
    const float right = float(ofGetWidth()) - 16.0f;
    const float bottom = s.panelBottom >= 0.0f ? s.panelBottom
                                               : float(ofGetHeight()) - 8.0f;
    const std::vector<std::pair<std::string, float>> vals =
        valuesAt(model, evaluation, s.listeningZ);

    const std::vector<KeyGroup> all = keyMap(keys);
    const std::vector<KeyGroup> groups =
        panelGroups(all, s.levelsKeysAt.x >= 0.0f);
    // With the 3D view open, the levels keys go beneath it, the rest here.
    if (s.levelsKeysAt.x >= 0.0f) {
        for (const KeyGroup& lv : all) {
            if (lv.title != "levels") continue;
            const std::size_t chars =
                std::size_t(std::max(12.0f, s.levelsKeysWidth / cw));
            float yy = s.levelsKeysAt.y;
            ofSetColor(235, 238, 245);
            text(lv.title, s.levelsKeysAt.x, yy);
            yy += lh + 1.0f;
            for (const KeyEntry& e : lv.entries) {
                std::string k = e.keys;
                if (k.size() < 12) k.resize(12, ' ');
                std::string t = k + " " + e.action;
                if (t.size() > chars) t = t.substr(0, chars - 3) + "...";
                ofSetColor(e.available ? ofColor(200, 205, 215)
                                       : ofColor(82, 88, 100));
                text(t, s.levelsKeysAt.x, yy);
                yy += lh + 1.0f;
            }
        }
    }
    const KeyFlow flow = flowKeys(groups, x, right, panelTop(lh),
                                  panelKeysTop(lh, !vals.empty()), bottom,
                                  cw, lh);

    const std::size_t width = flow.chars;
    // Any line longer than the column is shortened rather than run off it.
    auto fit = [width](std::string s) {
        if (s.size() > width) s = s.substr(0, width - 3) + "...";
        return s;
    };
    const float step = lh + 2.0f;
    float y = panelTop(lh);
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
        g += "  " + ofToString(int(std::lround(1.0f / spacing)))
           + (kind == GridKind::Polar ? " to a side" : " across") + "  "
           + ofToString(ofRadToDeg(rotation), 0) + " deg";
        if (kind == GridKind::Polar) g += "  " + ofToString(spokes) + " spokes";
    }
    text(fit(g), x, y + 2.0f * step);
    text(fit(std::string("auto-output ") + (model.autoOutput() ? "on" : "off")
             + "  (p)"), x, y + 3.0f * step);
    text(fit("nodes " + ofToString(manifold.nodeCount())
             + "   regions " + ofToString(manifold.regionCount())
             + "   selected " + ofToString(selection.size())), x, y + 4.0f * step);
    // The selection's fill, so a q or w press can be read before and after;
    // and how many of it are silent, which c undoes (3A-10).
    if (!selection.empty()) {
        float lo = 2.0f, hi = -1.0f;
        std::size_t silent = 0;
        for (NodeID id : selection.nodes()) {
            if (model.outputCount(id) == 0) { ++silent; continue; }
            lo = std::min(lo, mapping.outputFraction(id));
            hi = std::max(hi, mapping.outputFraction(id));
        }
        std::string line;
        if (hi < 0.0f) {
            line = "silent -- c gives back its own output";
        } else {
            line = "fill " + ofToString(int(std::lround(lo * 100))) + "%"
                 + (std::lround(lo * 100) == std::lround(hi * 100) ? std::string()
                    : " to " + ofToString(int(std::lround(hi * 100))) + "%");
            if (silent) line += "   " + ofToString(silent) + " silent (c)";
        }
        if (silent) ofSetColor(232, 170, 88);
        text(fit(line), x, y + 5.0f * step);
        ofSetColor(180, 186, 200);
    }
    if (!model.topology().orphans.empty()) {
        ofSetColor(140, 146, 160);
        text(fit("orphans " + ofToString(model.topology().orphans.size())
                 + "  (in no region yet)"), x, y + 6.0f * step);
    }
    y += 7.0f * step + lh * 0.5f;

    // ---- the message: two lines, always here -- or the typing line ----
    // Wrapped at the column's width, at a space where there is one.
    auto twoLines = [width](const std::string& m, std::string& a,
                            std::string& b) {
        a = m; b.clear();
        if (a.size() <= width) return;
        std::size_t cut = a.rfind(' ', width);
        if (cut == std::string::npos || cut < width / 2) cut = width;
        b = a.substr(cut + (a[cut] == ' ' ? 1 : 0));
        a = a.substr(0, cut);
        if (b.size() > width) b = b.substr(0, width - 3) + "...";
    };
    std::string m1, m2;
    if (s.typing) {
        twoLines(s.typingPrompt + ": " + s.typingLine
                 + "_   return makes it, esc cancels", m1, m2);
        ofSetColor(255, 214, 90);
    } else if (st.messageShown) {
        twoLines(st.message, m1, m2);
        ofSetColor(st.warning ? ofColor(232, 96, 88) : ofColor(120, 200, 160));
    }
    if (!m1.empty()) text(m1, x, y);
    if (!m2.empty()) text(m2, x, y + step);
    y += 2.0f * step + lh * 0.5f;

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
             + " -- hover a fader"), x, y + 2.0f * step);
    y += 3.0f * step + lh * 0.5f;

    // ---- node values at the listening point (3A-7), only if there are any:
    // every value, the current one marked, blending as the point moves ----
    if (!vals.empty()) {
        std::string line = "values";
        for (std::size_t v = 0; v < vals.size(); ++v) {
            line += (v == model.currentValue() ? "  >" : "  ") + vals[v].first
                  + " " + ofToString(vals[v].second, 3);
        }
        ofSetColor(200, 190, 240);
        text(fit(line), x, y);
    }

    // ---- the keys, where flowKeys put them ----
    // Grouped by intention; lit when the key would act, dim when it would
    // not -- drawn from the same key map that keyPressed() asks, so lit and
    // working cannot drift apart. b's line says what it WOULD do now.
    drawKeyLines(s, groups, flow);
}

void drawKeySheet(const Scene& s, const KeyContext& keys) {
    const float lh = s.text->lineHeight(), cw = s.text->charWidth();
    ofPushStyle();
    ofFill();
    ofSetColor(18, 20, 26, 240);
    ofDrawRectangle(0.0f, 0.0f, float(ofGetWidth()), float(ofGetHeight()));
    ofSetColor(255, 214, 90);
    const float top = 24.0f + lh;
    s.text->draw("every key -- any key or click closes", 24.0f, top);
    const std::vector<KeyGroup> groups = keyMap(keys);
    const float first = top + 2.0f * (lh + 1.0f);
    const KeyFlow flow = flowKeys(groups, 24.0f, float(ofGetWidth()) - 24.0f,
                                  first, first, float(ofGetHeight()) - 16.0f,
                                  cw, lh);
    drawKeyLines(s, groups, flow);
    ofPopStyle();
}

} // namespace editor
} // namespace ofxManifold
