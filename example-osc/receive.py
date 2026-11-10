#!/usr/bin/env python3
"""
ofxManifold -- example-osc's receiver in plain Python (round 3B-1, protocol
2: several maps at once).

No dependencies. Listens for the roll call, /manifold/map, and follows every
map it hears: prints each map's outputs as it describes them, and its levels
as numbers -- a line per map when they change, at most twice a second, so it
is quiet while nothing moves. Says hello to each map once a second, at the
port its roll call gives, so example-osc shows it answered; asks every map to
describe itself when it starts; lets a map go when its roll call has been
silent for five seconds.

    python3 receive.py              listen on 57120
    python3 receive.py 9000         another port (set the same in the app)

Stop sclang first if it is running: only one program can listen on a port.
"""

import socket
import struct
import sys
import time

ROOT = "/manifold"
FIRST_PORT, PORTS = 57121, 16      # where copies of example-osc may hear answers
SILENT_AFTER = 5.0


def read_string(data, at):
    end = data.index(b"\0", at)
    s = data[at:end].decode("utf-8")
    return s, end + 1 + (4 - (end + 1) % 4) % 4


def decode(data):
    """(address, args) for i, f and s arguments."""
    address, at = read_string(data, 0)
    tags, at = read_string(data, at)
    args = []
    for t in tags[1:]:
        if t in "if":
            args.append(struct.unpack(">i" if t == "i" else ">f", data[at:at + 4])[0])
            at += 4
        elif t == "s":
            s, at = read_string(data, at)
            args.append(s)
    return address, args


def encode(address, *strings):
    def pad(s):
        b = s.encode() + b"\0"
        return b + b"\0" * ((4 - len(b) % 4) % 4)
    return pad(address) + pad("," + "s" * len(strings)) + b"".join(pad(s) for s in strings)


def main():
    listen = int(sys.argv[1]) if len(sys.argv) > 1 else 57120
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", listen))
    sock.settimeout(0.1)
    for k in range(PORTS):
        sock.sendto(encode(ROOT + "/describe"), ("127.0.0.1", FIRST_PORT + k))
    maps = {}            # prefix -> dict(port, names, seen, shown, printed)
    last_hello = 0.0
    print(f"listening on {listen} for {ROOT}/map -- ctrl-c stops")
    while True:
        now = time.time()
        if now - last_hello >= 1.0:
            last_hello = now
            for prefix in list(maps):
                m = maps[prefix]
                if now - m["seen"] > SILENT_AFTER:
                    print(f"{prefix} has gone")
                    del maps[prefix]
                elif m["port"]:
                    sock.sendto(encode(prefix + "/hello", "python"), ("127.0.0.1", m["port"]))
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        address, args = decode(data)
        if address == ROOT + "/map":
            prefix, port = args[0], args[1]
            m = maps.get(prefix)
            if m is None:
                maps[prefix] = m = dict(port=port, names=[], seen=now, shown=None,
                                        printed=0.0, warned=False)
                print(f"{prefix} playing, answers on port {port}")
            elif m["port"] != port and not m["warned"]:
                print(f"two maps called {prefix} (answering on {m['port']} and {port})"
                      " -- give one another name")
                m["warned"] = True
            m["seen"] = now
            continue
        prefix, _, what = address.rpartition("/")
        m = maps.get(prefix)
        if m is None:
            continue
        if what == "outputs":
            n = args[0]
            new = [args[1 + 2 * i] for i in range(n)]
            if new != m["names"]:
                m["names"] = new
                print(f"{prefix}: {n} outputs: {', '.join(new)}")
        elif what == "levels" and now - m["printed"] >= 0.5:
            shown = [round(v, 2) for v in args]
            if shown != m["shown"]:
                m["shown"], m["printed"] = shown, now
                print(f"{prefix}: " + "  ".join(f"{nm} {v:.2f}"
                                               for nm, v in zip(m["names"], shown)))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
