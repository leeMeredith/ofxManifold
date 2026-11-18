#pragma once

// ofxManifold -- points moved from outside (round 3B-2f).
//
// Anything on this machine -- SuperCollider, the SuperCollider Audio
// Engine's panel, a controller app -- can take a point and move it, by OSC to
// the map's answer port (the port its roll call gives). PointControl is the
// whole of the rule, with no sockets: the app hands it each message and who
// sent it, and applies what it returns. It follows the SuperCollider Audio
// Engine's rules for OSC coming in: paths fixed by the map and its control
// file, never chosen by a sender; exact argument counts; finite numbers;
// loopback only; every refusal counted, with its reason.
//
//     <prefix>/point/<n>/take     i id, i reply port   answered
//     <prefix>/point/<n>/release  i id                 answered
//     <prefix>/point/<n>/xy       f x, f y             from the holder
//     <prefix>/point/<n>/z        f z                  from the holder
//     <prefix>/point/<n>/u        f u                  from the holder
//     <prefix>/point/<n>/xyz      f x, f y, f z        from the holder
//     <prefix>/point/<n>/reply    i id, i accepted, s command, s reason   out
//     <prefix>/point/<n>/lost     s reason                                out
//                                 ("hand", "silent", or "removed": 3B-2g)
//
// <n> is the point's number, from 1, or its name if the control file gives it
// one. A reply goes to the sender's IP at the port its take named, or its own
// port when that is 0. A repeat of the last request id from the same sender
// is answered again, not acted on twice. Moves are not answered: at sixty a
// second, from the holder only.
//
// One driver per point: the hand, its own movement, or outside. Taking it
// stops its own movement; releasing it, or the holder falling silent, gives
// it back; the hand always wins, and the holder is told. Nothing from outside
// puts a point where its own movement could not: an xy off the floor plan is
// refused; z and u are held to the levels.

#include "ofxManifoldOsc.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace player {

// At most this many points, whatever a file says: a sender never adds one.
constexpr int kMaxPoints = 16;

// The inlets a point can take moves on. take and release are always open:
// they are the boundary itself.
enum Inlet : unsigned { InletXY = 1u, InletZ = 2u, InletU = 4u, InletXYZ = 8u,
                        InletsAll = 15u };

// What a controller sends on one axis, mapped onto the map's own extent: x
// and y onto 0..1, z onto the lowest to highest level start, u onto 0 to the
// top level. Unset: the map's own units.
struct ControlRange {
    bool  set = false;
    float lo = 0.0f, hi = 1.0f;
};

struct PointSpec {
    std::string  name;                 // "": reached by number only
    unsigned     inlets = InletsAll;
    ControlRange x, y, z, u;
    float        silence = 5.0f;       // seconds before a silent hold ends; 0: never
};

// What a map's control file says -- or, without one, one point taking every
// inlet.
struct ControlSpec {
    std::vector<PointSpec> points{PointSpec{}};
};

// Who sent a message: where it came from.
struct OscPeer {
    std::string ip;
    int         port = 0;
    bool operator==(const OscPeer& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const OscPeer& o) const { return !(*this == o); }
    std::string str() const { return ip + ":" + std::to_string(port); }
};

// A move to apply, in the map's own units -- x and y on the floor plan, z a
// height, u a level position -- already mapped and held.
struct PointMove {
    int   point = 1;                   // from 1
    bool  hasXY = false, hasZ = false, hasU = false;
    float x = 0.0f, y = 0.0f, z = 0.0f, u = 0.0f;
};

// A point changing hands.
struct PointEvent {
    enum Kind { Taken, Returned } kind = Taken;
    int         point = 1;
    std::string why;                   // Returned: "released", "silent", "hand"
    OscPeer     by;
};

// A message to send: a reply, or a holder told it has lost the point.
struct PointReply {
    OscPeer    to;
    OscMessage message;
};

class PointControl {
public:
    PointControl() { setSpec(ControlSpec{}); }

    // The map's prefix, as OscOut has it: "/manifold/room".
    void setPrefix(const std::string& prefix) { prefix_ = prefix; }
    const std::string& prefix() const { return prefix_; }

