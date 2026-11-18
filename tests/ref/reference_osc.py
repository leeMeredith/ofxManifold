#!/usr/bin/env python3
"""
ofxManifold -- OSC out reference (round 3B-1).

Writes tests/vectors/osc.vec: what ofxManifoldPlayer's OSC encoder and its
per-frame sender must produce. See PLAN-round3.md, section J, 3B-1.

Three kinds of vector:

  ENCODE   a message and its exact bytes. The encoder here is written from
           the OSC 1.0 specification with struct; where python-osc is
           installed, every one is also built by python-osc and must come out
           byte for byte the same (CROSS). Without it the file is identical,
           the cross check is only skipped -- so CI need not install it.
  DECODE   bytes and the message they hold, malformed bytes refused.
  SEND     a scripted run of the sender: settings, then frames at times with
           outputs, values and a position, and messages received; after each,
           exactly the messages it sends, in order.

Floats are float32 on the wire and printed with %.9g, which round-trips.
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vectors", "osc.vec")

try:
    from pythonosc.osc_message_builder import OscMessageBuilder  # type: ignore
    from pythonosc.osc_message import OscMessage as PyOscMessage  # type: ignore
    HAVE_PYTHONOSC = True
except ImportError:  # pragma: no cover
    HAVE_PYTHONOSC = False


# ---------------------------------------------------------------------------
# Encoding (OSC 1.0)
# ---------------------------------------------------------------------------

def f32(x):
    """x as the float32 it becomes on the wire."""
    return struct.unpack(">f", struct.pack(">f", x))[0]


def fmtf(x):
    return "%.9g" % f32(x)


def pad_string(s):
    b = s.encode("utf-8") + b"\0"
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def encode(address, args):
    """args: list of (type, value), type in 'i', 'f', 's'."""
    out = pad_string(address)
    out += pad_string("," + "".join(t for t, _ in args))
    for t, v in args:
        if t == "i":
            out += struct.pack(">i", v)
        elif t == "f":
            out += struct.pack(">f", v)
        elif t == "s":
            out += pad_string(v)
        else:
            raise ValueError(t)
    return out


def read_string(data, at):
    """(string, next offset) or None: null-terminated, padded to four."""
    end = data.find(b"\0", at)
    if end < 0:
        return None
    try:
        s = data[at:end].decode("utf-8")
    except UnicodeDecodeError:
        return None
    nxt = end + 1
    nxt += (4 - nxt % 4) % 4
    if nxt > len(data):
        return None
    if any(b != 0 for b in data[end:nxt]):
        return None
    return s, nxt


def decode(data):
    """(address, args) or None. Only i, f and s; anything else refused."""
    if len(data) % 4 != 0 or len(data) < 8:
        return None
    r = read_string(data, 0)
    if r is None or not r[0].startswith("/"):
        return None
    address, at = r
    r = read_string(data, at)
    if r is None or not r[0].startswith(","):
        return None
    tags, at = r
    args = []
    for t in tags[1:]:
        if t in "if":
            if at + 4 > len(data):
                return None
            v = struct.unpack(">i" if t == "i" else ">f", data[at:at + 4])[0]
            at += 4
            args.append((t, v))
        elif t == "s":
            r = read_string(data, at)
            if r is None:
                return None
            args.append(("s", r[0]))
            at = r[1]
        else:
            return None
    if at != len(data):
        return None
    return address, args


def cross_check(address, args, data):
    """python-osc builds the same bytes, and reads ours back the same."""
    if not HAVE_PYTHONOSC:
        return
    b = OscMessageBuilder(address=address)
    for t, v in args:
        b.add_arg(v, {"i": "i", "f": "f", "s": "s"}[t])
    built = b.build().dgram
    assert built == data, (address, built.hex(), data.hex())
    m = PyOscMessage(data)
    assert m.address == address
    got = list(m.params)
    for (t, v), g in zip(args, got):
        if t == "f":
            assert f32(v) == g, (v, g)
        else:
            assert v == g, (v, g)


def argtext(args):
    """Arguments as text: floats %.9g, ints plain, strings in double quotes
    (a name may hold a space)."""
    parts = []
    for t, v in args:
        parts.append(fmtf(v) if t == "f" else f'"{v}"' if t == "s" else str(v))
    return " ".join(parts)


def tags(args):
    return "," + "".join(t for t, _ in args)


# ---------------------------------------------------------------------------
# The sender
# ---------------------------------------------------------------------------

BAD = set(" #*,/?[]{}")


def clean(name):
    """A name as it can stand in an OSC address."""
    return "".join("_" if c in BAD else c for c in name) or "_"


class Sender:
    """What the sender sends, frame by frame. Mirrors OscOut in C++."""

    DESCRIBE_EVERY = 2.0
    REFRESH_EVERY = 0.1
    HELLO_LASTS = 3.0
    CHANGED = 1e-6
    PROTOCOL = 5        # 3B-2b: the shape message; 3B-2f: points from outside;
                        # 3B-2g: points 2 and up, each its own levels and position

    def __init__(self, prefix="/manifold", map_name="untitled", answer_port=0,
                 packed=True, per_output=False, rate=60.0):
        self.root = prefix.rstrip("/") or "/manifold"
        self.map_name = clean(map_name) if map_name else "untitled"
        self.prefix = self.root + "/" + self.map_name
        self.answer_port = answer_port
        self.packed = packed
        self.per_output = per_output
        self.rate = rate
        self.peer = ""
        self.last_hello = None
        self.points = []               # 3B-2f: [(number, name, inlets)], kept across starts
        self.sent_more = []            # 3B-2g: the further points last sent, by number;
                                       # kept across starts, not across a new prefix
        self.start_again()

    def start_again(self):
        """Everything said again: sent at once, announced, described."""
        self.next_send = None          # no send yet
        self.last_describe = None
        self.described = None          # the outputs last described
        self.describe_asked = False
        self.last_refresh = None
        self.sent_out = {}             # name -> level last sent per output
        self.sent_value = {}
        self.sent_shape = None         # the shape last sent, as bytes
        self.sent_points = None        # the points last listed, as bytes

    def set_map(self, name):
        """Another map's name: new addresses, so everything again."""
        self.map_name = clean(name) if name else "untitled"
        prefix = self.root + "/" + self.map_name
        if prefix != self.prefix:
            self.prefix = prefix
            self.sent_more = []        # another map's points: nothing to zero here
            self.start_again()

    def receive(self, now, address, args):
        mine = (self.prefix, self.root)
        if address in (p + "/describe" for p in mine):
            self.describe_asked = True
        elif address in (p + "/hello" for p in mine):
            self.last_hello = now
            self.peer = args[0][1] if args and args[0][0] == "s" else ""

    def connected(self, now):
        return self.last_hello is not None and now - self.last_hello < self.HELLO_LASTS

    def frame(self, now, outputs, values, position, shape=None, more=()):
        """outputs: [(name, channel, level)] in channel order; values:
        [(name, value)]; position: (x, y, z) or None; shape (3B-2b): None,
        or (heights, {name: (level, levels, placed, x, y, z)}) -- an output
        not in it on no level, unplaced; more (3B-2g): points 2 and up, as
        (number, [levels in the outputs' order], (x, y, z[, u])). The
        messages to send now, as (address, args)."""
        if self.next_send is not None and now < self.next_send - 1e-9:
            return []
        period = 1.0 / self.rate
        # The next send one period on -- or, after a late frame, one period
        # from now: never a burst to catch up.
        if self.next_send is None or now > self.next_send + period:
            self.next_send = now + period
        else:
            self.next_send += period
        msgs = []
        p = self.prefix
        layout = [(n, c) for n, c, _ in outputs]
        if (self.described != layout or self.describe_asked
                or self.last_describe is None
                or now >= self.last_describe + self.DESCRIBE_EVERY - 1e-9):
            # The roll call leads: which map, where it hears answers, how
            # many outputs, which protocol.
            msgs.append((self.root + "/map",
                         [("s", p), ("i", self.answer_port), ("i", len(layout)),
                          ("i", self.PROTOCOL), ("i", len(self.points))]))
            args = [("i", len(layout))]
            for n, c in layout:
                args += [("s", n), ("i", c)]
            msgs.append((p + "/outputs", args))
            self.described = layout
            self.describe_asked = False
            self.last_describe = now
            self.sent_shape = None     # a description is followed by the shape
            self.sent_points = None    # and by the points
        # 3B-2g: a point gone since the last send says every level 0 once,
        # before the list without it -- what it fed fades, never sticks.
        numbers = [n for n, _, _ in more]
        for n in self.sent_more:
            if n not in numbers:
                msgs.append((p + "/point/%d/levels" % n, [("f", 0.0) for _ in outputs]))
        self.sent_more = numbers
        if shape is not None:
            heights, places = shape
            args = [("i", len(heights))] + [("f", h) for h in heights]
            args.append(("i", len(outputs)))
            for n, c, _ in outputs:
                lv, lvs, placed, x, y, z = places.get(n, (-1, 0, 0, 0, 0, 0))
                if not placed:
                    x = y = z = 0.0
                args += [("s", n), ("i", c), ("i", lv), ("i", lvs),
                         ("i", 1 if placed else 0),
                         ("f", x), ("f", y), ("f", z)]
            data = encode(p + "/shape", args)
            if data != self.sent_shape:
                msgs.append((p + "/shape", args))
                self.sent_shape = data
        if self.points:
            args = [("i", len(self.points))]
            for number, name, inlets in self.points:
                args += [("i", number), ("s", name), ("s", inlets)]
            data = encode(p + "/points", args)
            if data != self.sent_points:
                msgs.append((p + "/points", args))
                self.sent_points = data
        if self.packed:
            msgs.append((p + "/levels", [("f", lv) for _, _, lv in outputs]))
        refresh = (self.last_refresh is None
                   or now >= self.last_refresh + self.REFRESH_EVERY - 1e-9)
        if self.per_output:
            for n, _, lv in outputs:
                last = self.sent_out.get(n)
                if refresh or last is None or abs(f32(lv) - last) > self.CHANGED:
                    msgs.append((p + "/out/" + clean(n), [("f", lv)]))
                    self.sent_out[n] = f32(lv)
        if self.packed and values:
            args = [("i", len(values))]
            for n, v in values:
                args += [("s", n), ("f", v)]
            msgs.append((p + "/values", args))
        if self.per_output:
            for n, v in values:
                last = self.sent_value.get(n)
                if refresh or last is None or abs(f32(v) - last) > self.CHANGED:
                    msgs.append((p + "/value/" + clean(n), [("f", v)]))
                    self.sent_value[n] = f32(v)
        if self.per_output and refresh:
            self.last_refresh = now
        if position is not None:
            msgs.append((p + "/position", [("f", c) for c in position]))
        # 3B-2g: points 2 and up, after point 1's: always packed, in number
        # order -- their levels, then their position.
        for n, levels, pos in sorted(more, key=lambda m: m[0]):
            msgs.append((p + "/point/%d/levels" % n, [("f", lv) for lv in levels]))
            msgs.append((p + "/point/%d/position" % n, [("f", c) for c in pos]))
        return msgs


