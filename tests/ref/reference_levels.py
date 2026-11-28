#!/usr/bin/env python3
"""
ofxManifold -- reference for the 3D map's geometry (round 3A-4).

A 3D map is one floor plan seen at several levels. This writes the vectors
for src/core/ofxManifoldLevels.h: making levels by copying, bending them,
refusing anything that would make levels cross -- touching is allowed since
round 3B-2d --, and weighting a point (x, y, z) over (node, level).

Formulated differently from the C++ on purpose:
  - Levels are held here as a full table of heights, every node on every
    level, rebuilt whole after each operation. The C++ keeps only heights
    that were set and falls back to the level's start.
  - A point is placed by scanning the PRISM CELLS from the top down -- each
    pair of neighbouring levels is one cell -- and taking the first whose
    vertical span at (x, y) holds z. The C++ walks up from the bottom.
  - Weights inside a cell come from the distance to the UPPER level,
    (H_upper - z) / (H_upper - H_lower), not from the lower.
  - A pinch (3B-2d) is found by collecting EVERY level whose height at the
    point is z and sharing the point between the top two of them; the C++
    looks one level down from the cell its walk found.
  - Weights by level position (3B-2d) are a hat over the levels, each level j
    weighing max(0, 1 - |u - j|); the C++ splits one cell by u's fraction.
  - A shaped map (3B-5a) -- nodes off their columns on a level -- keeps every
    node's place on every level as a full table of absolute positions, where
    the C++ keeps offsets that were set. A height is found by scanning the
    cells from the TOP, 64 looks a cell, and closing in by false position on
    the distance to the UPPER level; the C++ walks up from the bottom, 8
    looks a cell, halving. The plainest cases are not searched for at all:
    flat levels scaled about the centre have a closed form, written out here
    and emitted as ANALYTIC.
  - Sound slices (3B-5b). A region's area between two levels is fitted here
    through three slices (the bottom, the middle, the top) and its least
    value taken from the fit; the C++ multiplies the quadratic out from the
    nodes' moves. A fold is decided here from the sign of the volume's
    Jacobian -- the triple product of a triangle's two edges and the way its
    points travel -- at 65 slices; the C++ compares the height under a point
    at one slice and the next. Crossing is the textbook segment test.

The property that makes it a 3D map is checked in the runner, not assumed:
the weights reproduce the point. Sum over (node, level) of weight times
(x_i, y_i, height) is (x, y, z).

Writes tests/vectors/levels.vec.
"""

import math
import os
import sys

sys.path.insert(0, "tests/ref")
from reference_manifold import barycentric, fmt  # noqa: E402

OUT = "tests/vectors/levels.vec"


# ---------------------------------------------------------------------------
# the model
# ---------------------------------------------------------------------------

TOUCH = 1e-6


def settle(h, lo=None, hi=None):
    """3B-2d: a height within TOUCH past the level below (lo) or above (hi)
    is on it, exactly."""
    if lo is not None and lo - TOUCH <= h < lo:
        h = lo
    if hi is not None and hi < h <= hi + TOUCH:
        h = hi
    return h


class Stack:
    """bases[k], and H[k][i]: node i's height on level k, for every node."""

    def around(self, H, k, i):
        """Node i's heights on the levels either side of k in table H."""
        return (H[k - 1][i] if k > 0 else None,
                H[k + 1][i] if k + 1 < len(H) else None)

    def __init__(self, node_count):
        self.n = node_count
        self.bases = [0.0]
        self.H = [[0.0] * node_count]

    def ordered(self, H):
        return all(H[k][i] <= H[k + 1][i]
                   for k in range(len(H) - 1) for i in range(self.n))

    def touches(self):
        return (any(self.bases[k] == self.bases[k + 1]
                    for k in range(len(self.bases) - 1))
                or any(self.H[k][i] == self.H[k + 1][i]
                       for k in range(len(self.H) - 1) for i in range(self.n)))

    def copy(self, frm, new_base):
        if new_base in self.bases:
            return None, []
        shift = new_base - self.bases[frm]
        row = [h + shift for h in self.H[frm]]
        at = sum(1 for b in self.bases if b < new_base)
        H = self.H[:at] + [row] + self.H[at:]
        for i in range(self.n):
            row[i] = settle(row[i], *self.around(H, at, i))
        bad = [i for i in range(self.n)
               if (at > 0 and not row[i] >= H[at - 1][i])
               or (at + 1 < len(H) and not row[i] <= H[at + 1][i])]
        if bad:
            return None, bad
        self.H = H
        self.bases = self.bases[:at] + [new_base] + self.bases[at:]
        return at, []

    def raise_(self, k, nodes, dz):
        H = [list(r) for r in self.H]
        for i in nodes:
            H[k][i] = settle(H[k][i] + dz, *self.around(H, k, i))
        bad = [i for i in nodes
               if (k > 0 and not H[k][i] >= H[k - 1][i])
               or (k + 1 < len(H) and not H[k][i] <= H[k + 1][i])]
        if bad:
            return bad
        self.H = H
        return []

    def move(self, k, dz):
        """A whole level: its start and every node. Refused if a node, or
        the start, would pass a neighbour; reaching one is a touch."""
        H = [list(r) for r in self.H]
        row = [settle(h + dz, *self.around(H, k, i))
               for i, h in enumerate(self.H[k])]
        nb = settle(self.bases[k] + dz,
                    self.bases[k - 1] if k > 0 else None,
                    self.bases[k + 1] if k + 1 < len(self.bases) else None)
        bad = [i for i in range(self.n)
               if (k > 0 and not row[i] >= self.H[k - 1][i])
               or (k + 1 < len(self.H) and not row[i] <= self.H[k + 1][i])]
        bases_fit = ((k == 0 or nb >= self.bases[k - 1])
                     and (k + 1 >= len(self.H) or nb <= self.bases[k + 1]))
        if bad or not bases_fit:
            return bad if bad else ["-"]
        self.H[k] = row
        self.bases[k] = nb
        return []

    def room(self, k):
        inf = float("inf")
        down = up = inf
        if k > 0:
            down = min([self.bases[k] - self.bases[k - 1]]
                       + [self.H[k][i] - self.H[k - 1][i] for i in range(self.n)])
        if k + 1 < len(self.H):
            up = min([self.bases[k + 1] - self.bases[k]]
                     + [self.H[k + 1][i] - self.H[k][i] for i in range(self.n)])
        return down, up

    def set(self, k, i, h):
        H = [list(r) for r in self.H]
        H[k][i] = settle(h, *self.around(H, k, i))
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
        on = [k for k in range(L) if Hxy[k] == z]
        if len(on) >= 2:                        # a pinch: the top two share
            k = on[-2]
            return {(i, j): w * 0.5 for i, w in lam.items() if w != 0.0
                    for j in (k, k + 1)}
        for k in range(L - 2, -1, -1):          # top cell first
            lo, hi = Hxy[k], Hxy[k + 1]
            if lo - eps <= z or k == 0:
                if hi == lo:                    # a pinch, z within eps
                    return {(i, j): w * 0.5 for i, w in lam.items()
                            if w != 0.0 for j in (k, k + 1)}
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


def level_height(st, k, lam):
    """A level's height under the point; its base off the floor plan."""
    if not lam:
        return st.bases[k]
    return sum(w * st.H[k][i] for i, w in lam.items())


def height_at_position(st, lam, u, extrapolate=False):
    """3B-2c: the height at level position u -- 0 the bottom surface, 1 the
    next. Restrained: u held to 0..L-1. Extrapolated: the end cells carry on."""
    L = len(st.H)
    if L == 1:
        return level_height(st, 0, lam)
    if not extrapolate:
        u = min(max(u, 0.0), float(L - 1))
    k = min(max(int(math.floor(u)), 0), L - 2)
    lo, hi = level_height(st, k, lam), level_height(st, k + 1, lam)
    return lo + (u - k) * (hi - lo)


def position_at(st, lam, z, extrapolate=False):
    """Its inverse, worked out the other way: every cell whose span holds z
    is a candidate, and the LOWEST is taken -- at a pinch (3B-2d) many u have
    one z. Past the ends, the end cell, carried on; through an end cell with
    no gap there is no slope, so the nearer level."""
    L = len(st.H)
    if L == 1:
        return 0.0
    h = [level_height(st, k, lam) for k in range(L)]
    if not extrapolate:
        if z <= h[0]:
            return 0.0
        if z > h[-1]:
            return float(L - 1)
    held = [k for k in range(L - 1) if h[k] <= z <= h[k + 1]]
    k = min(held) if held else (0 if z < h[0] else L - 2)
    if h[k + 1] == h[k]:
        return float(k) if z <= h[k] else float(k + 1)
    return k + (z - h[k]) / (h[k + 1] - h[k])


def weigh_at_position(st, lam, u):
    """3B-2d: the weights at level position u, as a hat over the levels --
    level j weighs 1 - |u - j| where that is positive. None outside: off the
    floor plan, or u outside 0..L-1. One level: u ignored."""
    if lam is None:
        return None
    L = len(st.H)
    if L == 1:
        return st.weigh(lam, 0.0)
    if not 0.0 <= u <= L - 1:
        return None
    out = {}
    for j in range(L):
        hat = 1.0 - abs(u - j)
        if hat <= 0.0:
            continue
        for i, w in lam.items():
            if w * hat != 0.0:
                out[(i, j)] = w * hat
    return out


# ---------------------------------------------------------------------------
# round 3B-5a: a node's own place on each level
# ---------------------------------------------------------------------------

