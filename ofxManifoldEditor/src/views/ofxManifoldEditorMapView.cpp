#include "ofxManifoldEditorMapView.h"

#include <set>

#include "ofxManifoldEditorListening.h"
#include "ofxManifoldEditorNodeGlyph.h"
#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

#include <cstdio>
#include <cstring>

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
    const float rotation = look.rotation;
    const int spokes = look.spokes;
    if (kind == GridKind::Free) return;
    ofPushStyle();
    ofSetLineWidth(1.0f);
    const ofColor faint(70, 76, 92, 70), bright(150, 200, 255, 170);
    auto key = [](const GridAddress& a) { return std::make_pair(a.a, a.b); };

    if (kind == GridKind::Polar) {
        // The whole rings only, and the spokes out to the last of them
        // (3A-11): nothing cut off at the map's edge.
        const glm::vec2 c = grid.point({0, 0, true});
        const int steps = 96;
        auto ring = [&](int r) {
            const float rr = float(r) * look.spacing;
            for (int k = 0; k < steps; ++k) {
                const float a0 = rotation + k * TWO_PI / steps;
                const float a1 = rotation + (k + 1) * TWO_PI / steps;
                drawSegment(renderer, {c.x + rr * std::cos(a0), c.y + rr * std::sin(a0)},
                            {c.x + rr * std::cos(a1), c.y + rr * std::sin(a1)});
            }
        };
        ofSetColor(faint);
        for (int r = 1; r <= look.rings; ++r) ring(r);
        for (int k = 0; k < spokes && look.rings > 0; ++k) {
            drawSegment(renderer, c, grid.point({look.rings, k, true}));
        }
        if (look.lineOn && look.rings > 0) {
            ofSetColor(bright);
            ofSetLineWidth(2.0f);
            if (look.lineAt.a >= 1 && look.lineAt.a <= look.rings) ring(look.lineAt.a);
            if (look.lineAt.a >= 1) {
                drawSegment(renderer, c, grid.point({look.rings, look.lineAt.b, true}));
            }
            ofSetLineWidth(1.0f);
        }
        ofFill();
        ofSetColor(90, 96, 112, 140);
        auto dot = [&](glm::vec2 p) {
            const glm::vec2 q = renderer->toScreen(p);
            ofDrawCircle(q.x, q.y, 1.6f);
        };
        if (look.rings > 0) dot(c);
        for (int r = 1; r <= look.rings; ++r) {
            for (int k = 0; k < spokes; ++k) dot(grid.point({r, k, true}));
        }
        ofPopStyle();
        return;
    }

    // Lattice: the edges of the whole cells, each once, then their corners.
    std::set<std::pair<std::pair<int, int>, std::pair<int, int>>> edges;
    std::set<std::pair<int, int>> corners;
    if (look.cells) {
        for (const auto& cell : *look.cells) {
            for (std::size_t i = 0; i < cell.size(); ++i) {
                auto a = key(cell[i]), b = key(cell[(i + 1) % cell.size()]);
                if (b < a) std::swap(a, b);
                edges.insert({a, b});
                corners.insert(key(cell[i]));
            }
        }
    }
    auto pt = [&](std::pair<int, int> k) { return grid.point({k.first, k.second, true}); };
    ofSetColor(faint);
    for (const auto& e : edges) drawSegment(renderer, pt(e.first), pt(e.second));
    // The lines through the point a node is going to, brighter: its row and
    // column, and on the triangular grid the third direction -- each edge of
    // a whole cell that lies on one of them.
    if (look.lineOn) {
        ofSetColor(bright);
        ofSetLineWidth(2.0f);
        const GridAddress& at = look.lineAt;
        for (const auto& e : edges) {
            const auto& a = e.first;
            const auto& b = e.second;
            const bool row = a.second == at.b && b.second == at.b;
            const bool col = a.first == at.a && b.first == at.a;
            const bool third = kind == GridKind::Triangular
                && a.first + a.second == at.a + at.b
                && b.first + b.second == at.a + at.b;
            if (row || col || third) drawSegment(renderer, pt(a), pt(b));
        }
        ofSetLineWidth(1.0f);
    }
    ofFill();
    ofSetColor(90, 96, 112, 140);
    for (const auto& k : corners) {
        const glm::vec2 q = renderer->toScreen(pt(k));
        ofDrawCircle(q.x, q.y, 1.6f);
    }
    ofPopStyle();
}

void drawLinedUp(const Scene& s, const GridLook& look) {
    if (!look.lineOn || look.lined.empty()) return;
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.0f);
    ofSetColor(150, 200, 255, 220);
    for (NodeID n : look.lined) {
        const glm::vec2 q = s.renderer->toScreen(s.model->manifold().node(n).position);
        ofDrawCircle(q.x, q.y, 11.0f);
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

void drawLayoutPreview(const Scene& s) {
    if (!s.layoutPreview || !s.layoutPreview->ok) return;
    const LayoutPlan& plan = *s.layoutPreview;
    const ofxManifoldRenderer* renderer = s.renderer;
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(1.0f);
    ofSetColor(120, 200, 255, 110);
    for (const auto& r : plan.regions) {
        ofBeginShape();
        for (std::size_t i : r) {
            const glm::vec2 q = renderer->toScreen(plan.points[i]);
            ofVertex(q.x, q.y);
        }
        ofEndShape(true);
    }
    ofSetColor(120, 200, 255, 200);
    for (std::size_t i = 0; i < plan.points.size(); ++i) {
        const glm::vec2 q = renderer->toScreen(plan.points[i]);
        ofDrawCircle(q.x, q.y, 5.0f);
        // Numbered while there are few enough to read.
        if (plan.points.size() <= 16) {
            s.text->draw(std::to_string(i + 1), q.x + 6.0f, q.y - 6.0f);
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

// Each node's current value on the current level (3A-7), below-left of it,
// clear of the height offset above-right. Nothing without a current value.
void drawValues(const Scene& s) {
    const Model& model = *s.model;
    const std::size_t v = model.currentValue();
    if (v == Model::NoValue) return;
    const Manifold2D& manifold = model.manifold();
    const float lh = s.text->lineHeight(), cw = s.text->charWidth();
    ofPushStyle();
    ofSetColor(200, 190, 240);
    for (NodeID i = 0; i < manifold.nodeCount(); ++i) {
        char b[16];
        std::snprintf(b, sizeof b, "%.3g",
                      double(model.map3d().value(model.currentLevel(), i, v)));
        const glm::vec2 p = s.renderer->toScreen(manifold.node(i).position);
        s.text->draw(b, p.x - 12.0f - cw * float(std::strlen(b)),
                     p.y + lh + 4.0f);
    }
    ofPopStyle();
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
    drawValues(s);
    drawLayoutPreview(s);
    drawLinedUp(s, look);

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
