// A RECORDING stand-in for openFrameworks: every drawing call is written to a
// log instead of drawn, with its numbers and text. Used to check that a change
// meant to leave the screen alone -- moving drawing code between files, say --
// really does: record a scripted session before, record it after, compare.
//
// Fuller than tests/stub/ofMain.h on purpose. That stub only has to compile;
// this one has to KEEP what is drawn: colours, rectangles, the text of
// numbers. A recorder that threw them away would pass a move that broke them.
//
// Not a substitute for looking at the real screen. It proves two versions of
// the code draw the SAME thing, not that the thing is right.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#define TWO_PI 6.28318530718f
#define PI 3.14159265359f
#define HALF_PI 1.57079632679f

// ---- the log ----------------------------------------------------------------
inline std::string& drawlog() { static std::string s; return s; }
inline std::set<int>& drawlogHeld() { static std::set<int> s; return s; }
inline std::string drawlogNum(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}
inline void drawlogRecord(const std::string& what,
                          std::initializer_list<double> nums,
                          const std::string& text = std::string()) {
    std::string& l = drawlog();
    l += what;
    for (double v : nums) l += " " + drawlogNum(v);
    if (!text.empty() || what == "text") l += " \"" + text + "\"";
    l += "\n";
}

// ---- values that keep what they are given -------------------------------------
struct ofColor {
    unsigned char r = 255, g = 255, b = 255, a = 255;
    ofColor() {}
    ofColor(int R, int G, int B) : r(R), g(G), b(B), a(255) {}
    ofColor(int R, int G, int B, int A) : r(R), g(G), b(B), a(A) {}
};
struct ofRectangle {
    float x = 0, y = 0, width = 0, height = 0;
    ofRectangle() {}
    ofRectangle(float X, float Y, float W, float H) : x(X), y(Y), width(W), height(H) {}
    float getX() const { return x; }
    float getY() const { return y; }
    float getWidth() const { return width; }
    float getHeight() const { return height; }
    float getRight() const { return x + width; }
    float getBottom() const { return y + height; }
    bool inside(float px, float py) const {
        return px > x && py > y && px < x + width && py < y + height;
    }
};
struct ofBuffer {
    std::string t;
    ofBuffer() {}
    void set(const std::string& s) { t = s; }
    std::string getText() const { return t; }
};
inline ofBuffer ofBufferFromFile(const std::string&) { return ofBuffer(); }
inline bool ofBufferToFile(const std::string&, ofBuffer&) { return true; }
inline int ofToInt(const std::string& s) { return std::atoi(s.c_str()); }

// ---- drawing: recorded ---------------------------------------------------------
inline void ofPushStyle() { drawlogRecord("push", {}); }
inline void ofPopStyle() { drawlogRecord("pop", {}); }
inline void ofPushMatrix() { drawlogRecord("pushm", {}); }
inline void ofPopMatrix() { drawlogRecord("popm", {}); }
inline void ofFill() { drawlogRecord("fill", {}); }
inline void ofNoFill() { drawlogRecord("nofill", {}); }
inline void ofSetColor(const ofColor& c) { drawlogRecord("color", {double(c.r), double(c.g), double(c.b), double(c.a)}); }
inline void ofSetColor(const ofColor& c, float a) { drawlogRecord("color", {double(c.r), double(c.g), double(c.b), a}); }
inline void ofSetColor(float r, float g, float b) { drawlogRecord("color", {r, g, b, 255}); }
inline void ofSetColor(float r, float g, float b, float a) { drawlogRecord("color", {r, g, b, a}); }
inline void ofSetLineWidth(float w) { drawlogRecord("width", {w}); }
inline void ofBackground(int r, int g, int b) { drawlogRecord("background", {double(r), double(g), double(b)}); }
inline void ofTranslate(float x, float y) { drawlogRecord("translate", {x, y}); }
inline void ofRotateRad(float a) { drawlogRecord("rotate", {a}); }
inline void ofDrawRectangle(const ofRectangle& r) { drawlogRecord("rect", {r.x, r.y, r.width, r.height}); }
inline void ofDrawRectangle(float x, float y, float w, float h) { drawlogRecord("rect", {x, y, w, h}); }
inline void ofDrawTriangle(float a, float b, float c, float d, float e, float f) { drawlogRecord("tri", {a, b, c, d, e, f}); }
inline void ofDrawLine(float a, float b, float c, float d) { drawlogRecord("line", {a, b, c, d}); }
inline void ofDrawCircle(float x, float y, float r) { drawlogRecord("circle", {x, y, r}); }
inline void ofDrawBitmapString(const std::string& s, float x, float y) { drawlogRecord("text", {x, y}, s); }
inline void ofBeginShape() { drawlogRecord("begin", {}); }
inline void ofEndShape(bool close) { drawlogRecord("end", {double(close)}); }
inline void ofVertex(float x, float y) { drawlogRecord("v", {x, y}); }

