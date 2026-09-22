# Generalized regions — plan

Adding mean-value coordinates so a region may have N ≥ 3 nodes, without
changing anything downstream of the weight vector.

Kept separate from `ROADMAP.md` while the work is in progress. It folds into
`DECISIONS.md` when done, and this file is deleted.

---

## Why this before the editor

The editor's central interaction is "create a region from selected nodes." If a
region can only be a triangle, that is a three-node pick with three-node
affordances. If it can be any ring of N ≥ 3, it is a different interaction and a
different data model.

Building the editor first would mean rebuilding it. This also exercises the
extension point (§7) before something large depends on it.

---

## Decisions settled before writing code

### D-A · Value type, not an interface

**Decided: one value type holding an ordered ring of `NodeID`, dispatching on
`size()`.**

§7 committed to `Region` as an interface with `Triangle` implementing it. That is
reversed.

The interface was written as an escape hatch for exactly this moment. Now that
the moment is here, a `std::vector<NodeID>` does the same job with less
machinery: `Manifold2D` stays copyable, no heap allocation per region, no
virtual call, no `unique_ptr` in a class that gets moved in `loadManifold`.

The argument that settles it: an interface here would be a **one-implementation
abstraction that stays that way**. A tetrahedron lives in `Manifold3D` because it
takes a `vec3` point, not here. An abstraction with two members and no prospect
of a third is a struct wearing a costume.

Value types also serialize obviously, copy obviously, and read as their contents
in a debugger.

### D-B · No new noun

**Decided: `Region` is the concept. No `Polyset`, no `Polygon`, no `Cell`.**

Given D-A there is one struct holding an ordered ring. A region with three nodes
and one with seven are the same type differing in `size()`. A separate name would
name a thing that is not a separate thing.

- `Manifold2D::addRegion(std::vector<NodeID>)` is the general call
- `addTriangle(a, b, c)` stays as a convenience that forwards to it
- `Triangle` stops being a public type and becomes the fast path inside the
  solve, which is where an optimization belongs

This is the same shape of finding as the node taxonomy: terminal, null and
composite nodes turned out to be one struct differing in cardinality (§6.1).
Regions differ in arity. Neither needed a type hierarchy.

### D-C · Non-convex regions are ACCEPTED

**Decided: accept any simple polygon. Stars and L-shapes included.**

An earlier draft of this plan proposed rejecting non-convex regions on the
grounds that MVC can produce negative weights inside them. That was wrong, and
measuring it is what showed so:

    shape                    interior samples with a negative   most negative
    star, inner radius 0.19        0 of 6356                      0.000000
    star, inner radius 0.07     1384 of 2336                     -0.000416
    L-shape, one reflex vertex  6288 of 9984                     -0.012705

A moderate star never goes negative at all. Negatives appear only in deep
notches and around a strong reflex vertex, and are tiny when they do.

Floater and Hormann's 2006 paper is titled *Mean value coordinates for arbitrary
planar polygons*. Arbitrary is the achievement. Rejecting non-convex would throw
away the feature the algorithm exists to provide.

The reasoning that led to the wrong answer is worth recording: **an
audio-specific concern was allowed to decide a geometry question.** A negative
gain is a phase inversion, which is usually wrong — for a panner. This is not a
panner. For parameter interpolation a negative weight is extrapolation, pulling
slightly beyond a node's value, which is sometimes exactly what is wanted.

The manifold describes relationships. A negative weight is a relationship.
Whether it is acceptable is interpretation.

### D-D · Negative weights get help, in the interpretation layer

**Decided: a clamp-and-renormalize policy beside the curves, plus diagnostics.**

`clampNegative()` zeroes negative weights and renormalizes the rest. Cheap — the
worst measured case redistributes about 1% of the vector.

It must be documented as **breaking affinity**: after clamping the result is no
longer a true affine combination, so a blend of two parameter values will not
land where the geometry says. Right for gains, wrong for positions. The same
distinction as D-003, so it sits beside the curves with the same warning.

Not applied automatically anywhere.

---

## Work, in order

### 1 · Python reference — DONE

`tests/ref/reference_regions.py`, 67 vectors written (55 ANALYTIC, 12 CROSS).

