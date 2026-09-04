#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — spread");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    build();
}

void ofApp::build() {
    manifold = Manifold2D();

    // Four bound nodes and a centre.
    const auto N = manifold.addNode("N", {0.50f, 0.78f});
    const auto E = manifold.addNode("E", {0.78f, 0.50f});
    const auto S = manifold.addNode("S", {0.50f, 0.22f});
    const auto W = manifold.addNode("W", {0.22f, 0.50f});
    const auto O = manifold.addNode("O", {0.50f, 0.50f});

    // A ring of null nodes. They take weight and give nothing back, which is
    // what makes the second half of this example visible.
    const auto n0 = manifold.addNode("null.NW", {0.08f, 0.92f});
    const auto n1 = manifold.addNode("null.NE", {0.92f, 0.92f});
    const auto n2 = manifold.addNode("null.SE", {0.92f, 0.08f});
    const auto n3 = manifold.addNode("null.SW", {0.08f, 0.08f});

    manifold.addTriangle(O, N, E);
    manifold.addTriangle(O, E, S);
    manifold.addTriangle(O, S, W);
    manifold.addTriangle(O, W, N);

    manifold.addTriangle(N, n0, n1);
    manifold.addTriangle(N, n1, E);
    manifold.addTriangle(E, n1, n2);
    manifold.addTriangle(E, n2, S);
    manifold.addTriangle(S, n2, n3);
    manifold.addTriangle(S, n3, W);
    manifold.addTriangle(W, n3, n0);
    manifold.addTriangle(W, n0, N);

    mapping = Mapping();
    mapping.bind(N, mapping.addTarget("out.N"));
    mapping.bind(E, mapping.addTarget("out.E"));
    mapping.bind(S, mapping.addTarget("out.S"));
    mapping.bind(W, mapping.addTarget("out.W"));
    // The centre is composite: it feeds all four.
    mapping.bind(O, mapping.addTarget("out.N"));
    mapping.bind(O, mapping.addTarget("out.E"));
    mapping.bind(O, mapping.addTarget("out.S"));
    mapping.bind(O, mapping.addTarget("out.W"));
    // null.* deliberately bound to nothing.

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    renderer->style.drawRegions = false;
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));

    if (autoSweep) {
        amount = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * 0.5f);
    }

    evaluation = evaluator->evaluate(point);

    // THE OPERATION. One call, one scalar. Note it takes a node COUNT rather
    // than the manifold: this layer operates on weight vectors and knows
    // nothing about where nodes are.
    spreadWeights = spread(evaluation.weights, amount, manifold.nodeCount());
    dense = mapping.toDenseVector(spreadWeights);
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);

    // Every node's share, drawn as brightness and size. At spread 0 only three
    // nodes are lit; by spread 1 the whole map glows evenly. This is the
    // picture the slider exists to make.
    ofPushStyle();
    for (const auto& wn : spreadWeights) {
        const Node& n = manifold.node(wn.id);
        const glm::vec2 p = renderer->toScreen(n.position);
        const float w = ofClamp(wn.weight, 0.0f, 1.0f);
        const bool isNull = (mapping.linkCount(wn.id) == 0);

        // Null nodes in a different colour, because what happens to their
        // share is the whole second half of the demonstration.
        ofSetColor(isNull ? ofColor(232, 140, 96) : ofColor(120, 200, 160),
                   40 + 215 * std::sqrt(w));
        ofFill();
        ofDrawCircle(p.x, p.y, 4.0f + 26.0f * std::sqrt(w));
    }
    ofPopStyle();

    renderer->drawEvaluation(evaluation, point);

    const float x = renderer->viewport().getRight() + 40.0f;
    float y = 48.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("spread", x, y);                           y += 18.0f;
    ofSetColor(70, 74, 88);
    ofDrawRectangle(x, y - 8.0f, 220.0f, 6.0f);
    ofSetColor(255, 214, 90);
    ofDrawRectangle(x + 220.0f * amount - 2.0f, y - 11.0f, 4.0f, 12.0f);
    y += 16.0f;
    ofSetColor(140, 146, 160);
    ofDrawBitmapString("pinpoint", x, y);
    ofDrawBitmapString("wash", x + 186.0f, y);
    ofSetColor(180, 186, 200);
    ofDrawBitmapString(ofToString(amount, 2), x + 100.0f, y);     y += 30.0f;

    // Two totals, side by side. They agree at spread 0 and diverge from there.
    float nodeSum = 0.0f;
    for (const auto& wn : spreadWeights) nodeSum += wn.weight;
    float targetSum = 0.0f;
    for (float v : dense) targetSum += v;

    ofSetColor(120, 200, 160);
    ofDrawBitmapString("node weights sum   " + ofToString(nodeSum, 3), x, y);
    y += 16.0f;
    ofSetColor(targetSum < 0.995f ? ofColor(232, 140, 96)
                                  : ofColor(120, 200, 160));
    ofDrawBitmapString("target total       " + ofToString(targetSum, 3), x, y);
    y += 16.0f;
    ofSetColor(140, 146, 160);
    ofDrawBitmapString("discarded          "
                       + ofToString(1.0f - targetSum, 3), x, y);
    y += 28.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("targets", x, y);                          y += 20.0f;
    for (std::size_t i = 0; i < dense.size(); ++i) {
        const TargetID id = static_cast<TargetID>(i);
        ofSetColor(120, 200, 160, 80 + 175 * ofClamp(dense[i], 0.f, 1.f));
        ofDrawRectangle(x, y - 9.0f, 150.0f * ofClamp(dense[i], 0.f, 1.f),
                        11.0f);
        ofSetColor(235, 238, 245);
        ofDrawBitmapString(mapping.targetName(id) + "   "
                           + ofToString(dense[i], 3), x + 4.0f, y);
        y += 18.0f;
    }
    y += 20.0f;

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag       move the point", x, y);        y += 16.0f;
    ofDrawBitmapString("< >        spread", x, y);                y += 16.0f;
    ofDrawBitmapString("a          auto sweep", x, y);            y += 32.0f;

    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Hold the point still and sweep.", x, y);  y += 24.0f;
    ofDrawBitmapString("Node weights stay at 1.000 the whole", x, y);
    y += 15.0f;
    ofDrawBitmapString("way: spread is a lerp between two", x, y);
    y += 15.0f;
    ofDrawBitmapString("vectors that each sum to one.", x, y);    y += 24.0f;
    ofSetColor(232, 140, 96);
    ofDrawBitmapString("But the target total FALLS, because", x, y);
    y += 15.0f;
    ofDrawBitmapString("spread hands weight to the orange", x, y);
    y += 15.0f;
    ofDrawBitmapString("null nodes too, and they discard it.", x, y);
    y += 24.0f;
    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Nothing was told to fade. It is what", x, y);
    y += 15.0f;
    ofDrawBitmapString("two correct layers do when composed.", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == ',' || key == '<') { amount = std::max(0.0f, amount - 0.02f); autoSweep = false; }
    if (key == '.' || key == '>') { amount = std::min(1.0f, amount + 0.02f); autoSweep = false; }
    if (key == 'a') autoSweep = !autoSweep;
    if (key == 'n') showNullRing = !showNullRing;
}
