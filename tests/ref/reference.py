#!/usr/bin/env python3
"""
ofxManifold — independent reference implementation of triangle barycentric
evaluation, and generator for the conformance vector file.

This exists to be a SECOND implementation, not a copy of the C++ one. It is
therefore written from the linear-system formulation and solved with Cramer's
rule. The C++ kernel uses signed sub-triangle cross products. The two are
algebraically equivalent and structurally different, so agreement between them
is evidence and not tautology.

Vector classes emitted, in decreasing strength:

  ANALYTIC  expected value is known by hand from geometry alone
            (centroid -> thirds, vertex -> 1.0, edge midpoint -> 0.5/0.5/0)
            A failure means the arithmetic is wrong. No appeal.

  CROSS     expected value produced by this reference implementation
            A failure means the two implementations disagree.

  SPEC      expected value follows from a rule we invented (per-node weight
            bias and its renormalization). There is no external authority.
            Green means internally consistent. It does NOT mean correct.

Anyone reading a green run must keep that distinction. It is the whole reason
the class column exists.
"""

from fractions import Fraction
import sys

TOL = 1e-6


def barycentric(a, b, c, p):
    """
    Solve p = wA*a + wB*b + wC*c subject to wA + wB + wC = 1.

    Substituting wA = 1 - wB - wC gives the 2x2 system

        (b - a) * wB + (c - a) * wC = p - a

    solved here by Cramer's rule. Returns (wA, wB, wC) or None if degenerate.
    """
    v0x, v0y = b[0] - a[0], b[1] - a[1]
    v1x, v1y = c[0] - a[0], c[1] - a[1]
    v2x, v2y = p[0] - a[0], p[1] - a[1]

    det = v0x * v1y - v1x * v0y
    if abs(det) < 1e-12:
        return None

    wB = (v2x * v1y - v1x * v2y) / det
    wC = (v0x * v2y - v2x * v0y) / det
    wA = 1.0 - wB - wC
    return (wA, wB, wC)


def biased(weights, node_weights):
    """
    Apply per-node weight bias, then renormalize.

    This is the SPEC rule from section 6.3 of the architecture document: the
    multiplier is applied to the barycentric coordinate before normalization.
    There is no external authority for this behaviour. SpaceMap biases silent
    nodes and does not publish the value; MIAP exposes a multiplier defaulting
    to 1. We chose multiply-then-renormalize. It is a decision, not a finding.
    """
    scaled = [w * nw for w, nw in zip(weights, node_weights)]
    total = sum(scaled)
    if abs(total) < 1e-12:
        return None
    return tuple(s / total for s in scaled)


# ---------------------------------------------------------------------------
# Vector definitions
# ---------------------------------------------------------------------------

# A single canonical triangle in normalized space, deliberately NOT symmetric
# about any axis, so that a transposed index or a swapped subtraction shows up
# rather than cancelling.
TRI = ((0.20, 0.20), (0.80, 0.25), (0.45, 0.85))

# A second triangle sharing edge B-C with the first, for the shared-edge pair.
TRI_D = (0.95, 0.80)   # node D, on the far side of edge B-C


def emit(rows, name, cls, tri, nw, p, expect, note):
    rows.append({
        "name": name, "class": cls, "tri": tri, "nw": nw,
        "p": p, "expect": expect, "note": note,
    })


