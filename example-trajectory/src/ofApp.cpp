#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — trajectory");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    buildVenueA();
}

void ofApp::adoptManifold() {
    // The Evaluator holds a region hint into a specific manifold. Swapping the
    // manifold out from under it would leave that hint naming a region in a
    // map that no longer exists, so both are rebuilt together.
    //
    // The TRAJECTORY is deliberately NOT rebuilt. It survives the swap
    // untouched, which is the entire point of this example.
    evaluator = std::make_unique<Evaluator>(manifold);
    renderer  = std::make_unique<ofxManifoldRenderer>(manifold);
}

void ofApp::buildVenueA() {
    manifold = Manifold2D();
    const auto A = manifold.addNode("A", {0.10f, 0.10f});
    const auto B = manifold.addNode("B", {0.90f, 0.10f});
    const auto C = manifold.addNode("C", {0.50f, 0.90f});
    const auto O = manifold.addNode("O", {0.50f, 0.40f});
    manifold.addTriangle(A, B, O);
    manifold.addTriangle(B, C, O);
    manifold.addTriangle(C, A, O);
    venue = 1;
    venueName = "venue A  wide, centre low";
    adoptManifold();
}

void ofApp::buildVenueB() {
    // Same node NAMES, different positions. A different room with the same
    // rig labels -- which is exactly the touring situation.
    manifold = Manifold2D();
    const auto A = manifold.addNode("A", {0.05f, 0.30f});
    const auto B = manifold.addNode("B", {0.95f, 0.20f});
    const auto C = manifold.addNode("C", {0.60f, 0.95f});
    const auto O = manifold.addNode("O", {0.55f, 0.55f});
    manifold.addTriangle(A, B, O);
    manifold.addTriangle(B, C, O);
    manifold.addTriangle(C, A, O);
    venue = 2;
    venueName = "venue B  skewed, centre high";
    adoptManifold();
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));

    if (playing && path.sampleCount() > 1) {
        const float dur = path.duration();

        if (realTime && dur > 0.0f) {
            // Real seconds. rate 1.0 replays at the tempo it was performed,
            // pauses included, because finalize() kept the sample spacing.
            elapsed += (1.0f / 60.0f) * rate;
            while (elapsed > dur) elapsed -= dur;
            point    = path.pointAtSeconds(elapsed);
            velocity = path.velocityAtSeconds(elapsed);
        } else {
            // No duration known -- a path loaded from an older file, or one
            // whose samples arrived already normalized. Sweep the parameter
            // instead so playback still works.
            elapsed += (1.0f / 60.0f) * rate * 0.25f;
            while (elapsed > 1.0f) elapsed -= 1.0f;
            point    = path.pointAt(elapsed);
            velocity = path.velocityAt(elapsed);
        }
        speed = glm::length(velocity);
    }

    evaluation = evaluator->evaluate(point);
}