    // The points and their inlets. Every hold ends (a new map, a new file):
    // no event, no message -- the app starts over with it.
    void setSpec(const ControlSpec& spec) {
        spec_ = spec;
        if (spec_.points.empty()) spec_.points.push_back(PointSpec{});
        if (int(spec_.points.size()) > kMaxPoints) spec_.points.resize(kMaxPoints);
        state_.assign(spec_.points.size(), Hold{});
    }
    const ControlSpec& spec() const { return spec_; }
    int count() const { return int(spec_.points.size()); }

    // A point added after the last (round 3B-2g: a in the player), taking
    // every inlet unless told otherwise. Every hold is kept. False at
    // kMaxPoints.
    bool addPoint(const PointSpec& p = PointSpec{}) {
        if (count() >= kMaxPoints) return false;
        spec_.points.push_back(p);
        state_.push_back(Hold{});
        return true;
    }
    // The last point removed -- only the last, so no point's number changes
    // under a sender. Its holder, if any, is told ("removed"); the others
    // keep theirs. False when only point 1 is left.
    bool removeLast() {
        if (count() <= 1) return false;
        if (held(count())) lose(count(), "removed");
        spec_.points.pop_back();
        state_.pop_back();
        return true;
    }

    // Whether (x, y) is on the floor plan. Unset: everywhere.
    void setInside(std::function<bool(float, float)> inside) { inside_ = std::move(inside); }

    // The levels' extent: the lowest and highest level start, and the top
    // level's number. z and u are held to it; ranges map onto it.
    void setExtent(float zLow, float zHigh, float uTop) {
        zLow_ = zLow; zHigh_ = zHigh; uTop_ = uTop;
    }

    // One message from `from`. False: not ours (not under prefix/point/),
    // left for the app. True: handled, accepted or refused.
    bool receive(double now, const OscMessage& m, const OscPeer& from);

    // A silent holder lets go after its point's silence.
    void tick(double now);

    // The hand takes the point back: the holder is told.
    void handTook(int point);

    // What to apply, what changed hands, what to send -- each taken once.
    std::vector<PointMove>  takeMoves()   { return std::exchange(moves_, {}); }
    std::vector<PointEvent> takeEvents()  { return std::exchange(events_, {}); }
    std::vector<PointReply> takeReplies() { return std::exchange(replies_, {}); }

    bool held(int point) const { return valid(point) && state_[point - 1].held; }
    OscPeer holder(int point) const { return valid(point) ? state_[point - 1].holder : OscPeer{}; }

    // Counted: what was taken in, what was refused and why, and the last
    // refusal.
    long accepted() const { return accepted_; }
    long refused() const { return refused_; }
    const std::map<std::string, long>& refusedBy() const { return refusedBy_; }
    const std::string& lastRefusal() const { return lastRefusal_; }

    // The addresses a point answers on, in full, for the screen.
    std::string address(int point, const std::string& command) const {
        return prefix_ + "/point/" + std::to_string(point) + "/" + command;
    }

    // A point's inlets as the description says them: "xy z u xyz".
    static std::string inletNames(unsigned inlets) {
        std::string s;
        auto add = [&](unsigned bit, const char* name) {
            if (inlets & bit) s += (s.empty() ? "" : " ") + std::string(name);
        };
        add(InletXY, "xy"); add(InletZ, "z"); add(InletU, "u"); add(InletXYZ, "xyz");
        return s;
    }

private:
    struct Hold {
        bool    held = false;
        OscPeer holder;
        OscPeer replyTo;               // the holder's answers go here
        double  heard = 0.0;           // last message from the holder
        // The last command answered, to answer a repeat again.
        bool    answered = false;
        OscPeer lastFrom;
        int     lastId = 0;
        std::string lastCommand;
        OscMessage  lastReply;
        OscPeer     lastReplyTo;
    };

    bool valid(int point) const { return point >= 1 && point <= count(); }

    void refuse(const std::string& why) {
        ++refused_;
        ++refusedBy_[why];
        lastRefusal_ = why;
    }