# ---------------------------------------------------------------------------
# Vectors
# ---------------------------------------------------------------------------

def encode_vectors(out):
    out.append("# ENCODE name | address | tags | args | hex")
    cases = [
        ("levels_two", "/manifold/levels", [("f", 0.5), ("f", 0.25)]),
        ("levels_none", "/manifold/levels", []),
        ("levels_odd", "/manifold/levels", [("f", 1.0), ("f", 0.0), ("f", 0.1)]),
        ("outputs_described", "/manifold/outputs",
         [("i", 3), ("s", "front"), ("i", 0), ("s", "left"), ("i", 1),
          ("s", "rear"), ("i", 3)]),
        ("string_pads_to_four", "/a", [("s", "abc")]),
        ("string_exactly_four", "/abc", [("s", "abcd")]),
        ("string_empty", "/x", [("s", "")]),
        ("int_negative", "/manifold/n", [("i", -2)]),
        ("int_large", "/manifold/n", [("i", 2147483647)]),
        ("float_small", "/manifold/out/front", [("f", 1e-7)]),
        ("float_negative_zero", "/f", [("f", -0.0)]),
        ("position", "/manifold/position", [("f", 0.3), ("f", 0.8), ("f", 0.55)]),
        ("position_with_level", "/manifold/room/position",
         [("f", 0.3), ("f", 0.8), ("f", 0.55), ("f", 1.25)]),
        ("values", "/manifold/values",
         [("i", 2), ("s", "brightness"), ("f", 0.75), ("s", "reverb"), ("f", 0.2)]),
        ("hello", "/manifold/hello", [("s", "sclang")]),
        ("describe", "/manifold/describe", []),
        ("roll_call", "/manifold/map",
         [("s", "/manifold/room"), ("i", 57121), ("i", 12), ("i", 2)]),
        ("utf8_name", "/manifold/out/café", [("f", 0.5)]),
        ("roll_call_protocol_3", "/manifold/map",
         [("s", "/manifold/room"), ("i", 57121), ("i", 12), ("i", 3)]),
        ("roll_call_protocol_4", "/manifold/map",
         [("s", "/manifold/room"), ("i", 57121), ("i", 12), ("i", 4), ("i", 1)]),
        ("roll_call_protocol_5", "/manifold/map",
         [("s", "/manifold/room"), ("i", 57121), ("i", 12), ("i", 5), ("i", 3)]),
        # 3B-2g: a further point's levels and position
        ("point_levels", "/manifold/room/point/2/levels",
         [("f", 0.5), ("f", 0.0), ("f", 0.125)]),
        ("point_position", "/manifold/room/point/12/position",
         [("f", 0.3), ("f", 0.8), ("f", 0.55), ("f", 1.25)]),
        # 3B-2f: a point named, a point not, their inlets as words
        ("points", "/manifold/room/points",
         [("i", 2), ("i", 1), ("s", "lead"), ("s", "xy z"),
          ("i", 2), ("s", ""), ("s", "xy z u xyz")]),
        # 3B-2b: two levels; FL on the floor, placed; FL-2 on the upper
        # level, placed over the same node; a send fed by none, unplaced
        ("shape", "/manifold/room/shape",
         [("i", 2), ("f", 0.0), ("f", 0.25), ("i", 3),
          ("s", "FL"), ("i", 0), ("i", 0), ("i", 1), ("i", 1),
          ("f", 0.2), ("f", 0.8), ("f", 0.0),
          ("s", "FL-2"), ("i", 1), ("i", 1), ("i", 1), ("i", 1),
          ("f", 0.2), ("f", 0.8), ("f", 0.25),
          ("s", "verb"), ("i", 2), ("i", -1), ("i", 0), ("i", 0),
          ("f", 0.0), ("f", 0.0), ("f", 0.0)]),
    ]
    for name, address, args in cases:
        data = encode(address, args)
        cross_check(address, args, data)
        assert decode(data) is not None
        out.append(f"ENCODE {name} | {address} | {tags(args)} | {argtext(args)} | "
                   f"{data.hex()}")