class Shaped:
    """A stack, a floor plan, and P[k][i]: node i's place on level k."""

    def __init__(self, st, nodes, tris, offs):
        self.st, self.tris = st, tris
        self.floor = dict(nodes)
        self.n = len(nodes)
        self.P = [[tuple(nodes[i]) for i in range(self.n)] for _ in st.H]
        for (k, i), (dx, dy) in offs.items():
            self.P[k][i] = (nodes[i][0] + dx, nodes[i][1] + dy)

    def cut(self, k, t):
        """The slice t of the way from level k up: places and heights."""
        if k + 1 >= len(self.P):
            return self.P[k], self.st.H[k]
        pl = [(a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
              for a, b in zip(self.P[k], self.P[k + 1])]
        hs = [a + t * (b - a) for a, b in zip(self.st.H[k], self.st.H[k + 1])]
        return pl, hs

    def lam_at(self, k, t, p):
        pl, _ = self.cut(k, t)
        return floor_weights(dict(enumerate(pl)), self.tris, p)

    def split(self, lam, k, t):
        out = {}
        for i, w in lam.items():
            if w * (1.0 - t) != 0.0:
                out[(i, k)] = w * (1.0 - t)
            if w * t != 0.0:
                out[(i, k + 1)] = w * t
        return out

    def weigh_u(self, p, u):
        """By level position: the slice at u, no search."""
        L = len(self.P)
        if L == 1:
            lam = self.lam_at(0, 0.0, p)
            return None if lam is None else self.split(lam, 0, 0.0)
        if not 0.0 <= u <= L - 1:
            return None
        k = min(int(math.floor(u)), L - 2)
        lam = self.lam_at(k, u - k, p)
        return None if lam is None else self.split(lam, k, u - k)

    def height_u(self, p, u):
        L = len(self.P)
        u = min(max(u, 0.0), float(L - 1))
        k = min(int(math.floor(u)), max(L - 2, 0))
        lam = self.lam_at(k, u - k, p)
        if lam is None:
            return height_at_position(self.st, {}, u)
        _, hs = self.cut(k, u - k)
        return sum(w * hs[i] for i, w in lam.items())

    def find(self, p, z):
        """(k, t, lam) for the height z under p, or None. Cells from the
        top; in each, 64 looks; the short of the upper level, 'under', falls
        as t rises, and its zero is closed in on by false position."""
        L = len(self.P)
        eps = 1e-6

        def under(k, t):
            lam = self.lam_at(k, t, p)
            if lam is None:
                return None
            _, hs = self.cut(k, t)
            return z - sum(w * hs[i] for i, w in lam.items())

        def gap(k, t):
            lam = self.lam_at(k, t, p)
            return sum(w * (self.st.H[k + 1][i] - self.st.H[k][i])
                       for i, w in lam.items())

        def inner(k, a, b):
            """a inside, b outside: the last t inside toward b."""
            for _ in range(60):
                m = 0.5 * (a + b)
                if under(k, m) is None:
                    b = m
                else:
                    a = m
            return a

        hits = []
        for k in range(L - 2, -1, -1):
            N = 64
            for s in range(N):
                a, b = s / N, (s + 1) / N
                ua, ub = under(k, a), under(k, b)
                if ua is None and ub is None:
                    continue
                if ub is None:
                    b = inner(k, a, b); ub = under(k, b)
                if ua is None:
                    a = inner(k, b, a); ua = under(k, a)
                if not (ua >= -eps and ub <= eps):
                    continue
                for _ in range(200):
                    if b - a < 1e-15 or ua == ub:
                        break
                    m = a + (b - a) * ua / (ua - ub)
                    m = min(max(m, a), b)
                    um = under(k, m)
                    if um is None:
                        return None
                    if abs(um) < 1e-15:
                        a = b = m; ua = ub = um
                        break
                    if um > 0:
                        # keep false position from stalling: halve as well
                        a, ua = m, um
                        h = 0.5 * (a + b); uh = under(k, h)
                        if uh is not None and uh > 0:
                            a, ua = h, uh
                        elif uh is not None:
                            b, ub = h, uh
                    else:
                        b, ub = m, um
                        h = 0.5 * (a + b); uh = under(k, h)
                        if uh is not None and uh <= 0:
                            b, ub = h, uh
                        elif uh is not None:
                            a, ua = h, uh
                t = a if abs(ua) <= abs(ub) else b
                if t < eps:
                    t = 0.0
                if t > 1.0 - eps:
                    t = 1.0
                hits.append((k, t))
                break
        if not hits:
            return None
        # Every hit is the same height; where levels touch, the pinch: the
        # two levels with no gap between them here share the point.
        for k, t in hits:
            if not gap(k, t) > 0.0:
                return (k, 0.5, self.lam_at(k, 0.5, p) or self.lam_at(k, t, p))
        k, t = max(hits) if any(t == 0.0 for _, t in hits) else hits[0]
        if t == 1.0 and k + 2 < L:
            k, t = k + 1, 0.0
        return (k, t, self.lam_at(k, t, p))

    def fault(self):
        """None if sound, else (kind, cell, region)."""
        L = len(self.P)
        eps = 1e-6
        for k in (range(L - 1) if L > 1 else [0]):
            def ring(r, t):
                pl, _ = self.cut(k, t)
                return [pl[i] for i in self.tris[r]]

            def area2(q):
                return sum(q[i][0] * q[(i + 1) % len(q)][1]
                           - q[(i + 1) % len(q)][0] * q[i][1]
                           for i in range(len(q)))
            for r, ids in enumerate(self.tris):
                sign = 1.0 if area2([self.floor[i] for i in ids]) > 0 else -1.0
                f0, fh, f1 = (sign * area2(ring(r, t)) for t in (0.0, 0.5, 1.0))
                c2, c1 = 2.0 * (f0 - 2.0 * fh + f1), -3.0 * f0 + 4.0 * fh - f1
                least = min(f0, f1)
                if c2 != 0.0 and 0.0 < -c1 / (2.0 * c2) < 1.0:
                    t = -c1 / (2.0 * c2)
                    least = min(least, f0 + c1 * t + c2 * t * t)
                if abs(least) < eps:
                    return ("FLAT", k, r)
                if least < 0.0:
                    return ("TURNED", k, r)
                if len(ids) > 3:
                    for j in range(65):
                        if crosses(ring(r, j / 64.0)):
                            return ("CROSSING", k, r)
            if L == 1:
                continue
            for r, ids in enumerate(self.tris):
                if len(ids) != 3:
                    continue                    # folds: triangles only, here
                sign = 1.0 if area2([self.floor[i] for i in ids]) > 0 else -1.0
                A = [self.P[k][i] + (self.st.H[k][i],) for i in ids]
                B = [self.P[k + 1][i] + (self.st.H[k + 1][i],) for i in ids]
                V = [tuple(b[j] - a[j] for j in range(3)) for a, b in zip(A, B)]
                for j in range(65):
                    t = j / 64.0
                    X = [tuple(a[q] + t * v[q] for q in range(3))
                         for a, v in zip(A, V)]
                    e1 = tuple(X[1][q] - X[0][q] for q in range(3))
                    e2 = tuple(X[2][q] - X[0][q] for q in range(3))
                    for lam in ((1 / 3, 1 / 3, 1 / 3), (0.8, 0.1, 0.1),
                                (0.1, 0.8, 0.1), (0.1, 0.1, 0.8),
                                (0.45, 0.45, 0.1), (0.1, 0.45, 0.45),
                                (0.45, 0.1, 0.45)):
                        ft = tuple(sum(l * v[q] for l, v in zip(lam, V))
                                   for q in range(3))
                        det = (e1[0] * (e2[1] * ft[2] - e2[2] * ft[1])
                               - e1[1] * (e2[0] * ft[2] - e2[2] * ft[0])
                               + e1[2] * (e2[0] * ft[1] - e2[1] * ft[0]))
                        if sign * det < -1e-9:
                            return ("FOLDED", k, r)
        return None

    def weigh(self, p, z):
        L = len(self.P)
        if L == 1:
            lam = self.lam_at(0, 0.0, p)
            return None if lam is None else self.split(lam, 0, 0.0)
        f = self.find(p, z)
        return None if f is None or f[2] is None else self.split(f[2], f[0], f[1])

    def position(self, p, z):
        f = self.find(p, z)
        if f is None:
            return position_at(self.st, {}, z)
        return f[0] + f[1]


def crosses(q):
    """Do two edges of the ring that share no corner cross?"""
    def side(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    n = len(q)
    for i in range(n):
        for j in range(i + 1, n):
            if j == i + 1 or (i == 0 and j == n - 1):
                continue
            a, b, c, d = q[i], q[(i + 1) % n], q[j], q[(j + 1) % n]
            if side(a, b, c) * side(a, b, d) < 0 and \
                    side(c, d, a) * side(c, d, b) < 0:
                return True
    return False


def offs_s(sh, nodes):
    return " ".join(
        f"L{k} " + ";".join(f"{fmt(q[0] - nodes[i][0])},{fmt(q[1] - nodes[i][1])}"
                            for i, q in enumerate(row))
        for k, row in enumerate(sh.P))


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

    # The octahedron's floor plan (3B-2d): a diamond rim N E S W and its
    # centre C, four triangles fanned from C.
    dia = {0: (0.5, 0.8), 1: (0.8, 0.5), 2: (0.5, 0.2), 3: (0.2, 0.5),
           4: (0.5, 0.5)}
    dtris = [(4, 0, 1), (4, 1, 2), (4, 2, 3), (4, 3, 0)]
    out.append("NODES diamond " + " ".join(f"{i}:{fmt(x)},{fmt(y)}"
                                           for i, (x, y) in dia.items()))
    out.append("TRIS diamond " + " ".join(f"{a},{b},{c}" for a, b, c in dtris))
    out.append("")

    stacks = {}

    def stack_rec(name, ops, note=None, plan="square", nodes=4):
        st = Stack(nodes)
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
            elif kind == "MOVE":
                bad = st.move(op[1], op[2])
                results.append(f"MOVE {op[1]} {fmt(op[2])} ="
                               + ("OK" if not bad else
                                  "REFUSED:" + ",".join(map(str, bad))))
            elif kind == "ROOM":
                d, u = st.room(op[1])
                f = (lambda v: "inf" if v == float("inf") else fmt(v))
                results.append(f"ROOM {op[1]} ={f(d)},{f(u)}")
            elif kind == "REMOVE":
                ok = st.remove(op[1])
                results.append(f"REMOVE {op[1]} ="
                               + ("OK" if ok else "REFUSED"))
        if note:
            for line in note:
                out.append("# " + line)
        out.append(f"STACK {name} NODES {plan} OPS " + " ; ".join(results))
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

    out.append("# Round 3A-6b: a WHOLE level moved, its start and every node, bends")
    out.append("# kept; refused, all or nothing, where any node -- or its start --")
    out.append("# would reach a neighbour. ROOM: how far it can go, down and up.")
    stack_rec("moved",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0), ("RAISE", 1, 0.3, [2]),
               ("ROOM", 1), ("ROOM", 0), ("ROOM", 2),
               ("MOVE", 1, -0.25), ("MOVE", 1, 0.2), ("MOVE", 1, -0.25),
               ("MOVE", 2, 0.5), ("MOVE", 0, -0.25), ("ROOM", 1)],
              ["the bent middle level (node 2 at 0.8) has room 0.5 down and",
               "0.2 up -- node 2 is 0.2 under the top. Down 0.25 keeps the",
               "bend (node 2 at 0.55); up 0.2 is fine from there; down 0.25",
               "again. The ends move freely away from their one neighbour,",
               "and the middle's room follows"])
    stack_rec("moved_refused",
              [("COPY", 0, 0.5), ("RAISE", 1, -0.3, [0]),
               ("MOVE", 1, -0.25), ("MOVE", 0, 0.25), ("MOVE", 1, -0.2)],
              ["node 0 bent down to 0.2 on the upper level: moving that level",
               "down 0.25 would put node 0 below the ground -- refused, naming",
               "it; the ground up 0.25 likewise; down 0.2 reaches the ground",
               "-- refused before 3B-2d, a touch now, and allowed. In float",
               "0.5 - 0.3 - 0.2 lands 1.5e-8 below the ground: within kTouch,",
               "it settles on it exactly"])
    stack_rec("moved_bases",
              [("COPY", 0, 0.5), ("RAISE", 1, 0.3, [0, 1, 2, 3]),
               ("MOVE", 0, 0.6), ("MOVE", 0, 0.4)],
              ["every node of the upper level raised to 0.8: the ground can",
               "rise 0.6 without a node touching, but its start would pass the",
               "upper level's start (0.5) -- refused, naming no node; 0.4 fits"])
    out.append("")

    out.append("# REFUSED, changing nothing, and naming the nodes:")
    stack_rec("refuse_reach",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0),
               ("RAISE", 1, 0.6, [2]), ("RAISE", 1, 0.6, [1, 2]),
               ("RAISE", 1, -0.6, [0]),
               ("RAISE", 1, 0.3, [2]), ("RAISE", 1, 0.3, [1, 2])],
              ["raising the middle level's node 2 by 0.6 would PASS the top",
               "level (1.0): refused. (By 0.5 it would touch, refused before",
               "3B-2d and allowed now: see `touch`.) Then",
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
               ("SET", 0, 1, -0.25), ("SET", 1, 0, -0.25), ("SET", 0, 0, 0.25)],
              ["set one node's height, as a file does: 1.5 fits; 0.0 on",
               "level 1 touches the ground -- refused before 3B-2d, allowed",
               "now; -0.25 on the ground fits. Then node 0 on level 1 below",
               "the ground, and the ground's node 0 above level 1's: both",
               "cross, both refused"])
    out.append("")

    out.append("# Round 3B-2d: LEVELS THAT TOUCH. Reaching the next level is a")
    out.append("# touch, allowed; passing it is still refused, all or nothing.")
    stack_rec("touch",
              [("COPY", 0, 0.5), ("RAISE", 1, -0.5, [2]),
               ("RAISE", 1, -0.25, [2]), ("MOVE", 1, -0.25), ("ROOM", 1),
               ("COPY", 0, 0.25), ("MOVE", 1, 0.25), ("ROOM", 1)],
              ["the upper level's node 2 lowered by 0.5 to the ground: a",
               "touch, allowed. Lowered 0.25 more it would pass the ground:",
               "refused. Moving the whole level down: node 2 would pass,",
               "refused; its room down is 0. A copy of the ground at 0.25",
               "would put node 2 above itself on the level over it: refused.",
               "Up 0.25, and the room down is 0.25"])
    stack_rec("touch_bases",
              [("COPY", 0, 0.5), ("MOVE", 0, 0.5), ("ROOM", 0),
               ("MOVE", 0, 0.25), ("ROOM", 1)],
              ["the ground moved up to the level above: every node and the",
               "start touch -- allowed, room 0 up. Further would pass every",
               "node: refused, naming all four"])
    stack_rec("copy_touch_below",
              [("COPY", 0, 0.5), ("RAISE", 0, 0.25, [1]), ("COPY", 1, 0.25)],
              ["a copy may touch the level below it: the upper level copied",
               "to 0.25 meets the ground's node 1, raised to 0.25"])
    stack_rec("copy_touch_above",
              [("COPY", 0, 0.5), ("RAISE", 1, -0.25, [2]), ("COPY", 0, 0.25)],
              ["and the level above it: the ground copied to 0.25 meets the",
               "upper level's node 2, lowered to 0.25"])
    stack_rec("pinch3",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0), ("RAISE", 1, -0.5, [2])],
              ["three levels, the middle one's node 2 down on the ground: a",
               "pinch between the lower two there, the top still above"])
    stack_rec("pinch_top",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0), ("RAISE", 1, 0.5, [2])],
              ["three levels, the middle one's node 2 up at the top: a pinch",
               "between the upper two there"])
    stack_rec("pinch_all",
              [("COPY", 0, 0.5), ("COPY", 1, 1.0), ("RAISE", 1, -0.5, [3]),
               ("RAISE", 2, -1.0, [3])],
              ["three levels all at 0 over node 3: where more than two meet,",
               "the top two share the point"])
    stack_rec("octa",
              [("COPY", 0, 0.5), ("MOVE", 0, -0.5),
               ("RAISE", 0, 0.5, [0, 1, 2, 3]), ("RAISE", 1, -0.5, [0, 1, 2, 3])],
              ["THE OCTAHEDRON on the diamond: the bottom level starts at",
               "-0.5, the top at 0.5; the rim N E S W raised on the bottom and",
               "lowered on the top to 0, where the two meet. The centre stays",
               "at the starts, the apexes. Taller than a regular one."],
              plan="diamond", nodes=5)
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
    out.append("# HEIGHT IN LEVELS (round 3B-2c). UPOS: level position u to a")
    out.append("# height at the point; ZPOS: back. Restrained (R) holds u to")
    out.append("# 0..L-1; extrapolated (X) carries the end cells on. Off the")
    out.append("# floor plan (W -) the levels are flat, at their bases.")
    out.append("#" + "-" * 68)

    def upos(name, cls, sname, lam, u, ex, want=None, note=None):
        for line in note or []:
            out.append("# " + line)
        z = height_at_position(stacks[sname], lam, u, ex)
        if want is not None:
            assert abs(z - want) < 1e-12, (name, z, want)
        out.append(f"UPOS {name} {cls} {sname} W {lam_s(lam) if lam else '-'} "
                   f"U {fmt(u)} {'X' if ex else 'R'} EXPECT {fmt(z)}")
        return z

    def zpos(name, cls, sname, lam, z, ex, want=None, note=None):
        for line in note or []:
            out.append("# " + line)
        u = position_at(stacks[sname], lam, z, ex)
        if want is not None:
            assert abs(u - want) < 1e-12, (name, u, want)
        out.append(f"ZPOS {name} {cls} {sname} W {lam_s(lam) if lam else '-'} "
                   f"Z {fmt(z)} {'X' if ex else 'R'} EXPECT {fmt(u)}")
        return u

    upos("u_one_level", "ANALYTIC", "flat", mid, 1.7, False, 0.0,
         ["one level: u means nothing; the height is that level's"])
    zpos("z_one_level", "ANALYTIC", "flat", mid, 3.0, False, 0.0)
    upos("u_half_cell", "ANALYTIC", "three", mid, 0.5, False, 0.25,
         ["flat levels at 0, 0.5, 1: u = 0.5 half way up the first gap"])
    upos("u_one_and_half", "ANALYTIC", "three", mid, 1.5, False, 0.75)
    upos("u_top", "ANALYTIC", "three", mid, 2.0, False, 1.0)
    upos("u_held_above", "ANALYTIC", "three", mid, 2.4, False, 1.0,
         ["restrained: past the top is the top"])
    upos("u_past_top", "ANALYTIC", "three", mid, 2.4, True, 1.2,
         ["extrapolated: the top gap, 0.5, carried on"])
    upos("u_held_below", "ANALYTIC", "three", mid, -0.5, False, 0.0)
    upos("u_past_bottom", "ANALYTIC", "three", mid, -0.5, True, -0.25)
    upos("u_on_bend", "ANALYTIC", "bent", {2: 1.0}, 1.0, False, 0.8,
         ["at node 2 the middle level is raised to 0.8: on it is 0.8"])
    upos("u_under_bend", "ANALYTIC", "bent", {2: 1.0}, 0.5, False, 0.4,
         ["half way from the ground to the bent middle level"])
    upos("u_over_bend", "ANALYTIC", "bent", {2: 1.0}, 1.5, False, 0.9)
    upos("u_away_from_bend", "ANALYTIC", "bent", {0: 1.0}, 1.0, False, 0.5,
         ["at node 0 the middle level is still at 0.5"])
    upos("u_partial_bend", "CROSS", "bent", {0: 0.2, 1: 0.3, 2: 0.4, 3: 0.1}, 1.0,
         False, 0.62, ["where the bend is partial: 0.5 + 0.4 x 0.3 = 0.62"])
    upos("u_bend_past_top", "CROSS", "bent", {2: 1.0}, 2.5, True, 1.1,
         ["past the top over the bend: the top gap there is 0.2"])
    upos("u_off_plan", "ANALYTIC", "bent", None, 1.5, False, 0.75,
         ["off the floor plan: the bases, flat -- the bend is not there"])
    zpos("z_half_cell", "ANALYTIC", "three", mid, 0.25, False, 0.5)
    zpos("z_upper_cell", "ANALYTIC", "three", mid, 0.9, False, 1.8)
    zpos("z_on_level", "ANALYTIC", "three", mid, 0.5, False, 1.0)
    zpos("z_held_above", "ANALYTIC", "three", mid, 1.2, False, 2.0)
    zpos("z_past_top", "ANALYTIC", "three", mid, 1.2, True, 2.4)
    zpos("z_held_below", "ANALYTIC", "three", mid, -0.3, False, 0.0)
    zpos("z_past_bottom", "ANALYTIC", "three", mid, -0.3, True, -0.6)
    zpos("z_under_bend", "ANALYTIC", "bent", {2: 1.0}, 0.4, False, 0.5)
    zpos("z_over_bend", "ANALYTIC", "bent", {2: 1.0}, 0.9, False, 1.5)
    zpos("z_off_plan", "ANALYTIC", "bent", None, 0.75, False, 1.5)
    out.append("# round trips, u to z to u, every cell, bent and flat, both modes")
    for i, (sname, lam) in enumerate([("bent", {0: 0.2, 1: 0.3, 2: 0.4, 3: 0.1}),
                                      ("copybent", {0: 0.5, 2: 0.5}),
                                      ("between", mid), ("bent", {2: 1.0})]):
        for u in (0.0, 0.3, 1.0, 1.7, 2.0):
            z = height_at_position(stacks[sname], lam, u, False)
            back = position_at(stacks[sname], lam, z, False)
            assert abs(back - u) < 1e-9, (sname, u, back)
            zpos(f"round_{i}_{fmt(u)}", "CROSS", sname, lam, z, False, u)
        for u in (-0.4, 2.6):
            z = height_at_position(stacks[sname], lam, u, True)
            assert abs(position_at(stacks[sname], lam, z, True) - u) < 1e-9
            zpos(f"round_{i}_past_{fmt(u)}", "CROSS", sname, lam, z, True, u)
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

    out.append("#" + "-" * 68)
    out.append("# PINCHES (round 3B-2d). Where two levels are one height at the")
    out.append("# point, a point there is on both: shared equally, t = 0.5. Where")
    out.append("# more meet, the top two. A gap shrinking toward a pinch is")
    out.append("# still divided, never by 0.")
    out.append("#" + "-" * 68)
    eval_rec("pinch_on_ground", "ANALYTIC", "pinch3", {2: 1.0}, 0.0,
             ["over node 2 the middle level is on the ground: z = 0 is on",
              "both, shared equally -- not given to the middle level alone"])
    eval_rec("pinch_above_it", "ANALYTIC", "pinch3", {2: 1.0}, 0.25,
             ["above the pinch, the upper cell, as anywhere"])
    eval_rec("pinch_beside", "CROSS", "pinch3", {1: 0.5, 2: 0.5}, 0.125,
             ["half way to node 1 the middle level is at 0.25: a gap, divided"])
    eval_rec("pinch_three", "ANALYTIC", "pinch_all", {3: 1.0}, 0.0,
             ["three levels meeting at node 3: the top two share it"])
    eval_rec("touch_bases_on", "ANALYTIC", "touch_bases", mid, 0.5,
             ["two levels touching everywhere: every point between them is",
              "on both"])
    eval_rec("octa_rim_vertex", "ANALYTIC", "octa", {1: 1.0}, 0.0,
             ["the octahedron's rim, at node E: on both faces, shared"])
    eval_rec("octa_near_rim", "CROSS", "octa", {1: 0.98, 4: 0.02}, 0.005,
             ["just inside the rim the faces are 0.02 apart: still divided"])
    eval_rec("octa_above_rim", "ANALYTIC", "octa", {1: 1.0}, 0.01,
             ["over the rim nothing is above the faces: outside"])

    # The solid's own geometry: over the diamond, the centre's floor-plan
    # weight is 1 - r, r the diamond's own distance |dx| + |dy| over 0.3,
    # and the faces are at -0.5 (1 - r) and +0.5 (1 - r). Inside exactly
    # when between them -- worked out here from the shape, not the levels.
    def octa_inside(p, z):
        r = (abs(p[0] - 0.5) + abs(p[1] - 0.5)) / 0.3
        if r > 1.0 + 1e-12:
            return False
        return abs(z) <= 0.5 * (1.0 - r) + 1e-6
    out.append("# The octahedron, through the C++ floor plan: each point is")
    out.append("# inside exactly when the solid's own faces hold it")
    for name, cls, p, z in (
            ("octa_centre", "ANALYTIC", (0.5, 0.5), 0.0),
            ("octa_under_apex", "ANALYTIC", (0.5, 0.5), 0.4),
            ("octa_past_apex", "ANALYTIC", (0.5, 0.5), 0.55),
            ("octa_half_out", "CROSS", (0.65, 0.5), 0.2),
            ("octa_half_on_face", "CROSS", (0.65, 0.5), -0.25),
            ("octa_half_past", "CROSS", (0.65, 0.5), 0.3),
            ("octa_rim_edge", "CROSS", (0.65, 0.65), 0.0),
            ("octa_beyond_rim", "ANALYTIC", (0.75, 0.75), 0.0),
            ("octa_lower_quarter", "CROSS", (0.4, 0.45), -0.2)):
        lam = floor_weights(dia, dtris, p)
        w = None if lam is None else stacks["octa"].weigh(lam, z)
        assert (w is not None) == octa_inside(p, z), (name, w)
        out.append(f"EVALXYZ {name} {cls} octa diamond P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} EXPECT {w3_s(w)}")
    out.append("")

    out.append("# EVALU: weights by level position u (3B-2d), as the player")
    out.append("# rides: the cell floor(u), t its fraction -- no division, so")
    out.append("# a pinch is like anywhere else. Away from pinches it must")
    out.append("# agree with EVAL3 at the height u gives.")

    def evalu(name, cls, sname, lam, u, note=None):
        for line in note or []:
            out.append("# " + line)
        st = stacks[sname]
        w = weigh_at_position(st, lam, u)
        out.append(f"EVALU {name} {cls} {sname} W {lam_s(lam) if lam else '-'} "
                   f"U {fmt(u)} EXPECT {w3_s(w)}")
        return w
    for name, sname, lam, u in (("u_eval_half", "three", mid, 0.5),
                                ("u_eval_upper", "bent", {2: 1.0}, 1.5),
                                ("u_eval_partial", "bent",
                                 {0: 0.2, 1: 0.3, 2: 0.4, 3: 0.1}, 0.7),
                                ("u_eval_on_level", "three", mid, 1.0),
                                ("u_eval_top", "bent", {2: 1.0}, 2.0),
                                ("u_eval_bottom", "three", mid, 0.0)):
        w = evalu(name, "CROSS", sname, lam, u)
        z = height_at_position(stacks[sname], lam, u)
        wz = stacks[sname].weigh(lam, z)
        assert set(w) == set(wz) and all(abs(w[k] - wz[k]) < 1e-9 for k in w), \
            (name, w, wz)
    evalu("u_eval_one_level", "ANALYTIC", "flat", mid, 1.7,
          ["one level: u ignored, as z is"])
    evalu("u_eval_past_top", "ANALYTIC", "three", mid, 2.4,
          ["past the top: outside, as above it by z"])
    evalu("u_eval_below", "ANALYTIC", "three", mid, -0.1)
    evalu("u_eval_off_plan", "ANALYTIC", "three", None, 0.5,
          ["off the floor plan: outside"])
    evalu("u_eval_rim", "ANALYTIC", "octa", {1: 1.0}, 0.25,
          ["on the octahedron's rim, a quarter of the way: a quarter on the",
           "top level -- by height it would be ambiguous, by u it is not"])
    evalu("u_eval_apexes", "ANALYTIC", "octa", {4: 1.0}, 0.5,
          ["half way from apex to apex: the centre, shared"])
    evalu("u_eval_pinch3", "ANALYTIC", "pinch3", {2: 1.0}, 0.5)
    out.append("")

    out.append("# AT A PINCH many positions have one height: positionAt gives")
    out.append("# the lowest. Extrapolated through an end cell with no gap,")
    out.append("# there is no slope: the nearer level.")
    zpos("z_pinch_lowest", "ANALYTIC", "pinch3", {2: 1.0}, 0.0, False, 0.0)
    zpos("z_pinch_lowest_x", "ANALYTIC", "pinch3", {2: 1.0}, 0.0, True, 0.0)
    zpos("z_pinch_above", "ANALYTIC", "pinch3", {2: 1.0}, 0.5, False, 1.5)
    zpos("z_pinch_top", "ANALYTIC", "pinch_top", {2: 1.0}, 1.0, False, 1.0,
         ["on the top surface where it touches the middle: the lowest, the",
          "middle -- not the top, as a shortcut for 'at or above the top'",
          "gave until 3B-2e"])
    zpos("z_pinch_top_x", "ANALYTIC", "pinch_top", {2: 1.0}, 1.0, True, 1.0)
    zpos("z_top_plain", "ANALYTIC", "pinch_top", {0: 1.0}, 1.0, False, 2.0,
         ["on the top where nothing touches it: the top"])
    zpos("z_pinch_all", "ANALYTIC", "pinch_all", {3: 1.0}, 0.0, True, 0.0,
         ["three levels at one height: the lowest of them all"])
    zpos("z_octa_rim", "ANALYTIC", "octa", {1: 1.0}, 0.0, False, 0.0)
    zpos("z_octa_rim_above_x", "ANALYTIC", "octa", {1: 1.0}, 0.3, True, 1.0)
    zpos("z_octa_rim_below_x", "ANALYTIC", "octa", {1: 1.0}, -0.3, True, 0.0)
    zpos("z_octa_centre", "ANALYTIC", "octa", {4: 1.0}, 0.25, False, 0.75)
    upos("u_octa_rim", "ANALYTIC", "octa", {1: 1.0}, 0.6, False, 0.0,
         ["anywhere on the rim is the rim's height"])
    upos("u_pinch_all", "ANALYTIC", "pinch_all", {3: 1.0}, 1.5, False, 0.0)
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
    out.append("# A NODE'S OWN PLACE ON EACH LEVEL (round 3B-5a). SHAPED: a stack")
    out.append("# with nodes off their columns, k:i=dx,dy. EVALXYZ and CONT on a")
    out.append("# shaped stack go through the floor plan at each height; EVALSU")
    out.append("# weighs by level position, UPOSS and ZPOSS are height and level")
    out.append("# position, SCOPY and SREMAP what a copied level and a removed")
    out.append("# node do to the places. Every weight record reproduces its point")
    out.append("# from each node's place on its level.")
    out.append("#" + "-" * 68)
    stack_rec("pair", [("COPY", 0, 1.0)], ["two flat levels, at 0 and 1"])
    stack_rec("touch", [("COPY", 0, 0.5), ("RAISE", 0, 0.5, [0])],
              ["two levels touching at node 0: the lower raised to the upper"])
    C = (0.5, 0.5)
    shapes = {}

    def scaled(k, f):
        return {(k, i): ((f - 1.0) * (sq[i][0] - C[0]),
                         (f - 1.0) * (sq[i][1] - C[1])) for i in sq}

    def shaped(name, frm, offs, note):
        for line in note:
            out.append("# " + line)
        sh = Shaped(stacks[frm], sq, tris, offs)
        shapes[name] = sh
        out.append(f"SHAPED {name} FROM {frm} OFFS " + " ".join(
            f"{k}:{i}={fmt(d[0])},{fmt(d[1])}"
            for (k, i), d in sorted(offs.items())))
        return sh

    shaped("funnel", "pair", scaled(1, 1.5),
           ["a funnel: the top level half as wide again about the centre"])
    shaped("hourglass", "three", scaled(1, 0.5),
           ["an hourglass: the middle of three levels half as wide"])
    shaped("onenode", "pair", {(1, 2): (0.2, 0.1)},
           ["one node, node 2, pushed out on the top level alone"])
    bs = scaled(2, 1.25)
    bs[(1, 0)] = (-0.05, -0.05)
    shaped("bentshape", "bent", bs,
           ["bent and shaped: node 2 raised on the middle level, node 0 out",
            "on it, the top level a quarter wider"])
    shaped("touchshape", "touch", {(1, 2): (0.1, 0.1)},
           ["levels touching at node 0, the upper's node 2 pushed out"])

    def closed(sname, scales, p, z):
        """Flat levels each scaled about the centre: no search. t from z,
        the slice's scale from t, the point carried back onto the floor
        plan through it."""
        st = stacks[sname]
        L = len(st.bases)
        if z < st.bases[0] or z > st.bases[-1]:
            return None
        k = max(j for j in range(L - 1) if st.bases[j] <= z)
        t = (z - st.bases[k]) / (st.bases[k + 1] - st.bases[k])
        f = scales[k] + t * (scales[k + 1] - scales[k])
        q = (C[0] + (p[0] - C[0]) / f, C[1] + (p[1] - C[1]) / f)
        lam = floor_weights(sq, tris, q)
        if lam is None:
            return None
        outw = {}
        for i, w in lam.items():
            if w * (1 - t) > 1e-12:
                outw[(i, k)] = w * (1 - t)
            if w * t > 1e-12:
                outw[(i, k + 1)] = w * t
        return outw

    def same(a, b):
        if a is None or b is None:
            return a is None and b is None
        keys = {k for k, v in a.items() if abs(v) > 1e-7} \
            | {k for k, v in b.items() if abs(v) > 1e-7}
        return all(abs(a.get(k, 0.0) - b.get(k, 0.0)) < 1e-7 for k in keys)

    def sxyz(name, sname, p, z, scales=None, base=None, note=None):
        if note:
            for line in note:
                out.append("# " + line)
        w = shapes[sname].weigh(p, z)
        cls = "CROSS"
        if scales is not None:
            c = closed(base, scales, p, z)
            assert same(c, w), (name, c, w)
            w, cls = c, "ANALYTIC"
        if w is not None:
            w = {k: v for k, v in w.items() if abs(v) > 1e-9}
        out.append(f"EVALXYZ {name} {cls} {sname} SQUARE P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} EXPECT {w3_s(w)}")

    fs, hs = [1.0, 1.5], [1.0, 0.5, 1.0]
    sxyz("funnel_mid", "funnel", (0.3, 0.6), 0.5, fs, "pair",
         ["half way up the funnel the slice is 1.25 wide: the point is",
          "weighed where it falls on the floor plan through that slice"])
    sxyz("funnel_low", "funnel", (0.7, 0.2), 0.2, fs, "pair")
    sxyz("funnel_high", "funnel", (0.25, 0.8), 0.9, fs, "pair")
    sxyz("funnel_bottom", "funnel", (0.3, 0.6), 0.0, fs, "pair",
         ["on the bottom level: its own plan, that level alone"])
    sxyz("funnel_top", "funnel", (0.3, 0.6), 1.0, fs, "pair")
    sxyz("funnel_wide_top", "funnel", (0.95, 0.95), 1.0, fs, "pair",
         ["past the floor plan's edge, but inside the wider top level"])
    sxyz("funnel_edge_outside", "funnel", (0.05, 0.5), 0.1, fs, "pair",
         ["outside the slice at this height: the funnel is not yet that wide"])
    sxyz("funnel_edge_inside", "funnel", (0.05, 0.5), 0.5, fs, "pair",
         ["the same (x, y), higher: inside now"])
    sxyz("funnel_above", "funnel", (0.3, 0.6), 1.01, fs, "pair")
    sxyz("funnel_below", "funnel", (0.3, 0.6), -0.1, fs, "pair")
    sxyz("hourglass_centre_low", "hourglass", (0.5, 0.5), 0.25, hs, "three")
    sxyz("hourglass_waist", "hourglass", (0.45, 0.6), 0.5, hs, "three",
         ["on the waist: the middle level alone, at half its width"])
    sxyz("hourglass_upper", "hourglass", (0.4, 0.55), 0.8, hs, "three")
    sxyz("hourglass_corner_low", "hourglass", (0.2, 0.2), 0.1, hs, "three",
         ["a corner: inside near the bottom, where the hourglass is wide"])
    sxyz("hourglass_corner_waist", "hourglass", (0.2, 0.2), 0.4, hs, "three",
         ["the same corner near the waist: outside"])
    sxyz("hourglass_corner_high", "hourglass", (0.2, 0.2), 0.95, hs, "three")
    sxyz("hourglass_just_inside_below", "hourglass", (0.22, 0.3), 0.29, hs,
         "three",
         ["just inside the narrowing wall: the map's edge at this (x, y) is",
          "at 0.3, between the look that finds the point inside and the next,",
          "which finds it outside -- the edge has to be closed in on"])
    sxyz("hourglass_just_inside_above", "hourglass", (0.22, 0.3), 0.71, hs,
         "three", ["the same, above the waist: the edge at 0.7, from above"])
    sxyz("hourglass_just_outside_below", "hourglass", (0.22, 0.3), 0.31, hs,
         "three")
    out.append("# one node pushed out, bends, a pinch: searched for, CROSS")
    sxyz("onenode_mid", "onenode", (0.6, 0.6), 0.5)
    sxyz("onenode_near", "onenode", (0.85, 0.8), 0.7)
    sxyz("onenode_out_low", "onenode", (1.0, 0.92), 0.2,
         note=["where only the pushed-out corner reaches: outside low down"])
    sxyz("onenode_out_high", "onenode", (1.0, 0.92), 0.9)
    sxyz("onenode_other_side", "onenode", (0.2, 0.4), 0.6,
         note=["in the triangle the moved node is no part of: as unshaped"])
    sxyz("bentshape_lower", "bentshape", (0.6, 0.5), 0.3)
    sxyz("bentshape_near_bend", "bentshape", (0.8, 0.8), 0.7)
    sxyz("bentshape_upper", "bentshape", (0.3, 0.4), 0.8)
    sxyz("bentshape_on_middle", "bentshape", (0.1, 0.9), 0.5,
         note=["at node 3, on the middle level: that level alone"])
    sxyz("bentshape_wide_top", "bentshape", (0.05, 0.5), 0.97)
    sxyz("touchshape_pinch", "touchshape", (0.1, 0.1), 0.5,
         note=["at node 0, where the levels touch: shared equally"])
    sxyz("touchshape_open", "touchshape", (0.8, 0.7), 0.3)
    out.append("")

    def su(name, sname, p, u, note=None):
        if note:
            for line in note:
                out.append("# " + line)
        w = shapes[sname].weigh_u(p, u)
        if w is not None:
            w = {k: v for k, v in w.items() if abs(v) > 1e-9}
        out.append(f"EVALSU {name} CROSS {sname} P {fmt(p[0])} {fmt(p[1])} "
                   f"U {fmt(u)} EXPECT {w3_s(w)}")

    su("u_funnel_half", "funnel", (0.3, 0.6), 0.5,
       ["by level position: the slice at u, no search"])
    su("u_funnel_top", "funnel", (0.95, 0.95), 1.0)
    su("u_funnel_outside_slice", "funnel", (0.05, 0.5), 0.1)
    su("u_funnel_past_top", "funnel", (0.3, 0.6), 1.2)
    su("u_hourglass_upper", "hourglass", (0.45, 0.6), 1.5)
    su("u_bentshape", "bentshape", (0.8, 0.8), 0.75)
    su("u_onenode", "onenode", (1.0, 0.92), 0.9)

    def pos(name, sname, p, z, note=None):
        if note:
            for line in note:
                out.append("# " + line)
        sh = shapes[sname]
        u = sh.position(p, z)
        out.append(f"ZPOSS {name} CROSS {sname} P {fmt(p[0])} {fmt(p[1])} "
                   f"Z {fmt(z)} EXPECT {fmt(u)}")
        out.append(f"UPOSS {name}_back CROSS {sname} P {fmt(p[0])} "
                   f"{fmt(p[1])} U {fmt(u)} EXPECT {fmt(sh.height_u(p, u))}")

    pos("pos_funnel", "funnel", (0.3, 0.6), 0.35,
        ["a height to a level position, and back to the height"])
    pos("pos_bentshape_lower", "bentshape", (0.8, 0.8), 0.6)
    pos("pos_bentshape_upper", "bentshape", (0.8, 0.8), 0.9)
    pos("pos_hourglass", "hourglass", (0.45, 0.6), 0.75)
    pos("pos_outside_flat", "hourglass", (0.2, 0.2), 0.4,
        ["outside the slice at that height: the levels taken flat"])
    out.append("")

    for name, sname, a, b, steps in [
            ("cont_funnel", "funnel", (0.2, 0.3, 0.05), (0.8, 0.7, 0.95), 240),
            ("cont_bentshape", "bentshape", (0.3, 0.3, 0.1), (0.75, 0.7, 0.9), 240),
            ("cont_onenode", "onenode", (0.5, 0.4, 0.1), (0.9, 0.85, 0.95), 240)]:
        prev, worst = None, 0.0
        for q in range(steps + 1):
            f = q / steps
            p = tuple(a[j] + (b[j] - a[j]) * f for j in range(3))
            w = shapes[sname].weigh(p[:2], p[2])
            assert w is not None, (name, q)
            if prev is not None:
                for key in set(w) | set(prev):
                    worst = max(worst, abs(w.get(key, 0.0) - prev.get(key, 0.0)))
            prev = w
        out.append(f"CONT {name} CROSS {sname} SQUARE FROM {fmt(a[0])} "
                   f"{fmt(a[1])} {fmt(a[2])} TO {fmt(b[0])} {fmt(b[1])} "
                   f"{fmt(b[2])} STEPS {steps} MAXSTEP {fmt(worst * 1.5 + 1e-4)}")

    out.append("# a level copied takes its shape with it; a node removed, the")
    out.append("# places follow the renumbering")
    sh = shapes["bentshape"]
    cp = Shaped(sh.st, sq, tris, {})
    cp.P = [list(r) for r in sh.P]
    cp.P.insert(3, list(sh.P[2]))
    out.append(f"SCOPY copy_takes_shape ANALYTIC bentshape FROM 2 BASE 1.5 "
               f"EXPECT 3 OFFS {offs_s(cp, sq)}")
    rm = Shaped(sh.st, sq, tris, {})
    keep = [0, 2, 3]
    rm.P = [[r[i] for i in keep] for r in sh.P]
    out.append(f"SREMAP remove_node_1 ANALYTIC bentshape TABLE 0,-,1,2 "
               f"OFFS {offs_s(rm, {j: sq[i] for j, i in enumerate(keep)})}")
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# SOUND SLICES (round 3B-5b). SOUND: the first fault of a shaped")
    out.append("# stack -- FLAT, TURNED, CROSSING or FOLDED, the cell and the")
    out.append("# region -- or OK. STRY: a change tried through the checked doors;")
    out.append("# refused, nothing may have changed.")
    out.append("#" + "-" * 68)
    quad = [(0, 1, 2, 3)]
    out.append("NODES quad " + " ".join(f"{i}:{fmt(x)},{fmt(y)}"
                                        for i, (x, y) in sorted(sq.items())))
    out.append("TRIS quad 0,1,2,3")
    stack_rec("qpair", [("COPY", 0, 1.0)], ["one four-sided region, two levels"],
              plan="quad")
    stack_rec("ramp", [("COPY", 0, 1.0), ("RAISE", 0, 0.5, [1, 2])],
              ["two levels, the lower a ramp: nodes 1 and 2 raised half way"])
    stack_rec("steep", [("COPY", 0, 1.0), ("RAISE", 0, 0.9, [1, 2])],
              ["the same, steeper: raised to 0.9"])

    def rot(k, deg, f=1.0):
        c, s_ = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        o = {}
        for i in sq:
            x, y = sq[i][0] - C[0], sq[i][1] - C[1]
            o[(k, i)] = (f * (c * x - s_ * y) - x, f * (s_ * x + c * y) - y)
        return o

    def shaped_on(name, frm, offs, note, regions=tris):
        for line in note:
            out.append("# " + line)
        sh = Shaped(stacks[frm], sq, regions, offs)
        shapes[name] = sh
        out.append(f"SHAPED {name} FROM {frm} OFFS " + " ".join(
            f"{k}:{i}={fmt(d[0])},{fmt(d[1])}"
            for (k, i), d in sorted(offs.items())))
        return sh

    shift = {(1, i): (0.5, 0.0) for i in sq}
    shaped_on("turned", "pair", {(1, 2): (-0.9, -0.9)},
              ["node 2 dragged past the opposite corner on the top level"])
    shaped_on("spun", "pair", rot(1, 180.0, 1.5),
              ["the top level turned half round and half as wide again: each",
               "level is sound, but 0.4 of the way up every node is at the",
               "centre -- between two of the slices the C++ looks at"])
    shaped_on("quarter", "pair", rot(1, 90.0),
              ["turned a quarter round: narrower half way, never flat"])
    shaped_on("onto", "pair", {(1, 2): (0.0, -0.8)},
              ["node 2 put onto node 1 on the top level"])
    shaped_on("qfunnel", "qpair", scaled(1, 1.5),
              ["the four-sided region as a funnel"], quad)
    shaped_on("bowtie", "qpair", {(1, 2): (-0.9, -0.4)},
              ["node 2 pulled across the far edge on the top level"], quad)
    shaped_on("sheared", "ramp", shift,
              ["a ramp below, the top level half a map to the right: the",
               "columns lean, but less steeply than the ramp climbs"])
    shaped_on("folded", "steep", shift,
              ["the same lean over the steeper ramp: on the right the columns",
               "lean flatter than the ramp, and the volume folds over itself"])
    shaped_on("oneflat", "flat", {(0, 2): (0.2, 0.1)},
              ["one level, node 2 off its column"])
    shaped_on("oneturned", "flat", {(0, 2): (-0.9, -0.9)}, ["and turned over"])
    spin3 = rot(1, 90.0); spin3.update(rot(2, 180.0))
    shaped_on("spiral", "three", spin3,
              ["three levels, each a quarter turn on from the one below"])

    shaped_on("plainpair", "pair", {}, ["two levels, nothing off its column"])

    def fault_s(f):
        return "OK" if f is None else f"{f[0]} {f[1]} {f[2]}"

    for nm in ["funnel", "hourglass", "onenode", "bentshape", "touchshape",
               "turned", "spun", "quarter", "onto", "qfunnel", "bowtie",
               "sheared", "folded", "oneflat", "oneturned", "spiral"]:
        out.append(f"SOUND sound_{nm} CROSS {nm} EXPECT "
                   + fault_s(shapes[nm].fault()))
    assert shapes["spun"].fault()[0] == "FLAT"
    assert shapes["bowtie"].fault()[0] == "CROSSING"
    assert shapes["folded"].fault()[0] == "FOLDED"
    assert shapes["turned"].fault()[0] == "TURNED"
    assert shapes["sheared"].fault() is None and shapes["spiral"].fault() is None

    import copy as _copy

    def stry(name, sname, op, note=None):
        if note:
            for line in note:
                out.append("# " + line)
        sh = _copy.deepcopy(shapes[sname])
        ok = True
        if op[0] == "OFFS":
            for i, d in op[2].items():
                sh.P[op[1]][i] = (sq[i][0] + d[0], sq[i][1] + d[1])
            txt = f"OFFS {op[1]} " + ";".join(
                f"{i}={fmt(d[0])},{fmt(d[1])}" for i, d in sorted(op[2].items()))
        elif op[0] == "RAISE":
            ok = not sh.st.raise_(op[1], op[3], op[2])
            txt = f"RAISE {op[1]} {fmt(op[2])} " + ",".join(map(str, op[3]))
        elif op[0] == "MOVE":
            ok = not sh.st.move(op[1], op[2])
            txt = f"MOVE {op[1]} {fmt(op[2])}"
        elif op[0] == "REMOVE":
            ok = sh.st.remove(op[1])
            del sh.P[op[1]]
            txt = f"REMOVE {op[1]}"
        f = sh.fault() if ok else None
        res = "REFUSED -" if not ok else ("OK" if f is None
                                          else "REFUSED " + fault_s(f))
        out.append(f"STRY {name} CROSS {sname} OP {txt} EXPECT {res}")
        return res

    assert stry("try_push_further", "onenode", ("OFFS", 1, {2: (0.3, 0.2)}),
                ["a node pushed further out: taken"]) == "OK"
    assert stry("try_drag_across", "onenode", ("OFFS", 1, {2: (-0.9, -0.9)}),
                ["dragged across the map: refused, and still where it was"]
                ).startswith("REFUSED")
    assert stry("try_all_together", "plainpair", ("OFFS", 1, dict(
        (i, d) for (k, i), d in rot(1, 90.0).items()))) == "OK"
    stry("try_one_of_a_turn", "plainpair", ("OFFS", 1, {0: rot(1, 90.0)[(1, 0)]}),
         ["one node of that quarter turn on its own: node 0 lands on node 1's",
          "column -- the group is judged by where it ends, a single move too"])
    assert stry("try_steepen_the_ramp", "sheared", ("RAISE", 0, 0.4, [1, 2]),
                ["a height changed: the ramp steepened under leaning columns",
                 "folds the volume, and is refused"]).startswith("REFUSED FOLDED")
    assert stry("try_steepen_a_little", "sheared", ("RAISE", 0, 0.1, [1, 2])) == "OK"
    assert stry("try_lower_the_top", "sheared", ("MOVE", 1, -0.4),
                ["a level moved: the top brought down flattens the lean past",
                 "the ramp's climb"]).startswith("REFUSED FOLDED")
    assert stry("try_remove_the_middle", "spiral", ("REMOVE", 1),
                ["a level removed: its neighbours meet, a half turn apart"]
                ).startswith("REFUSED FLAT")
    stry("try_height_refused_first", "sheared", ("RAISE", 0, 5.0, [1, 2]),
         ["a height refused by the levels' own rule, before any slice"])
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
    maps = build_map3d(out, sq, tris)
    build_files3(out, sq, tris, maps)
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
from reference_outputs import use_rig as _use_rig  # noqa: E402


