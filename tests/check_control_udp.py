#!/usr/bin/env python3
"""
ofxManifoldPlayer -- points moved from outside, over a real socket (3B-2f).

Two senders on this machine, each its own UDP socket, send what a controller
would -- built by python-osc, not by our own encoder -- to tests/control_udp,
which hands them to PointControl as example-osc does and sends its answers
back over UDP. This checks the answers that arrive, decoded by python-osc,
and what control_udp applied, against tests/ref/reference_control.py.
"""

import os
import socket
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "ref"))
import reference_control as C  # noqa: E402

try:
    from pythonosc.osc_message import OscMessage
    from pythonosc.osc_message_builder import OscMessageBuilder
except ImportError:
    print("  skip  python-osc is not installed (pip install python-osc)")
    sys.exit(0)


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def packet(address, args):
    b = OscMessageBuilder(address=address)
    for t, v in args:
        b.add_arg(v, {"i": "i", "f": "f", "s": "s"}[t])
    return b.build().dgram


def main():
    binary = os.path.join("build", "control_udp")
    listen = free_port()
    a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    a.bind(("127.0.0.1", 0))
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b.bind(("127.0.0.1", 0))
    replies = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    replies.bind(("127.0.0.1", 0))
    for s in (a, b, replies):
        s.settimeout(2.0)
    pa, pb, pr = a.getsockname()[1], b.getsockname()[1], replies.getsockname()[1]

    room = "/manifold/room/point/1/"
    steps = [  # (socket, peer port, address, args)
        (a, pa, room + "take", [("i", 1), ("i", pr)]),
        (a, pa, room + "xy", [("f", 0.25), ("f", 0.75)]),
        (a, pa, room + "u", [("f", 1.5)]),
        (a, pa, room + "xy", [("f", 0.9), ("f", 0.5)]),
        (b, pb, room + "take", [("i", 4), ("i", 0)]),
        (b, pb, room + "xy", [("f", 0.5), ("f", 0.5)]),
        (a, pa, room + "xyz", [("f", 0.5), ("f", 0.5), ("f", 0.75)]),
        (a, pa, room + "release", [("i", 2)]),
        (b, pb, room + "take", [("i", 5), ("i", 0)]),
    ]

    # What the reference says, step by step.
    ref = C.Control("/manifold/room", C.default_spec(), "square", (0.0, 0.5, 2.0))
    want_lines, want_replies = [], []
    for k, (_, port, address, args) in enumerate(steps):
        ref.recv(C.f32(0.1 * k), address, list(args), ("127.0.0.1", port))
        for mv in ref.out_moves:
            line = f"MOVE {mv['point']}"
            if "xy" in mv:
                line += " xy %.6g %.6g" % (C.f32(mv["xy"][0]), C.f32(mv["xy"][1]))
            if "z" in mv:
                line += " z %.6g" % C.f32(mv["z"])
            if "u" in mv:
                line += " u %.6g" % C.f32(mv["u"])
            want_lines.append(line)
        for kind, point, why, _ in ref.out_events:
            want_lines.append(f"EVENT {kind} {point} {why or '-'}")
        for to, (addr, rargs) in ref.out_replies:
            want_replies.append((to[1], addr, [v for _, v in rargs]))
        ref.out_moves, ref.out_events, ref.out_replies = [], [], []
    want_lines.append(f"COUNTS {ref.accepted} {ref.refused} {ref.last or '-'}")

    proc = subprocess.Popen([binary, str(listen)], stdout=subprocess.PIPE, text=True)
    assert proc.stdout.readline().strip() == "listening"
    got_replies = []
    for sock, _, address, args in steps:
        sock.sendto(packet(address, args), ("127.0.0.1", listen))
        # An answer, if this step has one, before the next is sent: arrival
        # order is the order of the steps.
    a.sendto(packet("/done", []), ("127.0.0.1", listen))
    out, _ = proc.communicate(timeout=10)
    got_lines = out.strip().splitlines()

    by_port = {pa: a, pb: b, pr: replies}
    for port, _, _ in want_replies:
        data, _ = by_port[port].recvfrom(4096)
        m = OscMessage(data)
        got_replies.append((port, m.address, list(m.params)))

    ok = got_lines == want_lines and got_replies == want_replies
    if not ok:
        print("  FAIL  points from outside over UDP")
        print("    want lines  ", want_lines)
        print("    got lines   ", got_lines)
        print("    want replies", want_replies)
        print("    got replies ", got_replies)
        return 1
    print(f"  ok  points from outside over UDP: {len(steps)} messages from two senders, "
          f"{len(got_replies)} answers back, decoded by python-osc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
