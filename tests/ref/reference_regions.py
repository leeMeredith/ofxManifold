#!/usr/bin/env python3
"""
ofxManifold — independent reference for generalized regions.

Mean-value coordinates over a ring of N >= 3 nodes, following Hormann & Floater
(2006), "Mean value coordinates for arbitrary planar polygons", ACM TOG 25(4).
The three-node case is handled by the ordinary barycentric solve; this file
covers everything and asserts the two agree where they overlap.

FORMULATION. For a point v and a ring v_0 .. v_{n-1}:

    s_i = v_i - v
    r_i = |s_i|
    A_i = signed area of the triangle (v, v_i, v_{i+1})
    D_i = dot(s_i, s_{i+1})

    w_i = (r_{i-1} - D_{i-1}/r_i) / A_{i-1}  +  (r_{i+1} - D_i/r_i) / A_i

then normalized. The robust form, which avoids the tangent half-angles and the
cancellation they bring near an edge.

TWO SPECIAL CASES, both of which the naive formula divides by zero on:

  * v coincident with a vertex -- that vertex takes all the weight
  * v on an edge -- the two endpoints split it linearly and every other
    vertex is zero

They are not decoration. A control point dragged along an edge sits in the
second case continuously, not occasionally.

WHAT THIS FILE DOES NOT DECIDE. Whether a negative weight is acceptable. MVC
over a non-convex region produces them at some interior points, and that is the
algorithm working as designed -- see DECISIONS.md D-016b (D-C). The geometry reports
the relationship; the consumer decides what it means.
"""

import math
import sys

TOL = 1e-6

# Below this a point is treated as coincident with a vertex, or an area as
# degenerate. Chosen the same way kAreaEpsilon was (DECISIONS.md D-001): above
# the float noise floor, well below anything an author would author.
EPS = 1e-9


def fmt(x):
    """Ten significant digits. See DECISIONS.md D-006."""
    return f"{x:.10g}"


# ---------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------

def cross(ax, ay, bx, by):
    return ax * by - bx * ay


def signed_area(poly):
    """Twice the signed area of the ring. Positive for counter-clockwise."""
    n = len(poly)
    return sum(cross(poly[i][0], poly[i][1],
                     poly[(i + 1) % n][0], poly[(i + 1) % n][1])
               for i in range(n))


def is_convex(poly, eps=EPS):
    """
    Every turn the same way round. Reported as INFORMATION, never as a reason
    to refuse a region (DECISIONS.md D-016b (D-C)): a star is a legitimate control
    surface and MVC handles it.
    """
    n = len(poly)
    sign = 0
    for i in range(n):
        a, b, c = poly[i], poly[(i + 1) % n], poly[(i + 2) % n]
        z = cross(b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1])
        if abs(z) < eps:
            continue
        s = 1 if z > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def segments_cross(p1, p2, p3, p4, eps=EPS):
    """Proper intersection of two open segments, excluding shared endpoints."""
    def orient(a, b, c):
        z = cross(b[0] - a[0], b[1] - a[1], c[0] - a[0], c[1] - a[1])
        return 0 if abs(z) < eps else (1 if z > 0 else -1)
    o1, o2 = orient(p1, p2, p3), orient(p1, p2, p4)
    o3, o4 = orient(p3, p4, p1), orient(p3, p4, p2)
    return o1 != o2 and o3 != o4 and o1 != 0 and o2 != 0 and o3 != 0 and o4 != 0


def self_intersects(poly):
    """
    A bowtie has no coherent inside, so its vertex ordering is meaningless
    rather than merely unusual. This is the one shape refused at construction.
    """
    n = len(poly)
    for i in range(n):
        for j in range(i + 1, n):
            if j == i or (j + 1) % n == i or (i + 1) % n == j:
                continue
            if segments_cross(poly[i], poly[(i + 1) % n],
                              poly[j], poly[(j + 1) % n]):
                return True
    return False


