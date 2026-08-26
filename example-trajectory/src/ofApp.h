#pragma once

// ofxManifold — example-trajectory.
//
// The touring case, as an event rather than a comparison.
//
// Record a path by dragging. Play it back. Then swap the venue underneath it
// WHILE IT IS PLAYING and watch the weights reorganize around a gesture that
// has not changed.
//
// That is what SpaceMap does on tour: the move renders in real time during the
// performance, so a production builds a new map for the new rig and replays its
// existing cues untouched. It is also the reason section 5 chose normalized
// coordinates over screen pixels. A path in pixels could not survive a change
// of window, let alone of room.
//
// The point worth watching: the yellow dot traces the identical shape in both
// venues. Only the numbers change.

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
    void keyPressed(int key) override;

private:
    void buildVenueA();
    void buildVenueB();
    void adoptManifold();

    ofxManifold::Manifold2D                 manifold;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    ofxManifold::Trajectory   path;
    ofxManifold::Evaluation   evaluation;
    glm::vec2                 point{0.5f, 0.5f};

    bool   recording = false;
    bool   playing   = false;
    float  recordStart = 0.0f;
    float  phase = 0.0f;          // normalized position along the path
    float  rate  = 0.25f;         // path lengths per second
    int    venue = 1;
    std::string venueName;
};
