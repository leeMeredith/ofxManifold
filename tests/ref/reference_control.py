#!/usr/bin/env python3
"""Reference for points moved from outside (round 3B-2f): PointControl and
the control file.

Writes tests/vectors/control.vec and its fixtures under tests/fixtures/control/.
tests/run_control.cpp replays each script through ofxManifoldPlayer's
PointControl and reads each file with loadControl, and checks every result.

Worked out here another way than the C++:

  - an address is split by a regular expression, not by finding slashes;
  - who holds a point is a dictionary of holders, not a per-point record;
  - a repeat is found in a log of every answer given, not the last one kept;
  - a range is mapped as a fraction of its width, then scaled;
  - the file is read by Python's own json, its rules checked one by one
    from a table.
"""
import json
import math
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vectors", "control.vec")
FIX = os.path.join(HERE, "..", "fixtures", "control")

MAX_POINTS = 16
INLETS = ["xy", "z", "u", "xyz"]          # in the order a file is written


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def num(v):
    return "%.9g" % f32(v)


# ---- the control file ------------------------------------------------------

def default_spec():
    return [dict(name="", inlets=set(INLETS), range={}, silence=5.0)]


NAME = re.compile(r"[A-Za-z][A-Za-z0-9_-]{0,31}$")


def load_control(text, points=MAX_POINTS):
    """(spec, None) or (None, why)."""
    try:
        d = json.loads(text)
    except ValueError:
        return None, "json"
    if not isinstance(d, dict):
        return None, "root is not an object"
    extra = sorted(set(d) - {"control", "points"})
    if extra:
        return None, "unknown key: " + extra[0]
    if d.get("control") != 1:
        return None, "missing or unsupported control version"
    ps = d.get("points")
    if not isinstance(ps, list) or not ps:
        return None, "points must be a list of at least one"
    if len(ps) > MAX_POINTS:
        return None, f"at most {MAX_POINTS} points"
    if len(ps) > points:
        return None, f"this player has {points} point" + ("" if points == 1 else "s")
    spec = []
    for k, p in enumerate(ps, 1):
        at = f"point {k}: "
        if not isinstance(p, dict):
            return None, at + "not an object"
        extra = sorted(set(p) - {"number", "name", "inlets", "range", "silence"})
        if extra:
            return None, at + "unknown key: " + extra[0]
        if p.get("number") != k or isinstance(p.get("number"), bool):
            return None, at + "numbered out of order"
        s = dict(name="", inlets=set(INLETS), range={}, silence=5.0)
        if "name" in p:
            if not isinstance(p["name"], str) or not NAME.match(p["name"]):
                return None, at + "a name is a letter, then letters, digits, - or _, up to 32"
            if any(o["name"] == p["name"] for o in spec):
                return None, at + "the name " + p["name"] + " is taken"
            s["name"] = p["name"]
        if "inlets" in p:
            if not isinstance(p["inlets"], list):
                return None, at + "inlets must be a list"
            s["inlets"] = set()
            for n in p["inlets"]:
                if n not in INLETS:
                    return None, at + "unknown inlet: " + (n if isinstance(n, str) else "")
                if n in s["inlets"]:
                    return None, at + "the inlet " + n + " twice"
                s["inlets"].add(n)
        if "range" in p:
            if not isinstance(p["range"], dict):
                return None, at + "range must be an object"
            for axis in sorted(p["range"]):
                if axis not in ("x", "y", "z", "u"):
                    return None, at + "unknown range axis: " + axis
                v = p["range"][axis]
                if (not isinstance(v, list) or len(v) != 2
                        or not all(isinstance(x, (int, float)) and not isinstance(x, bool)
                                   for x in v)):
                    return None, at + "a range is two numbers, low and high"
                if not (math.isfinite(v[0]) and math.isfinite(v[1]) and v[0] < v[1]):
                    return None, at + "a range's low must be below its high"
                s["range"][axis] = (float(v[0]), float(v[1]))
        if "silence" in p:
            v = p["silence"]
            if (not isinstance(v, (int, float)) or isinstance(v, bool)
                    or not math.isfinite(v) or v < 0):
                return None, at + "silence is 0 or more seconds"
            s["silence"] = float(v)
        spec.append(s)
    return spec, None