class Map3:
    registry = {}                      # name -> Map3, for USERIG

    def __init__(self, n):
        self.stack = Stack(n)
        self.maps = [OutMap()]
        self.defs = []                 # [name, fallback, lo, hi]
        self.vals = [{}]               # per level: {(value, node): x}
        self.places = [{}]             # per level: {node: (dx, dy)} (3B-5c)
        self.plan = None               # (nodes, regions), once shaped

    def shape(self):
        """The map as a Shaped: its stack, and each node where it is."""
        nodes, regions = self.plan
        return Shaped(self.stack, nodes, regions,
                      {(k, i): d for k, row in enumerate(self.places)
                       for i, d in row.items()})

    def is_shaped(self):
        return any(d != (0.0, 0.0) for row in self.places for d in row.values())

    def soundly(self, change):
        """3B-5c: make a change on a copy; keep it only if still sound.
        change(m) -> None if done, or the levels' own refusal."""
        trial = _copy.deepcopy(self)
        refused = change(trial)
        if refused is not None:
            return "REFUSED:" + refused
        f = trial.shape().fault() if trial.is_shaped() else None
        if f is not None:
            return f"REFUSED:{f[0]},{f[1]},{f[2]}"
        self.__dict__.update(trial.__dict__)
        return "OK"

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
            self.places.insert(at, dict(self.places[frm]))
            return str(at)
        elif v == "PLACE":
            # 3B-5c: PLACE k i=dx,dy/i=dx,dy -- nodes of a level off their
            # columns, all or none.
            k = int(w[1])

            def put(m):
                for one in w[2].split("/"):
                    i, d = one.split("=")
                    m.places[k][int(i)] = tuple(float(x) for x in d.split(","))
            return self.soundly(put)
        elif v == "SSETH":
            return self.soundly(lambda m: None if m.stack.set(
                int(w[1]), int(w[2]), float(w[3])) else "-")
        elif v == "SMOVE":
            return self.soundly(lambda m: None if not m.stack.move(
                int(w[1]), float(w[2])) else "-")
        elif v == "SCOPY":
            before = len(self.stack.bases)
            got = []

            def cp(m):
                r = m.op(["COPY", w[1], w[2]])
                got.append(r)
                return "-" if r.startswith("REFUSED") else None
            r = self.soundly(cp)
            return got[0] if r == "OK" else r
        elif v == "SREMOVELEVEL":
            return self.soundly(lambda m: None if m.op(
                ["REMOVELEVEL", w[1]]) == "OK" else "-")
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
        elif v == "RANGE":
            # Round 3A-7: a value's range changed after it was made; every
            # number it holds, and its fallback, brought inside.
            lo, hi = float(w[2]), float(w[3])
            d = next((d for d in self.defs if d[0] == w[1]), None)
            if d is None or not lo < hi:
                return "REFUSED"
            clamp = (lambda x: min(hi, max(lo, x)))
            d[1], d[2], d[3] = clamp(d[1]), lo, hi
            for vals in self.vals:
                for key in vals:
                    if key[0] == w[1]:
                        vals[key] = clamp(vals[key])
            return "OK"
        elif v == "RENAMEVALUE":
            d = next((d for d in self.defs if d[0] == w[1]), None)
            if d is None or any(e[0] == w[2] for e in self.defs):
                return "REFUSED"
            d[0] = w[2]
            for k, vals in enumerate(self.vals):
                self.vals[k] = {((w[2] if nm == w[1] else nm), n): x
                                for (nm, n), x in vals.items()}
            return "OK"
        elif v == "DROPVALUE":
            if not any(d[0] == w[1] for d in self.defs):
                return "REFUSED"
            self.defs = [d for d in self.defs if d[0] != w[1]]
            for k, vals in enumerate(self.vals):
                self.vals[k] = {key: x for key, x in vals.items()
                                if key[0] != w[1]}
            return "OK"
        elif v == "SETH":
            ok = self.stack.set(int(w[1]), int(w[2]), float(w[3]))
            return "OK" if ok else "REFUSED"
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
            del self.places[int(w[1])]
            return "OK"
        elif v == "USERIG":
            # The rig is another map's room, as rigOf() makes it: outputs,
            # after-fed derived outputs, and the channels it fills, listed
            # with no sources. Every level moves onto it, or none does.
            src = Map3.registry[w[1]].maps[0]
            rig = OutMap()
            rig.outputs = [list(o) for o in src.outputs]
            rig.derived = [[d[0], d[1], d[2], d[3],
                            list(d[4]) if d[5] == "outputs" else [], d[5]]
                           for d in src.derived]
            moved, bad = [], set()
            for m in self.maps:
                r, refused = _use_rig(m, rig)
                bad |= set(refused)
                moved.append(r)
            if bad:
                return "REFUSED:" + ",".join(sorted(bad))
            self.maps = moved
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
        Map3.registry[name] = m
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
    # Round 3A-6: a rig used on every level at once. "wider" has the forest's
    # outputs in another order, on other channels, plus one more; "narrow"
    # lacks Top.
    map_rec("wider", ["OUT Top 5 1", "OUT R 2 0.25", "OUT L 1 1",
                      "OUT Floor 4 1", "AFTER sub 9 0.8 linear L:1,R:1",
                      "TAP tap 8 1 linear 1:1"],
            ["a room for the forest to move onto: its outputs reordered,",
             "Top on channel 5, R trimmed to a quarter, and Floor besides"])
    map_rec("narrow", ["OUT L 1 1", "OUT R 2 0.5",
                       "AFTER sub 9 0.8 linear L:1,R:1",
                       "TAP tap 8 1 linear 1:1"],
            ["a room without Top"])
    # Round 3A-7: values renamed, re-ranged and removed after they are made.
    map_rec("forest_values", base_ops + [
                "RANGE bright 0.3 0.8", "RENAMEVALUE dist far",
                "RANGE far 1 0", "RANGE far 0.5 0.5", "RENAMEVALUE far bright",
                "RENAMEVALUE gone x", "RANGE gone 0 1",
                "VALUE extra 0.25 0 1", "SETV 1 2 extra 0.75",
                "DROPVALUE extra", "DROPVALUE extra"],
            ["bright's range narrowed to 0.3..0.8: every number it holds,",
             "and its fallback, brought inside (1 to 0.8, 0.1 to 0.3); dist",
             "renamed far, keeping its numbers. Refused: a range upside down",
             "or empty, a name already taken, a value not there. A value",
             "made and removed leaves nothing behind"])
    map_rec("forest_onrig", base_ops + ["USERIG wider"],
            ["the forest on the wider rig: every level moves at once, each",
             "keeping its own bindings by name; the tap keeps the forest's",
             "own sources; heights and values are untouched"])
    map_rec("forest_badrig", base_ops + ["USERIG narrow"],
            ["the narrow rig is refused, naming Top, and nothing changes:",
             "not even the levels that bind nothing to Top"])
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
    out.append("# on the wider rig: Top reads on channel 5, R at its new trim,")
    out.append("# Floor silent, the tap from the forest's own nodes")
    w3_rec("CH3", "onrig_canopy", "ANALYTIC", "forest_onrig", on(2, mid))
    w3_rec("CH3", "onrig_between", "CROSS", "forest_onrig",
           on(1, {0: 0.1, 1: 0.2, 2: 0.3, 3: 0.4}, 0.6))
    out.append("# refused: exactly the forest's own reading")
    w3_rec("CH3", "badrig_unchanged", "CROSS", "forest_badrig",
           on(1, {0: 0.1, 1: 0.2, 2: 0.3, 3: 0.4}, 0.6))
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
            ("bright_onrig", "ANALYTIC", "forest_onrig", on(1, mid, 0.5),
             "bright"),
            # unequal weights on the renumbered nodes 1 and 2: with equal
            # ones, values left at their OLD ids sum to the same answer, and
            # a remap that moved nothing passed
            ("bright_remap", "CROSS", "forest_remap",
             on(0, {0: 0.4, 1: 0.4, 2: 0.2}, 0.5), "bright"),
            ("bright_nomid", "CROSS", "forest_nomid",
             on(0, {0: 0.3, 1: 0.3, 2: 0.2, 3: 0.2}, 0.5), "bright"),
            ("ranged_ground", "ANALYTIC", "forest_values", on(0, mid), "bright"),
            ("ranged_canopy", "ANALYTIC", "forest_values", on(2, mid), "bright"),
            ("ranged_outside", "ANALYTIC", "forest_values", None, "bright"),
            ("renamed_far", "ANALYTIC", "forest_values", on(0, {1: 1.0}), "far")):
        m = maps[mname]
        wtxt = ("OUTSIDE" if w3 is None else
                " ".join(f"{n}@{k}={fmt(w)}" for (n, k), w in sorted(w3.items())))
        out.append(f"VAL3 {name} {cls} {mname} W {wtxt} VALUE {vname} "
                   f"EXPECT {fmt(m.value_at(vname, w3))}")
    return maps


