#pragma once

// ofxManifold — example-smoothing.
//
// Raw weights and smoothed weights, plotted against each other over time.
//
// The trace is the point of this example. Numbers tell you the current value;
// a trace tells you the SHAPE, and smoothing is entirely about shape.
//
// Four things to provoke, each a different kind of discontinuity:
//
//   j       jitter on the input, as a tracker or a network message gives you
//   space   a jump, as a fired cue or a scrubbed trajectory gives you
//   drag out  leaving the hull, where three weights become none in one frame
//   2       a T-junction fixture, where the weights genuinely step
//
// Note what is NOT in that list: an ordinary region crossing. On a conforming
// mesh the weights are already continuous there, so smoothing has nothing to
// fix. Watch the trace while crossing an interior edge in fixture 1 and the
// raw line does not jump at all. That was worth knowing before building any of
// this -- see DECISIONS.md D-016.

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
    void buildFan();
    void buildTJunction();
    void adopt();
    void drawTrace(float x, float y, float w, float h) const;
    float weightOf(const ofxManifold::WeightVector& v,
                   ofxManifold::NodeID id) const;

    ofxManifold::Manifold2D                 manifold;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;
    ofxManifold::WeightSmoother             smoother;

    glm::vec2                 point{0.5f, 0.55f};
    glm::vec2                 jittered{0.5f, 0.55f};
    ofxManifold::Evaluation   evaluation;
    ofxManifold::WeightVector smoothed;

    // The node whose weight is traced. Any one will do; the shape is the
    // same and one line is readable where four are not.
    ofxManifold::NodeID traced = 0;

    std::vector<float> rawHistory, smoothHistory;
    static const int   kHistory = 320;

    float jitterAmount = 0.0f;       // 0 = clean input
    float halfLife     = 0.08f;
    float slewRate     = 0.0f;       // 0 = off
    bool  smoothingOn  = true;
    std::string fixtureName;
};
