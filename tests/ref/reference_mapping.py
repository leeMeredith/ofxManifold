#!/usr/bin/env python3
"""
ofxManifold — independent reference for the mapping layer.

Formulation differs from the C++ where a difference is possible:

  resolve   C++ iterates NODES and pushes each node's share out along its
            links. This inverts the loop: it builds a target -> contributions
            index first, then pulls. Same result, opposite direction of travel,
            so a bug in either accumulation order shows up as disagreement.

  aggregate C++ scans the weight vector once per source node. This builds a
            dict of the weight vector first and looks up.

Classes:
  ANALYTIC  known by hand. Terminal identity, null discarding, composite split
            summing back to the node's weight, dense arity.
  CROSS     from this reference.
  SPEC      our rules: link weights normalized within a node, duplicate binds
            accumulating rather than replacing, zero-contribution targets
            dropped from the sparse result but present in the dense one.

Node and target IDs here are deliberately NOT contiguous from zero in several
cases. See DECISIONS.md D-004: sequential IDs starting at zero are
indistinguishable from positional indices, and a suite built only from them
cannot detect an implementation that reindexes its output.
"""

import math
import sys

TOL = 1e-6


def resolve(targets, links, aggregators, wv):
    """
    Inverted loop: build target -> [(node, share)] first, then pull.

    A node's weight is divided among its links in proportion to link weight,
    normalized within the node. A node with no links discards its weight
    entirely -- that shortfall is the fade a null node exists to produce.
    """
    index = {}
    for node, ls in links.items():
        total = sum(w for _, w in ls)
        if abs(total) < 1e-9:
            continue
        for tid, lw in ls:
            index.setdefault(tid, []).append((node, lw / total))

    src = {}
    for nid, w in wv:
        src[nid] = src.get(nid, 0.0) + w

    out = []
    for tid in range(len(targets)):
        acc = 0.0
        for node, share in index.get(tid, []):
            acc += src.get(node, 0.0) * share
        if abs(acc) >= 1e-9:
            out.append((tid, acc))

    aggs = []
    for _, sources, mode in aggregators:
        v = 0.0
        for s in sources:
            w = src.get(s, 0.0)
            v += w if mode == "linear" else w * w
        if mode == "power":
            v = 0.0 if v <= 0.0 else math.sqrt(v)
        aggs.append(v)
    return out, aggs


def dense(targets, links, aggregators, wv):
    sparse, _ = resolve(targets, links, aggregators, wv)
    d = [0.0] * len(targets)
    for tid, w in sparse:
        d[tid] = w
    return d


def spread(v, amount, total):
    """Explicit per-component lerp toward uniform, as in the interpretation
    reference. Repeated here so the composition test does not depend on
    importing across reference files."""
    if amount <= 0.0 or total == 0:
        return list(v)
    amount = min(amount, 1.0)
    uniform = 1.0 / total
    src = {i: w for i, w in v}
    return [(i, (1.0 - amount) * src.get(i, 0.0) + amount * uniform)
            for i in range(total)]


def blend_by_name(ra, names_a, rb, names_b, t, curve="linear"):
    """
    Crossfade two RESOLVED target vectors by target NAME.

    The C++ scans its output list for a matching name; this uses a dict.
    Different lookup, same merge.
    """
    import math as _m
    t = max(0.0, min(1.0, t))
    if curve == "linear":
        ga, gb = 1.0 - t, t
    elif curve == "equalPower":
        ga, gb = _m.sqrt(1.0 - t), _m.sqrt(t)
    else:                                   # cosine
        ga = _m.cos((1.0 - (1.0 - t)) * (_m.pi / 2))
        gb = _m.cos((1.0 - t) * (_m.pi / 2))
        ga, gb = _m.sin((1.0 - t) * (_m.pi / 2)), _m.sin(t * (_m.pi / 2))

    acc, order = {}, []
    for tid, w in ra:
        n = names_a[tid]
        if n not in acc:
            acc[n] = 0.0
            order.append(n)
        acc[n] += w * ga
    for tid, w in rb:
        n = names_b[tid]
        if n not in acc:
            acc[n] = 0.0
            order.append(n)
        acc[n] += w * gb
    return [(n, acc[n]) for n in order if abs(acc[n]) >= 1e-9]


