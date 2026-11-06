#!/usr/bin/env python3
"""
ofxManifold — run the mutation gates listed in tests/gates.txt.

Each gate breaks one file on purpose, runs only the suites that compile that
file (tests/gate.py), and expects them to go red. A gate the suites do not
catch is a hole in the tests, and is reported.

    python3 tests/run_gates.py                  every gate, one after another
    python3 tests/run_gates.py --shard 2/6      every sixth gate, from the third
    python3 tests/run_gates.py --only trim      gates whose name contains "trim"
    python3 tests/run_gates.py --list           names only; nothing is run

Shards are dealt round-robin so neighbouring gates -- which tend to break the
same file and cost the same -- spread across the shards instead of piling into
one. CI runs every shard as its own job, side by side.

The file is restored to its exact bytes after every gate, caught or not, even
if this script is interrupted. Exit status is 0 only if every gate run was
caught; 2 if the list itself is malformed.

Standard library only.
"""

import os
import subprocess
import sys
import time

LIST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gates.txt")
KEYS = ("gate", "file", "find", "replace", "nl")


class Malformed(Exception):
    pass


def read_gates(path=LIST):
    """
    The gates, in file order, each a dict with gate/file/find/replace/nl and
    the line it starts on. Values are everything after "key: ", spaces kept.
    """
    gates = []
    cur = None
    with open(path) as f:
        lines = f.read().split("\n")
    for n, line in enumerate(lines, 1):
        if line.startswith("#"):
            continue
        if line.strip() == "":
            if cur is not None:
                gates.append(cur)
                cur = None
            continue
        key, sep, rest = line.partition(":")
        if not sep or key not in KEYS:
            raise Malformed(f"gates.txt line {n}: expected one of "
                            f"{', '.join(k + ':' for k in KEYS)} -- got {line[:60]!r}")
        if cur is None:
            if key != "gate":
                raise Malformed(f"gates.txt line {n}: a gate starts with "
                                f"'gate:', not '{key}:'")
            cur = {"line": n, "nl": False, "seen": set()}
        if key in cur["seen"]:
            raise Malformed(f"gates.txt line {n}: '{key}:' twice in one gate")
        cur["seen"].add(key)
        value = rest[1:] if rest.startswith(" ") else rest
        if key == "nl":
            if value.strip() not in ("yes", "no"):
                raise Malformed(f"gates.txt line {n}: nl is yes or no")
            cur["nl"] = value.strip() == "yes"
        else:
            cur[key] = value
    if cur is not None:
        gates.append(cur)

    names = set()
    for g in gates:
        for k in ("gate", "file", "find", "replace"):
            if k not in g["seen"]:
                raise Malformed(f"gates.txt line {g['line']}: gate "
                                f"{g.get('gate', '?')!r} has no '{k}:'")
        if g["gate"] in names:
            raise Malformed(f"gates.txt line {g['line']}: two gates named "
                            f"{g['gate']!r}")
        names.add(g["gate"])
        if g["nl"]:
            g["find"] = g["find"].replace("\\n", "\n")
            g["replace"] = g["replace"].replace("\\n", "\n")
        del g["seen"]
    return gates


def parse_shard(text):
    try:
        i, n = (int(x) for x in text.split("/"))
    except ValueError:
        raise Malformed(f"--shard takes i/N, got {text!r}")
    if n < 1 or not 0 <= i < n:
        raise Malformed(f"--shard {text}: need 0 <= i < N")
    return i, n


def run_gate(g):
    """(caught, detail). The file is back to its bytes when this returns."""
    path = g["file"]
    with open(path, "rb") as f:
        original = f.read()
    text = original.decode()
    count = text.count(g["find"])
    if count != 1:
        return False, f"anchor matches {count} times in {path}, must be once"
    try:
        with open(path, "w") as f:
            f.write(text.replace(g["find"], g["replace"], 1))
        p = subprocess.run([sys.executable, "tests/gate.py", path],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)
        if p.returncode == 0:
            return False, "the suites stayed green:\n" + p.stdout[-1500:]
        return True, ""
    finally:
        with open(path, "wb") as f:
            f.write(original)


def main():
    args = sys.argv[1:]
    shard = None
    only = None
    listing = False
    try:
        while args:
            a = args.pop(0)
            if a == "--shard" and args:
                shard = parse_shard(args.pop(0))
            elif a == "--only" and args:
                only = args.pop(0)
            elif a == "--list":
                listing = True
            else:
                print("usage: run_gates.py [--shard i/N] [--only TEXT] [--list]",
                      file=sys.stderr)
                return 2
        gates = read_gates()
    except Malformed as e:
        print("run_gates:", e, file=sys.stderr)
        return 2

    chosen = list(enumerate(gates))
    if shard:
        i, n = shard
        chosen = [(k, g) for k, g in chosen if k % n == i]
    if only:
        chosen = [(k, g) for k, g in chosen if only.lower() in g["gate"].lower()]

    if listing:
        for k, g in chosen:
            print(f"{k:4d}  {g['gate']}  ({g['file']})")
        return 0

    where = f"shard {shard[0]}/{shard[1]}, " if shard else ""
    print(f"{len(chosen)} of {len(gates)} gates ({where}from tests/gates.txt)")
    start = time.time()
    missed = []
    for done, (k, g) in enumerate(chosen, 1):
        t = time.time()
        caught, detail = run_gate(g)
        mark = "caught" if caught else "NOT CAUGHT"
        print(f"  {mark:10s} {time.time() - t:5.0f} s  [{k}] {g['gate']}",
              flush=True)
        if not caught:
            missed.append((k, g, detail))
            # Inside GitHub Actions this becomes an annotation on the run.
            if os.environ.get("GITHUB_ACTIONS"):
                print(f"::error file=tests/gates.txt,line={g['line']}::"
                      f"not caught: {g['gate']}")

    print(f"\n{len(chosen) - len(missed)}/{len(chosen)} caught "
          f"in {time.time() - start:.0f} s")
    for k, g, detail in missed:
        print(f"\nNOT CAUGHT [{k}] {g['gate']}  (tests/gates.txt line "
              f"{g['line']}, {g['file']})")
        print("  " + detail.replace("\n", "\n  "))
    return 1 if missed else 0


if __name__ == "__main__":
    sys.exit(main())
