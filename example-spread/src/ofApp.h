#pragma once

// ofxManifold — example-spread.
//
// One slider from pinpoint to wash.
//
// Spread interpolates between the evaluated weight vector and a uniform vector
// over every node in the map. At 0 the point is localized; at 1 it is
// everywhere. SpaceMap calls this divergence, and MIAP's barycentricpower is a
// cousin of it -- both are transformations of the INFLUENCE WEIGHTS, applied
// before anyone decides what the weights mean. Neither is an audio feature.
//
// Two things worth watching, and the second is the interesting one.
//
// 1. The weights always sum to 1, at every spread amount. Spread is a linear
//    interpolation between two vectors that each sum to one, so the result
//    does too.
//
// 2. The RESOLVED TARGETS do not. Spread hands part of the weight to every
//    node including the null ones ringing the map, and a null node discards
//    its share -- so spreading fades the output without anything being told
//    to fade. That is emergent behaviour of two correct layers composed, and
//    it reads as a bug in a rehearsal room unless someone wrote it down first.

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

    ofxManifold::Manifold2D                 manifold;
    ofxManifold::Mapping                    mapping;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    glm::vec2                 point{0.5f, 0.5f};
    ofxManifold::Evaluation   evaluation;
    ofxManifold::WeightVector spreadWeights;
    std::vector<float>        dense;

    float amount = 0.0f;      // 0 = localized, 1 = everywhere
    bool  autoSweep = false;
    bool  showNullRing = true;
};
