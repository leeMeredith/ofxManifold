#!/usr/bin/env python3
"""
ofxManifold — independent reference for Manifold2D behaviour, and generator for
the manifold conformance vectors.

Containment here uses the EDGE-SIGN test: a point is inside a triangle when it
falls on the same side of all three directed edges. The C++ uses barycentric
non-negativity, which requires the division and the normalization that the
edge-sign test does not. Same predicate, unrelated arithmetic, so agreement is
evidence rather than tautology. This is the same discipline as the triangle
solve, where Python uses Cramer's rule against the C++'s signed sub-areas.

Vector classes, as in triangle.vec:

  ANALYTIC  known by hand from the constructed topology. The T-junction and
            orphan cases are ANALYTIC because the topology is built to contain
            a specific known fault, not because a program found one.
  CROSS     from this reference implementation.
  SPEC      follows a rule we invented -- first-hit ordering on overlap, and
            the treatment of a point exactly on a shared edge. Green means
            self-consistent, NOT correct.
"""

import sys

TOL = 1e-6
EDGE_EPS = 1e-6          # matches kEdgeEpsilon
COLLINEAR_EPS = 1e-5     # matches kCollinearEpsilon


# ---------------------------------------------------------------------------
# Geometry, edge-sign formulation
# ---------------------------------------------------------------------------

def cross(ox, oy, ax, ay, bx, by):
    return (ax - ox) * (by - oy) - (bx - ox) * (ay - oy)


def contains_edge_sign(a, b, c, p):
    """
    Inside if p is on the same side of all three directed edges.

    No division and no normalization, unlike the barycentric test the C++ uses.
    Winding is handled by accepting either all-non-negative or all-non-positive
    rather than by assuming counter-clockwise.
    """
    d1 = cross(a[0], a[1], b[0], b[1], p[0], p[1])
    d2 = cross(b[0], b[1], c[0], c[1], p[0], p[1])
    d3 = cross(c[0], c[1], a[0], a[1], p[0], p[1])

    # Scale the tolerance by the triangle's own size so that it means the same
    # thing for a large region and a small one.
    area2 = abs(cross(a[0], a[1], b[0], b[1], c[0], c[1]))
    eps = EDGE_EPS * max(area2, 1e-12)

    neg = d1 < -eps or d2 < -eps or d3 < -eps
    pos = d1 > eps or d2 > eps or d3 > eps
    return not (neg and pos)


def barycentric(a, b, c, p):
    """Cramer's rule, as in the triangle reference."""
    v0x, v0y = b[0] - a[0], b[1] - a[1]
    v1x, v1y = c[0] - a[0], c[1] - a[1]
    v2x, v2y = p[0] - a[0], p[1] - a[1]
    det = v0x * v1y - v1x * v0y
    if abs(det) < 1e-12:
        return None
    wB = (v2x * v1y - v1x * v2y) / det
    wC = (v0x * v2y - v2x * v0y) / det
    return (1.0 - wB - wC, wB, wC)


def biased(w, nw):
    scaled = [x * y for x, y in zip(w, nw)]
    total = sum(scaled)
    if abs(total) < 1e-12:
        return None
    return tuple(s / total for s in scaled)


def on_segment_interior(a, b, n):
    abx, aby = b[0] - a[0], b[1] - a[1]
    anx, any_ = n[0] - a[0], n[1] - a[1]
    len2 = abx * abx + aby * aby
    if len2 < 1e-12:
        return False
    cr = abx * any_ - aby * anx
    dist = abs(cr) / (len2 ** 0.5)
    if dist > COLLINEAR_EPS:
        return False
    t = (abx * anx + aby * any_) / len2
    return COLLINEAR_EPS < t < 1.0 - COLLINEAR_EPS


# ---------------------------------------------------------------------------
# Reference manifold
# ---------------------------------------------------------------------------

