#!/usr/bin/env python3
"""Reference for example-osc's SuperCollider patch (round 3B-2b): what each
output sounds, from the map's shape, for each musical behaviour.

Writes tests/vectors/music.vec. tests/sc/check_music.scd runs the patch's own
functions -- the part between "// ---- music" and "// ---- end of music" in
example-osc/supercollider/manifold-receive.scd -- in sclang, without a
server, and checks them against these lines (make sc).

Worked out here another way than the patch:

  - \\registers' chord tones come from a written-out table of the stack,
    1 3 5 7 9 #11 13 and two octaves up, not from an index into seven;
  - \\progression's and \\somber's chords are spelled as scale degrees
    counted in thirds up the whole keyboard, as written, then folded down by
    octaves into the octave above the root -- not taken as pitch classes;
  - \\series is a list of harmonics of the root divided by the members, not
    a level plus a fraction.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vectors", "music.vec")

SPAN, QUIET = 12, 0.4
SCALE = [0, 2, 4, 7, 9]
STACK = [0, 4, 7, 11, 14, 18, 21]
DEGREES = [0, 3, 4, 5, 1, 2]              # I IV V vi ii iii
SOMBER = [0, 3, 5, 4, 2, 6]               # i iv VI v III VII
# The scales over many octaves, so a degree is an index into them.
MAJOR_KEYS = [12 * o + s for o in range(8) for s in (0, 2, 4, 5, 7, 9, 11)]
MINOR_KEYS = [12 * o + s for o in range(8) for s in (0, 2, 3, 5, 7, 8, 10)]


def one_register(written, at):
    """A chord note as written, brought down by octaves into the octave above
    the root; from the eighth note on, an octave higher."""
    while written >= 12:
        written -= 12
    return written + 12 * (at // 7)


def cps(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def lin(x, a, b, c, d):
    return c + (x - a) * (d - c) / (b - a)


def stack_tone(i):
    table = STACK + [t + 24 for t in STACK] + [t + 48 for t in STACK]
    return table[i]


def voicing(shape, lowest, count, music, fit):
    if shape is None:
        out = []
        for i in range(count):
            note = lowest + SCALE[i % 5] + 12 * (i // 5)
            pan = lin(i, 0, count - 1, -0.9, 0.9) if count > 1 else 0.0
            out.append((cps(note), pan, 1.0))
        return out
    heights, outs = shape
    n = len(outs)
    L = max(1, len(heights))
    own = {k: [i for i, o in enumerate(outs) if o[2] == 1 and o[1] == k]
           for k in range(L)}
    out = []
    for i, (name, k, levels, placed, x, y, z) in enumerate(outs):
        if placed:
            pan = lin(x, 0, 1, -0.9, 0.9)
        else:
            pan = lin(i, 0, n - 1, -0.9, 0.9) if n > 1 else 0.0
        if levels != 1 or not 0 <= k < L:
            out.append((cps(lowest), pan, QUIET))
            continue
        at = own[k].index(i)
        if music in ("progression", "somber"):
            degrees, keys = ((DEGREES, MAJOR_KEYS) if music == "progression"
                             else (SOMBER, MINOR_KEYS))
            written = keys[degrees[k % 6] + 2 * at]
            freq = cps(lowest + one_register(written, at))
        elif music == "series":
            m = len(own[k])
            harmonics = [(k + 1) * m + j for j in range(m)]   # of root / m
            freq = cps(lowest) / m * harmonics[at]
        else:
            root = lowest + (SPAN * k) // L if fit else lowest + 12 * k
            freq = cps(root + stack_tone(at))
        out.append((freq, pan, 1.0))
    return out


def brightness(heights, z, u):
    if u is not None and len(heights) > 1:
        return min(1.0, max(0.0, u / (len(heights) - 1)))
    if len(heights) > 1 and heights[-1] > heights[0]:
        return lin(z, heights[0], heights[-1], 0, 1)
    return 0.5


def corners(prefix_level, k, h):
    return [(f"{n}{prefix_level}", k, 1, True, x, y, h)
            for n, x, y in (("FL", 0.2, 0.8), ("FR", 0.8, 0.8),
                            ("RL", 0.2, 0.2), ("RR", 0.8, 0.2))]


SHAPES = {
    "room": ([0.0, 0.25, 0.5],
             corners("", 0, 0.0) + corners("-2", 1, 0.25) + corners("-3", 2, 0.5)),
    "five": ([0.0, 0.25, 0.5, 0.75, 1.0],
             [(f"o{k}{c}", k, 1, False, 0, 0, 0) for k in range(5) for c in "ab"]),
    "shared": ([0.0, 0.25],
               [("a", 0, 1, True, 0.1, 0.5, 0.0), ("b", 0, 2, True, 0.5, 0.5, 0.0),
                ("c", -1, 0, True, 0.9, 0.5, 0.0), ("d", 1, 1, False, 0, 0, 0),
                ("e", 0, 1, False, 0, 0, 0)]),
    "octahedron": ([-0.5, 0.5],
                   [(n, 0, 1, True, x, y, h) for n, x, y, h in
                    (("N", .5, .8, 0), ("E", .8, .5, 0), ("S", .5, .2, 0),
                     ("W", .2, .5, 0), ("C", .5, .5, -.5))]
                   + [(n + "-2", 1, 1, True, x, y, h) for n, x, y, h in
                      (("N", .5, .8, 0), ("E", .8, .5, 0), ("S", .5, .2, 0),
                       ("W", .2, .5, 0), ("C", .5, .5, .5))]),
    "nine_on_one": ([0.0], [(f"n{i}", 0, 1, False, 0, 0, 0) for i in range(9)]),
    "seven_levels": ([0.1 * k for k in range(7)],
                     [(f"L{k}", k, 1, False, 0, 0, 0) for k in range(7)]),
    "single": ([0.0], [("only", 0, 1, True, 0.5, 0.5, 0.0)]),
}


def shape_line(name):
    heights, outs = SHAPES[name]
    return ("SHAPE " + name + " " + " ".join(f"{h:.9g}" for h in heights) + " | "
            + ",".join(f"{n}:{k}:{lv}:{int(p)}:{x:.9g}:{y:.9g}:{z:.9g}"
                       for n, k, lv, p, x, y, z in outs))


def main():
    out = ["# music.vec -- example-osc's SuperCollider patch, round 3B-2b",
           "# generated by tests/ref/reference_music.py; checked in sclang by",
           "# tests/sc/check_music.scd (make sc)",
           "#",
           "# SHAPE name heights | name:level:levels:placed:x:y:z,...",
           "# VOICES name shape|- count music fit lowest EXPECT freq pan gain ; ...",
           "# BRIGHT name shape|- z u|- EXPECT b",
           ""]
    for name in SHAPES:
        out.append(shape_line(name))
    out.append("")
    n = 0
    cases = []
    for shape in SHAPES:
        for music, fit in (("progression", 1), ("somber", 1), ("registers", 1),
                           ("registers", 0), ("series", 1)):
            cases.append((f"{shape}_{music}{'' if fit else '_spread'}", shape,
                          len(SHAPES[shape][1]), music, fit, 48))
    cases += [("room_second_map", "room", 12, "progression", 1, 55),
              ("room_second_map_registers", "room", 12, "registers", 1, 55),
              ("climb_five", None, 5, "registers", 1, 48),
              ("climb_twelve", None, 12, "series", 1, 55),
              ("climb_one", None, 1, "registers", 1, 48)]
    for name, shape, count, music, fit, lowest in cases:
        vs = voicing(SHAPES[shape] if shape else None, lowest, count, music, fit)
        out.append(f"VOICES {name} {shape or '-'} {count} {music} {fit} {lowest} "
                   "EXPECT " + " ; ".join(f"{f:.9g} {p:.9g} {g:.9g}"
                                         for f, p, g in vs))
        n += 1
    out.append("")
    for name, shape, z, u in (("bright_by_u", "room", 0.25, 1.0),
                              ("bright_by_u_past", "room", 0.6, 2.4),
                              ("bright_by_z", "room", 0.125, None),
                              ("bright_by_z_octa", "octahedron", -0.25, None),
                              ("bright_one_level", "single", 0.3, 0.0),
                              ("bright_no_shape", None, 0.3, 0.7)):
        heights = SHAPES[shape][0] if shape else []
        b = brightness(heights, z, u)
        out.append(f"BRIGHT {name} {shape or '-'} {z:.9g} "
                   f"{'-' if u is None else f'{u:.9g}'} EXPECT {b:.9g}")
        n += 1
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"music.vec: {n} vectors")


if __name__ == "__main__":
    main()
