#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — smoothing");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    rawHistory.assign(kHistory, 0.0f);
    smoothHistory.assign(kHistory, 0.0f);
    buildFan();
}

void ofApp::adopt() {
    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    smoother.reset();
    rawHistory.assign(kHistory, 0.0f);
    smoothHistory.assign(kHistory, 0.0f);
}

void ofApp::buildFan() {
    manifold = Manifold2D();
    const auto O = manifold.addNode("O", {0.50f, 0.50f});
    manifold.addNode("N", {0.50f, 0.90f});
    manifold.addNode("E", {0.90f, 0.50f});
    manifold.addNode("S", {0.50f, 0.10f});
    manifold.addNode("W", {0.10f, 0.50f});
    manifold.addTriangle(O, 1, 2);
    manifold.addTriangle(O, 2, 3);
    manifold.addTriangle(O, 3, 4);
    manifold.addTriangle(O, 4, 1);
    // O, the centre, is a vertex of ALL FOUR triangles, so its weight is
    // non-zero everywhere inside the fan. N sits at the top and belongs to
    // only two of them, so tracing N flat-lines across the bottom half of the
    // map -- truthfully, but uselessly. A trace is only worth watching if the
    // node it follows is present where you are looking.
    traced = 0;                 // node O
    fixtureName = "1  fan (conforming)";
    adopt();
}

void ofApp::buildTJunction() {
    manifold = Manifold2D();
    manifold.addNode("A", {0.10f, 0.15f});
    manifold.addNode("B", {0.90f, 0.15f});
    manifold.addNode("C", {0.50f, 0.85f});
    manifold.addNode("M", {0.50f, 0.15f});
    manifold.addNode("D", {0.50f, 0.04f});
    manifold.addTriangle(0, 1, 2);
    manifold.addTriangle(0, 4, 3);
    // B here, deliberately. B belongs to the large triangle and not the
    // small one, so it DOES drop to zero as the point crosses -- and that
    // drop is the fault this fixture exists to show.
    traced = 1;                 // node B, the one that steps
    fixtureName = "2  T-junction (broken on purpose)";
    adopt();
}

float ofApp::weightOf(const WeightVector& v, NodeID id) const {
    for (const auto& w : v) if (w.id == id) return w.weight;
    return 0.0f;
}

void ofApp::update() {
    const float side = ofGetHeight() - 260.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));

    // Jitter stands in for a real source. A tracker, a network message, a
    // fader read at frame rate -- all of them give you a point that trembles,
    // and the manifold faithfully reports the trembling because that is what
    // it was handed.
    jittered = point;
    if (jitterAmount > 0.0f) {
        jittered += glm::vec2(ofRandom(-jitterAmount, jitterAmount),
                              ofRandom(-jitterAmount, jitterAmount));
    }

    evaluation = evaluator->evaluate(jittered);

    smoother.setHalfLife(smoothingOn ? halfLife : 0.0f);
    smoother.setMaxRate(smoothingOn ? slewRate : 0.0f);
    smoothed = smoother.update(evaluation.weights, 1.0f / 60.0f);

    rawHistory.erase(rawHistory.begin());
    rawHistory.push_back(weightOf(evaluation.weights, traced));
    smoothHistory.erase(smoothHistory.begin());
    smoothHistory.push_back(weightOf(smoothed, traced));
}

