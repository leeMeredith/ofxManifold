#pragma once

// ofxManifold — example-regions.
//
// Regions of three, six and ten nodes in one map, convex and not.
//
// A region is an ordered ring of N >= 3 nodes. Three take the barycentric
// solve; more take mean-value coordinates, after Hormann & Floater (2006). The
// two agree exactly at three nodes, which is what makes the larger regions an
// extension rather than a replacement.
//
// Four shapes, one per quadrant, chosen to show four different things:
//
//   triangle   the ordinary case, three weights
//   hexagon    convex, six weights, never negative
//   L-shape    non-convex, one reflex vertex -- weights go negative over much
//              of it, down to about -0.013
//   star       non-convex, ten nodes, deep notches -- negative too, but only
//              down to about -0.0004
//
// Non-convex regions are ACCEPTED (DECISIONS.md D-016b (D-C)). A negative weight is
// a relationship, drawn as a warning-coloured line pulling backwards from the
// point. Whether it is acceptable is the consumer's call: press c to apply
// clampNegative(), which removes negatives and renormalizes -- right for
// speaker gains, wrong for parameter positions, and never automatic.

#include "ofMain.h"
#include "ofxManifold.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mouseDragged(int x, int y, int button) override;
    void mousePressed(int x, int y, int button) override;
    void keyPressed(int key) override;

private:
    void build();
    ofxManifold::RegionID addShape(const std::string& prefix,
                                   const std::vector<glm::vec2>& ring);

    ofxManifold::Manifold2D                 manifold;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    glm::vec2                  point{0.30f, 0.30f};
    ofxManifold::Evaluation    raw;
    ofxManifold::WeightVector  shown;
    ofxManifold::TopologyReport topology;
    std::vector<std::string>   regionNames;
    bool clampOn = false;
};
