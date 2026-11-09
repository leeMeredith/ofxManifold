#pragma once

// ofxManifold -- example-osc (round 3B-1).
//
// The smallest player: one map, one point, and OSC out that grows and
// shrinks with the map -- for SuperCollider, Max, Pd, TouchDesigner, a
// script, whatever is listening. Drag the point; hold up or down to move it
// through the levels. What goes out, and whether anyone answered, is on the
// right.
//
// The receiver need not know how many outputs there are: the app describes
// them (/manifold/outputs) on the first send, when they change, when asked,
// and every two seconds. See ofxManifoldPlayer/OSC.md for the protocol, and
// supercollider/manifold-receive.scd and receive.py beside this file for two
// receivers.
//
// Everything that decides what to send is in ofxManifoldPlayer (OscOut,
// encode, decode), tested without openFrameworks; this file owns the
// window, the sockets and the keys.
//
//   drag            move the point
//   up / down       hold: move it through the levels (shift: slowly)
//   p               packed messages on or off
//   i               per-output messages on or off
//   [ ]             send rate down / up
//   space           pause sending
//   o               open a map pair (map + outputs)
//   e               the example map

#include "ofMain.h"
#include "ofxManifold.h"
#include "ofxManifoldEditor.h"
#include "ofxManifoldPlayer.h"
#include "ofxNetwork.h"

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
    ofxManifold::player::OscFrame frameNow() const;

    ofxManifold::editor::Model                    model;
    std::unique_ptr<ofxManifold::Evaluator>       evaluator;
    std::unique_ptr<ofxManifoldRenderer>          renderer;
    ofxManifold::Evaluation                       evaluation;

    glm::vec2 point{0.5f, 0.5f};
    float     z = 0.0f;
    int       glideDir = 0;             // -1, 0, +1 while down / up is held

    ofxManifold::player::OscOut osc;
    ofxUDPManager               sender, listener;
    bool                        paused = false;

    // What went out in the last second, for the panel.
    std::deque<std::pair<double, std::size_t>> sentLog;   // time, bytes
    std::vector<std::string>    lastSent;                 // the last send
    std::string                 message;
    float                       messageUntil = 0.0f;
};