Self-checked before being trusted, and the self-check found two faults:

**Check ordering.** A bowtie's two lobes have opposite signed area and cancel
to zero, so the area test fired before the self-intersection test and reported
`degenerate` — the right verdict for the wrong reason, sending an author
looking for a collapsed region instead of a swapped vertex. Same failure as
D-007. Self-intersection is now tested first, because a self-intersecting
ring's signed area is meaningless.

**A vector that proved nothing.** A hand-built "uneven" bowtie, added to
exercise the reordering, turned out to cancel to zero as well. Found one by
searching instead: signed area 0.6655, so an area test would **accept** it, and
only the self-intersection check refuses it.

Deleting `self_intersects()` now produces both failure modes — the uneven
bowtie is wrongly accepted, and the symmetric one is refused for the wrong
reason. Before that second fixture, the whole check could have been removed
with every construction vector still passing.

Measured, over full interior sweeps rather than probes:

    N = 3 agreement, worst error          3.33e-16
    partition of unity, all five shapes   2.22e-16
    edge special case joins the interior  8.2e-05 at a 1e-4 offset

### 2 · Vectors, before any C++

- **N = 3 agreement** — MVC and the triangle solve must agree. ANALYTIC: it
  follows from the definition, and it is what makes this an extension rather
  than a replacement.
- Partition of unity across quad, pentagon, star, L-shape
- Vertex coincidence returns a delta
- Edge points interpolate linearly along that edge, with the two non-edge
  vertices at zero
- Affine invariance for N > 3, as §7.0 has for triangles
- Negative weights present where measured and absent where measured — the star
  and L-shape numbers above, asserted rather than described
- Construction rejections: fewer than three nodes, repeated nodes, zero area,
  self-intersection
- Construction acceptances: convex, star, L-shape — the guard against a
  validity check that rejects everything

### 3 · C++ — DONE

- `Region` as the value type (D-A)
- `Manifold2D` stores `std::vector<Region>`; `addRegion`, `addTriangle`
  forwarding
- Triangle fast path preserved exactly — 31 vectors and four gates depend on it
- `TopologyReport` gains non-convex regions as **information**, beside orphans
  and duplicates
- `Evaluation` gains a cheap negative-weight flag

### 4 · The contract test — PASSED, with a caveat (D-017 §1)

`git diff --stat` on `interpretation/`, `mapping/`, `io/` and `sources/` must
be **empty**.

That is the claim §3.1 has been making since the layering was drawn: nothing
above the weight-vector line is visible below it. If adding a second coordinate
algorithm requires a change down there, the contract was wrong and **that is the
finding**, not an inconvenience.

### 5 · Mutation gates — DONE, 12 caught

Expect the gaps to be in the special cases, as in every previous round. Ones to
write deliberately:

- edge fallback removed
- vertex coincidence removed
- convexity test inverted
- self-intersection test dropped
- MVC used for N = 3 instead of the triangle path, and the reverse

### 6 · Serialization — DONE

`"triangles"` becomes `"regions"`, arrays of any length ≥ 3. Old files keep
loading: a `"triangles"` key is read as three-node regions. Format version stays
1 — this is additive, and refusing files that worked yesterday would be worse
than a version bump is worth.

### 7 · Example — NEXT

A quad, a hexagon, a star and a triangle in one map, with the negative-weight
diagnostic visible and `clampNegative()` on a key so the difference can be seen
rather than read about.

---

## Deliberately not in scope

- **Automatic decomposition** of non-convex regions into convex pieces. Not
  needed now that non-convex is accepted.
- **Wachspress or other generalized barycentric schemes.** MVC handles arbitrary
  simple polygons; a second scheme would need its own reason.
- **`Manifold3D`.** Unchanged by this work, and still likely unnecessary given
  `example-blend`.

---

## Open, to settle during the work

1. **Does `addTriangle` stay in the public API?** It keeps existing code
   working and reads better for the common case. Leaning yes.
2. **Should `validate()` report convexity per region, or just count?** Per
   region is more useful and costs a vector of ids.
3. **Where does `clampNegative()` sit relative to curves?** Probably before —
   clamp, then curve — but that wants a vector rather than a preference.
