#pragma once

// ofxManifoldPlayer -- clocks: what drives the count a Motion is asked
// (round 3B-2).
//
// A Motion is asked a count and knows nothing of where the count comes from.
// The player keeps a clock and asks it for the count now:
//
//   SecondsClock   the app's own seconds, pausable
//   PhaseClock     bars, from a phase that wraps 0..1 once a bar -- as
//                  SuperCollider sends it through ofxScPhaseBridge's /phase
//
// Not tied to the bridge: ofxManifold uses none of its code and it needs
// nothing from ofxManifold. readPhase() and readBeat() speak its contract --
// "/phase <float 0..1> [<int bar>]" and "/beat <int step>" -- on messages the
// app has already read; the clock itself takes a phase from anywhere.
//
// Header-only, no openFrameworks.

#include "ofxManifoldOsc.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace ofxManifold {
namespace player {

// The app's own seconds from when it started (or was reset). Paused, the
// count holds, and goes on from there when resumed.
class SecondsClock {
public:
    double count(double now) const { return paused_ ? held_ : now - origin_; }
    bool   paused() const { return paused_; }
    void pause(double now) {
        if (paused_) return;
        held_ = count(now);
        paused_ = true;
    }
    void resume(double now) {
        if (!paused_) return;
        origin_ = now - held_;
        paused_ = false;
    }
    void reset(double now) { origin_ = now; held_ = 0.0; }

private:
    double origin_ = 0.0, held_ = 0.0;
    bool   paused_ = false;
};

// A count of bars from a phase that wraps. Fed each phase as it arrives,
// it answers bars: bar 3 at phase 0.25 is 3.25.
//
//   A wrap: a drop of more than half (0.98, then 0.02) is a new bar. The
//   bridge keeps a bar to at least 0.1 s and sends a hundred phases a second:
//   at least ten readings a bar, plenty to tell.
//   Never backwards: a smaller drop -- a packet out of order -- is ignored;
//   the count holds until the phase passes it again.
//   A restart (bridge.scd run again, the phase back to 0) late in a bar reads
//   as a new bar; early in one, the count holds until the phase catches up.
//   The bar, when sent: "/phase <f> <i bar>" is the bar itself, so apps on
//   one bridge agree on the bar, not only the beat; still never backwards.
//   Not heard: no phase for kHeardFor seconds -- the count holds.
class PhaseClock {
public:
    static constexpr double kHeardFor = 1.0;
    static constexpr int    kNoBar = -1;

    void feed(double phase, double now, int bar = kNoBar) {
        phase = std::min(std::max(phase, 0.0), std::nextafter(1.0, 0.0));
        last_ = now;
        if (!has_) {
            has_ = true;
            phase_ = phase;
            bar_ = bar != kNoBar ? bar : 0;
            return;
        }
        if (bar != kNoBar) {
            if (bar > bar_ || (bar == bar_ && phase > phase_)) {
                bar_ = bar;
                phase_ = phase;
            }
            return;
        }
        if (phase_ - phase > 0.5) {
            ++bar_;
            phase_ = phase;
        } else if (phase >= phase_) {
            phase_ = phase;
        }
    }

    double count() const { return has_ ? double(bar_) + phase_ : 0.0; }
    bool   heard(double now) const { return has_ && now - last_ <= kHeardFor; }
    bool   everHeard() const { return has_; }
    long   bar() const { return bar_; }
    double phase() const { return phase_; }
    void   reset() { *this = PhaseClock(); }

private:
    bool   has_ = false;
    long   bar_ = 0;
    double phase_ = 0.0;
    double last_ = -std::numeric_limits<double>::infinity();
};

// "/phase <f> [<i bar>]": fed to the clock; false if the message is not one.
inline bool readPhase(const OscMessage& m, PhaseClock& clock, double now) {
    if (m.address != "/phase" || m.args.empty() || m.args[0].type != 'f') return false;
    const int bar = m.args.size() > 1 && m.args[1].type == 'i' ? int(m.args[1].i)
                                                               : PhaseClock::kNoBar;
    clock.feed(double(m.args[0].f), now, bar);
    return true;
}

// "/beat <i step>": the step SuperCollider just sounded; false if not one.
inline bool readBeat(const OscMessage& m, int& step) {
    if (m.address != "/beat" || m.args.empty() || m.args[0].type != 'i') return false;
    step = int(m.args[0].i);
    return true;
}

} // namespace player
} // namespace ofxManifold
