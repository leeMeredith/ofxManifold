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


BIAS_EPS = 1e-6
WEIGHT_SUM_EPS = 1e-4


def position_of(m, weights):
    """
    Weights -> position, undoing the per-node bias first.

    Forward evaluation applies bias and renormalizes, so the emitted weights
    are NOT the barycentric coordinates of the point. Summing node positions
    against them directly lands somewhere else on any biased manifold. The bias
    is exactly invertible -- raw_i is proportional to b_i / nw_i -- so it is
    undone here before combining.

    Returns (x, y, well_posed).
    """
    if not weights:
        return (0.0, 0.0, False)

    total = sum(w for _, w in weights)

    raw = []
    invertible = True
    for name, w in weights:
        i = m.names.index(name)
        nw = m.wt[i]
        if abs(nw) < BIAS_EPS:
            invertible = False
            break
        raw.append(w / nw)

    if not invertible or abs(sum(raw)) < BIAS_EPS:
        x = sum(w * m.pos[m.names.index(n)][0] for n, w in weights)
        y = sum(w * m.pos[m.names.index(n)][1] for n, w in weights)
        return (x, y, False)

    rt = sum(raw)
    x = sum((r / rt) * m.pos[m.names.index(n)][0]
            for r, (n, _) in zip(raw, weights))
    y = sum((r / rt) * m.pos[m.names.index(n)][1]
            for r, (n, _) in zip(raw, weights))

    ok = abs(total - 1.0) <= WEIGHT_SUM_EPS and shares_region(m, weights)
    return (x, y, ok)


def shares_region(m, weights):
    idx = [m.names.index(n) for n, _ in weights]
    for ids in m.tris:
        if all(i in ids for i in idx):
            return True
    return False


def would_accept_move(m, node_name, to):
    """
    Would setNodePosition succeed? True unless the move flattens or inverts a
    region the node belongs to. Sign is compared against the winding recorded
    when the region was constructed.
    """
    i = m.names.index(node_name)
    old = m.pos[i]
    m.pos[i] = to
    ok = True
    for r, (ia, ib, ic) in enumerate(m.tris):
        if i not in (ia, ib, ic):
            continue
        area2 = cross(m.pos[ia][0], m.pos[ia][1],
                      m.pos[ib][0], m.pos[ib][1],
                      m.pos[ic][0], m.pos[ic][1])
        sign = 1 if area2 > 0 else -1
        if abs(area2) < 1e-6 or sign != m.signs[r]:
            ok = False
            break
    m.pos[i] = old
    return ok


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
        self.signs = []         # winding sign at construction

    def add_node(self, name, x, y, w=1.0):
        self.names.append(name)
        self.pos.append((x, y))
        self.wt.append(w)
        return len(self.names) - 1

    def add_tri(self, name, a, b, c):
        ia, ib, ic = (self.names.index(a), self.names.index(b),
                      self.names.index(c))
        area2 = cross(self.pos[ia][0], self.pos[ia][1],
                      self.pos[ib][0], self.pos[ib][1],
                      self.pos[ic][0], self.pos[ic][1])
        self.tris.append((ia, ib, ic))
        self.tri_names.append(name)
        self.signs.append(1 if area2 > 0 else -1)
        return len(self.tris) - 1

    def evaluate_hinted(self, p, hint):
        """As evaluate(), but the hint region is tested first."""
        order = ([hint] if hint is not None else []) + \
                [r for r in range(len(self.tris)) if r != hint]
        for r in order:
            ia, ib, ic = self.tris[r]
            if contains_edge_sign(self.pos[ia], self.pos[ib], self.pos[ic], p):
                w = barycentric(self.pos[ia], self.pos[ib], self.pos[ic], p)
                if w is None:
                    continue
                w = biased(w, (self.wt[ia], self.wt[ib], self.wt[ic]))
                return (r, [(self.names[ia], w[0]),
                            (self.names[ib], w[1]),
                            (self.names[ic], w[2])])
        return None

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


def emit_extras(out, m, roundtrips, inverses, moves, tracks):
    for qname, cls, p, qnote in roundtrips:
        res = m.evaluate(p)
        if qnote:
            out.append(f"# {qnote}")
        if res is None:
            out.append(f"ROUNDTRIP {qname} {cls} {fmt(p[0])} {fmt(p[1])} NONE")
            continue
        _, ws = res
        x, y, ok = position_of(m, ws)
        out.append(f"ROUNDTRIP {qname} {cls} {fmt(p[0])} {fmt(p[1])} "
                   f"{fmt(x)} {fmt(y)} {1 if ok else 0}")

    for qname, cls, weights, qnote in inverses:
        x, y, ok = position_of(m, weights)
        if qnote:
            out.append(f"# {qnote}")
        line = f"INVERSE {qname} {cls} {fmt(x)} {fmt(y)} {1 if ok else 0}"
        for n, w in weights:
            line += f" {n}={fmt(w)}"
        out.append(line)

    for qname, cls, node, to, qnote in moves:
        ok = would_accept_move(m, node, to)
        if qnote:
            out.append(f"# {qnote}")
        out.append(f"MOVE {qname} {cls} {node} {fmt(to[0])} {fmt(to[1])} "
                   f"{1 if ok else 0}")

    for qname, cls, path, qnote in tracks:
        hint = None
        parts = []
        for p in path:
            res = m.evaluate_hinted(p, hint)
            if res is None:
                hint = None
                parts.append(f"{fmt(p[0])} {fmt(p[1])} NONE")
            else:
                hint = res[0]
                parts.append(f"{fmt(p[0])} {fmt(p[1])} {m.tri_names[hint]}")
        if qnote:
            out.append(f"# {qnote}")
        out.append(f"TRACK {qname} {cls} " + " ".join(parts))


