#pragma once

// ofxManifold — example-basic.
//
// Drag the point. Watch the weights. That is the whole addon in one screen.
//
// Note what this example does NOT contain: any barycentric arithmetic, any
// containment test, any transform maths. The manifold answers where the point
// is, the renderer draws it, and this file only decides what a mouse drag
// means.

#include "ofMain.h"
#include "ofxManifold.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mouseDragged(int x, int y, int button) override;
    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void keyPressed(int key) override;

private:
    void buildFan();
    void buildTJunction();
    void buildOverlap();

    // Which node is under the cursor, or InvalidNode. Hit testing happens in
    // SCREEN space: a node's grab radius should be the same number of pixels
    // whatever the window size, which is not what a fixed normalized radius
    // would give.
    ofxManifold::NodeID nodeAt(const glm::vec2& screen) const;

    ofxManifold::Manifold2D           manifold;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    glm::vec2                     point{0.5f, 0.5f};
    ofxManifold::Evaluation       evaluation;
    ofxManifold::TopologyReport   topology;
    bool                          showTopology = true;

    // Node dragging. The refusal is the point: setNodePosition() returns false
    // when a move would invert or flatten a region, and the node stops rather
    // than passing through the fold.
    ofxManifold::NodeID           dragging = ofxManifold::InvalidNode;
    ofxManifold::NodeID           hovered  = ofxManifold::InvalidNode;
    bool                          lastMoveRefused = false;
    float                         refusedFlash = 0.0f;
    std::string                   fixtureName;
    std::vector<std::string>      guidance;
};
