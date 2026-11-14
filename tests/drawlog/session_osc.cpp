// example-osc through the recording stand-in (tests/drawlog/ofMain.h), as
// session.cpp does the editor: every drawing call of every frame to stdout.
// Not checked against anything -- it shows the player's drawing runs on the
// editor's views (map, faders, level dots, 3D view) with nothing selected,
// and gives a record to compare when that drawing changes. The window is the
// stand-in's 1024 x 640; ofxOsc is tests/stub's, which sends nowhere.

#include "ofApp.h"

#include <cstdio>

namespace {

ofApp* app = nullptr;
int frameNo = 0;

void frame(const char* what) {
    app->update();
    drawlog() += "==== frame " + std::to_string(frameNo++) + ": " + what + "\n";
    app->draw();
}

void key(int k) { app->keyPressed(k); app->keyReleased(k); }
void run(int frames) { for (int i = 0; i < frames; ++i) app->update(); }   // unrecorded
void shiftClick(int x, int y) {
    drawlogHeld().insert(OF_KEY_SHIFT);
    app->mousePressed(x, y, 0);
    app->mouseReleased(x, y, 0);
    drawlogHeld().erase(OF_KEY_SHIFT);
}

} // namespace

int main() {
    ofApp a;
    app = &a;
    a.setup();
    frame("the room, the floor's middle");
    key(OF_KEY_TAB);           frame("tab: the next output ringed");
    key('l');                  frame("links of every node");
    key('l');                  frame("links off");
    key('l');                  frame("links of the nodes heard");
    a.mousePressed(100, 100, 0);
    for (int k = 0; k < 6; ++k) frame("a click across the room, settling");
    a.mousePressed(90, 560, 0); frame("a click on a fader picks its output");
    a.mouseMoved(90, 560);     frame("hovering it: its numbers");
    key('+'); key('+'); key('_'); frame("shift - =: a hundredth at a time");
    key('3');                  frame("3D view off");
    key('3');                  frame("3D view on again");
    shiftClick(80, 80);        frame("shift-click FL: followed, pinned");
    a.mousePressed(225, 225, 0);
    frame("the point by RR: FL still followed");
    shiftClick(150, 150);      frame("shift-click on no node: the loudest again");
    key('E');                  frame("the editor's flat example");
    a.mousePressed(118, 176, 0);
    frame("the flat example, by O: the loudest followed");
    key('l');                  frame("links of the nodes heard");
    key('l');                  frame("links of every node");
    // Movement (3B-2). Each update is 1/60 s of the app's own seconds.
    key('e');                  frame("the room again, moved by hand");
    key('m');  run(40);        frame("m: jump -- stepped, on the map");
    key('m');  run(40);        frame("m: glide");
    key('c');                  frame("c: steps on your clicks");
    key(OF_KEY_RETURN); run(30); frame("return: one step");
    key('h');  key('c'); key('c'); run(70);
    frame("h: height across the levels; steps on a timer again");
    key('n');  key('<'); key('{'); run(70);
    frame("n, <, {: a new seed, shorter travel, shorter steps");
    key('m');                  frame("m: your hand again");
    key('r');
    for (int i = 0; i < 10; ++i) { a.mouseDragged(100 + 6 * i, 150, 0); app->update(); }
    frame("recording: REC, the path drawn as it grows");
    for (int i = 10; i < 20; ++i) { a.mouseDragged(100 + 6 * i, 150, 0); app->update(); }
    key('r');  run(10);        frame("r, drag, r: the recording plays, its loop drawn");
    run(60);                   frame("the recording looping");
    key('r');                  frame("r again: the playing stops, your hand records anew");
    for (int i = 0; i < 15; ++i) { a.mouseDragged(150, 100 + 6 * i, 0); app->update(); }
    key('r');  run(20);        frame("r: the new recording plays");
    key('M');  run(20);        frame("M: your hand, the point where it was");
    key('?');                  frame("?: every key");
    a.mousePressed(300, 300, 0); frame("a click: back to the map");
    key('b');                  frame("b: SuperCollider's bars -- no phase heard");
    std::fwrite(drawlog().data(), 1, drawlog().size(), stdout);
    return 0;
}