def emit_manifold(out, name, m, queries, topo_class="ANALYTIC", note="",
                  roundtrips=(), inverses=(), moves=(), tracks=()):
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

    emit_extras(out, m, roundtrips, inverses, moves, tracks)

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
    ], note="one triangle, clean topology",
       roundtrips=[
        ("rt_centroid", "ANALYTIC", (cx, cy),
         "forward then inverse must return the same point. Neither direction"
         " is the authority for the other"),
        ("rt_interior", "CROSS", (0.45, 0.40), ""),
        ("rt_near_vertex", "CROSS", (0.21, 0.21), ""),
       ],
       inverses=[
        ("inv_equal_thirds", "CROSS",
         [("A", 1/3), ("B", 1/3), ("C", 1/3)],
         "an explicit even blend must land on the centroid"),
        ("inv_single_node", "ANALYTIC", [("A", 1.0)],
         "all weight on one node lands on that node, but A alone belongs to"
         " a region only together with B and C, so this is still well posed"),
        ("inv_sum_not_one", "SPEC",
         [("A", 0.5), ("B", 0.2), ("C", 0.1)],
         "weights summing to 0.8 are not well posed; a position is still"
         " returned and the caller decides"),
       ],
       moves=[
        ("move_small", "ANALYTIC", "A", (0.22, 0.22),
         "a small move inside the region keeps the winding: accepted"),
        ("move_across", "ANALYTIC", "A", (0.65, 0.80),
         "A dragged past edge B-C inverts the triangle: refused"),
        ("move_onto_edge", "ANALYTIC", "A", (0.625, 0.55),
         "A moved onto the line through B and C flattens the region: refused"),
       ])

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
    ], note="four triangles fanned around O, conforming, clean",
       inverses=[
        ("inv_within_one_region", "CROSS",
         [("O", 0.5), ("N", 0.25), ("E", 0.25)],
         "all three nodes belong to Q0: well posed"),
        ("inv_spans_disjoint_regions", "ANALYTIC",
         [("N", 0.5), ("S", 0.5)],
         "N and S share NO region -- Q0 holds N, Q2 holds S. The weights sum"
         " to one and the recovered position lands exactly on O, which looks"
         " entirely plausible. Only the flag distinguishes it. This is the"
         " case that makes wellPosed worth having rather than decorative"),
        ("inv_opposite_rim", "ANALYTIC",
         [("E", 0.5), ("W", 0.5)],
         "the other diagonal: same fault, same plausible-looking centre"),
       ])

    # -- 6b. overlapping regions, for hint hysteresis -----------------------
    # Two triangles overlapping in a lens-shaped zone. A source that entered
    # through T1's exclusive area must STAY in T1 while crossing the overlap,
    # even though a fresh evaluation would report T0 by insertion order. That
    # difference is the whole point of the hint living on the source.
    m = Manifold()
    m.add_node("A", 0.05, 0.05)
    m.add_node("B", 0.55, 0.05)
    m.add_node("C", 0.30, 0.55)
    m.add_node("D", 0.30, 0.05)
    m.add_node("E", 0.80, 0.05)
    m.add_node("F", 0.55, 0.55)
    m.add_tri("T0", "A", "B", "C")
    m.add_tri("T1", "D", "E", "F")
    emit_manifold(out, "overlap", m, [
        ("fresh_in_overlap", "SPEC", (0.40, 0.15),
         "no hint: first-hit gives T0"),
        ("exclusive_to_t1", "CROSS", (0.65, 0.15), ""),
    ], topo_class="SPEC",
       note="T0 and T1 overlap by design. Topology counts here are SPEC, not "
            "ANALYTIC: overlap produces incidental T-junctions that are a "
            "consequence of the construction rather than a hand-known fault.",
       tracks=[
        ("track_enters_t1_then_overlap", "SPEC",
         [(0.65, 0.15), (0.50, 0.15), (0.40, 0.15)],
         "entering through T1's exclusive zone and moving into the overlap "
         "must hold T1 throughout -- hysteresis"),
        ("track_enters_t0_then_overlap", "SPEC",
         [(0.15, 0.15), (0.30, 0.15), (0.40, 0.15)],
         "the mirror case: entering through T0 holds T0 in the same overlap"),
        ("track_leaves_and_returns", "SPEC",
         [(0.65, 0.15), (0.95, 0.45), (0.40, 0.15)],
         "leaving the hull clears the hint, so re-entry falls back to "
         "first-hit rather than inheriting a stale region"),
       ])

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
    ], note="node A biased 2.0: the bias must survive the manifold layer",
       roundtrips=[
        ("rt_biased_centroid", "ANALYTIC", (cx, cy),
         "THE case for un-biasing in positionOf. A naive weighted sum of node"
         " positions against biased weights lands elsewhere; only undoing the"
         " bias first returns the original point"),
        ("rt_biased_interior", "CROSS", (0.45, 0.40), ""),
       ])

    return out


def main():
    out = build()
    path = "tests/vectors/manifold.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")

    counts = {}
    for line in out:
        parts = line.split()
        if not parts:
            continue
        if parts[0] in ("QUERY", "ROUNDTRIP", "INVERSE", "MOVE", "TRACK"):
            cls = parts[2]
        elif parts[0] == "TOPOLOGY":
            cls = parts[1]
        else:
            continue
        counts[cls] = counts.get(cls, 0) + 1

    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