class Manifold:
    def __init__(self):
        self.names = []
        self.pos = []
        self.wt = []
        self.tris = []          # list of (ia, ib, ic)
        self.tri_names = []

    def add_node(self, name, x, y, w=1.0):
        self.names.append(name)
        self.pos.append((x, y))
        self.wt.append(w)
        return len(self.names) - 1

    def add_tri(self, name, a, b, c):
        ia, ib, ic = (self.names.index(a), self.names.index(b),
                      self.names.index(c))
        self.tris.append((ia, ib, ic))
        self.tri_names.append(name)
        return len(self.tris) - 1

    def evaluate(self, p):
        """First hit in insertion order. Returns (region_index, weights) or None."""
        for r, (ia, ib, ic) in enumerate(self.tris):
            if contains_edge_sign(self.pos[ia], self.pos[ib], self.pos[ic], p):
                w = barycentric(self.pos[ia], self.pos[ib], self.pos[ic], p)
                if w is None:
                    continue
                w = biased(w, (self.wt[ia], self.wt[ib], self.wt[ic]))
                return (r, [(self.names[ia], w[0]),
                            (self.names[ib], w[1]),
                            (self.names[ic], w[2])])
        return None

    def validate(self):
        tj = []
        for r, ids in enumerate(self.tris):
            for e in range(3):
                ia, ib = ids[e], ids[(e + 1) % 3]
                for n in range(len(self.names)):
                    if n in ids:
                        continue
                    if on_segment_interior(self.pos[ia], self.pos[ib],
                                           self.pos[n]):
                        tj.append((n, r, ia, ib))
        used = set()
        for ids in self.tris:
            used.update(ids)
        orphans = [n for n in range(len(self.names)) if n not in used]

        dup = []
        seen = []
        for r, ids in enumerate(self.tris):
            key = tuple(sorted(ids))
            if key in seen:
                dup.append(r)
            seen.append(key)
        return tj, orphans, dup


# ---------------------------------------------------------------------------
# Test manifolds
# ---------------------------------------------------------------------------

def fmt(x):
    return f"{x:.17g}"


def emit_manifold(out, name, m, queries, topo_class="ANALYTIC", note=""):
    out.append("")
    if note:
        out.append(f"# {note}")
    out.append(f"MANIFOLD {name}")
    for i, nm in enumerate(m.names):
        x, y = m.pos[i]
        out.append(f"NODE {nm} {fmt(x)} {fmt(y)} {fmt(m.wt[i])}")
    for i, (ia, ib, ic) in enumerate(m.tris):
        out.append(f"TRI {m.tri_names[i]} {m.names[ia]} {m.names[ib]} "
                   f"{m.names[ic]}")

    for q in queries:
        qname, cls, p, qnote = q
        res = m.evaluate(p)
        line = f"QUERY {qname} {cls} {fmt(p[0])} {fmt(p[1])} "
        if res is None:
            line += "NONE"
        else:
            r, ws = res
            line += m.tri_names[r]
            for nm, w in ws:
                line += f" {nm}={fmt(w)}"
        if qnote:
            out.append(f"# {qnote}")
        out.append(line)

    tj, orphans, dup = m.validate()
    out.append(f"TOPOLOGY {topo_class} {len(tj)} {len(orphans)} {len(dup)}")
    out.append("END")


