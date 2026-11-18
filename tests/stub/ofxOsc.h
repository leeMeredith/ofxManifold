#pragma once

// Stand-in for ofxOsc, for the syntax check only (make wrapper): the calls
// example-osc makes, with the signatures of openFrameworks 0.12.1's
// addons/ofxOsc/src headers (ofxOscSender.h, ofxOscReceiver.h,
// ofxOscMessage.h, ofxOscArg.h).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

enum ofxOscArgType {
    OFXOSC_TYPE_INT32  = 'i',
    OFXOSC_TYPE_INT64  = 'h',
    OFXOSC_TYPE_FLOAT  = 'f',
    OFXOSC_TYPE_DOUBLE = 'd',
    OFXOSC_TYPE_STRING = 's',
    OFXOSC_TYPE_SYMBOL = 'S',
    OFXOSC_TYPE_INDEXOUTOFBOUNDS = 0
};

// Messages hold what they are given, so a drawlog session can hand the app
// one (ofxOscStubInbox) and read what it sent (ofxOscStubOutbox).
class ofxOscMessage {
public:
    struct Arg { ofxOscArgType type; std::int32_t i; float f; std::string s; };
    void setAddress(const std::string& a) { address_ = a; }
    std::string getAddress() const { return address_; }
    std::size_t getNumArgs() const { return args_.size(); }
    ofxOscArgType getArgType(std::size_t k) const {
        return k < args_.size() ? args_[k].type : OFXOSC_TYPE_INDEXOUTOFBOUNDS;
    }
    std::int32_t getArgAsInt32(std::size_t k) const { return args_[k].i; }
    float getArgAsFloat(std::size_t k) const { return args_[k].f; }
    std::string getArgAsString(std::size_t k) const { return args_[k].s; }
    ofxOscMessage& addIntArg(std::int32_t v) { args_.push_back({OFXOSC_TYPE_INT32, v, 0.0f, {}}); return *this; }
    ofxOscMessage& addFloatArg(float v) { args_.push_back({OFXOSC_TYPE_FLOAT, 0, v, {}}); return *this; }
    ofxOscMessage& addStringArg(const std::string& v) { args_.push_back({OFXOSC_TYPE_STRING, 0, 0.0f, v}); return *this; }
    std::string getRemoteHost() const { return host_; }
    int getRemotePort() const { return port_; }
    void setRemoteEndpoint(const std::string& host, int port) { host_ = host; port_ = port; }
private:
    std::string address_;
    std::vector<Arg> args_;
    std::string host_ = "127.0.0.1";
    int port_ = 0;
};

// What waits on each port, and what was sent to "host:port".
inline std::map<int, std::deque<ofxOscMessage>>& ofxOscStubInbox() {
    static std::map<int, std::deque<ofxOscMessage>> inbox;
    return inbox;
}
inline std::vector<std::pair<std::string, ofxOscMessage>>& ofxOscStubOutbox() {
    static std::vector<std::pair<std::string, ofxOscMessage>> outbox;
    return outbox;
}

class ofxOscSender {
public:
    bool setup(const std::string& host, int port, bool = false) {
        to_ = host + ":" + std::to_string(port);
        return true;
    }
    bool sendMessage(const ofxOscMessage& m, bool = false) {
        ofxOscStubOutbox().emplace_back(to_, m);
        if (ofxOscStubOutbox().size() > 4096) ofxOscStubOutbox().clear();
        return true;
    }
private:
    std::string to_;
};

struct ofxOscReceiverSettings {
    int port = 0;
    std::string host = "0.0.0.0";
    bool reuse = true;
    bool start = true;
};

class ofxOscReceiver {
public:
    bool setup(int port) { port_ = port; return true; }
    bool setup(const ofxOscReceiverSettings& s) { port_ = s.port; return true; }
    bool hasWaitingMessages() const {
        const auto it = ofxOscStubInbox().find(port_);
        return it != ofxOscStubInbox().end() && !it->second.empty();
    }
    bool getNextMessage(ofxOscMessage& m) {
        auto& q = ofxOscStubInbox()[port_];
        if (q.empty()) return false;
        m = q.front();
        q.pop_front();
        return true;
    }
private:
    int port_ = 0;
};
