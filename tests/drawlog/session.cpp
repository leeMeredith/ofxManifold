// A scripted editing session, recorded frame by frame through the recording
// stand-in (tests/drawlog/ofMain.h). Built against example-editor as it is,
// it writes every drawing call of every frame to stdout. Two versions of the
// editor that should look identical must produce identical output.
//
// The window is 1024 x 640, so the map is the square at (24, 24), 462 wide,
// y flipped: a node at (x, y) is on screen at (24 + 462x, 24 + 462(1 - y)).
// The faders start at y 502.

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

// Screen position of a map point.
int sx(float x) { return int(std::lround(24.0f + 462.0f * x)); }
int sy(float y) { return int(std::lround(24.0f + 462.0f * (1.0f - y))); }

void click(int x, int y) { app->mousePressed(x, y, 0); app->mouseReleased(x, y, 0); }
void key(int k) { app->keyPressed(k); app->keyReleased(k); }
void hold(int k) { drawlogHeld().insert(k); }
void release(int k) { drawlogHeld().erase(k); }

} // namespace

int main() {
    ofApp a;
    app = &a;
    a.setup();
    frame("the example map");

    a.mouseMoved(60, 560);
    frame("hover over the first fader");

    click(sx(0.30f), sy(0.50f));                   // O
    frame("O selected: links, fill lines, contributions");

    key('q');
    frame("fill step down");
    key('Q');
    frame("fine fill step down");

    hold(OF_KEY_SHIFT);
    click(sx(0.50f), sy(0.50f));                   // E as well
    release(OF_KEY_SHIFT);
    frame("O and E selected");

    key('b');
    frame("b on O and E");

    a.mousePressed(sx(0.90f), sy(0.65f), 0);       // C
    a.mouseDragged(sx(0.90f) - 10, sy(0.65f) + 14, 0);
    frame("dragging C: keys dimmed");
    a.mouseReleased(sx(0.90f) - 10, sy(0.65f) + 14, 0);
    frame("after the drag");

    // From empty space, a box around A and B (both at y 0.35).
    a.mousePressed(sx(0.97f), sy(0.25f), 0);
    a.mouseDragged(sx(0.60f), sy(0.42f), 0);
    frame("box select in progress");
    a.mouseReleased(sx(0.60f), sy(0.42f), 0);
    frame("box select done");

    a.keyPressed(' ');
    a.mousePressed(sx(0.40f), sy(0.55f), 0);
    a.mouseDragged(sx(0.42f), sy(0.52f), 0);
    frame("auditioning");
    a.mouseReleased(sx(0.42f), sy(0.52f), 0);
    a.keyReleased(' ');
    frame("audition released");

    key('v');
    frame("audition locked");
    key('v');

    click(60, 560);
    frame("fader clicked");
    hold(OF_KEY_SHIFT);
    click(120, 560);
    release(OF_KEY_SHIFT);
    frame("second fader shift-clicked");

    key('B');
    frame("B: every output");
    key('c');
    frame("c: cleared");

    key('4');
    frame("polar grid");
    key(']');
    key('r');
    frame("more spokes, rotated");
    key('3');
    frame("triangular grid");
    key('1');
    frame("free grid");

    key('d');
    frame("derived output");

    key('p');
    for (int i = 0; i < 30; ++i) key('o');
    frame("thirty more outputs");
    a.mouseMoved(400, 560);
    frame("hover among narrow faders");

    key(OF_KEY_ESC);
    key('x');
    frame("empty map");
    key('p');
    click(sx(0.2f), sy(0.2f));
    click(sx(0.8f), sy(0.2f));
    click(sx(0.5f), sy(0.8f));
    frame("three nodes placed");
    key('a');
    key('f');
    frame("joined");

    hold(OF_KEY_COMMAND);
    key('z');
    release(OF_KEY_COMMAND);
    frame("undone");

    key('a');                                      // undo cleared it
    key(OF_KEY_DEL);
    frame("delete");

    // Round 3A-6: levels. Appended, so every frame above is as it was.
    key('e');
    key('n');
    key('n');
    key(OF_KEY_DOWN);
    frame("three levels, on the middle one: the column of dots");
    click(sx(0.30f), sy(0.50f));                   // O
    key('y');
    key('Y');
    frame("O raised on the middle level: its offset shown");
    a.keyPressed(' ');
    a.keyPressed(OF_KEY_UP);
    for (int i = 0; i < 30; ++i) a.update();       // half a second held
    frame("auditioning, glided up between the middle and top levels");
    a.keyReleased(OF_KEY_UP);
    for (int i = 0; i < 60; ++i) a.update();
    a.keyPressed(OF_KEY_UP);
    for (int i = 0; i < 60; ++i) a.update();
    frame("glided past the top level: outside, the mark greyed");
    a.keyReleased(OF_KEY_UP);
    a.keyReleased(' ');
    frame("audition released: back on the middle level");
    // 3A-6b: the dots answer the mouse; i and k move a whole level.
    {
        // the column as the map view places it: the map is 462 square at
        // (24, 24) in this window, the column 15 to its right, inset 15%
        const float x = 24.0f + 462.0f + 15.0f;
        const float top = 24.0f + 462.0f * 0.15f;
        const float bottom = 24.0f + 462.0f * 0.85f;
        click(int(x), int(top));                     // the top level's dot
        frame("clicked the top dot: level 3 current");
        a.mousePressed(int(x), int((top + bottom) / 2), 0);
        a.mouseDragged(int(x), int((top + bottom) / 2 + 20), 0);
        frame("dragging the middle dot down: its landing shown");
        a.mouseReleased(int(x), int((top + bottom) / 2 + 20), 0);
        frame("let go: the middle level moved");
    }
    key('i');
    key('K');
    frame("i then shift-k on the middle level");
    // 3A-7: a value named on the typing line, and set.
    key('g');
    for (char c : std::string("bright")) a.keyPressed(c);
    frame("typing a value's name");
    a.keyPressed(13);
    click(sx(0.30f), sy(0.50f));                   // O
    key('\'');
    key('\'');
    key('"');
    frame("O's bright raised: its number beside it, the readout");
    key('z');
    a.keyPressed(127);
    frame("z: the value's line, to rename or re-range");
    a.keyPressed(27);
    hold(OF_KEY_COMMAND);
    key('3');
    release(OF_KEY_COMMAND);
    frame("the 3D view, its levels keys beneath it, the rest beside");
    a.keyPressed(' ');
    a.keyPressed(OF_KEY_UP);
    for (int i = 0; i < 30; ++i) a.update();
    a.keyReleased(OF_KEY_UP);
    frame("auditioning between levels in the 3D view: the nodes heard glow");
    a.keyReleased(' ');
    hold(OF_KEY_COMMAND);
    key(OF_KEY_DEL);
    release(OF_KEY_COMMAND);
    frame("the middle level removed");

    std::fputs(drawlog().c_str(), stdout);
    return 0;
}
