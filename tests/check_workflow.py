#!/usr/bin/env python3
"""
ofxManifold — workflow sanity check. Standard library only.

Exists because of a specific trap. YAML 1.1 reads the bare word `on` as the
boolean true. GitHub Actions parses with YAML 1.2, where it stays a string, so a
hand-written `on:` trigger is correct -- but PyYAML is a 1.1 parser, and loading
this file and dumping it back rewrites the key as `true:`. GitHub then rejects
the entire workflow with "Unexpected value 'true'", with every job and step
intact and the file useless.

This check reads the RAW TEXT and imports no YAML library, for two reasons.

First, practical: PyYAML is not installed on the GitHub macOS runner, and this
check failed there for that reason alone.

Second, and more to the point: a parser is the wrong instrument here. PyYAML
reports `True` as a key for a PERFECTLY VALID workflow, because that is what a
1.1 parser does with `on`. Asking the parsed structure whether the trigger
survived answers the same either way. The first version of this file made
exactly that mistake and failed a good workflow.

Run: python3 tests/check_workflow.py
"""

import os
import shlex
import sys

PATH = ".github/workflows/kernel.yml"


def gate_commands(raw):
    """
    Yield the argv of every `python3 tests/mutate.py ...` invocation.

    Line continuations are joined, since the gates are written across three
    lines for readability.
    """
    lines = raw.split("\n")
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("python3 tests/mutate.py"):
            parts = []
            while i < len(lines):
                cur = lines[i].rstrip()
                parts.append(cur.rstrip("\\").strip())
                if not cur.endswith("\\"):
                    break
                i += 1
            try:
                yield shlex.split(" ".join(parts))
            except ValueError as e:
                yield ["<unparseable>", str(e)]
        i += 1


def main():
    if not os.path.exists(PATH):
        print(f"  FAIL: {PATH} does not exist")
        return 1

    raw = open(PATH).read()
    fails = []

    # The only reliable trigger test: the literal characters at column zero.
    if "\non:\n" not in raw:
        fails.append("no literal top-level 'on:' key -- a YAML round trip has "
                     "probably rewritten it as 'true:'")
    if "\ntrue:\n" in raw:
        fails.append("a literal 'true:' key is present; GitHub will reject the "
                     "workflow outright")

    for key in ("name:", "jobs:"):
        if f"\n{key}" not in raw:
            fails.append(f"no top-level '{key}'")

    # Tabs are not legal YAML indentation and the error GitHub gives is opaque.
    for n, line in enumerate(raw.split("\n"), 1):
        if line.startswith("\t") or (line.strip() and "\t" in line[:len(line) - len(line.lstrip())]):
            fails.append(f"tab used for indentation on line {n}")

    # Every gate must name a file that exists, or the gate silently cannot run.
    gates = 0
    for argv in gate_commands(raw):
        if argv and argv[0] == "<unparseable>":
            fails.append(f"a mutate.py invocation will not tokenize: {argv[1]}")
            continue
        gates += 1
        try:
            path = argv[3] if "--nl" in argv else argv[2]
        except IndexError:
            fails.append(f"a mutate.py invocation has too few arguments: "
                         f"{' '.join(argv)[:80]}")
            continue
        if not os.path.exists(path):
            fails.append(f"gate targets a file that does not exist: {path}")

    if gates == 0:
        fails.append("no mutation gates found; the suite-can-fail job would "
                     "assert nothing")

    for f in fails:
        print("  FAIL:", f)
    if fails:
        return 1

    print("  ok  literal 'on:' trigger present")
    print(f"  ok  {gates} mutation gates, all targeting files that exist")
    return 0


if __name__ == "__main__":
    sys.exit(main())
