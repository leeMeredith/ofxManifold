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
// Every key, for the ? page: a group's title, then key and what it does.
struct OscKey { const char* key; const char* does; };
struct OscKeyGroup { const char* title; std::vector<OscKey> keys; };
const std::vector<OscKeyGroup>& keyGroups() {
    static const std::vector<OscKeyGroup> groups = {
        {"the point", {{"drag", "move it (with your hand)"},
                       {"up / down", "hold: through the levels (shift: slowly)"},
                       {"shift-click", "follow a node; on none: the loudest"},
                       {"click a fader", "that output's nodes ringed"},
                       {"tab", "the next output (shift: the last)"}}},
        {"movement", {{"m", "your hand, the recording, jump, glide"},
                      {"M", "your hand again, at once"},
                      {"r", "record (stops anything moving); r: stop"},
                      {", .", "a step more / less often"},
                      {"< >", "travel shorter / longer (glide)"},
                      {"{ }", "steps shorter / longer"},
                      {"c", "steps on a timer, clicks, the bridge's beats"},
                      {"return", "one step (with clicks; or click the map)"},
                      {"h", "height stays / across the levels"},
                      {"n", "a new seed"},
                      {"b", "clock: seconds / SuperCollider's bars"}}},
        {"sending", {{"space", "pause sending"},
                     {"p", "packed messages on / off"},
                     {"i", "per-output messages on / off"},
                     {"[ ]", "send rate down / up"}}},
        {"smoothing", {{"s", "on / off"},
                       {"- =", "half-life a step shorter / longer"},
                       {"shift - =", "a hundredth of a second"}}},
        {"showing", {{"3", "the 3D view (drag in it to turn it)"},
                     {"l", "links: followed, heard, all, none"},
                     {"?", "this page (again, or a click: away)"}}},
        {"maps", {{"o", "open a map you saved in the editor"},
                  {"e", "the room: three levels"},
                  {"E", "the editor's flat example"}}},
    };
    return groups;
}

// Movement's settings, stepped through by keys, in the clock's units
// (seconds, or bars on SuperCollider's clock).
constexpr double kIntervals[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
constexpr double kTravels[]   = {0.05, 0.1, 0.2, 0.4, 0.8, 1.6};
constexpr double kSteps[]     = {0.0, 0.5, 0.25, 0.1, 0.05};     // 0: anywhere
template <class T, std::size_t N> int stepIn(const T (&)[N], int at, int by) {
    return std::max(0, std::min(int(N) - 1, at + by));
}

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

    // ofxScPhaseBridge's port: /phase and /beat from bridge.scd. Not shared:
    // if the bridge's own example app has it, this says so and goes on.
    {
        ofxOscReceiverSettings s;
        s.port = kBridgePort;
        s.reuse = false;
        bridgeOpen = bridge.setup(s);
    }

    model.loadRoom();
    adopt("room");
    cam.enableMouseInput();
}

double ofApp::count() const {
    return onPhase ? phase.count() : seconds.count(appSeconds);
}

std::string ofApp::movingName() const {
    switch (moving) {
    case Moving::Hand:      return "your hand";
    case Moving::Recording: return "the recording";
    case Moving::Jump:      return "jump";
    case Moving::Glide:     return "glide";
    }
    return "";
}

// The walk again, from where the point is now, with the settings as they are.
// Its count starts now; it stays on the map and never crosses off it.
void ofApp::startMoving() {
    movedFrom = count();
    if (moving != Moving::Jump && moving != Moving::Glide) return;
    WalkSettings s;
    // The seed and how many times the walk has started again: the same seed
    // starting again from where the first step went would only go there
    // again, so each start draws afresh -- still the same sequence of walks
    // for the same seed and keys, every run.
    s.seed = SplitMix64(seed * 0x100000001B3ULL + ++restarts).next();
    s.start = glm::dvec3(point.x, point.y, z);
    s.steps = steps == Steps::Timer ? StepTiming::every(kIntervals[intervalAt])
                                    : StepTiming::clicks();
    s.travel = moving == Moving::Jump ? 0.0 : kTravels[travelAt];
    s.easing = Easing::Smooth;
    s.step = kSteps[stepAt];
    const Levels& lv = model.map3d().levels();
    if (heightAcross && lv.count() > 1) {
        s.zLow = lv.base(0);
        s.zHigh = lv.base(lv.count() - 1);
    } else {
        s.zLow = s.zHigh = z;
    }
    s.insideMap = true;
    s.straight = true;
    walk = RandomWalk(s);
    // An evaluator of its own: the app's keeps track of which region the
    // point was last in, and the walk's tries must not disturb that.
    auto ev = std::make_shared<Evaluator>(model.manifold());
    walk.setInside([ev](glm::dvec2 p) {
        return ev->evaluate(glm::vec2(float(p.x), float(p.y))).inside;
    });
}