def build():
    rows = []
    a, b, c = TRI
    unit = (1.0, 1.0, 1.0)

    # -- ANALYTIC ----------------------------------------------------------
    # Centroid of any triangle is exactly (1/3, 1/3, 1/3). Known by hand.
    centroid = ((a[0] + b[0] + c[0]) / 3.0, (a[1] + b[1] + c[1]) / 3.0)
    third = 1.0 / 3.0
    emit(rows, "centroid_thirds", "ANALYTIC", TRI, unit, centroid,
         (third, third, third), "centroid is exactly one third at each vertex")

    for i, (nm, v) in enumerate((("a", a), ("b", b), ("c", c))):
        e = [0.0, 0.0, 0.0]
        e[i] = 1.0
        emit(rows, f"vertex_{nm}_unity", "ANALYTIC", TRI, unit, v, tuple(e),
             f"a point at vertex {nm.upper()} is entirely that vertex")

    # Edge midpoints: the opposite vertex contributes exactly zero, the two
    # endpoints exactly one half. Known by hand.
    mids = (
        ("ab", ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), (0.5, 0.5, 0.0)),
        ("bc", ((b[0] + c[0]) / 2, (b[1] + c[1]) / 2), (0.0, 0.5, 0.5)),
        ("ca", ((c[0] + a[0]) / 2, (c[1] + a[1]) / 2), (0.5, 0.0, 0.5)),
    )
    for nm, p, e in mids:
        emit(rows, f"midpoint_{nm}", "ANALYTIC", TRI, unit, p, e,
             f"midpoint of edge {nm.upper()}: opposite vertex contributes zero")

    # -- CROSS -------------------------------------------------------------
    # Arbitrary interior points. Expected values come from this reference.
    interior = [
        ("interior_1", (0.45, 0.40)),
        ("interior_2", (0.35, 0.30)),
        ("interior_3", (0.60, 0.45)),
        ("interior_4", (0.48, 0.70)),
    ]
    for nm, p in interior:
        w = barycentric(a, b, c, p)
        emit(rows, nm, "CROSS", TRI, unit, p, w,
             "interior point, value from the Python reference")

    # Partition-of-unity is asserted by the runner for every row, but one row
    # explicitly exercises a point very near an edge, where cancellation is
    # most likely to show.
    near_edge = (0.5 * (a[0] + b[0]) + 0.001, 0.5 * (a[1] + b[1]) + 0.001)
    w = barycentric(a, b, c, near_edge)
    emit(rows, "near_edge_ab", "CROSS", TRI, unit, near_edge, w,
         "just inside edge AB; catches cancellation in the solve")

    # -- SPEC --------------------------------------------------------------
    # Per-node weight bias. Our rule, no external authority.
    # At the centroid with bias (2,1,1) the raw thirds cancel, so the result is
    # exactly the normalized bias vector: (0.5, 0.25, 0.25). That much IS hand
    # checkable, which makes it the best available anchor for a spec rule.
    emit(rows, "bias_at_centroid", "SPEC", TRI, (2.0, 1.0, 1.0), centroid,
         (0.5, 0.25, 0.25),
         "equal coords, so result is the normalized bias vector")

    emit(rows, "bias_unity_is_identity", "SPEC", TRI, (1.0, 1.0, 1.0),
         (0.45, 0.40), barycentric(a, b, c, (0.45, 0.40)),
         "bias of 1.0 everywhere must not perturb the result")

    w_raw = barycentric(a, b, c, (0.45, 0.40))
    emit(rows, "bias_asymmetric", "SPEC", TRI, (1.0, 3.0, 0.5), (0.45, 0.40),
         biased(w_raw, (1.0, 3.0, 0.5)),
         "multiply-then-renormalize, from the Python reference")

    # A null node biased toward zero must not produce NaN, only a vanishing
    # share. Guards the renormalization denominator.
    emit(rows, "bias_near_zero", "SPEC", TRI, (1.0, 1.0, 1e-4), (0.45, 0.40),
         biased(w_raw, (1.0, 1.0, 1e-4)),
         "near-zero bias must vanish smoothly, not divide by zero")

    return rows


def build_pair():
    """
    Shared-edge agreement. Two triangles ABC and BCD share edge B-C. A point on
    that edge must produce identical weights for B and C from both, and exactly
    zero for the opposite vertex in each.

    This is ANALYTIC: it follows from the opposite coordinate being zero. It is
    emitted separately because it needs two triangles in one assertion.
    """
    a, b, c = TRI
    d = TRI_D
    p = (0.5 * (b[0] + c[0]), 0.5 * (b[1] + c[1]))
    w_abc = barycentric(a, b, c, p)
    w_bcd = barycentric(b, c, d, p)
    return {
        "p": p,
        "abc": (a, b, c), "w_abc": w_abc,
        "bcd": (b, c, d), "w_bcd": w_bcd,
    }


def fmt(x):
    return f"{x:.17g}"


