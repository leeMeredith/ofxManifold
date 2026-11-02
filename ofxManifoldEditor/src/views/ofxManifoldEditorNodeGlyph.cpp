#include "ofxManifoldEditorNodeGlyph.h"

#include "ofxManifoldEditorStyle.h"
#include "ofxManifoldEditorText.h"

namespace ofxManifold {
namespace editor {

// Keep the part of a polygon at or below screen height `cut` (screen y grows
// downward, so "below" is a larger y). One edge of Sutherland-Hodgman.
static std::vector<glm::vec2> keepBelow(const std::vector<glm::vec2>& poly,
                                        float cut) {
    std::vector<glm::vec2> out;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = poly[i];
        const glm::vec2 b = poly[(i + 1) % n];
        const bool ina = a.y >= cut, inb = b.y >= cut;
        if (ina) out.push_back(a);
        if (ina != inb) {
            const float t = (cut - a.y) / (b.y - a.y);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

float nodeRadius(const Manifold2D& manifold, NodeID id) {
    return 6.0f * ofClamp(std::sqrt(std::max(manifold.node(id).weight, 0.0f)),
                          0.35f, 2.5f);
}

// One property per visual channel (decision H):
//
//   shape  what the node does with its share
//            no output      hollow square
//            one output     circle
//            several        triangle
//   fill   how much of that share reaches an output, from the bottom up
//   size   the node's bias weight, as the renderer draws it
//
// All of it computed from the mapping every frame, never stored: a stored
// type that could disagree with the bindings would be the D-018/019/020
// pattern again. Shape carries the meaning, not colour, so it still reads on
// a projector or a monochrome printout.
void drawNode(const Scene& s, NodeID id) {
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

    const Node& n = manifold.node(id);
    const glm::vec2 p = renderer->toScreen(n.position);
    const float r = nodeRadius(manifold, id);
    const std::size_t outs = model.outputCount(id);
    const float frac = mapping.outputFraction(id);

    std::vector<glm::vec2> shape;
    if (outs == 0) {
        const float h = r * 0.95f;
        shape = {{p.x - h, p.y - h}, {p.x + h, p.y - h},
                 {p.x + h, p.y + h}, {p.x - h, p.y + h}};
    } else if (outs == 1) {
        for (int k = 0; k < 24; ++k) {
            const float a = k * TWO_PI / 24.0f;
            shape.push_back({p.x + r * std::cos(a), p.y + r * std::sin(a)});
        }
    } else {
        // Pointing up, as if distributing outward.
        const float R = r * 1.3f;
        for (int k = 0; k < 3; ++k) {
            const float a = -HALF_PI + k * TWO_PI / 3.0f;
            shape.push_back({p.x + R * std::cos(a), p.y + R * std::sin(a)});
        }
    }

    ofPushStyle();
    const ofColor ink(215, 220, 232);
    if (outs > 0 && frac > 0.0f) {
        float top = shape[0].y, bottom = shape[0].y;
        for (const glm::vec2& v : shape) {
            top = std::min(top, v.y);
            bottom = std::max(bottom, v.y);
        }
        const std::vector<glm::vec2> part =
            keepBelow(shape, bottom - frac * (bottom - top));
        ofFill();
        ofSetColor(ink);
        ofBeginShape();
        for (const glm::vec2& v : part) ofVertex(v.x, v.y);
        ofEndShape(true);
    }
    ofNoFill();
    ofSetLineWidth(1.5f);
    ofSetColor(ink);
    for (std::size_t k = 0; k < shape.size(); ++k) {
        const glm::vec2 a = shape[k], b = shape[(k + 1) % shape.size()];
        ofDrawLine(a.x, a.y, b.x, b.y);
    }
    ofSetColor(180, 186, 200);
    // A node with an output of its own -- one named after it -- has its name
    // in that output's colour, so a line's colour leads to its node, and on
    // to the fader of the same colour.
    const TargetID own = mapping.findTarget(n.name);
    if (own != InvalidTarget) ofSetColor(outputColour(mapping, own));
    text(n.name, p.x + r + 6.0f, p.y - 6.0f);
    ofPopStyle();
}

// A node's LIVE share while auditioning, as a halo behind it -- the glow from
// example-spread, brought into the editor.
//
// Its own element, so it does not compete with the node's shape, fill or
// size (decision H). Two tones, matching the chart:
//
//   green core   the share that reaches outputs
//   grey rim     the share that goes to silence
//
// AREA grows in proportion to the share, not radius, because area is what
// the eye compares: radius sqrt(r^2 + K w), so a node with twice the share
// has twice the halo. A negative weight -- possible inside a non-convex
// region -- draws no halo; the renderer already draws it as a red line.
void drawHalo(const Scene& s, NodeID id, float weight) {
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

    if (weight <= 0.0f || id >= manifold.nodeCount()) return;
    const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
    const float r = nodeRadius(manifold, id);
    const float K = (r + 30.0f) * (r + 30.0f) - r * r;   // full share: +30 px
    const float frac = mapping.outputFraction(id);
    const float total = std::sqrt(r * r + K * weight);
    const float core  = std::sqrt(r * r + K * weight * frac);

    ofPushStyle();
    ofFill();
    ofSetColor(120, 126, 140, 110);
    ofDrawCircle(p.x, p.y, total);
    if (frac > 0.0f) {
        ofSetColor(120, 200, 160, 130);
        ofDrawCircle(p.x, p.y, core);
    }
    ofPopStyle();
}

} // namespace editor
} // namespace ofxManifold
