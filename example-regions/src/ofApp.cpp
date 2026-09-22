#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — regions");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    build();
}

// Place a unit-square shape into one quadrant of the map.
static std::vector<glm::vec2> place(const std::vector<glm::vec2>& unit,
                                    float cx, float cy, float s) {
    std::vector<glm::vec2> out;
    for (const glm::vec2& p : unit) {
        out.push_back({cx + (p.x - 0.5f) * s, cy + (p.y - 0.5f) * s});
    }
    return out;
}

RegionID ofApp::addShape(const std::string& prefix,
                         const std::vector<glm::vec2>& ring) {
    std::vector<NodeID> ids;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        ids.push_back(manifold.addNode(prefix + ofToString(i), ring[i]));
    }
    const RegionID r = manifold.addRegion(ids);
    if (r == InvalidRegion) {
        ofLogError() << prefix << " refused: " << manifold.lastRegionError();
    }
    return r;
}

void ofApp::build() {
    manifold = Manifold2D();
    regionNames.clear();

    const std::vector<glm::vec2> tri = {{0.20f, 0.20f}, {0.80f, 0.25f},
                                        {0.45f, 0.85f}};
    std::vector<glm::vec2> hexa;
    for (int k = 0; k < 6; ++k) {
        const float a = k * PI / 3.0f;
        hexa.push_back({0.5f + 0.42f * std::cos(a), 0.5f + 0.42f * std::sin(a)});
    }
    const std::vector<glm::vec2> ell = {{0.10f, 0.10f}, {0.90f, 0.10f},
                                        {0.90f, 0.40f}, {0.40f, 0.40f},
                                        {0.40f, 0.90f}, {0.10f, 0.90f}};
    std::vector<glm::vec2> star;
    for (int k = 0; k < 10; ++k) {
        const float a = HALF_PI + k * PI / 5.0f;
        const float r = (k % 2 == 0) ? 0.45f : 0.07f;
        star.push_back({0.5f + r * std::cos(a), 0.5f + r * std::sin(a)});
    }

    // Order matters only for which region a shared point would resolve to,
    // and these four do not overlap -- checked before this was written.
    addShape("t", place(tri,  0.25f, 0.75f, 0.42f));
    regionNames.push_back("triangle");
    addShape("h", place(hexa, 0.75f, 0.75f, 0.42f));
    regionNames.push_back("hexagon");
    addShape("L", place(ell,  0.25f, 0.25f, 0.42f));
    regionNames.push_back("L-shape");
    addShape("s", place(star, 0.75f, 0.25f, 0.46f));
    regionNames.push_back("star");

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
    renderer->style.drawLabels = false;     // 25 nodes; the shapes matter more
    topology  = manifold.validate();
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));

    raw   = evaluator->evaluate(point);
    shown = clampOn ? clampNegative(raw.weights) : raw.weights;
}

void ofApp::draw() {
    renderer->draw(raw.regionID);

    // Draw from the SHOWN weights, so pressing c visibly removes the
    // warning-coloured lines rather than only changing a number.
    Evaluation drawn = raw;
    drawn.weights = shown;
    renderer->drawEvaluation(drawn, point);

    const float x = renderer->viewport().getRight() + 40.0f;
    float y = 48.0f;

    ofSetColor(235, 238, 245);
    if (raw.inside && raw.regionID < regionNames.size()) {
        const Region& reg = manifold.region(raw.regionID);
        ofDrawBitmapString(regionNames[raw.regionID] + "   "
                           + ofToString(reg.size()) + " nodes", x, y);
        y += 16.0f;
        ofSetColor(140, 146, 160);
        ofDrawBitmapString(std::string(reg.size() == 3
                               ? "barycentric solve"
                               : "mean-value coordinates"), x, y);
        y += 16.0f;
        ofDrawBitmapString(std::string(reg.convex() ? "convex"
                                                    : "non-convex"), x, y);
    } else {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("outside every region", x, y);
    }
    y += 28.0f;

    // The two numbers a consumer deciding whether to care actually wants.
    const bool neg = anyNegative(raw.weights);
    ofSetColor(neg ? ofColor(232, 96, 88) : ofColor(120, 200, 160));
    ofDrawBitmapString(std::string("negative weights  ")
                       + (neg ? "yes" : "no"), x, y);
    y += 16.0f;
    ofSetColor(180, 186, 200);
    ofDrawBitmapString("most negative     "
                       + ofToString(mostNegative(raw.weights), 5), x, y);
    y += 16.0f;
    ofSetColor(clampOn ? ofColor(255, 214, 90) : ofColor(140, 146, 160));
    ofDrawBitmapString(std::string("clampNegative     ")
                       + (clampOn ? "ON" : "off"), x, y);
    y += 16.0f;
    ofSetColor(180, 186, 200);
    ofDrawBitmapString("sum               " + ofToString(sum(shown), 4), x, y);
    y += 28.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("weights", x, y);                          y += 18.0f;
    for (const auto& wn : shown) {
        ofSetColor(wn.weight < -1e-6f ? ofColor(232, 96, 88)
                                      : ofColor(180, 186, 200));
        ofDrawBitmapString("  " + manifold.node(wn.id).name + "  "
                           + ofToString(wn.weight, 4), x, y);
        y += 14.0f;
    }
    y += 20.0f;

    if (!topology.nonConvex.empty()) {
        ofSetColor(150, 190, 220);
        ofDrawBitmapString("non-convex regions: "
                           + ofToString(topology.nonConvex.size())
                           + "  (information,", x, y);
        y += 14.0f;
        ofDrawBitmapString("not a fault -- clean() "
                           + std::string(topology.clean() ? "true" : "false")
                           + ")", x, y);
        y += 26.0f;
    }

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag   move the point", x, y);            y += 16.0f;
    ofDrawBitmapString("c      clampNegative on / off", x, y);    y += 30.0f;

    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Drag into the L-shape near its inner", x, y);
    y += 15.0f;
    ofDrawBitmapString("corner. A red line pulls backwards", x, y);
    y += 15.0f;
    ofDrawBitmapString("toward a vertex you cannot see from", x, y);
    y += 15.0f;
    ofDrawBitmapString("there. That is MVC working.", x, y);      y += 24.0f;
    ofDrawBitmapString("Press c: the pull is removed and the", x, y);
    y += 15.0f;
    ofDrawBitmapString("rest renormalize. Right for speakers,", x, y);
    y += 15.0f;
    ofDrawBitmapString("wrong for parameters -- never automatic.", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == 'c') clampOn = !clampOn;
}
