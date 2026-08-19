#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — mapping");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    build();
}

void ofApp::build() {
    manifold = Manifold2D();
    mapping  = Mapping();

    // ---- the working area ------------------------------------------------
    const auto C  = manifold.addNode("centre",   {0.50f, 0.50f});
    const auto NW = manifold.addNode("front.L",  {0.28f, 0.72f});
    const auto NE = manifold.addNode("front.R",  {0.72f, 0.72f});
    const auto SE = manifold.addNode("rear.R",   {0.72f, 0.28f});
    const auto SW = manifold.addNode("rear.L",   {0.28f, 0.28f});

    // ---- the ring of null nodes -----------------------------------------
    //
    // Four corners bound to NOTHING. Drag the point out toward one and the
    // weights still sum to 1 in the manifold -- but the resolved TARGETS sum
    // to less, because a null node's share is discarded rather than
    // redistributed. That shortfall is the fade.
    //
    // SpaceMap added these in the 1980s because sound dragged off the map edge
    // cut out abruptly. Ringing a map with them turns fade-to-nothing into
    // ordinary barycentric behaviour instead of a special case in the code.
    const auto n0 = manifold.addNode("null.NW", {0.06f, 0.94f});
    const auto n1 = manifold.addNode("null.NE", {0.94f, 0.94f});
    const auto n2 = manifold.addNode("null.SE", {0.94f, 0.06f});
    const auto n3 = manifold.addNode("null.SW", {0.06f, 0.06f});

    manifold.addTriangle(C, NW, NE);
    manifold.addTriangle(C, NE, SE);
    manifold.addTriangle(C, SE, SW);
    manifold.addTriangle(C, SW, NW);

    manifold.addTriangle(NW, n0, n1);
    manifold.addTriangle(NW, n1, NE);
    manifold.addTriangle(NE, n1, n2);
    manifold.addTriangle(NE, n2, SE);
    manifold.addTriangle(SE, n2, n3);
    manifold.addTriangle(SE, n3, SW);
    manifold.addTriangle(SW, n3, n0);
    manifold.addTriangle(SW, n0, NW);

    // ---- bindings --------------------------------------------------------
    const auto out1 = mapping.addTarget("out.1");
    const auto out2 = mapping.addTarget("out.2");
    const auto out3 = mapping.addTarget("out.3");
    const auto out4 = mapping.addTarget("out.4");
    const auto sub  = mapping.addTarget("out.sub");

    // TERMINAL: one node, one target.
    mapping.bind(NW, out1);
    mapping.bind(NE, out2);
    mapping.bind(SE, out3);

    // MANY TO ONE: rear.L also feeds out.3. The map is not a projection of
    // real space, so two nodes may legitimately be the same output.
    mapping.bind(SW, out3);
    mapping.bind(SW, out4);

    // COMPOSITE: the centre node feeds all four outputs at once. This is the
    // overhead-speaker case -- a node that stands for a group rather than a
    // single output. Its total contribution does not change with how many
    // targets it feeds, because link weights normalize WITHIN the node.
    mapping.bind(C, out1);
    mapping.bind(C, out2);
    mapping.bind(C, out3);
    mapping.bind(C, out4);

    // A weighted composite: the sub gets three parts front, one part rear.
    mapping.bind(NW, sub, 3.0f);
    mapping.bind(SW, sub, 1.0f);

    // NULL: n0..n3 are bound to nothing at all. No code marks them as a type;
    // they simply have no bindings.
    (void)n0; (void)n1; (void)n2; (void)n3;

    // AGGREGATOR: runs the routing backwards. Rather than distributing to
    // linked nodes, it RECEIVES their sum. Not part of any region, and not a
    // node type -- modelling it as one would put a non-participating object
    // into the geometry and force every region operation to filter it out.
    mapping.addAggregator("front.energy", {NW, NE}, SumMode::Linear);
    mapping.addAggregator("front.power",  {NW, NE}, SumMode::PowerPreserving);

    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));
    evaluation = evaluator->evaluate(point);
    resolved   = mapping.resolve(evaluation.weights);
    dense      = mapping.toDenseVector(evaluation.weights);
}

void ofApp::drawTargets(float x, float y) {
    ofSetColor(235, 238, 245);
    ofDrawBitmapString("targets", x, y);
    y += 22.0f;

    // Dense: one slot per target, ALWAYS, including targets with no share this
    // frame. An OSC receiver reading argument 3 must get the same output every
    // frame, so arity is a property of the mapping and not of the point.
    float targetTotal = 0.0f;
    for (std::size_t i = 0; i < dense.size(); ++i) {
        const TargetID id = static_cast<TargetID>(i);
        targetTotal += dense[i];
        ofSetColor(120, 200, 160, 90 + 165 * ofClamp(dense[i], 0.f, 1.f));
        ofDrawRectangle(x, y - 9.0f, 120.0f * ofClamp(dense[i], 0.f, 1.f),
                        11.0f);
        ofSetColor(235, 238, 245);
        ofDrawBitmapString(mapping.targetName(id) + "   "
                           + ofToString(dense[i], 3), x + 4.0f, y);
        y += 18.0f;
    }

    y += 12.0f;

    float weightTotal = 0.0f;
    for (const auto& wn : evaluation.weights) weightTotal += wn.weight;

    ofSetColor(180, 186, 200);
    ofDrawBitmapString("node weights sum   " + ofToString(weightTotal, 3),
                       x, y);
    y += 16.0f;

    // The number this example exists to show. Inside the working area these
    // agree. Out among the null nodes the target total falls while the node
    // weights still sum to one -- the discarded share IS the fade.
    ofSetColor(targetTotal < 0.995f ? ofColor(255, 214, 90)
                                    : ofColor(120, 200, 160));
    ofDrawBitmapString("target total       " + ofToString(targetTotal, 3),
                       x, y);
    y += 16.0f;
    ofSetColor(140, 146, 160);
    ofDrawBitmapString("shortfall          "
                       + ofToString(1.0f - targetTotal, 3)
                       + "   (held by null nodes)", x, y);
    y += 28.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("aggregators", x, y);
    y += 22.0f;
    const auto& ags = mapping.aggregators();
    for (std::size_t i = 0; i < ags.size() && i < resolved.aggregates.size();
         ++i) {
        ofSetColor(180, 186, 200);
        ofDrawBitmapString(ags[i].name + "   "
                           + ofToString(resolved.aggregates[i], 3), x, y);
        y += 16.0f;
    }

    y += 24.0f;
    ofSetColor(140, 146, 160);
    ofDrawBitmapString("centre    composite, feeds all four", x, y);
    y += 15.0f;
    ofDrawBitmapString("rear.L    many-to-one, shares out.3", x, y);
    y += 15.0f;
    ofDrawBitmapString("null.*    bound to nothing: the fade", x, y);
    y += 15.0f;
    ofDrawBitmapString("out.sub   3 parts front, 1 part rear", x, y);
    y += 24.0f;
    ofDrawBitmapString("drag out toward a corner and watch", x, y);
    y += 15.0f;
    ofDrawBitmapString("the target total fall while the node", x, y);
    y += 15.0f;
    ofDrawBitmapString("weights still sum to one.", x, y);
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);
    renderer->drawEvaluation(evaluation, point);
    drawTargets(renderer->viewport().getRight() + 40.0f, 48.0f);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
}

void ofApp::keyPressed(int key) {
    if (key == 'r') showRing = !showRing;
}