# ---------------------------------------------------------------------------
# the two solves
# ---------------------------------------------------------------------------

def barycentric(a, b, c, p):
    """Cramer's rule, as elsewhere in this project's references."""
    v0x, v0y = b[0] - a[0], b[1] - a[1]
    v1x, v1y = c[0] - a[0], c[1] - a[1]
    v2x, v2y = p[0] - a[0], p[1] - a[1]
    det = v0x * v1y - v1x * v0y
    if abs(det) < 1e-12:
        return None
    wB = (v2x * v1y - v1x * v2y) / det
    wC = (v0x * v2y - v2x * v0y) / det
    return (1.0 - wB - wC, wB, wC)


def mvc(poly, p, eps=EPS):
    """
    Mean-value coordinates. Returns a list of n weights summing to one, or
    None if the ring is degenerate.
    """
    n = len(poly)
    if n < 3:
        return None

    s = [(v[0] - p[0], v[1] - p[1]) for v in poly]
    r = [math.hypot(x, y) for x, y in s]

    # Case 1: on a vertex.
    for i in range(n):
        if r[i] < eps:
            w = [0.0] * n
            w[i] = 1.0
            return w

    A = [0.0] * n
    D = [0.0] * n
    for i in range(n):
        j = (i + 1) % n
        A[i] = 0.5 * cross(s[i][0], s[i][1], s[j][0], s[j][1])
        D[i] = s[i][0] * s[j][0] + s[i][1] * s[j][1]

    # Case 2: on an edge. Zero area with the two vertices on opposite sides of
    # the point, which the negative dot product detects.
    for i in range(n):
        j = (i + 1) % n
        if abs(A[i]) < eps and D[i] < 0.0:
            w = [0.0] * n
            total = r[i] + r[j]
            w[i] = r[j] / total
            w[j] = r[i] / total
            return w

    w = [0.0] * n
    for i in range(n):
        h = (i - 1) % n
        j = (i + 1) % n
        if abs(A[h]) > eps:
            w[i] += (r[h] - D[h] / r[i]) / A[h]
        if abs(A[i]) > eps:
            w[i] += (r[j] - D[i] / r[i]) / A[i]

    total = sum(w)
    if abs(total) < 1e-12:
        return None
    return [x / total for x in w]


def evaluate(poly, p):
    """
    What Manifold2D will do: the triangle solve at N = 3, MVC above it.

    Not MVC everywhere. The triangle path has 31 vectors and four mutation
    gates behind it, and is four multiplies and a subtract against MVC's
    divisions and square roots. The agreement between them at N = 3 is
    asserted rather than assumed -- see the N3AGREE vectors.
    """
    if len(poly) == 3:
        return barycentric(poly[0], poly[1], poly[2], p)
    return mvc(poly, p)


# ---------------------------------------------------------------------------
# construction validity
# ---------------------------------------------------------------------------

def why_invalid(poly, ids=None):
    """
    Returns a reason string, or None if the ring is constructible.

    Only genuinely ill-defined rings are refused. Non-convex is NOT one of
    them (DECISIONS.md D-016b (D-C)).
    """
    n = len(poly)
    if n < 3:
        return "fewer than three nodes"
    if ids is not None and len(set(ids)) != len(ids):
        return "repeated node"

    # Self-intersection BEFORE area. A bowtie's two lobes have opposite signed
    # area and cancel to zero, so an area test placed first reports
    # "degenerate" -- the right verdict for the wrong reason, which sends the
    # author looking for a collapsed region instead of a swapped vertex.
    # Same failure as DECISIONS.md D-007.
    if self_intersects(poly):
        return "self-intersecting"
    if abs(signed_area(poly)) < 1e-6:
        return "degenerate: area below epsilon"
    return None


# ---------------------------------------------------------------------------
# shapes used by the vectors
# ---------------------------------------------------------------------------