void ofApp::draw() {
    renderer->draw(evaluation.regionID);

    // The recorded path, drawn through the CURRENT venue's transform. Same
    // normalized samples, so the shape on screen is identical in both venues
    // even though everything underneath it moved.
    if (path.sampleCount() > 1) {
        ofPushStyle();
        ofSetColor(255, 214, 90, 110);
        ofSetLineWidth(1.5f);
        ofNoFill();
        ofBeginShape();
        for (std::size_t i = 0; i < path.sampleCount(); ++i) {
            const glm::vec2 s = renderer->toScreen(path.sample(i).position);
            ofVertex(s.x, s.y);
        }
        ofEndShape(false);
        ofPopStyle();
    }

    renderer->drawEvaluation(evaluation, point);

    const float x = renderer->viewport().getRight() + 40.0f;
    float y = 48.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString(venueName, x, y);                          y += 28.0f;

    if (recording) {
        ofSetColor(232, 96, 88);
        ofDrawBitmapString("RECORDING   " + ofToString(path.sampleCount())
                           + " samples", x, y);
    } else if (playing) {
        ofSetColor(120, 200, 160);
        const float dur = path.duration();
        if (realTime && dur > 0.0f) {
            ofDrawBitmapString("playing     " + ofToString(elapsed, 2) + " / "
                               + ofToString(dur, 2) + " s", x, y);
        } else {
            ofDrawBitmapString("playing     phase " + ofToString(elapsed, 3),
                               x, y);
        }
    } else if (path.sampleCount() > 1) {
        ofSetColor(180, 186, 200);
        ofDrawBitmapString("ready       " + ofToString(path.sampleCount())
                           + " samples", x, y);
    } else {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("no path yet", x, y);
    }
    y += 28.0f;

    // Duration is the thing normalization would have thrown away. A path with
    // no duration replays at an arbitrary speed; a path with one replays as it
    // was performed.
    ofSetColor(180, 186, 200);
    ofDrawBitmapString("duration    "
                       + (path.duration() > 0.0f
                          ? ofToString(path.duration(), 2) + " s"
                          : std::string("unknown")), x, y);
    y += 16.0f;
    ofDrawBitmapString("rate        " + ofToString(rate, 2) + " x", x, y);
    y += 16.0f;

    // Speed is a derivative of stored data, not a stateful accumulator. The
    // architecture keeps MOTION outside the kernel because the evaluator is
    // stateless -- but a recorded path IS history, complete before anyone
    // asks, so a derivative over it is a pure function.
    ofDrawBitmapString("speed       " + ofToString(speed, 3), x, y);
    y += 16.0f;

    // The pause is visible here and nowhere else. Hold still while recording,
    // and on playback the dot stops while the clock keeps running -- speed
    // drops to zero and stays there. Uniform resampling would have replayed it
    // as steady motion (D-011).
    ofSetColor(speed < 0.02f ? ofColor(255, 214, 90) : ofColor(140, 146, 160));
    ofDrawBitmapString(speed < 0.02f ? "            (holding)" : "", x, y);
    y += 28.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("weights", x, y);                          y += 20.0f;
    for (const auto& wn : evaluation.weights) {
        ofSetColor(180, 186, 200);
        ofDrawBitmapString(manifold.node(wn.id).name + "   "
                           + ofToString(wn.weight, 3), x, y);
        y += 16.0f;
    }
    if (!evaluation.inside) {
        ofSetColor(232, 96, 88);
        ofDrawBitmapString("outside the hull", x, y);
        y += 16.0f;
    }
    y += 24.0f;

    ofSetColor(140, 146, 160);
    ofDrawBitmapString("r        record (drag to draw a path)", x, y);
    y += 16.0f;
    ofDrawBitmapString("space    play / pause, looping", x, y);   y += 16.0f;
    ofDrawBitmapString("1 2      swap venue, MID-PLAYBACK", x, y);y += 16.0f;
    ofDrawBitmapString("[ ]      slower / faster", x, y);         y += 16.0f;
    ofDrawBitmapString(std::string("t        real seconds: ")
                       + (realTime ? "on" : "off"), x, y);        y += 16.0f;
    ofDrawBitmapString("s l      save / load path.json", x, y);   y += 32.0f;

    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Record a path, press space, then press", x, y);
    y += 15.0f;
    ofDrawBitmapString("2 while it is still playing.", x, y);     y += 24.0f;
    ofDrawBitmapString("The dot traces the SAME shape. Every", x, y);
    y += 15.0f;
    ofDrawBitmapString("node moved, so the weights reorganize", x, y);
    y += 15.0f;
    ofDrawBitmapString("around a gesture that did not change.", x, y);
    y += 24.0f;
    ofDrawBitmapString("That is why the manifold lives in", x, y); y += 15.0f;
    ofDrawBitmapString("normalized coordinates rather than", x, y);y += 15.0f;
    ofDrawBitmapString("pixels: a path in pixels could not", x, y);y += 15.0f;
    ofDrawBitmapString("survive a change of window, let alone", x, y);
    y += 15.0f;
    ofDrawBitmapString("of room.", x, y);                        y += 24.0f;
    ofDrawBitmapString("Pause while recording. On playback the", x, y);
    y += 15.0f;
    ofDrawBitmapString("dot stops and the clock keeps running.", x, y);
}

void ofApp::mousePressed(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
    if (recording) {
        path.addSample(ofGetElapsedTimef() - recordStart, point);
    }
}

void ofApp::mouseDragged(int x, int y, int) {
    point = renderer->toManifold(glm::vec2(x, y));
    if (recording) {
        // Real elapsed time, not frame count. A pause while dragging is stored
        // as a pause, and finalize() preserves that spacing when it rescales
        // to [0, 1]. Sampling uniformly would replay a held position as a slow
        // drift.
        path.addSample(ofGetElapsedTimef() - recordStart, point);
    }
}

void ofApp::mouseReleased(int, int, int) {}

void ofApp::keyPressed(int key) {
    if (key == 'r') {
        if (!recording) {
            path.clear();
            recording = true;
            playing = false;
            recordStart = ofGetElapsedTimef();
        } else {
            recording = false;
            // Rescales sample times to [0, 1] AND captures the duration
            // before doing so. Without that capture, the tempo would be gone.
            path.finalize();
            elapsed = 0.0f;
            ofLogNotice() << "recorded " << path.sampleCount()
                          << " samples over " << path.duration() << "s";
        }
    }
    if (key == ' ' && path.sampleCount() > 1) playing = !playing;
    if (key == '1' && venue != 1) buildVenueA();
    if (key == '2' && venue != 2) buildVenueB();
    if (key == '[') rate = std::max(0.05f, rate * 0.8f);
    if (key == ']') rate = std::min(8.00f, rate * 1.25f);
    if (key == 't') { realTime = !realTime; elapsed = 0.0f; }

    if (key == 's' && path.sampleCount() > 1) {
        ofBuffer buf;
        buf.set(io::saveTrajectory(path));
        ofBufferToFile("path.json", buf);
        ofLogNotice() << "wrote " << ofToDataPath("path.json");
    }
    if (key == 'l') {
        ofBuffer buf = ofBufferFromFile("path.json");
        Trajectory loaded;
        const io::LoadResult r = io::loadTrajectory(buf.getText(), loaded);
        if (r.ok) {
            path = loaded;
            elapsed = 0.0f;
            ofLogNotice() << "loaded " << path.sampleCount() << " samples";
        } else {
            ofLogError() << "load failed: " << r.error;
        }
    }
}
