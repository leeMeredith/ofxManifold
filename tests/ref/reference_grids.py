#!/usr/bin/env python3
"""
ofxManifold — independent reference for authoring grids and node movement.

Three grid modes -- free, lattice, polar -- and the batch move. See
PLAN-authoring.md for the decisions this file encodes.

BRUTE FORCE, DELIBERATELY. The C++ will snap by rounding and then checking a
small neighbourhood. This reference searches a much wider window instead, so it
cannot share that shortcut's mistakes. Measured before this was written:
rounding alone picks a non-nearest point 16% of the time on a triangular grid
and 22% on a five-spoke polar one.

TIES ARE NOT LEFT TO round(). Python rounds halves to even and C++ rounds them
away from zero, so round(0.5) is 0 here and 1 there. Every tie in this file is
resolved by comparing distances and applying the lowest-address rule
explicitly (PLAN-authoring.md decision L).
"""

import math
import sys

TOL = 1e-6
TIE = 1e-9          # distances closer than this are a tie


def fmt(x):
    """Ten significant digits. See DECISIONS.md D-006."""
    return f"{x:.10g}"


# ---------------------------------------------------------------------------
# small linear algebra
# ---------------------------------------------------------------------------

def rot(ang):
    c, s = math.cos(ang), math.sin(ang)
    return ((c, -s), (s, c))


def mv(M, v):
    return (M[0][0] * v[0] + M[0][1] * v[1], M[1][0] * v[0] + M[1][1] * v[1])


def mm(A, B):
    return tuple(tuple(sum(A[i][k] * B[k][j] for k in range(2))
                       for j in range(2)) for i in range(2))


def inv(M):
    (a, b), (c, d) = M
    t = a * d - b * c
    return ((d / t, -b / t), (-c / t, a / t))


def better(d, addr, best):
    """
    Is (d, addr) better than best = (d, addr, point)?

    Strictly closer wins. Within TIE of each other, the lower address wins --
    compared as a tuple, so the rule is total and identical in any language.
    """
    if best is None:
        return True
    if d < best[0] - TIE:
        return True
    if abs(d - best[0]) <= TIE and addr < best[1]:
        return True
    return False


# ---------------------------------------------------------------------------
# grids
# ---------------------------------------------------------------------------

class Free:
    kind = "FREE"

    def snap(self, p):
        return (p[0], p[1]), None

    def spec(self):
        return "FREE"


class Lattice:
    """
    Points origin + i*u + j*v, rotated by `angle` about the origin.

    Basis vectors u and v are COLUMNS. Rotation multiplies both.
    """
    kind = "LATTICE"

    def __init__(self, u, v, origin=(0.0, 0.0), angle=0.0):
        self.u, self.v, self.origin, self.angle = u, v, origin, angle
        B = ((u[0], v[0]), (u[1], v[1]))
        self.B = mm(rot(angle), B)
        self.Binv = inv(self.B)

    def point(self, addr):
        q = mv(self.B, addr)
        return (self.origin[0] + q[0], self.origin[1] + q[1])

    def snap(self, p, window=6):
        rel = (p[0] - self.origin[0], p[1] - self.origin[1])
        c = mv(self.Binv, rel)
        i0, j0 = math.floor(c[0]), math.floor(c[1])
        best = None
        for i in range(i0 - window, i0 + window + 1):
            for j in range(j0 - window, j0 + window + 1):
                q = self.point((i, j))
                d = math.dist(p, q)
                if better(d, (i, j), best):
                    best = (d, (i, j), q)
        return best[2], best[1]

    def spacing(self):
        return min(math.hypot(self.B[0][0], self.B[1][0]),
                   math.hypot(self.B[0][1], self.B[1][1]))

    def spec(self):
        return (f"LATTICE {fmt(self.u[0])} {fmt(self.u[1])} "
                f"{fmt(self.v[0])} {fmt(self.v[1])} "
                f"{fmt(self.origin[0])} {fmt(self.origin[1])} "
                f"{fmt(self.angle)}")


