#include "ofxManifoldEditorLevelView.h"

#include "ofxManifoldEditorListening.h"

#include <algorithm>

namespace ofxManifold {
namespace editor {

namespace {

// The map is drawn this many world units across: the floor plan's 0..1 in x
// and y, and the same scale in height, so a level a quarter of the map above
// another is drawn a quarter of the map's width above it.
float worldSize(const ofRectangle& area) {
    return std::min(area.getWidth(), area.getHeight()) * 0.9f;
}

} // namespace

void aimLevelView(ofEasyCam& cam, const ofRectangle& area) {
    const float S = worldSize(area);
    cam.setControlArea(area);
    cam.setPosition(0.0f, S * 0.7f, S * 1.2f);
    cam.lookAt(glm::vec3(0.0f));
    cam.setTarget(glm::vec3(0.0f));
    cam.setDistance(S * 1.4f);
}

void drawLevelView(const Scene& s, ofEasyCam& cam, const ofRectangle& area) {
    const Model& model = *s.model;
    const Manifold2D& m = model.manifold();
    const Levels& lv = model.map3d().levels();
    const std::size_t n = lv.count();
    const LevelID cur = model.currentLevel();
    const float S = worldSize(area);
    // Centred on the middle of the levels' starting heights.
    const float zMid = (lv.base(0) + lv.base(n - 1)) * 0.5f;

    // Floor plan x across, height up, floor plan y away from the viewer.
    auto at = [&](glm::vec2 p, float z) {
        return glm::vec3((p.x - 0.5f) * S, (z - zMid) * S, -(p.y - 0.5f) * S);
    };
    auto nodeAt = [&](LevelID k, NodeID i) {
        return at(m.node(i).position, lv.height(k, i));
    };

    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(1.0f);
    ofSetColor(60, 64, 76);
    ofDrawRectangle(area);

    cam.begin(area);
    ofSetSphereResolution(8);

    // Each node straight up to itself on the level above.
    ofSetColor(90, 96, 112, 140);
    for (LevelID k = 0; k + 1 < n; ++k) {
        for (NodeID i = 0; i < m.nodeCount(); ++i) {
            ofDrawLine(nodeAt(k, i), nodeAt(k + 1, i));
        }
    }

    // Each level's regions, the current one brighter and drawn last.
    for (std::size_t pass = 0; pass < 2; ++pass) {
        for (LevelID k = 0; k < n; ++k) {
            if ((k == cur) != (pass == 1)) continue;
            ofSetLineWidth(k == cur ? 2.0f : 1.0f);
            if (k == cur) ofSetColor(190, 200, 220, 235);
            else          ofSetColor(120, 126, 140, 120);
            for (RegionID r = 0; r < m.regionCount(); ++r) {
                const auto& ids = m.region(r).ids();
                for (std::size_t j = 0; j < ids.size(); ++j) {
                    ofDrawLine(nodeAt(k, ids[j]),
                               nodeAt(k, ids[(j + 1) % ids.size()]));
                }
            }
        }
    }

    // The nodes of the current level; the selected ones in the selection's
    // yellow.
    ofFill();
    for (NodeID i = 0; i < m.nodeCount(); ++i) {
        if (s.selection->contains(i)) ofSetColor(255, 214, 90);
        else                          ofSetColor(190, 200, 220);
        ofDrawSphere(nodeAt(cur, i), S * 0.008f);
    }

    // The listening point, and a line dropped to the level below it: green
    // among the levels, grey outside them.
    const Evaluation& ev = *s.evaluation;
    if (ev.inside) {
        const Evaluation3D e = lv.evaluate(ev, s.listeningZ);
        const glm::vec3 p = at(s.listeningPoint, s.listeningZ);
        if (e.inside) ofSetColor(120, 200, 160);
        else          ofSetColor(110, 115, 125);
        ofDrawSphere(p, S * 0.012f);
        float below = 0.0f;
        bool drop = false;
        if (e.inside) {
            below = lv.heightAt(e.below, ev.weights);
            drop = true;
        } else if (s.listeningZ > lv.heightAt(n - 1, ev.weights)) {
            below = lv.heightAt(n - 1, ev.weights);      // above the top
            drop = true;
        }
        if (drop) {
            ofSetLineWidth(1.0f);
            ofDrawLine(p, at(s.listeningPoint, below));
        }
    }

    cam.end();
    ofPopStyle();
}

} // namespace editor
} // namespace ofxManifold
