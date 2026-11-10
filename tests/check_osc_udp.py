#!/usr/bin/env python3
"""
ofxManifoldPlayer -- the OSC sender over a real socket (round 3B-1).

Starts build/osc_udp, which sends a fixed run of frames as UDP packets to a
port listened on here. Every packet must be exactly what the reference sender
(tests/ref/reference_osc.py) sends for the same run, decoded by the reference
decoder -- and by python-osc too, where it is installed. Then a hello goes
back, and the sender must count the receiver connected, by the name given.

    make osc-live      (part of make test)
"""

import os
import socket
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "ref"))
import reference_osc as R  # noqa: E402

BIN = os.path.join(HERE, "..", "build", "osc_udp")


def expected(listen_port):
    s = R.Sender(per_output=True, map_name="room", answer_port=listen_port)
    msgs = []
    for k in range(10):
        now = k / 60.0
        # Sixteenths: exact in float, so the C++ gets the same numbers
        # whether or not its compiler fuses 1 - step into one rounding.
        step = k / 16.0
        outs = [("front", 0, 1.0 - step), ("left", 1, step)]
        if k >= 4:
            outs.append(("rear", 3, 0.0))
        msgs += s.frame(now, outs, [("brightness", 0.5)],
                        (step, 0.5, 0.0))
    return [(a, [(t, R.f32(v) if t == "f" else v) for t, v in args]) for a, args in msgs]


def main():
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.bind(("127.0.0.1", 0))
    rx.settimeout(3.0)
    send_port = rx.getsockname()[1]
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    listen_port = probe.getsockname()[1]
    probe.close()
    want = expected(listen_port)

    proc = subprocess.Popen([BIN, str(send_port), str(listen_port)],
                            stdout=subprocess.PIPE, text=True)
    got = []
    fails = []
    try:
        for _ in range(len(want)):
            data, _ = rx.recvfrom(2048)
            d = R.decode(data)
            if d is None:
                fails.append(f"undecodable packet {data.hex()}")
                continue
            if R.HAVE_PYTHONOSC:
                m = R.PyOscMessage(data)
                if m.address != d[0] or list(m.params) != [v for _, v in d[1]]:
                    fails.append(f"python-osc reads {m.address} {m.params}")
            got.append(d)
    except socket.timeout:
        fails.append(f"timed out after {len(got)} of {len(want)} packets")
    first = proc.stdout.readline().strip()
    if first != f"sent {len(want)}":
        fails.append(f"sender says '{first}', expected 'sent {len(want)}'")
    for k, (g, w) in enumerate(zip(got, want)):
        if g != w:
            fails.append(f"packet {k}: {g} -- want {w}")
            break
    hello = R.encode("/manifold/room/hello", [("s", "python")])
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(hello, ("127.0.0.1", listen_port))
    last = proc.stdout.readline().strip()
    proc.wait(timeout=5)
    if last != "connected python":
        fails.append(f"after a hello the sender says '{last}'")
    if fails:
        for f in fails:
            print("  FAIL ", f)
        sys.exit(1)
    print(f"  ok  {len(want)} packets over UDP, each exactly the reference's"
          + (", python-osc agreeing" if R.HAVE_PYTHONOSC else "")
          + "; a hello back counted")


if __name__ == "__main__":
    main()
