#!/usr/bin/env python3
"""
ofxManifold -- independent reference for movement (round 3B-2).

A Motion answers one question: where is the point at a given count -- x and
y on the map, and a height. The count is the clock's: seconds, or bars from
SuperCollider's phase. This file is the reference for every movement in the
slice, and for the clocks that drive them:

  splitmix64          the generator every random choice draws from
  RandomWalk          jump (travel 0) and glide, one class
  RecordedMotion      a recorded path, held or looped
  step timing         every interval (with jitter), at given counts (clicks),
                      a pattern repeating in a cycle
  PhaseClock          a count of bars from a phase that wraps
  SecondsClock        the app's own seconds, pausable
  motion files        "motion": 1, saved and loaded, and refused

The walk is worked in doubles, as the C++ is; the runner compares positions
within 1e-9 and counts within 1e-12. Where a yes/no decision could fall
either way on the last bit -- a candidate on a boundary -- the reference
refuses to write the vector (`margin`), so the C++ and this file always make
the same choices.

Classes:
  ANALYTIC  known without this file: splitmix64's published outputs; half way
            at half the travel; 0.15625 of the way at a quarter, eased; steps
            exactly at clicks and at pattern offsets; a phase wrapping.
  CROSS     whole walks, seeded, against this file.
  SPEC      our rules: 64 tries, three draws a try, what a restart or an
            out-of-order phase does.
"""

import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vectors", "motion.vec")

MASK = (1 << 64) - 1
TIME_STREAM = 0x5851F42D4C957F2D     # seed ^ this: the stream jitter draws from
TRIES = 64
ALONG = 16                            # points checked along a straight path
CHECKPOINT = 256


# ---- the generator ---------------------------------------------------------

class SplitMix64:
    """Sebastiano Vigna's splitmix64. Integers, so it matches the C++ exactly."""

    def __init__(self, seed):
        self.state = seed & MASK

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def unit(self):
        """0 <= u < 1: the top 24 bits over 2^24, exact in a float."""
        return (self.next() >> 40) / float(1 << 24)

    def copy(self):
        c = SplitMix64(0)
        c.state = self.state
        return c


# ---- bounds ----------------------------------------------------------------

def square(x, y):
    """The room's floor plan: the square from 0.2 to 0.8."""
    return 0.2 <= x <= 0.8 and 0.2 <= y <= 0.8


def lshape(x, y):
    """An L: the bottom bar and the left bar of the square 0.1 to 0.9."""
    return (0.1 <= x <= 0.9 and 0.1 <= y <= 0.4) or (0.1 <= x <= 0.4 and 0.1 <= y <= 0.9)


INSIDE = {"none": None, "square": square, "lshape": lshape}
EDGES = {"none": [], "square": [0.2, 0.8], "lshape": [0.1, 0.4, 0.9]}


def margin(values, edges):
    """Refuse a vector whose decision rests on the last bit."""
    for v in values:
        for e in edges:
            assert abs(v - e) > 1e-9, f"{v} too near the edge {e} to compare"


# ---- the walk --------------------------------------------------------------

def smooth(u):
    return u * u * (3.0 - 2.0 * u)


