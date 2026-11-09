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
    key('3');
    key('E');                  frame("the editor's flat example");
    key('l');                  frame("its links, every node");
    std::fwrite(drawlog().data(), 1, drawlog().size(), stdout);
    return 0;
}
