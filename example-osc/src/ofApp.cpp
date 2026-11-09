#include "ofApp.h"

using namespace ofxManifold;
using namespace ofxManifold::editor;
using namespace ofxManifold::player;

namespace {

std::string describe(const OscMessage& m) {
    std::string t = m.address;
    for (const OscArg& a : m.args) {
        t += " ";
        if (a.type == 'f') t += ofToString(a.f, 3);
        else if (a.type == 'i') t += ofToString(a.i);
        else t += a.s;
    }
    return t;
}

// OscOut's messages as ofxOsc's, and back. Only i, f and s go out.
ofxOscMessage toOfx(const OscMessage& m) {
    ofxOscMessage o;
    o.setAddress(m.address);
    for (const OscArg& a : m.args) {
        if (a.type == 'i') o.addIntArg(a.i);
        else if (a.type == 'f') o.addFloatArg(a.f);
        else o.addStringArg(a.s);
    }
    return o;
}

// What comes back (describe, hello) in OscOut's terms. A receiver may send a
// symbol for a string or a double for a float; both are read as ours. A
// message with any other kind of argument is not one of ours: false.
bool fromOfx(const ofxOscMessage& o, OscMessage& m) {
    m = OscMessage{};
    m.address = o.getAddress();
    for (std::size_t k = 0; k < o.getNumArgs(); ++k) {
        switch (o.getArgType(k)) {
        case OFXOSC_TYPE_INT32:  m.args.push_back(OscArg::integer(o.getArgAsInt32(k))); break;
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_DOUBLE: m.args.push_back(OscArg::number(o.getArgAsFloat(k))); break;
        case OFXOSC_TYPE_STRING:
        case OFXOSC_TYPE_SYMBOL: m.args.push_back(OscArg::text(o.getArgAsString(k))); break;
        default: return false;
        }
    }
    return true;
}

} // namespace

void ofApp::setup() {
    ofSetFrameRate(120);              // the sender keeps its own rate
    ofBackground(30, 30, 34);
    model.loadExample();
    adopt();

    sender.setup(host, sendPort);
    listener.setup(listenPort);
}

void ofApp::adopt() {
    evaluator = std::make_unique<Evaluator>(model.manifold());
    renderer = std::make_unique<ofxManifoldRenderer>(model.manifold());
    evaluation = evaluator->evaluate(point);
    z = listenStart(model, evaluation);
    osc.reset();                      // the new map's outputs, described at once
}

// The map's outputs in channel order with their levels at the point, the
// node values there, and the point.
OscFrame ofApp::frameNow() const {
    OscFrame f;
    const Selection none;
    const ChartLevels lv = chartLevels3D(model, none, evaluation, z);
    const Mapping& mp = model.mapping();
    std::vector<OscOutput> outs;
    auto level = [&](int ch) {
        return (ch >= 0 && std::size_t(ch) < lv.levels.size()) ? lv.levels[std::size_t(ch)]
                                                               : 0.0f;
    };
    for (TargetID t = 0; t < mp.targetCount(); ++t) {
        const int ch = mp.targetChannel(t);
        if (ch == NoChannel) continue;
        outs.push_back({mp.targetName(t), ch, level(ch)});
    }
    for (const Aggregator& ag : mp.aggregators()) {
        if (ag.channel == NoChannel) continue;
        outs.push_back({ag.name, ag.channel, level(ag.channel)});
    }
    std::sort(outs.begin(), outs.end(), [](const OscOutput& a, const OscOutput& b) {
        return a.channel < b.channel;
    });
    f.outputs = outs;
    for (const auto& v : valuesAt(model, evaluation, z)) f.values.push_back({v.first, v.second});
    f.hasPosition = true;
    f.position = {point.x, point.y, z};
    return f;
}

void ofApp::update() {
    const double now = ofGetElapsedTimef();
    if (glideDir != 0) {
        z = glide(z, glideDir, ofGetKeyPressed(OF_KEY_SHIFT), float(ofGetLastFrameTime()));
    }
    evaluation = evaluator->evaluate(point);

    // Answers: describe, hello.
    for (int k = 0; k < 64 && listener.hasWaitingMessages(); ++k) {
        ofxOscMessage in;
        if (!listener.getNextMessage(in)) break;
        OscMessage m;
        if (fromOfx(in, m)) osc.receive(now, m);
    }

    if (!paused) {
        const auto msgs = osc.frame(now, frameNow());
        if (!msgs.empty()) lastSent.clear();
        for (const OscMessage& m : msgs) {
            // One message per packet, no bundle (OSC.md); false is ofxOsc's
            // default in 0.12.1, said here so it stays so.
            sender.sendMessage(toOfx(m), false);
            sentLog.emplace_back(now, encode(m).size());   // the bytes on the wire
            lastSent.push_back(describe(m));
        }
    }
    while (!sentLog.empty() && sentLog.front().first < now - 1.0) sentLog.pop_front();
}