class Walk:
    def __init__(self, seed=1, start=(0.5, 0.5, 0.0), clock=("every", 0.5, 0.0),
                 travel=("duration", 0.4), easing="smooth",
                 rect=(0.0, 0.0, 1.0, 1.0), z=(0.0, 0.0), step=0.0,
                 inside="none", straight=True):
        self.seed, self.start = seed, tuple(start)
        self.clock = clock
        self.travel, self.easing = travel, easing
        self.rect, self.z, self.step = rect, z, step
        self.inside_name = inside
        self.inside = INSIDE[inside]
        self.straight = straight
        self.clicks = list(clock[1]) if clock[0] == "at" else []
        self.edges = list(rect) + EDGES[inside]

    # The state after k steps: k, when step k happened, where its trip began,
    # where it goes, how long it takes, and both generators.
    def initial(self):
        return dict(k=0, tk=-math.inf, frm=self.start, dest=self.start, dur=0.0,
                    place=SplitMix64(self.seed),
                    time=SplitMix64(self.seed ^ TIME_STREAM))

    def next_time(self, st):
        """When step k+1 happens, and the time generator after it; inf: never."""
        kind = self.clock[0]
        k = st["k"]
        if kind == "every":
            interval, jitter = self.clock[1], self.clock[2]
            gen = st["time"].copy()
            i = interval
            if jitter > 0:
                i = interval * (1.0 + jitter * (2.0 * gen.unit() - 1.0))
            base = 0.0 if k == 0 else st["tk"]
            return base + i, gen
        if kind == "at":
            return (self.clicks[k] if k < len(self.clicks) else math.inf), st["time"]
        cycle, offsets = self.clock[1], self.clock[2]
        n, j = divmod(k, len(offsets))
        return n * cycle + offsets[j], st["time"]

    def position(self, st, t):
        if st["k"] == 0 or t < st["tk"]:
            return self.start if st["k"] == 0 else st["frm"]
        d = st["dur"]
        if d <= 0.0 or t >= st["tk"] + d:
            return st["dest"]
        u = (t - st["tk"]) / d
        e = smooth(u) if self.easing == "smooth" else u
        f, g = st["frm"], st["dest"]
        return tuple(f[i] + (g[i] - f[i]) * e for i in range(3))

    def ok(self, p, c, within):
        x0, y0, x1, y1 = self.rect
        margin([c[0]], [x0, x1] + EDGES[self.inside_name])
        margin([c[1]], [y0, y1] + EDGES[self.inside_name])
        if not within:
            return False
        if not (x0 <= c[0] <= x1 and y0 <= c[1] <= y1):
            return False
        if self.inside is None:
            return True
        if not self.inside(c[0], c[1]):
            return False
        if not self.straight:
            return True
        for i in range(1, ALONG + 1):
            f = i / ALONG
            qx, qy = p[0] + (c[0] - p[0]) * f, p[1] + (c[1] - p[1]) * f
            margin([qx, qy], EDGES[self.inside_name])
            if not self.inside(qx, qy):
                return False
        return True

    def choose(self, p, gen):
        x0, y0, x1, y1 = self.rect
        zlo, zhi = self.z
        for _ in range(TRIES):
            ux, uy, uz = gen.unit(), gen.unit(), gen.unit()
            if self.step > 0:
                dx = (2.0 * ux - 1.0) * self.step
                dy = (2.0 * uy - 1.0) * self.step
                r2, s2 = dx * dx + dy * dy, self.step * self.step
                assert abs(r2 - s2) > 1e-12, "a draw on the circle's edge"
                cx, cy, within = p[0] + dx, p[1] + dy, r2 <= s2
            else:
                cx, cy, within = x0 + ux * (x1 - x0), y0 + uy * (y1 - y0), True
            cz = zlo + uz * (zhi - zlo)
            c = (cx, cy, cz)
            if self.ok(p, c, within):
                return c
        return p

    def advance(self, st):
        tn, tgen = self.next_time(st)
        p = self.position(st, tn)
        gen = st["place"].copy()
        dest = self.choose(p, gen)
        if self.travel[0] == "duration":
            dur = self.travel[1]
        else:
            dist = math.sqrt(sum((dest[i] - p[i]) ** 2 for i in range(3)))
            dur = dist / self.travel[1]
        return dict(k=st["k"] + 1, tk=tn, frm=p, dest=dest, dur=dur, place=gen, time=tgen)

    def at(self, t):
        """Walked from the start every time: the reference needs no cache."""
        st = self.initial()
        while True:
            tn, _ = self.next_time(st)
            if not (tn <= t):
                break
            st = self.advance(st)
        return self.position(st, t), st

    def step_time(self, k):
        """When step k (from 1) happens."""
        st = self.initial()
        for _ in range(k - 1):
            st = self.advance(st)
        return self.next_time(st)[0]


# ---- a recorded path -------------------------------------------------------

class Recorded:
    """Samples at normalized t (finalized), a duration, held or looped."""

    def __init__(self, samples, duration, end):
        self.samples, self.duration, self.end = samples, duration, end

    def point(self, u):
        s = self.samples
        if u <= s[0][0]:
            return s[0][1:]
        if u >= s[-1][0]:
            return s[-1][1:]
        for i in range(1, len(s)):
            if u < s[i][0]:
                a, b = s[i - 1], s[i]
                f = (u - a[0]) / (b[0] - a[0])
                return tuple(a[j] + (b[j] - a[j]) * f for j in (1, 2, 3))
        return s[-1][1:]

    def at(self, t):
        d = self.duration if self.duration > 0 else 1.0
        if t < 0:
            return self.point(0.0)
        if self.end == "loop":
            t = math.fmod(t, d)
        else:
            t = min(t, d)
        return self.point(t / d)


# ---- clocks ----------------------------------------------------------------

