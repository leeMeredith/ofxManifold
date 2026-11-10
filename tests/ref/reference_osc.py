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
    PROTOCOL = 2

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

    def set_map(self, name):
        """Another map's name: new addresses, so everything again."""
        self.map_name = clean(name) if name else "untitled"
        prefix = self.root + "/" + self.map_name
        if prefix != self.prefix:
            self.prefix = prefix
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

    def frame(self, now, outputs, values, position):
        """outputs: [(name, channel, level)] in channel order; values:
        [(name, value)]; position: (x, y, z) or None. The messages to send
        now, as (address, args)."""
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
                          ("i", self.PROTOCOL)]))
            args = [("i", len(layout))]
            for n, c in layout:
                args += [("s", n), ("i", c)]
            msgs.append((p + "/outputs", args))
            self.described = layout
            self.describe_asked = False
            self.last_describe = now
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
        ("values", "/manifold/values",
         [("i", 2), ("s", "brightness"), ("f", 0.75), ("s", "reverb"), ("f", 0.2)]),
        ("hello", "/manifold/hello", [("s", "sclang")]),
        ("describe", "/manifold/describe", []),
        ("roll_call", "/manifold/map",
         [("s", "/manifold/room"), ("i", 57121), ("i", 12), ("i", 2)]),
        ("utf8_name", "/manifold/out/café", [("f", 0.5)]),
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

    def frame(self, now, outputs, values=(), position=None):
        o = ",".join(f"{n}:{c}:{fmtf(lv)}" for n, c, lv in outputs) or "-"
        v = ",".join(f"{n}:{fmtf(x)}" for n, x in values) or "-"
        pos = " ".join(fmtf(c) for c in position) if position else "-"
        msgs = self.s.frame(now, list(outputs), list(values), position)
        self.out.append(f"FRAME {now!r} | {o} | {v} | {pos} | {len(msgs)}")
        for address, args in msgs:
            self.out.append(f"MSG {address} {tags(args)} {argtext(args)}".rstrip())

    def map(self, name):
        self.s.set_map(name)
        self.out.append(f"MAP {name}")

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
