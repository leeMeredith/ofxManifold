#!/usr/bin/env python3
"""
ofxManifold -- reference for the 3D map's geometry (round 3A-4).

A 3D map is one floor plan seen at several levels. This writes the vectors
for src/core/ofxManifoldLevels.h: making levels by copying, bending them,
refusing anything that would make levels cross or touch, and weighting a
point (x, y, z) over (node, level).

Formulated differently from the C++ on purpose:
  - Levels are held here as a full table of heights, every node on every
    level, rebuilt whole after each operation. The C++ keeps only heights
    that were set and falls back to the level's start.
  - A point is placed by scanning the PRISM CELLS from the top down -- each
    pair of neighbouring levels is one cell -- and taking the first whose
    vertical span at (x, y) holds z. The C++ walks up from the bottom.
  - Weights inside a cell come from the distance to the UPPER level,
    (H_upper - z) / (H_upper - H_lower), not from the lower.

The property that makes it a 3D map is checked in the runner, not assumed:
the weights reproduce the point. Sum over (node, level) of weight times
(x_i, y_i, height) is (x, y, z).

Writes tests/vectors/levels.vec.
"""

import os
import sys

sys.path.insert(0, "tests/ref")
from reference_manifold import barycentric, fmt  # noqa: E402

OUT = "tests/vectors/levels.vec"


# ---------------------------------------------------------------------------
# the model
# ---------------------------------------------------------------------------

class Stack:
    """bases[k], and H[k][i]: node i's height on level k, for every node."""

    def __init__(self, node_count):
        self.n = node_count
        self.bases = [0.0]
        self.H = [[0.0] * node_count]

    def ordered(self, H):
        return all(H[k][i] < H[k + 1][i]
                   for k in range(len(H) - 1) for i in range(self.n))

    def copy(self, frm, new_base):
        if new_base in self.bases:
            return None, []
        shift = new_base - self.bases[frm]
        row = [h + shift for h in self.H[frm]]
        at = sum(1 for b in self.bases if b < new_base)
        H = self.H[:at] + [row] + self.H[at:]
        bad = [i for i in range(self.n)
               if (at > 0 and not row[i] > H[at - 1][i])
               or (at + 1 < len(H) and not row[i] < H[at + 1][i])]
        if bad:
            return None, bad
        self.H = H
        self.bases = self.bases[:at] + [new_base] + self.bases[at:]
        return at, []

    def raise_(self, k, nodes, dz):
        H = [list(r) for r in self.H]
        for i in nodes:
            H[k][i] += dz
        bad = [i for i in nodes
               if (k > 0 and not H[k][i] > H[k - 1][i])
               or (k + 1 < len(H) and not H[k][i] < H[k + 1][i])]
        if bad:
            return bad
        self.H = H
        return []

    def set(self, k, i, h):
        H = [list(r) for r in self.H]
        H[k][i] = h
        if not self.ordered(H):
            return False
        self.H = H
        return True

    def remove(self, k):
        if len(self.H) <= 1:
            return False
        del self.H[k]
        del self.bases[k]
        return True

    def remap(self, table):
        """table[old] = new id, or -1 if removed."""
        n = sum(1 for t in table if t >= 0)
        H = []
        for row in self.H:
            r = [0.0] * n
            for old, new in enumerate(table):
                if new >= 0:
                    r[new] = row[old]
            H.append(r)
        self.H = H
        self.n = n

    def add_node(self):
        """A node with no height of its own: each level's start."""
        for k, row in enumerate(self.H):
            row.append(self.bases[k])
        self.n += 1

    def weigh(self, lam, z):
        """lam: {node: weight} from the floor plan. Returns None outside, or
        {(node, level): weight}."""
        L = len(self.H)
        if L == 1:
            return {(i, 0): w for i, w in lam.items() if w != 0.0}
        Hxy = [sum(w * self.H[k][i] for i, w in lam.items())
               for k in range(L)]
        eps = 1e-6
        if z < Hxy[0] - eps or z > Hxy[-1] + eps:
            return None
        for k in range(L - 2, -1, -1):          # top cell first
            lo, hi = Hxy[k], Hxy[k + 1]
            if lo - eps <= z or k == 0:
                up = (hi - z) / (hi - lo)       # 1 at the lower level
                up = min(1.0, max(0.0, up))
                out = {}
                for i, w in lam.items():
                    if w * up != 0.0:
                        out[(i, k)] = w * up
                    if w * (1.0 - up) != 0.0:
                        out[(i, k + 1)] = w * (1.0 - up)
                return out
        return None


def floor_weights(nodes, tris, p):
    """Barycentric weights over the first triangle holding p, or None."""
    for a, b, c in tris:
        w = barycentric(nodes[a], nodes[b], nodes[c], p)
        if w is None:
            continue
        if min(w) >= -1e-9:
            return {a: w[0], b: w[1], c: w[2]}
    return None