class PhaseClock:
    HEARD_FOR = 1.0

    def __init__(self):
        self.has, self.bar, self.phase, self.last = False, 0, 0.0, -math.inf

    def feed(self, phase, now, bar=None):
        phase = min(max(phase, 0.0), math.nextafter(1.0, 0.0))
        self.last = now
        if not self.has:
            self.has, self.phase = True, phase
            self.bar = bar if bar is not None else 0
            return
        if bar is not None:
            if (bar, phase) > (self.bar, self.phase):
                self.bar, self.phase = bar, phase
            return
        if self.phase - phase > 0.5:
            self.bar += 1
            self.phase = phase
        elif phase >= self.phase:
            self.phase = phase

    def count(self):
        return self.bar + self.phase if self.has else 0.0

    def heard(self, now):
        return self.has and now - self.last <= self.HEARD_FOR


class SecondsClock:
    def __init__(self):
        self.origin, self.paused, self.held = 0.0, False, 0.0

    def count(self, now):
        return self.held if self.paused else now - self.origin

    def pause(self, now):
        if not self.paused:
            self.held, self.paused = self.count(now), True

    def resume(self, now):
        if self.paused:
            self.origin, self.paused = now - self.held, False


# ---- files -----------------------------------------------------------------

def num(x):
    """Shortest of %.15g and %.17g that reads back exactly -- as the C++."""
    s = "%.15g" % x
    return s if float(s) == x else "%.17g" % x


def save(w, units="seconds"):
    o = ['{"motion": 1, "kind": "walk", "seed": %d, "units": "%s",' % (w.seed, units),
         ' "start": [%s],' % ", ".join(num(v) for v in w.start)]
    c = w.clock
    if c[0] == "every":
        o.append(' "clock": {"every": %s, "jitter": %s},' % (num(c[1]), num(c[2])))
    elif c[0] == "at":
        o.append(' "clock": {"at": [%s]},' % ", ".join(num(v) for v in w.clicks))
    else:
        o.append(' "clock": {"cycle": %s, "at": [%s]},'
                 % (num(c[1]), ", ".join(num(v) for v in c[2])))
    o.append(' "travel": {"%s": %s},' % (w.travel[0], num(w.travel[1])))
    o.append(' "easing": "%s",' % w.easing)
    o.append(' "bounds": {"rect": [%s], "z": [%s], "step": %s, "inside": "%s", "straight": %s}}'
             % (", ".join(num(v) for v in w.rect), ", ".join(num(v) for v in w.z),
                num(w.step), "none" if w.inside is None else "map",
                "true" if w.straight else "false"))
    return "\n".join(o) + "\n"


def load(text):
    """(walk, units) or (None, error), with the C++'s refusals."""
    try:
        d = json.loads(text)
    except ValueError:
        return None, "json"
    if not isinstance(d, dict):
        return None, "root is not an object"
    if d.get("motion") != 1:
        return None, "missing or unsupported motion version"
    if d.get("kind") != "walk":
        return None, "unknown kind"
    units = d.get("units", "seconds")
    if units not in ("seconds", "bars"):
        return None, "unknown units"
    c = d.get("clock", {})
    if "every" in c:
        if c["every"] <= 0:
            return None, "the interval must be above 0"
        j = c.get("jitter", 0.0)
        if not 0.0 <= j <= 0.9:
            return None, "jitter must be 0 to 0.9"
        clock = ("every", float(c["every"]), float(j))
    elif "cycle" in c:
        cyc, offs = float(c["cycle"]), [float(v) for v in c.get("at", [])]
        if cyc <= 0 or not offs:
            return None, "a pattern needs a cycle above 0 and offsets"
        if any(o < 0 or o >= cyc for o in offs) or any(b <= a for a, b in zip(offs, offs[1:])):
            return None, "a pattern's offsets must rise within its cycle"
        clock = ("pattern", cyc, offs)
    elif "at" in c:
        at = [float(v) for v in c["at"]]
        if any(b < a for a, b in zip(at, at[1:])):
            return None, "clicks must not go backwards"
        clock = ("at", at)
    else:
        return None, "missing clock"
    tr = d.get("travel", {"duration": 0.0})
    if "duration" in tr:
        if tr["duration"] < 0:
            return None, "travel must not be below 0"
        travel = ("duration", float(tr["duration"]))
    elif "speed" in tr:
        if tr["speed"] <= 0:
            return None, "speed must be above 0"
        travel = ("speed", float(tr["speed"]))
    else:
        return None, "missing travel"
    easing = d.get("easing", "linear")
    if easing not in ("linear", "smooth"):
        return None, "unknown easing"
    b = d.get("bounds", {})
    rect = tuple(float(v) for v in b.get("rect", [0, 0, 1, 1]))
    if rect[0] > rect[2] or rect[1] > rect[3]:
        return None, "the rectangle is inside out"
    z = tuple(float(v) for v in b.get("z", [0, 0]))
    if z[0] > z[1]:
        return None, "z low is above z high"
    step = float(b.get("step", 0.0))
    if step < 0:
        return None, "the step distance must not be below 0"
    inside = b.get("inside", "none")
    if inside not in ("none", "map"):
        return None, "inside is map or none"
    start = tuple(float(v) for v in d.get("start", [0.5, 0.5, 0.0]))
    w = Walk(seed=int(d.get("seed", 0)), start=start, clock=clock, travel=travel,
             easing=easing, rect=rect, z=z, step=step,
             inside="lshape" if inside == "map" else "none",
             straight=bool(b.get("straight", True)))
    return (w, units), ""