TRI = [(0.20, 0.20), (0.80, 0.25), (0.45, 0.85)]

QUAD = [(0.15, 0.15), (0.85, 0.20), (0.80, 0.85), (0.20, 0.80)]

PENT = [(0.50 + 0.36 * math.cos(math.pi / 2 + k * 2 * math.pi / 5),
         0.50 + 0.36 * math.sin(math.pi / 2 + k * 2 * math.pi / 5))
        for k in range(5)]


def star(inner):
    return [(0.50 + (0.45 if k % 2 == 0 else inner)
             * math.cos(math.pi / 2 + k * math.pi / 5),
             0.50 + (0.45 if k % 2 == 0 else inner)
             * math.sin(math.pi / 2 + k * math.pi / 5))
            for k in range(10)]


STAR = star(0.19)          # measured: never negative inside
DEEPSTAR = star(0.07)      # measured: negative over much of the interior
LSHAPE = [(0.10, 0.10), (0.90, 0.10), (0.90, 0.40),
          (0.40, 0.40), (0.40, 0.90), (0.10, 0.90)]
# Two lobes of equal, opposite area: the signed total is zero, so an area
# test placed before the self-intersection test reports "degenerate".
BOWTIE = [(0.20, 0.20), (0.80, 0.80), (0.80, 0.20), (0.20, 0.80)]

# A self-intersecting quad whose signed area does NOT cancel -- found by
# searching rather than by construction, after a hand-built "uneven" bowtie
# turned out to cancel to zero like the symmetric one.
#
# |signed area| is 0.6655, so an area test would ACCEPT this ring. Only the
# self-intersection check refuses it, which makes this the vector that proves
# that check runs at all. Without it, self_intersects() could be deleted and
# every other construction vector would still pass.
BOWTIE_UNEVEN = [(0.43, 0.93), (0.94, 0.10), (0.92, 0.14), (0.11, 0.08)]


def poly_s(poly):
    return " ".join(f"{fmt(x)},{fmt(y)}" for x, y in poly)


def inside(poly, p):
    """Ray cast, for sweeping interiors."""
    n = len(poly)
    c = False
    for i in range(n):
        j = (i - 1) % n
        xi, yi = poly[i]
        xj, yj = poly[j]
        if ((yi > p[1]) != (yj > p[1])) and \
           (p[0] < (xj - xi) * (p[1] - yi) / (yj - yi) + xi):
            c = not c
    return c


def sweep_negatives(poly, steps=120):
    """How much of the interior carries a negative weight, and how negative."""
    worst = 1.0
    n_in = n_neg = 0
    for iy in range(steps):
        for ix in range(steps):
            p = (ix / (steps - 1), iy / (steps - 1))
            if not inside(poly, p):
                continue
            n_in += 1
            w = mvc(poly, p)
            if w is None:
                continue
            mn = min(w)
            if mn < worst:
                worst = mn
            if mn < -1e-9:
                n_neg += 1
    return n_in, n_neg, worst


# ---------------------------------------------------------------------------
# vector file
# ---------------------------------------------------------------------------