class Polar:
    """
    Rings of spacing dr around a centre, `spokes` equally spaced, the first
    at `angle`. Ring 0 is the centre alone, where angle is undefined.
    """
    kind = "POLAR"

    def __init__(self, centre, dr, spokes, angle=0.0):
        self.c, self.dr, self.spokes, self.angle = centre, dr, spokes, angle

    def point(self, addr):
        ring, spoke = addr
        if ring == 0:
            return self.c
        a = self.angle + spoke * 2.0 * math.pi / self.spokes
        return (self.c[0] + ring * self.dr * math.cos(a),
                self.c[1] + ring * self.dr * math.sin(a))

    def snap(self, p):
        r = math.dist(p, self.c)
        top = int(r / self.dr) + 3
        best = None
        d0 = r
        if better(d0, (0, 0), best):
            best = (d0, (0, 0), self.c)
        for ring in range(1, top + 1):
            for spoke in range(self.spokes):
                q = self.point((ring, spoke))
                d = math.dist(p, q)
                if better(d, (ring, spoke), best):
                    best = (d, (ring, spoke), q)
        return best[2], best[1]

    def spacing(self):
        return self.dr

    def spec(self):
        return (f"POLAR {fmt(self.c[0])} {fmt(self.c[1])} {fmt(self.dr)} "
                f"{self.spokes} {fmt(self.angle)}")


def snap_hysteresis(grid, p, current, dead):
    """
    Keep the current address while the pointer is not CLEARLY closer to
    another -- clearly meaning by more than dead * spacing.

    Near-ties happen constantly during a drag; exact ties almost never. This is
    the rule that stops a snap flickering between two points on a boundary.
    """
    q, addr = grid.snap(p)
    if current is None or addr == current:
        return q, addr
    cq = grid.point(current)
    if math.dist(p, cq) <= math.dist(p, q) + dead * grid.spacing():
        return cq, current
    return q, addr


# ---------------------------------------------------------------------------
# rings, for the batch move
# ---------------------------------------------------------------------------

def area2(ring):
    n = len(ring)
    return sum(ring[i][0] * ring[(i + 1) % n][1] -
               ring[(i + 1) % n][0] * ring[i][1] for i in range(n))


def orient(a, b, c, eps=1e-9):
    z = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
    return 0 if abs(z) < eps else (1 if z > 0 else -1)


def self_intersects(ring):
    n = len(ring)
    for i in range(n):
        for j in range(i + 1, n):
            if (j + 1) % n == i or (i + 1) % n == j:
                continue
            p1, p2 = ring[i], ring[(i + 1) % n]
            p3, p4 = ring[j], ring[(j + 1) % n]
            o1, o2 = orient(p1, p2, p3), orient(p1, p2, p4)
            o3, o4 = orient(p3, p4, p1), orient(p3, p4, p2)
            if o1 != o2 and o3 != o4 and 0 not in (o1, o2, o3, o4):
                return True
    return False


def ring_ok(ring, sign):
    """
    Would this ring be accepted -- by construction's rules, plus its winding?

    PLAN-authoring.md decision K: movement enforces the same rules as
    building. Checking the winding sign alone let a quad be dragged into a
    self-intersecting shape that construction would have refused.
    """
    if self_intersects(ring):
        return False
    a = area2(ring)
    if abs(a) < 1e-6:
        return False
    return (1 if a > 0 else -1) == sign


def batch_move(nodes, regions, moves):
    """
    Apply every move or none. Each region touching a moved node is checked
    against the FINAL positions, not against positions partway through.
    Returns (accepted, final_nodes).
    """
    signs = [1 if area2([nodes[i] for i in r]) > 0 else -1 for r in regions]
    final = dict(nodes)
    final.update(moves)
    moved = set(moves)
    for r, sign in zip(regions, signs):
        if not moved.intersection(r):
            continue
        if not ring_ok([final[i] for i in r], sign):
            return False, dict(nodes)
    return True, final


