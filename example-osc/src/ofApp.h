#pragma once

// ofxManifold -- example-osc (rounds 3B-1 and 3B-2).
//
// The smallest player: one map, its points, and OSC out that grows and
// shrinks with the map -- for SuperCollider, Max, Pd, TouchDesigner, a
// script, whatever is listening. Drag the point; hold up or down to move it
// through the levels. It starts on the room example: four corners on the
// floor, the middle and the top. Beside the map, the column of level dots and
// the editor's 3D view, where the point is a green ball with a line dropped
// to the level below; the level it is on or above is the brighter one. What
// goes out, and whether anyone answered, is on the right, in sections.
//
// The map is drawn as the editor draws it, without the editing: node shapes
// by kind, each node's share as a halo, values, links, and the outputs'
// faders along the bottom -- so an output that sounds wrong can be seen to be
// unbound, trimmed or fed from somewhere unexpected.
//
// Where the editor has a selection, this follows one node: the loudest at
// the point, or one pinned with shift-click. It is drawn as if selected --
// ringed in yellow, its links on the map, and on the faders its share as a
// brighter band, a dot over every output it feeds (its own, and any it is
// bound to) and its fill line -- so moving toward a bound node shows its
// share rising on both faders.
//
// What is sent is smoothed (OutputSmoother): one number per channel and per
// node value, a fixed list, so a click across the map, a level crossed or the
// point leaving the map glides over the half-life instead of stepping. The
// faders show what is sent, smoothed. Click far from the point to hear it; s
// to compare without.
//
// The point can move itself (3B-2): a recording of your hand, looped; random
// jumps; random glides. A movement is asked a count -- the app's own seconds,
// or bars from SuperCollider's phase (ofxScPhaseBridge's /phase on port 9001;
// bridge.scd drives it unchanged) -- so on SuperCollider's clock a step every
// 1/16 lands on its sixteenths. Jumps and glides stay on the map, never cross
// off it on the way, and step on a timer, on your clicks, or on the bridge's
// beats: each armed step that sounds moves the point once.
//
// More than one point (3B-2g): a adds one, up to 16, at the mouse; A removes
// the last. One is chosen at a time -- left / right, or a press on its dot --
// and the keys of the point and of movement act on it; the others carry on
// with what they were given. Each sends its own levels and position (point 1
// under the addresses it always had, the others under /point/<n>/), and
// SuperCollider decides what each sounds like. A map's control file says how
// many it opens with.
//
// The receiver need not know how many outputs there are: the app describes
// them (/manifold/outputs) on the first send, when they change, when asked,
// and every two seconds. See ofxManifoldPlayer/OSC.md for the protocol, and
// supercollider/manifold-receive.scd and receive.py beside this file for two
// receivers.
//
// Everything that decides what to send is in ofxManifoldPlayer (OscOut),
// tested without openFrameworks; this file owns the window, the keys and the
// two ofxOsc ends, and turns OscOut's messages into ofxOscMessages and back.
//
// The panel and the ? page are drawn in sections (3B-2g), each with its
// header: sending, points, the point, movement, smoothing, from outside,
// showing, maps. The panel shows what is true now, with the key that changes
// it; the ? page every key.
//
//   drag            move the chosen point
//   up / down       hold: move it through the levels (shift: slowly)
//   a  A            add a point at the mouse / remove the last
//   left / right    choose the last / next point (or press its dot)
//   p               packed messages on or off
//   i               per-output messages on or off
//   [ ]             send rate down / up
//   s               smoothing on or off
//   - =             smoothing's half-life a step shorter / longer
//   shift - =       a hundredth of a second shorter / longer
//   shift-click     follow that node, wherever the point goes; on nothing:
//                   follow the loudest again
//   l               links: of the node followed, the nodes heard, every
//                   node, none
//   tab, shift-tab  the next / last output: its nodes ringed in its colour
//   click a fader   that output
//   space           pause sending
//   3               the 3D view on or off (drag in it to turn it)
//   o               open a map pair (map + outputs)
//
// Its messages go under /manifold/<map's name> (the file's name; room, or
// example), each description led by a roll call on /manifold/map, so copies
// of this app with different maps can run at once, each heard apart.
//
//   e               the room example: three levels
//   E               the editor's flat example
//
// Movement -- of the chosen point (m M r h { } < > n), or of every point
// (, . c return b):
//   m               the movement: your hand, the recording, jump, glide
//   M               your hand again at once: anything moving stops there
//   r               record: anything moving stops and your hand takes over,
//                   the path drawn as you go; r again stops, and it loops
//   , .             the step interval shorter / longer
//   < >             the travel time shorter / longer (glide)
//   { }             the step distance shorter / longer (anywhere at the top)
//   c               when steps happen: a timer, your clicks, the bridge's beats
//   return          one step (with clicks; a click on the map does the same)
//   k               the height measured on the levels (riding their bends,
//                   held between bottom and top), past them, or in units
//   h               the height: by hand, jumping, gliding -- on its own,
//                   whatever moves the floor: drag the point while the
//                   height wanders, or hold up / down while the floor does
//   n               a new seed
//   b               the clock: the app's own seconds, or SuperCollider's bars
//
//   ?               every key, on one page (again: none; again: the panel)

