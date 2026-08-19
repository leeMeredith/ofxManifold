#pragma once

// ofxManifold — renderer.
//
// The first file in this addon that includes ofMain.h, and the boundary where
// the discipline changes.
//
// Everything below src/ofx is provable: two independent implementations of the
// solve, of containment, of JSON, agreeing to a tolerance. A renderer has no
// second implementation and no ground truth. There is no Python reference for
// whether a manifold LOOKS right. So this file carries design weight rather
// than test weight, and it is meant to be judged on screen.
//
// What that buys it in return is a hard constraint: it contains NO geometry.
// Every number it draws comes from Manifold2D or from an Evaluation. If you
// find yourself computing a barycentric coordinate here, it belongs in the
// kernel where it can be proved.
//
// The renderer holds a CONST REFERENCE and does not own the manifold. A
// manifold is a shared resource with many independent readers -- several
// Evaluators, one or more renderers -- and MIAP makes the same split between
// its panner and its UI object, sharing map data the way groove~ shares a
// buffer~.

#include "../core/ofxManifold2D.h"
#include "../core/ofxManifoldEvaluator.h"

#include "ofMain.h"

class ofxManifoldRenderer {
public:
    // Colours and toggles. Every one is a presentation decision and none of
    // them mean anything to the manifold.
    struct Style {
        ofColor background      = ofColor(18, 18, 22);
        ofColor regionFill      = ofColor(48, 52, 66);
        ofColor regionActive    = ofColor(70, 96, 130);
        ofColor edge            = ofColor(96, 104, 124);
        ofColor node            = ofColor(210, 214, 224);
        ofColor nodeLabel       = ofColor(235, 238, 245);
        ofColor point           = ofColor(255, 214, 90);
        ofColor weightBar       = ofColor(120, 200, 160);
        ofColor warning         = ofColor(232, 96, 88);
        ofColor frame           = ofColor(70, 74, 88);

        float   nodeRadius      = 6.0f;
        float   pointRadius     = 7.0f;
        float   edgeWidth       = 1.5f;

        bool    drawRegions     = true;
        bool    drawEdges       = true;
        bool    drawNodes       = true;
        bool    drawLabels      = true;
        bool    drawFrame       = true;

        // Normalized y increases UPWARD on screen.
        //
        // The manifold does not care -- affine invariance (architecture doc
        // 7.0) means the weights are identical either way. But the fixtures
        // name a node at y = 0.9 "N" for north, and an author who writes that
        // expects to see it at the top.
        bool    flipY           = true;
    };

    explicit ofxManifoldRenderer(const ofxManifold::Manifold2D& m)
        : manifold_(&m) {
        viewport_ = ofRectangle(0, 0, 512, 512);
    }

    // ---- viewport and transform -----------------------------------------

    // The unit square is mapped into the largest CENTRED SQUARE that fits the
    // viewport, aspect always preserved.
    //
    // Deliberately not a fit-to-bounding-box of the manifold's own nodes. A
    // transform that depended on the manifold's contents would place the same
    // normalized coordinate at a different screen position in two different
    // maps -- and section 11 requires that a trajectory authored against one
    // map replays against another. Fit-to-contents would silently break that,
    // and it would break it in a way that looks fine until the second map.
    void setViewport(const ofRectangle& r) { viewport_ = r; }
    const ofRectangle& viewport() const { return viewport_; }

    ofRectangle square() const {
        const float s = std::min(viewport_.getWidth(), viewport_.getHeight());
        return ofRectangle(viewport_.getX() + (viewport_.getWidth()  - s) * 0.5f,
                           viewport_.getY() + (viewport_.getHeight() - s) * 0.5f,
                           s, s);
    }

    glm::vec2 toScreen(const glm::vec2& n) const {
        const ofRectangle sq = square();
        const float y = style.flipY ? (1.0f - n.y) : n.y;
        return glm::vec2(sq.getX() + n.x * sq.getWidth(),
                         sq.getY() + y   * sq.getHeight());
    }

    glm::vec2 toManifold(const glm::vec2& s) const {
        const ofRectangle sq = square();
        if (sq.getWidth() <= 0.0f || sq.getHeight() <= 0.0f) {
            return glm::vec2(0.0f);
        }
        const float x = (s.x - sq.getX()) / sq.getWidth();
        float       y = (s.y - sq.getY()) / sq.getHeight();
        if (style.flipY) y = 1.0f - y;
        return glm::vec2(x, y);
    }

    // ---- drawing ---------------------------------------------------------

    // The manifold itself. activeRegion highlights one region; pass
    // ofxManifold::InvalidRegion for none.
    void draw(ofxManifold::RegionID activeRegion =
                  ofxManifold::InvalidRegion) const;

    // The control point, plus a weight readout beside it.
    void drawEvaluation(const ofxManifold::Evaluation& e,
                        const glm::vec2& point) const;

    // Faults drawn where they are, rather than listed somewhere else. A
    // T-junction is a position on an edge, and reading a node name out of a
    // console tells you far less than seeing the spot.
    void drawTopology(const ofxManifold::TopologyReport& rep) const;

    // Weight readout at an arbitrary screen position, for layouts that want it
    // somewhere other than beside the point.
    void drawWeights(const ofxManifold::Evaluation& e,
                     const glm::vec2& at) const;

    Style style;

private:
    const ofxManifold::Manifold2D* manifold_;
    ofRectangle viewport_;
};
