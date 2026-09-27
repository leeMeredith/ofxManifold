#!/usr/bin/env python3
"""
The editor's one door (PLAN-editor.md decision D): nothing outside
editor::Model may change the map or its outputs.

Enforced by the compiler -- the model's state is private, reachable only
through const accessors -- and checked here by snippets that must FAIL to
compile. The door was open before this existed: with the model's members
public, an app could write model.manifold.addNode() and skip every rule an
operation applies. It compiled.

A CONTROL snippet must COMPILE. Without it, a broken include path would make
every snippet fail, and this check would pass while proving nothing. The first
version of this check, written as a Makefile rule, did exactly that: a quoting
mistake made every snippet fail for a reason that had nothing to do with the
door.
"""

import os
import shlex
import subprocess
import sys
import tempfile

CXX = os.environ.get("CXX", "g++")
FLAGS = shlex.split(os.environ.get("CXXFLAGS", "-std=c++17 -Ilibs"))
INC = ["-Isrc", "-IofxManifoldEditor/src"]

CODE = ('#include "ofxManifoldEditorModel.h"\n'
        'using namespace ofxManifold;\n'
        'int main() {{ editor::Model m; {} return 0; }}\n')

CONTROL = "m.place({0.5f, 0.5f}, Grid::free()); (void)m.manifold().nodeCount();"

MUST_FAIL = [
    'm.manifold().addNode("x", {0.5f, 0.5f});',   # through the accessor
    "m.mapping().bind(0, 0);",
    'm.manifold_.addNode("x", {0.5f, 0.5f});',     # around it
    "m.mapping_.clearBindings(0);",
    "m.currentOutput_ = 0;",
    "m.autoOutput_ = false;",
]


def compiles(body):
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
        f.write(CODE.format(body))
        path = f.name
    try:
        r = subprocess.run([CXX, *FLAGS, *INC, "-fsyntax-only", path],
                           capture_output=True, text=True)
        return r.returncode == 0, r.stderr
    finally:
        os.unlink(path)


def main():
    ok, err = compiles(CONTROL)
    if not ok:
        print("  door check: the CONTROL snippet does not compile, so the")
        print("  snippets below would fail for the wrong reason:")
        print("   ", err.strip().splitlines()[0] if err.strip() else "")
        return 1
    open_ = [w for w in MUST_FAIL if compiles(w)[0]]
    for w in open_:
        print(f"  door open: {w} compiles")
    if open_:
        return 1
    print("  ok  the model changes only through its own operations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