#include "ofMain.h"
#include "ofxManifold.h"
#include "ofxManifoldEditorViews.h"
#include "ofxManifoldPlayer.h"
#include "ofxOsc.h"

#include <deque>
#include <map>
#include <memory>
#include <string>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mousePressed(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void mouseMoved(int x, int y) override;
    void mouseDragged(int x, int y, int button) override;
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // Where the messages go, and where answers come back. sclang listens on
    // 57120 by default. Answers: the first port free from kFirstAnswerPort,
    // so several copies can run, one map each; each gives its port in the
    // roll call, so receivers know where to answer it. 0: none was free.
    std::string host       = "127.0.0.1";
    int         sendPort   = 57120;
    static constexpr int kFirstAnswerPort = 57121, kAnswerPorts = 16;
    int         listenPort = 0;
    // ofxScPhaseBridge's /phase and /beat. Not shared: another app on this
    // port -- the SuperCollider Audio Engine's panel listens on 9001 too --
    // leaves this one without a bridge, and the panel says so. Change it
    // here, and send the bridge here, to run both.
    static constexpr int kBridgePort = 9001;

    // ---- movement (3B-2) ----
    enum class Moving { Hand, Recording, Jump, Glide };
    enum class Steps { Timer, Clicks, Beats };
    // The height moves on its own: by hand (the arrows; or, while a
    // recording plays, the heights recorded), or jumping or gliding at
    // random across the map's levels, whatever moves the floor.
    enum class HeightBy { Hand, Jump, Glide };
    // How the height is measured (3B-2c). On the levels: a level position u
    // -- 0 on the bottom surface, 1 on the next -- at the point, so it rides
    // the bends and is held between the bottom and the top. Past the levels:
    // the same, free to carry on beyond them (silent out there). In units:
    // the height itself, as before.
    enum class HeightIn { Levels, Past, Units };

    // ---- a point (3B-2g) ----
    // Each is a whole point: where it is, how it moves, its own walks,
    // recording and seed, and what it is heard as, smoothed. The clock, the
    // step timing and how the height is measured are every point's.
    struct PlayerPoint {
        int       number = 1;            // from 1: its addresses
        glm::vec2 xy{0.5f, 0.5f};
        float     z = 0.0f;
        float     u = 0.0f;              // the level position, kept in step with z
        Moving    moving = Moving::Hand;
        HeightBy  heightBy = HeightBy::Hand;
        double    movedFrom = 0.0;       // the walks are asked count() - these
        double    heightFrom = 0.0;
        ofxManifold::RandomWalk     walk;
        ofxManifold::RandomWalk     heightWalk;   // x and y held: only its height is used
        ofxManifold::RecordedMotion recorded;
        bool      hasRecording = false;
        bool      recordedOnPhase = false;
        bool      recordedInLevels = false;
        std::uint64_t seed = 1;
        std::uint64_t restarts = 0;      // walks started: each draws afresh
        int       travelAt = 3;          // kTravels[]: 0.4
        int       stepAt = 0;            // kSteps[]: anywhere
        // From outside (3B-2f): held, and what drove it before.
        bool      outsideHeld = false;
        Moving    movingBefore = Moving::Hand;
        HeightBy  heightBefore = HeightBy::Hand;
        // Heard: its own evaluator (each keeps track of the region it was
        // last in), the evaluation, and the levels and values smoothed.
        std::shared_ptr<ofxManifold::Evaluator> evaluator;
        ofxManifold::Evaluation evaluation;
        ofxManifold::player::OutputSmoother levelSmoother, valueSmoother;
        ofxManifold::editor::ChartLevels heard;
        std::vector<std::pair<std::string, float>> heardValues;

        bool floorWalks() const { return moving == Moving::Jump || moving == Moving::Glide; }
        bool heightWalks() const { return heightBy != HeightBy::Hand; }
    };
    std::vector<PlayerPoint> points;     // at least point 1
    std::size_t chosen = 0;              // the one the keys act on
    PlayerPoint&       cur()       { return points[chosen]; }
    const PlayerPoint& cur() const { return points[chosen]; }
    static ofColor pointColour(int number);
    PlayerPoint newPoint(int number, glm::vec2 xy, float u) const;
    void addPoint();                    // a
    void removePoint();                 // A
    void choose(std::size_t k);         // left / right, a press on a dot
    void describePoints();              // the points, to the description
    int  dotAt(glm::vec2 screen) const; // a point's dot within 12 px; -1: none
    int  grabbing = -1;                 // a dot pressed: dragging takes it by hand

    void adopt(const std::string& name); // after the map changes: its name
    void open();
    void layoutWindow();                // the map, the column, the 3D view
    void followLevel();                 // the chosen point's level is current
    ofxManifold::editor::Scene scene() const;
    ofxManifold::player::OscFrame frameNow() const;
    // The map's levels at each point (routed, after trim, silence) and the
    // node values, smoothed: what the faders show and what is sent.
    void smooth(PlayerPoint& p, float dt, bool following);
    void applySmoothing();              // smoothOn and halfLife to every point

    double count() const;               // the clock's count now
    void   startMoving(PlayerPoint& p); // the walks again, from where it is
    void   startHeight(PlayerPoint& p); // the height's walk again
    void   startEvery();                // every point's walks: shared timing changed
    void   step();                      // a click or a beat: every point one step
    void   movePoint(PlayerPoint& p, bool isChosen, float dt);   // a frame of it
    void   readBridge(double now);      // /phase and /beat on kBridgePort
    std::string movingName(const PlayerPoint& p) const;
    std::string driverName(std::size_t k) const;   // the panel's word for who drives it
    void finishRecording();             // r a second time
    void drawPaths() const;             // the path being recorded, or playing
    void drawKeySheet() const;          // ? : every key on one page
    // The panel's and the ? page's sections (3B-2g): each its state lines,
    // with the key that changes them, and its keys.
    std::vector<ofxManifold::editor::KeyGroup> sections() const;
    // What ? shows, in turn (3B-2e): the panel beside the maps; every key
    // on one page; nothing -- the maps as large as the window allows, for
    // recording, only a small "?" left.
    enum class Keys { Panel, Sheet, Hidden };
    Keys keys = Keys::Panel;
    bool keySheet() const { return keys == Keys::Sheet; }

    ofxManifold::editor::Model                    model;
    std::unique_ptr<ofxManifoldRenderer>          renderer;

    // Where things are, from the window's size (layoutWindow): the map, the
    // 3D view beside it when shown, and the panel's left edge.
    ofRectangle mapArea, view3D;
    glm::vec2   mouse{-1.0f, -1.0f};     // for the faders' hover labels
    float       panelX = 0.0f;
    float       chartTop = 0.0f;         // the faders' band, across the bottom
    ofxManifold::editor::ChartLayout chart;
    // The node followed: the pinned one if there is one, else the loudest at
    // the chosen point. Drawn as the editor draws a selection.
    ofxManifold::NodeID            pinned = ofxManifold::InvalidNode;
    ofxManifold::editor::Selection followed;
    void follow();
    ofxManifold::NodeID nodeAt(glm::vec2 screen) const;
    // Whose links are drawn: the node followed (0), the nodes heard (1),
    // every node (2), none (3).
    int         links = 0;
    ofxManifold::editor::Selection linkSet;
    bool        show3D = true;
    bool        aimed  = false;          // the camera aimed once, then dragged
    ofEasyCam   cam;
    ofxManifold::editor::Text      text; // the column's label, the panel

    int       glideDir = 0;             // -1, 0, +1 while down / up is held

    // Smoothing what is sent: the levels by channel (routed, after trim, and
    // silence, so the faders' bars and ticks move together) and the node
    // values, each a list as long as the map makes it. Reset when the map
    // changes.
    bool  smoothOn = true;
    float halfLife = 0.05f;

    // Movement's shared settings, and the clock.
    Steps       steps = Steps::Timer;
    int         intervalAt = 3;          // kIntervals[]: 0.5
    double      appSeconds = 0.0;        // summed frame times: the seconds clock
    ofxManifold::player::SecondsClock seconds;
    ofxManifold::player::PhaseClock   phase;
    bool        onPhase = false;         // b: bars from SuperCollider
    ofxOscReceiver bridge;
    bool        bridgeOpen = false;
    HeightIn    heightIn = HeightIn::Levels;
    float       topLevel() const;        // count() - 1: u's top
    std::vector<ofxManifold::WeightedNode> floorWeights(const PlayerPoint& p) const;
    ofxManifold::Evaluation3D heardAt(const PlayerPoint& p) const;   // by u on the levels, z in units
    // One recording at a time, of the point chosen when r was pressed.
    bool        recording = false;
    std::size_t recordingPoint = 0;
    ofxManifold::Trajectory     taking;  // being recorded
    double      takingFrom = 0.0;

    ofxManifold::player::OscOut osc;
    ofxOscSender                sender;
    ofxOscReceiver              listener;

    // ---- points moved from outside (3B-2f) ----
    // On the answer port: <prefix>/point/<n>/take, release, xy, z, u, xyz.
    // The rule is PointControl's; this applies what it returns, and sends
    // its answers. The map's control file, if it has one, beside the map
    // pair: it says how many points the map opens with (3B-2g).
    ofxManifold::player::PointControl control;
    ofxManifold::player::ControlSpec  controlSpec;
    std::string controlPath;            // "": a built-in map, no file
    std::map<std::string, std::unique_ptr<ofxOscSender>> replySenders;
    void readAnswers(double now);       // describe, hello -- and the points
    void applyControl();                // what PointControl returned
    void useControl();                  // the spec, to the control and the points
    void writeControl();                // C: the control file beside the map
    bool                        paused = false;

    // What went out in the last second, for the panel.
    std::deque<std::pair<double, std::size_t>> sentLog;   // time, bytes
    std::vector<std::string>    lastSent;                 // the last send
    std::string                 message;
    float                       messageUntil = 0.0f;
    void say(const std::string& t, float seconds = 2.5f);
};