# ---------------------------------------------------------------------------
# emitting
# ---------------------------------------------------------------------------

def lam_s(lam):
    return " ".join(f"{i}={fmt(w)}" for i, w in sorted(lam.items()))


def w3_s(w):
    if w is None:
        return "OUTSIDE"
    return " ".join(f"{i}@{k}={fmt(v)}" for (i, k), v in sorted(w.items()))


def heights_s(st):
    return " ".join(f"L{k} " + ",".join(fmt(h) for h in row)
                    for k, row in enumerate(st.H))


def build():
    out = []
    out.append("# ofxManifold -- 3D map geometry vectors (round 3A-4)")
    out.append("# Generated by tests/ref/reference_levels.py. Do not edit.")
    out.append("#")
    out.append("# NODES name i:x,y ...          the floor plan's node positions")
    out.append("# TRIS name a,b,c ...           its triangles")
    out.append("# STACK name NODES n OPS ...    levels built by operations, each")
    out.append("#                               with the result it must give")
    out.append("# HEIGHTS name cls stack ...    every node's height, every level")
    out.append("# EVAL3 / EVALXYZ               weights over node@level; the")
    out.append("#                               runner also checks they sum to 1,")
    out.append("#                               are >= 0, and reproduce the point")
    out.append("")

    # A square floor plan of two triangles.
    sq = {0: (0.1, 0.1), 1: (0.9, 0.1), 2: (0.9, 0.9), 3: (0.1, 0.9)}
    tris = [(0, 1, 2), (0, 2, 3)]
    out.append("NODES square " + " ".join(f"{i}:{fmt(x)},{fmt(y)}"
                                          for i, (x, y) in sq.items()))
    out.append("TRIS square " + " ".join(f"{a},{b},{c}" for a, b, c in tris))
    out.append("")

    stacks = {}

    def stack_rec(name, ops, note=None):
        st = Stack(4)
        results = []
        for op in ops:
            kind = op[0]
            if kind == "COPY":
                at, bad = st.copy(op[1], op[2])
                results.append(f"COPY {op[1]} {fmt(op[2])} ="
                               + (str(at) if at is not None else
                                  "REFUSED:" + (",".join(map(str, bad)) or "-")))
            elif kind == "RAISE":
                bad = st.raise_(op[1], op[3], op[2])
                results.append(f"RAISE {op[1]} {fmt(op[2])} "
                               + ",".join(map(str, op[3])) + " ="
                               + ("OK" if not bad else
                                  "REFUSED:" + ",".join(map(str, bad))))
            elif kind == "SET":
                ok = st.set(op[1], op[2], op[3])
                results.append(f"SET {op[1]} {op[2]} {fmt(op[3])} ="
                               + ("OK" if ok else "REFUSED"))
            elif kind == "REMOVE":
                ok = st.remove(op[1])
                results.append(f"REMOVE {op[1]} ="
                               + ("OK" if ok else "REFUSED"))
        if note:
            for line in note:
                out.append("# " + line)
        out.append(f"STACK {name} NODES square OPS " + " ; ".join(results))
        out.append(f"HEIGHTS heights_{name} ANALYTIC {name} {heights_s(st)}")
        stacks[name] = st
        return st

    stack_rec("flat", [], ["one level: today's map. z is ignored."])
    stack_rec("three",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0)],
              ["three flat levels, at 0, 0.5 and 1: copy the floor, then",
               "copy that copy"])
    stack_rec("bent",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0),
               ("RAISE", 1, 0.3, [2])],
              ["the middle level bent: node 2 raised from 0.5 to 0.8, a",
               "branch leaning up toward the top level"])
    stack_rec("between",
              [("COPY", 0, 1.0), ("COPY", 0, 0.4)],
              ["a level copied to a height BETWEEN two others goes between",
               "them: copy of 0 at 1.0 is level 1, then a copy at 0.4 is",
               "inserted as the new level 1"])
    stack_rec("copybent",
              [("COPY", 0, 0.5), ("RAISE", 1, 0.2, [0, 1]),
               ("COPY", 1, 1.0)],
              ["copying a bent level copies the bend: the new level at 1.0",
               "has nodes 0 and 1 at 1.2"])
    out.append("")

    out.append("# REFUSED, changing nothing, and naming the nodes:")
    stack_rec("refuse_reach",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0),
               ("RAISE", 1, 0.5, [2]), ("RAISE", 1, 0.6, [1, 2]),
               ("RAISE", 1, -0.6, [0]),
               ("RAISE", 1, 0.3, [2]), ("RAISE", 1, 0.3, [1, 2])],
              ["raising the middle level's node 2 by 0.5 would TOUCH the top",
               "level (1.0): touching is refused, not just crossing. Then",
               "nodes 1 and 2 by 0.6: both refused, so neither moves -- all",
               "or nothing. Lowering node 0 below the ground: refused.",
               "Then node 2 up 0.3 (to 0.8, fine), and nodes 1 and 2 up 0.3",
               "more: node 1 would fit, node 2 would cross the top. Refused,",
               "and node 1 stays at 0.5 -- HEIGHTS shows it."])
    stack_rec("refuse_copy",
              [("COPY", 0, 0.5), ("RAISE", 1, 0.4, [3]),
               ("COPY", 0, 0.7), ("COPY", 0, 0.5)],
              ["copying the ground to 0.7 puts the new level above the middle",
               "one -- but the middle level's node 3 was raised to 0.9, and",
               "0.7 is below it: refused for node 3, nothing changes. A",
               "second level at 0.5: refused, one already starts there."])
    stack_rec("refuse_copy_upper",
              [("COPY", 0, 1.0), ("RAISE", 1, -0.6, [1]), ("COPY", 0, 0.5)],
              ["the level ABOVE checks a copy too: the top level's node 1 is",
               "lowered to 0.4, so a copy of the ground at 0.5 would put",
               "node 1 above it -- refused for node 1"])
    stack_rec("refuse_last",
              [("REMOVE", 0)],
              ["the only level cannot be removed"])
    stack_rec("remove_middle",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0), ("RAISE", 1, 0.3, [2]),
               ("REMOVE", 1)],
              ["removing the bent middle level leaves 0 and 1 flat"])
    stack_rec("set_heights",
              [("COPY", 0, 1.0), ("SET", 1, 2, 1.5), ("SET", 1, 0, 0.0),
               ("SET", 0, 1, -0.25)],
              ["set one node's height, as a file does: 1.5 fits; 0.0 on",
               "level 1 touches the ground and is refused; -0.25 on the",
               "ground fits"])
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# WEIGHTS for given floor-plan weights. Every record is also")
    out.append("# checked for summing to 1, no negatives, and reproducing the")
    out.append("# point from the node positions and heights.")
    out.append("#" + "-" * 68)

    def eval_rec(name, cls, sname, lam, z, note=None):
        if note:
            for line in note:
                out.append("# " + line)
        w = stacks[sname].weigh(lam, z)
        out.append(f"EVAL3 {name} {cls} {sname} W {lam_s(lam)} Z {fmt(z)} "
                   f"EXPECT {w3_s(w)}")

    mid = {0: 0.25, 1: 0.25, 2: 0.25, 3: 0.25}
    eval_rec("flat_ignores_z", "ANALYTIC", "flat", mid, 7.0,
             ["one level: z ignored, the floor plan's weights on level 0"])
    eval_rec("prism_quarter", "ANALYTIC", "three", mid, 0.125,
             ["flat levels are exact prisms: a quarter of the way from 0 to",
              "0.5, each node weighs 0.25 x 0.75 below and 0.25 x 0.25 above"])
    eval_rec("on_middle_level", "ANALYTIC", "three", mid, 0.5,
             ["exactly on the middle level: that level alone"])
    eval_rec("on_bottom", "ANALYTIC", "three", mid, 0.0)
    eval_rec("on_top", "ANALYTIC", "three", mid, 1.0)
    eval_rec("upper_cell", "ANALYTIC", "three",
             {0: 0.5, 1: 0.5}, 0.875)
    eval_rec("above_top", "ANALYTIC", "three", mid, 1.01,
             ["above the top level: OUTSIDE, as off a 2D map's edge -- not",
              "clamped to the top level"])
    eval_rec("below_bottom", "ANALYTIC", "three", mid, -0.2)
    out.append("# the bent level: under node 2 the middle level is higher")
    eval_rec("bent_at_raised", "ANALYTIC", "bent", {2: 1.0}, 0.5,
             ["at node 2 the middle level is at 0.8: z = 0.5 is 5/8 of the",
              "way up from the ground, not on the middle level"])
    eval_rec("bent_elsewhere", "ANALYTIC", "bent", {0: 1.0}, 0.5,
             ["at node 0 the middle level is still at 0.5"])
    eval_rec("bent_between", "CROSS", "bent",
             {0: 0.2, 1: 0.3, 2: 0.4, 3: 0.1}, 0.55,
             ["where the bend is partial: middle level at 0.5 + 0.4 x 0.3",
              "= 0.62 here"])
    eval_rec("bent_upper", "CROSS", "bent",
             {1: 0.3, 2: 0.7}, 0.9)
    eval_rec("copybent_mid", "CROSS", "copybent",
             {0: 0.5, 2: 0.5}, 0.95)
    eval_rec("between_inserted", "ANALYTIC", "between", mid, 0.2,
             ["the level copied to 0.4 sits between 0 and 1.0: z = 0.2 is",
              "halfway between the ground and IT"])
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# THE WHOLE PATH: a real point (x, y, z) through the C++ floor")
    out.append("# plan's own evaluation, then the levels. Here the point")
    out.append("# reproduced is the query point itself.")
    out.append("#" + "-" * 68)
    sqp = {i: p for i, p in sq.items()}
    for name, cls, sname, p, z in (
            ("xyz_centre", "ANALYTIC", "three", (0.5, 0.5), 0.25),
            ("xyz_lower_tri", "CROSS", "bent", (0.7, 0.3), 0.6),
            ("xyz_upper_tri", "CROSS", "bent", (0.3, 0.7), 0.6),
            ("xyz_on_diagonal", "CROSS", "bent", (0.4, 0.4), 0.6),
            ("xyz_near_raised", "CROSS", "bent", (0.85, 0.85), 0.75),
            ("xyz_outside_floor", "ANALYTIC", "three", (0.05, 0.5), 0.5),
            # a point exactly on the top level whose height there, summed
            # in single precision from the floor plan's weights, lands one
            # rounding step BELOW 1: without a tolerance it would read as
            # above the top, outside. Found by searching the square.
            ("xyz_top_by_rounding", "ANALYTIC", "three", (0.108, 0.124), 1.0),
            ("xyz_above", "ANALYTIC", "bent", (0.85, 0.85), 1.2)):
        lam = floor_weights(sqp, tris, p)
        w = None if lam is None else stacks[sname].weigh(lam, z)
        out.append(f"EVALXYZ {name} {cls} {sname} SQUARE P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} EXPECT {w3_s(w)}")
    out.append("")

    out.append("# CONTINUITY: sweeping across the square's diagonal -- from one")
    out.append("# triangle into the other -- while rising through the bent")
    out.append("# middle level. No step may jump: the runner bounds the change")
    out.append("# in every weight between neighbouring steps.")
    for name, sname, a, b in (("sweep_diagonal", "bent",
                               (0.3, 0.6, 0.1), (0.7, 0.4, 0.95)),
                              ("sweep_under_bend", "bent",
                               (0.2, 0.2, 0.45), (0.85, 0.85, 0.45))):
        steps = 200
        prev, worst = None, 0.0
        for s in range(steps + 1):
            u = s / steps
            p = (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)
            z = a[2] + (b[2] - a[2]) * u
            lam = floor_weights(sqp, tris, p)
            w = stacks[sname].weigh(lam, z)
            assert w is not None, (name, s)
            if prev is not None:
                keys = set(w) | set(prev)
                worst = max(worst, max(abs(w.get(k, 0.0) - prev.get(k, 0.0))
                                       for k in keys))
            prev = w
        out.append(f"CONT {name} CROSS {sname} SQUARE FROM {fmt(a[0])} "
                   f"{fmt(a[1])} {fmt(a[2])} TO {fmt(b[0])} {fmt(b[1])} "
                   f"{fmt(b[2])} STEPS {steps} MAXSTEP {fmt(worst * 1.5 + 1e-4)}")
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# NODES ADDED AND REMOVED. A new node needs nothing: on every")
    out.append("# level it sits at that level's start. Removing node 1 of four")
    out.append("# renumbers 2 and 3; the bend on old node 2 must follow it to")
    out.append("# its new id, 1.")
    out.append("#" + "-" * 68)
    st = Stack(4)
    st.copy(0, 0.5); st.copy(1, 1.0); st.raise_(1, [2], 0.3)
    st.add_node()
    out.append(f"NEWNODE newnode_bases ANALYTIC bent NODE 4 HEIGHTS "
               + ",".join(fmt(st.H[k][4]) for k in range(len(st.H))))
    st2 = Stack(4)
    st2.copy(0, 0.5); st2.copy(1, 1.0); st2.raise_(1, [2], 0.3)
    table = [0, -1, 1, 2]
    st2.remap(table)
    out.append(f"REMAP3 remap_follows_bend ANALYTIC bent TABLE "
               + " ".join(map(str, table)) + f" {heights_s(st2)}")
    return out


def main():
    out = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        parts = line.split()
        if len(parts) > 2 and parts[0] in ("HEIGHTS", "EVAL3", "EVALXYZ",
                                            "CONT", "NEWNODE", "REMAP3"):
            counts[parts[2]] = counts.get(parts[2], 0) + 1
    print(f"wrote {OUT}")
    for k in sorted(counts):
        print(f"  {k:<9} {counts[k]}")
    print(f"  TOTAL     {sum(counts.values())}")


if __name__ == "__main__":
    main()
