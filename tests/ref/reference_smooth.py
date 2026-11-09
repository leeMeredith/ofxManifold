#!/usr/bin/env python3
"""
ofxManifoldPlayer -- independent reference for the output smoother (3B-1).

The player smooths what it sends: one level per output, a list whose length
the map fixes. Like the kernel's smoother suite this tests SEQUENCES -- snap,
then tick, and tick, with the list asserted at every step -- because a
smoother right on the first tick and wrong on the fourth would pass any
single-shot check.

Formulation differs from the C++ where it can: the C++ closes the share
1 - 0.5**(dt/h) of the distance; this multiplies what is LEFT by
2**(-dt/h) and works in doubles. The runner compares within 1e-6.

The one place float and double could disagree is the arrival rule: a number
within 1e-4 of its target becomes its target. Every step here is checked to
be well clear of that line, so the C++ makes the same call.

Classes:
  ANALYTIC  known by hand: passthrough when off, exactly half after one
            half-life, the same after one tick of 0.1 s as after four of
            0.025 s, arrival on the fourteenth half-life, the speed limit.
  CROSS     step-by-step values from this reference; and, in scripts marked
            kernel, the runner drives the kernel's WeightSmoother alongside on
            the same numbers and they must agree.
  SPEC      our rules: a different length or a reset starts over, the speed
            limit applied after the exponential step.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vectors", "smooth.vec")
ARRIVED = 1e-4


def fmt(x):
    return f"{x:.10g}"


class Smoother:
    def __init__(self, half_life, max_rate):
        self.h, self.r = half_life, max_rate
        self.state, self.settled = [], True

    def enabled(self):
        return self.h > 0 or self.r > 0

    def snap(self, v):
        self.state, self.settled = list(v), True

    def reset(self):
        self.state, self.settled = [], True

    def update(self, target, dt):
        if len(target) != len(self.state) or not self.enabled():
            self.snap(target)
            return self.state
        if dt <= 0:
            self.settled = self.state == list(target)
            return self.state
        keep = 2.0 ** (-dt / self.h) if self.h > 0 else 0.0
        limit = self.r * dt if self.r > 0 else None
        out = []
        for frm, to in zip(self.state, target):
            v = to - (to - frm) * keep
            if limit is not None:
                v = min(max(v, frm - limit), frm + limit)
            gap = abs(to - v)
            assert not (0.99 * ARRIVED < gap < 1.01 * ARRIVED), \
                f"a step {gap} too near the arrival line to compare"
            out.append(to if gap < ARRIVED else v)
        self.state = out
        self.settled = all(a == b for a, b in zip(out, target))
        return self.state


class Script:
    def __init__(self, out, name, cls, half_life, max_rate, note, kernel=False):
        self.out, self.s = out, Smoother(half_life, max_rate)
        out.append(f"# {note}")
        out.append(f"SMOOTH {name} {cls} {fmt(half_life)} {fmt(max_rate)} {int(kernel)}")

    def snap(self, v):
        self.s.snap(v)
        self.out.append("SNAP " + " ".join([str(len(v))] + [fmt(x) for x in v]))

    def reset(self):
        self.s.reset()
        self.out.append("RESET")

    def tick(self, dt, target):
        got = self.s.update(target, dt)
        self.out.append("TICK " + fmt(dt) + " "
                        + " ".join([str(len(target))] + [fmt(x) for x in target])
                        + " EXPECT "
                        + " ".join([str(len(got))] + [fmt(x) for x in got])
                        + f" {int(self.s.settled)}")

    def end(self):
        self.out.append("END")
        self.out.append("")


# The room's twelve outputs (FL FR RL RR on the floor, -2 the middle, -3 the
# top) at the middle of the floor plan, as the point rises: what example-osc
# sends for the room.
def room(z):
    floor = [0.25] * 4
    if z <= 0.0:
        return floor + [0.0] * 8
    if z < 0.25:
        t = z / 0.25
        return [0.25 * (1 - t)] * 4 + [0.25 * t] * 4 + [0.0] * 4
    if z < 0.5:
        t = (z - 0.25) / 0.25
        return [0.0] * 4 + [0.25 * (1 - t)] * 4 + [0.25 * t] * 4
    return [0.0] * 8 + [0.25] * 4


def scripts(out):
    t = Script(out, "off_is_passthrough", "ANALYTIC", 0, 0,
               "off: every update is its target")
    t.tick(0.1, [0.3, 0.7]); t.tick(0.1, [1.0, 0.0]); t.tick(0.0, [0.5, 0.5])
    t.end()

    t = Script(out, "first_update_taken", "ANALYTIC", 0.05, 0,
               "nothing to glide from: the first target is taken at once")
    t.tick(1 / 60, [0.25, 0.75]); t.tick(1 / 60, [0.25, 0.75])
    t.end()

    t = Script(out, "one_half_life", "ANALYTIC", 0.05, 0,
               "after exactly one half-life, exactly half way")
    t.snap([1.0, 0.0]); t.tick(0.05, [0.0, 1.0])
    t.end()

    t = Script(out, "rate_one_tick", "ANALYTIC", 0.05, 0,
               "one tick of 0.1 s: two half-lives, three quarters of the way")
    t.snap([1.0, 0.0]); t.tick(0.1, [0.0, 1.0])
    t.end()

    t = Script(out, "rate_four_ticks", "ANALYTIC", 0.05, 0,
               "four ticks of 0.025 s land where one of 0.1 s does: the same at "
               "any frame rate")
    t.snap([1.0, 0.0])
    for _ in range(4):
        t.tick(0.025, [0.0, 1.0])
    t.end()

    t = Script(out, "arrives_exactly", "ANALYTIC", 0.05, 0,
               "from 1 to 0 a half-life a tick: 2^-13 is still more than 1e-4 "
               "away, 2^-14 is less, so the fourteenth tick lands on 0 and it "
               "is settled; then it stays put")
    t.snap([1.0])
    for _ in range(14):
        t.tick(0.05, [0.0])
    t.tick(0.05, [0.0]); t.tick(1 / 60, [0.0])
    t.end()

    t = Script(out, "dt_zero", "ANALYTIC", 0.05, 0,
               "no time passed: nothing moves; settled says whether it is "
               "already there")
    t.snap([1.0, 0.0]); t.tick(0.0, [0.0, 1.0]); t.tick(0.0, [1.0, 0.0])
    t.end()

    t = Script(out, "limit_only", "ANALYTIC", 0, 2.0,
               "a speed limit alone: 2 a second, 0.2 a tick of 0.1 s, arriving "
               "exactly on the fifth")
    t.snap([0.0])
    for _ in range(6):
        t.tick(0.1, [1.0])
    t.end()

    t = Script(out, "exponential_then_limit", "SPEC", 0.05, 1.0,
               "both: the half-life would go half way, the limit allows 0.05")
    t.snap([0.0, 1.0]); t.tick(0.05, [1.0, 0.0]); t.tick(0.05, [1.0, 0.0])
    t.end()

    t = Script(out, "length_change_starts_over", "SPEC", 0.05, 0,
               "a list of another length is another map's outputs: taken at "
               "once, not glided into")
    t.snap([1.0, 0.0, 0.0, 0.0])
    t.tick(1 / 60, room(0.0)); t.tick(1 / 60, room(0.5))
    t.tick(1 / 60, [0.0, 1.0, 0.0, 0.0])
    t.end()

    t = Script(out, "reset_starts_over", "SPEC", 0.05, 0,
               "reset (a map of the same size opened): the next target is "
               "taken at once")
    t.snap([1.0, 0.0]); t.tick(1 / 60, [0.0, 1.0])
    t.reset(); t.tick(1 / 60, [0.0, 1.0])
    t.end()

    t = Script(out, "jump_across_the_room", "CROSS", 0.05, 0,
               "the room's twelve outputs: a click from the floor's middle to "
               "the top, twenty frames at 60 a second; always twelve numbers")
    t.snap(room(0.0))
    for _ in range(20):
        t.tick(1 / 60, room(0.5))
    t.end()

    t = Script(out, "gliding_up_the_room", "CROSS", 0.05, 0,
               "up held through the levels, the target moving every frame")
    t.snap(room(0.0))
    for k in range(1, 31):
        t.tick(1 / 60, room(k / 60))
    t.end()

    t = Script(out, "leaving_the_map", "CROSS", 0.05, 0,
               "off the map: every level to zero, and arriving")
    t.snap([0.25] * 4)
    for _ in range(45):
        t.tick(1 / 60, [0.0] * 4)
    t.end()

    jitter = [0.5, 0.52, 0.47, 0.9, 0.1, 0.1, 0.1, 0.6, 0.6, 0.0,
              0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.98, 1.0, 0.33]
    dts = [1 / 60, 1 / 30, 1 / 120, 0.02, 1 / 60] * 4
    t = Script(out, "kernel_half_life", "CROSS", 0.05, 0,
               "one number through jitter and jumps at uneven frame times: "
               "the kernel's smoother on the same numbers agrees",
               kernel=True)
    t.snap([0.5])
    for v, dt in zip(jitter, dts):
        t.tick(dt, [v])
    t.end()

    t = Script(out, "kernel_half_life_and_limit", "CROSS", 0.08, 3.0,
               "the same with a speed limit as well", kernel=True)
    t.snap([0.5])
    for v, dt in zip(jitter, dts):
        t.tick(dt, [v])
    t.end()


def main():
    out = [
        "# ofxManifoldPlayer output smoother vectors (round 3B-1).",
        "# Generated by tests/ref/reference_smooth.py -- do not edit by hand.",
        "#",
        "# SMOOTH name class half-life max-rate kernel",
        "# SNAP n values... | RESET",
        "# TICK dt n targets... EXPECT n values... settled",
        "",
    ]
    scripts(out)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        if line.startswith("SMOOTH "):
            c = line.split()[2]
            counts[c] = counts.get(c, 0) + 1
    print(f"wrote {OUT}")
    for k in sorted(counts):
        print(f"  {k:<9} {counts[k]}")
    print(f"  TOTAL     {sum(counts.values())}")


if __name__ == "__main__":
    sys.exit(main())
