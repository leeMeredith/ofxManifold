#include "ofApp.h"

using namespace ofxManifold;
using namespace ofxManifold::editor;
using namespace ofxManifold::player;

namespace {

// The panel's lines are cut to this many characters (8 pixels each in the
// bitmap font), so the panel needs this much room beside the views.
constexpr std::size_t kOscPanelChars = 44;
constexpr float       kOscPanelWidth = kOscPanelChars * 8.0f;
// Room for the column of level dots and its label, right of the map.
constexpr float       kColumnRoom = 70.0f;
// The half-lives - and = step through, in seconds; shift steps by kFineStep
// between the first and the last.
constexpr float kHalfLives[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f};
constexpr float kFineStep = 0.01f;
// The faders' band along the bottom: this share of the window, within limits.
constexpr float kChartShare = 0.24f, kChartMin = 130.0f, kChartMax = 220.0f;

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
    text.setSize(12);
    applySmoothing();
    sender.setup(host, sendPort);
    // The first answer port free. Not shared (reuse off): a second copy of
    // this app must not quietly split the first one's answers.
    for (int k = 0; k < kAnswerPorts && listenPort == 0; ++k) {
        ofxOscReceiverSettings s;
        s.port = kFirstAnswerPort + k;
        s.reuse = false;
        if (listener.setup(s)) listenPort = s.port;
    }
    osc.setAnswerPort(listenPort);

    model.loadRoom();
    adopt("room");
    cam.enableMouseInput();
}

void ofApp::adopt(const std::string& name) {
    osc.setMapName(name);             // its addresses, /manifold/<name>/...
    evaluator = std::make_unique<Evaluator>(model.manifold());
    renderer = std::make_unique<ofxManifoldRenderer>(model.manifold());
    evaluation = evaluator->evaluate(point);
    z = listenStart(model, evaluation);
    osc.reset();                      // the new map's outputs, described at once
    levelSmoother.reset();            // another map: taken at once, not glided into
    valueSmoother.reset();
}

void ofApp::applySmoothing() {
    const float h = smoothOn ? halfLife : 0.0f;
    levelSmoother.setHalfLife(h);
    valueSmoother.setHalfLife(h);
}

// The map at the point, smoothed. The levels go through as one list --
// routed by channel, after trim by channel, then silence -- so a fader's bar,
// its trim tick and the silence bar glide together; its length is fixed by
// the map's channels. The values are a second list, in the map's order.
void ofApp::smooth(float dt) {
    const Selection noSelection;
    const ChartLevels raw = chartLevels3D(model, noSelection, evaluation, z);
    std::vector<float> v(raw.routed);
    v.insert(v.end(), raw.levels.begin(), raw.levels.end());
    v.push_back(raw.silence);
    const std::vector<float>& s = levelSmoother.update(v, dt);
    const std::size_t n = raw.routed.size(), m = raw.levels.size();
    heard = raw;
    heard.routed.assign(s.begin(), s.begin() + std::ptrdiff_t(n));
    heard.levels.assign(s.begin() + std::ptrdiff_t(n), s.begin() + std::ptrdiff_t(n + m));
    heard.silence = s[n + m];

    heardValues = valuesAt(model, evaluation, z);
    v.clear();
    for (const auto& p : heardValues) v.push_back(p.second);
    const std::vector<float>& sv = valueSmoother.update(v, dt);
    for (std::size_t k = 0; k < heardValues.size(); ++k) heardValues[k].second = sv[k];
}

// The map at the left, the column of level dots right of it (only with two
// or more levels), the 3D view the same size beside that, the panel after;
// the faders across the bottom. The map and the view shrink together to
// leave the panel and the faders their room.
void ofApp::layoutWindow() {
    const float W = float(ofGetWidth()), H = float(ofGetHeight());
    const float column = model.map3d().count() > 1 ? kColumnRoom : 0.0f;
    const float band = std::max(kChartMin, std::min(kChartMax, H * kChartShare));
    chartTop = H - band;
    float side = chartTop - 24.0f - 16.0f;
    const float room = W - 24.0f - column - 24.0f - kOscPanelWidth;
    side = std::min(side, show3D ? (room - 24.0f) / 2.0f : room);
    side = std::max(side, 160.0f);
    mapArea = ofRectangle(24.0f, 24.0f, side, side);
    view3D = show3D ? ofRectangle(mapArea.getRight() + column, 24.0f, side, side)
                    : ofRectangle();
    panelX = (show3D ? view3D.getRight() : mapArea.getRight() + column) + 24.0f;
    if (renderer) renderer->setViewport(mapArea);
    chart = layoutChart(model.mapping(), 24.0f, chartTop, W - 48.0f, band - 20.0f,
                        ChartText{text.charWidth(), text.lineHeight()});
}

