#pragma once

// ofxManifold — example-blend.
//
// Two manifolds stacked in the same square, one point evaluated against both,
// one crossfade combining them.
//
// This is the historical answer to three dimensions (architecture doc 9.3).
// SpaceMap did not reach the third axis with tetrahedra; it ran several
// concurrent 2D maps and combined them downstream. The crossfade slider here
// IS that third axis: drag the point to move in x and y, drag the slider to
// move through a dimension neither map has.
//
// Watch the target list rather than the geometry. The point can sit still
// while every output changes, because what changed was which map is speaking.
//
// The two maps deliberately share some outputs and not others. Shared ones sum;
// unshared ones fade in and out. Both maps' nodes are numbered from zero --
// they always are, being per-manifold indices -- which is exactly why this
// crossfade happens by target NAME after each side resolves, and not at the
// node level. See DECISIONS.md D-013.

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
    void drawMap(const ofxManifold::Manifold2D& m,
                 const ofxManifold::Evaluation& e,
                 const ofColor& tint, float alpha) const;

    ofxManifold::Manifold2D                 mapA, mapB;
    ofxManifold::Mapping                    mapPA, mapPB;
    std::unique_ptr<ofxManifold::Evaluator> evalA, evalB;
    std::unique_ptr<ofxManifoldRenderer>    rendA, rendB;

    glm::vec2                 point{0.5f, 0.5f};
    ofxManifold::Evaluation   eA, eB;
    std::vector<ofxManifold::NamedWeight> mixed;

    float mix = 0.5f;                 // 0 = map A alone, 1 = map B alone
    bool  useEqualPower = false;
    bool  autoSweep = false;
};
