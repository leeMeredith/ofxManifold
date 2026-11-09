#!/usr/bin/env python3
"""
ofxManifold -- example-osc's receiver in plain Python (round 3B-1).

No dependencies. Prints the outputs as example-osc describes them and a bar
for each level, and says hello once a second so the app shows it answered.

    python3 receive.py              listen on 57120, answer to 57121
    python3 receive.py 9000 9001    other ports (set the same in the app)

Stop sclang first if it is running: only one program can listen on a port.
"""

import socket
import struct
import sys
import time


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
    answer = int(sys.argv[2]) if len(sys.argv) > 2 else 57121
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", listen))
    sock.settimeout(0.1)
    app = ("127.0.0.1", answer)
    sock.sendto(encode("/manifold/describe"), app)
    names, last_hello, last_print = [], 0.0, 0.0
    print(f"listening on {listen}, answering to {answer} -- ctrl-c stops")
    while True:
        now = time.time()
        if now - last_hello >= 1.0:
            sock.sendto(encode("/manifold/hello", "python"), app)
            last_hello = now
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            continue
        address, args = decode(data)
        if address.endswith("/outputs"):
            n = args[0]
            new = [args[1 + 2 * i] for i in range(n)]
            if new != names:
                names = new
                print(f"\n{n} outputs: {', '.join(names)}")
        elif address.endswith("/levels") and now - last_print > 0.1:
            last_print = now
            bars = "  ".join(f"{nm[:6]:>6} {'#' * int(round(v * 20)):<20}"
                             for nm, v in zip(names, args))
            print("\r" + bars, end="", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print()
