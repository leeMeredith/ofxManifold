#pragma once

// ofxManifold -- example-osc (round 3B-1).
//
// The smallest player: one map, one point, and OSC out that grows and
// shrinks with the map -- for SuperCollider, Max, Pd, TouchDesigner, a
// script, whatever is listening. Drag the point; hold up or down to move it
// through the levels. It starts on the room example: four corners on the
// floor, the middle and the top. Beside the map, the column of level dots and
// the editor's 3D view, where the point is a green ball with a line dropped
// to the level below; the level it is on or above is the brighter one. What
// goes out, and whether anyone answered, is on the right.
//
// The map is drawn as the editor draws it, without the editing: node shapes
// by kind, each node's share as a halo, values, the links of the nodes being
// heard, and the outputs' faders along the bottom -- so an output that sounds
// wrong can be seen to be unbound, trimmed or fed from somewhere unexpected.
//
// What is sent is smoothed (OutputSmoother): one number per channel and per
// node value, a fixed list, so a click across the map, a level crossed or the
// point leaving the map glides over the half-life instead of stepping. The
// faders show what is sent, smoothed. Click far from the point to hear it; s
// to compare without.
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
//   drag            move the point
//   up / down       hold: move it through the levels (shift: slowly)
//   p               packed messages on or off
//   i               per-output messages on or off
//   [ ]             send rate down / up
//   s               smoothing on or off
//   - =             smoothing's half-life a step shorter / longer
//   shift - =       a hundredth of a second shorter / longer
//   l               links: of the nodes heard, of every node, none
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

#include "ofMain.h"
#include "ofxManifold.h"
#include "ofxManifoldEditorViews.h"
#include "ofxManifoldPlayer.h"
#include "ofxOsc.h"

#include <deque>
#include <memory>
#include <string>

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mousePressed(int x, int y, int button) override;
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

    void adopt(const std::string& name); // after the map changes: its name
    void open();
    void layoutWindow();                // the map, the column, the 3D view
    void followLevel();                 // the point's level is current
    ofxManifold::editor::Scene scene() const;
    ofxManifold::player::OscFrame frameNow(const ofxManifold::editor::ChartLevels& lv) const;
    // The map's levels at the point (routed, after trim, silence) and the
    // node values, smoothed: what the faders show and what is sent.
    void smooth(float dt);
    void applySmoothing();              // smoothOn and halfLife to both

    ofxManifold::editor::Model                    model;
    std::unique_ptr<ofxManifold::Evaluator>       evaluator;
    std::unique_ptr<ofxManifoldRenderer>          renderer;
    ofxManifold::Evaluation                       evaluation;

    // Where things are, from the window's size (layoutWindow): the map, the
    // 3D view beside it when shown, and the panel's left edge.
    ofRectangle mapArea, view3D;
    glm::vec2   mouse{-1.0f, -1.0f};     // for the faders' hover labels
    float       panelX = 0.0f;
    float       chartTop = 0.0f;         // the faders' band, across the bottom
    ofxManifold::editor::ChartLayout chart;
    ofxManifold::editor::ChartLevels heard;         // smoothed: shown and sent
    std::vector<std::pair<std::string, float>> heardValues;
    // Whose links are drawn: the nodes heard (0), every node (1), none (2).
    int         links = 0;
    ofxManifold::editor::Selection linkSet;
    bool        show3D = true;
    bool        aimed  = false;          // the camera aimed once, then dragged
    ofEasyCam   cam;
    ofxManifold::editor::Text      text; // the column's label
    ofxManifold::editor::Selection none; // nothing selected: a player

    glm::vec2 point{0.5f, 0.5f};
    float     z = 0.0f;
    int       glideDir = 0;             // -1, 0, +1 while down / up is held

    // Smoothing what is sent: the levels by channel (routed, after trim, and
    // silence, so the faders' bars and ticks move together) and the node
    // values, each a list as long as the map makes it. Reset when the map
    // changes.
    ofxManifold::player::OutputSmoother levelSmoother, valueSmoother;
    bool  smoothOn = true;
    float halfLife = 0.05f;

    ofxManifold::player::OscOut osc;
    ofxOscSender                sender;
    ofxOscReceiver              listener;
    bool                        paused = false;

    // What went out in the last second, for the panel.
    std::deque<std::pair<double, std::size_t>> sentLog;   // time, bytes
    std::vector<std::string>    lastSent;                 // the last send
    std::string                 message;
    float                       messageUntil = 0.0f;
};