def decode_vectors(out):
    out.append("# DECODE name | hex | address | tags | args")
    good = [
        ("from_sclang_hello", "/manifold/hello", [("s", "sclang")]),
        ("from_describe", "/manifold/describe", []),
        ("mixed", "/m", [("i", 7), ("f", 2.5), ("s", "hi")]),
    ]
    for name, address, args in good:
        if HAVE_PYTHONOSC:
            b = OscMessageBuilder(address=address)
            for t, v in args:
                b.add_arg(v, t)
            data = b.build().dgram
            assert data == encode(address, args)
        else:
            data = encode(address, args)
        d = decode(data)
        assert d == (address, [(t, f32(v) if t == "f" else v) for t, v in args])
        out.append(f"DECODE {name} | {data.hex()} | {address} | {tags(args)} | "
                   f"{argtext(args)}")
    out.append("# REFUSE name | hex: malformed, decoded to nothing")
    ok = encode("/m", [("i", 7), ("f", 2.5), ("s", "hi")])
    bad = [
        ("empty", b""),
        ("not_four", ok[:-1]),
        ("no_slash", encode("/m", [])[:0] + pad_string("m") + pad_string(",")),
        ("no_comma", pad_string("/m") + pad_string("i") + struct.pack(">i", 1)),
        ("short_int", pad_string("/m") + pad_string(",i")),
        ("unknown_tag", pad_string("/m") + pad_string(",d") + b"\0" * 8),
        ("trailing", ok + b"\0\0\0\0"),
        ("string_unterminated", pad_string("/m") + pad_string(",s") + b"abcd"),
        ("padding_not_zero", b"/m\0x" + pad_string(",")),
    ]
    for name, data in bad:
        assert decode(data) is None, name
        out.append(f"REFUSE {name} | {data.hex() or '-'}")