def main():
    rows = build()
    pair = build_pair()

    out = []
    out.append("# ofxManifold triangle conformance vectors")
    out.append("# GENERATED by tests/ref/reference.py -- do not hand edit")
    out.append("#")
    out.append("# class ANALYTIC : expected value known by hand from geometry")
    out.append("# class CROSS    : expected value from the Python reference")
    out.append("# class SPEC     : expected value follows a rule we invented;")
    out.append("#                  green means self-consistent, not correct")
    out.append("#")
    out.append("# TRI  name class ax ay bx by cx cy  na nb nc  px py  ea eb ec")
    out.append(f"# tolerance {TOL:g}")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")
    out.append("")

    for r in rows:
        (ax, ay), (bx, by), (cx, cy) = r["tri"]
        na, nb, nc = r["nw"]
        px, py = r["p"]
        ea, eb, ec = r["expect"]
        out.append(f"# {r['note']}")
        out.append(
            "TRI " + " ".join([
                r["name"], r["class"],
                fmt(ax), fmt(ay), fmt(bx), fmt(by), fmt(cx), fmt(cy),
                fmt(na), fmt(nb), fmt(nc),
                fmt(px), fmt(py),
                fmt(ea), fmt(eb), fmt(ec),
            ])
        )
        out.append("")

    (ax, ay), (bx, by), (cx, cy) = pair["abc"]
    (dx, dy), (ex, ey), (fx, fy) = pair["bcd"]
    px, py = pair["p"]
    out.append("# shared edge B-C: both triangles must agree on B and C, and")
    out.append("# each must report exactly zero for its opposite vertex")
    out.append(
        "PAIR " + " ".join([
            "shared_edge_bc", "ANALYTIC",
            fmt(ax), fmt(ay), fmt(bx), fmt(by), fmt(cx), fmt(cy),
            fmt(dx), fmt(dy), fmt(ex), fmt(ey), fmt(fx), fmt(fy),
            fmt(px), fmt(py),
        ])
    )
    out.append("")

    # -- construction-time validation ---------------------------------------
    #
    # kAreaEpsilon is 1e-6 on the DOUBLED signed area, i.e. a minimum triangle
    # area of 5e-7. The value is set by float precision: FLT_EPSILON is 1.19e-7,
    # so products of order 1 carry rounding noise near 1e-8, and any threshold
    # below that is noise rather than tolerance.
    #
    # This matters concretely. On Apple Silicon the compiler contracts
    # a*b - c*d into one FMA, and a genuinely collinear triangle yields 4.17e-09
    # instead of zero. An epsilon of 1e-9 accepted it. The same source on x86
    # emitted no FMA, returned exact zero, and passed -- so the earlier green
    # was a platform accident.
    #
    # Note also that 0.7000000001 and 0.7 are the SAME float. An earlier sliver
    # row used them and was therefore a duplicate of the collinear row, testing
    # nothing additional.

    out.append("# exactly collinear: doubled area is zero in exact arithmetic")
    out.append("# and 4.17e-09 under FMA contraction. Must be REJECTED either way.")
    out.append("DEGENERATE collinear_zero_area ANALYTIC "
               "0.1 0.1 0.4 0.4 0.7 0.7")
    out.append("")

    # A genuinely thin triangle: unit base, height 1e-7, doubled area 1e-7.
    # Below kAreaEpsilon, and distinct from the collinear case in float.
    out.append("# thin but not collinear: doubled area 1e-7, below epsilon")
    out.append("DEGENERATE sliver_below_eps ANALYTIC "
               "0.0 0.0 1.0 0.0 0.5 0.0000001")
    out.append("")

    # Unit base, height 1e-5 -> doubled area 1e-5, one order above epsilon.
    out.append("# thin but valid: doubled area 1e-5, ten times epsilon.")
    out.append("# Must be ACCEPTED -- this is the guard against an epsilon")
    out.append("# raised so far that it eats legitimate topology.")
    out.append("ACCEPT thin_but_valid ANALYTIC "
               "0.0 0.0 1.0 0.0 0.5 0.00001")
    out.append("")

    # A cell from a 100x100 normalized grid: doubled area 1e-4.
    out.append("# a cell of a 100x100 normalized grid: doubled area 1e-4.")
    out.append("# Ordinary fine-mesh topology, must be ACCEPTED.")
    out.append("ACCEPT fine_mesh_cell ANALYTIC "
               "0.0 0.0 0.01 0.0 0.0 0.01")
    out.append("")

    # The canonical working triangle, for completeness.
    out.append("# the canonical test triangle: unremarkable, must be ACCEPTED")
    out.append("ACCEPT canonical_triangle ANALYTIC "
               "0.2 0.2 0.8 0.25 0.45 0.85")
    out.append("")

    text = "\n".join(out)
    path = "tests/vectors/triangle.vec"
    with open(path, "w") as f:
        f.write(text)

    counts = {}
    for r in rows:
        counts[r["class"]] = counts.get(r["class"], 0) + 1
    counts["ANALYTIC"] = counts.get("ANALYTIC", 0) + 6  # pair + 2 degenerate + 3 accept

    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