def build():
    out = []
    out.append("# ofxManifold generalized region conformance vectors")
    out.append("# GENERATED by tests/ref/reference_regions.py -- do not hand edit")
    out.append("#")
    out.append("# Mean-value coordinates over a ring of N >= 3 nodes, after")
    out.append("# Hormann & Floater (2006). The three-node case uses the")
    out.append("# ordinary barycentric solve; the N3AGREE vectors assert the")
    out.append("# two agree, which is what makes this an extension rather")
    out.append("# than a replacement.")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")
    out.append("")

    # ---- N = 3 agreement -------------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# N = 3 AGREEMENT")
    out.append("#")
    out.append("# MVC reduces to barycentric coordinates at three nodes. Not")
    out.append("# approximately -- the worst disagreement across these probes")
    out.append("# is around 1e-16, which is machine epsilon for a double.")
    out.append("#")
    out.append("# This is the property the whole extension rests on. If it")
    out.append("# failed, a map would mean one thing before a fourth node was")
    out.append("# added to a region and something else after.")
    out.append("#" + "-" * 68)
    out.append("")
    worst = 0.0
    for nm, p in [("centroid", ((0.20 + 0.80 + 0.45) / 3,
                                (0.20 + 0.25 + 0.85) / 3)),
                  ("interior", (0.45, 0.40)),
                  ("near_a", (0.24, 0.24)),
                  ("near_edge_ab", (0.50, 0.225)),
                  ("off_centre", (0.60, 0.45))]:
        b = barycentric(*TRI, p)
        m = mvc(TRI, p)
        worst = max(worst, max(abs(x - y) for x, y in zip(b, m)))
        out.append(f"N3AGREE n3_{nm} ANALYTIC POLY {poly_s(TRI)} "
                   f"AT {fmt(p[0])} {fmt(p[1])}")
    out.append(f"# worst disagreement over these probes: {worst:.3e}")
    out.append("")

    # ---- ordinary evaluation --------------------------------------------
    for label, poly, probes, cls in [
        ("quad", QUAD, [("centre", (0.5, 0.5)), ("near_corner", (0.25, 0.25)),
                        ("off", (0.65, 0.40))], "CROSS"),
        ("pentagon", PENT, [("centre", (0.5, 0.5)), ("toward_apex", (0.5, 0.7)),
                            ("off", (0.60, 0.42))], "CROSS"),
        ("star", STAR, [("centre", (0.5, 0.5)), ("arm", (0.5, 0.78)),
                        ("notch", (0.5, 0.33))], "CROSS"),
        ("lshape", LSHAPE, [("long_arm", (0.70, 0.25)),
                            ("tall_arm", (0.25, 0.70)),
                            ("near_reflex", (0.45, 0.45))], "CROSS"),
    ]:
        out.append(f"# {label}, {len(poly)} nodes")
        for nm, p in probes:
            w = evaluate(poly, p)
            out.append(f"EVAL {label}_{nm} {cls} POLY {poly_s(poly)} "
                       f"AT {fmt(p[0])} {fmt(p[1])} "
                       f"EXPECT " + " ".join(fmt(x) for x in w))
        out.append("")

    # ---- partition of unity ---------------------------------------------
    out.append("# Partition of unity across every shape, at several interior")
    out.append("# points. The invariant every layer below the weight vector")
    out.append("# relies on, and the one a new coordinate algorithm is most")
    out.append("# likely to break.")
    for label, poly in [("quad", QUAD), ("pentagon", PENT), ("star", STAR),
                        ("deepstar", DEEPSTAR), ("lshape", LSHAPE)]:
        pts = [(0.5, 0.5), (0.45, 0.42), (0.55, 0.55)]
        for k, p in enumerate(pts):
            if not inside(poly, p):
                continue
            out.append(f"SUMONE sum_{label}_{k} ANALYTIC POLY {poly_s(poly)} "
                       f"AT {fmt(p[0])} {fmt(p[1])}")
    out.append("")

    # ---- the two special cases ------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# SPECIAL CASES")
    out.append("#")
    out.append("# The naive formula divides by zero on both. They are not")
    out.append("# rare: a control point dragged along an edge sits in the")
    out.append("# second one continuously.")
    out.append("#" + "-" * 68)
    out.append("")
    out.append("# on a vertex: that vertex takes everything, the rest are zero")
    for i, v in enumerate(QUAD):
        w = evaluate(QUAD, v)
        out.append(f"EVAL quad_on_vertex_{i} ANALYTIC POLY {poly_s(QUAD)} "
                   f"AT {fmt(v[0])} {fmt(v[1])} "
                   f"EXPECT " + " ".join(fmt(x) for x in w))
    out.append("")
    out.append("# on an edge: the two endpoints split it linearly by distance")
    out.append("# and every other vertex is exactly zero")
    for i in range(len(QUAD)):
        j = (i + 1) % len(QUAD)
        for t, tag in ((0.5, "mid"), (0.25, "quarter")):
            p = (QUAD[i][0] + t * (QUAD[j][0] - QUAD[i][0]),
                 QUAD[i][1] + t * (QUAD[j][1] - QUAD[i][1]))
            w = evaluate(QUAD, p)
            out.append(f"EVAL quad_on_edge_{i}_{tag} ANALYTIC "
                       f"POLY {poly_s(QUAD)} AT {fmt(p[0])} {fmt(p[1])} "
                       f"EXPECT " + " ".join(fmt(x) for x in w))
    out.append("")
    out.append("# a point just off an edge must be close to the point on it:")
    out.append("# the special case has to JOIN the general formula, not sit")
    out.append("# beside it")
    mid = ((QUAD[0][0] + QUAD[1][0]) / 2, (QUAD[0][1] + QUAD[1][1]) / 2)
    nudged = (mid[0], mid[1] + 1e-4)
    out.append(f"CONTINUOUS edge_joins_interior ANALYTIC POLY {poly_s(QUAD)} "
               f"ON {fmt(mid[0])} {fmt(mid[1])} "
               f"NEAR {fmt(nudged[0])} {fmt(nudged[1])} WITHIN 0.01")
    out.append("")

    # ---- invariance: similarity yes, general affine NO -------------------
    out.append("#" + "-" * 68)
    out.append("# INVARIANCE, AND ITS LIMIT")
    out.append("#")
    out.append("# Barycentric coordinates are ratios of AREAS, which any affine")
    out.append("# map preserves. Triangles are affine-invariant (section 7.0).")
    out.append("#")
    out.append("# MVC is built from LENGTHS AND ANGLES, which similarity maps")
    out.append("# preserve and non-uniform scale and shear do not. Polygons are")
    out.append("# only similarity-invariant.")
    out.append("#")
    out.append("# An earlier version of this file claimed MVC was affine")
    out.append("# invariant and wrote the untransformed weights as the expected")
    out.append("# answer for all six maps. The C++ disagreed on two of them. It")
    out.append("# was right: Python and C++ agree on the transformed result to")
    out.append("# about 1e-7, and the expected values were an assumption, not a")
    out.append("# computation. See DECISIONS.md D-017.")
    out.append("#")
    out.append("# The alternative scheme, Wachspress coordinates, IS affine")
    out.append("# invariant but requires strictly convex polygons. MVC was")
    out.append("# chosen because stars are an explicit requirement")
    out.append("# (DECISIONS.md D-016b (D-C)), and that choice gave this up.")
    out.append("#" + "-" * 68)
    out.append("")
    ang = 0.7
    cs, sn = math.cos(ang), math.sin(ang)
    p = (0.52, 0.47)
    base = evaluate(PENT, p)
    for nm, m, similar in [("translate", (1, 0, 0, 1, 0.37, -0.22), True),
                           ("scale", (2.5, 0, 0, 2.5, 0, 0), True),
                           ("rotate", (cs, -sn, sn, cs, 0, 0), True),
                           ("reflect", (-1, 0, 0, 1, 0, 0), True),
                           ("nonuniform", (3.0, 0, 0, 0.4, 0, 0), False),
                           ("shear", (1, 0.8, 0, 1, 0, 0), False)]:
        m00, m01, m10, m11, tx, ty = m
        tp = [(m00 * x + m01 * y + tx, m10 * x + m11 * y + ty)
              for x, y in PENT]
        q = (m00 * p[0] + m01 * p[1] + tx, m10 * p[0] + m11 * p[1] + ty)
        if similar:
            # Invariant: the expected answer is the UNtransformed weights,
            # and that is a property, knowable without computing anything.
            out.append(f"EVAL pent_similar_{nm} ANALYTIC POLY {poly_s(tp)} "
                       f"AT {fmt(q[0])} {fmt(q[1])} "
                       f"EXPECT " + " ".join(fmt(x) for x in base))
        else:
            # Not invariant: the expected answer must be COMPUTED on the
            # transformed polygon. CROSS, because only the reference knows it.
            w = evaluate(tp, q)
            out.append(f"EVAL pent_affine_{nm} CROSS POLY {poly_s(tp)} "
                       f"AT {fmt(q[0])} {fmt(q[1])} "
                       f"EXPECT " + " ".join(fmt(x) for x in w))
            # And the non-invariance itself, asserted: an implementation that
            # somehow WAS affine invariant here would not be computing MVC.
            d = max(abs(a - b) for a, b in zip(base, w))
            out.append(f"# {nm}: weights move by up to {d:.3f} -- MVC is not")
            out.append(f"# affine invariant, and this asserts that it is not")
            out.append(f"VARIES pent_varies_{nm} ANALYTIC POLY {poly_s(tp)} "
                       f"AT {fmt(q[0])} {fmt(q[1])} "
                       f"FROM " + " ".join(fmt(x) for x in base) +
                       f" BYATLEAST 0.05")
    out.append("")

    # Triangles, by contrast, ARE affine invariant -- through the same
    # non-uniform map that moves the pentagon's weights by 0.19.
    tbase = evaluate(TRI, p)
    tp = [(3.0 * x, 0.4 * y) for x, y in TRI]
    q = (3.0 * p[0], 0.4 * p[1])
    out.append("# the same non-uniform map applied to a TRIANGLE leaves its")
    out.append("# weights unchanged. Same map, opposite outcome, and the")
    out.append("# difference is the region's arity.")
    out.append(f"EVAL tri_affine_nonuniform ANALYTIC POLY {poly_s(tp)} "
               f"AT {fmt(q[0])} {fmt(q[1])} "
               f"EXPECT " + " ".join(fmt(x) for x in tbase))
    out.append("")

    # ---- per-node bias at arity > 3 -------------------------------------
    out.append("# Per-node bias must apply to polygon regions exactly as it")
    out.append("# does to triangles: multiply, then renormalize (section 6.3).")
    out.append("# Every bias vector in the manifold suite is on a triangle, so")
    out.append("# a polygon path that ignored bias entirely passed all of them.")
    for nm, bias, p in [("quad_uneven", [2.0, 1.0, 0.5, 1.0], (0.5, 0.5)),
                        ("quad_one_null", [1.0, 1.0, 1.0, 1e-3], (0.4, 0.45)),
                        ("pent_mixed", [1.5, 0.5, 1.0, 2.0, 1.0], (0.52, 0.47))]:
        poly = QUAD if nm.startswith("quad") else PENT
        raw = mvc(poly, p)
        scaled = [w * b for w, b in zip(raw, bias)]
        tot = sum(scaled)
        want = [x / tot for x in scaled]
        out.append(f"EVALBIAS bias_{nm} CROSS POLY {poly_s(poly)} "
                   f"AT {fmt(p[0])} {fmt(p[1])} "
                   f"BIAS " + " ".join(fmt(b) for b in bias) +
                   " EXPECT " + " ".join(fmt(x) for x in want))
    out.append("")

    # ---- containment through the manifold -------------------------------
    out.append("#" + "-" * 68)
    out.append("# CONTAINMENT, THROUGH Manifold2D::evaluate()")
    out.append("#")
    out.append("# Every other region vector calls the solver directly, which")
    out.append("# is how a containment bug stayed invisible: evaluate() tested")
    out.append("# weight sign, which equals geometric containment only for")
    out.append("# convex shapes. 63% of an L-shape's interior was reported")
    out.append("# OUTSIDE, and those were exactly the points carrying negative")
    out.append("# weights -- so the negatives D-C accepted non-convex regions")
    out.append("# for could never reach a consumer (DECISIONS.md D-018).")
    out.append("#")
    out.append("# These go through evaluate(), and each point is inside the")
    out.append("# region geometrically AND carries a negative weight.")
    out.append("#" + "-" * 68)
    out.append("")
    probes = []
    for iy in range(40):
        for ix in range(40):
            p = (ix / 39, iy / 39)
            if not inside(LSHAPE, p):
                continue
            w = mvc(LSHAPE, p)
            if w and min(w) < -1e-4:
                probes.append((p, w))
    # A spread of them, not the first few in scan order.
    picks = [probes[i] for i in range(0, len(probes), max(1, len(probes) // 5))][:5]
    for k, (p, w) in enumerate(picks):
        out.append(f"CONTAINS lshape_negative_inside_{k} ANALYTIC "
                   f"POLY {poly_s(LSHAPE)} AT {fmt(p[0])} {fmt(p[1])} "
                   f"INSIDE 1 NEGATIVE 1 EXPECT " +
                   " ".join(fmt(x) for x in w))
    out.append("")
    out.append("# the deep star, same property")
    sp = []
    for iy in range(60):
        for ix in range(60):
            p = (ix / 59, iy / 59)
            if inside(DEEPSTAR, p):
                w = mvc(DEEPSTAR, p)
                if w and min(w) < -1e-5:
                    sp.append((p, w))
    for k, (p, w) in enumerate(sp[:: max(1, len(sp) // 3)][:3]):
        out.append(f"CONTAINS star_negative_inside_{k} ANALYTIC "
                   f"POLY {poly_s(DEEPSTAR)} AT {fmt(p[0])} {fmt(p[1])} "
                   f"INSIDE 1 NEGATIVE 1 EXPECT " +
                   " ".join(fmt(x) for x in w))
    out.append("")
    out.append("# ON THE BOUNDARY: inside. A bare crossing-number test is")
    out.append("# ambiguous exactly on an edge -- measured, it calls 6 of 12")
    out.append("# edge points of the quad and 12 of 18 of the L-shape OUTSIDE.")
    out.append("# A point dragged along an edge would flicker between the")
    out.append("# region and nothing. The explicit boundary check is what")
    out.append("# prevents that, and every edge point below is one it rescues")
    out.append("# or one it must not break.")
    for label, poly in [("quad", QUAD), ("lshape", LSHAPE)]:
        n = len(poly)
        for i in range(n):
            a, b = poly[i], poly[(i + 1) % n]
            for t in (0.25, 0.5, 0.75):
                q = (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
                out.append(f"CONTAINS {label}_edge_{i}_{int(t*100)} ANALYTIC "
                           f"POLY {poly_s(poly)} AT {fmt(q[0])} {fmt(q[1])} "
                           f"INSIDE 1 NEGATIVE ANY EXPECT")
    out.append("")
    out.append("# and points genuinely OUTSIDE a non-convex region -- in the")
    out.append("# L's notch and between the star's arms -- must stay outside.")
    out.append("# A containment test that just said yes would pass everything")
    out.append("# above and fail here.")
    for nm, poly, p in [("lshape_notch", LSHAPE, (0.7, 0.7)),
                        ("star_between_arms", DEEPSTAR, (0.5 + 0.3 * math.cos(math.pi/2 + math.pi/5),
                                                         0.5 + 0.3 * math.sin(math.pi/2 + math.pi/5)))]:
        assert not inside(poly, p), nm
        out.append(f"CONTAINS {nm}_outside ANALYTIC POLY {poly_s(poly)} "
                   f"AT {fmt(p[0])} {fmt(p[1])} INSIDE 0 NEGATIVE 0 EXPECT")
    out.append("")

    # ---- negatives, measured --------------------------------------------
    out.append("#" + "-" * 68)
    out.append("# NEGATIVE WEIGHTS, MEASURED RATHER THAN DESCRIBED")
    out.append("#")
    out.append("# MVC over a non-convex region produces negative weights at")
    out.append("# some interior points. That is the algorithm working, not a")
    out.append("# fault: Floater and Hormann's paper is titled 'Mean value")
    out.append("# coordinates for arbitrary planar polygons'.")
    out.append("#")
    out.append("# An earlier draft of the plan proposed refusing non-convex")
    out.append("# regions on these grounds. Sweeping the interiors is what")
    out.append("# showed that wrong -- a moderate star never goes negative at")
    out.append("# all, and where negatives do appear they are tiny.")
    out.append("#" + "-" * 68)
    out.append("")
    for label, poly in [("quad", QUAD), ("pentagon", PENT), ("star", STAR),
                        ("deepstar", DEEPSTAR), ("lshape", LSHAPE)]:
        n_in, n_neg, wst = sweep_negatives(poly)
        out.append(f"# {label}: {n_neg} of {n_in} interior samples negative, "
                   f"worst {wst:+.6f}")
        out.append(f"NEGSWEEP neg_{label} ANALYTIC POLY {poly_s(poly)} "
                   f"ANYNEGATIVE {1 if n_neg > 0 else 0}")
    out.append("")

    # ---- construction validity ------------------------------------------
    out.append("# Construction. Only genuinely ill-defined rings are refused.")
    out.append("# A star and an L-shape are legitimate control surfaces.")
    for label, poly, ok, why in [
        ("triangle", TRI, 1, ""),
        ("quad", QUAD, 1, ""),
        ("pentagon", PENT, 1, ""),
        ("star", STAR, 1, "non-convex and ACCEPTED"),
        ("lshape", LSHAPE, 1, "one reflex vertex, ACCEPTED"),
        ("bowtie", BOWTIE, 0, "self-intersecting, and its lobes cancel to "
                              "zero signed area -- so the ORDER of the checks "
                              "decides which reason is reported"),
        ("bowtie_uneven", BOWTIE_UNEVEN, 0,
         "self-intersecting, |signed area| 0.6655 -- an area test would "
         "ACCEPT this, so it is the only vector that proves the "
         "self-intersection check runs at all"),
        ("collinear", [(0.1, 0.1), (0.4, 0.4), (0.7, 0.7)], 0,
         "zero area"),
        ("twopoint", [(0.1, 0.1), (0.9, 0.9)], 0, "fewer than three nodes"),
    ]:
        if why:
            out.append(f"# {label}: {why}")
        reason = why_invalid(poly) or ""
        # The reason is asserted, not just the verdict. A ring refused for the
        # wrong reason sends its author looking for the wrong problem
        # (DECISIONS.md D-007).
        out.append(f"CONSTRUCT construct_{label} ANALYTIC POLY {poly_s(poly)} "
                   f"VALID {ok} REASON "
                   + (reason.split(":")[0].replace(" ", "_") if reason
                      else "none"))
    out.append("")

    # ---- convexity reporting ---------------------------------------------
    out.append("# Convexity is INFORMATION in the topology report, beside")
    out.append("# orphans and duplicates -- never a reason to refuse.")
    for label, poly in [("triangle", TRI), ("quad", QUAD), ("pentagon", PENT),
                        ("star", STAR), ("lshape", LSHAPE)]:
        out.append(f"CONVEX convex_{label} ANALYTIC POLY {poly_s(poly)} "
                   f"IS {1 if is_convex(poly) else 0}")
    return out


def main():
    out = build()
    path = "tests/vectors/regions.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        p = line.split()
        if p and p[0] in ("N3AGREE", "EVAL", "SUMONE", "CONTINUOUS",
                          "NEGSWEEP", "CONSTRUCT", "CONVEX", "VARIES",
                          "EVALBIAS", "CONTAINS"):
            counts[p[2]] = counts.get(p[2], 0) + 1
    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