def one_by_one(nodes, regions, moves):
    """What applying the same moves sequentially through the single-node path
    would do: refuse at the first move that breaks a region, leaving the rest
    unapplied. Used to show the batch move is not the same operation."""
    signs = [1 if area2([nodes[i] for i in r]) > 0 else -1 for r in regions]
    cur = dict(nodes)
    for nid, to in moves.items():
        trial = dict(cur)
        trial[nid] = to
        for r, sign in zip(regions, signs):
            if nid in r and not ring_ok([trial[i] for i in r], sign):
                return False, cur
        cur = trial
    return True, cur


# ---------------------------------------------------------------------------
# vectors
# ---------------------------------------------------------------------------

S = 0.1
H = S * math.sqrt(3) / 2

SQUARE = Lattice((S, 0), (0, S))
RECT = Lattice((S, 0), (0, S * 0.6))
TRI = Lattice((S, 0), (S / 2, H))
SHEAR = Lattice((S, 0), (S * 2.5, S))       # strongly sheared: needs reduction
TRI_ROT = Lattice((S, 0), (S / 2, H), angle=math.radians(17))
POLAR5 = Polar((0.5, 0.5), 0.08, 5, angle=math.pi / 2)
POLAR12 = Polar((0.5, 0.5), 0.05, 12)


def naive_lattice(g, p):
    """The shortcut the C++ must not take alone: round in lattice coords."""
    c = mv(g.Binv, (p[0] - g.origin[0], p[1] - g.origin[1]))
    return g.point((math.floor(c[0] + 0.5), math.floor(c[1] + 0.5)))


def naive_polar(g, p):
    r = math.dist(p, g.c)
    ring = math.floor(r / g.dr + 0.5)
    if ring == 0:
        return g.c
    a = math.atan2(p[1] - g.c[1], p[0] - g.c[0]) - g.angle
    spoke = math.floor(a / (2 * math.pi / g.spokes) + 0.5) % g.spokes
    return g.point((ring, spoke))


def window_unreduced(g, p):
    """Round in the ORIGINAL basis, then check one step each way. What the
    C++ would do if it skipped basis reduction: right almost always, and
    wrong 43 times in 6,000 on a strongly sheared lattice."""
    c = mv(g.Binv, (p[0] - g.origin[0], p[1] - g.origin[1]))
    i0, j0 = math.floor(c[0] + 0.5), math.floor(c[1] + 0.5)
    best = None
    for i in range(i0 - 1, i0 + 2):
        for j in range(j0 - 1, j0 + 2):
            q = g.point((i, j))
            d = math.dist(p, q)
            if better(d, (i, j), best):
                best = (d, (i, j), q)
    return best[2]


def ring_window(g, p):
    """Search only the rounded ring and one either side. What the C++ would
    do if it treated polar like a lattice. Between two spokes the closest
    radius is R cos(pi / spokes), which with few spokes can be two or more
    rings inward."""
    r = math.dist(p, g.c)
    r0 = math.floor(r / g.dr + 0.5)
    best = (r, (0, 0), g.c)
    for ring in range(max(1, r0 - 1), r0 + 2):
        for spoke in range(g.spokes):
            q = g.point((ring, spoke))
            d = math.dist(p, q)
            if better(d, (ring, spoke), best):
                best = (d, (ring, spoke), q)
    return best[2]


def rounding_traps(g, naive, n, seed):
    """Points where naive rounding gives a NON-nearest answer, found by
    searching rather than chosen by hand."""
    import random
    rnd = random.Random(seed)
    found = []
    while len(found) < n:
        p = (rnd.uniform(0.1, 0.9), rnd.uniform(0.1, 0.9))
        q, _ = g.snap(p)
        if math.dist(p, naive(g, p)) - math.dist(p, q) > 1e-6:
            found.append(p)
    return found


