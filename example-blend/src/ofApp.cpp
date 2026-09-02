#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — blending two maps");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    build();
}

void ofApp::build() {
    // ---- map A: a wide front triangle ------------------------------------
    mapA = Manifold2D();
    const auto a0 = mapA.addNode("L",  {0.12f, 0.20f});
    const auto a1 = mapA.addNode("R",  {0.88f, 0.20f});
    const auto a2 = mapA.addNode("Cf", {0.50f, 0.86f});
    const auto a3 = mapA.addNode("Of", {0.50f, 0.45f});
    mapA.addTriangle(a0, a1, a3);
    mapA.addTriangle(a1, a2, a3);
    mapA.addTriangle(a2, a0, a3);

    // ---- map B: a rotated triangle over the same square ------------------
    //
    // Note the node ids: both maps number from zero, because NodeIDs are
    // per-manifold indices. Blending these weight vectors directly would add
    // L to Lr simply because both are node 0.
    mapB = Manifold2D();
    const auto b0 = mapB.addNode("Lr", {0.20f, 0.80f});
    const auto b1 = mapB.addNode("Rr", {0.80f, 0.80f});
    const auto b2 = mapB.addNode("Cr", {0.50f, 0.14f});
    const auto b3 = mapB.addNode("Or", {0.50f, 0.55f});
    mapB.addTriangle(b0, b1, b3);
    mapB.addTriangle(b1, b2, b3);
    mapB.addTriangle(b2, b0, b3);

    // ---- mappings ---------------------------------------------------------
    //
    // out.L and out.R are in BOTH maps, so they sum across the crossfade.
    // out.front is only in A and out.rear only in B, so they fade in and out.
    //
    // The target NAMES are declared in different orders in the two mappings,
    // which is deliberate: TargetIDs are per-Mapping indices too, so anything
    // combining these by id rather than by name would be wrong here.
    mapPA = Mapping();
    mapPA.bind(a0, mapPA.addTarget("out.L"));
    mapPA.bind(a1, mapPA.addTarget("out.R"));
    mapPA.bind(a2, mapPA.addTarget("out.front"));
    mapPA.bind(a3, mapPA.addTarget("out.L"));       // composite: centre feeds
    mapPA.bind(a3, mapPA.addTarget("out.R"));       // both sides

    mapPB = Mapping();
    mapPB.bind(b2, mapPB.addTarget("out.rear"));    // declared FIRST here
    mapPB.bind(b0, mapPB.addTarget("out.L"));
    mapPB.bind(b1, mapPB.addTarget("out.R"));
    mapPB.bind(b3, mapPB.addTarget("out.rear"));

    evalA = std::make_unique<Evaluator>(mapA);
    evalB = std::make_unique<Evaluator>(mapB);
    rendA = std::make_unique<ofxManifoldRenderer>(mapA);
    rendB = std::make_unique<ofxManifoldRenderer>(mapB);

    // Stacked, so both draw into the same square and the same normalized
    // point means the same place in each.
    rendA->style.drawRegions = false;
    rendB->style.drawRegions = false;
    rendA->style.drawFrame   = false;
    rendB->style.drawFrame   = false;
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    const ofRectangle vp(24, 24, side, side);
    rendA->setViewport(vp);
    rendB->setViewport(vp);

    if (autoSweep) {
        mix = 0.5f + 0.5f * std::sin(ofGetElapsedTimef() * 0.4f);
    }

    eA = evalA->evaluate(point);
    eB = evalB->evaluate(point);

    // THE OPERATION. Each side resolves through its OWN mapping first, then
    // the two are merged by target name. Nothing crosses at the node level,
    // because nothing at the node level is comparable between two maps.
    mixed = blendByName(mapPA.resolve(eA.weights), mapPA,
                        mapPB.resolve(eB.weights), mapPB,
                        mix,
                        useEqualPower ? curve::equalPower : curve::linear);
}

void ofApp::drawMap(const Manifold2D& m, const Evaluation& e,
                    const ofColor& tint, float alpha) const {
    const ofxManifoldRenderer& r = (&m == &mapA) ? *rendA : *rendB;

    ofPushStyle();
    ofSetColor(tint, 40.0f + 150.0f * alpha);
    ofSetLineWidth(1.0f + 1.5f * alpha);
    for (std::size_t i = 0; i < m.regionCount(); ++i) {
        const auto& ids = m.region(static_cast<RegionID>(i)).ids();
        for (int k = 0; k < 3; ++k) {
            const glm::vec2 p = r.toScreen(m.node(ids[k]).position);
            const glm::vec2 q = r.toScreen(m.node(ids[(k + 1) % 3]).position);
            ofDrawLine(p.x, p.y, q.x, q.y);
        }
    }
    ofFill();
    for (std::size_t i = 0; i < m.nodeCount(); ++i) {
        const Node& n = m.node(static_cast<NodeID>(i));
        const glm::vec2 p = r.toScreen(n.position);
        ofSetColor(tint, 90.0f + 165.0f * alpha);
        ofDrawCircle(p.x, p.y, 4.0f + 3.0f * alpha);
        ofDrawBitmapString(n.name, p.x + 8.0f, p.y - 6.0f);
    }
    // Influence lines, faded by how much this map is contributing.
    if (e.inside) {
        const glm::vec2 s = r.toScreen(point);
        for (const auto& wn : e.weights) {
            const glm::vec2 n = r.toScreen(m.node(wn.id).position);
            ofSetColor(tint, 200.0f * alpha * ofClamp(wn.weight, 0.f, 1.f));
            ofSetLineWidth(1.0f + 4.0f * ofClamp(wn.weight, 0.0f, 1.0f));
            ofDrawLine(s.x, s.y, n.x, n.y);
        }
    }
    ofPopStyle();
}

