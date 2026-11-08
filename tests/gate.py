#!/usr/bin/env python3
"""
Run only the suites a mutated file can reach (CI mutation gates).

    python3 tests/gate.py src/mapping/ofxManifoldRig.h

Exits 0 if every reached suite passes -- which, after a mutation, means the
mutation was NOT caught -- and non-zero if any fails. A gate reads:

    python3 tests/mutate.py FILE 'find' 'replace'
    if python3 tests/gate.py FILE; then echo "::error::not caught"; exit 1; fi
    git checkout -- FILE

Why: every gate used to `make clean` and run all of `make test`. With 250
gates that was over an hour a push, most of it rebuilding suites that never
include the mutated file. A suite that cannot see a file cannot catch a
mutation in it, so skipping it gives up nothing.

Which suites reach a file is ASKED of the compiler (`-MM`), not written down
here, so a new #include is followed without anyone remembering to update a
table. The binaries of the reached suites are deleted before building, so a
dependency missing from the Makefile cannot leave a stale binary testing the
unmutated code -- that happened once (round 3A-4c), locally, and made fifteen
gates look missed.

Fails safe in every direction:
  - a file no suite reaches runs ALL of `make test`, not nothing;
  - a `test-*` target in the Makefile missing from SUITES below stops with an
    error rather than being silently skipped;
  - a file under ofxManifoldEditor/ also runs the door check.
"""

import glob
import os
import re
import subprocess
import sys

# target -> (binary under build/, sources compiled into it)
EDITOR_CPP = sorted(glob.glob("ofxManifoldEditor/src/*.cpp"))
SUITES = {
    "test-triangle":       ("run_vectors",        ["tests/run_vectors.cpp"]),
    "test-manifold":       ("run_manifold",       ["tests/run_manifold.cpp"]),
    "test-interpretation": ("run_interpretation", ["tests/run_interpretation.cpp"]),
    "test-mapping":        ("run_mapping",        ["tests/run_mapping.cpp"]),
    "test-serialize":      ("run_serialize",      ["tests/run_serialize.cpp"]),
    "test-trajectory":     ("run_trajectory",     ["tests/run_trajectory.cpp"]),
    "test-smoother":       ("run_smoother",       ["tests/run_smoother.cpp"]),
    "test-regions":        ("run_regions",        ["tests/run_regions.cpp"]),
    "test-grids":          ("run_grids",          ["tests/run_grids.cpp"]),
    "test-outputs":        ("run_outputs",        ["tests/run_outputs.cpp"]),
    "test-editor":         ("run_editor",         ["tests/run_editor.cpp"] + EDITOR_CPP),
    "test-levels":         ("run_levels",         ["tests/run_levels.cpp"]),
    # the editor window's layout (3A-10), not a vector suite
    "panel":               ("check_panel",        ["tests/check_panel.cpp"] + EDITOR_CPP),
}
INCLUDES = ["-Ilibs", "-Isrc", "-IofxManifoldEditor/src"]


def makefile_suites():
    with open("Makefile") as f:
        text = f.read()
    m = re.search(r"^test:(.*?)\n\t", text, re.S | re.M)
    return set(re.findall(r"test-[a-z]+", m.group(1)))


def reached_by(source):
    """Every file one source compiles, by asking the compiler."""
    cxx = os.environ.get("CXX", "c++")
    out = subprocess.run([cxx, "-std=c++17", *INCLUDES, "-MM", source],
                         capture_output=True, text=True, check=True).stdout
    deps = out.replace("\\\n", " ").split(":", 1)[1].split()
    return {os.path.normpath(d) for d in deps}


def main():
    if len(sys.argv) != 2:
        print("usage: gate.py FILE", file=sys.stderr)
        return 2
    target = os.path.normpath(sys.argv[1])

    unknown = makefile_suites() - set(SUITES)
    if unknown:
        print("gate.py: the Makefile runs suites this does not know: "
              + ", ".join(sorted(unknown)), file=sys.stderr)
        return 2

    chosen = []
    for name, (binary, sources) in SUITES.items():
        files = set()
        for src in sources:
            files |= reached_by(src)
            files.add(os.path.normpath(src))
        if target in files:
            chosen.append((name, binary))

    if not chosen:
        print(f"gate.py: no suite reaches {target}; running all of make test")
        return subprocess.run(["make", "--no-print-directory", "test"]).returncode

    for _name, binary in chosen:
        path = os.path.join("build", binary)
        if os.path.exists(path):
            os.remove(path)
    targets = [name for name, _b in chosen]
    if target.startswith("ofxManifoldEditor" + os.sep):
        targets.append("door")
    print("gate.py: " + target + " -> " + " ".join(targets))
    return subprocess.run(["make", "--no-print-directory", *targets]).returncode


if __name__ == "__main__":
    sys.exit(main())