// A click or a beat: one step, at the clock's count.
void ofApp::step() {
    if (moving == Moving::Jump || moving == Moving::Glide) walk.addStep(count() - movedFrom);
}

// /phase into the phase clock; /beat, with beats as the steps, one step.
void ofApp::readBridge(double now) {
    if (!bridgeOpen) return;
    for (int k = 0; k < 256 && bridge.hasWaitingMessages(); ++k) {
        ofxOscMessage in;
        if (!bridge.getNextMessage(in)) break;
        OscMessage m;
        if (!fromOfx(in, m)) continue;
        if (readPhase(m, phase, now)) continue;
        int beat = 0;
        if (readBeat(m, beat) && steps == Steps::Beats) step();
    }
}

void ofApp::adopt(const std::string& name) {
    recording = false;
    pinned = InvalidNode;             // another map's nodes: follow the loudest
    followed.clear();
    osc.setMapName(name);             // its addresses, /manifold/<name>/...
    evaluator = std::make_unique<Evaluator>(model.manifold());
    renderer = std::make_unique<ofxManifoldRenderer>(model.manifold());
    evaluation = evaluator->evaluate(point);
    z = listenStart(model, evaluation);
    osc.reset();                      // the new map's outputs, described at once
    levelSmoother.reset();            // another map: taken at once, not glided into
    valueSmoother.reset();
    startMoving();                    // a walk on this map's own bounds
}

void ofApp::applySmoothing() {
    const float h = smoothOn ? halfLife : 0.0f;
    levelSmoother.setHalfLife(h);
    valueSmoother.setHalfLife(h);
}