# ---- vectors ---------------------------------------------------------------

def walk_line(name, cls, w):
    c = w.clock
    if c[0] == "every":
        clock = f"every {num(c[1])} {num(c[2])}"
    elif c[0] == "at":
        clock = "at " + " ".join([str(len(w.clicks))] + [num(v) for v in w.clicks])
    else:
        clock = "pattern " + " ".join([num(c[1]), str(len(c[2]))] + [num(v) for v in c[2]])
    return (f"WALK {name} {cls} seed={w.seed} start={','.join(num(v) for v in w.start)}"
            f" travel={w.travel[0]}:{num(w.travel[1])} easing={w.easing}"
            f" rect={','.join(num(v) for v in w.rect)} z={','.join(num(v) for v in w.z)}"
            f" step={num(w.step)} inside={w.inside_name} straight={int(w.straight)}"
            f" clock={clock.replace(' ', ',')}")


def ask(out, w, t):
    p, st = w.at(t)
    out.append(f"ASK {num(t)} {num(p[0])} {num(p[1])} {num(p[2])} {st['k']}")
    return p, st


def walks(out):
    out.append("# ---- jump and glide")

    w = Walk(seed=3, travel=("duration", 0.0), clock=("every", 0.5, 0.0))
    out.append("# a jump: still until the first step, then at each destination at once,")
    out.append("# held until the next")
    out.append(walk_line("jump_holds_then_moves", "ANALYTIC", w))
    p0 = ask(out, w, 0.0)[0]
    assert p0 == w.start
    ask(out, w, 0.49)
    p1 = ask(out, w, 0.5)[0]
    q1 = ask(out, w, 0.99)[0]
    assert p1 == q1 and p1 != w.start
    p2 = ask(out, w, 1.0)[0]
    assert p2 != p1
    out.append("END")

    w = Walk(seed=3, travel=("duration", 0.4), easing="linear")
    p1 = w.at(0.5 + 0.4)[0]
    out.append("# a glide, linear: half way at half the travel; there at the end")
    out.append(walk_line("glide_linear_half_way", "ANALYTIC", w))
    ask(out, w, 0.5)
    h = ask(out, w, 0.7)[0]
    for i in range(3):
        assert abs(h[i] - (w.start[i] + p1[i]) / 2) < 1e-12
    ask(out, w, 0.9)
    ask(out, w, 0.95)
    out.append("END")

    w = Walk(seed=3, travel=("duration", 0.4), easing="smooth")
    p1 = w.at(0.9)[0]
    out.append("# eased in and out: 0.15625 of the way at a quarter of the travel")
    out.append(walk_line("glide_smooth_quarter", "ANALYTIC", w))
    q = ask(out, w, 0.6)[0]
    assert abs(q[0] - (w.start[0] + (p1[0] - w.start[0]) * 0.15625)) < 1e-12
    ask(out, w, 0.7)
    ask(out, w, 0.9)
    out.append("END")

    w = Walk(seed=11, z=(0.0, 0.5), travel=("duration", 0.3))
    out.append("# heights drawn between low and high; one height when they are equal")
    out.append(walk_line("heights_in_range", "ANALYTIC", w))
    for k in range(1, 9):
        p = ask(out, w, k * 0.5 + 0.3)[0]
        assert 0.0 <= p[2] <= 0.5
    out.append("END")
    w = Walk(seed=11, z=(0.25, 0.25), start=(0.5, 0.5, 0.25))
    out.append(walk_line("one_height", "ANALYTIC", w))
    for k in range(1, 6):
        assert ask(out, w, k * 0.5 + 0.4)[0][2] == 0.25
    out.append("END")

    w = Walk(seed=5, step=0.1, travel=("duration", 0.0))
    out.append("# a step distance: no step longer than 0.1")
    out.append(walk_line("step_distance", "ANALYTIC", w))
    prev = w.start
    for k in range(1, 41):
        p = ask(out, w, k * 0.5)[0]
        assert math.hypot(p[0] - prev[0], p[1] - prev[1]) <= 0.1 + 1e-12
        prev = p
    out.append("END")

    w = Walk(seed=8, inside="square", start=(0.5, 0.5, 0.0), travel=("duration", 0.0))
    out.append("# every destination inside the map's test")
    out.append(walk_line("inside_the_square", "ANALYTIC", w))
    for k in range(1, 41):
        p = ask(out, w, k * 0.5)[0]
        assert square(p[0], p[1])
    out.append("END")

    w = Walk(seed=21, inside="lshape", start=(0.2, 0.2, 0.0), travel=("duration", 0.4),
             easing="linear")
    out.append("# an L: no glide crosses the notch -- every point along every trip on the map")
    out.append(walk_line("lshape_straight", "ANALYTIC", w))
    for k in range(1, 31):
        for f in (0.1, 0.25, 0.5, 0.75, 0.9):
            p = ask(out, w, k * 0.5 + 0.4 * f)[0]
            assert lshape(p[0], p[1]), p
    out.append("END")

    w = Walk(seed=21, inside="lshape", start=(0.2, 0.2, 0.0), travel=("duration", 0.4),
             easing="linear", straight=False)
    out.append("# the same with straight paths off: destinations inside, trips may cut across")
    out.append(walk_line("lshape_not_straight", "SPEC", w))
    crossed = False
    for k in range(1, 31):
        for f in (0.25, 0.5, 0.75):
            p = ask(out, w, k * 0.5 + 0.4 * f)[0]
            crossed = crossed or not lshape(p[0], p[1])
        assert lshape(*w.at(k * 0.5 + 0.4)[0][:2])
    assert crossed, "the seed should show a trip across the notch"
    out.append("END")

    w = Walk(seed=14, start=(0.05, 0.05, 0.0), step=0.2, travel=("duration", 0.0),
             rect=(0.0, 0.0, 1.0, 1.0))
    out.append("# near the corner with a step of 0.2: never outside the rectangle")
    out.append(walk_line("step_near_edge", "ANALYTIC", w))
    for k in range(1, 31):
        p = ask(out, w, k * 0.5)[0]
        assert 0.0 <= p[0] <= 1.0 and 0.0 <= p[1] <= 1.0
    out.append("END")

    w = Walk(seed=2, rect=(0.0, 0.0, 0.1, 0.1), inside="square",
             start=(0.5, 0.5, 0.0), travel=("duration", 0.0))
    out.append("# nowhere to go: a rectangle outside the map; it stays where it is")
    out.append(walk_line("nowhere_stays", "ANALYTIC", w))
    for k in range(1, 4):
        assert ask(out, w, k * 0.5)[0] == w.start
    out.append("END")

    out.append("# ---- when steps happen")
    clicks = [0.8, 1.9, 2.0, 3.25]
    w = Walk(seed=4, clock=("at", clicks), travel=("duration", 0.0))
    out.append("# clicks: each click one step, exactly at its count; none after the last")
    out.append(walk_line("clicks_step_once", "ANALYTIC", w))
    for t in (0.79, 0.8, 1.89, 1.9, 1.99, 2.0, 3.24, 3.25, 9.0):
        ask(out, w, t)
    for k, c in enumerate(clicks, 1):
        assert w.step_time(k) == c
    out.append("# a click added later counts; one before the last is refused")
    for c, took in ((9.5, 1), (9.4, 0), (9.5, 1), (12.0, 1)):
        if took:
            w.clicks.append(c)
        out.append(f"CLICK {num(c)} {took}")
    for t in (9.49, 9.5, 11.0, 12.0):
        ask(out, w, t)
    out.append("END")

    w = Walk(seed=4, clock=("at", [1.0, 1.2]), travel=("duration", 0.5), easing="linear")
    out.append("# a click mid-trip: the next trip starts from where the point is")
    out.append(walk_line("click_mid_trip", "ANALYTIC", w))
    p, st = w.at(1.2)
    mid = w.at(1.19999999)[0]
    assert all(abs(st["frm"][i] - w.position(w.advance(w.initial()), 1.2)[i]) < 1e-12 for i in range(3))
    ask(out, w, 1.1)
    ask(out, w, 1.2)
    ask(out, w, 1.45)
    ask(out, w, 1.7)
    out.append("END")

    w = Walk(seed=6, clock=("pattern", 1.0, [0.0, 0.1875, 0.5]), travel=("duration", 0.0))
    out.append("# a pattern: steps 0, 3 and 8 of sixteen, every bar, exactly")
    out.append(walk_line("pattern_steps", "ANALYTIC", w))
    for k in range(1, 31):
        n, j = divmod(k - 1, 3)
        assert w.step_time(k) == n * 1.0 + [0.0, 0.1875, 0.5][j]
        out.append(f"STEP {k} {num(w.step_time(k))}")
    ask(out, w, 0.0)
    ask(out, w, 0.2)
    ask(out, w, 7.6)
    out.append("END")

    w = Walk(seed=6, clock=("pattern", 0.5, [0.0, 0.125, 0.375]), travel=("duration", 0.0))
    out.append("# a pattern in a cycle of half a bar")
    out.append(walk_line("pattern_half_cycle", "ANALYTIC", w))
    for k in range(1, 13):
        n, j = divmod(k - 1, 3)
        assert w.step_time(k) == n * 0.5 + [0.0, 0.125, 0.375][j]
        out.append(f"STEP {k} {num(w.step_time(k))}")
    ask(out, w, 2.2)
    out.append("END")

    w = Walk(seed=9, clock=("every", 0.5, 0.4), travel=("duration", 0.1))
    out.append("# jitter: each interval 0.5 x (1 +/- 0.4), from its own stream")
    out.append(walk_line("jitter", "SPEC", w))
    prev = 0.0
    for k in range(1, 21):
        t = w.step_time(k)
        assert 0.3 - 1e-12 <= t - prev <= 0.7 + 1e-12
        out.append(f"STEP {k} {num(t)}")
        prev = t
    ask(out, w, 4.0)
    out.append("END")
    w2 = Walk(seed=9, clock=("every", 0.5, 0.0), travel=("duration", 0.0))
    w3 = Walk(seed=9, clock=("every", 0.5, 0.4), travel=("duration", 0.0))
    assert w2.at(0.5)[1]["dest"] == w3.at(w3.step_time(1))[1]["dest"], \
        "jitter must not change where the steps go"

    out.append("# ---- whole walks")
    for name, kw, times in [
        ("glide_seed_42", dict(seed=42, travel=("duration", 0.4)), None),
        ("glide_speed", dict(seed=42, travel=("speed", 0.5), easing="linear"), None),
        ("wander_in_square", dict(seed=17, step=0.15, inside="square",
                                  z=(0.0, 0.5), travel=("duration", 0.25)), None),
        ("bars_sixteenths", dict(seed=99, clock=("every", 0.0625, 0.0),
                                 travel=("duration", 0.25), easing="smooth"), None),
        ("trip_longer_than_interval", dict(seed=12, clock=("every", 0.3, 0.0),
                                           travel=("duration", 0.7), easing="linear"), None),
    ]:
        w = Walk(**kw)
        out.append(walk_line(name, "CROSS", w))
        for i in range(60):
            ask(out, w, i * 0.137)
        out.append("END")

    w = Walk(seed=31, clock=("every", 0.01, 0.0), travel=("duration", 0.005))
    out.append("# asked out of order -- far ahead, back, across checkpoints of 256 steps")
    out.append(walk_line("out_of_order", "SPEC", w))
    most = 0
    for t in (7.333, 0.004, 2.5601, 7.3331, 5.12, 0.0, 2.559, 30.0, 2.56, 12.8):
        most = max(most, ask(out, w, t)[1]["k"])
    out.append(f"# memory: a checkpoint every {CHECKPOINT} steps, no more")
    out.append(f"CHECKPOINTS {most // CHECKPOINT + 1}")
    out.append("END")