def build():
    out = []
    out.append("# ofxManifold Manifold2D conformance vectors")
    out.append("# GENERATED by tests/ref/reference_manifold.py -- do not hand edit")
    out.append("#")
    out.append("# Containment in this reference uses the EDGE-SIGN test; the C++")
    out.append("# uses barycentric non-negativity. Different arithmetic, same")
    out.append("# predicate, so agreement is evidence rather than tautology.")
    out.append("#")
    out.append("# TOPOLOGY <class> <tjunctions> <orphans> <duplicates>")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")

    # -- 1. single triangle -------------------------------------------------
    m = Manifold()
    m.add_node("A", 0.20, 0.20)
    m.add_node("B", 0.80, 0.25)
    m.add_node("C", 0.45, 0.85)
    m.add_tri("T0", "A", "B", "C")
    cx = (0.20 + 0.80 + 0.45) / 3.0
    cy = (0.20 + 0.25 + 0.85) / 3.0
    emit_manifold(out, "single_triangle", m, [
        ("centroid", "ANALYTIC", (cx, cy),
         "centroid: exact thirds, and the containing region must be T0"),
        ("at_vertex_a", "ANALYTIC", (0.20, 0.20),
         "a vertex is contained, and is entirely itself"),
        ("interior", "CROSS", (0.45, 0.40), ""),
        ("outside_left", "ANALYTIC", (0.02, 0.50),
         "outside the hull: inside == false, empty weights, no clamping"),
        ("outside_below", "ANALYTIC", (0.50, 0.02), ""),
    ], note="one triangle, clean topology")

    # -- 2. two triangles sharing an edge ----------------------------------
    # Quad A B C D split along B-C. Weight continuity across the shared edge
    # is the property that makes a manifold usable as a control surface.
    m = Manifold()
    m.add_node("A", 0.10, 0.10)
    m.add_node("B", 0.90, 0.10)
    m.add_node("C", 0.50, 0.90)
    m.add_node("D", 0.95, 0.85)
    m.add_tri("T0", "A", "B", "C")
    m.add_tri("T1", "B", "D", "C")
    emit_manifold(out, "shared_edge", m, [
        ("in_first", "CROSS", (0.40, 0.30), ""),
        ("in_second", "CROSS", (0.80, 0.55), ""),
        ("on_shared_edge", "SPEC", (0.70, 0.50),
         "on edge B-C: both regions contain it, first-hit gives T0."
         " Ordering is our rule, hence SPEC"),
        ("near_edge_first", "CROSS", (0.699, 0.499), ""),
        ("near_edge_second", "CROSS", (0.701, 0.501), ""),
    ], note="two triangles sharing edge B-C, conforming, clean")

    # -- 3. deliberate T-junction ------------------------------------------
    # M sits exactly on the midpoint of edge A-B of T0, but is not one of T0's
    # vertices. T1 uses it. Crossing A-B near M jumps discontinuously.
    m = Manifold()
    m.add_node("A", 0.10, 0.10)
    m.add_node("B", 0.90, 0.10)
    m.add_node("C", 0.50, 0.80)
    m.add_node("M", 0.50, 0.10)     # midpoint of A-B, a vertex of nothing in T0
    m.add_node("D", 0.50, 0.02)
    m.add_tri("T0", "A", "B", "C")
    m.add_tri("T1", "A", "D", "M")
    emit_manifold(out, "t_junction", m, [
        ("interior_of_first", "CROSS", (0.50, 0.40), ""),
    ], topo_class="ANALYTIC",
       note="M lies on the interior of T0's edge A-B without being a vertex "
            "of T0. Exactly one T-junction, known by construction.")

    # -- 4. orphan node -----------------------------------------------------
    m = Manifold()
    m.add_node("A", 0.20, 0.20)
    m.add_node("B", 0.80, 0.25)
    m.add_node("C", 0.45, 0.85)
    m.add_node("LOST", 0.05, 0.95)
    m.add_tri("T0", "A", "B", "C")
    emit_manifold(out, "orphan_node", m, [
        ("centroid", "CROSS", (0.483333, 0.433333), ""),
    ], topo_class="ANALYTIC",
       note="LOST belongs to no region: exactly one orphan, zero T-junctions")

    # -- 5. duplicate region ------------------------------------------------
    m = Manifold()
    m.add_node("A", 0.20, 0.20)
    m.add_node("B", 0.80, 0.25)
    m.add_node("C", 0.45, 0.85)
    m.add_tri("T0", "A", "B", "C")
    m.add_tri("T1", "B", "C", "A")     # same three nodes, rotated
    emit_manifold(out, "duplicate_region", m, [
        ("centroid", "SPEC", (0.483333, 0.433333),
         "overlapping identical regions: first-hit gives T0. SPEC, our rule"),
    ], topo_class="ANALYTIC",
       note="T1 covers the same three nodes as T0: exactly one duplicate")

    # -- 6. fan around a centre node ---------------------------------------
    # Four triangles sharing a central node, the commonest real topology.
    # Every interior edge is shared whole, so the report must be clean.
    m = Manifold()
    m.add_node("N", 0.50, 0.90)
    m.add_node("E", 0.90, 0.50)
    m.add_node("S", 0.50, 0.10)
    m.add_node("W", 0.10, 0.50)
    m.add_node("O", 0.50, 0.50)
    m.add_tri("Q0", "O", "N", "E")
    m.add_tri("Q1", "O", "E", "S")
    m.add_tri("Q2", "O", "S", "W")
    m.add_tri("Q3", "O", "W", "N")
    emit_manifold(out, "fan", m, [
        ("q0", "CROSS", (0.62, 0.62), ""),
        ("q1", "CROSS", (0.62, 0.38), ""),
        ("q2", "CROSS", (0.38, 0.38), ""),
        ("q3", "CROSS", (0.38, 0.62), ""),
        ("at_centre", "SPEC", (0.50, 0.50),
         "the shared vertex: every region contains it, first-hit gives Q0"),
        ("outside_corner", "ANALYTIC", (0.95, 0.95),
         "beyond the diamond hull: inside == false"),
    ], note="four triangles fanned around O, conforming, clean")

    # -- 7. per-node weight at manifold level -------------------------------
    m = Manifold()
    m.add_node("A", 0.20, 0.20, 2.0)
    m.add_node("B", 0.80, 0.25, 1.0)
    m.add_node("C", 0.45, 0.85, 1.0)
    m.add_tri("T0", "A", "B", "C")
    emit_manifold(out, "weighted_nodes", m, [
        ("centroid_biased", "SPEC", (cx, cy),
         "equal raw coordinates, so the result is the normalized bias vector"),
        ("interior_biased", "SPEC", (0.45, 0.40), ""),
    ], note="node A biased 2.0: the bias must survive the manifold layer")

    return out


def main():
    out = build()
    path = "tests/vectors/manifold.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")

    counts = {}
    for line in out:
        parts = line.split()
        if parts and parts[0] in ("QUERY", "TOPOLOGY"):
            counts[parts[2] if parts[0] == "QUERY" else parts[1]] = \
                counts.get(parts[2] if parts[0] == "QUERY" else parts[1], 0) + 1

    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