// The map at the point, smoothed. The levels go through as one list --
// routed by channel, after trim by channel, the followed node's part by
// channel, then silence -- so a fader's bar, its trim tick, its bright band
// and the silence bar glide together. Its length is fixed by the map's
// channels: the followed node's part is always there, zeros when nothing is
// followed, so changing who is followed never makes the smoother start over
// (which would be a jump in the sound). The values are a second list.
void ofApp::smooth(float dt) {
    const ChartLevels raw = chartLevels3D(model, followed, evaluation, z);
    const std::size_t n = raw.routed.size(), m = raw.levels.size();
    std::vector<float> part(raw.fromSelection);
    part.resize(n, 0.0f);
    std::vector<float> v(raw.routed);
    v.insert(v.end(), raw.levels.begin(), raw.levels.end());
    v.insert(v.end(), part.begin(), part.end());
    v.push_back(raw.silence);
    const std::vector<float>& s = levelSmoother.update(v, dt);
    heard = raw;
    auto at = [&](std::size_t k) { return s.begin() + std::ptrdiff_t(k); };
    heard.routed.assign(at(0), at(n));
    heard.levels.assign(at(n), at(n + m));
    heard.fromSelection.assign(at(n + m), at(n + m + n));
    heard.silence = s[n + m + n];

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

// The node followed: pinned, while it is on the map, else the loudest at the
// point; nothing off the map.
void ofApp::follow() {
    NodeID id = InvalidNode;
    if (pinned != InvalidNode && pinned < model.manifold().nodeCount()) {
        id = pinned;
    } else {
        pinned = InvalidNode;
        float most = 0.0f;
        for (const auto& wn : evaluation.weights) {
            if (wn.weight > most) { most = wn.weight; id = wn.id; }
        }
    }
    if (followed.size() == 1 && followed.contains(id)) return;
    followed.clear();
    if (id != InvalidNode) followed.toggle(id);
}

// The node under the mouse, as the editor finds it: within 10 pixels.
NodeID ofApp::nodeAt(glm::vec2 screen) const {
    const Manifold2D& mf = model.manifold();
    for (std::size_t i = 0; i < mf.nodeCount(); ++i) {
        const glm::vec2 p = renderer->toScreen(mf.node(NodeID(i)).position);
        if (glm::distance(p, screen) <= 10.0f) return NodeID(i);
    }
    return InvalidNode;
}

Scene ofApp::scene() const {
    Scene s;
    s.model = &model;
    s.selection = &followed;
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
    appSeconds += ofGetLastFrameTime();
    readBridge(now);

    // Moving itself: the point is where the movement says at the count.
    if (moving == Moving::Recording && hasRecording) {
        const glm::vec3 p = recorded.at(count() - movedFrom);
        point = glm::vec2(p.x, p.y);
        z = p.z;
    } else if (moving == Moving::Jump || moving == Moving::Glide) {
        const glm::vec3 p = walk.at(count() - movedFrom);
        point = glm::vec2(p.x, p.y);
        z = p.z;
    } else if (glideDir != 0) {
        z = glide(z, glideDir, ofGetKeyPressed(OF_KEY_SHIFT), float(ofGetLastFrameTime()));
    }
    // Recording takes the point wherever it is, on the clock running.
    if (recording) taking.addSample(float(count() - takingFrom), glm::vec3(point.x, point.y, z));
    followLevel();
    text.fit();
    layoutWindow();
    evaluation = evaluator->evaluate(point);

    follow();

    // Whose links the map draws: the node followed, the nodes the point is
    // hearing, or all.
    linkSet.clear();
    if (links == 0) {
        for (NodeID id : followed.nodes()) linkSet.toggle(id);
    } else if (links == 1) {
        for (const auto& wn : evaluation.weights) if (wn.weight > 0.0f) linkSet.toggle(wn.id);
    } else if (links == 2) {
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
    drawPaths();
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
    line("? for every key", ofColor(255, 214, 90));
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
        // Movement.
        line("moving: " + movingName() + ", seed " + ofToString(seed) + " (m n)", plain);
        if (moving == Moving::Jump || moving == Moving::Glide) {
            line("every " + ofToString(kIntervals[intervalAt]) + ", travel "
                 + (moving == Moving::Jump ? std::string("none") : ofToString(kTravels[travelAt]))
                 + " (, . < >)", plain);
            line("step " + (kSteps[stepAt] > 0.0 ? ofToString(kSteps[stepAt]) : std::string("anywhere"))
                 + ", height " + (heightAcross ? "across levels" : "stays") + " ({ } h)", plain);
            const char* when[] = {"steps on a timer", "steps on your clicks", "steps on the bridge's beats"};
            line(std::string(when[int(steps)]) + " (c)", plain);
        }
        if (recording) {
            line("recording " + ofToString(count() - takingFrom, 1) + " (r: stop)", ofColor(255, 150, 120));
        } else if (hasRecording) {
            line("a recording, " + ofToString(recorded.path().duration(), 1)
                 + (recordedOnPhase ? " bars" : " s") + ", loops (r: again)", plain);
        }
        if (onPhase) {
            line(phase.heard(now) ? "clock: bar " + ofToString(phase.bar()) + ", phase "
                                        + ofToString(phase.phase(), 2) + " (b)"
                                  : std::string("clock: bars -- phase not heard (b)"),
                 phase.heard(now) ? plain : ofColor(255, 150, 120));
        } else {
            line("clock: seconds, " + ofToString(count(), 1) + " (b)", plain);
        }
        if (!bridgeOpen) line("port 9001 taken: no bridge here", dim);

        const char* whose[] = {"followed", "heard", "all", "off"};
        std::string out = "none";
        const auto& sel = model.selectedOutputs();
        if (!sel.empty()) out = model.mapping().targetName(sel.front());
        line("output " + out + " (tab)   links " + whose[links] + " (l)", plain);
        std::string who = "nothing";
        if (!followed.nodes().empty()) who = model.manifold().node(followed.nodes().front()).name;
        line("following " + who + (pinned != InvalidNode ? ", pinned" : ", the loudest")
             + " (shift-click)", plain);
    }
    y += 8.0f;
    const float messageY = y;
    line("last send:", ofColor(235, 238, 245));
    for (std::size_t k = 0; k < lastSent.size() && k < 24; ++k) line(lastSent[k], dim);
    if (!message.empty() && ofGetElapsedTimef() < messageUntil) {
        // Under the status, over the list, so the faders never hide it.
        const float keep = y;
        y = messageY;
        ofSetColor(30, 30, 34);
        ofDrawRectangle(x - 4.0f, y - 12.0f, kOscPanelWidth + 8.0f, 18.0f);
        line(message, ofColor(255, 214, 90));
        y = keep;
    }
    if (keySheet) drawKeySheet();
}

// The path being recorded, bright, as it grows; the recording playing, dim,
// the whole loop. On the map, at the floor plan: heights are in the 3D view.
void ofApp::drawPaths() const {
    auto path = [&](const Trajectory& tr, const ofColor& c, float width) {
        if (tr.sampleCount() < 2) return;
        ofSetColor(c);
        ofSetLineWidth(width);
        for (std::size_t i = 1; i < tr.sampleCount(); ++i) {
            const glm::vec2 a = renderer->toScreen(tr.sample(i - 1).position);
            const glm::vec2 b = renderer->toScreen(tr.sample(i).position);
            ofDrawLine(a.x, a.y, b.x, b.y);
        }
        ofSetLineWidth(1.0f);
    };
    if (recording) {
        path(taking, ofColor(255, 110, 90), 3.0f);
        // REC, by the map's top left corner.
        ofSetColor(255, 80, 70);
        ofFill();
        ofDrawCircle(mapArea.getX() + 14.0f, mapArea.getY() + 14.0f, 6.0f);
        ofDrawBitmapString("REC -- r to stop", mapArea.getX() + 26.0f, mapArea.getY() + 19.0f);
    } else if (moving == Moving::Recording && hasRecording) {
        path(recorded.path(), ofColor(255, 150, 120, 140), 2.0f);
    }
}

// Every key on one page, in groups, over everything else.
void ofApp::drawKeySheet() const {
    const float W = float(ofGetWidth()), H = float(ofGetHeight());
    ofSetColor(18, 18, 22, 235);
    ofFill();
    ofDrawRectangle(0.0f, 0.0f, W, H);
    float x = 40.0f, y = 50.0f;
    ofSetColor(255, 214, 90);
    ofDrawBitmapString("every key -- ? again, or a click, to go back", x, y);
    y += 32.0f;
    const float top = y, column = 480.0f;   // the longest line is 464 pixels
    for (const OscKeyGroup& g : keyGroups()) {
        const float need = 22.0f + 16.0f * float(g.keys.size()) + 14.0f;
        if (y + need > H - 30.0f && y > top) { y = top; x += column; }
        ofSetColor(150, 200, 255);
        ofDrawBitmapString(g.title, x, y);
        y += 22.0f;
        for (const OscKey& k : g.keys) {
            ofSetColor(255, 255, 255);
            ofDrawBitmapString(k.key, x, y);
            ofSetColor(200, 205, 215);
            ofDrawBitmapString(k.does, x + 112.0f, y);
            y += 16.0f;
        }
        y += 14.0f;
    }
}

void ofApp::mousePressed(int x, int y, int) {
    if (keySheet) { keySheet = false; return; }    // a click: back to the map
    // A fader picks its output: its nodes ringed in its colour on the map.
    const TargetID hit = chart.outputAt(float(x), float(y));
    if (hit != InvalidTarget) { model.pickOutput(hit); return; }
    // Moving by itself: a click on the map is a step (with clicks), or puts
    // the walk somewhere new to go on from.
    if (moving != Moving::Hand && !ofGetKeyPressed(OF_KEY_SHIFT)
        && renderer->viewport().inside(x, y)) {
        if (moving != Moving::Recording && steps == Steps::Clicks) { step(); return; }
        point = renderer->toManifold({float(x), float(y)});
        if (moving != Moving::Recording) startMoving();
        return;
    }
    // Shift-click on the map: follow that node, or the loudest again.
    if (ofGetKeyPressed(OF_KEY_SHIFT) && renderer->viewport().inside(x, y)) {
        pinned = nodeAt(glm::vec2(float(x), float(y)));
        follow();
        message = pinned != InvalidNode ? "following " + model.manifold().node(pinned).name + ", pinned"
                                        : "following the loudest node";
        messageUntil = ofGetElapsedTimef() + 2.5f;
        return;
    }
    if (renderer->viewport().inside(x, y)) point = renderer->toManifold({float(x), float(y)});
}

void ofApp::mouseMoved(int x, int y) { mouse = glm::vec2(float(x), float(y)); }

void ofApp::mouseDragged(int x, int y, int) {
    mouse = glm::vec2(float(x), float(y));
    if (ofGetKeyPressed(OF_KEY_SHIFT)) return;      // a pin, not a move
    if (moving != Moving::Hand) return;             // it moves itself
    if (renderer->viewport().inside(x, y)) point = renderer->toManifold({float(x), float(y)});
}

void ofApp::keyPressed(int key) {
    auto say = [&](const std::string& t) {
        message = t;
        messageUntil = ofGetElapsedTimef() + 2.5f;
    };
    if (key == '?' || key == '/') { keySheet = !keySheet; return; }
    if (keySheet) return;             // the page is up: keys wait for it to go
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
        links = (links + 1) % 4;
        const char* said[] = {"links of the node followed", "links of the nodes heard",
                              "links of every node", "links off"};
        say(said[links]);
    }
    else if (key == OF_KEY_TAB) {
        model.cycleOutput(ofGetKeyPressed(OF_KEY_SHIFT) ? -1 : +1);
    }
    else if (key == 'e') { model.loadRoom(); adopt("room"); say("the room: floor, middle and top"); }
    else if (key == 'E') { model.loadExample(); adopt("example"); say("the editor's flat example"); }
    else if (key == 'o') open();
    // ---- movement ----
    else if (key == 'm') {
        moving = moving == Moving::Hand      ? (hasRecording ? Moving::Recording : Moving::Jump)
               : moving == Moving::Recording ? Moving::Jump
               : moving == Moving::Jump      ? Moving::Glide
                                             : Moving::Hand;
        startMoving();
        say("moving: " + movingName());
    }
    else if (key == 'M') {
        recording = false;
        moving = Moving::Hand;
        startMoving();
        say("your hand: the point stays where it is");
    }
    else if (key == 'r') {
        if (!recording) {
            // Whatever was moving stops where it is, and your hand takes over:
            // a recording is of what you do, never of the last one playing.
            moving = Moving::Hand;
            startMoving();
            taking.clear();
            takingFrom = count();
            recording = true;
            say("recording -- drag the point; r again to stop");
        } else {
            recording = false;
            taking.finalize();
            if (taking.sampleCount() > 1) {
                recorded = RecordedMotion(taking, RecordedMotion::End::Loop);
                hasRecording = true;
                recordedOnPhase = onPhase;
                moving = Moving::Recording;
                startMoving();
                say("recorded: it loops (m for the others)");
            } else {
                say("nothing recorded");
            }
        }
    }
    else if (key == ',' || key == '.') {
        intervalAt = stepIn(kIntervals, intervalAt, key == '.' ? 1 : -1);
        startMoving();
        say("a step every " + ofToString(kIntervals[intervalAt]));
    }
    else if (key == '<' || key == '>') {
        travelAt = stepIn(kTravels, travelAt, key == '>' ? 1 : -1);
        startMoving();
        say("travel " + ofToString(kTravels[travelAt]));
    }
    else if (key == '{' || key == '}') {
        // { shorter steps (further down the list), } longer, up to anywhere.
        stepAt = stepIn(kSteps, stepAt, key == '{' ? 1 : -1);
        startMoving();
        say(kSteps[stepAt] > 0.0 ? "steps of up to " + ofToString(kSteps[stepAt])
                                 : std::string("steps anywhere on the map"));
    }
    else if (key == 'c') {
        steps = steps == Steps::Timer ? Steps::Clicks
              : steps == Steps::Clicks ? Steps::Beats : Steps::Timer;
        startMoving();
        const char* said[] = {"steps on a timer", "steps on your clicks (or return)",
                              "steps on the bridge's beats"};
        say(said[int(steps)]);
    }
    else if (key == OF_KEY_RETURN) {
        if (steps == Steps::Clicks) step();
    }
    else if (key == 'h') {
        heightAcross = !heightAcross;
        startMoving();
        say(heightAcross ? "height: random across the levels" : "height: stays");
    }
    else if (key == 'n') {
        ++seed;
        startMoving();
        say("seed " + ofToString(seed));
    }
    else if (key == 'b') {
        onPhase = !onPhase;
        startMoving();                // the count changes units: start again
        say(onPhase ? (phase.heard(ofGetElapsedTimef()) ? "the clock: SuperCollider's bars"
                                                        : "the clock: bars -- no phase heard yet on 9001")
                    : "the clock: seconds");
    }
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
