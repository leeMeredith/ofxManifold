#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — basic");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    buildFan();
}

void ofApp::buildFan() {
    manifold = Manifold2D();
    const auto O = manifold.addNode("O", {0.50f, 0.50f});
    const auto N = manifold.addNode("N", {0.50f, 0.90f});
    const auto E = manifold.addNode("E", {0.90f, 0.50f});
    const auto S = manifold.addNode("S", {0.50f, 0.10f});
    const auto W = manifold.addNode("W", {0.10f, 0.50f});
    manifold.addTriangle(O, N, E);
    manifold.addTriangle(O, E, S);
    manifold.addTriangle(O, S, W);
    manifold.addTriangle(O, W, N);
    fixtureName = "1  fan (conforming)";
    guidance = {
        "every interior edge is shared whole,",
        "so weights vary continuously everywhere.",
        "sum stays 1.000 wherever you drag."};

    // The Evaluator is rebuilt with the manifold because it holds a region
    // hint, and a hint into a manifold that no longer exists is nonsense.
    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    topology  = manifold.validate();
}

void ofApp::buildTJunction() {
    // Deliberately broken, so the topology report has something to draw. M sits
    // on the interior of edge A-B without being one of that triangle's
    // vertices, so weights jump discontinuously as the point crosses there.
    manifold = Manifold2D();
    const auto A = manifold.addNode("A", {0.10f, 0.15f});
    const auto B = manifold.addNode("B", {0.90f, 0.15f});
    const auto C = manifold.addNode("C", {0.50f, 0.85f});
    const auto M = manifold.addNode("M", {0.50f, 0.15f});
    const auto D = manifold.addNode("D", {0.50f, 0.04f});
    manifold.addTriangle(A, B, C);
    manifold.addTriangle(A, D, M);
    fixtureName = "2  T-junction (broken on purpose)";
    guidance = {
        "M sits on edge A-B without being a",
        "vertex of ABC. Cross that edge slowly",
        "just left or right of M and watch B:",
        "it JUMPS from about 0.5 to 0.",
        "",
        "That discontinuity is the fault. It",
        "reads like an ordinary region change,",
        "which is exactly why it needs a",
        "detector rather than an eye."};

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    topology  = manifold.validate();
}

void ofApp::buildOverlap() {
    // Two triangles overlapping in a lens. The fan has no overlap, so it
    // cannot show hysteresis -- there is only ever one region containing a
    // point. Here two regions contain the middle, and which one you get
    // depends on where you came FROM.
    manifold = Manifold2D();
    const auto A = manifold.addNode("A", {0.08f, 0.20f});
    const auto B = manifold.addNode("B", {0.62f, 0.20f});
    const auto C = manifold.addNode("C", {0.35f, 0.80f});
    const auto D = manifold.addNode("D", {0.38f, 0.20f});
    const auto E = manifold.addNode("E", {0.92f, 0.20f});
    const auto F = manifold.addNode("F", {0.65f, 0.80f});
    manifold.addTriangle(A, B, C);
    manifold.addTriangle(D, E, F);
    fixtureName = "3  overlapping regions";
    guidance = {
        "The two triangles overlap in the middle.",
        "",
        "Enter the overlap from the LEFT and the",
        "region stays 0. Enter from the RIGHT and",
        "it stays 1. Same point, different answer,",
        "depending on history.",
        "",
        "That is the Evaluator's hint doing its",
        "job: a source stays in the region it was",
        "already in rather than popping to",
        "whichever comes first in insertion order."};

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    topology  = manifold.validate();
}

ofxManifold::NodeID ofApp::nodeAt(const glm::vec2& screen) const {
    const float grab = 12.0f;
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        const NodeID id = static_cast<NodeID>(i);
        const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
        if (glm::distance(p, screen) <= grab) return id;
    }
    return InvalidNode;
}