void ofApp::draw() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));
    renderer->draw(evaluation.regionID);
    renderer->drawEvaluation(evaluation, point);

    const double now = ofGetElapsedTimef();
    std::size_t bytes = 0;
    for (const auto& e : sentLog) bytes += e.second;
    float x = renderer->viewport().getRight() + 30.0f, y = 40.0f;
    auto line = [&](const std::string& t, const ofColor& c) {
        ofSetColor(c);
        ofDrawBitmapString(t, x, y);
        y += 16.0f;
    };
    const ofColor plain(200, 205, 215), dim(130, 136, 150);
    line("OSC to " + host + ":" + ofToString(sendPort)
         + "   answers on " + ofToString(listenPort), ofColor(235, 238, 245));
    line("prefix " + osc.prefix() + "   " + ofToString(osc.rate(), 0) + " a second"
         + (paused ? "   PAUSED (space)" : ""), plain);
    line(std::string("packed ") + (osc.packed() ? "on" : "off") + " (p)   per output "
         + (osc.perOutput() ? "on" : "off") + " (i)", plain);
    line(ofToString(sentLog.size()) + " messages, " + ofToString(bytes) + " bytes in the last second",
         plain);
    if (osc.connected(now)) {
        line("answered by " + (osc.peer().empty() ? std::string("a receiver") : osc.peer()),
             ofColor(120, 200, 160));
    } else {
        line("no answer yet -- fine: nothing waits for one", dim);
    }
    line("point " + ofToString(point.x, 3) + " " + ofToString(point.y, 3)
         + "   height " + ofToString(z, 3) + " (up / down)", plain);
    y += 8.0f;
    line("last send:", ofColor(235, 238, 245));
    for (std::size_t k = 0; k < lastSent.size() && k < 24; ++k) {
        std::string t = lastSent[k];
        if (t.size() > 60) t = t.substr(0, 57) + "...";
        line(t, dim);
    }
    if (!message.empty() && ofGetElapsedTimef() < messageUntil) {
        y += 8.0f;
        line(message, ofColor(255, 214, 90));
    }
}

void ofApp::mousePressed(int x, int y, int) {
    if (renderer->viewport().inside(x, y)) point = renderer->toManifold({float(x), float(y)});
}

void ofApp::mouseDragged(int x, int y, int button) { mousePressed(x, y, button); }

void ofApp::keyPressed(int key) {
    auto say = [&](const std::string& t) {
        message = t;
        messageUntil = ofGetElapsedTimef() + 2.5f;
    };
    if (key == OF_KEY_UP) glideDir = +1;
    else if (key == OF_KEY_DOWN) glideDir = -1;
    else if (key == 'p') { osc.setPacked(!osc.packed()); osc.reset(); }
    else if (key == 'i') { osc.setPerOutput(!osc.perOutput()); osc.reset(); }
    else if (key == '[') osc.setRate(osc.rate() >= 20.0 ? osc.rate() - 10.0 : osc.rate() / 2.0);
    else if (key == ']') osc.setRate(osc.rate() >= 10.0 ? osc.rate() + 10.0 : osc.rate() * 2.0);
    else if (key == ' ') paused = !paused;
    else if (key == 'e') { model.loadExample(); adopt(); say("the example map"); }
    else if (key == 'o') open();
}

void ofApp::keyReleased(int key) {
    if ((key == OF_KEY_UP && glideDir > 0) || (key == OF_KEY_DOWN && glideDir < 0)) glideDir = 0;
}

void ofApp::open() {
    ofFileDialogResult d = ofSystemLoadDialog("Open a map -- either file of a pair");
    if (!d.bSuccess) return;
    const Result r = openPair(model, d.getPath());
    message = r.message;
    messageUntil = ofGetElapsedTimef() + 3.0f;
    if (r.changed) adopt();
}