def recordings(out):
    out.append("# ---- a recording as a motion")
    samples = [(0.0, 0.1, 0.1, 0.0), (0.5, 0.9, 0.1, 0.25), (1.0, 0.9, 0.9, 0.5)]
    for end in ("hold", "loop"):
        r = Recorded(samples, 2.0, end)
        out.append(f"# a two-second path, {end} at the end")
        out.append(f"REC rec_{end} ANALYTIC {end} 2 3 "
                   + " ".join(",".join(num(v) for v in s) for s in samples))
        for t in (-1.0, 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 5.0):
            p = r.at(t)
            out.append(f"ASK {num(t)} {num(p[0])} {num(p[1])} {num(p[2])} 0")
        out.append("END")
    assert Recorded(samples, 2.0, "hold").at(1.0) == (0.9, 0.1, 0.25)
    assert Recorded(samples, 2.0, "loop").at(3.0) == (0.9, 0.1, 0.25)


def clocks(out):
    out.append("# ---- clocks")

    def phase_script(name, cls, note, feeds):
        out.append(f"# {note}")
        out.append(f"PHASE {name} {cls}")
        c = PhaseClock()
        for f in feeds:
            if f[0] == "feed":
                _, now, ph, bar = f
                c.feed(ph, now, bar)
                out.append(f"FEED {num(now)} {num(ph)}" + (f" {bar}" if bar is not None else ""))
            else:
                now = f[1]
                out.append(f"COUNT {num(now)} {num(c.count())} {int(c.heard(now))}")
        out.append("END")
        return c

    c = phase_script("phase_wraps", "ANALYTIC",
                     "a drop of more than half (0.98 to 0.02) is a new bar; of less (0.5 to 0.01) is not",
                     [("count", 0.0), ("feed", 0.0, 0.1, None), ("count", 0.0),
                      ("feed", 0.01, 0.6, None), ("feed", 0.02, 0.98, None), ("count", 0.02),
                      ("feed", 0.03, 0.02, None), ("count", 0.03),
                      ("feed", 0.04, 0.5, None), ("feed", 0.05, 0.01, None), ("count", 0.05)])
    assert c.count() == 1.5
    c = phase_script("phase_out_of_order", "SPEC",
                     "a smaller drop is a packet out of order: held, never backwards",
                     [("feed", 0.0, 0.4, None), ("feed", 0.01, 0.42, None),
                      ("feed", 0.02, 0.41, None), ("count", 0.02),
                      ("feed", 0.03, 0.43, None), ("count", 0.03),
                      ("feed", 0.04, 0.9, None), ("feed", 0.05, 0.45, None), ("count", 0.05)])
    assert c.count() == 0.9
    phase_script("phase_restart", "SPEC",
                 "bridge.scd run again: the phase back to 0 late in a bar is a new bar",
                 [("feed", 0.0, 0.3, None), ("feed", 0.5, 0.8, None),
                  ("feed", 0.6, 0.0, None), ("count", 0.6), ("feed", 0.7, 0.2, None),
                  ("count", 0.7)])
    phase_script("phase_bar_given", "ANALYTIC",
                 "a bar number sent with the phase is the bar itself; never backwards",
                 [("feed", 0.0, 0.25, 7), ("count", 0.0), ("feed", 0.1, 0.9, 7),
                  ("feed", 0.2, 0.1, 8), ("count", 0.2), ("feed", 0.3, 0.95, 7),
                  ("count", 0.3)])
    phase_script("phase_not_heard", "SPEC",
                 "no phase for over a second: not heard, the count held",
                 [("feed", 0.0, 0.5, None), ("count", 0.5), ("count", 1.0),
                  ("count", 1.01), ("feed", 3.0, 0.6, None), ("count", 3.0)])
    # The bridge's shortest bar, 0.1 s, at a hundred phases a second: ten a
    # bar, every wrap counted for a hundred bars.
    feeds, now = [], 0.0
    for i in range(1000):
        now = i * 0.01
        feeds.append(("feed", now, math.fmod(now / 0.1, 1.0), None))
    feeds.append(("count", now))
    c = phase_script("phase_shortest_bar", "ANALYTIC",
                     "the bridge's shortest bar, 0.1 s, a hundred phases a second", feeds)
    assert abs(c.count() - 99.9) < 1e-9

    out.append("# the app's own seconds, paused and resumed")
    out.append("SECS seconds_pause ANALYTIC")
    s = SecondsClock()
    for op, now in [("count", 1.5), ("pause", 2.0), ("count", 2.5), ("count", 9.0),
                    ("resume", 10.0), ("count", 10.5), ("pause", 11.0), ("pause", 12.0),
                    ("resume", 13.0), ("count", 13.25)]:
        if op == "count":
            out.append(f"COUNT {num(now)} {num(s.count(now))} 1")
        else:
            getattr(s, op)(now)
            out.append(f"{op.upper()} {num(now)}")
    assert s.count(13.25) == 3.25
    out.append("END")


