// Minimal stub of the oF API this addon uses, so the wrapper can be
// syntax-checked without an openFrameworks install. NOT a substitute for
// building against real oF.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#define TWO_PI 6.28318530718f
struct ofColor {
    ofColor(){} ofColor(int,int,int){} ofColor(int,int,int,int){}
};
struct ofRectangle {
    ofRectangle(){} ofRectangle(float,float,float,float){}
    float getX() const {return 0;} float getY() const {return 0;}
    float getWidth() const {return 1;} float getHeight() const {return 1;}
    float getRight() const {return 1;}
};
struct ofBuffer { ofBuffer(){} void set(const std::string&){}
                  std::string getText() const {return "";} };
inline ofBuffer ofBufferFromFile(const std::string&){return ofBuffer();}
inline void ofPushStyle(){} inline void ofPopStyle(){}
inline void ofPushMatrix(){} inline void ofPopMatrix(){}
inline void ofFill(){} inline void ofNoFill(){}
inline void ofSetColor(const ofColor&){}
inline void ofSetColor(const ofColor&,float){}
inline void ofSetColor(float,float,float){}
inline void ofSetColor(float,float,float,float){}
inline void ofSetLineWidth(float){}
inline void ofBackground(int,int,int){}
inline void ofTranslate(float,float){}
inline void ofRotateRad(float){}
inline void ofDrawRectangle(const ofRectangle&){}
inline void ofDrawRectangle(float,float,float,float){}
inline void ofDrawTriangle(float,float,float,float,float,float){}
inline void ofDrawLine(float,float,float,float){}
inline void ofDrawCircle(float,float,float){}
inline void ofDrawBitmapString(const std::string&,float,float){}
inline void ofBeginShape(){} inline void ofEndShape(bool){}
inline void ofVertex(float,float){}
inline void ofSetWindowTitle(const std::string&){}
inline void ofSetFrameRate(int){}
inline float ofGetElapsedTimef(){return 0.f;}
inline void ofEnableAlphaBlending(){}
inline float ofGetWidth(){return 1024;} inline float ofGetHeight(){return 640;}
inline float ofClamp(float v,float a,float b){return v<a?a:(v>b?b:v);}
template<typename T> std::string ofToString(const T&){return "";}
template<typename T> std::string ofToString(const T&,int){return "";}
inline std::string ofToDataPath(const std::string& s){return s;}
inline bool ofBufferToFile(const std::string&,ofBuffer&){return true;}
struct ofLogNotice { template<typename T> ofLogNotice& operator<<(const T&){return *this;} };
struct ofLogError { template<typename T> ofLogError& operator<<(const T&){return *this;} };
struct ofBaseApp {
    virtual ~ofBaseApp(){}
    virtual void setup(){} virtual void update(){} virtual void draw(){}
    virtual void mouseDragged(int,int,int){}
    virtual void mousePressed(int,int,int){}
    virtual void mouseReleased(int,int,int){}
    virtual void mouseMoved(int,int){}
    virtual void keyPressed(int){}
};
