#pragma once

// ofxManifold — example-parameter-morphing.
//
// The example that explains what the addon is for.
//
// Four presets sit at four nodes. Each preset is an ordinary struct: a colour,
// a size, a rotation speed, a count. Dragging the point produces a continuous
// blend of all four, and every field interpolates independently without any of
// them knowing the others exist.
//
// The manifold never learns what a Preset is. It answers "which nodes, and by
// how much"; interpolate() does the rest. That separation is the whole design,
// and this file is the shortest demonstration of it.

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
    // Anything with `operator+` and `operator*(float)` can be interpolated.
    // Nothing here is special to ofxManifold.
    struct Preset {
        glm::vec3 colour{255.0f, 255.0f, 255.0f};
        float     size      = 40.0f;
        float     spin      = 0.0f;
        float     count     = 3.0f;

        Preset operator+(const Preset& o) const {
            return Preset{colour + o.colour, size + o.size,
                          spin + o.spin, count + o.count};
        }
        Preset operator*(float k) const {
            return Preset{colour * k, size * k, spin * k, count * k};
        }
    };

    void buildPresets();
    void drawShape(const Preset& p, const glm::vec2& at) const;

    ofxManifold::Manifold2D                 manifold;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    std::unique_ptr<ofxManifoldRenderer>    renderer;

    std::vector<Preset>       presets;    // indexed by NodeID
    glm::vec2                 point{0.5f, 0.5f};
    ofxManifold::Evaluation   evaluation;
    Preset                    blended;
    float                     phase = 0.0f;
    bool                      useCurve = false;
};