def rng(out):
    out.append("# ---- the generator")
    # Published: Rosetta Code, "Pseudo-random numbers/Splitmix64", seed 1234567.
    published = [6457827717110365317, 3203168211198807973, 9817491932198370423,
                 4593380528125082431, 16408922859458223821]
    g = SplitMix64(1234567)
    got = [g.next() for _ in range(5)]
    assert got == published, got
    out.append("RNG splitmix_published ANALYTIC 1234567 " + " ".join(str(v) for v in got))
    g = SplitMix64(0)
    out.append("RNG splitmix_zero CROSS 0 " + " ".join(str(g.next()) for _ in range(5)))
    g = SplitMix64(42)
    out.append("UNIT unit_seed_42 CROSS 42 " + " ".join(num(g.unit()) for _ in range(8)))


def files(out):
    out.append("# ---- motion files")
    w = Walk(seed=7, start=(0.1 + 0.2, 0.5, 0.0), clock=("every", 0.5, 0.25),
             travel=("duration", 0.4), easing="smooth", z=(0.0, 0.5), step=0.2,
             inside="lshape", straight=True)
    text = save(w, "bars")
    (w2, units), err = load(text)
    assert not err and units == "bars" and save(w2, "bars") == text
    assert "0.30000000000000004" in text, "a number that needs seventeen digits" 
    out.append("FILE file_walk_round_trip ANALYTIC ok bars")
    out.append("JSON " + text.replace("\n", "\\n"))
    for t in (0.6, 1.3, 2.0):
        p = w2.at(t)[0]
        out.append(f"ASK {num(t)} {num(p[0])} {num(p[1])} {num(p[2])} {w2.at(t)[1]['k']}")
    out.append("END")

    for name, clock in [("file_clicks", {"at": [0.25, 1.0, 1.5]}),
                        ("file_pattern", {"cycle": 1, "at": [0, 0.25, 0.75]})]:
        d = {"motion": 1, "kind": "walk", "seed": 3, "clock": clock,
             "travel": {"speed": 0.5}, "easing": "linear"}
        text = json.dumps(d)
        (w, units), err = load(text)
        assert not err
        out.append(f"FILE {name} ANALYTIC ok {units}")
        out.append("JSON " + text)
        out.append("SAVED " + save(w, units).replace("\n", "\\n"))
        for t in (0.3, 1.2, 2.6):
            p = w.at(t)[0]
            out.append(f"ASK {num(t)} {num(p[0])} {num(p[1])} {num(p[2])} {w.at(t)[1]['k']}")
        out.append("END")

    good = {"motion": 1, "kind": "walk", "seed": 1, "clock": {"every": 0.5},
            "travel": {"duration": 0.4}}
    refusals = [
        ("refuse_version", dict(good, motion=2)),
        ("refuse_kind", dict(good, kind="orbit")),
        ("refuse_units", dict(good, units="beats")),
        ("refuse_interval", dict(good, clock={"every": 0})),
        ("refuse_jitter", dict(good, clock={"every": 0.5, "jitter": 0.95})),
        ("refuse_clicks_back", dict(good, clock={"at": [1.0, 0.5]})),
        ("refuse_pattern_order", dict(good, clock={"cycle": 1, "at": [0.5, 0.25]})),
        ("refuse_pattern_cycle", dict(good, clock={"cycle": 1, "at": [0.0, 1.0]})),
        ("refuse_travel", dict(good, travel={"duration": -0.1})),
        ("refuse_speed", dict(good, travel={"speed": 0})),
        ("refuse_easing", dict(good, easing="bounce")),
        ("refuse_rect", dict(good, bounds={"rect": [1, 0, 0, 1]})),
        ("refuse_z", dict(good, bounds={"z": [0.5, 0.0]})),
        ("refuse_step", dict(good, bounds={"step": -0.1})),
        ("refuse_inside", dict(good, bounds={"inside": "room"})),
    ]
    for name, d in refusals:
        r, err = load(json.dumps(d))
        assert r is None, name
        out.append(f"FILE {name} ANALYTIC refused -")
        out.append("JSON " + json.dumps(d))
        out.append("END")


def main():
    out = [
        "# ofxManifold movement vectors (round 3B-2).",
        "# Generated by tests/ref/reference_motion.py -- do not edit by hand.",
        "",
    ]
    rng(out)
    walks(out)
    recordings(out)
    clocks(out)
    files(out)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        parts = line.split()
        if parts and parts[0] in ("RNG", "UNIT", "WALK", "REC", "PHASE", "SECS", "FILE"):
            counts[parts[2]] = counts.get(parts[2], 0) + 1
    print(f"wrote {OUT}")
    for k in sorted(counts):
        print(f"  {k:<9} {counts[k]}")
    print(f"  TOTAL     {sum(counts.values())}")


if __name__ == "__main__":
    sys.exit(main())