// The level the point is on or above becomes the current one, so the 3D
// view brightens it and the column fills its dot. Only for showing: what is
// sent is worked out from every level at the point's height either way.
void ofApp::followLevel() {
    const Levels& lv = model.map3d().levels();
    LevelID k = 0;
    for (LevelID j = 0; j < lv.count(); ++j) {
        if (lv.base(j) <= z) k = j;
    }
    if (k != model.currentLevel()) model.goToLevel(k);
}

Scene ofApp::scene() const {
    Scene s;
    s.model = &model;
    s.selection = &none;
    s.renderer = renderer.get();
    s.evaluation = &evaluation;
    s.text = &text;
    s.listeningPoint = point;
    s.listeningZ = z;
    s.auditioning = true;             // the point is always heard here
    s.mouse = mouse;                  // the faders' hover labels
    s.linked = &linkSet;
    return s;
}

// The map's outputs in channel order with their levels -- `lv`, smoothed --
// the node values, and the point.
OscFrame ofApp::frameNow(const ChartLevels& lv) const {
    OscFrame f;
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
    for (const auto& v : heardValues) f.values.push_back({v.first, v.second});
    f.hasPosition = true;
    f.position = {point.x, point.y, z};
    return f;
}

void ofApp::update() {
    const double now = ofGetElapsedTimef();
    if (glideDir != 0) {
        z = glide(z, glideDir, ofGetKeyPressed(OF_KEY_SHIFT), float(ofGetLastFrameTime()));
    }
    followLevel();
    text.fit();
    layoutWindow();
    evaluation = evaluator->evaluate(point);

    // Whose links the map draws: the nodes the point is hearing, or all.
    linkSet.clear();
    if (links == 0) {
        for (const auto& wn : evaluation.weights) if (wn.weight > 0.0f) linkSet.toggle(wn.id);
    } else if (links == 1) {
        linkSet.selectAll(model.manifold().nodeCount());
    }

    // Answers: describe, hello.
    for (int k = 0; k < 64 && listener.hasWaitingMessages(); ++k) {
        ofxOscMessage in;
        if (!listener.getNextMessage(in)) break;
        OscMessage m;
        if (fromOfx(in, m)) osc.receive(now, m);
    }

    // Smoothed every frame, sent or not, so the glide keeps time with the
    // clock rather than with the send rate.
    smooth(float(ofGetLastFrameTime()));
    const OscFrame frame = frameNow(heard);
    if (!paused) {
        const auto msgs = osc.frame(now, frame);
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
    layoutWindow();
    const Scene sc = scene();
    // The editor's map, nothing selected and no grid: shapes, halos, values,
    // the links chosen with l, the current output's nodes ringed, the point
    // and its lines, the level dots.
    GridLook look;
    look.kind = GridKind::Free;
    drawMap(sc, look, MapOverlay{});
    // The faders: what is sent, smoothed. Hover one for its numbers.
    drawChart(sc, chart, heard);
    if (show3D) {
        if (!aimed) { aimLevelView(cam, view3D); aimed = true; }
        cam.setControlArea(view3D);
        drawLevelView(sc, cam, view3D);
    }

    const double now = ofGetElapsedTimef();
    std::size_t bytes = 0;
    for (const auto& e : sentLog) bytes += e.second;
    float x = panelX, y = 40.0f;
    auto line = [&](std::string t, const ofColor& c) {
        if (y > chartTop - 12.0f) return;            // above the faders only
        if (t.size() > kOscPanelChars) t = t.substr(0, kOscPanelChars - 3) + "...";
        ofSetColor(c);
        ofDrawBitmapString(t, x, y);
        y += 16.0f;
    };
    const ofColor plain(200, 205, 215), dim(130, 136, 150);
    line("OSC to " + host + ":" + ofToString(sendPort), ofColor(235, 238, 245));
    line(listenPort ? "answers on " + ofToString(listenPort)
                    : "no answer port free (" + ofToString(kFirstAnswerPort) + " on)",
         listenPort ? plain : ofColor(255, 150, 120));
    line(osc.prefix() + "   " + ofToString(osc.rate(), 0) + " a second"
         + (paused ? "   PAUSED" : ""), ofColor(235, 238, 245));
    line(std::string("packed ") + (osc.packed() ? "on" : "off") + " (p)  per output "
         + (osc.perOutput() ? "on" : "off") + " (i)", plain);
    line(ofToString(sentLog.size()) + " messages, " + ofToString(bytes) + " bytes a second",
         plain);
    if (smoothOn) {
        const bool settling = !levelSmoother.settled() || !valueSmoother.settled();
        line("smoothing " + ofToString(halfLife, 2) + " s (s, - =, shift)"
             + (settling ? "  settling" : ""), plain);
    } else {
        line("smoothing off (s)", plain);
    }
    if (osc.connected(now)) {
        line("answered by " + (osc.peer().empty() ? std::string("a receiver") : osc.peer()),
             ofColor(120, 200, 160));
    } else {
        line("no answer yet -- fine: nothing waits for one", dim);
    }
    line("point " + ofToString(point.x, 3) + " " + ofToString(point.y, 3), plain);
    line("height " + ofToString(z, 3) + " (up / down)   level "
         + ofToString(model.currentLevel() + 1) + " of " + ofToString(model.map3d().count()),
         plain);
    {
        const char* whose[] = {"of the nodes heard", "of every node", "off"};
        std::string out = "none";
        const auto& sel = model.selectedOutputs();
        if (!sel.empty()) out = model.mapping().targetName(sel.front());
        line("output " + out + " (tab)   links " + whose[links] + " (l)", plain);
    }
    y += 8.0f;
    line("last send:", ofColor(235, 238, 245));
    for (std::size_t k = 0; k < lastSent.size() && k < 24; ++k) line(lastSent[k], dim);
    if (!message.empty() && ofGetElapsedTimef() < messageUntil) {
        // Under the status, over the list, so the faders never hide it.
        const float keep = y;
        y = 40.0f + 16.0f * 11.0f;
        ofSetColor(30, 30, 34);
        ofDrawRectangle(x - 4.0f, y - 12.0f, kOscPanelWidth + 8.0f, 18.0f);
        line(message, ofColor(255, 214, 90));
        y = keep;
    }
}

void ofApp::mousePressed(int x, int y, int) {
    // A fader picks its output: its nodes ringed in its colour on the map.
    const TargetID hit = chart.outputAt(float(x), float(y));
    if (hit != InvalidTarget) { model.pickOutput(hit); return; }
    if (renderer->viewport().inside(x, y)) point = renderer->toManifold({float(x), float(y)});
}

void ofApp::mouseMoved(int x, int y) { mouse = glm::vec2(float(x), float(y)); }

void ofApp::mouseDragged(int x, int y, int) {
    mouse = glm::vec2(float(x), float(y));
    if (renderer->viewport().inside(x, y)) point = renderer->toManifold({float(x), float(y)});
}

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
    else if (key == 's') {
        smoothOn = !smoothOn;
        applySmoothing();
        say(smoothOn ? "smoothing on, half-life " + ofToString(halfLife, 2) + " s"
                     : "smoothing off: every jump steps");
    }
    else if (key == '_' || key == '+') {
        // Shift: a hundredth of a second at a time, for between the steps.
        halfLife += key == '+' ? kFineStep : -kFineStep;
        halfLife = std::round(halfLife / kFineStep) * kFineStep;
        halfLife = std::max(kHalfLives[0], std::min(halfLife, kHalfLives[std::size(kHalfLives) - 1]));
        smoothOn = true;
        applySmoothing();
        say("smoothing half-life " + ofToString(halfLife, 2) + " s");
    }
    else if (key == '-' || key == '=') {
        const std::size_t n = sizeof kHalfLives / sizeof kHalfLives[0];
        std::size_t at = 0;
        while (at + 1 < n && kHalfLives[at] < halfLife - 1e-6f) ++at;
        if (key == '-' && at > 0) --at;
        if (key == '=' && at + 1 < n && kHalfLives[at] <= halfLife + 1e-6f) ++at;
        halfLife = kHalfLives[at];
        smoothOn = true;
        applySmoothing();
        say("smoothing half-life " + ofToString(halfLife, 2) + " s");
    }
    else if (key == '3' || key == '#') {
        show3D = !show3D;
        if (show3D) cam.enableMouseInput();
        else        cam.disableMouseInput();
        say(show3D ? "3D view on -- drag in it to turn it" : "3D view off -- 3 to show it");
    }
    else if (key == 'l') {
        links = (links + 1) % 3;
        const char* said[] = {"links of the nodes heard", "links of every node", "links off"};
        say(said[links]);
    }
    else if (key == OF_KEY_TAB) {
        model.cycleOutput(ofGetKeyPressed(OF_KEY_SHIFT) ? -1 : +1);
    }
    else if (key == 'e') { model.loadRoom(); adopt("room"); say("the room: floor, middle and top"); }
    else if (key == 'E') { model.loadExample(); adopt("example"); say("the editor's flat example"); }
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
    if (r.changed) {
        // The map's name: the pair's, without its folder.
        const std::string base = pairFor(d.getPath()).base;
        adopt(base.substr(base.find_last_of("/\\") + 1));
    }
}
