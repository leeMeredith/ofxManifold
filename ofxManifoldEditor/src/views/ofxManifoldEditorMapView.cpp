#include "ofxManifoldEditorMapView.h"

#include "ofxManifoldEditorListening.h"
#include "ofxManifoldEditorNodeGlyph.h"
#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

#include <cstdio>

namespace ofxManifold {
namespace editor {

// A segment in manifold space, clipped to the unit square before drawing, so
// grid lines stop at the map's edge instead of running into the panel.
// Liang-Barsky: clip the parameter range against each of the four edges.
static void drawSegment(const ofxManifoldRenderer* renderer,
                        glm::vec2 a, glm::vec2 b) {
    float t0 = 0.0f, t1 = 1.0f;
    const glm::vec2 d = b - a;
    const float p[4] = {-d.x, d.x, -d.y, d.y};
    const float q[4] = {a.x, 1.0f - a.x, a.y, 1.0f - a.y};
    for (int k = 0; k < 4; ++k) {
        if (std::fabs(p[k]) < 1e-12f) {
            if (q[k] < 0.0f) return;           // parallel and outside
            continue;
        }
        const float t = q[k] / p[k];
        if (p[k] < 0.0f) t0 = std::max(t0, t);
        else             t1 = std::min(t1, t);
        if (t0 > t1) return;
    }
    const glm::vec2 s0 = renderer->toScreen(a + d * t0);
    const glm::vec2 s1 = renderer->toScreen(a + d * t1);
    ofDrawLine(s0.x, s0.y, s1.x, s1.y);
}

void drawGrid(const Scene& s, const GridLook& look) {
    const ofxManifoldRenderer* renderer = s.renderer;
    const GridKind kind = look.kind;
    const Grid& grid = *look.grid;
    const float spacing = look.spacing;
    const float rotation = look.rotation;
    const int spokes = look.spokes;
    if (kind == GridKind::Free) return;
    ofPushStyle();
    ofSetLineWidth(1.0f);

    // Faint lines first, so the points sit on top of them.
    ofSetColor(70, 76, 92, 70);
    if (kind == GridKind::Polar) {
        const glm::vec2 c = grid.point({0, 0, true});
        const int rings = int(0.75f / spacing) + 1;
        // Rings, as polylines through many points, each piece clipped.
        const int steps = 96;
        for (int ring = 1; ring <= rings; ++ring) {
            const float r = ring * spacing;
            for (int k = 0; k < steps; ++k) {
                const float a0 = rotation + k * TWO_PI / steps;
                const float a1 = rotation + (k + 1) * TWO_PI / steps;
                drawSegment(renderer, {c.x + r * std::cos(a0), c.y + r * std::sin(a0)},
                            {c.x + r * std::cos(a1), c.y + r * std::sin(a1)});
            }
        }
        // Spokes, from the centre out past the last ring.
        for (int k = 0; k < spokes; ++k) {
            drawSegment(renderer, c, grid.point({rings, k, true}));
        }
    } else {
        // Lattice lines: one family along each basis direction, and for a
        // triangular grid a third along their difference, which is what makes
        // the triangles visible rather than a skewed square mesh.
        const int n = int(1.6f / spacing) + 2;
        for (int i = -n; i <= n; ++i) {
            drawSegment(renderer, grid.point({i, -n, true}), grid.point({i, n, true}));
            drawSegment(renderer, grid.point({-n, i, true}), grid.point({n, i, true}));
            if (kind == GridKind::Triangular) {
                // Points (c - t, t): the direction v - u.
                drawSegment(renderer, grid.point({i + n, -n, true}),
                            grid.point({i - n,  n, true}));
            }
        }
    }

    // Points.
    ofFill();
    ofSetColor(90, 96, 112, 140);
    auto dot = [&](glm::vec2 p) {
        if (p.x < -0.001f || p.x > 1.001f || p.y < -0.001f || p.y > 1.001f) {
            return;
        }
        const glm::vec2 s = renderer->toScreen(p);
        ofDrawCircle(s.x, s.y, 1.6f);
    };
    if (kind == GridKind::Polar) {
        dot(grid.point({0, 0, true}));
        const int rings = int(0.75f / spacing) + 1;
        for (int ring = 1; ring <= rings; ++ring) {
            for (int k = 0; k < spokes; ++k) dot(grid.point({ring, k, true}));
        }
    } else {
        const int n = int(1.6f / spacing) + 2;
        for (int i = -n; i <= n; ++i) {
            for (int j = -n; j <= n; ++j) dot(grid.point({i, j, true}));
        }
    }
    ofPopStyle();
}

// Plug at the sending end, socket at the receiving end, as on gear: outputs
// male, inputs female. Each line in the colour of the output it feeds -- the
// same colour as that output's fader and its node's name -- heavy and sized
// by share when the sender is selected, faint when only the receiver is.
//
// Lines are ARCS, bending to the side with more clearance from other nodes
// (editor::linkLines decides, tested), so a line never seems to join a node
// it only passes; two nodes feeding each other arc to opposite sides. The
// percentage sits a quarter of the way from the SENDER: lines leaving one
// node fan apart there, where at their middles they crossed and overlapped.
void drawLinks(const Scene& s) {
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

    const std::vector<LinkLine> lines = linkLines(model, selection);
    if (lines.empty()) return;
    ofPushStyle();
    for (const LinkLine& l : lines) {
        const glm::vec2 a = renderer->toScreen(manifold.node(l.from).position);
        const glm::vec2 b = renderer->toScreen(manifold.node(l.to).position);
        const float pullA = nodeRadius(manifold, l.from) + 4.0f;
        const float pullB = nodeRadius(manifold, l.to) + 5.0f;
        if (glm::distance(a, b) <= pullA + pullB + 6.0f) continue;

        // The arc, sampled, trimmed back to the edges of both nodes so the
        // plug and the socket stay visible.
        std::vector<glm::vec2> pts;
        for (int k = 0; k <= 40; ++k) {
            const glm::vec2 p =
                renderer->toScreen(linkPoint(model, l, float(k) / 40.0f));
            if (glm::distance(p, a) < pullA || glm::distance(p, b) < pullB) {
                continue;
            }
            pts.push_back(p);
        }
        if (pts.size() < 2) continue;

        ofColor c = outputColour(mapping, l.output);
        if (l.heavy) {
            ofSetLineWidth(1.0f + 5.0f * ofClamp(l.share, 0.0f, 1.0f));
        } else {
            c.a = 120;
            ofSetLineWidth(1.0f);
        }
        ofSetColor(c);
        ofNoFill();
        ofBeginShape();
        for (const glm::vec2& p : pts) ofVertex(p.x, p.y);
        ofEndShape(false);

        ofSetLineWidth(1.5f);
        ofFill();
        ofDrawCircle(pts.front().x, pts.front().y, 3.5f);   // plug
        ofNoFill();
        ofDrawCircle(pts.back().x, pts.back().y, 4.5f);     // socket

        if (selection.size() == 1 && l.heavy) {
            // Near the sender, nudged off the line to the outside of the arc.
            const glm::vec2 at =
                renderer->toScreen(linkPoint(model, l, 0.25f));
            const glm::vec2 ahead =
                renderer->toScreen(linkPoint(model, l, 0.30f));
            glm::vec2 dir = ahead - at;
            const float dl = glm::length(dir);
            dir = dl > 0.0f ? dir / dl : glm::vec2(1.0f, 0.0f);
            const glm::vec2 mid = renderer->toScreen(linkPoint(model, l, 0.5f));
            glm::vec2 out(-dir.y, dir.x);
            if (glm::dot(out, mid - (a + b) * 0.5f) < 0.0f) out = -out;
            const glm::vec2 tp = at + out * 10.0f;
            const std::string pct =
                ofToString(int(std::lround(l.share * 100))) + "%";
            ofSetColor(c);
            text(pct, tp.x - 0.5f * cw * float(pct.size()), tp.y + 0.3f * lh);
        }
    }
    ofPopStyle();
}

void drawOffsets(const Scene& s) {
    const Model& model = *s.model;
    const Manifold2D& manifold = model.manifold();
    const Levels& lv = model.map3d().levels();
    const LevelID k = model.currentLevel();
    const float lh = s.text->lineHeight();
    bool any = false;
    for (NodeID i = 0; i < manifold.nodeCount(); ++i) {
        if (!lv.bent(k, i)) continue;
        if (!any) { ofPushStyle(); any = true; }
        char b[16];
        std::snprintf(b, sizeof b, "%+.2f", double(lv.height(k, i) - lv.base(k)));
        const glm::vec2 p = s.renderer->toScreen(manifold.node(i).position);
        ofSetColor(150, 190, 235);
        s.text->draw(b, p.x + 12.0f, p.y + lh + 4.0f);
    }
    if (any) ofPopStyle();
}

void drawLevelColumn(const Scene& s) {
    const Model& model = *s.model;
    const ofRectangle vp = s.renderer->viewport();
    const float inset = vp.getHeight() * kColumnInset;
    const DotColumn c = dotColumn(model, vp.getY() + inset,
                                  vp.getBottom() - inset, true,
                                  *s.evaluation, s.listeningZ);
    if (!c.shown) return;
    const float x = vp.getRight() + kColumnOffset;
    const Levels& lv = model.map3d().levels();
    // A dot being dragged sits where its level would land, a gap between
    // dots being a quarter of the map, and its label says where.
    const float gap = c.dots.size() > 1 ? c.dots[0] - c.dots[1] : 1.0f;
    auto dotY = [&](std::size_t k) {
        if (int(k) != s.levelDrag) return c.dots[k];
        return c.dots[k] - (s.levelDragBase - lv.base(LevelID(k))) / 0.25f * gap;
    };
    ofPushStyle();
    // the spine, faint, then a dot per level
    ofSetLineWidth(1.0f);
    ofSetColor(70, 76, 92);
    ofDrawLine(x, c.dots.front(), x, c.dots.back());
    for (std::size_t k = 0; k < c.dots.size(); ++k) {
        if (k == c.current) {
            ofFill();
            ofSetColor(235, 238, 245);
        } else {
            ofNoFill();
            ofSetColor(150, 156, 170);
        }
        if (int(k) == s.levelDrag) {
            ofFill();
            ofSetColor(255, 214, 90);
        }
        ofDrawCircle(x, dotY(k), 4.0f);
    }
    // the current level's starting height beside its dot -- or, while one
    // is dragged, where that one would land
    const std::size_t shownK = s.levelDrag >= 0 ? std::size_t(s.levelDrag)
                                                : c.current;
    char b[16];
    std::snprintf(b, sizeof b, "%g",
                  double(s.levelDrag >= 0 ? s.levelDragBase
                                          : lv.base(LevelID(shownK))));
    if (s.levelDrag >= 0) ofSetColor(255, 214, 90);
    else                  ofSetColor(235, 238, 245);
    s.text->draw(b, x + 9.0f, dotY(shownK) + s.text->lineHeight() * 0.35f);
    // the listening mark: green among the levels, grey outside them
    if (c.marked) {
        ofSetLineWidth(2.0f);
        if (c.outside) ofSetColor(110, 115, 125);
        else           ofSetColor(120, 200, 160);
        ofDrawLine(x - 8.0f, c.mark, x + 8.0f, c.mark);
    }
    ofPopStyle();
}

void drawMap(const Scene& s, const GridLook& look, const MapOverlay& o) {
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

    drawGrid(s, look);
    renderer->draw(evaluation.inside ? evaluation.regionID : InvalidRegion);

    // Halos first, so they sit behind the nodes.
    // Halos always: each node's live share, the quantity the faders add up.
    for (const auto& wn : evaluation.weights) drawHalo(s, wn.id, wn.weight);

    // Link lines, under the nodes: how the selected nodes connect to others.
    drawLinks(s);
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        drawNode(s, NodeID(i));
    }
    drawOffsets(s);