def build():
    out = []
    out.append("# ofxManifold authoring grid conformance vectors")
    out.append("# GENERATED by tests/ref/reference_grids.py -- do not hand edit")
    out.append("#")
    out.append("# The reference snaps by BRUTE FORCE over a wide window, so it")
    out.append("# cannot share the rounding shortcut's mistakes. Ties are")
    out.append("# broken explicitly by lowest address, never by round(), since")
    out.append("# Python and C++ round halves differently.")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")
    out.append("")

    # ---- free is the identity ------------------------------------------
    out.append("# FREE mode returns the point unchanged, exactly. It is a mode,")
    out.append("# not an absence: the editor calls snap() everywhere and free")
    out.append("# is the identity, so free-hand cannot quietly snap in one")
    out.append("# code path nobody tested.")
    for k, p in enumerate([(0.123456, 0.654321), (0.0, 1.0), (0.5, 0.5)]):
        out.append(f"SNAP free_{k} ANALYTIC FREE AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(p[0])} {fmt(p[1])} ADDR NONE")
    out.append("")

    # ---- ordinary snapping ----------------------------------------------
    for label, g in [("square", SQUARE), ("rect", RECT), ("tri", TRI),
                     ("shear", SHEAR), ("tri_rot17", TRI_ROT)]:
        out.append(f"# {label}")
        for k, p in enumerate([(0.4237, 0.5891), (0.1111, 0.8888),
                               (0.7351, 0.2468)]):
            q, a = g.snap(p)
            out.append(f"SNAP {label}_{k} CROSS {g.spec()} "
                       f"AT {fmt(p[0])} {fmt(p[1])} "
                       f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
        out.append("")

    # ---- the rounding traps ---------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# ROUNDING TRAPS")
    out.append("#")
    out.append("# Points where rounding in grid coordinates gives a point that")
    out.append("# is NOT the nearest. Found by searching, not hand-picked. Any")
    out.append("# implementation that rounds without checking neighbours fails")
    out.append("# these and passes every square-grid vector above.")
    out.append("#" + "-" * 68)
    out.append("")
    for label, g, naive in [("tri", TRI, naive_lattice),
                            ("shear", SHEAR, naive_lattice),
                            ("tri_rot17", TRI_ROT, naive_lattice),
                            ("polar5", POLAR5, naive_polar),
                            ("polar12", POLAR12, naive_polar)]:
        for k, p in enumerate(rounding_traps(g, naive, 4, hash(label) & 0xffff)):
            q, a = g.snap(p)
            wrong = naive(g, p)
            out.append(f"# naive rounding would give ({wrong[0]:.4f}, "
                       f"{wrong[1]:.4f}), {math.dist(p, wrong) - math.dist(p, q):.4f} "
                       f"further away")
            out.append(f"SNAP trap_{label}_{k} ANALYTIC {g.spec()} "
                       f"AT {fmt(p[0])} {fmt(p[1])} "
                       f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
        out.append("")

    # ---- traps for the two shortcuts that are ALMOST right -------------
    out.append("# SHORTCUTS THAT ARE ALMOST RIGHT")
    out.append("#")
    out.append("# The traps above catch plain rounding. These catch the two")
    out.append("# subtler shortcuts, each of which was measured to be wrong and")
    out.append("# then -- until these existed -- held in place by nothing.")
    out.append("# Mutation testing put both back and the suite stayed green.")
    out.append("")
    out.append("# 1. Checking one step each way WITHOUT reducing the basis first.")
    out.append("#    Right for every sensible grid; wrong on a strongly sheared")
    out.append("#    one, where the true nearest point sits further out.")
    for k, p in enumerate(rounding_traps(SHEAR, window_unreduced, 4, 77)):
        q, a = SHEAR.snap(p)
        out.append(f"SNAP trap_unreduced_{k} ANALYTIC {SHEAR.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
    out.append("")
    out.append("# 2. Searching only one ring either side on a polar grid. A fine")
    out.append("#    five-spoke grid, pointer far out between two spokes: the")
    out.append("#    nearest point is two rings inward.")
    fine5 = Polar((0.5, 0.5), 0.04, 5, angle=math.pi / 2)
    for k, p in enumerate(rounding_traps(fine5, ring_window, 4, 91)):
        q, a = fine5.snap(p)
        r0 = math.floor(math.dist(p, fine5.c) / fine5.dr + 0.5)
        out.append(f"# pointer rounds to ring {r0}; nearest point is on "
                   f"ring {a[0]}")
        out.append(f"SNAP trap_ringwindow_{k} ANALYTIC {fine5.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
    out.append("")

    # ---- polar ----------------------------------------------------------
    out.append("# polar centre: angle is undefined there, and it snaps to the")
    out.append("# centre with address (0, 0) whatever the spoke count")
    for k, p in enumerate([(0.5, 0.5), (0.5001, 0.4999), (0.52, 0.5)]):
        q, a = POLAR5.snap(p)
        out.append(f"SNAP polar_centre_{k} ANALYTIC {POLAR5.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
    out.append("")
    out.append("# a five-spoke grid rotated a quarter turn puts spoke 0 at the")
    out.append("# top -- the star question answered by rotation, not by a")
    out.append("# default (decision H)")
    q, a = POLAR5.snap((0.5, 0.5 + 0.16))
    out.append(f"SNAP polar5_top ANALYTIC {POLAR5.spec()} "
               f"AT 0.5 0.66 EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
    out.append("")

    # ---- ties ----------------------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# EXACT TIES, resolved by lowest address")
    out.append("#")
    out.append("# round(0.5) is 0 in Python and 1 in C++. These points sit")
    out.append("# exactly between grid points, so an implementation relying on")
    out.append("# its language's rounding gets whichever answer that language")
    out.append("# happens to give (decision L).")
    out.append("#" + "-" * 68)
    for k, p in enumerate([(0.05, 0.3), (0.25, 0.45), (0.35, 0.35)]):
        q, a = SQUARE.snap(p)
        out.append(f"SNAP tie_square_{k} ANALYTIC {SQUARE.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(q[0])} {fmt(q[1])} ADDR {a[0]} {a[1]}")
    out.append("")

    # ---- address round trip --------------------------------------------
    out.append("# address -> position -> address returns the same address")
    for label, g in [("square", SQUARE), ("tri", TRI), ("shear", SHEAR),
                     ("tri_rot17", TRI_ROT), ("polar5", POLAR5)]:
        for a in ([(0, 0), (3, 5), (-2, 7)] if g.kind == "LATTICE"
                  else [(0, 0), (2, 3), (4, 1)]):
            out.append(f"ADDRRT addr_{label}_{a[0]}_{a[1]} ANALYTIC {g.spec()} "
                       f"ADDR {a[0]} {a[1]}")
    out.append("")

    # ---- subdivision ---------------------------------------------------
    out.append("# every point of a grid is a point of the grid at half spacing")
    for label, g, gh in [
            ("square", SQUARE, Lattice((S / 2, 0), (0, S / 2))),
            ("tri", TRI, Lattice((S / 2, 0), (S / 4, H / 2)))]:
        out.append(f"NESTS nest_{label} ANALYTIC {g.spec()} INTO {gh.spec()}")
    out.append("")

    # ---- rotation ------------------------------------------------------
    out.append("# rotation changes where the grid sits, not what it is: the")
    out.append("# snap in a rotated grid equals rotating the point, snapping in")
    out.append("# the unrotated grid, and rotating back")
    ang = math.radians(17)
    for k, p in enumerate([(0.4237, 0.5891), (0.31, 0.72)]):
        R, Ri = rot(ang), rot(-ang)
        pu = mv(Ri, p)
        qu, _ = TRI.snap(pu)
        want = mv(R, qu)
        out.append(f"SNAP rotation_equivariant_{k} ANALYTIC {TRI_ROT.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"EXPECT {fmt(want[0])} {fmt(want[1])} ADDR ANY")
    out.append("")

    # ---- hysteresis ----------------------------------------------------
    out.append("# HYSTERESIS: during a drag the current point is kept until")
    out.append("# the pointer is CLEARLY closer to another (decision I)")
    for nm, p, cur, dead, cls in [
            ("keeps_inside_dead_zone", (0.155, 0.2), (1, 2), 0.15, "ANALYTIC"),
            ("moves_when_clearly_closer", (0.19, 0.2), (1, 2), 0.15, "ANALYTIC"),
            ("no_current_uses_nearest", (0.155, 0.2), None, 0.15, "ANALYTIC"),
            ("zero_dead_zone_is_plain_snap", (0.155, 0.2), (1, 2), 0.0,
             "ANALYTIC")]:
        q, a = snap_hysteresis(SQUARE, p, cur, dead)
        c = f"{cur[0]} {cur[1]}" if cur else "NONE"
        out.append(f"HYST hyst_{nm} {cls} {SQUARE.spec()} "
                   f"AT {fmt(p[0])} {fmt(p[1])} CURRENT {c} DEAD {fmt(dead)} "
                   f"ADDR {a[0]} {a[1]}")
    out.append("")

    # ---- batch move ----------------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# BATCH MOVE: all or nothing, checked against FINAL positions")
    out.append("#" + "-" * 68)
    out.append("")
    # Two triangles sharing an edge, and a quad.
    nodes = {0: (0.2, 0.2), 1: (0.5, 0.2), 2: (0.35, 0.5),
             3: (0.6, 0.6), 4: (0.9, 0.6), 5: (0.9, 0.9), 6: (0.6, 0.9)}
    regions = [(0, 1, 2), (3, 4, 5, 6)]
    ns = " ".join(f"{k}:{fmt(v[0])},{fmt(v[1])}" for k, v in nodes.items())
    rs = " ".join(",".join(str(i) for i in r) for r in regions)

    def case(nm, moves, cls, note):
        ok, fin = batch_move(nodes, regions, moves)
        seq_ok, _ = one_by_one(nodes, regions, moves)
        ms = " ".join(f"{k}:{fmt(v[0])},{fmt(v[1])}" for k, v in moves.items())
        out.append(f"# {note}")
        out.append(f"# one-by-one would {'succeed' if seq_ok else 'REFUSE'}; "
                   f"as a batch it is {'ACCEPTED' if ok else 'REFUSED'}")
        out.append(f"BATCH batch_{nm} {cls} NODES {ns} REGIONS {rs} "
                   f"MOVES {ms} ACCEPT {1 if ok else 0}")
        out.append("")

    case("translate_triangle",
         {0: (0.25, 0.25), 1: (0.55, 0.25), 2: (0.40, 0.55)}, "ANALYTIC",
         "the whole triangle shifted together")
    case("order_would_invert",
         {0: (0.55, 0.30), 1: (0.85, 0.30), 2: (0.70, 0.60)}, "ANALYTIC",
         "a triangle slid right by more than its width. Moved one node at a "
         "time, the first move drags node 0 past node 1 and inverts the "
         "triangle, so the sequence stops there. The finished shape is fine, "
         "and as a batch the move is accepted -- the reason decision J exists")
    case("final_inverts",
         {2: (0.35, 0.05)}, "ANALYTIC",
         "node 2 dragged below the base: the final triangle is inverted, "
         "refused, and NOTHING is applied")
    case("quad_self_intersects",
         {5: (0.55, 0.75)}, "ANALYTIC",
         "a quad corner dragged across: winding sign unchanged, ring "
         "self-intersecting. Refused -- decision K. The single-node path "
         "accepted exactly this before")
    case("untouched_region_ignored",
         {3: (0.62, 0.62)}, "ANALYTIC",
         "a small move inside the quad's own region leaves the triangle "
         "untouched and valid")
    return out


def main():
    out = build()
    path = "tests/vectors/grids.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        p = line.split()
        if p and p[0] in ("SNAP", "ADDRRT", "NESTS", "HYST", "BATCH"):
            counts[p[2]] = counts.get(p[2], 0) + 1
    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