# ---------------------------------------------------------------------------
# round 3A-4c: files for the 3D map (PLAN-round3.md decision G)
#
# The fixtures are written here from the reference's own model, in its own
# words -- json.dumps, not the C++ writer -- so loading them proves the C++
# reads the format, and saving then reloading proves it writes it.
# ---------------------------------------------------------------------------

import json as _json

from reference_outputs import write_fixture  # noqa: E402


def files_of(m3, sq, tris):
    """The pair of files for a Map3 model, as plain JSON."""
    names = [f"n{i}" for i in range(len(sq))]
    mp = {
        "version": 5 if m3.is_shaped() else 4 if m3.stack.touches() else 3,
        "space": "normalized",
        "nodes": [{"id": names[i], "position": [sq[i][0], sq[i][1]],
                   "weight": 1.0} for i in sorted(sq)],
        "regions": [[names[a], names[b], names[c]] for a, b, c in tris],
        "values": [{"name": d[0], "fallback": d[1], "lo": d[2], "hi": d[3]}
                   for d in m3.defs],
        "levels": [],
    }
    for k, base in enumerate(m3.stack.bases):
        lv = {"base": base, "heights": [], "values": []}
        if m3.is_shaped():
            lv["places"] = [{"node": names[i], "dx": d[0], "dy": d[1]}
                            for i, d in sorted(m3.places[k].items())
                            if d != (0.0, 0.0)]
        for i in range(m3.stack.n):
            if m3.stack.H[k][i] != base:
                lv["heights"].append({"node": names[i],
                                      "height": m3.stack.H[k][i]})
        for d in m3.defs:
            for i in range(m3.stack.n):
                x = m3.value(k, i, d[0])
                if x != d[1]:
                    lv["values"].append({"value": d[0], "node": names[i],
                                         "x": x})
        mp["levels"].append(lv)

    room = m3.maps[0]
    outs = {
        "version": 3,
        "outputs": [{"name": o[0], "channel": o[1], "trim": o[2]}
                    for o in room.outputs],
        "bindings": [],
        "aggregators": [],
    }
    for k, m in enumerate(m3.maps):
        for node in sorted(m.links):
            for kind, dest, w in m.links[node]:
                b = {"node": names[node]}
                if k > 0:
                    b["level"] = k
                if kind == "silence":
                    b["kind"] = "silence"
                else:
                    b["target"] = dest
                b["weight"] = w
                outs["bindings"].append(b)
    for name, ch, tr, mode, sources, feed in room.derived:
        a = {"name": name, "mode": mode, "channel": ch, "trim": tr}
        if feed == "outputs":
            a["sources"] = [{"output": s, "weight": g} for s, g in sources]
        else:
            a["sources"] = [{"node": names[s], "weight": g}
                            for s, g in sources]
        outs["aggregators"].append(a)
    return mp, outs


