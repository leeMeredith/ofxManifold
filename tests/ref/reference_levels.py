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
    build_map3d(out, sq, tris)
    return out


# ---------------------------------------------------------------------------
# round 3A-4b: what the 3D map drives (PLAN-round3.md decisions D and F)
#
# Bindings and node values per level, read through the 3D weights. The
# outputs belong to every level at once.
#
# Formulated per OUTPUT NAME over every (node, level) weight, through the
# outputs reference's own Mapping -- one per level -- rather than the C++'s
# per-level resolve and sum. Derived outputs fed by outputs are computed from
# that total; those fed by nodes from each node's weight summed over levels.
# ---------------------------------------------------------------------------

import copy as _copy
import math as _math

from reference_outputs import Mapping as OutMap  # noqa: E402


class Map3:
    def __init__(self, n):
        self.stack = Stack(n)
        self.maps = [OutMap()]
        self.defs = []                 # [name, fallback, lo, hi]
        self.vals = [{}]               # per level: {(value, node): x}

    def value(self, k, node, name):
        d = next(d for d in self.defs if d[0] == name)
        return self.vals[k].get((name, node), d[1])

    def op(self, words):
        """Apply one operation; return its result token or None."""
        w = words
        v = w[0]
        if v == "OUT":
            for m in self.maps:
                m.add_output(w[1], int(w[2]), float(w[3]))
        elif v == "AFTER":
            srcs = [(s.split(":")[0], float(s.split(":")[1]))
                    for s in w[5].split(",")]
            for m in self.maps:
                m.add_derived_after(w[1], int(w[2]), float(w[3]), w[4], srcs)
        elif v == "TAP":
            srcs = [(int(s.split(":")[0]), float(s.split(":")[1]))
                    for s in w[5].split(",")]
            for m in self.maps:
                m.add_derived(w[1], int(w[2]), float(w[3]), w[4], srcs)
        elif v == "COPY":
            frm, base = int(w[1]), float(w[2])
            at, bad = self.stack.copy(frm, base)
            if at is None:
                return "REFUSED:" + (",".join(map(str, bad)) or "-")
            self.maps.insert(at, _copy.deepcopy(self.maps[frm if frm < at
                                                          else frm]))
            self.vals.insert(at, dict(self.vals[frm]))
            return str(at)
        elif v == "RAISE":
            bad = self.stack.raise_(int(w[1]), [int(x) for x in w[3].split(",")],
                                    float(w[2]))
            return "OK" if not bad else "REFUSED:" + ",".join(map(str, bad))
        elif v == "BIND":
            self.maps[int(w[1])].bind(int(w[2]), w[3], float(w[4]))
        elif v == "SILENCE":
            self.maps[int(w[1])].bind_silence(int(w[2]), float(w[3]))
        elif v == "CLEAR":
            self.maps[int(w[1])].links.pop(int(w[2]), None)
        elif v == "FRACTION":
            ok = self.maps[int(w[1])].set_fraction(int(w[2]), float(w[3]))
            return "OK" if ok else "REFUSED"
        elif v == "VALUE":
            if any(d[0] == w[1] for d in self.defs):
                return "REFUSED"
            self.defs.append([w[1], float(w[2]), float(w[3]), float(w[4])])
            return str(len(self.defs) - 1)
        elif v == "SETV":
            self.vals[int(w[1])][(w[3], int(w[2]))] = float(w[4])
        elif v == "REMOVEOUT":
            for m in self.maps:
                m.remove_output(w[1])
        elif v == "TRIM":
            for m in self.maps:
                for o in m.outputs:
                    if o[0] == w[1]:
                        o[2] = float(w[2])
        elif v == "REMOVELEVEL":
            if not self.stack.remove(int(w[1])):
                return "REFUSED"
            del self.maps[int(w[1])]
            del self.vals[int(w[1])]
            return "OK"
        elif v == "REMAP":
            table = [int(x) for x in w[1].split(",")]
            self.stack.remap(table)
            self.maps = [m.remap(table) for m in self.maps]
            self.vals = [{(nm, table[nd]): x for (nm, nd), x in lv.items()
                          if table[nd] >= 0} for lv in self.vals]
        return None

    # ---- reading, per output name ------------------------------------------

    def channels(self, w3, levels):
        """w3: {(node, level): weight}, or None outside."""
        room = self.maps[0]
        used = room.channels_in_use()
        dense = [0.0] * (max(used) + 1) if used else []
        if w3 is None:
            return dense, 0.0
        routed = {o[0]: 0.0 for o in room.outputs}
        silence = 0.0
        for k, m in enumerate(self.maps):
            wv = [(n, w) for (n, kk), w in w3.items() if kk == k]
            if not wv:
                continue
            r = m.routed(wv)
            for name in routed:
                routed[name] += r[name]
            silence += m.silence(wv)
        floor = {}
        for (n, _k), w in w3.items():
            floor[n] = floor.get(n, 0.0) + w
        trim = {o[0]: o[2] for o in room.outputs}
        sent = {nm: routed[nm] * trim[nm] for nm in routed}
        for name, ch, tr in room.outputs:
            dense[ch] = routed[name] * (tr if levels else 1.0)
        for name, ch, tr, mode, sources, feed in room.derived:
            read = sent if feed == "outputs" else floor
            if mode == "linear":
                v = sum(g * read.get(s, 0.0) for s, g in sources)
            else:
                v = _math.sqrt(sum((g * read.get(s, 0.0)) ** 2
                                   for s, g in sources))
            dense[ch] = v * (tr if levels else 1.0)
        return dense, silence

    def value_at(self, name, w3):
        d = next(d for d in self.defs if d[0] == name)
        if w3 is None:
            return d[1]
        return sum(w * self.value(k, n, name) for (n, k), w in w3.items())


