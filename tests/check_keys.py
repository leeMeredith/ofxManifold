#!/usr/bin/env python3
"""
ofxManifold -- every key in the editor's key panel reaches the app.

The panel (ofxManifoldEditorKeys.cpp) and the app that answers keys
(example-editor/src/ofApp.cpp) are written apart, and the editor scripts test
the model, not the app's key handling. So a key could be listed, lit, and do
nothing -- which happened: m and M (3A-6c) were in the panel and the model but
never wired to a key (found 3A-7). This checks, by reading both files, that
every keyboard entry in the panel is returned by ofApp::keyId() and answered
in ofApp::keyPressed(). Mouse entries -- "click ...", "drag ..." -- are
answered by the mouse handlers and are left out.

Standard library only.
"""

import re
import sys

KEYS = "ofxManifoldEditor/src/ofxManifoldEditorKeys.cpp"
APP = "example-editor/src/ofApp.cpp"


def main():
    keys = open(KEYS).read()
    app = open(APP).read()
    entries = re.findall(r'entry\("([a-z0-9]+)",\s*"([^"]*)"', keys)
    if not entries:
        print("  FAIL: no key entries found in " + KEYS)
        return 1
    start = app.index("std::string ofApp::keyId")
    end = app.index("\nvoid ofApp::", start)
    returned = set(re.findall(r'"([a-z0-9]+)"', app[start:end]))
    answered = set(re.findall(r'id == "([a-z0-9]+)"', app))
    fails = []
    for ident, label in entries:
        if "click" in label or "drag" in label:
            continue
        if ident not in returned:
            fails.append(f"'{ident}' ({label}) is in the panel but no key "
                         f"returns it in ofApp::keyId")
        if ident not in answered:
            fails.append(f"'{ident}' ({label}) is in the panel but "
                         f"ofApp::keyPressed never answers it")
    for f in fails:
        print("  FAIL: " + f)
    if fails:
        return 1
    n = sum(1 for _, l in entries if "click" not in l and "drag" not in l)
    print(f"  ok  every key in the panel reaches the app ({n} keys)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
