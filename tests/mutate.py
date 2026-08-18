#!/usr/bin/env python3
"""
ofxManifold — mutation helper for CI gates.

Applies a LITERAL string replacement to a source file. Not sed, deliberately.

Mutation patterns are C++ fragments, and C++ is full of characters that sed
treats as syntax: `||` collides with the `s|...|...|` delimiter, `[` and `]` are
bracket expressions, `*` and `.` are metacharacters, and `&` in the replacement
means "the whole match". Every one of those has to be escaped correctly for a
pattern that is only ever meant to match itself, exactly once, as plain text.

Two failures in this project came from that escaping rather than from the code
under test. This takes both strings verbatim and does nothing clever with them.

Exit codes are distinct on purpose:

    0   the mutation was applied
    2   the anchor was not found

A sed that fails to match exits 0 and changes nothing, which makes "the mutation
did not apply" indistinguishable from "the mutation applied and the suite caught
it" until someone reads the diff. Exit 2 makes it loud.
"""

import sys


def main():
    args = sys.argv[1:]

    # A multi-line anchor cannot survive the shell: YAML and bash both pass
    # \n through as a literal backslash followed by n. --nl says to convert
    # those two characters into a real newline, in both the pattern and the
    # replacement.
    #
    # It is a flag rather than automatic because a C++ fragment may legitimately
    # contain a backslash-n -- a string literal with an escape in it, for
    # instance -- and silently rewriting those would corrupt the anchor in a way
    # that reads as "anchor not found" and sends the reader looking in the wrong
    # place entirely.
    newline = False
    if args and args[0] == "--nl":
        newline = True
        args = args[1:]

    if len(args) != 3:
        print("usage: mutate.py [--nl] <file> <find> <replace>",
              file=sys.stderr)
        return 2

    path, find, replace = args
    if newline:
        find = find.replace("\\n", "\n")
        replace = replace.replace("\\n", "\n")

    try:
        with open(path) as f:
            text = f.read()
    except OSError as e:
        print(f"mutate: cannot read {path}: {e}", file=sys.stderr)
        return 2

    count = text.count(find)
    if count == 0:
        print(f"mutate: anchor not found in {path}", file=sys.stderr)
        print(f"  looking for: {find[:120]}", file=sys.stderr)
        return 2
    if count > 1:
        # An ambiguous anchor mutates the first occurrence and leaves the rest,
        # which is a weaker mutation than intended and reports as if it were
        # the full one.
        print(f"mutate: anchor appears {count} times in {path}; "
              f"it must be unique", file=sys.stderr)
        return 2

    with open(path, "w") as f:
        f.write(text.replace(find, replace, 1))
    print(f"mutate: applied to {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