def build_map3d(out, sq, tris):
    out.append("")
    out.append("#" + "-" * 68)
    out.append("# WHAT THE 3D MAP DRIVES (round 3A-4b): bindings and node")
    out.append("# values per level, read through the 3D weights. Outputs")
    out.append("# belong to every level at once. Every record is also checked")
    out.append("# for: every level agreeing about the room; and, where the")
    out.append("# weights sit on one level, equalling that level's own Mapping.")
    out.append("#" + "-" * 68)

    base_ops = [
        "OUT L 1 1", "OUT R 2 0.5", "OUT Top 3 1",
        "AFTER sub 9 0.8 linear L:1,R:1",
        "TAP tap 8 1 linear 0:1,2:1",
        "VALUE bright 0.5 0 1", "VALUE dist 0 0 1", "VALUE bright 0.2 0 1",
        # the ground
        "BIND 0 0 L 1", "BIND 0 1 R 1", "BIND 0 2 L 1", "BIND 0 2 R 1",
        "BIND 0 3 L 1", "SILENCE 0 3 1",
        "SETV 0 0 bright 0.1", "SETV 0 2 bright 0.2", "SETV 0 1 dist 0.3",
        # the middle: a copy of the ground, node 2 moved to Top
        "COPY 0 0.5", "CLEAR 1 2", "BIND 1 2 Top 1", "SETV 1 2 bright 0.7",
        # the canopy: a copy of the middle, nodes 0 and 1 to Top, all bright
        "COPY 1 1", "CLEAR 2 0", "BIND 2 0 Top 1", "CLEAR 2 1",
        "BIND 2 1 Top 1", "SETV 2 0 bright 1", "SETV 2 1 bright 1",
        "SETV 2 2 bright 1", "SETV 2 3 bright 1",
        "RAISE 1 0.3 2", "FRACTION 2 3 0.25",
    ]
    maps = {}

    def map_rec(name, ops, note):
        m = Map3(4)
        shown = []
        for o in ops:
            r = m.op(o.split())
            shown.append(o + (f" ={r}" if r is not None else ""))
        for line in note:
            out.append("# " + line)
        out.append(f"MAP3 {name} NODES square OPS " + " ; ".join(shown))
        maps[name] = m
        return m

    map_rec("forest", base_ops,
            ["the forest: ground binds to L and R, the middle level moves",
             "node 2 to Top, the canopy sends nearly everything to Top. A",
             "second 'bright' is refused; the copies carry bindings and",
             "values up with them"])
    map_rec("single", base_ops[:17],
            ["one level: today's map, a Map3D with nothing above the ground"])
    map_rec("forest_noR", base_ops + ["REMOVEOUT R"],
            ["R removed: from every level at once, and from the sub"])
    map_rec("forest_late", base_ops + ["OUT Floor 4 1", "BIND 2 3 Floor 1"],
            ["an output added AFTER the levels exist reaches every level --",
             "every earlier output was added to the ground and copied up,",
             "which a first-level-only add would also pass. The canopy binds",
             "node 3 to it."])
    map_rec("forest_trim", base_ops + ["TRIM Top 0.25"],
            ["Top trimmed to a quarter: on every level at once"])
    map_rec("forest_nomid", base_ops + ["REMOVELEVEL 1"],
            ["the middle level removed: ground and canopy keep their own",
             "bindings and values"])
    map_rec("forest_remap", base_ops + ["REMAP 0,-1,1,2"],
            ["node 1 removed from the floor plan: on every level its",
             "bindings and values go, and those of nodes 2 and 3 follow them",
             "to their new ids"])

    def w3_rec(kind, name, cls, mname, w3, extra=""):
        m = maps[mname]
        routed, sil = m.channels(w3, False)
        levels, _ = m.channels(w3, True)
        wtxt = ("OUTSIDE" if w3 is None else
                " ".join(f"{n}@{k}={fmt(w)}" for (n, k), w in sorted(w3.items())))
        out.append(f"{kind} {name} {cls} {mname} W {wtxt}{extra} "
                   f"ROUTED {' '.join(fmt(x) for x in routed)} "
                   f"SILENCE {fmt(sil)} "
                   f"LEVELS {' '.join(fmt(x) for x in levels)}")

    mid = {0: 0.25, 1: 0.25, 2: 0.25, 3: 0.25}

    def on(k, lam, t=None):
        if t is None:
            return {(n, k): w for n, w in lam.items()}
        r = {(n, k): w * (1 - t) for n, w in lam.items()}
        r.update({(n, k + 1): w * t for n, w in lam.items()})
        return r

    out.append("# on the ground alone: exactly level 0's own Mapping")
    w3_rec("CH3", "ground_only", "ANALYTIC", "forest", on(0, mid))
    out.append("# on the canopy alone: node 3 sends a quarter of itself to L")
    out.append("# and the rest to silence; the others go to Top")
    w3_rec("CH3", "canopy_only", "ANALYTIC", "forest", on(2, mid))
    out.append("# halfway between ground and middle: node 2's share splits")
    out.append("# between L and R below and Top above")
    w3_rec("CH3", "ground_mid_half", "ANALYTIC", "forest", on(0, mid, 0.5))
    out.append("# the sub is computed ONCE from what L and R send in total --")
    out.append("# not one sub per level, summed")
    w3_rec("CH3", "sub_from_total", "ANALYTIC", "forest",
           on(0, {0: 0.5, 1: 0.5}, 0.25))
    out.append("# the tap reads the floor plan's weights for nodes 0 and 2:")
    out.append("# the same at every height")
    w3_rec("CH3", "tap_ignores_height_low", "ANALYTIC", "forest",
           on(0, {0: 0.6, 2: 0.4}, 0.2))
    w3_rec("CH3", "tap_ignores_height_high", "ANALYTIC", "forest",
           on(1, {0: 0.6, 2: 0.4}, 0.9))
    w3_rec("CH3", "mixed_upper", "CROSS", "forest",
           on(1, {0: 0.1, 1: 0.2, 2: 0.3, 3: 0.4}, 0.6))
    out.append("# outside: nothing reaches the room, and nothing is silent")
    w3_rec("CH3", "outside", "ANALYTIC", "forest", None)
    out.append("# one level: the Map3D is today's Mapping exactly")
    w3_rec("CH3", "single_is_mapping", "ANALYTIC", "single",
           on(0, {0: 0.3, 1: 0.3, 2: 0.2, 3: 0.2}))
    out.append("# after removals")
    w3_rec("CH3", "noR_between", "CROSS", "forest_noR",
           on(0, {0: 0.3, 1: 0.3, 2: 0.2, 3: 0.2}, 0.5))
    w3_rec("CH3", "trim_canopy", "ANALYTIC", "forest_trim", on(2, mid))
    w3_rec("CH3", "late_output_canopy", "ANALYTIC", "forest_late", on(2, mid))
    w3_rec("CH3", "nomid_between", "CROSS", "forest_nomid",
           on(0, {0: 0.3, 1: 0.3, 2: 0.2, 3: 0.2}, 0.5))
    out.append("# node 1 removed: old node 2 is now node 1, old 3 is node 2")
    w3_rec("CH3", "remap_between", "CROSS", "forest_remap",
           on(0, {0: 0.4, 1: 0.3, 2: 0.3}, 0.5))
    out.append("")

    out.append("# THE WHOLE PATH: a point (x, y, z), the floor plan's own")
    out.append("# evaluation, the bent levels, then the bindings")
    for name, p, z in (("xyz_low", (0.6, 0.3), 0.2),
                       ("xyz_under_bend", (0.8, 0.75), 0.6),
                       ("xyz_canopy", (0.3, 0.7), 0.95),
                       ("xyz_above", (0.3, 0.7), 1.1)):
        lam = floor_weights({i: q for i, q in sq.items()}, tris, p)
        w3 = maps["forest"].stack.weigh(lam, z)
        m = maps["forest"]
        routed, sil = m.channels(w3, False)
        levels, _ = m.channels(w3, True)
        out.append(f"CH3XYZ {name} CROSS forest SQUARE P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} "
                   f"ROUTED {' '.join(fmt(x) for x in routed)} "
                   f"SILENCE {fmt(sil)} "
                   f"LEVELS {' '.join(fmt(x) for x in levels)}")
    out.append("")

    out.append("# VALUES: each level's node values by the 3D weights, always")
    out.append("# linearly. Node 1 was never given 'bright' on the ground:")
    out.append("# it carries the fallback, 0.5. A null node carries values too")
    out.append("# (node 3 on the ground is half silent; still counted).")
    for name, cls, mname, w3, vname in (
            ("bright_ground", "ANALYTIC", "forest", on(0, mid), "bright"),
            ("bright_canopy", "ANALYTIC", "forest", on(2, mid), "bright"),
            ("bright_between", "ANALYTIC", "forest", on(0, mid, 0.5), "bright"),
            ("dist_ground_node1", "ANALYTIC", "forest", on(0, {1: 1.0}), "dist"),
            ("dist_copied_up", "ANALYTIC", "forest", on(1, {1: 1.0}), "dist"),
            ("bright_outside", "ANALYTIC", "forest", None, "bright"),
            # unequal weights on the renumbered nodes 1 and 2: with equal
            # ones, values left at their OLD ids sum to the same answer, and
            # a remap that moved nothing passed
            ("bright_remap", "CROSS", "forest_remap",
             on(0, {0: 0.4, 1: 0.4, 2: 0.2}, 0.5), "bright"),
            ("bright_nomid", "CROSS", "forest_nomid",
             on(0, {0: 0.3, 1: 0.3, 2: 0.2, 3: 0.2}, 0.5), "bright")):
        m = maps[mname]
        wtxt = ("OUTSIDE" if w3 is None else
                " ".join(f"{n}@{k}={fmt(w)}" for (n, k), w in sorted(w3.items())))
        out.append(f"VAL3 {name} {cls} {mname} W {wtxt} VALUE {vname} "
                   f"EXPECT {fmt(m.value_at(vname, w3))}")


def main():
    out = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        parts = line.split()
        if len(parts) > 2 and parts[0] in ("HEIGHTS", "EVAL3", "EVALXYZ",
                                            "CONT", "NEWNODE", "REMAP3",
                                            "CH3", "CH3XYZ", "VAL3"):
            counts[parts[2]] = counts.get(parts[2], 0) + 1
    print(f"wrote {OUT}")
    for k in sorted(counts):
        print(f"  {k:<9} {counts[k]}")
    print(f"  TOTAL     {sum(counts.values())}")


if __name__ == "__main__":
    main()
