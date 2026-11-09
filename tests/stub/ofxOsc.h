#pragma once

// Stand-in for ofxOsc, for the syntax check only (make wrapper): the calls
// example-osc makes, with the signatures of openFrameworks 0.12.1's
// addons/ofxOsc/src headers (ofxOscSender.h, ofxOscReceiver.h,
// ofxOscMessage.h, ofxOscArg.h).

#include <cstddef>
#include <cstdint>
#include <string>

enum ofxOscArgType {
    OFXOSC_TYPE_INT32  = 'i',
    OFXOSC_TYPE_INT64  = 'h',
    OFXOSC_TYPE_FLOAT  = 'f',
    OFXOSC_TYPE_DOUBLE = 'd',
    OFXOSC_TYPE_STRING = 's',
    OFXOSC_TYPE_SYMBOL = 'S',
    OFXOSC_TYPE_INDEXOUTOFBOUNDS = 0
};

class ofxOscMessage {
public:
    void setAddress(const std::string&) {}
    std::string getAddress() const { return {}; }
    std::size_t getNumArgs() const { return 0; }
    ofxOscArgType getArgType(std::size_t) const { return OFXOSC_TYPE_INDEXOUTOFBOUNDS; }
    std::int32_t getArgAsInt32(std::size_t) const { return 0; }
    float getArgAsFloat(std::size_t) const { return 0.0f; }
    std::string getArgAsString(std::size_t) const { return {}; }
    ofxOscMessage& addIntArg(std::int32_t) { return *this; }
    ofxOscMessage& addFloatArg(float) { return *this; }
    ofxOscMessage& addStringArg(const std::string&) { return *this; }
};

class ofxOscSender {
public:
    bool setup(const std::string&, int, bool = false) { return true; }
    bool sendMessage(const ofxOscMessage&, bool = false) { return true; }
};

class ofxOscReceiver {
public:
    bool setup(int) { return true; }
    bool hasWaitingMessages() const { return false; }
    bool getNextMessage(ofxOscMessage&) { return false; }
};