def fmt(x):
    """
    Ten significant digits, deliberately not seventeen.

    IEEE-754 requires sqrt to be correctly rounded, so it is identical on every
    platform. It requires NOTHING of sin and cos, and libm implementations
    differ by about one unit in the last place. At 17 significant digits that
    difference is visible, so a vector file generated on one platform did not
    regenerate byte-for-byte on another and CI failed the drift check -- on a
    file whose values were fine.

    Ten digits resolve to about 1e-10, four orders below the 1e-6 comparison
    tolerance, and absorb the last-place divergence. See DECISIONS.md D-006.
    """
    return f"{x:.10g}"


def wv_s(v):
    return " ".join(f"{i}={fmt(w)}" for i, w in v)


def tv_s(names, v):
    return " ".join(f"{names[i]}={fmt(w)}" for i, w in v)


class Fixture:
    def __init__(self, name):
        self.name = name
        self.targets = []
        self.links = {}
        self.aggregators = []
        # Raw log of bind() calls, in order. The emitted vector file must
        # replay the CALLS, not the accumulated result -- otherwise a
        # duplicate bind is collapsed here and the C++ never sees one.
        self.bind_log = []

    def target(self, n):
        if n not in self.targets:
            self.targets.append(n)
        return self.targets.index(n)

    def bind(self, node, target, w=1.0):
        tid = self.target(target)
        self.bind_log.append((node, target, w))
        ls = self.links.setdefault(node, [])
        for i, (t, lw) in enumerate(ls):
            if t == tid:
                ls[i] = (t, lw + w)     # accumulate, do not replace
                return
        ls.append((tid, w))

    def aggregate(self, name, sources, mode="linear"):
        self.aggregators.append((name, sources, mode))

    def emit(self, out, queries, note=""):
        out.append("")
        if note:
            out.append(f"# {note}")
        out.append(f"MAPPING {self.name}")
        for t in self.targets:
            out.append(f"TARGET {t}")
        for node, tname, lw in self.bind_log:
            out.append(f"BIND {node} {tname} {fmt(lw)}")
        for nm, srcs, mode in self.aggregators:
            out.append(f"AGGREGATOR {nm} {mode} "
                       + " ".join(str(s) for s in srcs))

        for kind, qname, cls, v, qnote in queries:
            if qnote:
                out.append(f"# {qnote}")
            if kind == "RESOLVE":
                sparse, _ = resolve(self.targets, self.links,
                                    self.aggregators, v)
                out.append(f"RESOLVE {qname} {cls} IN {wv_s(v)} OUT "
                           + tv_s(self.targets, sparse))
            elif kind == "DENSE":
                d = dense(self.targets, self.links, self.aggregators, v)
                out.append(f"DENSE {qname} {cls} IN {wv_s(v)} OUT "
                           + " ".join(fmt(x) for x in d))
            elif kind == "SUM":
                sparse, _ = resolve(self.targets, self.links,
                                    self.aggregators, v)
                out.append(f"TARGETSUM {qname} {cls} IN {wv_s(v)} SUM "
                           f"{fmt(sum(w for _, w in sparse))}")
            elif kind == "AGG":
                _, aggs = resolve(self.targets, self.links,
                                  self.aggregators, v)
                out.append(f"AGGREGATE {qname} {cls} IN {wv_s(v)} OUT "
                           + " ".join(fmt(a) for a in aggs))
        out.append("END")