    // Nodes feeding the CURRENT output: a green ring, outside the selection
    // ring so both can show at once. With the fader marks below, this is how
    // a binding can be seen before and after pressing b.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(1.5f);
    if (!model.selectedOutputs().empty()) {
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            TargetID fed = InvalidTarget;
            for (TargetID t : model.selectedOutputs()) {
                if (model.feeds(NodeID(i), t)) { fed = t; break; }
            }
            if (fed == InvalidTarget) continue;
            const glm::vec2 p = renderer->toScreen(manifold.node(NodeID(i)).position);
            ofSetColor(outputColour(mapping, fed));
            ofDrawCircle(p.x, p.y, 18.0f);
        }
    }
    ofPopStyle();

    // Selection rings, over the nodes.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.0f);
    for (NodeID id : selection.nodes()) {
        const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
        ofSetColor(o.refused ? ofColor(232, 96, 88)
                                       : ofColor(255, 214, 90));
        ofDrawCircle(p.x, p.y, 13.0f);
    }
    if (o.boxing && glm::distance(o.boxTo, o.boxFrom) >= 4.0f) {
        ofSetColor(255, 214, 90, 160);
        ofSetLineWidth(1.0f);
        ofDrawRectangle(std::min(o.boxFrom.x, o.boxTo.x),
                        std::min(o.boxFrom.y, o.boxTo.y),
                        std::fabs(o.boxTo.x - o.boxFrom.x),
                        std::fabs(o.boxTo.y - o.boxFrom.y));
    }
    ofPopStyle();

    // The listening point, always. Its influence lines only while
    // auditioning; otherwise just the point, quieter, so it is there without
    // competing with editing.
    if (s.auditioning) {
        renderer->drawEvaluation(evaluation, s.listeningPoint);
    } else {
        const glm::vec2 lp = renderer->toScreen(s.listeningPoint);
        ofPushStyle();
        ofNoFill();
        ofSetLineWidth(1.5f);
        ofSetColor(235, 238, 245, 140);
        ofDrawCircle(lp.x, lp.y, 7.0f);
        ofDrawLine(lp.x - 11.0f, lp.y, lp.x - 4.0f, lp.y);
        ofDrawLine(lp.x + 4.0f, lp.y, lp.x + 11.0f, lp.y);
        ofDrawLine(lp.x, lp.y - 11.0f, lp.x, lp.y - 4.0f);
        ofDrawLine(lp.x, lp.y + 4.0f, lp.x, lp.y + 11.0f);
        ofPopStyle();
    }

    drawLevelColumn(s);
}

} // namespace editor
} // namespace ofxManifold
