#include "ofxManifoldRenderer.h"

using namespace ofxManifold;

void ofxManifoldRenderer::draw(RegionID activeRegion) const {
    ofPushStyle();

    const ofRectangle sq = square();

    if (style.drawFrame) {
        ofNoFill();
        ofSetLineWidth(1.0f);
        ofSetColor(style.frame);
        ofDrawRectangle(sq);
    }

    // Regions first, so nodes and edges land on top of them.
    if (style.drawRegions) {
        ofFill();
        for (std::size_t r = 0; r < manifold_->regionCount(); ++r) {
            const auto& ids = manifold_->region(static_cast<RegionID>(r)).ids();
            const glm::vec2 a = toScreen(manifold_->node(ids[0]).position);
            const glm::vec2 b = toScreen(manifold_->node(ids[1]).position);
            const glm::vec2 c = toScreen(manifold_->node(ids[2]).position);

            ofSetColor(static_cast<RegionID>(r) == activeRegion
                       ? style.regionActive : style.regionFill);
            ofDrawTriangle(a.x, a.y, b.x, b.y, c.x, c.y);
        }
    }

    if (style.drawEdges) {
        ofSetColor(style.edge);
        ofSetLineWidth(style.edgeWidth);
        for (std::size_t r = 0; r < manifold_->regionCount(); ++r) {
            const auto& ids = manifold_->region(static_cast<RegionID>(r)).ids();
            for (int e = 0; e < 3; ++e) {
                const glm::vec2 p = toScreen(manifold_->node(ids[e]).position);
                const glm::vec2 q =
                    toScreen(manifold_->node(ids[(e + 1) % 3]).position);
                ofDrawLine(p.x, p.y, q.x, q.y);
            }
        }
    }

    if (style.drawNodes) {
        ofFill();
        // Labels placed so far, so a later one can step aside rather than
        // stack. Two nodes close together produced "MC" -- two names drawn on
        // top of each other, which reads as one node with a nonsense name.
        std::vector<glm::vec2> placed;
        placed.reserve(manifold_->nodeCount());

        for (std::size_t i = 0; i < manifold_->nodeCount(); ++i) {
            const Node& n = manifold_->node(static_cast<NodeID>(i));
            const glm::vec2 p = toScreen(n.position);

            // Per-node weight shown as radius. The kernel treats it as an
            // ordinary scalar with no meaning; here it is the one property a
            // node has that is otherwise invisible, and a map whose author
            // forgot they biased a node is a map that behaves oddly for no
            // apparent reason.
            const float radius = style.nodeRadius
                               * ofClamp(std::sqrt(std::max(n.weight, 0.0f)),
                                         0.35f, 2.5f);
            ofSetColor(style.node);
            ofDrawCircle(p.x, p.y, radius);

            if (style.drawLabels) {
                glm::vec2 at(p.x + radius + 4.0f, p.y - 4.0f);

                // Step down until clear of every label already drawn. Bounded
                // so a dense cluster degrades into a short column rather than
                // a label wandering off across the window.
                for (int attempt = 0; attempt < 6; ++attempt) {
                    bool clash = false;
                    for (const glm::vec2& q : placed) {
                        if (std::abs(q.y - at.y) < 12.0f
                            && std::abs(q.x - at.x) < 70.0f) {
                            clash = true;
                            break;
                        }
                    }
                    if (!clash) break;
                    at.y += 13.0f;
                }
                placed.push_back(at);

                ofSetColor(style.nodeLabel);
                ofDrawBitmapString(n.name, at.x, at.y);
            }
        }
    }

    ofPopStyle();
}

void ofxManifoldRenderer::drawEvaluation(const Evaluation& e,
                                         const glm::vec2& point) const {
    ofPushStyle();

    const glm::vec2 s = toScreen(point);

    // Lines from the point to each contributing node, weight as thickness.
    // This is the picture the addon exists to make legible: not just where the
    // point is, but which nodes it is pulling on and by how much.
    if (e.inside) {
        for (const auto& wn : e.weights) {
            const glm::vec2 n = toScreen(manifold_->node(wn.id).position);
            ofSetColor(style.weightBar, 60 + 195 * ofClamp(wn.weight, 0.f, 1.f));
            ofSetLineWidth(1.0f + 5.0f * ofClamp(wn.weight, 0.0f, 1.0f));
            ofDrawLine(s.x, s.y, n.x, n.y);
        }
    }

    ofFill();
    ofSetColor(style.point);
    ofDrawCircle(s.x, s.y, style.pointRadius);

    // Outside the hull, say so where the point is. An empty weight readout and
    // a point that still draws is otherwise indistinguishable from a bug.
    if (!e.inside) {
        ofSetColor(style.warning);
        ofDrawBitmapString("outside", s.x + 10.0f, s.y - 8.0f);
    }

    ofPopStyle();
    drawWeights(e, glm::vec2(s.x + 14.0f, s.y + 14.0f));
}

void ofxManifoldRenderer::drawWeights(const Evaluation& e,
                                      const glm::vec2& at) const {
    if (!e.inside) return;

    ofPushStyle();
    float y = at.y;
    for (const auto& wn : e.weights) {
        // Text only. A bar drawn behind the number overlapped it, and the
        // number is the truth -- magnitude is already carried by the
        // thickness of the line from the point to the node.
        const std::string label = manifold_->node(wn.id).name + "  "
                                + ofToString(wn.weight, 3);
        ofSetColor(style.nodeLabel);
        ofDrawBitmapString(label, at.x, y);
        y += 15.0f;
    }
    ofPopStyle();
}

void ofxManifoldRenderer::drawTopology(const TopologyReport& rep) const {
    if (rep.clean()) return;

    ofPushStyle();
    ofSetColor(style.warning);

    // T-junctions where they are. Weights jump discontinuously as the point
    // crosses such an edge, and the author needs to see WHICH node sits on
    // WHICH edge to move it -- a name in a console does not locate anything.
    for (const auto& t : rep.tJunctions) {
        const glm::vec2 n = toScreen(manifold_->node(t.node).position);
        const glm::vec2 a = toScreen(manifold_->node(t.edgeA).position);
        const glm::vec2 b = toScreen(manifold_->node(t.edgeB).position);

        ofNoFill();
        ofSetLineWidth(2.0f);
        ofDrawCircle(n.x, n.y, style.nodeRadius + 6.0f);
        ofDrawLine(a.x, a.y, b.x, b.y);
        ofDrawBitmapString("T-junction", n.x + 12.0f, n.y + 4.0f);
    }

    // Orphans contribute to nothing. Almost always an authoring slip.
    ofFill();
    for (NodeID id : rep.orphans) {
        const glm::vec2 p = toScreen(manifold_->node(id).position);
        ofNoFill();
        ofSetLineWidth(2.0f);
        ofDrawCircle(p.x, p.y, style.nodeRadius + 6.0f);
        ofDrawBitmapString("orphan", p.x + 12.0f, p.y + 4.0f);
    }

    ofPopStyle();
}