    // "1" -> 1, "lead" -> the point named so; 0: none.
    int pointFor(const std::string& token) const {
        if (token.empty() || token.size() > 32) return 0;
        bool digits = true;
        for (char c : token) digits = digits && c >= '0' && c <= '9';
        if (digits) {
            if (token[0] == '0' || token.size() > 2) return 0;
            const int n = std::stoi(token);
            return valid(n) ? n : 0;
        }
        for (int k = 0; k < count(); ++k) {
            if (!spec_.points[k].name.empty() && spec_.points[k].name == token) return k + 1;
        }
        return 0;
    }

    static bool finiteArg(const OscArg& a, float& v) {
        if (a.type == 'i') { v = float(a.i); return true; }
        if (a.type == 'f' && std::isfinite(a.f)) { v = a.f; return true; }
        return false;
    }
    static bool numberArg(const OscArg& a) { return a.type == 'i' || a.type == 'f'; }

    static float mapped(float v, const ControlRange& r, float lo, float hi) {
        if (!r.set) return v;
        return lo + (v - r.lo) / (r.hi - r.lo) * (hi - lo);
    }
    float heldZ(float z) const { return std::min(std::max(z, zLow_), zHigh_); }
    float heldU(float u) const { return std::min(std::max(u, 0.0f), uTop_); }

    void answer(int point, const OscPeer& to, int id, bool ok,
                const std::string& command, const std::string& why,
                const OscPeer& from) {
        OscMessage r{address(point, "reply"),
                     {OscArg::integer(id), OscArg::integer(ok ? 1 : 0),
                      OscArg::text(command), OscArg::text(why)}};
        Hold& h = state_[point - 1];
        h.answered = true;
        h.lastFrom = from;
        h.lastId = id;
        h.lastCommand = command;
        h.lastReply = r;
        h.lastReplyTo = to;
        replies_.push_back({to, std::move(r)});
    }

    void lose(int point, const std::string& why) {
        Hold& h = state_[point - 1];
        replies_.push_back({h.replyTo, OscMessage{address(point, "lost"), {OscArg::text(why)}}});
        events_.push_back({PointEvent::Returned, point, why, h.holder});
        h.held = false;
        h.holder = OscPeer{};
        h.replyTo = OscPeer{};
    }

    ControlSpec spec_;
    std::vector<Hold> state_;
    std::string prefix_ = "/manifold";
    std::function<bool(float, float)> inside_;
    float zLow_ = 0.0f, zHigh_ = 0.0f, uTop_ = 0.0f;

    std::vector<PointMove>  moves_;
    std::vector<PointEvent> events_;
    std::vector<PointReply> replies_;
    long accepted_ = 0, refused_ = 0;
    std::map<std::string, long> refusedBy_;
    std::string lastRefusal_;
};

