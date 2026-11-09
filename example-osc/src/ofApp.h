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
// What is sent is smoothed (OutputSmoother): one number per output and per
// node value, a fixed list, so a click across the map, a level crossed or the
// point leaving the map glides over the half-life instead of stepping. Click
// far from the point to hear it; s to compare without.
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
//   - =             smoothing's half-life shorter / longer
//   space           pause sending
//   3               the 3D view on or off (drag in it to turn it)
//   o               open a map pair (map + outputs)
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
    void mouseDragged(int x, int y, int button) override;
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // Where the messages go and where answers come back. sclang listens on
    // 57120 by default; the patch beside this example answers to 57121.
    // Change them here.
    std::string host       = "127.0.0.1";
    int         sendPort   = 57120;
    int         listenPort = 57121;

    void adopt();                       // after the map changes
    void open();
    void layoutWindow();                // the map, the column, the 3D view
    void followLevel();                 // the point's level is current
    ofxManifold::editor::Scene scene() const;
    ofxManifold::player::OscFrame frameNow() const;
    void smooth(ofxManifold::player::OscFrame& f, float dt);
    void applySmoothing();              // smoothOn and halfLife to both

    ofxManifold::editor::Model                    model;
    std::unique_ptr<ofxManifold::Evaluator>       evaluator;
    std::unique_ptr<ofxManifoldRenderer>          renderer;
    ofxManifold::Evaluation                       evaluation;

    // Where things are, from the window's size (layoutWindow): the map, the
    // 3D view beside it when shown, and the panel's left edge.
    ofRectangle mapArea, view3D;
    float       panelX = 0.0f;
    bool        show3D = true;
    bool        aimed  = false;          // the camera aimed once, then dragged
    ofEasyCam   cam;
    ofxManifold::editor::Text      text; // the column's label
    ofxManifold::editor::Selection none; // nothing selected: a player

    glm::vec2 point{0.5f, 0.5f};
    float     z = 0.0f;
    int       glideDir = 0;             // -1, 0, +1 while down / up is held

    // Smoothing what is sent: the outputs' levels and the node values, each
    // a list as long as the map makes it. Reset when the map changes.
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
