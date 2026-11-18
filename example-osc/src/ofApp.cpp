#include "ofApp.h"

#include <fstream>
#include <sstream>

using namespace ofxManifold;
using namespace ofxManifold::editor;
using namespace ofxManifold::player;

namespace {

// The panel's room beside the views, in characters of the panel's text.
constexpr std::size_t kOscPanelChars = 44;
// Room for the column of level dots and its label, right of the map.
constexpr float       kColumnRoom = 70.0f;
// The half-lives - and = step through, in seconds; shift steps by kFineStep
// between the first and the last.
constexpr float kHalfLives[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f};
constexpr float kFineStep = 0.01f;
// The faders' band along the bottom: this share of the window, within limits.
constexpr float kChartShare = 0.24f, kChartMin = 130.0f, kChartMax = 220.0f;
// A press this close to a point's dot chooses it (3B-2g).
constexpr float kDotReach = 12.0f;

// Movement's settings, stepped through by keys, in the clock's units
// (seconds, or bars on SuperCollider's clock).
constexpr double kIntervals[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
constexpr double kTravels[]   = {0.05, 0.1, 0.2, 0.4, 0.8, 1.6};
constexpr double kSteps[]     = {0.0, 0.5, 0.25, 0.1, 0.05};     // 0: anywhere
// The arrows on the levels: levels a second (shift: a quarter of that).
constexpr float kLevelsPerSecond = 1.0f, kLevelsPerSecondFine = 0.25f;
template <class T, std::size_t N> int stepIn(const T (&)[N], int at, int by) {
    return std::max(0, std::min(int(N) - 1, at + by));
}

// The sections' colours (3B-2g): each its own, the same on the panel and
// the ? page.
const KeyColour kSending{150, 200, 255}, kPoints{255, 190, 110},
    kThePoint{120, 215, 170}, kMovement{190, 170, 255}, kSmoothing{110, 215, 225},
    kOutside{240, 220, 120}, kShowing{235, 150, 210}, kMaps{185, 190, 205},
    kLastSend{130, 136, 150};

// A row of a section: a key and what it does, or (state) what is true now
// and the key that changes it. `on` false: drawn dim.
KeyEntry row(const std::string& keys, const std::string& what, bool state = false,
             bool on = true) {
    KeyEntry e;
    e.keys = keys;
    e.action = what;
    e.available = on;
    e.state = state;
    return e;
}
KeyEntry now(const std::string& keys, const std::string& what, bool on = true) {
    return row(keys, what, true, on);
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

    points.emplace_back();
    model.loadRoom();
    adopt("room");
    cam.enableMouseInput();
}

void ofApp::say(const std::string& t, float seconds) {
    message = t;
    messageUntil = ofGetElapsedTimef() + seconds;
}

double ofApp::count() const {
    return onPhase ? phase.count() : seconds.count(appSeconds);
}

std::string ofApp::movingName(const PlayerPoint& p) const {
    switch (p.moving) {
    case Moving::Hand:      return "your hand";
    case Moving::Recording: return "the recording";
    case Moving::Jump:      return "jump";
    case Moving::Glide:     return "glide";
    }
    return "";
}

// Who drives point k, in a word or two, for the panel's list.
std::string ofApp::driverName(std::size_t k) const {
    const PlayerPoint& p = points[k];
    if (p.outsideHeld) return "from outside, " + control.holder(p.number).str();
    if (recording && recordingPoint == k) return "recording";
    std::string s = p.moving == Moving::Hand ? "yours" : movingName(p);
    if (p.heightWalks()) s += p.heightBy == HeightBy::Jump ? ", height jumps" : ", height glides";
    return s;
}

// ---- points (3B-2g) ----

ofColor ofApp::pointColour(int number) {
    static const ofColor c[] = {
        {120, 200, 160}, {255, 150, 90},  {120, 170, 255}, {235, 120, 200},
        {230, 220, 100}, {160, 120, 255}, {90, 215, 225},  {255, 110, 110},
        {180, 230, 120}, {255, 190, 150}, {150, 200, 255}, {240, 170, 230},
        {200, 190, 140}, {200, 170, 255}, {150, 235, 215}, {255, 170, 170}};
    return c[std::size_t(std::max(0, number - 1)) % (sizeof c / sizeof c[0])];
}

// A point at xy, at level position u, by hand: its height from the
// surfaces there.
ofApp::PlayerPoint ofApp::newPoint(int number, glm::vec2 xy, float u) const {
    PlayerPoint p;
    p.number = number;
    p.xy = xy;
    p.evaluator = std::make_shared<Evaluator>(model.manifold());
    p.evaluation = p.evaluator->evaluate(p.xy);
    const Levels& lv = model.map3d().levels();
    p.u = std::min(std::max(u, 0.0f), topLevel());
    p.z = lv.heightAtPosition(floorWeights(p), p.u, heightIn == HeightIn::Past);
    if (heightIn == HeightIn::Units) p.u = lv.positionAt(floorWeights(p), p.z, true);
    const float h = smoothOn ? halfLife : 0.0f;
    p.levelSmoother.setHalfLife(h);
    p.valueSmoother.setHalfLife(h);
    return p;
}

void ofApp::describePoints() {
    std::vector<OscOut::PointInfo> pts;
    for (int k = 0; k < control.count(); ++k) {
        const PointSpec& p = control.spec().points[std::size_t(k)];
        pts.push_back({k + 1, p.name, PointControl::inletNames(p.inlets)});
    }
    osc.setPoints(pts);
}

// a: a point after the last, at the mouse if it is over the map, else the
// middle, at the chosen point's level position; it becomes the chosen one.
void ofApp::addPoint() {
    if (recording) { say("recording point " + ofToString(cur().number) + " -- r to stop first"); return; }
    if (int(points.size()) >= kMaxPoints || !control.addPoint()) {
        say("sixteen points: no more");
        return;
    }
    const glm::vec2 at = renderer->viewport().inside(mouse.x, mouse.y)
                       ? renderer->toManifold(mouse) : glm::vec2(0.5f, 0.5f);
    points.push_back(newPoint(int(points.size()) + 1, at, cur().u));
    startMoving(points.back());
    chosen = points.size() - 1;
    describePoints();
    say("point " + ofToString(cur().number) + " added, chosen -- left / right to choose");
}

// A: the last point goes -- only the last, so no number changes under a
// receiver or a sender. Its holder outside is told.
void ofApp::removePoint() {
    if (points.size() <= 1) { say("point 1 stays"); return; }
    const std::size_t last = points.size() - 1;
    if (recording && recordingPoint == last) { recording = false; taking.clear(); }
    const bool held = points[last].outsideHeld;
    control.removeLast();
    applyControl();                   // its holder told, and the panel says so
    points.pop_back();
    if (chosen >= points.size()) chosen = points.size() - 1;
    describePoints();
    if (!held) say("point " + ofToString(int(last) + 1) + " removed");
}

void ofApp::choose(std::size_t k) {
    if (k >= points.size() || k == chosen) return;
    if (recording) { say("recording point " + ofToString(cur().number) + " -- r to stop first"); return; }
    chosen = k;
    glideDir = 0;
    say("point " + ofToString(cur().number) + " chosen: " + driverName(chosen));
}

int ofApp::dotAt(glm::vec2 screen) const {
    if (points.size() < 2) return -1;     // one point: a press moves it, as always
    int best = -1;
    float nearest = kDotReach;
    for (std::size_t k = 0; k < points.size(); ++k) {
        const float d = glm::distance(renderer->toScreen(points[k].xy), screen);
        if (d <= nearest) { nearest = d; best = int(k); }
    }
    return best;
}

// ---- movement ----

// The walks again, from where the point is now, with the settings as they
// are. Their counts start now; the floor's stays on the map and never crosses
// off it; the height's is its own.
void ofApp::startMoving(PlayerPoint& p) {
    p.movedFrom = count();
    startHeight(p);
    if (!p.floorWalks()) return;
    WalkSettings s;
    // The seed and how many times the walk has started again: the same seed
    // starting again from where the first step went would only go there
    // again, so each start draws afresh -- still the same sequence of walks
    // for the same seed and keys, every run. Each point draws its own.
    s.seed = SplitMix64(p.seed * 0x100000001B3ULL + ++p.restarts
                        + std::uint64_t(p.number - 1) * 0x9E3779B97F4A7C15ULL).next();
    s.start = glm::dvec3(p.xy.x, p.xy.y, p.z);
    s.steps = steps == Steps::Timer ? StepTiming::every(kIntervals[intervalAt])
                                    : StepTiming::clicks();
    s.travel = p.moving == Moving::Jump ? 0.0 : kTravels[p.travelAt];
    s.easing = Easing::Smooth;
    s.step = kSteps[p.stepAt];
    s.zLow = s.zHigh = p.z;           // the floor's walk: its height is not used
    s.insideMap = true;
    s.straight = true;
    p.walk = RandomWalk(s);
    // An evaluator of its own: the point's keeps track of which region it
    // was last in, and the walk's tries must not disturb that.
    auto ev = std::make_shared<Evaluator>(model.manifold());
    p.walk.setInside([ev](glm::dvec2 q) {
        return ev->evaluate(glm::vec2(float(q.x), float(q.y))).inside;
    });
}

// The height's own walk: the same timing and travel as the floor's (one set
// of keys), its own draws, the map's levels for its range; x and y held
// still, so only its height moves. A map with one level has no range: the
// height stays.
void ofApp::startHeight(PlayerPoint& p) {
    p.heightFrom = count();
    if (!p.heightWalks()) return;
    WalkSettings s;
    s.seed = SplitMix64((p.seed ^ 0xA0761D6478BD642FULL) * 0x100000001B3ULL + ++p.restarts
                        + std::uint64_t(p.number - 1) * 0x9E3779B97F4A7C15ULL).next();
    s.start = glm::dvec3(0.5, 0.5, 0.0);
    s.steps = steps == Steps::Timer ? StepTiming::every(kIntervals[intervalAt])
                                    : StepTiming::clicks();
    s.travel = p.heightBy == HeightBy::Jump ? 0.0 : kTravels[p.travelAt];
    s.easing = Easing::Smooth;
    s.x0 = s.x1 = 0.5;
    s.y0 = s.y1 = 0.5;
    // On the levels (or past them), it walks the level position across the
    // levels -- always somewhere real, riding the bends. In units, across the
    // levels' flat bases, as before.
    if (heightIn == HeightIn::Units) {
        const Levels& lv = model.map3d().levels();
        s.start.z = p.z;
        s.zLow = lv.base(0);
        s.zHigh = lv.base(lv.count() - 1);
    } else {
        s.start.z = p.u;
        s.zLow = 0.0;
        s.zHigh = topLevel();
    }
    p.heightWalk = RandomWalk(s);
}

void ofApp::startEvery() {
    for (PlayerPoint& p : points) startMoving(p);
}

// A click or a beat: one step, at the clock's count -- of every point's
// floor walk and height walk, whichever are walking, so they keep time.
void ofApp::step() {
    for (PlayerPoint& p : points) {
        if (p.floorWalks()) p.walk.addStep(count() - p.movedFrom);
        if (p.heightWalks()) p.heightWalk.addStep(count() - p.heightFrom);
    }
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

void ofApp::finishRecording() {
    recording = false;
    taking.finalize();
    PlayerPoint& p = points[recordingPoint];
    if (taking.sampleCount() > 1) {
        p.recorded = RecordedMotion(taking, RecordedMotion::End::Loop);
        p.hasRecording = true;
        p.recordedOnPhase = onPhase;
        p.recordedInLevels = heightIn != HeightIn::Units;
        p.moving = Moving::Recording;
        startMoving(p);
        say("recorded: it loops (m for the others)");
    } else {
        say("nothing recorded");
    }
}

void ofApp::adopt(const std::string& name) {
    recording = false;
    pinned = InvalidNode;             // another map's nodes: follow the loudest
    followed.clear();
    osc.setMapName(name);             // its addresses, /manifold/<name>/...
    // A built-in map, or one just opened: no control file until open() reads
    // one. Point 1 alone, taking every inlet; any hold from outside ends.
    controlSpec = ControlSpec{};
    controlPath.clear();
    renderer = std::make_unique<ofxManifoldRenderer>(model.manifold());
    // Nodes are drawn here, by the editor's glyphs -- not the renderer too,
    // or every name is drawn twice, a hair apart (3B-2b).
    renderer->style.drawNodes = false;
    // Point 1 where it was, at the map's own starting height; the others go
    // with the old map.
    const glm::vec2 at = points.front().xy;
    PlayerPoint first = newPoint(1, at, 0.0f);
    first.z = listenStart(model, first.evaluation);
    first.u = model.map3d().levels().positionAt(floorWeights(first), first.z,
                                                heightIn == HeightIn::Past);
    points.assign(1, first);
    chosen = 0;
    osc.reset();                      // the new map's outputs, described at once
    startMoving(points.front());      // a walk on this map's own bounds
    useControl();
}

// ---- points moved from outside (3B-2f) ----

// The spec to the control, and as many points as it has: a map's control
// file says how many it opens with (3B-2g). Points beyond the first, when
// the file brings them, round the middle of the map.
void ofApp::useControl() {
    control.setPrefix(osc.prefix());
    control.setSpec(controlSpec);
    // An evaluator of its own, as the walk has: the points' must not be
    // disturbed by a move it then refuses.
    auto ev = std::make_shared<Evaluator>(model.manifold());
    control.setInside([ev](float x, float y) { return ev->evaluate(glm::vec2(x, y)).inside; });
    const Levels& lv = model.map3d().levels();
    control.setExtent(lv.base(0), lv.base(LevelID(lv.count() - 1)), topLevel());
    for (PlayerPoint& p : points) p.outsideHeld = false;
    if (recording && recordingPoint >= std::size_t(control.count())) recording = false;
    points.resize(std::min(points.size(), std::size_t(control.count())));
    Evaluator probe(model.manifold());
    while (int(points.size()) < control.count()) {
        const int n = int(points.size()) + 1;
        const float a = float(n - 2) * 2.399963f;          // the golden angle
        glm::vec2 at(0.5f + 0.15f * std::cos(a), 0.5f + 0.15f * std::sin(a));
        if (!probe.evaluate(at).inside) at = glm::vec2(0.5f, 0.5f);
        points.push_back(newPoint(n, at, points.front().u));
        startMoving(points.back());
    }
    if (chosen >= points.size()) chosen = 0;
    describePoints();
}

void ofApp::readAnswers(double now) {
    for (int k = 0; k < 64 && listener.hasWaitingMessages(); ++k) {
        ofxOscMessage in;
        if (!listener.getNextMessage(in)) break;
        OscMessage m;
        if (!fromOfx(in, m)) continue;
        if (!control.receive(now, m, OscPeer{in.getRemoteHost(), in.getRemotePort()})) {
            osc.receive(now, m);          // describe, hello
        }
    }
    control.tick(now);
    applyControl();
}

void ofApp::applyControl() {
    for (const PointEvent& e : control.takeEvents()) {
        if (e.point < 1 || std::size_t(e.point) > points.size()) continue;
        PlayerPoint& p = points[std::size_t(e.point - 1)];
        const std::string n = "point " + ofToString(e.point);
        if (e.kind == PointEvent::Taken) {
            // Its own movement stops; what drove it is kept to go back to.
            p.movingBefore = p.moving;
            p.heightBefore = p.heightBy;
            if (recording && recordingPoint == std::size_t(e.point - 1)) recording = false;
            p.moving = Moving::Hand;
            p.heightBy = HeightBy::Hand;
            p.outsideHeld = true;
            startMoving(p);
            say(n + " taken from outside, by " + e.by.str(), 3.0f);
        } else {
            p.outsideHeld = false;
            if (e.why != "hand" && e.why != "removed") {
                p.moving = p.movingBefore;
                p.heightBy = p.heightBefore;
                startMoving(p);
            }
            say(e.why == "hand"    ? n + " back in your hand; " + e.by.str() + " told"
              : e.why == "silent"  ? n + " back: " + e.by.str() + " fell silent"
              : e.why == "removed" ? n + " removed; " + e.by.str() + " told"
                                   : n + " released by " + e.by.str(), 3.0f);
        }
    }
    const Levels& lv = model.map3d().levels();
    const bool past = heightIn == HeightIn::Past;
    for (const PointMove& m : control.takeMoves()) {
        if (m.point < 1 || std::size_t(m.point) > points.size()) continue;
        PlayerPoint& p = points[std::size_t(m.point - 1)];
        if (m.hasXY) p.xy = glm::vec2(m.x, m.y);
        if (!m.hasZ && !m.hasU) continue;
        const Evaluation e = p.evaluator->evaluate(p.xy);
        const std::vector<WeightedNode> w = e.inside ? e.weights : std::vector<WeightedNode>{};
        if (m.hasZ) {
            if (heightIn == HeightIn::Units) p.z = m.z;
            else p.u = lv.positionAt(w, m.z, past);
        }
        if (m.hasU) {
            if (heightIn == HeightIn::Units) p.z = lv.heightAtPosition(w, m.u, true);
            else p.u = m.u;
        }
    }
    for (const PointReply& r : control.takeReplies()) {
        const std::string key = r.to.str();
        auto it = replySenders.find(key);
        if (it == replySenders.end()) {
            auto s = std::make_unique<ofxOscSender>();
            s->setup(r.to.ip, r.to.port);
            it = replySenders.emplace(key, std::move(s)).first;
        }
        it->second->sendMessage(toOfx(r.message), false);
    }
}

void ofApp::writeControl() {
    if (controlPath.empty()) {
        say("a built-in map has no file: open one (o) to write its control file", 3.0f);
        return;
    }
    std::ofstream f(controlPath, std::ios::binary);
    f << saveControl(control.spec());
    say(f ? "wrote " + controlPath.substr(controlPath.find_last_of("/\\") + 1)
              + " -- " + ofToString(control.count())
              + (control.count() == 1 ? " point" : " points")
          : "could not write " + controlPath, 3.0f);
}

void ofApp::applySmoothing() {
    const float h = smoothOn ? halfLife : 0.0f;
    for (PlayerPoint& p : points) {
        p.levelSmoother.setHalfLife(h);
        p.valueSmoother.setHalfLife(h);
    }
}

// The map at a point, smoothed. The levels go through as one list -- routed
// by channel, after trim by channel, the followed node's part by channel,
// then silence -- so a fader's bar, its trim tick, its bright band and the
// silence bar glide together. Its length is fixed by the map's channels: the
// followed node's part is always there, zeros when nothing is followed (or
// for a point not chosen), so changing who is followed never makes the
// smoother start over (which would be a jump in the sound). The values are a
// second list.
void ofApp::smooth(PlayerPoint& p, float dt, bool following) {
    static const Selection none;
    const Evaluation3D e = heardAt(p);
    const ChartLevels raw = chartLevels3D(model, following ? followed : none, p.evaluation, e);
    const std::size_t n = raw.routed.size(), m = raw.levels.size();
    std::vector<float> part(raw.fromSelection);
    part.resize(n, 0.0f);
    std::vector<float> v(raw.routed);
    v.insert(v.end(), raw.levels.begin(), raw.levels.end());
    v.insert(v.end(), part.begin(), part.end());
    v.push_back(raw.silence);
    const std::vector<float>& s = p.levelSmoother.update(v, dt);
    p.heard = raw;
    auto at = [&](std::size_t k) { return s.begin() + std::ptrdiff_t(k); };
    p.heard.routed.assign(at(0), at(n));
    p.heard.levels.assign(at(n), at(n + m));
    p.heard.fromSelection.assign(at(n + m), at(n + m + n));
    p.heard.silence = s[n + m + n];

    p.heardValues = valuesAt(model, p.evaluation.inside ? e : Evaluation3D{});
    v.clear();
    for (const auto& q : p.heardValues) v.push_back(q.second);
    const std::vector<float>& sv = p.valueSmoother.update(v, dt);
    for (std::size_t k = 0; k < p.heardValues.size(); ++k) p.heardValues[k].second = sv[k];
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
    // With the keys hidden, the panel's room goes to the maps.
    const float panel = keys == Keys::Hidden ? 0.0f : float(kOscPanelChars) * text.charWidth();
    const float room = W - 24.0f - column - 24.0f - panel;
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

// The level the chosen point is on or above becomes the current one, so the
// 3D view brightens it and the column fills its dot. Only for showing: what
// is sent is worked out from every level at each point's height either way.
//
// The level is read from the level position -- from the surfaces under the
// point, bends and all -- not from the levels' flat bases.
void ofApp::followLevel() {
    const float top = topLevel();
    const float at = std::min(std::max(std::floor(cur().u + 1e-6f), 0.0f), top);
    const LevelID k = LevelID(at);
    if (k != model.currentLevel()) model.goToLevel(k);
}

// The floor plan's weights at a point; none off it (the levels are then
// taken flat, at their bases).
std::vector<WeightedNode> ofApp::floorWeights(const PlayerPoint& p) const {
    return p.evaluation.inside ? p.evaluation.weights : std::vector<WeightedNode>{};
}

float ofApp::topLevel() const { return float(model.map3d().count() - 1); }

// The 3D weights a point is heard by (3B-2d). On the levels it rides by its
// level position u, so it is read by u: no division, and where levels touch
// -- the octahedron's rim -- u still says how far up it is. In units it
// moves by height, and is read by height.
Evaluation3D ofApp::heardAt(const PlayerPoint& p) const {
    const Levels& lv = model.map3d().levels();
    return heightIn == HeightIn::Units ? lv.evaluate(p.evaluation, p.z)
                                       : lv.evaluateAtPosition(p.evaluation, p.u);
}

// The node followed: pinned, while it is on the map, else the loudest at the
// chosen point; nothing off the map.
void ofApp::follow() {
    NodeID id = InvalidNode;
    if (pinned != InvalidNode && pinned < model.manifold().nodeCount()) {
        id = pinned;
    } else {
        pinned = InvalidNode;
        float most = 0.0f;
        for (const auto& wn : cur().evaluation.weights) {
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
    s.evaluation = &cur().evaluation;
    s.text = &text;
    s.listeningPoint = cur().xy;
    s.listeningZ = cur().z;
    s.auditioning = true;             // the point is always heard here
    s.mouse = mouse;                  // the faders' hover labels
    s.linked = &linkSet;
    if (points.size() > 1) {
        for (std::size_t k = 0; k < points.size(); ++k) {
            const PlayerPoint& p = points[k];
            s.points.push_back({p.xy, p.z, pointColour(p.number), p.number, k == chosen});
        }
    }
    return s;
}

// The map's outputs in channel order with point 1's levels -- smoothed --
// the node values, and point 1's place; then every other point's levels in
// the same order, and its place (3B-2g).
OscFrame ofApp::frameNow() const {
    OscFrame f;
    const Mapping& mp = model.mapping();
    const ChartLevels& lv = points.front().heard;
    std::vector<OscOutput> outs;
    auto level = [](const ChartLevels& h, int ch) {
        return (ch >= 0 && std::size_t(ch) < h.levels.size()) ? h.levels[std::size_t(ch)]
                                                              : 0.0f;
    };
    // Where each output sits (round 3B-2b): the shape message says it, and
    // SuperCollider decides what it sounds like.
    const std::vector<OutputPlace> places = model.outputPlaces();
    for (TargetID t = 0; t < mp.targetCount(); ++t) {
        const int ch = mp.targetChannel(t);
        if (ch == NoChannel) continue;
        OscOutput o{mp.targetName(t), ch, level(lv, ch)};
        o.firstLevel = places[t].level;
        o.levelCount = places[t].levels;
        o.placed = places[t].placed;
        o.place = places[t].position;
        outs.push_back(o);
    }
    for (const Aggregator& ag : mp.aggregators()) {
        if (ag.channel == NoChannel) continue;
        outs.push_back({ag.name, ag.channel, level(lv, ag.channel)});
    }
    std::sort(outs.begin(), outs.end(), [](const OscOutput& a, const OscOutput& b) {
        return a.channel < b.channel;
    });
    f.outputs = outs;
    const PlayerPoint& one = points.front();
    for (const auto& v : one.heardValues) f.values.push_back({v.first, v.second});
    f.hasPosition = true;
    f.position = {one.xy.x, one.xy.y, one.z};
    f.hasLevelPosition = true;        // /position's fourth: where between the levels
    f.levelPosition = one.u;
    f.hasShape = true;                // /shape: the levels' starts, each output's place
    const Levels& lv3 = model.map3d().levels();
    for (LevelID k = 0; k < lv3.count(); ++k) f.levelHeights.push_back(lv3.base(k));
    for (std::size_t k = 1; k < points.size(); ++k) {
        const PlayerPoint& p = points[k];
        OscPoint q;
        q.number = p.number;
        for (const OscOutput& o : outs) q.levels.push_back(level(p.heard, o.channel));
        q.position = {p.xy.x, p.xy.y, p.z};
        q.hasLevelPosition = true;
        q.levelPosition = p.u;
        f.more.push_back(std::move(q));
    }
    return f;
}

// One frame of a point: the floor where its movement says at the count, the
// height where its own says -- each on its own. Only the chosen one hears
// the arrows.
void ofApp::movePoint(PlayerPoint& p, bool isChosen, float dt) {
    bool recordedHeight = false;
    float recordedValue = 0.0f;
    if (p.moving == Moving::Recording && p.hasRecording) {
        const glm::vec3 q = p.recorded.at(count() - p.movedFrom);
        p.xy = glm::vec2(q.x, q.y);
        recordedHeight = p.heightBy == HeightBy::Hand;   // the heights recorded
        recordedValue = q.z;
    } else if (p.floorWalks()) {
        const glm::vec3 q = p.walk.at(count() - p.movedFrom);
        p.xy = glm::vec2(q.x, q.y);
    }
    p.evaluation = p.evaluator->evaluate(p.xy);

    // The height. On the levels, u moves and the height follows from the
    // surfaces under the point; in units, the height moves and u follows.
    const Levels& lv = model.map3d().levels();
    const std::vector<WeightedNode> w = floorWeights(p);
    const bool past = heightIn == HeightIn::Past;
    const int dir = isChosen ? glideDir : 0;
    if (heightIn != HeightIn::Units) {
        if (p.heightWalks()) {
            p.u = p.heightWalk.at(count() - p.heightFrom).z;
        } else if (recordedHeight) {
            p.u = p.recordedInLevels ? recordedValue : lv.positionAt(w, recordedValue, past);
        } else if (dir != 0) {
            p.u += float(dir) * (ofGetKeyPressed(OF_KEY_SHIFT) ? kLevelsPerSecondFine
                                                               : kLevelsPerSecond) * dt;
        }
        if (!past) p.u = std::min(std::max(p.u, 0.0f), topLevel());
        p.z = lv.heightAtPosition(w, p.u, past);
    } else {
        if (p.heightWalks()) {
            p.z = p.heightWalk.at(count() - p.heightFrom).z;
        } else if (recordedHeight) {
            p.z = p.recordedInLevels ? lv.heightAtPosition(w, recordedValue, true) : recordedValue;
        } else if (dir != 0) {
            p.z = glide(p.z, dir, ofGetKeyPressed(OF_KEY_SHIFT), dt);
        }
        p.u = lv.positionAt(w, p.z, true);
    }
}

void ofApp::update() {
    const double now = ofGetElapsedTimef();
    appSeconds += ofGetLastFrameTime();
    readBridge(now);
    // What came in -- describe, hello, and a point moved from outside --
    // before moving: a move from outside lands this frame.
    readAnswers(now);

    const float dt = float(ofGetLastFrameTime());
    for (std::size_t k = 0; k < points.size(); ++k) movePoint(points[k], k == chosen, dt);
    // Recording takes the point wherever it is, on the clock running: its
    // level position on the levels, so it replays riding the shape.
    if (recording) {
        const PlayerPoint& p = points[recordingPoint];
        taking.addSample(float(count() - takingFrom),
                         glm::vec3(p.xy.x, p.xy.y, heightIn == HeightIn::Units ? p.z : p.u));
    }
    followLevel();
    text.fit();
    layoutWindow();

    follow();

    // Whose links the map draws: the node followed, the nodes the chosen
    // point is hearing, or all.
    linkSet.clear();
    if (links == 0) {
        for (NodeID id : followed.nodes()) linkSet.toggle(id);
    } else if (links == 1) {
        for (const auto& wn : cur().evaluation.weights) if (wn.weight > 0.0f) linkSet.toggle(wn.id);
    } else if (links == 2) {
        linkSet.selectAll(model.manifold().nodeCount());
    }

    // Smoothed every frame, sent or not, so the glide keeps time with the
    // clock rather than with the send rate.
    for (std::size_t k = 0; k < points.size(); ++k) smooth(points[k], dt, k == chosen);
    const OscFrame frame = frameNow();
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

// ---- the panel and the ? page, in sections (3B-2g) ----

std::vector<KeyGroup> ofApp::sections() const {
    const double t = ofGetElapsedTimef();
    const PlayerPoint& p = cur();
    const std::string pn = "point " + ofToString(p.number);
    std::vector<KeyGroup> g;

    // ---- sending ----
    {
        std::size_t bytes = 0;
        for (const auto& e : sentLog) bytes += e.second;
        KeyGroup s{"sending", {}, kSending};
        s.entries.push_back(now("", osc.prefix() + " to " + host + ":" + ofToString(sendPort)));
        s.entries.push_back(paused ? now("space", "PAUSED -- nothing goes out")
                                   : now("[ ]", ofToString(osc.rate(), 0) + " a second: "
                                                + ofToString(sentLog.size()) + " messages, "
                                                + ofToString(bytes) + " bytes"));
        s.entries.push_back(now("p i", std::string("packed ") + (osc.packed() ? "on" : "off")
                                       + ", per output " + (osc.perOutput() ? "on" : "off")));
        s.entries.push_back(osc.connected(t)
            ? now("", "answered by " + (osc.peer().empty() ? std::string("a receiver") : osc.peer()))
            : now("", "no answer yet (none is needed)", false));
        s.entries.push_back(row("space", "pause sending"));
        s.entries.push_back(row("p", "packed messages on / off"));
        s.entries.push_back(row("i", "per-output messages on / off"));
        s.entries.push_back(row("[ ]", "send rate down / up"));
        g.push_back(s);
    }
    // ---- points ----
    {
        KeyGroup s{"points", {}, kPoints};
        s.entries.push_back(now("a A", ofToString(points.size()) + " of " + ofToString(kMaxPoints)
                                       + (points.size() == 1 ? " -- a adds one" : "")));
        if (points.size() > 1) {
            s.entries.push_back(now("arrows", pn + " chosen"));
            for (std::size_t k = 0; k < points.size(); ++k) {
                const std::string& name = control.spec().points[k].name;
                s.entries.push_back(now(ofToString(points[k].number) + (k == chosen ? " >" : ""),
                                        (name.empty() ? std::string() : name + ": ")
                                        + driverName(k)));
            }
        }
        s.entries.push_back(row("a", "add a point at the mouse (up to 16)"));
        s.entries.push_back(row("A", "remove the last point (point 1 stays)"));
        s.entries.push_back(row("left / right", "choose the last / next point"));
        s.entries.push_back(row("click a dot", "choose that point"));
        s.entries.push_back(row("drag a dot", "take that point by hand"));
        g.push_back(s);
    }
    // ---- the point ----
    {
        KeyGroup s{"the point", {}, kThePoint};
        const std::string who = points.size() > 1 ? pn + " at " : "at ";
        s.entries.push_back(now("drag", who + ofToString(p.xy.x, 3) + " " + ofToString(p.xy.y, 3)
                                        + ", height " + ofToString(p.z, 3)
                                        + (p.evaluation.inside ? "" : " -- off the map")));
        const int levels = int(model.map3d().count());
        std::string where = "level 1 of 1";
        if (levels > 1) {
            const float at = std::floor(p.u + 1e-4f);
            const bool on = std::fabs(p.u - std::round(p.u)) < 1e-3f;
            if (p.u < -1e-4f || p.u > topLevel() + 1e-4f) where = "past the levels, silent";
            else if (on) where = "on level " + ofToString(int(std::round(p.u)) + 1) + " of " + ofToString(levels);
            else where = "between " + ofToString(int(at) + 1) + " and " + ofToString(int(at) + 2);
        }
        s.entries.push_back(now("up down", "level " + ofToString(p.u, 2) + ": " + where));
        const char* in[] = {"on the levels", "past the levels", "in plain units"};
        s.entries.push_back(now("k", std::string("height measured ") + in[int(heightIn)]));
        std::string who2 = "nothing";
        if (!followed.nodes().empty()) who2 = model.manifold().node(followed.nodes().front()).name;
        std::string out = "none";
        const auto& sel = model.selectedOutputs();
        if (!sel.empty()) out = model.mapping().targetName(sel.front());
        s.entries.push_back(now("tab", "following " + who2
                                             + (pinned != InvalidNode ? " (pinned)" : "")
                                             + ", output " + out));
        s.entries.push_back(row("drag", "move it (with your hand)"));
        s.entries.push_back(row("up / down", "hold: the height (shift: slowly)"));
        s.entries.push_back(row("k", "height on the levels, past them, in units"));
        s.entries.push_back(row("shift-click", "follow a node; on none: the loudest"));
        s.entries.push_back(row("click a fader", "that output's nodes ringed"));
        s.entries.push_back(row("tab", "the next output (shift: the last)"));
        g.push_back(s);
    }
    // ---- movement ----
    {
        KeyGroup s{"movement", {}, kMovement};
        s.entries.push_back(now("m n", "moving: " + movingName(p) + ", seed " + ofToString(p.seed)));
        const char* heights[] = {"by hand", "jumps", "glides"};
        s.entries.push_back(now("h", std::string("height ") + heights[int(p.heightBy)]
                                     + (p.heightWalks() && model.map3d().count() < 2
                                            ? " -- one level: stays" : "")));
        bool anyWalks = false;
        for (const PlayerPoint& q : points) anyWalks = anyWalks || q.floorWalks() || q.heightWalks();
        if (anyWalks) {
            s.entries.push_back(now(", .", "a step every " + ofToString(kIntervals[intervalAt])
                                           + (points.size() > 1 ? " (every point)" : "")));
            const char* when[] = {"steps on a timer", "steps on your clicks", "steps on the bridge's beats"};
            s.entries.push_back(now("c", when[int(steps)]));
        }
        if (p.floorWalks() || p.heightWalks()) {
            s.entries.push_back(now("< >", "travel " + ofToString(kTravels[p.travelAt])));
            if (p.floorWalks())
                s.entries.push_back(now("{ }", kSteps[p.stepAt] > 0.0
                                                   ? "steps up to " + ofToString(kSteps[p.stepAt])
                                                   : std::string("steps anywhere")));
        }
        if (recording) {
            s.entries.push_back(now("r", "recording point " + ofToString(recordingPoint + 1) + ", "
                                         + ofToString(count() - takingFrom, 1) + " -- r: stop"));
        } else if (p.hasRecording) {
            s.entries.push_back(now("r", "a recording, " + ofToString(p.recorded.path().duration(), 1)
                                         + (p.recordedOnPhase ? " bars" : " s") + ", loops"));
        }
        if (onPhase) {
            s.entries.push_back(phase.heard(t)
                ? now("b", "clock: bar " + ofToString(phase.bar()) + ", phase "
                           + ofToString(phase.phase(), 2))
                : now("b", "clock: bars -- phase not heard", false));
        } else {
            s.entries.push_back(now("b", "clock: seconds, " + ofToString(count(), 1)));
        }
        if (!bridgeOpen)
            s.entries.push_back(now("", "port " + ofToString(kBridgePort) + " taken: no bridge here", false));
        const std::string mine = points.size() > 1 ? " (this point)" : "";
        const std::string every = points.size() > 1 ? " (every point)" : "";
        s.entries.push_back(row("m", "your hand, the recording, jump, glide" + mine));
        s.entries.push_back(row("M", "your hand again, at once" + mine));
        s.entries.push_back(row("r", "record (stops anything moving); r: stop" + mine));
        s.entries.push_back(row("h", "height: by hand, jumps, glides" + mine));
        s.entries.push_back(row("< >", "travel shorter / longer (glide)" + mine));
        s.entries.push_back(row("{ }", "steps shorter / longer" + mine));
        s.entries.push_back(row("n", "a new seed" + mine));
        s.entries.push_back(row(", .", "a step more / less often" + every));
        s.entries.push_back(row("c", "steps on a timer, clicks, the bridge's beats" + every));
        s.entries.push_back(row("return", "one step (with clicks; or click the map)" + every));
        s.entries.push_back(row("b", "clock: seconds / SuperCollider's bars" + every));
        g.push_back(s);
    }
    // ---- smoothing ----
    {
        KeyGroup s{"smoothing", {}, kSmoothing};
        bool settling = false;
        for (const PlayerPoint& q : points)
            settling = settling || !q.levelSmoother.settled() || !q.valueSmoother.settled();
        s.entries.push_back(smoothOn ? now("s - =", "half-life " + ofToString(halfLife, 2) + " s"
                                                    + (settling ? "  settling" : ""))
                                     : now("s", "off: every jump steps"));
        s.entries.push_back(row("s", "on / off"));
        s.entries.push_back(row("- =", "half-life a step shorter / longer"));
        s.entries.push_back(row("shift - =", "a hundredth of a second"));
        g.push_back(s);
    }
    // ---- from outside ----
    {
        KeyGroup s{"from outside", {}, kOutside};
        const std::string port = listenPort ? ofToString(listenPort) : std::string("no port free");
        s.entries.push_back(listenPort
            ? now("", "on port " + port)
            : now("", "no answer port free (" + ofToString(kFirstAnswerPort) + " on)", false));
        s.entries.push_back(p.outsideHeld
            ? now("", pn + ": from " + control.holder(p.number).str())
            : now("", control.address(p.number, "take")));
        if (control.accepted() || control.refused()) {
            s.entries.push_back(now("", ofToString(control.accepted()) + " in, "
                                        + ofToString(control.refused()) + " refused"
                                        + (control.lastRefusal().empty() ? "" : " -- " + control.lastRefusal()),
                                    false));
        }
        const PointSpec& ps = control.spec().points[chosen];
        s.entries.push_back(row("port", port + (points.size() > 1 ? ", " + pn : std::string())));
        s.entries.push_back(row("at", control.address(p.number, "") + "..."
                                      + (ps.name.empty() ? "" : "  or /point/" + ps.name + "/")));
        s.entries.push_back(row("take", "i id, i reply port (0: yours)"));
        s.entries.push_back(row("release", "i id"));
        s.entries.push_back(row("xy z u xyz", "f ... -- open: " + PointControl::inletNames(ps.inlets)));
        s.entries.push_back(row("reply lost", "back to the taker"));
        g.push_back(s);
    }
    // ---- showing ----
    {
        KeyGroup s{"showing", {}, kShowing};
        const char* whose[] = {"followed", "heard", "all", "off"};
        s.entries.push_back(now("3 l", std::string("3D view ") + (show3D ? "on" : "off")
                                       + ", links " + whose[links]));
        s.entries.push_back(row("3", "the 3D view (drag in it to turn it)"));
        s.entries.push_back(row("l", "links: followed, heard, all, none"));
        s.entries.push_back(row("?", "this page; again: hide keys; again: back"));
        g.push_back(s);
    }
    // ---- maps ----
    {
        KeyGroup s{"maps", {}, kMaps};
        s.entries.push_back(now("o e x E", osc.mapName() + ", " + ofToString(model.map3d().count())
                                           + (model.map3d().count() == 1 ? " level" : " levels")));
        s.entries.push_back(now("C", controlPath.empty() ? std::string("built in: no control file")
                                     : "control file " + controlPath.substr(controlPath.find_last_of("/\\") + 1)));
        s.entries.push_back(row("o", "open a map you saved in the editor"));
        s.entries.push_back(row("e", "the room: three levels"));
        s.entries.push_back(row("x", "the octahedron: two levels touching"));
        s.entries.push_back(row("E", "the editor's flat example"));
        s.entries.push_back(row("C", "write the map's control file beside it"));
        g.push_back(s);
    }
    return g;
}

void ofApp::draw() {
    layoutWindow();
    const Scene sc = scene();
    // The editor's map, nothing selected and no grid: shapes, halos, values,
    // the links chosen with l, the current output's nodes ringed, the point
    // and its lines, the level dots -- and every point, with more than one.
    GridLook look;
    look.kind = GridKind::Free;
    drawMap(sc, look, MapOverlay{});
    drawPaths();
    // The faders: what the chosen point sends, smoothed. Hover one for its
    // numbers.
    drawChart(sc, chart, cur().heard);
    if (show3D) {
        if (!aimed) { aimLevelView(cam, view3D); aimed = true; }
        cam.setControlArea(view3D);
        drawLevelView(sc, cam, view3D);
    }

    if (keys == Keys::Hidden) {
        // Nothing but the maps and the faders -- and a "?" to come back by,
        // and a message, while it lasts, along the bottom of the map.
        ofSetColor(150, 156, 170);
        ofDrawBitmapString("?", float(ofGetWidth()) - 20.0f, 20.0f);
        if (!message.empty() && ofGetElapsedTimef() < messageUntil) {
            ofSetColor(30, 30, 34);
            ofDrawRectangle(mapArea.getX(), mapArea.getBottom() - 18.0f,
                            mapArea.getWidth(), 18.0f);
            ofSetColor(255, 214, 90);
            ofDrawBitmapString(message, mapArea.getX() + 6.0f, mapArea.getBottom() - 5.0f);
        }
        return;
    }

    // The panel: the sections' state lines, each with the key that changes
    // it, flowed down the panel and on into a second column if there is
    // room for one; then the last send, in what room is left.
    const float cw = text.charWidth(), lh = text.lineHeight();
    const float W = float(ofGetWidth());
    const float bottom = chartTop - 12.0f;
    float y = 40.0f;
    ofSetColor(255, 214, 90);
    text.draw("? for every key; ?? hides them", panelX, y);
    const float top = y + lh + keyGroupGap(lh);
    const std::vector<KeyGroup> state = keyRows(sections(), true);
    const KeyFlow flow = flowKeys(state, panelX, W - 16.0f, top, top, bottom - lh - 8.0f,
                                  cw, lh, true, false);
    drawKeyGroups(text, state, flow, "lines more -- a larger window shows them");
    {
        // The last send, under the last line, while there is room.
        float lx = panelX, ly = top;
        for (const KeyLine& l : flow.lines) {
            if (l.x > lx + 0.5f) { lx = l.x; ly = l.y; }
            else if (std::fabs(l.x - lx) < 0.5f) ly = std::max(ly, l.y);
        }
        const float row = lh + 1.0f;
        const float from = ly + row + keyGroupGap(lh);
        const int fits = int((bottom - lh - 8.0f - from) / row);
        if (fits >= 2 && !lastSent.empty()) {
            KeyGroup s{"last send", {}, kLastSend};
            for (std::size_t k = 0; k < lastSent.size() && int(k) + 1 < fits; ++k)
                s.entries.push_back(now("", lastSent[k], false));
            const std::vector<KeyGroup> one{s};
            const KeyFlow f = flowKeys(one, lx, lx + flow.columnWidth, from, from,
                                       bottom - lh - 8.0f, cw, lh, false, false);
            drawKeyGroups(text, one, f);
        }
    }
    if (!message.empty() && ofGetElapsedTimef() < messageUntil) {
        // Along the panel's foot, over anything there, so the faders never
        // hide it.
        ofSetColor(30, 30, 34);
        ofFill();
        ofDrawRectangle(panelX - 4.0f, bottom - lh - 2.0f, W - panelX - 12.0f, lh + 8.0f);
        ofSetColor(255, 214, 90);
        std::string m = message;
        const std::size_t room = std::size_t(std::max(12.0f, (W - panelX - 16.0f) / cw));
        if (m.size() > room) m = m.substr(0, room - 3) + "...";
        text.draw(m, panelX, bottom);
    }
    if (keySheet()) drawKeySheet();
}

// The path being recorded, bright, as it grows; each recording playing, dim,
// the whole loop, in its point's colour. On the map, at the floor plan:
// heights are in the 3D view.
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
    for (std::size_t k = 0; k < points.size(); ++k) {
        const PlayerPoint& p = points[k];
        if (recording && recordingPoint == k) continue;
        if (p.moving != Moving::Recording || !p.hasRecording) continue;
        const ofColor c = points.size() > 1 ? pointColour(p.number) : ofColor(255, 150, 120);
        path(p.recorded.path(), ofColor(c.r, c.g, c.b, 140), 2.0f);
    }
    if (recording) {
        path(taking, ofColor(255, 110, 90), 3.0f);
        // REC, by the map's top left corner.
        ofSetColor(255, 80, 70);
        ofFill();
        ofDrawCircle(mapArea.getX() + 14.0f, mapArea.getY() + 14.0f, 6.0f);
        ofDrawBitmapString(points.size() > 1 ? "REC point " + ofToString(recordingPoint + 1)
                                                   + " -- r to stop"
                                             : std::string("REC -- r to stop"),
                           mapArea.getX() + 26.0f, mapArea.getY() + 19.0f);
    }
}

// Every key on one page, in the panel's sections, over everything else.
void ofApp::drawKeySheet() const {
    const float W = float(ofGetWidth()), H = float(ofGetHeight());
    const float cw = text.charWidth(), lh = text.lineHeight();
    ofSetColor(18, 18, 22, 235);
    ofFill();
    ofDrawRectangle(0.0f, 0.0f, W, H);
    const float x = 40.0f, y = 50.0f;
    ofSetColor(255, 214, 90);
    text.draw("every key -- ? again hides them all, a click goes back", x, y);
    const float first = y + 2.0f * (lh + 1.0f);
    const std::vector<KeyGroup> keysOnly = keyRows(sections(), false);
    const KeyFlow flow = flowKeys(keysOnly, x, W - 40.0f, first, first, H - 30.0f,
                                  cw, lh, true, false);
    drawKeyGroups(text, keysOnly, flow);
}

void ofApp::mousePressed(int x, int y, int) {
    if (keySheet()) { keys = Keys::Panel; return; }  // a click: back to the map
    grabbing = -1;
    // A fader picks its output: its nodes ringed in its colour on the map.
    const TargetID hit = chart.outputAt(float(x), float(y));
    if (hit != InvalidTarget) { model.pickOutput(hit); return; }
    const glm::vec2 at{float(x), float(y)};
    // A point's dot, with more than one (3B-2g): that point chosen; a drag
    // from here takes it by hand.
    if (!ofGetKeyPressed(OF_KEY_SHIFT) && renderer->viewport().inside(x, y)) {
        const int dot = dotAt(at);
        if (dot >= 0) {
            if (std::size_t(dot) != chosen) choose(std::size_t(dot));
            if (std::size_t(dot) == chosen) grabbing = dot;
            return;
        }
    }
    PlayerPoint& p = cur();
    // Moving by itself: a click on the map is a step (with clicks), or puts
    // the walk somewhere new to go on from.
    if (p.moving != Moving::Hand && !ofGetKeyPressed(OF_KEY_SHIFT)
        && renderer->viewport().inside(x, y)) {
        if (p.moving != Moving::Recording && steps == Steps::Clicks) { step(); return; }
        p.xy = renderer->toManifold(at);
        if (p.moving != Moving::Recording) startMoving(p);
        return;
    }
    // Shift-click on the map: follow that node, or the loudest again.
    if (ofGetKeyPressed(OF_KEY_SHIFT) && renderer->viewport().inside(x, y)) {
        pinned = nodeAt(at);
        follow();
        say(pinned != InvalidNode ? "following " + model.manifold().node(pinned).name + ", pinned"
                                  : "following the loudest node");
        return;
    }
    if (renderer->viewport().inside(x, y)) {
        // The hand always wins: held from outside, the point comes back.
        if (p.outsideHeld) { control.handTook(p.number); applyControl(); }
        p.xy = renderer->toManifold(at);
    }
}

void ofApp::mouseReleased(int, int, int) { grabbing = -1; }

void ofApp::mouseMoved(int x, int y) { mouse = glm::vec2(float(x), float(y)); }

void ofApp::mouseDragged(int x, int y, int) {
    mouse = glm::vec2(float(x), float(y));
    if (ofGetKeyPressed(OF_KEY_SHIFT)) return;      // a pin, not a move
    if (!renderer->viewport().inside(x, y)) return;
    PlayerPoint& p = cur();
    if (grabbing >= 0 && std::size_t(grabbing) == chosen && p.moving != Moving::Hand) {
        // A dot dragged: that point by hand, as M -- the others carry on.
        if (recording && recordingPoint == chosen) recording = false;
        p.moving = Moving::Hand;
        startMoving(p);
        say("point " + ofToString(p.number) + " by hand");
    }
    if (p.moving != Moving::Hand) return;            // it moves itself
    if (p.outsideHeld) { control.handTook(p.number); applyControl(); }
    p.xy = renderer->toManifold(mouse);
}

void ofApp::keyPressed(int key) {
    if (key == '?' || key == '/') {
        // The panel, every key, none -- and round again.
        keys = keys == Keys::Panel ? Keys::Sheet
             : keys == Keys::Sheet ? Keys::Hidden : Keys::Panel;
        layoutWindow();
        return;
    }
    if (keySheet()) return;           // the page is up: keys wait for it to go
    PlayerPoint& p = cur();
    const std::string pn = points.size() > 1 ? "point " + ofToString(p.number) + ": " : "";
    if (key == OF_KEY_UP || key == OF_KEY_DOWN) {
        if (p.heightWalks()) say("the height is moving itself -- h for by hand");
        glideDir = key == OF_KEY_UP ? +1 : -1;
    }
    // ---- points (3B-2g) ----
    else if (key == 'a') addPoint();
    else if (key == 'A') removePoint();
    else if (key == OF_KEY_LEFT || key == OF_KEY_RIGHT) {
        if (points.size() < 2) { say("one point -- a adds another"); return; }
        const std::size_t n = points.size();
        choose(key == OF_KEY_RIGHT ? (chosen + 1) % n : (chosen + n - 1) % n);
    }
    // ---- sending ----
    else if (key == 'p') { osc.setPacked(!osc.packed()); osc.reset(); }
    else if (key == 'i') { osc.setPerOutput(!osc.perOutput()); osc.reset(); }
    else if (key == '[') osc.setRate(osc.rate() >= 20.0 ? osc.rate() - 10.0 : osc.rate() / 2.0);
    else if (key == ']') osc.setRate(osc.rate() >= 10.0 ? osc.rate() + 10.0 : osc.rate() * 2.0);
    else if (key == ' ') paused = !paused;
    // ---- smoothing ----
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
    // ---- showing ----
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
    // ---- maps ----
    else if (key == 'e') { model.loadRoom(); adopt("room"); say("the room: floor, middle and top"); }
    else if (key == 'E') { model.loadExample(); adopt("example"); say("the editor's flat example"); }
    else if (key == 'C') writeControl();
    else if (key == 'x') {
        model.loadOctahedron(); adopt("octahedron");
        say("the octahedron: two levels meeting at the rim");
    }
    else if (key == 'o') open();
    // ---- movement: the chosen point's ----
    else if (key == 'm') {
        p.moving = p.moving == Moving::Hand      ? (p.hasRecording ? Moving::Recording : Moving::Jump)
                 : p.moving == Moving::Recording ? Moving::Jump
                 : p.moving == Moving::Jump      ? Moving::Glide
                                                 : Moving::Hand;
        if (recording && recordingPoint == chosen) recording = false;
        startMoving(p);
        say(pn + "moving: " + movingName(p));
    }
    else if (key == 'M') {
        if (recording && recordingPoint == chosen) recording = false;
        p.moving = Moving::Hand;
        startMoving(p);
        say(pn + "your hand: the point stays where it is");
    }
    else if (key == 'r') {
        if (!recording) {
            // Whatever was moving stops where it is, and your hand takes over:
            // a recording is of what you do, never of the last one playing.
            p.moving = Moving::Hand;
            startMoving(p);
            taking.clear();
            takingFrom = count();
            recording = true;
            recordingPoint = chosen;
            say(pn + "recording -- drag the point; r again to stop");
        } else {
            finishRecording();
        }
    }
    else if (key == '<' || key == '>') {
        p.travelAt = stepIn(kTravels, p.travelAt, key == '>' ? 1 : -1);
        startMoving(p);
        say(pn + "travel " + ofToString(kTravels[p.travelAt]));
    }
    else if (key == '{' || key == '}') {
        // { shorter steps (further down the list), } longer, up to anywhere.
        p.stepAt = stepIn(kSteps, p.stepAt, key == '{' ? 1 : -1);
        startMoving(p);
        say(pn + (kSteps[p.stepAt] > 0.0 ? "steps of up to " + ofToString(kSteps[p.stepAt])
                                         : std::string("steps anywhere on the map")));
    }
    else if (key == 'h') {
        p.heightBy = p.heightBy == HeightBy::Hand ? HeightBy::Jump
                   : p.heightBy == HeightBy::Jump ? HeightBy::Glide : HeightBy::Hand;
        startHeight(p);
        const char* said[] = {"height by hand: up and down", "height jumps at random",
                              "height glides at random"};
        say(pn + said[int(p.heightBy)]);
    }
    else if (key == 'n') {
        ++p.seed;
        startMoving(p);
        say(pn + "seed " + ofToString(p.seed));
    }
    // ---- movement: every point's ----
    else if (key == ',' || key == '.') {
        intervalAt = stepIn(kIntervals, intervalAt, key == '.' ? 1 : -1);
        startEvery();
        say("a step every " + ofToString(kIntervals[intervalAt]));
    }
    else if (key == 'c') {
        steps = steps == Steps::Timer ? Steps::Clicks
              : steps == Steps::Clicks ? Steps::Beats : Steps::Timer;
        startEvery();
        const char* said[] = {"steps on a timer", "steps on your clicks (or return)",
                              "steps on the bridge's beats"};
        say(said[int(steps)]);
    }
    else if (key == OF_KEY_RETURN) {
        if (steps == Steps::Clicks) step();
    }
    else if (key == 'k') {
        heightIn = heightIn == HeightIn::Levels ? HeightIn::Past
                 : heightIn == HeightIn::Past ? HeightIn::Units : HeightIn::Levels;
        // Every point stays where it is: u from the height, held when restrained.
        for (PlayerPoint& q : points) {
            q.u = model.map3d().levels().positionAt(floorWeights(q), q.z, heightIn == HeightIn::Past);
            startHeight(q);
        }
        const char* said[] = {"height on the levels: riding their bends, held between bottom and top",
                              "height on the levels, free to go past them (silent out there)",
                              "height in plain units"};
        say(said[int(heightIn)]);
    }
    else if (key == 'b') {
        onPhase = !onPhase;
        startEvery();                 // the count changes units: start again
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
    say(r.message, 3.0f);
    if (r.changed) {
        // The map's name: the pair's, without its folder.
        const std::string base = pairFor(d.getPath()).base;
        adopt(base.substr(base.find_last_of("/\\") + 1));
        // Its control file, if it has one: how many points, and what a sender
        // may move from outside. None: point 1, every inlet. Refused: the
        // same, and why.
        controlPath = base + "-control.json";
        std::ifstream f(controlPath, std::ios::binary);
        if (f) {
            std::ostringstream in;
            in << f.rdbuf();
            ControlSpec spec;
            const auto cr = loadControl(in.str(), spec, kMaxPoints);
            if (cr.ok) {
                controlSpec = spec;
                useControl();
                if (control.count() > 1)
                    say(r.message + " -- " + ofToString(control.count()) + " points", 3.0f);
            } else {
                say(r.message + " -- its control file refused: " + cr.error, 3.0f);
            }
        }
    }
}