class Script:
    def __init__(self, out, name, doc, **settings):
        self.out = out
        self.s = Sender(**settings)
        out.append(f"# {doc}")
        # The prefix as given, so the C++ must clean it the same way.
        given = settings.get("prefix", "/manifold")
        out.append(f"SEND {name} prefix={given} map={settings.get('map_name', 'untitled')} "
                   f"port={self.s.answer_port} packed={int(self.s.packed)} "
                   f"per_output={int(self.s.per_output)} rate={self.s.rate:g}")

    def shape(self, heights, places):
        """3B-2b: the map's shape for the frames after this, until another
        or none."""
        self.shape_now = (list(heights), dict(places))
        self.out.append("SHAPE " + (" ".join(fmtf(h) for h in heights) or "-")
                        + " | " + (",".join(
                            f"{n}:{lv}:{lvs}:{int(pl)}:{fmtf(x)}:{fmtf(y)}:{fmtf(z)}"
                            for n, (lv, lvs, pl, x, y, z) in places.items())
                            or "-"))

    def set_points(self, points):
        """3B-2f: the points taking moves from outside, for the frames after
        this: [(number, name, inlets)]."""
        self.s.points = list(points)
        self.out.append("POINTS " + (",".join(
            f"{n}:{name}:{inlets.replace(' ', '_')}" for n, name, inlets in points) or "-"))

    def more(self, points):
        """3B-2g: points 2 and up for the frames after this: [(number,
        [levels], (x, y, z[, u]))]."""
        self.more_now = [(n, list(lv), tuple(pos)) for n, lv, pos in points]
        self.out.append("MORE " + (" ".join(
            f"{n}=" + ",".join(fmtf(x) for x in lv) + "@" + ",".join(fmtf(c) for c in pos)
            for n, lv, pos in points) or "-"))

    def no_shape(self):
        self.shape_now = None
        self.out.append("NOSHAPE")

    def frame(self, now, outputs, values=(), position=None):
        o = ",".join(f"{n}:{c}:{fmtf(lv)}" for n, c, lv in outputs) or "-"
        v = ",".join(f"{n}:{fmtf(x)}" for n, x in values) or "-"
        pos = " ".join(fmtf(c) for c in position) if position else "-"
        msgs = self.s.frame(now, list(outputs), list(values), position,
                            getattr(self, "shape_now", None),
                            getattr(self, "more_now", []))
        self.out.append(f"FRAME {now!r} | {o} | {v} | {pos} | {len(msgs)}")
        for address, args in msgs:
            self.out.append(f"MSG {address} {tags(args)} {argtext(args)}".rstrip())

    def map(self, name):
        self.s.set_map(name)
        self.out.append(f"MAP {name}")

    def reset(self):
        """The app starting again on the same map (the same name opened
        again, or p or i): everything said again; a point gone still zeroed."""
        self.s.start_again()
        self.out.append("RESET")

    def recv(self, now, address, args=()):
        self.s.receive(now, address, list(args))
        self.out.append(f"RECV {now!r} {address} {tags(list(args))} "
                        f"{argtext(list(args))}".rstrip())

    def conn(self, now):
        c = self.s.connected(now)
        self.out.append(f'CONN {now!r} {int(c)} "{self.s.peer if c else ""}"')

    def end(self):
        self.out.append("END")


