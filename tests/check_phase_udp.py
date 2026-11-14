#!/usr/bin/env python3
"""
ofxManifoldPlayer -- the bridge's contract over a real socket (round 3B-2).

A stand-in for ofxScPhaseBridge's bridge.scd sends what it sends: /phase,
0 to 1 once a bar, a hundred a second, wrapping; and /beat <step> when an
armed step sounds -- here steps 0, 4 and 10 of sixteen, over three bars, one
packet out of order among the phases. tests/phase_udp.cpp reads them as
example-osc does and prints where it ended; this checks that against the
reference: the count of bars, the steps the beats made, the walk's place.
"""

import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "ref"))
import reference_motion as M   # noqa: E402
import reference_osc as R      # noqa: E402


def stream():
    """The packets, in order, and the reference's own reading of them."""
    packets, clock = [], M.PhaseClock()
    walk = M.Walk(seed=5, clock=("at", []), travel=("duration", 0.0))
    armed, steps, last_step = {0, 4, 10}, 16, -1
    bar_seconds = 0.4
    for i in range(int(3 * bar_seconds * 100)):
        now = i / 100.0
        phase = R.f32((now / bar_seconds) % 1.0)
        packets.append(("/phase", [("f", phase)]))
        clock.feed(phase, i)
        if i == 57:                       # one late packet: an older phase
            old = R.f32(((now - 0.02) / bar_seconds) % 1.0)
            packets.append(("/phase", [("f", old)]))
            clock.feed(old, i)
        step = min(int(phase * steps), steps - 1)
        if step != last_step:
            if step in armed:
                packets.append(("/beat", [("i", step)]))
                walk.clicks.append(clock.count())
            last_step = step
    packets.append(("/done", []))
    t = clock.count()
    p, st = walk.at(t)
    return packets, clock, walk, t, p, st


def main():
    exe = os.path.join(HERE, "..", "build", "phase_udp")
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    proc = subprocess.Popen([exe, str(port)], stdout=subprocess.PIPE, text=True)
    if proc.stdout.readline().strip() != "listening":
        print("  FAIL  phase_udp did not start")
        sys.exit(1)
    packets, clock, walk, t, p, st = stream()
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for address, args in packets:
        tx.sendto(R.encode(address, args), ("127.0.0.1", port))
        time.sleep(0.0005)
    got = proc.stdout.readline().split()
    proc.wait(timeout=5)
    want_phases = sum(1 for a, _ in packets if a == "/phase")
    want_beats = sum(1 for a, _ in packets if a == "/beat")
    fails = []
    vals = dict(zip(got[0::2], got[1::2]))
    if int(vals["phases"]) != want_phases or int(vals["beats"]) != want_beats:
        fails.append(f"heard {vals['phases']} phases and {vals['beats']} beats,"
                     f" sent {want_phases} and {want_beats}")
    if abs(float(vals["count"]) - t) > 1e-12:
        fails.append(f"count {vals['count']}, want {t!r}")
    if int(vals["steps"]) != st["k"]:
        fails.append(f"{vals['steps']} steps, want {st['k']}")
    at = [float(x) for x in got[got.index("at") + 1:]]
    if any(abs(a - b) > 1e-9 for a, b in zip(at, p)):
        fails.append(f"at {at}, want {p}")
    if fails:
        for f in fails:
            print("  FAIL ", f)
        sys.exit(1)
    print(f"  ok  a stand-in bridge over UDP: {want_phases} phases, {want_beats} beats,"
          f" {int(t)} bars counted, one late packet held, each beat a step")


if __name__ == "__main__":
    main()