def save_control(spec):
    lines = ['{', '  "control": 1,', '  "points": [']
    for k, p in enumerate(spec, 1):
        o = f'    {{ "number": {k}'
        if p["name"]:
            o += f', "name": "{p["name"]}"'
        o += ', "inlets": [' + ", ".join(f'"{n}"' for n in INLETS if n in p["inlets"]) + ']'
        ranges = [f'"{a}": [{num(p["range"][a][0])}, {num(p["range"][a][1])}]'
                  for a in ("x", "y", "z", "u") if a in p["range"]]
        if ranges:
            o += ', "range": { ' + ", ".join(ranges) + ' }'
        o += f', "silence": {num(p["silence"])} }}'
        lines.append(o + ("," if k < len(spec) else ""))
    lines += ['  ]', '}']
    return "\n".join(lines) + "\n"


# ---- taking points from outside ---------------------------------------------

PLANS = {
    "all": lambda x, y: True,
    # the room's floor: its corners at 0.2 and 0.8
    "square": lambda x, y: f32(0.2) <= x <= f32(0.8) and f32(0.2) <= y <= f32(0.8),
}

ADDR = re.compile(r"^(?P<prefix>.*)/point/(?P<token>[^/]+)/(?P<cmd>[^/]+)$")


class Control:
    def __init__(self, prefix, spec, plan, extent):
        self.prefix = prefix
        self.spec = spec
        self.inside = PLANS[plan]
        self.zlo, self.zhi, self.utop = extent
        self.holders = {}            # point -> (peer, reply_to, heard)
        self.log = []                # every answer: (point, peer, id, cmd, to, msg)
        self.accepted = 0
        self.refused = 0
        self.last = ""
        self.out_moves, self.out_events, self.out_replies = [], [], []

    def refuse(self, why):
        self.refused += 1
        self.last = why

    def point_for(self, token):
        if re.fullmatch(r"[1-9][0-9]?", token):
            n = int(token)
            return n if 1 <= n <= len(self.spec) else 0
        if len(token) > 32:
            return 0
        for k, p in enumerate(self.spec, 1):
            if p["name"] and p["name"] == token:
                return k
        return 0

    def addr(self, point, cmd):
        return f"{self.prefix}/point/{point}/{cmd}"

    def answer(self, point, peer, rid, cmd, to, ok, why):
        msg = (self.addr(point, "reply"), [("i", rid), ("i", 1 if ok else 0),
                                           ("s", cmd), ("s", why)])
        self.log.append((point, peer, rid, cmd, to, msg))
        self.out_replies.append((to, msg))

    def recv(self, now, address, args, peer):
        head = self.prefix + "/point/"
        if not address.startswith(head):
            return False
        m = ADDR.match(address)
        if not m or m.group("prefix") != self.prefix:
            self.refuse("unknown command")
            return True
        token, cmd = m.group("token"), m.group("cmd")
        if peer[0] != "127.0.0.1":
            self.refuse("not loopback")
            return True
        point = self.point_for(token)
        if not point:
            self.refuse("no such point")
            return True
        held = self.holders.get(point)
        ps = self.spec[point - 1]

        if cmd in ("take", "release"):
            if not args or args[0][0] != "i" or args[0][1] < 1:
                self.refuse("bad request id")
                return True
            rid = args[0][1]
            to = peer
            ok_args = len(args) == (2 if cmd == "take" else 1)
            if ok_args and cmd == "take":
                t, port = args[1]
                if t != "i" or not (port == 0 or 1024 <= port <= 65535):
                    ok_args = False
                elif port:
                    to = (peer[0], port)
            # a repeat: the last answer for this point, if it was to this
            # sender, this id and this command
            mine = [e for e in self.log if e[0] == point]
            if ok_args and mine and mine[-1][1] == peer and mine[-1][2] == rid \
                    and mine[-1][3] == cmd:
                self.out_replies.append((mine[-1][4], mine[-1][5]))
                if held and held[0] == peer:
                    self.holders[point] = (held[0], held[1], now)
                return True
            if not ok_args:
                self.refuse("wrong arguments")
                self.answer(point, peer, rid, cmd, to, False, "wrong arguments")
                return True
            if cmd == "take":
                if held and held[0] != peer:
                    self.refuse("held")
                    self.answer(point, peer, rid, cmd, to, False,
                                f"held by {held[0][0]}:{held[0][1]}")
                    return True
                if not held:
                    self.out_events.append(("Taken", point, "", peer))
                self.holders[point] = (peer, to, now)
                self.accepted += 1
                self.answer(point, peer, rid, cmd, to, True, "")
                return True
            if not held:
                self.refuse("not held")
                self.answer(point, peer, rid, cmd, to, False, "not held")
                return True
            if held[0] != peer:
                self.refuse("not the holder")
                self.answer(point, peer, rid, cmd, to, False, "not the holder")
                return True
            del self.holders[point]
            self.accepted += 1
            self.out_events.append(("Returned", point, "released", peer))
            self.answer(point, peer, rid, cmd, held[1], True, "")
            return True

        want = {"xy": 2, "z": 1, "u": 1, "xyz": 3}.get(cmd)
        if want is None:
            self.refuse("unknown command")
            return True
        if cmd not in ps["inlets"]:
            self.refuse(cmd + " closed")
            return True
        if not held:
            self.refuse("not held")
            return True
        if held[0] != peer:
            self.refuse("not the holder")
            return True
        if len(args) != want or any(t not in ("i", "f") for t, _ in args):
            self.refuse("wrong arguments")
            return True
        vals = [f32(v) if t == "f" else float(v) for t, v in args]
        if not all(math.isfinite(v) for v in vals):
            self.refuse("not finite")
            return True
        self.holders[point] = (held[0], held[1], now)

        def onto(v, axis, lo, hi):
            r = ps["range"].get(axis)
            if r is None:
                return v
            frac = (v - r[0]) / (r[1] - r[0])
            return lo + frac * (hi - lo)

        mv = {"point": point}
        if cmd in ("xy", "xyz"):
            x, y = onto(vals[0], "x", 0.0, 1.0), onto(vals[1], "y", 0.0, 1.0)
            if not self.inside(f32(x), f32(y)):
                self.refuse("off the plan")
                return True
            mv["xy"] = (x, y)
        if cmd in ("z", "xyz"):
            z = onto(vals[0] if cmd == "z" else vals[2], "z", self.zlo, self.zhi)
            mv["z"] = sorted((self.zlo, z, self.zhi))[1]
        if cmd == "u":
            u = onto(vals[0], "u", 0.0, self.utop)
            mv["u"] = sorted((0.0, u, self.utop))[1]
        self.accepted += 1
        self.out_moves.append(mv)
        return True

    def lose(self, point, why):
        peer, to, _ = self.holders.pop(point)
        self.out_replies.append((to, (self.addr(point, "lost"), [("s", why)])))
        self.out_events.append(("Returned", point, why, peer))

    def tick(self, now):
        for point in sorted(self.holders):
            silence = self.spec[point - 1]["silence"]
            if silence > 0 and now - self.holders[point][2] > silence:
                self.lose(point, "silent")

    def hand(self, point):
        if point in self.holders:
            self.lose(point, "hand")

    def add(self):
        """3B-2g: a point after the last, every inlet; holds kept."""
        if len(self.spec) >= MAX_POINTS:
            return False
        self.spec.append(default_spec()[0])
        return True

    def remove(self):
        """3B-2g: the last point only; its holder told "removed"; what it
        last answered forgotten with it."""
        n = len(self.spec)
        if n <= 1:
            return False
        if n in self.holders:
            self.lose(n, "removed")
        self.log = [e for e in self.log if e[0] != n]
        self.spec.pop()
        return True

    def result(self):
        """One canonical line of everything since the last: moves, events,
        replies, counts -- as run_control.cpp prints it."""
        parts = ["MOVES"]
        for mv in self.out_moves:
            parts.append(str(mv["point"]))
            if "xy" in mv:
                parts += ["xy", num(mv["xy"][0]), num(mv["xy"][1])]
            if "z" in mv:
                parts += ["z", num(mv["z"])]
            if "u" in mv:
                parts += ["u", num(mv["u"])]
            parts.append(";")
        parts.append("EVENTS")
        for kind, point, why, peer in self.out_events:
            parts += [kind, str(point), why or "-", f"{peer[0]}:{peer[1]}", ";"]
        parts.append("REPLIES")
        for to, (addr, args) in self.out_replies:
            parts += [f"{to[0]}:{to[1]}", addr]
            for t, v in args:
                parts.append(f"{t}:{v.replace(' ', '_')}" if t == "s" else f"{t}:{v}")
            parts.append(";")
        parts += ["ACCEPTED", str(self.accepted), "REFUSED", str(self.refused),
                  "LAST", self.last.replace(" ", "_") or "-"]
        self.out_moves, self.out_events, self.out_replies = [], [], []
        return " ".join(parts)