def build():
    out = []
    out.append("# ofxManifold mapping layer conformance vectors")
    out.append("# GENERATED by tests/ref/reference_mapping.py -- do not hand edit")
    out.append("#")
    out.append("# The reference inverts the C++'s loop: it indexes")
    out.append("# target -> contributions and pulls, where the C++ iterates")
    out.append("# nodes and pushes. Node and target IDs are deliberately not")
    out.append("# contiguous from zero in several fixtures (D-004).")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")

    # -- 1. terminal bindings, the ordinary case ---------------------------
    f = Fixture("terminal")
    f.bind(0, "out.1")
    f.bind(1, "out.2")
    f.bind(2, "out.3")
    f.emit(out, [
        ("RESOLVE", "identity_even", "ANALYTIC",
         [(0, 1/3), (1, 1/3), (2, 1/3)],
         "one node, one target, weight 1.0: the weight passes through"),
        ("RESOLVE", "identity_uneven", "ANALYTIC",
         [(0, 0.2), (1, 0.5), (2, 0.3)], ""),
        ("SUM", "sum_preserved", "ANALYTIC", [(0, 0.2), (1, 0.5), (2, 0.3)],
         "with every node bound to exactly one target and nothing null, the"
         " total is preserved"),
        ("DENSE", "dense_arity", "ANALYTIC", [(0, 0.2), (1, 0.5), (2, 0.3)],
         "dense output has one slot per target, in TargetID order"),
    ], note="three nodes, three targets, one to one")

    # -- 2. null node ------------------------------------------------------
    f = Fixture("null_node")
    f.bind(0, "out.1")
    f.bind(1, "out.2")
    # node 2 is bound to nothing: a null node
    f.emit(out, [
        ("RESOLVE", "null_discards", "ANALYTIC",
         [(0, 0.4), (1, 0.4), (2, 0.2)],
         "node 2 is bound to nothing. Its 0.2 is discarded, not redistributed"),
        ("SUM", "sum_falls_short", "ANALYTIC",
         [(0, 0.4), (1, 0.4), (2, 0.2)],
         "the total resolves to 0.8, NOT 1.0. That shortfall IS the fade a"
         " null node exists to produce, and correcting it would defeat the"
         " purpose"),
        ("SUM", "sum_at_null_extreme", "ANALYTIC", [(2, 1.0)],
         "all the weight on the null node: silence, and no error"),
        ("RESOLVE", "null_extreme_empty", "ANALYTIC", [(2, 1.0)],
         "and the sparse result is empty rather than a zero-filled vector"),
    ], note="node 2 bound to nothing -- MIAP's silent node")

    # -- 3. composite node -------------------------------------------------
    f = Fixture("composite")
    f.bind(0, "corner.NW")
    f.bind(0, "corner.NE")
    f.bind(0, "corner.SW")
    f.bind(0, "corner.SE")
    f.bind(1, "out.direct")
    f.emit(out, [
        ("RESOLVE", "composite_splits_evenly", "ANALYTIC",
         [(0, 0.8), (1, 0.2)],
         "node 0 feeds four corners equally: 0.2 each, and the node's total"
         " contribution is unchanged by how many targets it feeds"),
        ("SUM", "composite_preserves_total", "ANALYTIC",
         [(0, 0.8), (1, 0.2)], ""),
        ("RESOLVE", "composite_alone", "ANALYTIC", [(0, 1.0)], ""),
    ], note="node 0 is composite over four corners -- MIAP's virtual node,"
            " added because not every rig has an overhead speaker")

    # -- 4. weighted links within a node -----------------------------------
    f = Fixture("weighted_links")
    f.bind(0, "near", 3.0)
    f.bind(0, "far", 1.0)
    f.bind(1, "other")
    f.emit(out, [
        ("RESOLVE", "links_normalize_within_node", "SPEC",
         [(0, 0.8), (1, 0.2)],
         "link weights 3 and 1 normalize to 0.75 and 0.25 WITHIN the node, so"
         " the node still contributes 0.8 in total. Normalizing within the"
         " node rather than globally is our rule"),
        ("SUM", "weighted_links_preserve_total", "SPEC",
         [(0, 0.8), (1, 0.2)], ""),
    ], note="unequal link weights on a composite node")

    # -- 5. many nodes to one target ---------------------------------------
    f = Fixture("many_to_one")
    f.bind(0, "shared")
    f.bind(1, "shared")
    f.bind(2, "solo")
    f.emit(out, [
        ("RESOLVE", "many_to_one_accumulates", "ANALYTIC",
         [(0, 0.3), (1, 0.4), (2, 0.3)],
         "nodes 0 and 1 both feed 'shared', which receives 0.7. MIAP states"
         " this directly: the map is not a projection of real space, so two"
         " adjacent nodes may be the same output"),
        ("SUM", "many_to_one_total", "ANALYTIC",
         [(0, 0.3), (1, 0.4), (2, 0.3)], ""),
    ], note="two nodes bound to the same target")

    # -- 5b. duplicate bind accumulates ------------------------------------
    f = Fixture("duplicate_bind")
    f.bind(0, "out.1", 0.5)
    f.bind(0, "out.1", 0.5)      # same node, same target, again
    f.bind(0, "out.2", 1.0)
    f.emit(out, [
        ("RESOLVE", "duplicate_bind_accumulates", "SPEC",
         [(0, 1.0)],
         "out.1 was bound twice at 0.5, totalling 1.0, so the node splits"
         " evenly with out.2. Replacing instead of accumulating would give"
         " one third and two thirds. A caller assembling bindings from two"
         " sources must not silently lose one"),
    ], note="the same node bound to the same target twice")

    # -- 6. sparse, non-contiguous node ids (D-004) ------------------------
    f = Fixture("sparse_ids")
    f.bind(11, "alpha")
    f.bind(4, "beta")
    f.bind(7, "gamma")
    f.bind(7, "alpha", 1.0)
    f.emit(out, [
        ("RESOLVE", "sparse_node_ids", "CROSS",
         [(4, 0.25), (7, 0.5), (11, 0.25)],
         "node IDs 4, 7 and 11 with targets bound out of creation order. An"
         " implementation that reindexed either side would pass with"
         " contiguous IDs and fails here"),
        ("DENSE", "sparse_dense_order", "CROSS",
         [(4, 0.25), (7, 0.5), (11, 0.25)],
         "dense order follows TargetID, which follows creation order:"
         " alpha, beta, gamma"),
        ("RESOLVE", "sparse_partial", "CROSS", [(7, 1.0)],
         "only node 7 active: it is composite over gamma and alpha"),
    ], note="non-contiguous node IDs, bindings created out of order")

    # -- 7. unbound target present in dense output -------------------------
    f = Fixture("unbound_target")
    f.target("out.1")
    f.target("out.unused")
    f.target("out.2")
    f.bind(0, "out.1")
    f.bind(1, "out.2")
    f.emit(out, [
        ("DENSE", "unbound_target_is_zero", "ANALYTIC",
         [(0, 0.6), (1, 0.4)],
         "'out.unused' has no bindings but still occupies slot 1 of the dense"
         " vector. Arity is a property of the mapping, not of the point --"
         " otherwise an OSC receiver reading argument 3 would be reading a"
         " different output from one frame to the next"),
        ("RESOLVE", "unbound_absent_from_sparse", "SPEC",
         [(0, 0.6), (1, 0.4)],
         "the sparse result omits it, matching the convention evaluate() uses"
         " outside the hull"),
    ], note="a declared target with no bindings")

    # -- 8. aggregators ----------------------------------------------------
    f = Fixture("aggregators")
    f.bind(0, "out.1")
    f.bind(1, "out.2")
    f.bind(2, "out.3")
    f.bind(3, "out.4")
    f.aggregate("sub.house", [0, 1], "linear")
    f.aggregate("sub.power", [0, 1], "power")
    f.aggregate("sub.all", [0, 1, 2, 3], "linear")
    f.emit(out, [
        ("AGG", "aggregate_linear", "ANALYTIC",
         [(0, 0.3), (1, 0.4), (2, 0.2), (3, 0.1)],
         "linear: 0.3 + 0.4 = 0.7. Power: sqrt(0.09 + 0.16) = 0.5. The third"
         " watches every node and must total 1.0"),
        ("AGG", "aggregate_absent_source", "CROSS", [(0, 1.0)],
         "a source node absent from the weight vector contributes zero rather"
         " than being an error"),
        ("AGG", "aggregate_empty_input", "ANALYTIC", [],
         "empty weight vector: every aggregate is zero, and the power mode"
         " does not take the root of a negative"),
    ], note="aggregators are SpaceMap's derived nodes -- they run the routing"
            " backwards and are not part of any region")

    # ---- spread composed with mapping ------------------------------------
    #
    # Spread and mapping are each well covered and were never composed. Their
    # interaction is not obvious, and it is worth asserting rather than
    # discovering.
    #
    # Spreading pushes weight onto EVERY node in the map, including the null
    # ones. A null node discards its share, so the resolved TARGET total falls
    # as spread rises -- spreading a map that is ringed with null nodes fades
    # the output, without anything being told to fade.
    #
    # That is emergent behaviour of two correct layers, and it is the sort of
    # thing that reads as a bug in a rehearsal room unless someone wrote it
    # down first.
    f = Fixture("spread_with_nulls")
    f.bind(0, "out.1")
    f.bind(1, "out.2")
    f.bind(2, "out.3")
    # nodes 3, 4, 5 are bound to nothing: a null ring
    f.emit(out, [], note="three bound nodes and three null ones")

    out.append("#" + "-" * 68)
    out.append("# SPREAD COMPOSED WITH MAPPING")
    out.append("#")
    out.append("# Node weights sum to 1 at every spread amount. Resolved")
    out.append("# targets do not, because spread hands part of the weight to")
    out.append("# nodes bound to nothing.")
    out.append("#" + "-" * 68)
    out.append("")

    base = [(0, 0.5), (1, 0.3), (2, 0.2)]
    for nm, amt, cls, note in [
        ("none", 0.0, "ANALYTIC",
         "no spread: all weight is on bound nodes, targets total 1"),
        ("quarter", 0.25, "CROSS", ""),
        ("half", 0.5, "CROSS", ""),
        ("full", 1.0, "ANALYTIC",
         "full spread: uniform over six nodes, three of them null, so "
         "exactly half the weight is discarded and the targets total 0.5"),
    ]:
        sv = spread(base, amt, 6)
        res, _ = resolve(f.targets, f.links, f.aggregators, sv)
        node_total = sum(w for _, w in sv)
        tgt_total = sum(w for _, w in res)
        if note:
            out.append(f"# {note}")
        out.append(f"SPREADRESOLVE spreadres_{nm} {cls} {fmt(amt)} 6 "
                   f"IN {wv_s(base)} "
                   f"NODESUM {fmt(node_total)} TARGETSUM {fmt(tgt_total)} "
                   f"OUT " + tv_s(f.targets, res))
        out.append("")

    # ---- blending ACROSS manifolds --------------------------------------
    #
    # Two maps, built independently, with DELIBERATELY COLLIDING ids.
    #
    # blend() merges by NodeID, and NodeIDs are per-manifold indices, so two
    # independently built maps always collide at 0, 1, 2. Every blend vector
    # in interpretation.vec uses hand-picked non-colliding ids -- A on 0 and 1,
    # B on 2 and 3 -- so the suite never met the case (D-013).
    #
    # These fixtures resolve each side through its own mapping first, then
    # merge by target NAME. Both mappings declare their targets in DIFFERENT
    # ORDERS on purpose: an implementation that merged by TargetID instead of
    # by name passes if the orders happen to agree and fails here.
    out.append("")
    out.append("#" + "-" * 68)
    out.append("# BLENDING ACROSS MANIFOLDS")
    out.append("#")
    out.append("# Node identity is local to a manifold. Target identity is")
    out.append("# local to a mapping. Only the NAME crosses between them.")
    out.append("#" + "-" * 68)

    # Map A's mapping.
    fa = Fixture("blend_map_a")
    fa.bind(0, "out.L")
    fa.bind(1, "out.R")
    fa.bind(2, "out.sub")

    # Map B's mapping. Note out.sub is declared FIRST here, so it lands on a
    # different TargetID than it has in A.
    fb = Fixture("blend_map_b")
    fb.bind(0, "out.sub")
    fb.bind(1, "out.R")
    fb.bind(2, "out.rear")

    fa.emit(out, [], note="map A's mapping: L, R, sub")
    fb.emit(out, [], note="map B's mapping: sub, R, rear -- a DIFFERENT id "
                          "order for the same names, so a merge by id rather "
                          "than by name gives the wrong answer")

    wa = [(0, 0.5), (1, 0.3), (2, 0.2)]
    wb = [(0, 0.1), (1, 0.4), (2, 0.5)]
    ra, _ = resolve(fa.targets, fa.links, fa.aggregators, wa)
    rb, _ = resolve(fb.targets, fb.links, fb.aggregators, wb)

    for nm, t, cls, note in [
        ("t0", 0.0, "ANALYTIC", "t=0 is map A alone"),
        ("t1", 1.0, "ANALYTIC", "t=1 is map B alone"),
        ("half", 0.5, "CROSS",
         "halfway: out.R and out.sub appear in BOTH maps and their "
         "contributions add; out.L and out.rear appear in one each"),
        ("quarter", 0.25, "CROSS", ""),
    ]:
        res = blend_by_name(ra, fa.targets, rb, fb.targets, t)
        if note:
            out.append(f"# {note}")
        out.append(f"BLENDNAME blendname_{nm} {cls} {fmt(t)} linear "
                   f"MAPA blend_map_a A " + wv_s(wa) +
                   " MAPB blend_map_b B " + wv_s(wb) +
                   " OUT " + " ".join(f"{n}={fmt(w)}" for n, w in res))
        out.append("")

    out.append("# t outside [0, 1] clamps rather than extrapolating into")
    out.append("# negative gains. A crossfade driven from an unclamped fader")
    out.append("# would otherwise invert one map as it passed the end.")
    for nm, t in (("clamp_high", 2.0), ("clamp_low", -1.0)):
        res_c = blend_by_name(ra, fa.targets, rb, fb.targets, t)
        out.append(f"BLENDNAME blendname_{nm} SPEC {fmt(t)} linear "
                   f"MAPA blend_map_a A " + wv_s(wa) +
                   " MAPB blend_map_b B " + wv_s(wb) +
                   " OUT " + " ".join(f"{n}={fmt(w)}" for n, w in res_c))
    out.append("")

    res = blend_by_name(ra, fa.targets, rb, fb.targets, 0.5, "equalPower")
    out.append("# constant-power crossfade between two maps, which is what a")
    out.append("# source moving from one rig to another wants")
    out.append(f"BLENDNAME blendname_equalpower CROSS {fmt(0.5)} equalPower "
               f"MAPA blend_map_a A " + wv_s(wa) +
               " MAPB blend_map_b B " + wv_s(wb) +
               " OUT " + " ".join(f"{n}={fmt(w)}" for n, w in res))
    out.append("")

    return out


def main():
    out = build()
    path = "tests/vectors/mapping.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        p = line.split()
        if p and p[0] in ("RESOLVE", "DENSE", "TARGETSUM", "AGGREGATE"):
            counts[p[2]] = counts.get(p[2], 0) + 1
    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