inline bool PointControl::receive(double now, const OscMessage& m, const OscPeer& from) {
    const std::string head = prefix_ + "/point/";
    if (m.address.compare(0, head.size(), head) != 0) return false;
    const std::string rest = m.address.substr(head.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos || rest.find('/', slash + 1) != std::string::npos) {
        refuse("unknown command");
        return true;
    }
    const std::string token = rest.substr(0, slash);
    const std::string command = rest.substr(slash + 1);

    // Loopback only: before anything else, and never answered.
    if (from.ip != "127.0.0.1") { refuse("not loopback"); return true; }

    const int point = pointFor(token);
    if (point == 0) { refuse("no such point"); return true; }
    Hold& h = state_[point - 1];
    const PointSpec& ps = spec_.points[point - 1];

    // ---- commands: a request id, answered ----
    if (command == "take" || command == "release") {
        const std::size_t want = command == "take" ? 2 : 1;
        // The request id: a positive int32, first. Without one there is
        // nothing to answer.
        if (m.args.empty() || m.args[0].type != 'i' || m.args[0].i < 1) {
            refuse("bad request id");
            return true;
        }
        const int id = m.args[0].i;
        // The reply port asked for; checked below. Until then, the sender.
        OscPeer replyTo = from;
        bool argsOk = m.args.size() == want;
        if (argsOk && command == "take") {
            const OscArg& p = m.args[1];
            if (p.type != 'i' || !(p.i == 0 || (p.i >= 1024 && p.i <= 65535))) argsOk = false;
            else if (p.i != 0) replyTo.port = p.i;
        }
        // A repeat from the same sender: the same answer, nothing done again.
        if (argsOk && h.answered && h.lastFrom == from && h.lastId == id
            && h.lastCommand == command) {
            replies_.push_back({h.lastReplyTo, h.lastReply});
            if (h.held && h.holder == from) h.heard = now;
            return true;
        }
        if (!argsOk) {
            refuse("wrong arguments");
            answer(point, replyTo, id, false, command, "wrong arguments", from);
            return true;
        }
        if (command == "take") {
            if (h.held && h.holder != from) {
                const std::string why = "held by " + h.holder.str();
                refuse("held");
                answer(point, replyTo, id, false, command, why, from);
                return true;
            }
            const bool already = h.held;
            h.held = true;
            h.holder = from;
            h.replyTo = replyTo;
            h.heard = now;
            ++accepted_;
            if (!already) events_.push_back({PointEvent::Taken, point, "", from});
            answer(point, replyTo, id, true, command, "", from);
            return true;
        }
        // release
        if (!h.held) {
            refuse("not held");
            answer(point, replyTo, id, false, command, "not held", from);
            return true;
        }
        if (h.holder != from) {
            refuse("not the holder");
            answer(point, replyTo, id, false, command, "not the holder", from);
            return true;
        }
        const OscPeer to = h.replyTo;
        ++accepted_;
        events_.push_back({PointEvent::Returned, point, "released", h.holder});
        h.held = false;
        h.holder = OscPeer{};
        h.replyTo = OscPeer{};
        answer(point, to, id, true, command, "", from);
        return true;
    }

    // ---- moves: from the holder, not answered ----
    unsigned inlet = 0;
    std::size_t want = 0;
    if (command == "xy")       { inlet = InletXY;  want = 2; }
    else if (command == "z")   { inlet = InletZ;   want = 1; }
    else if (command == "u")   { inlet = InletU;   want = 1; }
    else if (command == "xyz") { inlet = InletXYZ; want = 3; }
    else { refuse("unknown command"); return true; }

    if (!(ps.inlets & inlet)) { refuse(command + " closed"); return true; }
    if (!h.held) { refuse("not held"); return true; }
    if (h.holder != from) { refuse("not the holder"); return true; }
    if (m.args.size() != want) { refuse("wrong arguments"); return true; }
    for (const OscArg& a : m.args) {
        if (!numberArg(a)) { refuse("wrong arguments"); return true; }
    }
    float v[3] = {0.0f, 0.0f, 0.0f};
    for (std::size_t k = 0; k < want; ++k) {
        if (!finiteArg(m.args[k], v[k])) { refuse("not finite"); return true; }
    }
    h.heard = now;

    PointMove mv;
    mv.point = point;
    if (inlet == InletXY || inlet == InletXYZ) {
        mv.hasXY = true;
        mv.x = mapped(v[0], ps.x, 0.0f, 1.0f);
        mv.y = mapped(v[1], ps.y, 0.0f, 1.0f);
        if (inside_ && !inside_(mv.x, mv.y)) { refuse("off the plan"); return true; }
    }
    if (inlet == InletZ || inlet == InletXYZ) {
        mv.hasZ = true;
        mv.z = heldZ(mapped(inlet == InletZ ? v[0] : v[2], ps.z, zLow_, zHigh_));
    }
    if (inlet == InletU) {
        mv.hasU = true;
        mv.u = heldU(mapped(v[0], ps.u, 0.0f, uTop_));
    }
    ++accepted_;
    moves_.push_back(mv);
    return true;
}

inline void PointControl::tick(double now) {
    for (int k = 0; k < count(); ++k) {
        const Hold& h = state_[k];
        const float s = spec_.points[k].silence;
        if (h.held && s > 0.0f && now - h.heard > double(s)) lose(k + 1, "silent");
    }
}

inline void PointControl::handTook(int point) {
    if (held(point)) lose(point, "hand");
}

} // namespace player
} // namespace ofxManifold