void ofApp::update() {
    renderer->setViewport(ofRectangle(24, 24, ofGetHeight() - 48,
                                      ofGetHeight() - 48));
    evaluation = evaluator->evaluate(point);
    if (refusedFlash > 0.0f) refusedFlash -= 1.0f / 60.0f;
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);
    renderer->drawEvaluation(evaluation, point);
    if (showTopology) renderer->drawTopology(topology);

    const float x = renderer->viewport().getRight() + 40.0f;
    float y = 48.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString(fixtureName, x, y);                    y += 28.0f;
    ofDrawBitmapString("region  " + (evaluation.inside
        ? ofToString(evaluation.regionID) : std::string("none")), x, y);
    y += 16.0f;
    ofDrawBitmapString("hint    " + std::string(
        evaluator->hint() == InvalidRegion ? "cleared"
                                           : ofToString(evaluator->hint())),
        x, y);
    y += 28.0f;

    // Partition of unity, on screen. If this ever reads anything but 1.000
    // inside the hull, something is wrong that no amount of looking at the
    // picture would reveal.
    float total = 0.0f;
    for (const auto& wn : evaluation.weights) total += wn.weight;
    ofDrawBitmapString("sum     " + ofToString(total, 4), x, y);
    y += 28.0f;

    if (!topology.clean()) {
        ofSetColor(232, 96, 88);
        ofDrawBitmapString("topology faults:", x, y);            y += 16.0f;
        ofDrawBitmapString("  T-junctions " +
            ofToString(topology.tJunctions.size()), x, y);       y += 16.0f;
        ofDrawBitmapString("  orphans     " +
            ofToString(topology.orphans.size()), x, y);          y += 16.0f;
        ofDrawBitmapString("  duplicates  " +
            ofToString(topology.duplicates.size()), x, y);       y += 28.0f;
    } else {
        ofSetColor(120, 200, 160);
        ofDrawBitmapString("topology clean", x, y);              y += 28.0f;
    }

    // Node drag feedback, drawn where the node is.
    if (dragging != InvalidNode || hovered != InvalidNode) {
        const NodeID id = (dragging != InvalidNode) ? dragging : hovered;
        const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
        ofPushStyle();
        ofNoFill();
        ofSetLineWidth(2.0f);
        ofSetColor(refusedFlash > 0.0f ? ofColor(232, 96, 88)
                                       : ofColor(255, 214, 90));
        ofDrawCircle(p.x, p.y, 14.0f);
        if (refusedFlash > 0.0f) {
            ofDrawBitmapString("move refused: would invert a region",
                               p.x + 18.0f, p.y + 4.0f);
        }
        ofPopStyle();
    }

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag point  move the control point", x, y); y += 16.0f;
    ofDrawBitmapString("drag node   move a node", x, y);         y += 16.0f;
    ofDrawBitmapString("1           fan", x, y);                 y += 16.0f;
    ofDrawBitmapString("2           T-junction", x, y);          y += 16.0f;
    ofDrawBitmapString("3           overlap / hysteresis", x, y); y += 16.0f;
    ofDrawBitmapString("t           toggle warnings", x, y);     y += 16.0f;
    ofDrawBitmapString("s           save manifold.json", x, y);
    y += 32.0f;

    // What to look for in THIS fixture. A demonstration nobody knows how to
    // read demonstrates nothing.
    ofSetColor(150, 190, 220);
    for (const auto& line : guidance) {
        ofDrawBitmapString(line, x, y);
        y += 15.0f;
    }
}

void ofApp::mousePressed(int x, int y, int) {
    dragging = nodeAt(glm::vec2(x, y));
    if (dragging == InvalidNode) {
        point = renderer->toManifold(glm::vec2(x, y));
    }
}

void ofApp::mouseReleased(int, int, int) {
    dragging = InvalidNode;
    lastMoveRefused = false;
}

void ofApp::mouseMoved(int x, int y) {
    hovered = nodeAt(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    const glm::vec2 target = renderer->toManifold(glm::vec2(x, y));

    if (dragging != InvalidNode) {
        // THE GUARANTEE, made visible.
        //
        // Topology is discrete and edited; positions are continuous and
        // animated. Weights vary continuously as a node moves -- with exactly
        // one failure mode, which is a node travelling far enough to turn a
        // region inside out. The signed area passes through zero on the way
        // and the weights diverge.
        //
        // setNodePosition() compares against the winding sign recorded at
        // construction and REFUSES. Drag a vertex across the opposite edge and
        // the node stops dead at the fold. Nothing in the test suite can show
        // that: a vector reports false, which is not the same as watching it
        // happen.
        lastMoveRefused = !manifold.setNodePosition(dragging, target);
        if (lastMoveRefused) refusedFlash = 0.35f;

        // The manifold changed shape, so the Evaluator's cached region hint
        // may name a region the point is no longer in. Topology did not
        // change, so the hint is still VALID -- but clearing it is honest and
        // costs one linear scan on the next frame.
        evaluator->reset();
        topology = manifold.validate();
        return;
    }

    point = target;
}

void ofApp::keyPressed(int key) {
    if (key == '1') buildFan();
    if (key == '2') buildTJunction();
    if (key == '3') buildOverlap();
    if (key == 't') showTopology = !showTopology;
    if (key == 's') {
        ofBuffer buf;
        buf.set(io::saveManifold(manifold));
        ofBufferToFile("manifold.json", buf);
        ofLogNotice() << "wrote " << ofToDataPath("manifold.json");
    }
}
