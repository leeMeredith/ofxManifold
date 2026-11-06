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
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_gates import Malformed, read_gates  # noqa: E402

PATH = ".github/workflows/kernel.yml"


def shard_fails(raw):
    """
    The mutation job must run every shard. A matrix of [0, 1, 2] with a run
    line dividing by 4 would quietly skip a quarter of the gates and stay
    green, so both are read and compared.
    """
    fails = []
    m = re.search(r"\n {8}shard: \[([0-9, ]*)\]\n", raw)
    runs = re.findall(r"tests/run_gates\.py --shard \$\{\{ matrix\.shard \}\}/(\d+)",
                      raw)
    if not m:
        fails.append("no 'shard: [...]' matrix for the mutation job")
        return fails
    if len(runs) != 1:
        fails.append(f"expected one run_gates.py --shard line, found {len(runs)}")
        return fails
    shards = [int(x) for x in m.group(1).replace(" ", "").split(",") if x]
    n = int(runs[0])
    if shards != list(range(n)):
        fails.append(f"the matrix runs shards {shards} but the run line divides "
                     f"by {n}; it must run 0 to {n - 1}, each once")
    if "tests/mutate.py" in raw.split("\njobs:\n", 1)[-1]:
        fails.append("a gate is still written into the workflow; gates belong "
                     "in tests/gates.txt")
    return fails


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

    fails += shard_fails(raw)

    # Every gate must name a file that exists, or the gate silently cannot run.
    try:
        gates = read_gates()
    except Malformed as e:
        fails.append(str(e))
        gates = []
    for g in gates:
        path = g["file"]
        if not os.path.exists(path):
            fails.append(f"gate {g['gate']!r} targets a file that does not "
                         f"exist: {path}")
            continue

        # The anchor must match EXACTLY ONCE, checked here rather than left for
        # CI. run_gates.py refuses a missing or ambiguous anchor at run time,
        # which is correct -- but it means a refactor that renames the code a
        # gate targets breaks CI rather than the local build.
        #
        # This recurred: once when trajectory serialization duplicated a guard
        # (D-012), and twice more when the regions work rewrote fill() and the
        # loader's error text. Each time the gate was fine and the code under
        # it had moved.
        n = open(path).read().count(g["find"])
        if n != 1:
            fails.append(f"gate {g['gate']!r} (tests/gates.txt line {g['line']}): "
                         f"anchor matches {n} times in {path}, must be exactly "
                         f"once: {g['find'][:60]}")
        elif g["find"] == g["replace"]:
            fails.append(f"gate {g['gate']!r} replaces its anchor with itself")

    if not gates:
        fails.append("no mutation gates found; the suite-can-fail job would "
                     "assert nothing")

    for f in fails:
        print("  FAIL:", f)
    if fails:
        return 1

    print("  ok  literal 'on:' trigger present")
    print(f"  ok  {len(gates)} mutation gates in tests/gates.txt, every anchor "
          f"matching exactly once, every shard run")
    return 0


if __name__ == "__main__":
    sys.exit(main())
