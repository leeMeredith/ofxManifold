#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — parameter morphing");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    buildPresets();
}

void ofApp::buildPresets() {
    manifold = Manifold2D();

    // Four corners and a centre. The centre node holds a fifth preset, so the
    // middle of the space is a state in its own right rather than an average
    // of the corners.
    const auto NW = manifold.addNode("calm",   {0.15f, 0.85f});
    const auto NE = manifold.addNode("bright", {0.85f, 0.85f});
    const auto SE = manifold.addNode("fast",   {0.85f, 0.15f});
    const auto SW = manifold.addNode("heavy",  {0.15f, 0.15f});
    const auto C  = manifold.addNode("centre", {0.50f, 0.50f});

    manifold.addTriangle(C, NW, NE);
    manifold.addTriangle(C, NE, SE);
    manifold.addTriangle(C, SE, SW);
    manifold.addTriangle(C, SW, NW);

    // Indexed by NodeID, which is why addNode order and this order must agree.
    presets.assign(manifold.nodeCount(), Preset{});
    presets[NW] = Preset{{ 90.0f, 160.0f, 220.0f},  70.0f,  0.15f,  3.0f};
    presets[NE] = Preset{{255.0f, 232.0f, 120.0f},  45.0f,  0.60f, 12.0f};
    presets[SE] = Preset{{240.0f, 110.0f,  90.0f},  30.0f,  3.20f,  6.0f};
    presets[SW] = Preset{{140.0f, 100.0f, 200.0f}, 120.0f,  0.05f,  4.0f};
    presets[C]  = Preset{{200.0f, 205.0f, 215.0f},  60.0f,  0.80f,  8.0f};

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));

    evaluation = evaluator->evaluate(point);

    // THE LINE THE ADDON EXISTS FOR.
    //
    // One call turns a position into a blended Preset. Every field interpolates
    // independently, and the manifold has no idea what a Preset is.
    WeightVector w = evaluation.weights;

    // Optional, and shown because it is a real trap. equalPower turns weights
    // into gains whose SQUARES sum to one, so the plain sum rises above one and
    // an interpolated result comes out scaled up. Correct for amplitudes, wrong
    // for parameter positions -- a 50/50 blend of two sizes should be their
    // average, not 1.41 times it. normalize() puts it back.
    if (useCurve) {
        w = normalize(curve::apply(w, curve::equalPower));
    }

    blended = interpolate(w, presets);
    phase += blended.spin * 0.02f;
}

void ofApp::drawShape(const Preset& p, const glm::vec2& at) const {
    const int n = std::max(3, static_cast<int>(std::round(p.count)));
    ofPushMatrix();
    ofTranslate(at.x, at.y);
    ofRotateRad(phase);
    ofSetColor(p.colour.x, p.colour.y, p.colour.z);
    ofNoFill();
    ofSetLineWidth(2.5f);
    ofBeginShape();
    for (int i = 0; i < n; ++i) {
        const float a = TWO_PI * static_cast<float>(i) / static_cast<float>(n);
        ofVertex(std::cos(a) * p.size, std::sin(a) * p.size);
    }
    ofEndShape(true);
    ofPopMatrix();
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);
    renderer->drawEvaluation(evaluation, point);

    const float x = renderer->viewport().getRight() + 60.0f;
    const float panelCentreX = x + 130.0f;

    if (evaluation.inside) {
        drawShape(blended, glm::vec2(panelCentreX, 180.0f));
    }

    float y = 320.0f;
    ofSetColor(235, 238, 245);
    ofDrawBitmapString("blended preset", x, y);                  y += 24.0f;

    ofSetColor(180, 186, 200);
    ofDrawBitmapString("colour  " + ofToString(blended.colour.x, 0) + ", "
                                  + ofToString(blended.colour.y, 0) + ", "
                                  + ofToString(blended.colour.z, 0), x, y);
    y += 16.0f;
    ofDrawBitmapString("size    " + ofToString(blended.size, 1), x, y);
    y += 16.0f;
    ofDrawBitmapString("spin    " + ofToString(blended.spin, 2), x, y);
    y += 16.0f;
    ofDrawBitmapString("sides   " + ofToString(blended.count, 1), x, y);
    y += 28.0f;

    // coverage() is one minus the shortfall. Inside the hull with no null
    // nodes it reads 1.000; outside, it reads 0 and the shape disappears
    // rather than freezing on its last value.
    ofSetColor(120, 200, 160);
    ofDrawBitmapString("coverage " +
        ofToString(coverage(evaluation.weights, presets.size()), 3), x, y);
    y += 28.0f;

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag        morph between presets", x, y); y += 16.0f;
    ofDrawBitmapString(std::string("c           equal-power curve: ")
                       + (useCurve ? "on" : "off"), x, y);         y += 16.0f;
    ofDrawBitmapString("            (normalized after; try it", x, y);
    y += 16.0f;
    ofDrawBitmapString("             without to see the trap)", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == 'c') useCurve = !useCurve;
}