void ofApp::draw() {
    const ofColor tintA(120, 200, 160);
    const ofColor tintB(200, 140, 230);

    // Opacity tracks the crossfade, so the map that is speaking is the map you
    // can see. This is presentation only -- the weights are unaffected.
    drawMap(mapA, eA, tintA, 1.0f - mix);
    drawMap(mapB, eB, tintB, mix);

    ofPushStyle();
    ofFill();
    ofSetColor(255, 214, 90);
    const glm::vec2 s = rendA->toScreen(point);
    ofDrawCircle(s.x, s.y, 7.0f);
    ofPopStyle();

    const float x = rendA->viewport().getRight() + 40.0f;
    float y = 48.0f;

    // The crossfade, as a bar.
    ofSetColor(235, 238, 245);
    ofDrawBitmapString("crossfade", x, y);                        y += 18.0f;
    ofSetColor(70, 74, 88);
    ofDrawRectangle(x, y - 8.0f, 220.0f, 6.0f);
    ofSetColor(255, 214, 90);
    ofDrawRectangle(x + 220.0f * mix - 2.0f, y - 11.0f, 4.0f, 12.0f);
    y += 16.0f;
    ofSetColor(tintA);  ofDrawBitmapString("A", x, y);
    ofSetColor(tintB);  ofDrawBitmapString("B", x + 212.0f, y);
    ofSetColor(180, 186, 200);
    ofDrawBitmapString(ofToString(mix, 2), x + 100.0f, y);        y += 28.0f;

    ofSetColor(tintA);
    ofDrawBitmapString("map A", x, y);                            y += 18.0f;
    for (const auto& wn : eA.weights) {
        ofSetColor(150, 160, 170);
        ofDrawBitmapString("  " + mapA.node(wn.id).name + "  "
                           + ofToString(wn.weight, 3), x, y);
        y += 15.0f;
    }
    y += 10.0f;
    ofSetColor(tintB);
    ofDrawBitmapString("map B", x, y);                            y += 18.0f;
    for (const auto& wn : eB.weights) {
        ofSetColor(150, 160, 170);
        ofDrawBitmapString("  " + mapB.node(wn.id).name + "  "
                           + ofToString(wn.weight, 3), x, y);
        y += 15.0f;
    }
    y += 20.0f;

    // The combined result, by NAME. out.L and out.R are in both maps and sum;
    // out.front and out.rear belong to one each and fade.
    ofSetColor(235, 238, 245);
    ofDrawBitmapString("blended targets", x, y);                  y += 20.0f;
    float total = 0.0f;
    for (const auto& nw : mixed) {
        total += nw.weight;
        ofSetColor(120, 200, 160, 80 + 175 * ofClamp(nw.weight, 0.f, 1.f));
        ofDrawRectangle(x, y - 9.0f, 150.0f * ofClamp(nw.weight, 0.f, 1.f),
                        11.0f);
        ofSetColor(235, 238, 245);
        ofDrawBitmapString(nw.target + "   " + ofToString(nw.weight, 3),
                           x + 4.0f, y);
        y += 18.0f;
    }
    y += 8.0f;
    ofSetColor(useEqualPower ? ofColor(255, 214, 90) : ofColor(120, 200, 160));
    ofDrawBitmapString("total   " + ofToString(total, 3), x, y);  y += 28.0f;

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("drag       move the point", x, y);        y += 16.0f;
    ofDrawBitmapString("< >        crossfade", x, y);             y += 16.0f;
    ofDrawBitmapString("a          auto sweep", x, y);            y += 16.0f;
    ofDrawBitmapString(std::string("c          equal power: ")
                       + (useEqualPower ? "on" : "off"), x, y);   y += 32.0f;

    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Hold the point still and sweep the", x, y);
    y += 15.0f;
    ofDrawBitmapString("crossfade. Every output changes while", x, y);
    y += 15.0f;
    ofDrawBitmapString("the point has not moved at all.", x, y);  y += 24.0f;
    ofDrawBitmapString("That is the third axis: two 2D maps", x, y);
    y += 15.0f;
    ofDrawBitmapString("combined downstream, which is how", x, y);
    y += 15.0f;
    ofDrawBitmapString("SpaceMap reached three dimensions", x, y);
    y += 15.0f;
    ofDrawBitmapString("rather than with tetrahedra.", x, y);     y += 24.0f;
    ofDrawBitmapString("out.L and out.R live in BOTH maps and", x, y);
    y += 15.0f;
    ofDrawBitmapString("sum. out.front and out.rear belong to", x, y);
    y += 15.0f;
    ofDrawBitmapString("one map each and fade.", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = rendA->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = rendA->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == ',' || key == '<') { mix = std::max(0.0f, mix - 0.02f); autoSweep = false; }
    if (key == '.' || key == '>') { mix = std::min(1.0f, mix + 0.02f); autoSweep = false; }
    if (key == 'a') autoSweep = !autoSweep;
    if (key == 'c') useEqualPower = !useEqualPower;
}