# ---- the vectors ------------------------------------------------------------

def write_fixture(name, text):
    os.makedirs(FIX, exist_ok=True)
    with open(os.path.join(FIX, name), "w") as f:
        f.write(text)


class Script:
    def __init__(self, out, name, note, spec_file="-", plan="all",
                 extent=(0.0, 0.5, 2.0), prefix="/manifold/room"):
        self.out = out
        for line in note:
            out.append("# " + line)
        spec = default_spec()
        if spec_file != "-":
            with open(os.path.join(FIX, spec_file)) as f:
                spec, why = load_control(f.read())
            assert spec is not None, why
        self.c = Control(prefix, spec, plan, extent)
        out.append(f"SCRIPT {name} SPEC {spec_file} PLAN {plan} PREFIX {prefix} "
                   f"EXTENT {num(extent[0])} {num(extent[1])} {num(extent[2])}")

    def recv(self, now, address, args=(), peer=("127.0.0.1", 57120)):
        args = list(args)
        now = f32(now)
        ours = self.c.recv(now, address, args, peer)
        a = " ".join(f"{t}:{v}" for t, v in args) or "-"
        self.out.append(f"RECV {num(now)} {peer[0]} {peer[1]} {address} {a} "
                        f"OURS {int(ours)} EXPECT {self.c.result()}")

    def tick(self, now):
        now = f32(now)
        self.c.tick(now)
        self.out.append(f"TICK {num(now)} EXPECT {self.c.result()}")

    def hand(self, point):
        self.c.hand(point)
        self.out.append(f"HAND {point} EXPECT {self.c.result()}")

    def add(self):
        ok = self.c.add()
        self.out.append(f"ADD {int(ok)} EXPECT {self.c.result()}")

    def remove(self):
        ok = self.c.remove()
        self.out.append(f"REMOVE {int(ok)} EXPECT {self.c.result()}")

    def end(self):
        self.out.append("END")
        self.out.append("")