void ofApp::drawTrace(float x, float y, float w, float h) const {
    ofPushStyle();

    ofSetColor(34, 36, 44);
    ofFill();
    ofDrawRectangle(x, y, w, h);
    ofSetColor(60, 64, 76);
    ofNoFill();
    ofDrawRectangle(x, y, w, h);
    ofSetColor(52, 56, 68);
    ofDrawLine(x, y + h * 0.5f, x + w, y + h * 0.5f);

    const float step = w / static_cast<float>(kHistory - 1);

    // Raw first, so the smoothed line sits on top of it.
    ofNoFill();
    ofSetColor(232, 140, 96, 200);
    ofSetLineWidth(1.0f);
    ofBeginShape();
    for (int i = 0; i < kHistory; ++i) {
        ofVertex(x + i * step,
                 y + h - h * ofClamp(rawHistory[i], 0.0f, 1.0f));
    }
    ofEndShape(false);

    ofSetColor(120, 200, 160);
    ofSetLineWidth(2.0f);
    ofBeginShape();
    for (int i = 0; i < kHistory; ++i) {
        ofVertex(x + i * step,
                 y + h - h * ofClamp(smoothHistory[i], 0.0f, 1.0f));
    }
    ofEndShape(false);

    ofSetColor(232, 140, 96);
    ofDrawBitmapString("raw", x + 6.0f, y + 14.0f);
    ofSetColor(120, 200, 160);
    ofDrawBitmapString("smoothed", x + 44.0f, y + 14.0f);
    const bool present = weightOf(rawHistory.empty() ? WeightVector{}
                                                     : evaluation.weights,
                                  traced) != 0.0f
                      || !evaluation.inside;
    ofSetColor(present ? ofColor(120, 126, 140) : ofColor(255, 214, 90));
    ofDrawBitmapString("weight of " + manifold.node(traced).name
                       + (present ? "" : "  (not in this region -- press n)"),
                       x + w - 300.0f, y + 14.0f);
    ofPopStyle();
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);
    renderer->drawEvaluation(evaluation, jittered);

    const ofRectangle vp = renderer->viewport();
    drawTrace(24, vp.getBottom() + 24.0f, ofGetWidth() - 48.0f, 170.0f);

    const float x = vp.getRight() + 40.0f;
    float y = 48.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString(fixtureName, x, y);                        y += 28.0f;

    ofSetColor(smoothingOn ? ofColor(120, 200, 160) : ofColor(140, 146, 160));
    ofDrawBitmapString(std::string("smoothing  ")
                       + (smoothingOn ? "on" : "off"), x, y);     y += 18.0f;
    ofSetColor(180, 186, 200);
    ofDrawBitmapString("half-life  " + ofToString(halfLife, 3) + " s", x, y);
    y += 16.0f;
    ofDrawBitmapString("slew       "
                       + (slewRate > 0.0f ? ofToString(slewRate, 2) + " /s"
                                          : std::string("off")), x, y);
    y += 16.0f;
    ofSetColor(jitterAmount > 0.0f ? ofColor(232, 140, 96)
                                   : ofColor(140, 146, 160));
    ofDrawBitmapString("jitter     "
                       + (jitterAmount > 0.0f
                          ? ofToString(jitterAmount, 3)
                          : std::string("off")), x, y);
    y += 28.0f;

    // Both sums, side by side. Exponential smoothing preserves partition of
    // unity exactly, even mid-transition between disjoint node sets. A slew
    // limit does not, and the dip is visible here rather than hidden.
    ofSetColor(235, 238, 245);
    ofDrawBitmapString("raw sum       " + ofToString(sum(evaluation.weights), 4),
                       x, y);
    y += 16.0f;
    const float ssum = sum(smoothed);
    ofSetColor(std::fabs(ssum - 1.0f) < 0.002f ? ofColor(120, 200, 160)
                                               : ofColor(255, 214, 90));
    ofDrawBitmapString("smoothed sum  " + ofToString(ssum, 4), x, y);
    y += 16.0f;
    ofSetColor(140, 146, 160);
    ofDrawBitmapString("entries       " + ofToString(smoothed.size())
                       + "  (raw " + ofToString(evaluation.weights.size())
                       + ")", x, y);
    y += 28.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("smoothed weights", x, y);                 y += 20.0f;
    for (const auto& wn : smoothed) {
        ofSetColor(180, 186, 200);
        ofDrawBitmapString("  " + manifold.node(wn.id).name + "  "
                           + ofToString(wn.weight, 3), x, y);
        y += 15.0f;
    }
    y += 24.0f;

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag        move the point", x, y);       y += 16.0f;
    ofDrawBitmapString("j           jitter on / off", x, y);      y += 16.0f;
    ofDrawBitmapString("space       jump (a fired cue)", x, y);   y += 16.0f;
    ofDrawBitmapString("s           smoothing on / off", x, y);   y += 16.0f;
    ofDrawBitmapString("[ ]         half-life", x, y);            y += 16.0f;
    ofDrawBitmapString(", .         slew rate", x, y);            y += 16.0f;
    ofDrawBitmapString("n           trace a different node", x, y); y += 16.0f;
    ofDrawBitmapString("1 2         fan / T-junction", x, y);     y += 32.0f;

    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Cross an interior edge in fixture 1.", x, y);
    y += 15.0f;
    ofDrawBitmapString("The RAW line does not jump: on a", x, y); y += 15.0f;
    ofDrawBitmapString("conforming mesh the weights are", x, y);  y += 15.0f;
    ofDrawBitmapString("already continuous there.", x, y);        y += 24.0f;
    ofSetColor(232, 140, 96);
    ofDrawBitmapString("Now press j, or space, or drag", x, y);   y += 15.0f;
    ofDrawBitmapString("outside, or press 2. Those are the", x, y);
    y += 15.0f;
    ofDrawBitmapString("discontinuities smoothing is for.", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == 'j') jitterAmount = (jitterAmount > 0.0f) ? 0.0f : 0.02f;
    if (key == 's') smoothingOn = !smoothingOn;
    if (key == '[') halfLife = std::max(0.005f, halfLife * 0.75f);
    if (key == ']') halfLife = std::min(1.000f, halfLife * 1.33f);
    if (key == ',') slewRate = std::max(0.0f, slewRate - 0.5f);
    if (key == '.') slewRate = std::min(20.0f, slewRate + 0.5f);
    // Cycle which node the trace follows. A node absent from the region you
    // are in reads zero, which is correct and uninformative.
    if (key == 'n') {
        traced = static_cast<NodeID>((traced + 1) % manifold.nodeCount());
        rawHistory.assign(kHistory, 0.0f);
        smoothHistory.assign(kHistory, 0.0f);
    }
    if (key == '1') buildFan();
    if (key == '2') buildTJunction();

    // A jump, as a fired cue or a scrubbed trajectory gives you. The smoother
    // glides through it; snap() would not, which is the right choice when the
    // discontinuity is intended.
    if (key == ' ') {
        point = glm::vec2(ofRandom(0.15f, 0.85f), ofRandom(0.15f, 0.85f));
    }
}