def send_vectors(out):
    two = [("front", 0, 0.75), ("left", 1, 0.25)]

    t = Script(out, "packed_every_send",
               "packed: the outputs described on the first send, then every "
               "level at every send, 60 a second; nothing between sends")
    t.frame(0.0, two)
    t.frame(0.005, two)
    t.frame(1 / 60, [("front", 0, 0.5), ("left", 1, 0.5)])
    t.frame(2 / 60, [("front", 0, 0.5), ("left", 1, 0.5)])
    t.end()

    t = Script(out, "described_again",
               "described again when an output is added -- the messages grow "
               "-- when one goes, when asked, and every two seconds")
    t.frame(0.0, two)
    t.frame(0.1, two + [("rear", 3, 0.0)])
    t.frame(0.2, two + [("rear", 3, 0.0)])
    t.frame(0.3, [("front", 0, 1.0)])
    t.recv(0.31, "/manifold/describe")
    t.frame(0.4, [("front", 0, 1.0)])
    t.frame(2.3, [("front", 0, 1.0)])
    t.frame(2.31, [("front", 0, 1.0)])
    t.frame(4.31, [("front", 0, 1.0)])
    t.end()

    t = Script(out, "per_output_changes",
               "per output: only what changed at each send, everything every "
               "tenth of a second", packed=False, per_output=True)
    t.frame(0.0, two)
    t.frame(1 / 60, two)
    t.frame(2 / 60, [("front", 0, 0.7), ("left", 1, 0.25)])
    t.frame(3 / 60, [("front", 0, 0.7), ("left", 1, 0.25)])
    t.frame(0.1, [("front", 0, 0.7), ("left", 1, 0.25)])
    t.frame(0.1 + 1 / 60, [("front", 0, 0.7), ("left", 1, 0.2500001)])
    t.end()

    t = Script(out, "both_shapes_and_values",
               "both shapes at once, values and the position", per_output=True)
    t.frame(0.0, two, [("brightness", 0.5), ("reverb", 0.1)], (0.3, 0.8, 0.0))
    t.frame(1 / 60, two, [("brightness", 0.5), ("reverb", 0.2)], (0.31, 0.8, 0.0))
    t.end()

    t = Script(out, "position_with_level",
               "3B-2c: where between the levels, /position's fourth number; "
               "without it, three as before", map_name="room")
    t.frame(0.0, two, [], (0.3, 0.8, 0.375, 1.5))
    t.frame(1 / 60, two, [], (0.3, 0.8, 0.375))
    t.end()

    t = Script(out, "rate_and_late_frames",
               "at 30 a second: frames between sends send nothing; a late "
               "frame sends once, then the rate carries on from it -- no burst",
               rate=30.0)
    t.frame(0.0, two)
    t.frame(0.02, two)
    t.frame(1 / 30, two)
    t.frame(0.5, two)
    t.frame(0.51, two)
    t.frame(0.5 + 1 / 30, two)
    t.end()

    t = Script(out, "prefix_and_names",
               "a prefix of its own; names cleaned for an address",
               prefix="/room2/", packed=False, per_output=True)
    t.frame(0.0, [("front left", 0, 0.5), ("sub#1", 5, 0.5)],
            [("hue/a", 0.25)])
    t.recv(0.01, "/manifold/describe")
    t.frame(0.1, [("front left", 0, 0.5), ("sub#1", 5, 0.5)])
    t.recv(0.11, "/room2/describe")
    t.frame(0.2, [("front left", 0, 0.5), ("sub#1", 5, 0.5)])
    t.end()

    t = Script(out, "hello",
               "a hello marks the receiver connected for three seconds; the "
               "sender sends the same with or without one")
    t.conn(0.0)
    t.recv(0.5, "/manifold/hello", [("s", "sclang")])
    t.conn(0.6)
    t.conn(3.4)
    t.conn(3.5)
    t.recv(4.0, "/manifold/hello")
    t.conn(4.1)
    t.recv(4.2, "/other/hello", [("s", "pd")])
    t.conn(7.1)
    t.frame(7.2, [])
    t.end()

    # ---- round 3B-2b: the shape message ----
    room = [("FL", 0, 0.5), ("FR", 1, 0.25), ("FL-2", 2, 0.0), ("mix", 3, 0.0)]
    placed = {"FL": (0, 1, True, 0.2, 0.8, 0.0),
              "FR": (0, 1, True, 0.8, 0.8, 0.0),
              "FL-2": (1, 1, True, 0.2, 0.8, 0.25)}
    t = Script(out, "shape_with_description",
               "the shape follows every description -- the first send, when "
               "asked, every two seconds -- and is sent alone whenever it "
               "changes: a level's height, an owner's height, a feeding "
               "level; not when only the sound levels move", map_name="room")
    t.shape([0.0, 0.25], placed)
    t.frame(0.0, room)
    t.frame(1 / 60, [("FL", 0, 0.25), ("FR", 1, 0.5), ("FL-2", 2, 0.25),
                     ("mix", 3, 0.0)])
    t.shape([0.0, 0.3], placed)
    t.frame(2 / 60, room)
    moved = dict(placed)
    moved["FL-2"] = (1, 1, True, 0.2, 0.8, 0.4)
    t.shape([0.0, 0.3], moved)
    t.frame(3 / 60, room)
    shared = dict(moved)
    shared["FR"] = (0, 2, True, 0.8, 0.8, 0.0)
    t.shape([0.0, 0.3], shared)
    t.frame(4 / 60, room)
    t.frame(5 / 60, room)
    t.recv(0.09, "/manifold/room/describe")
    t.frame(6 / 60, room)
    t.frame(2.1, room)
    t.no_shape()
    t.frame(2.12, room)
    t.end()

    t = Script(out, "shape_unplaced_and_new_outputs",
               "an output the shape does not place: on no level, unplaced, "
               "0 0 0; an output added: described again, the shape with it",
               map_name="room")
    t.shape([0.5], {"FL": (0, 1, True, 0.2, 0.8, 0.5),
                    "mix": (-1, 0, False, 0.9, 0.9, 0.9)})
    t.frame(0.0, room[:2] + [("mix", 3, 0.0)])
    t.frame(1 / 60, room)
    t.end()

    # ---- round 3B-2f: points from outside, counted and listed ----
    t = Script(out, "points_listed",
               "the points a sender may move from outside: counted in the roll "
               "call, listed after each description and when they change",
               map_name="room")
    t.set_points([(1, "", "xy z u xyz")])
    t.frame(0.0, room)
    t.frame(1 / 60, room)
    t.set_points([(1, "lead", "xy z")])
    t.frame(2 / 60, room)
    t.frame(3 / 60, room)
    t.recv(0.06, "/manifold/room/describe")
    t.frame(4 / 60, room)
    t.set_points([])
    t.frame(5 / 60, room)
    t.end()

    # ---- round 3B-2g: points 2 and up ----
    t = Script(out, "points_more",
               "points 2 and up: each its levels and position after point 1's, "
               "in number order, always packed; point 1's addresses as before; "
               "the roll call counts them all", map_name="room")
    t.set_points([(1, "", "xy z u xyz"), (2, "", "xy z u xyz"), (3, "fm", "xy")])
    t.more([(3, [0.0, 0.5], (0.2, 0.2, 0.0, 0.0)),
            (2, [0.75, 0.25], (0.6, 0.4, 0.125, 0.5))])
    t.frame(0.0, two, [("brightness", 0.5)], (0.3, 0.8, 0.0, 0.0))
    t.frame(1 / 60, two, [("brightness", 0.5)], (0.3, 0.8, 0.0, 0.0))
    t.more([(2, [0.5, 0.5], (0.6, 0.45, 0.125))])
    t.frame(2 / 60, two, [], (0.3, 0.8, 0.0, 0.0))
    t.end()

    t = Script(out, "points_more_per_output",
               "per output on, packed off: point 1 per output as before; points "
               "2 and up still packed", map_name="room", packed=False,
               per_output=True)
    t.set_points([(1, "", "xy"), (2, "", "xy")])
    t.more([(2, [0.25, 0.75], (0.5, 0.5, 0.0))])
    t.frame(0.0, two, [], (0.3, 0.8, 0.0))
    t.frame(1 / 60, two, [], (0.3, 0.8, 0.0))
    t.end()

    t = Script(out, "point_removed_zeroed",
               "a point removed says every level 0 once, before the list "
               "without it, then nothing; one added later is sent again; "
               "starting again on the same map still zeroes it; a new map "
               "starts with nothing to zero", map_name="room")
    t.set_points([(1, "", "xy"), (2, "", "xy"), (3, "", "xy")])
    t.more([(2, [0.5, 0.5], (0.5, 0.5, 0.0)), (3, [0.25, 1.0], (0.1, 0.9, 0.0))])
    t.frame(0.0, two, [], (0.3, 0.8, 0.0))
    t.set_points([(1, "", "xy"), (2, "", "xy")])
    t.more([(2, [0.5, 0.5], (0.5, 0.5, 0.0))])
    t.frame(1 / 60, two, [], (0.3, 0.8, 0.0))
    t.frame(2 / 60, two, [], (0.3, 0.8, 0.0))
    t.set_points([(1, "", "xy")])
    t.more([])
    t.frame(3 / 60, [("front", 0, 0.75), ("left", 1, 0.25), ("rear", 3, 0.5)],
            [], (0.3, 0.8, 0.0))
    t.set_points([(1, "", "xy"), (2, "", "xy")])
    t.more([(2, [0.0, 0.0], (0.5, 0.5, 0.0))])
    t.frame(4 / 60, two, [], (0.3, 0.8, 0.0))
    t.reset()
    t.set_points([(1, "", "xy")])
    t.more([])
    t.frame(5 / 60, two, [], (0.3, 0.8, 0.0))
    t.set_points([(1, "", "xy"), (2, "", "xy")])
    t.more([(2, [0.0, 0.0], (0.5, 0.5, 0.0))])
    t.frame(6 / 60, two, [], (0.3, 0.8, 0.0))
    t.map("kitchen")
    t.set_points([(1, "", "xy")])
    t.more([])
    t.frame(7 / 60, two, [], (0.3, 0.8, 0.0))
    t.end()

    t = Script(out, "maps_named_and_announced",
               "protocol 2: the map's name in every address; the roll call on "
               "root/map leading every description, with the answer port; "
               "describe and hello heard on the map's prefix or the root, not "
               "another map's; a new name says everything again at once",
               map_name="room", answer_port=57122)
    t.frame(0.0, two, [("brightness", 0.5)])
    t.frame(1 / 60, two)
    t.recv(0.02, "/manifold/kitchen/describe")
    t.frame(2 / 60, two)
    t.recv(0.04, "/manifold/room/describe")
    t.frame(3 / 60, two)
    t.recv(0.06, "/manifold/describe")
    t.frame(4 / 60, two)
    t.recv(0.07, "/manifold/kitchen/hello", [("s", "pd")])
    t.conn(0.08)
    t.recv(0.09, "/manifold/room/hello", [("s", "sclang")])
    t.conn(0.1)
    t.recv(0.11, "/manifold/hello", [("s", "python")])
    t.conn(0.12)
    t.map("my room")
    t.frame(0.13, two)
    t.frame(0.14, two)
    t.map("my room")
    t.frame(0.15, two)
    t.map("")
    t.frame(0.2, two)
    t.end()


def main():
    out = [
        "# ofxManifold OSC out vectors (round 3B-1).",
        "# Generated by tests/ref/reference_osc.py -- do not edit by hand.",
        "",
    ]
    encode_vectors(out)
    decode_vectors(out)
    send_vectors(out)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        k = line.split(" ", 1)[0]
        if k in ("ENCODE", "DECODE", "REFUSE", "SEND"):
            counts[k] = counts.get(k, 0) + 1
    print(f"wrote {OUT}" + ("" if HAVE_PYTHONOSC
                            else "  (python-osc absent: cross check skipped)"))
    for k in sorted(counts):
        print(f"  {k:<7} {counts[k]}")
    print(f"  TOTAL   {sum(counts.values())}")


if __name__ == "__main__":
    main()
