#pragma once

// ofxManifoldPlayer -- OSC out that grows and shrinks with the map (3B-1).
//
// For whatever is listening: SuperCollider, Max, Pd, TouchDesigner, a
// script. The receiver need not know how many outputs there are; the sender
// says, and says again when that changes:
//
//     /manifold/outputs   N  name0 ch0  name1 ch1 ...
//     /manifold/levels    f0 f1 ... f(N-1)          packed, in that order
//     /manifold/out/NAME  f                          per output
//     /manifold/values    M  name0 v0  name1 v1 ...  packed
//     /manifold/value/NAME f                         per value
//     /manifold/position  x y z
//     /manifold/describe                             in: describe again
//     /manifold/hello     [name]                     in: a receiver is there
//
// The protocol in full is ofxManifoldPlayer/OSC.md; the decisions behind it
// PLAN-round3.md, section J. No sockets here: OscOut says what to send and
// encode() turns it into the bytes of one UDP packet, so the code the tests
// check (tests/vectors/osc.vec, from tests/ref/reference_osc.py, checked
// against python-osc) is the code that sends.

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ofxManifold {
namespace player {

// ---- messages and their bytes (OSC 1.0) ----

struct OscArg {
    char        type = 'f';    // 'i', 'f' or 's'
    std::int32_t i = 0;
    float       f = 0.0f;
    std::string s;

    static OscArg integer(std::int32_t v) { OscArg a; a.type = 'i'; a.i = v; return a; }
    static OscArg number(float v)         { OscArg a; a.type = 'f'; a.f = v; return a; }
    static OscArg text(std::string v)     { OscArg a; a.type = 's'; a.s = std::move(v); return a; }
};

struct OscMessage {
    std::string         address;
    std::vector<OscArg> args;

    std::string tags() const;      // ",ifs..."
};

// One message as the bytes of one UDP packet: the address and the type tags
// as strings, each null-terminated and padded to four bytes; ints and floats
// as four bytes, big-endian; strings as above.
std::vector<std::uint8_t> encode(const OscMessage& m);

// The message in one packet, or false: only i, f and s; every string
// terminated and zero-padded; nothing left over.
bool decode(const std::uint8_t* data, std::size_t size, OscMessage& out);

// A name as it can stand in an address: any of  #*,/?[]{}  becomes _.
std::string oscName(const std::string& name);

// ---- what is sent, frame by frame ----

// One frame's state: the outputs in channel order with their levels, the
// node values, and where the point is.
struct OscOutput { std::string name; int channel = 0; float level = 0.0f; };
struct OscValue  { std::string name; float value = 0.0f; };

struct OscFrame {
    std::vector<OscOutput> outputs;
    std::vector<OscValue>  values;
    bool                   hasPosition = false;
    glm::vec3              position{0.0f};
};

class OscOut {
public:
    static constexpr double kDescribeEvery = 2.0;    // seconds
    static constexpr double kRefreshEvery  = 0.1;    // per output: all again
    static constexpr double kHelloLasts    = 3.0;
    static constexpr float  kChanged       = 1e-6f;

    // ---- settings ----
    void setPrefix(const std::string& p);            // "/manifold"
    const std::string& prefix() const { return prefix_; }
    void setPacked(bool on) { packed_ = on; }
    bool packed() const { return packed_; }
    void setPerOutput(bool on) { perOutput_ = on; }
    bool perOutput() const { return perOutput_; }
    void setRate(double perSecond);                  // 1 to 1000; 60
    double rate() const { return rate_; }

    // The messages to send now, in order. Called every frame; sends at the
    // rate. Packed sends every level at every send. Per output sends what
    // changed by more than kChanged, and everything every kRefreshEvery. The
    // outputs are described on the first send, when they change, when asked,
    // and every kDescribeEvery.
    std::vector<OscMessage> frame(double now, const OscFrame& f);

    // A message from a receiver: describe, or hello.
    void receive(double now, const OscMessage& m);

    // A hello within kHelloLasts; and the name it gave.
    bool connected(double now) const;
    const std::string& peer() const { return peer_; }

    // Start again: the next frame sends, and describes.
    void reset();

private:
    std::string prefix_ = "/manifold";
    bool        packed_ = true;
    bool        perOutput_ = false;
    double      rate_ = 60.0;

    bool   sentYet_ = false;
    double nextSend_ = 0.0;
    bool   describedYet_ = false;
    double lastDescribe_ = 0.0;
    std::vector<std::pair<std::string, int>> described_;
    bool   describeAsked_ = false;
    bool   refreshedYet_ = false;
    double lastRefresh_ = 0.0;
    std::map<std::string, float> sentOut_, sentValue_;   // last sent, by name
    std::string peer_;
    bool   helloYet_ = false;
    double lastHello_ = 0.0;
};

} // namespace player
} // namespace ofxManifold
