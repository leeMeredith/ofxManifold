#pragma once

// ofxManifold — example-mapping.
//
// The node taxonomy, made visible.
//
// The kernel has NO node type field. Terminal, null and composite nodes are
// the same struct, and they differ only in what the mapping layer resolves
// them to: zero targets, one target, or many. Manifold2D cannot tell them
// apart and does not need to.
//
// That is easy to assert and hard to believe until you watch it. This example
// shows all three in one map, plus a target fed by two different nodes, plus
// an aggregator that runs the routing backwards.
//
// The historical note is worth keeping in view: every one of these exists
// because a production broke without it. Silent nodes were added because sound
// dragged off the map edge cut out abruptly. Virtual nodes were added when it
// became clear not every rig has an overhead speaker.

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
    void drawTargets(float x, float y);

    ofxManifold::Manifold2D                 manifold;
    ofxManifold::Mapping                    mapping;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    glm::vec2                  point{0.5f, 0.5f};
    ofxManifold::Evaluation    evaluation;
    std::vector<float>         dense;
    ofxManifold::Resolved      resolved;
    bool                       showRing = true;
};
