#!/usr/bin/env python3
"""
ofxManifold — workflow sanity check.

Exists because of a specific trap. YAML 1.1 reads the bare word `on` as the
boolean true. GitHub Actions parses with YAML 1.2, where it stays a string, so
a hand-written workflow is fine -- but PyYAML is a 1.1 parser, and loading the
file and dumping it back rewrites the trigger key as `true:`. GitHub then
rejects the whole workflow with "Unexpected value 'true'".

Note that PyYAML reports `True` as a key even for a PERFECTLY VALID file, so
the parsed structure cannot answer the question. Only the raw text can. That is
the trap inside the trap: the obvious check reads the wrong thing and fails a
good file.

Run: python3 tests/check_workflow.py
"""

import sys

import yaml

PATH = ".github/workflows/kernel.yml"


def main():
    raw = open(PATH).read()
    fails = []

    # The only reliable test: the literal characters, at column zero.
    if "\non:\n" not in raw:
        fails.append("no literal top-level 'on:' key -- a YAML round trip "
                     "probably rewrote it as 'true:'")
    if "\ntrue:\n" in raw:
        fails.append("a literal 'true:' key is present; GitHub will reject "
                     "the workflow")

    try:
        doc = yaml.safe_load(raw)
    except yaml.YAMLError as e:
        print(f"  FAIL: not valid YAML: {e}")
        return 1

    if "jobs" not in doc:
        fails.append("no jobs")
    else:
        for job, spec in doc["jobs"].items():
            if not spec.get("steps"):
                fails.append(f"job {job} has no steps")

    # Every mutation gate must name a file that exists, or the gate cannot run.
    import os
    import shlex
    for job, spec in doc.get("jobs", {}).items():
        for step in spec.get("steps", []):
            run = step.get("run", "")
            if "tests/mutate.py" not in run:
                continue
            line = run.strip().split("\n")[0]
            cont = []
            for ln in run.strip().split("\n"):
                cont.append(ln.rstrip("\\").strip())
                if not ln.rstrip().endswith("\\"):
                    break
            args = shlex.split(" ".join(cont))
            path = args[3] if "--nl" in args else args[2]
            if not os.path.exists(path):
                fails.append(f"gate '{step.get('name')}' targets a missing "
                             f"file: {path}")

    for f in fails:
        print("  FAIL:", f)
    if fails:
        return 1

    print("  ok  literal 'on:' trigger present")
    print("  ok  jobs:", ", ".join(doc["jobs"]))
    print("  ok  steps:", {k: len(v["steps"]) for k, v in doc["jobs"].items()})
    gates = sum(1 for j in doc["jobs"].values() for s in j["steps"]
                if "tests/mutate.py" in s.get("run", ""))
    print(f"  ok  {gates} mutation gates, all targeting files that exist")
    return 0


if __name__ == "__main__":
    sys.exit(main())