def build_files3(out, sq, tris, maps):
    out.append("")
    out.append("#" + "-" * 68)
    out.append("# FILES FOR THE 3D MAP (round 3A-4c). The usual pair: the map")
    out.append("# file gains 'values' and 'levels' (version 3), the outputs file")
    out.append("# a 'level' on bindings above the first (version 3).")
    out.append("#")
    out.append("# FILE3 loads a pair written HERE, by json.dumps, and checks it")
    out.append("# against the map built by operations: every height, every")
    out.append("# value, the channels on and between every level. Then saves")
    out.append("# it and checks save -> load -> save is byte-stable.")
    out.append("#" + "-" * 68)
    # (not forest_remap: its floor plan has three nodes, the square four)
    for name in ("forest", "forest_trim", "forest_nomid"):
        mp, outs = files_of(maps[name], sq, tris)
        write_fixture(f"map3_{name}.json", mp)
        write_fixture(f"map3_{name}-outputs.json", outs)
        out.append(f"FILE3 file3_{name} ANALYTIC map3_{name}.json "
                   f"map3_{name}-outputs.json AS {name}")
    out.append("# An outputs file from before outputs were declared (version 1):")
    out.append("# each output is made by the first binding naming it, at the next")
    out.append("# channel. Opened as a 3D map, the output is made on EVERY level,")
    out.append("# so the levels agree about the room; the binding is on the first.")
    out.append("# (Found by the editor opening an old pair, round 3A-6.)")
    for name, ops, binds in (
            ("legacy_one", ["OUT L 0 1", "OUT R 1 1", "BIND 0 0 L 1",
                            "BIND 0 1 R 1", "BIND 0 2 L 1"],
             [("n0", "L"), ("n1", "R"), ("n2", "L")]),
            ("legacy_two", ["OUT L 0 1", "COPY 0 1", "BIND 0 0 L 1"],
             [("n0", "L")])):
        m = Map3(4)
        shown = []
        for o in ops:
            r = m.op(o.split())
            shown.append(o + (f" ={r}" if r is not None else ""))
        maps[name] = m
        out.append(f"MAP3 {name} NODES square OPS " + " ; ".join(shown))
        mp, _outs = files_of(m, sq, tris)
        v1 = {"version": 1,
              "bindings": [{"node": n, "target": t, "weight": 1.0}
                           for n, t in binds]}
        write_fixture(f"map3_{name}.json", mp)
        write_fixture(f"map3_{name}-outputs.json", v1)
        out.append(f"FILE3 file3_{name} ANALYTIC map3_{name}.json "
                   f"map3_{name}-outputs.json AS {name}")
    out.append("")

    out.append("# SAVE3: the map built by operations, saved by the C++, with the")
    out.append("# versions it must be written as, reloaded and compared. One")
    out.append("# flat level and no values -- every map before 3D -- must save")
    out.append("# BYTE-IDENTICAL to saveManifold and saveMapping (PLAIN).")
    out.append("SAVE3 save3_forest ANALYTIC forest MAPV 3 OUTV 3")
    out.append("SAVE3 save3_nomid ANALYTIC forest_nomid MAPV 3 OUTV 3")
    out.append("SAVE3 save3_values ANALYTIC forest_values MAPV 3 OUTV 3")
    out.append("# one level, but with values: version 3 for the map file, and")
    out.append("# the outputs file exactly as a 2D map's -- version 3 here only")
    out.append("# because its sub is fed after the fader (3A-1)")
    out.append("SAVE3 save3_single_values ANALYTIC single MAPV 3 OUTV 3")
    m = Map3(4)
    for o in ("OUT L 1 1", "OUT R 2 0.5", "AFTER sub 9 0.8 linear L:1,R:1",
              "BIND 0 0 L 1", "BIND 0 1 R 1", "BIND 0 2 L 0.5",
              "SILENCE 0 2 0.5"):
        m.op(o.split())
    maps["plain"] = m
    out.append("MAP3 plain NODES square OPS OUT L 1 1 ; OUT R 2 0.5 ; "
               "AFTER sub 9 0.8 linear L:1,R:1 ; BIND 0 0 L 1 ; BIND 0 1 R 1 ; "
               "BIND 0 2 L 0.5 ; SILENCE 0 2 0.5")
    out.append("SAVE3 save3_plain ANALYTIC plain MAPV PLAIN OUTV PLAIN")
    out.append("# a single level bent at one node: only version 3 can say so")
    m2 = Map3(4)
    for o in ("OUT L 1 1", "BIND 0 0 L 1", "SETH 0 1 -0.25"):
        m2.op(o.split())
    maps["groundbent"] = m2
    out.append("MAP3 groundbent NODES square OPS OUT L 1 1 ; BIND 0 0 L 1 ; "
               "SETH 0 1 -0.25 =OK")
    out.append("SAVE3 save3_ground_bent ANALYTIC groundbent MAPV 3 OUTV 2")
    out.append("# two flat levels and no values: the levels alone need version 3")
    m3 = Map3(4)
    for o in ("OUT L 1 1", "BIND 0 0 L 1", "COPY 0 1"):
        m3.op(o.split())
    maps["twoflat"] = m3
    out.append("MAP3 twoflat NODES square OPS OUT L 1 1 ; BIND 0 0 L 1 ; "
               "COPY 0 1 =1")
    out.append("SAVE3 save3_two_flat ANALYTIC twoflat MAPV 3 OUTV 3")
    out.append("# one flat level that does not start at 0: copied below the")
    out.append("# ground, then the ground removed. Only version 3 can say where")
    out.append("# it starts.")
    m4 = Map3(4)
    for o in ("OUT L 1 1", "BIND 0 0 L 1", "COPY 0 -0.5", "REMOVELEVEL 1"):
        m4.op(o.split())
    maps["lowered"] = m4
    out.append("MAP3 lowered NODES square OPS OUT L 1 1 ; BIND 0 0 L 1 ; "
               "COPY 0 -0.5 =0 ; REMOVELEVEL 1 =OK")
    out.append("SAVE3 save3_lowered ANALYTIC lowered MAPV 3 OUTV 2")
    out.append("# levels that touch (3B-2d): the map file at version 4, and only")
    out.append("# then -- an older reader refuses it by its version, plainly,")
    out.append("# not by a height it took for crossing")
    m5 = Map3(4)
    for o in ("OUT L 1 1", "BIND 0 0 L 1", "COPY 0 1", "SETH 1 2 0"):
        m5.op(o.split())
    maps["touching"] = m5
    out.append("MAP3 touching NODES square OPS OUT L 1 1 ; BIND 0 0 L 1 ; "
               "COPY 0 1 =1 ; SETH 1 2 0 =OK")
    out.append("SAVE3 save3_touching ANALYTIC touching MAPV 4 OUTV 3")
    mp5, outs5 = files_of(m5, sq, tris)
    write_fixture("map3_touching.json", mp5)
    write_fixture("map3_touching-outputs.json", outs5)
    out.append("FILE3 file3_touching ANALYTIC map3_touching.json "
               "map3_touching-outputs.json AS touching")
    out.append("REFUSEOLD refuseold_touching ANALYTIC map3_touching.json "
               "unsupported version 4")
    out.append("")

    out.append("# A NODE'S OWN PLACE, IN A FILE AND THROUGH THE MAP (3B-5c).")
    out.append("# PLACE and the S- operations go through Map3D's sound doors:")
    out.append("# =OK, or =REFUSED: and what would have broken.")
    C = (0.5, 0.5)

    def ring(k, f):
        return f"PLACE {k} " + "/".join(
            f"{i}={fmt((f - 1.0) * (sq[i][0] - C[0]))},"
            f"{fmt((f - 1.0) * (sq[i][1] - C[1]))}" for i in sorted(sq))

    def map3(name, ops, note):
        for line in note:
            out.append("# " + line)
        m = Map3(4)
        m.plan = (sq, tris)
        shown = []
        for o in ops:
            r = m.op(o.split())
            shown.append(o + (f" ={r}" if r is not None else ""))
        maps[name] = m
        Map3.registry[name] = m
        out.append(f"MAP3 {name} NODES square OPS " + " ; ".join(shown))
        return m, shown

    _, sh = map3("stadium",
                 ["OUT L 1 1", "OUT R 2 1", "OUT up 3 1", "BIND 0 0 L 1",
                  "BIND 0 1 R 1", "BIND 0 2 R 1", "BIND 0 3 L 1", "COPY 0 0.5",
                  "COPY 1 1", "BIND 2 0 up 1", "BIND 2 2 up 1",
                  ring(1, 1.25), ring(2, 1.5), "PLACE 2 2=0.3,0.35",
                  "PLACE 2 2=-1.3,-1.3", "SSETH 1 2 0.7"],
                 ["a stadium: three levels, each wider than the one below, one",
                  "corner of the top pushed further out; then dragged across",
                  "the map (refused, and still where it was); then bent"])
    assert "=REFUSED:TURNED" in sh[-2] and sh[-1].endswith("=OK"), sh
    mp, outs = files_of(maps["stadium"], sq, tris)
    assert mp["version"] == 5
    write_fixture("map3_stadium.json", mp)
    write_fixture("map3_stadium-outputs.json", outs)
    out.append("FILE3 file3_stadium ANALYTIC map3_stadium.json "
               "map3_stadium-outputs.json AS stadium")
    out.append("SAVE3 save3_stadium ANALYTIC stadium MAPV 5 OUTV 3")
    out.append("REFUSEOLD refuseold_stadium ANALYTIC map3_stadium.json "
               "unsupported version 5")
    out.append("# the whole path on the stadium: the point through the floor")
    out.append("# plan at its own height, then the bindings")
    for name, p, z in (("stadium_low", (0.6, 0.3), 0.2),
                       ("stadium_mid", (0.3, 0.7), 0.6),
                       ("stadium_rim", (1.02, 0.5), 0.95),
                       ("stadium_rim_too_low", (1.02, 0.5), 0.3),
                       ("stadium_corner", (1.1, 1.15), 0.99)):
        m = maps["stadium"]
        w3 = m.shape().weigh(p, z)
        routed, sil = m.channels(w3, False)
        levels, _ = m.channels(w3, True)
        out.append(f"CH3XYZ {name} CROSS stadium SQUARE P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} "
                   f"ROUTED {' '.join(fmt(x) for x in routed)} "
                   f"SILENCE {fmt(sil)} "
                   f"LEVELS {' '.join(fmt(x) for x in levels)}")
    out.append("# each node's place on each level: its column, its offset, its")
    out.append("# height there -- where an output is in the room")
    m = maps["stadium"]
    shp = m.shape()
    out.append("PLACES3 places_stadium ANALYTIC stadium " + " ".join(
        f"{k}:{i}={fmt(shp.P[k][i][0])},{fmt(shp.P[k][i][1])},"
        f"{fmt(m.stack.H[k][i])}"
        for k in range(len(shp.P)) for i in sorted(sq)))

    _, sh = map3("unshaped_again",
                 ["OUT L 1 1", "BIND 0 0 L 1", "COPY 0 1", "PLACE 1 2=0.2,0.1",
                  "PLACE 1 2=0,0"],
                 ["a node pushed out and put back: no node off its column,",
                  "and the file is version 3 again"])
    out.append("SAVE3 save3_unshaped_again ANALYTIC unshaped_again MAPV 3 OUTV 3")
    map3("one_shaped", ["OUT L 1 1", "BIND 0 0 L 1", "PLACE 0 2=0.2,0.1"],
         ["one level, one node off its column: version 5 on its own"])
    out.append("SAVE3 save3_one_shaped ANALYTIC one_shaped MAPV 5 OUTV 2")
    map3("touch_shaped",
         ["OUT L 1 1", "BIND 0 0 L 1", "COPY 0 1", "SETH 1 2 0",
          "PLACE 1 0=-0.1,-0.1"],
         ["touching and shaped: version 5 says both"])
    out.append("SAVE3 save3_touch_shaped ANALYTIC touch_shaped MAPV 5 OUTV 3")

    def turn(k, deg):
        c, s_ = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        return f"PLACE {k} " + "/".join(
            f"{i}={fmt(c * (sq[i][0] - .5) - s_ * (sq[i][1] - .5) - (sq[i][0] - .5))},"
            f"{fmt(s_ * (sq[i][0] - .5) + c * (sq[i][1] - .5) - (sq[i][1] - .5))}"
            for i in sorted(sq))

    _, sh = map3("spiral3",
                 ["OUT L 1 1", "BIND 0 0 L 1", "COPY 0 0.5", "COPY 1 1",
                  turn(1, 90), turn(2, 180), "BIND 2 1 L 0.5",
                  "SREMOVELEVEL 1", "SCOPY 2 1.5", "SMOVE 3 -0.2",
                  "SREMOVELEVEL 2"],
                 ["three levels a quarter turn apart. The middle cannot be",
                  "removed -- its neighbours are a half turn apart; a level",
                  "copied, the copy moved, the original removed: the copy has",
                  "its shape and its bindings, the others keep theirs"])
    assert sh[7].endswith("=REFUSED:FLAT,0,0"), sh
    assert sh[8].endswith("=3") and sh[9].endswith("=OK") and sh[10].endswith("=OK"), sh
    out.append("SAVE3 save3_spiral ANALYTIC spiral3 MAPV 5 OUTV 3")
    for name, p, z in (("spiral_low", (0.3, 0.3), 0.05),
                       ("spiral_mid", (0.8, 0.7), 0.5),
                       ("spiral_top", (0.7, 0.4), 1.3)):
        m = maps["spiral3"]
        w3 = m.shape().weigh(p, z)
        assert w3 is not None, name
        routed, sil = m.channels(w3, False)
        levels, _ = m.channels(w3, True)
        out.append(f"CH3XYZ {name} CROSS spiral3 SQUARE P {fmt(p[0])} "
                   f"{fmt(p[1])} {fmt(z)} "
                   f"ROUTED {' '.join(fmt(x) for x in routed)} "
                   f"SILENCE {fmt(sil)} "
                   f"LEVELS {' '.join(fmt(x) for x in levels)}")

    out.append("# a column moved on the floor plan: every level's copy with it;")
    out.append("# refused, and put back, if the floor plan or a slice would break")
    for nm, mv in (("column_ok", "0=0.15,0.05"),
                   ("column_breaks_floor", "2=0,0"),
                   ("column_group", "0=0.2,0.1;1=1,0.1;2=1,0.9;3=0.2,0.9")):
        m = _copy.deepcopy(maps["stadium"])
        nodes2 = dict(sq)
        for one in mv.split(";"):
            i, d = one.split("=")
            nodes2[int(i)] = tuple(float(x) for x in d.split(","))

        def a2(ids, nd):
            q = [nd[i] for i in ids]
            return sum(q[i][0] * q[(i + 1) % 3][1] - q[(i + 1) % 3][0] * q[i][1]
                       for i in range(3))
        floor_ok = all(a2(t, nodes2) > 1e-6 for t in tris)
        m.plan = (nodes2, tris)
        f = m.shape().fault() if floor_ok else None
        res = "REFUSED -" if not floor_ok else (
            "OK" if f is None else f"REFUSED {f[0]} {f[1]} {f[2]}")
        out.append(f"COLUMN3 {nm} CROSS stadium MOVE {mv} EXPECT {res}")
    # A column move that only a slice refuses: the floor plan itself takes
    # it. Looked for on a grid, the first found.
    def a2f(ids, nd):
        q = [nd[i] for i in ids]
        return sum(q[i][0] * q[(i + 1) % 3][1] - q[(i + 1) % 3][0] * q[i][1]
                   for i in range(3))
    found = None
    for mname in ("spiral3", "stadium"):
        for i in sorted(sq):
            for gx in range(-4, 15):
                for gy in range(-4, 15):
                    nodes2 = dict(sq)
                    nodes2[i] = (gx / 10.0 + 0.03, gy / 10.0 + 0.02)
                    if not all(a2f(t, nodes2) > 1e-3 for t in tris):
                        continue
                    m = _copy.deepcopy(maps[mname])
                    m.plan = (nodes2, tris)
                    f = m.shape().fault()
                    if f is not None and found is None:
                        found = (mname, i, nodes2[i], f)
    assert found is not None
    mname, i, q, f = found
    out.append(f"COLUMN3 column_breaks_slice CROSS {mname} MOVE "
               f"{i}={fmt(q[0])},{fmt(q[1])} EXPECT REFUSED {f[0]} {f[1]} {f[2]}")
    out.append("")

    out.append("# REFUSED, each for its own reason, leaving nothing half-read")
    base_map, base_out = files_of(maps["forest"], sq, tris)

    def bad(fname_map, mp_obj, fname_out, out_obj, reason, note):
        if mp_obj is not None:
            write_fixture(fname_map, mp_obj)
        if out_obj is not None:
            write_fixture(fname_out, out_obj)
        out.append("# " + note)
        out.append(f"REFUSE3 {fname_map[:-5]}__{fname_out[:-5]} ANALYTIC "
                   f"{fname_map} {fname_out} {reason}")

    o2 = _json.loads(_json.dumps(base_out))
    o2["bindings"].append({"node": "n0", "level": 3, "target": "L",
                           "weight": 1.0})
    bad("map3_forest.json", None, "bad3_level_too_high-outputs.json", o2,
        "level 3", "a binding on level 3 of a three-level map")
    m3_ = _json.loads(_json.dumps(base_map))
    m3_["levels"][1]["heights"] = [{"node": "n2", "height": 1.2}]
    bad("bad3_cross.json", m3_, "map3_forest-outputs.json", None, "cross",
        "the middle level's node 2 above the canopy")
    m4 = _json.loads(_json.dumps(base_map))
    m4["levels"][1]["base"] = 1.5
    bad("bad3_bases.json", m4, "map3_forest-outputs.json", None,
        "increasing", "starting heights out of order")
    st_map, st_out = files_of(maps["stadium"], sq, tris)
    write_fixture("map3_stadium-outputs.json", st_out)
    s1 = _json.loads(_json.dumps(st_map))
    s1["version"] = 4
    bad("bad3_places_v4.json", s1, "map3_stadium-outputs.json", None,
        "needs version 5", "nodes off their columns in a version 4 file")
    s2 = _json.loads(_json.dumps(st_map))
    s2["levels"][1]["places"].append({"node": "nobody", "dx": 0.1, "dy": 0.0})
    bad("bad3_place_unknown.json", s2, "map3_stadium-outputs.json", None,
        "unknown node", "a place for a node the floor plan does not have")
    s3 = _json.loads(_json.dumps(st_map))
    s3["levels"][2]["places"] = [{"node": "n2", "dx": -1.3, "dy": -1.3}]
    bad("bad3_place_unsound.json", s3, "map3_stadium-outputs.json", None,
        "unsound", "a node dragged across the map on the top level")
    m5 = _json.loads(_json.dumps(base_map))
    m5["levels"][0]["values"].append({"value": "wet", "node": "n0", "x": 1})
    bad("bad3_undeclared.json", m5, "map3_forest-outputs.json", None,
        "never declared", "a value set that was never declared")
    m6 = _json.loads(_json.dumps(base_map))
    m6["values"].append({"name": "bright", "fallback": 0, "lo": 0, "hi": 1})
    bad("bad3_twice.json", m6, "map3_forest-outputs.json", None, "twice",
        "a value declared twice")
    m7 = _json.loads(_json.dumps(base_map))
    m7["levels"][2]["heights"] = [{"node": "n9", "height": 1.1}]
    bad("bad3_unknown_node.json", m7, "map3_forest-outputs.json", None,
        "unknown node", "a height for a node the floor plan does not have")
    m8 = _json.loads(_json.dumps(base_map))
    m8["version"] = 2
    del m8["regions"]
    m8["regions"] = base_map["regions"]
    bad("bad3_levels_v2.json", m8, "map3_forest-outputs.json", None,
        "version 3", "levels in a version 2 map file")
    m9 = _json.loads(_json.dumps(mp5))
    m9["version"] = 3
    bad("bad3_touch_v3.json", m9, "map3_touching-outputs.json", None,
        "need version 4", "levels that touch in a version 3 map file")
    o3 = _json.loads(_json.dumps(base_out))
    o3["bindings"].append({"node": "n0", "level": 1, "target": "Nowhere",
                           "weight": 1.0})
    bad("map3_forest.json", None, "bad3_undeclared_output-outputs.json", o3,
        "undeclared", "a binding to an output the file never declared")
    o4 = _json.loads(_json.dumps(base_out))
    o4["version"] = 2
    o4["aggregators"] = [a for a in o4["aggregators"]
                         if a["sources"] and "node" in a["sources"][0]]
    bad("map3_forest.json", None, "bad3_level_v2-outputs.json", o4,
        "version 3", "a binding with a level in a version 2 outputs file")
    out.append("")
    out.append("# The 2D readers refuse a 3D map's files rather than drop its")
    out.append("# levels: the map file by version, the outputs file by its")
    out.append("# first binding with a level")
    out.append("REFUSE2D refuse2d_mapfile ANALYTIC MAP map3_forest.json "
               "loadMap3D")
    out.append("REFUSE2D refuse2d_outputs ANALYTIC OUTPUTS "
               "map3_forest-outputs.json loadMap3D")
    out.append("REFUSE2D refuse2d_touching ANALYTIC MAP map3_touching.json "
               "loadMap3D")


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
                                            "CH3", "CH3XYZ", "VAL3",
                                            "FILE3", "SAVE3", "REFUSE3",
                                            "REFUSE2D", "UPOS", "ZPOS",
                                            "EVALSU", "UPOSS", "ZPOSS",
                                            "SCOPY", "SREMAP",
                                            "SOUND", "STRY",
                                            "PLACES3", "COLUMN3",
                                            "EVALU", "REFUSEOLD"):
            counts[parts[2]] = counts.get(parts[2], 0) + 1
    print(f"wrote {OUT}")
    for k in sorted(counts):
        print(f"  {k:<9} {counts[k]}")
    print(f"  TOTAL     {sum(counts.values())}")


if __name__ == "__main__":
    main()