// ---- the window, keys and files: fixed, or set by the script --------------------
inline void ofSetEscapeQuitsApp(bool) {}
inline void ofSetWindowTitle(const std::string& s) { drawlogRecord("title", {}, s); }
inline void ofSetFrameRate(int) {}
inline float ofGetElapsedTimef() { return 0.f; }
inline void ofEnableAlphaBlending() {}
// The window's size, which a session may change (3A-10: the layout at
// several sizes).
inline int& drawlogWidth()  { static int w = 1024; return w; }
inline int& drawlogHeight() { static int h = 640;  return h; }
inline int ofGetWidth() { return drawlogWidth(); }
inline int ofGetHeight() { return drawlogHeight(); }
inline int ofGetScreenWidth() { return 1440; }
inline int ofGetScreenHeight() { return 900; }
inline void ofSetWindowShape(int, int) {}
inline void ofSetWindowPosition(int, int) {}
inline void ofToggleFullscreen() { drawlogRecord("fullscreen", {}); }
#define OF_KEY_BACKSPACE 8
#define OF_KEY_DEL 127
#define OF_KEY_ESC 27
#define OF_KEY_TAB 9
#define OF_KEY_RETURN 13
#define OF_KEY_SHIFT 0x1
#define OF_KEY_ALT 0x2
#define OF_KEY_COMMAND 0x4
#define OF_KEY_CONTROL 0x8
inline bool ofGetKeyPressed(int k) { return drawlogHeld().count(k) > 0; }
struct ofFile { static bool doesFileExist(const std::string&) { return false; } };
inline float ofDegToRad(float d) { return d * 0.01745329f; }
inline float ofRadToDeg(float r) { return r * 57.2957795f; }

// The font never loads here, so text goes through ofDrawBitmapString and is
// recorded with its words.
#define OF_TTF_MONO "mono"
// Fails to load, so the editor falls back on the bitmap font's 8 x 13, as
// the session has always been recorded -- until a session turns fonts on;
// then it loads at any size, measured as Menlo is at 72 dpi (0.602 of the
// size across, 1.17 down), so the layout at big text sizes can be recorded.
inline bool& drawlogFonts() { static bool on = false; return on; }
class ofTrueTypeFont {
public:
    bool load(const std::string&, int size, bool = true, bool = true,
              bool = false, float = 0.3f, int = 0) {
        size_ = drawlogFonts() ? size : 0;
        return size_ > 0;
    }
    bool isLoaded() const { return size_ > 0; }
    void drawString(const std::string& s, float x, float y) const { drawlogRecord("ttf", {x, y}, s); }
    float stringWidth(const std::string& s) const { return 0.602f * size_ * float(s.size()); }
    float getLineHeight() const { return 1.17f * size_; }
private:
    int size_ = 0;
};
inline float ofClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float ofRandom(float a, float b) { return (a + b) * 0.5f; }
// As openFrameworks: plain stream output, or fixed with a precision.
template <typename T> std::string ofToString(const T& v) {
    std::ostringstream o; o << v; return o.str();
}
template <typename T> std::string ofToString(const T& v, int precision) {
    std::ostringstream o; o << std::fixed << std::setprecision(precision) << v; return o.str();
}
inline std::string ofToDataPath(const std::string& s) { return s; }
class ofFileDialogResult {
public:
    std::string getName() { return fileName; }
    std::string getPath() { return filePath; }
    std::string filePath;
    std::string fileName;
    bool bSuccess = false;
};
inline ofFileDialogResult ofSystemSaveDialog(const std::string&, const std::string&) { return {}; }
inline ofFileDialogResult ofSystemLoadDialog(const std::string& = "", bool = false, const std::string& = "") { return {}; }
// The arrows and the 3D view (round 3A-6). The camera is recorded where it
// begins and ends; inside, 3D lines and spheres are recorded with their
// world coordinates.
#define OF_KEY_LEFT 356
#define OF_KEY_UP 357
#define OF_KEY_RIGHT 358
#define OF_KEY_DOWN 359
inline double ofGetLastFrameTime() { return 1.0 / 60.0; }
inline void ofDrawLine(const glm::vec3& a, const glm::vec3& b) { drawlogRecord("line3", {a.x, a.y, a.z, b.x, b.y, b.z}); }
inline void ofDrawSphere(const glm::vec3& p, float r) { drawlogRecord("sphere", {p.x, p.y, p.z, r}); }
inline void ofSetSphereResolution(int) {}
class ofEasyCam {
public:
    void begin(const ofRectangle& r) { drawlogRecord("cam", {r.x, r.y, r.width, r.height}); }
    void end() { drawlogRecord("camend", {}); }
    void setTarget(const glm::vec3&) {}
    void setDistance(float) {}
    void setControlArea(const ofRectangle&) {}
    void enableMouseInput() {}
    void disableMouseInput() {}
    void setPosition(float, float, float) {}
    void lookAt(const glm::vec3&) {}
};
struct ofLogNotice { template <typename T> ofLogNotice& operator<<(const T&) { return *this; } };
struct ofLogError { template <typename T> ofLogError& operator<<(const T&) { return *this; } };
struct ofBaseApp {
    virtual ~ofBaseApp() {}
    virtual void setup() {} virtual void update() {} virtual void draw() {}
    virtual void mouseDragged(int, int, int) {}
    virtual void mousePressed(int, int, int) {}
    virtual void mouseReleased(int, int, int) {}
    virtual void keyReleased(int) {}
    virtual void mouseMoved(int, int) {}
    virtual void keyPressed(int) {}
};