def main():
    out = ["# control.vec -- points moved from outside, round 3B-2f",
           "# generated by tests/ref/reference_control.py; checked by",
           "# tests/run_control.cpp against ofxManifoldPlayer's PointControl",
           "# and loadControl / saveControl.",
           "#",
           "# FILE name file OK|REFUSED reason   -- loadControl, for N points",
           "# ROUND name file EXPECT-FILE file   -- read, written: exactly that",
           "# SCRIPT ... RECV/TICK/HAND ... EXPECT <moves events replies counts>",
           ""]
    n = 0

    # ---- files ----
    good = {
        "default.json": {"control": 1, "points": [{"number": 1}]},
        "lead.json": {"control": 1, "points": [
            {"number": 1, "name": "lead", "inlets": ["xy", "z"],
             "range": {"x": [0, 127], "y": [0, 127], "z": [0, 1]}, "silence": 2}]},
        "u_only.json": {"control": 1, "points": [
            {"number": 1, "name": "drone", "inlets": ["u"], "silence": 0}]},
        "two.json": {"control": 1, "points": [
            {"number": 1, "name": "lead"}, {"number": 2, "name": "bass", "inlets": ["xyz", "u"],
                                            "range": {"u": [-1, 1]}}]},
    }
    bad = [
        ("version.json", {"control": 2, "points": [{"number": 1}]},
         "missing or unsupported control version"),
        ("no_points.json", {"control": 1, "points": []},
         "points must be a list of at least one"),
        ("extra_key.json", {"control": 1, "points": [{"number": 1}], "x": 1},
         "unknown key: x"),
        ("order.json", {"control": 1, "points": [{"number": 2}]},
         "point 1: numbered out of order"),
        ("bad_name.json", {"control": 1, "points": [{"number": 1, "name": "1st"}]},
         "point 1: a name is a letter"),
        ("long_name.json", {"control": 1, "points": [{"number": 1, "name": "a" * 33}]},
         "point 1: a name is a letter"),
        ("twice_name.json", {"control": 1, "points": [{"number": 1, "name": "a"},
                                                      {"number": 2, "name": "a"}]},
         "point 2: the name a is taken"),
        ("inlet.json", {"control": 1, "points": [{"number": 1, "inlets": ["w"]}]},
         "point 1: unknown inlet: w"),
        ("inlet_twice.json", {"control": 1, "points": [{"number": 1, "inlets": ["z", "z"]}]},
         "point 1: the inlet z twice"),
        ("range_axis.json", {"control": 1, "points": [{"number": 1, "range": {"w": [0, 1]}}]},
         "point 1: unknown range axis: w"),
        ("range_order.json", {"control": 1, "points": [{"number": 1, "range": {"x": [1, 0]}}]},
         "point 1: a range's low must be below its high"),
        ("range_shape.json", {"control": 1, "points": [{"number": 1, "range": {"x": [0]}}]},
         "point 1: a range is two numbers"),
        ("silence.json", {"control": 1, "points": [{"number": 1, "silence": -1}]},
         "point 1: silence is 0 or more seconds"),
        ("point_key.json", {"control": 1, "points": [{"number": 1, "speed": 2}]},
         "point 1: unknown key: speed"),
        ("seventeen.json", {"control": 1, "points": [{"number": k} for k in range(1, 18)]},
         "at most 16 points"),
    ]
    out.append("# ---- the control file ----")
    for name, d in good.items():
        text = json.dumps(d, indent=2)
        write_fixture(name, text)
        spec, why = load_control(text)
        assert spec is not None, (name, why)
        out.append(f"FILE {name} 16 OK -")
        n += 1
    for name, d, why in bad:
        text = json.dumps(d, indent=2)
        write_fixture(name, text)
        spec, got = load_control(text)
        assert spec is None and got.startswith(why), (name, got)
        out.append(f"FILE {name} 16 REFUSED {why}")
        n += 1
    out.append("# a player with one point refuses a file with two")
    spec, got = load_control(json.dumps(good["two.json"]), points=1)
    assert got == "this player has 1 point"
    out.append("FILE two.json 1 REFUSED this player has 1 point")
    n += 1
    write_fixture("not_json.json", "{ control: 1 ")
    out.append("FILE not_json.json 16 REFUSED json")
    n += 1
    out.append("# read and written again: the player's own form, exactly")
    for name in good:
        with open(os.path.join(FIX, name)) as f:
            spec, _ = load_control(f.read())
        saved = save_control(spec)
        again, why = load_control(saved)
        assert again is not None and save_control(again) == saved, (name, why)
        written = name.replace(".json", ".saved.json")
        write_fixture(written, saved)
        out.append(f"ROUND {name} EXPECT-FILE {written}")
        n += 1
    out.append("")

    # ---- taking points from outside ----
    t = Script(out, "take_move_release",
               ["take, move, release: answered to the reply port; moves from the",
                "holder only, not answered; z and u held to the levels"])
    t.recv(0.0, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)])
    t.recv(0.1, "/manifold/room/point/1/take", [("i", 1), ("i", 9002)])
    t.recv(0.2, "/manifold/room/point/1/xy", [("f", 0.25), ("f", 0.75)])
    t.recv(0.3, "/manifold/room/point/1/z", [("f", 0.3)])
    t.recv(0.4, "/manifold/room/point/1/z", [("f", 0.9)])
    t.recv(0.5, "/manifold/room/point/1/u", [("f", -1.0)])
    t.recv(0.6, "/manifold/room/point/1/xyz", [("f", 0.5), ("i", 1), ("f", 0.1)])
    t.recv(0.7, "/manifold/room/point/1/release", [("i", 2)])
    t.recv(0.8, "/manifold/room/point/1/z", [("f", 0.2)])
    t.end()
    t = Script(out, "two_senders",
               ["a second sender cannot take a held point, nor move it, nor",
                "release it; the holder taking again is a retry"])
    other = ("127.0.0.1", 57300)
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 7), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/1/take", [("i", 1), ("i", 0)], peer=other)
    t.recv(0.2, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)], peer=other)
    t.recv(0.3, "/manifold/room/point/1/release", [("i", 2)], peer=other)
    t.recv(0.4, "/manifold/room/point/1/take", [("i", 8), ("i", 0)])
    t.recv(0.5, "/manifold/room/point/1/release", [("i", 9)])
    t.recv(0.6, "/manifold/room/point/1/take", [("i", 3), ("i", 0)], peer=other)
    t.end()
    t = Script(out, "repeats",
               ["a repeated request id from the same sender: the same answer",
                "again, nothing done twice -- a refusal repeated stays refused"])
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 5), ("i", 9002)])
    t.recv(0.1, "/manifold/room/point/1/take", [("i", 5), ("i", 9002)])
    t.recv(0.2, "/manifold/room/point/1/release", [("i", 6)])
    t.recv(0.3, "/manifold/room/point/1/release", [("i", 6)])
    t.recv(0.4, "/manifold/room/point/1/release", [("i", 7)])
    t.recv(0.5, "/manifold/room/point/1/release", [("i", 7)])
    t.end()
    t = Script(out, "refused_at_the_edge",
               ["every malformed message refused, counted, with its reason; a",
                "command whose id can be read is answered"])
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 1), ("i", 0)], peer=("192.168.1.5", 57120))
    t.recv(0.1, "/manifold/room/point/2/take", [("i", 1), ("i", 0)])
    t.recv(0.2, "/manifold/room/point/01/take", [("i", 1), ("i", 0)])
    t.recv(0.3, "/manifold/room/point/1/take", [])
    t.recv(0.4, "/manifold/room/point/1/take", [("i", 0), ("i", 0)])
    t.recv(0.5, "/manifold/room/point/1/take", [("f", 1.0), ("i", 0)])
    t.recv(0.6, "/manifold/room/point/1/take", [("i", 1)])
    t.recv(0.7, "/manifold/room/point/1/take", [("i", 2), ("i", 80)])
    t.recv(0.8, "/manifold/room/point/1/take", [("i", 3), ("f", 9002.0)])
    t.recv(0.9, "/manifold/room/point/1/spin", [("f", 1.0)])
    t.recv(1.0, "/manifold/room/point/1/x/y", [("f", 1.0)])
    t.recv(1.1, "/manifold/room/point/1/take", [("i", 4), ("i", 0)])
    t.recv(1.2, "/manifold/room/point/1/xy", [("f", 0.5)])
    t.recv(1.3, "/manifold/room/point/1/xy", [("f", 0.5), ("s", "a")])
    t.recv(1.35, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5), ("f", 0.5)])
    t.recv(1.4, "/manifold/room/point/1/z", [("f", float("inf"))])
    t.recv(1.5, "/manifold/room/point/1/z", [("f", float("nan"))])
    t.recv(1.6, "/manifold/forest/point/1/take", [("i", 5), ("i", 0)])
    t.recv(1.7, "/manifold/room/describe", [])
    t.end()
    t = Script(out, "off_the_plan",
               ["an xy off the floor plan is refused and the point stays; an xyz",
                "off it likewise, its z with it"], plan="square")
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)])
    t.recv(0.2, "/manifold/room/point/1/xy", [("f", 0.9), ("f", 0.5)])
    t.recv(0.3, "/manifold/room/point/1/xyz", [("f", 0.1), ("f", 0.1), ("f", 0.3)])
    t.recv(0.4, "/manifold/room/point/1/xy", [("f", 0.8), ("f", 0.2)])
    t.end()
    t = Script(out, "silence_and_hand",
               ["silent past its silence, a held point goes back, the holder told;",
                "a move keeps it; the hand always wins, the holder told"])
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 1), ("i", 9002)])
    t.tick(4.9)
    t.recv(5.0, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)])
    t.tick(9.9)
    t.tick(10.1)
    t.recv(10.2, "/manifold/room/point/1/take", [("i", 2), ("i", 9002)])
    t.hand(1)
    t.hand(1)
    t.recv(10.3, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)])
    t.end()
    t = Script(out, "named_ranged_and_closed",
               ["a point by its name or its number; a controller's 0..127 onto",
                "the map; an inlet the file closed is refused; held until",
                "released when its silence is 2, past 2"], spec_file="lead.json",
               extent=(-0.25, 0.75, 2.0))
    t.recv(0.0, "/manifold/room/point/lead/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/lead/xy", [("i", 0), ("f", 127.0)])
    t.recv(0.2, "/manifold/room/point/1/xy", [("f", 63.5), ("f", 31.75)])
    t.recv(0.3, "/manifold/room/point/lead/z", [("f", 0.5)])
    t.recv(0.4, "/manifold/room/point/lead/z", [("f", 2.0)])
    t.recv(0.5, "/manifold/room/point/lead/u", [("f", 1.0)])
    t.recv(0.6, "/manifold/room/point/lead/xyz", [("f", 1.0), ("f", 1.0), ("f", 1.0)])
    t.recv(0.7, "/manifold/room/point/Lead/take", [("i", 2), ("i", 0)])
    t.tick(2.6)
    t.tick(2.8)
    t.end()
    t = Script(out, "held_until_released",
               ["silence 0: never let go for silence; u alone, onto the levels"],
               spec_file="u_only.json", extent=(0.0, 1.0, 3.0))
    t.recv(0.0, "/manifold/room/point/drone/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/drone/u", [("f", 2.5)])
    t.recv(0.2, "/manifold/room/point/drone/xy", [("f", 0.5), ("f", 0.5)])
    t.tick(1000.0)
    t.recv(1000.1, "/manifold/room/point/drone/u", [("f", 7.0)])
    t.end()
    t = Script(out, "two_points",
               ["two points from a file: held apart, each its own holder, a",
                "range on u onto 0..top"], spec_file="two.json", extent=(0.0, 1.0, 2.0))
    t.recv(0.0, "/manifold/room/point/lead/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/bass/take", [("i", 1), ("i", 0)], peer=other)
    t.recv(0.2, "/manifold/room/point/2/xyz", [("f", 0.5), ("f", 0.5), ("f", 0.5)], peer=other)
    t.recv(0.3, "/manifold/room/point/2/u", [("f", 0.0)], peer=other)
    t.recv(0.4, "/manifold/room/point/1/xyz", [("f", 0.5), ("f", 0.5), ("f", 0.5)], peer=other)
    t.recv(0.5, "/manifold/room/point/3/take", [("i", 1), ("i", 0)])
    t.end()
    # ---- round 3B-2g: points added and removed by the player ----
    t = Script(out, "added_and_removed",
               ["3B-2g: a point added takes every inlet and is held apart; a",
                "hold on another is kept; only the last is removed, its holder",
                "told; a point added again answers no old repeat; point 1 is",
                "never removed"])
    t.recv(0.0, "/manifold/room/point/1/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/2/take", [("i", 1), ("i", 0)], peer=other)
    t.add()
    t.recv(0.2, "/manifold/room/point/2/take", [("i", 1), ("i", 0)], peer=other)
    t.recv(0.3, "/manifold/room/point/2/u", [("f", 1.0)], peer=other)
    t.recv(0.4, "/manifold/room/point/1/xy", [("f", 0.5), ("f", 0.5)])
    t.add()
    t.recv(0.5, "/manifold/room/point/3/xy", [("f", 0.5), ("f", 0.5)], peer=other)
    t.remove()
    t.recv(0.6, "/manifold/room/point/3/take", [("i", 1), ("i", 0)])
    t.remove()
    t.recv(0.7, "/manifold/room/point/2/u", [("f", 1.0)], peer=other)
    t.add()
    t.recv(0.8, "/manifold/room/point/2/take", [("i", 1), ("i", 0)], peer=other)
    t.recv(0.9, "/manifold/room/point/1/xy", [("f", 0.25), ("f", 0.25)])
    t.remove()
    t.remove()
    t.recv(1.0, "/manifold/room/point/1/release", [("i", 2)])
    t.end()
    t = Script(out, "sixteen_at_most",
               ["3B-2g: added up to sixteen, never a seventeenth; the sixteenth",
                "taken and moved like the first"])
    for _ in range(15):
        t.add()
    t.add()
    t.recv(0.0, "/manifold/room/point/16/take", [("i", 1), ("i", 0)])
    t.recv(0.1, "/manifold/room/point/16/xyz", [("f", 0.5), ("f", 0.5), ("f", 0.25)])
    t.recv(0.2, "/manifold/room/point/17/take", [("i", 1), ("i", 0)])
    t.end()
    n += sum(1 for line in out if line.startswith(("RECV", "TICK", "HAND", "ADD", "REMOVE")))

    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"control.vec: {n} vectors")


if __name__ == "__main__":
    main()
