# ROADMAP

What is built, what is next, and what is deliberately parked. Kept separate
from `DECISIONS.md`, which records reversals rather than intentions.

---

## Built and proved

Every layer below `src/ofx` is verified against an independent implementation
in another language — two solves, two containment tests, two JSON parsers.

| Layer | Suite | What it covers |
|---|---|---|
| `core` | 31 + 48 | triangle solve, bias, degeneracy, affine invariance, containment, topology, inverse, node movement, evaluator hint |
| `interpretation` | 60 | curves, spread, blend, interpolate, coverage |
| `mapping` | 24 | bindings, aggregators, dense output |
| `io` | 42 | JSON round trip, rejection, diagnosis |

205 vectors, 36 mutation gates, two architectures on every push.

## Built, judged on screen

`src/ofx` has no second implementation and no ground truth (DECISIONS.md
D-010). It is checked for typos against a stub and judged by eye.

- renderer: nodes, regions, edges, active region, control point, weights,
  topology faults drawn in place
- `example-basic`: drag the point, drag a node, three fixtures
- `example-parameter-morphing`: four presets, continuous blend
- `example-mapping`: the node taxonomy, null ring, aggregators, dense output

---

## Publication

Done:

- node label collision — labels step aside rather than stack
- README rewritten with lineage and attribution
- screenshot and ofxaddons thumbnail
- MIT LICENSE

Remaining:

1. **Make the repository public**, add topics, tag `v1.0.0`.
2. **Submit to ofxaddons.com.**
3. **Larger mesh sanity check.** Everything so far is five to nine nodes. A
   thirty-node map would show whether the renderer stays legible and whether
   the Evaluator hint earns its keep. Deliberately *after* the tag: it may
   change the renderer, and the renderer is the layer with no tests behind it,
   so a change there wants its own release rather than riding along with the
   first one.

---

## After publication

Roughly in order of what teaches most per line.

**Trajectories (§11).** A recorded path replayed against a DIFFERENT map. This
is the SpaceMap touring case, the strongest demonstration of the normalized
coordinate decision, and the piece that makes this a theatre tool rather than a
demo. `PointSource` is designed but unbuilt.

**Two manifolds blended (§9.3).** `blend()` exists and nothing demonstrates it.
Two maps, one crossfade, weights combined downstream — which is also the
historical answer to three dimensions, layered 2D rather than tetrahedra.

**Spread (§9.2).** One slider from pinpoint to wash. Trivial to demonstrate and
immediately legible; currently invisible.

Worth thinking of alongside MIAP's `barycentricpower`: both are transformations
of the influence weights themselves, applied before anyone decides what the
weights mean. Neither is an audio feature, and neither belongs in the
evaluator.

**Smoothing (§12).** Named in the architecture, unbuilt. A `WeightSmoother` with
per-frame slew limiting. Matters the moment a real control source is jittery,
which is every real control source.

**Interactive editor (§10).** Create, drag, name, connect, save. `example-basic`
already does half of it by accident.

---

## Parked, with reasons

**`evaluateAffine()` — extrapolation outside the hull.** Barycentric coordinates
outside a triangle are well defined and go negative, which is extrapolation
rather than interpolation: a point beyond the states that defined the space.
Interesting as a creative primitive.

Parked because it is purely additive — a new method that never touches
`evaluate()`, over arithmetic `solveRaw()` already computes. Nothing about
waiting makes it harder, and no consumer wants it today. Build it when something
asks.

**Weight derivatives.** `d(weight)/dt` as a reusable control signal. Correct and
useful, and it does not belong in the manifold: the evaluator is stateless by
design and a derivative needs history. A temporal consumer over a stateless
kernel, same shape as smoothing. The kernel already supports it by not
preventing it.

**Signed areas in the result.** `areaA` is `wA` times the total area, so it is
derivable from what is already returned. API surface with no new information.
If a diagnostic needs it, the renderer can compute it.

**A second coordinate kernel: mean-value coordinates over polygons.**

Not "triangles but with more vertices". A second generalized-barycentric
algorithm, sitting alongside the triangle solve and producing the same weight
vector, so that everything downstream is untouched.

MIAP already works this way: a **polyset** of N ≥ 3 nodes, weights from
**mean-value coordinates**, which reduce exactly to ordinary barycentric
coordinates at N = 3. That reduction is what makes it a clean extension rather
than a replacement — the existing triangle vectors would still have to pass.

References: M. S. Floater (2003), "Mean value coordinates", *CAGD* 20(1),
19–27; K. Hormann & M. S. Floater (2006), "Mean value coordinates for arbitrary
planar polygons", *ACM TOG* 25(4), 1424–1441.

### The invariant this must preserve

    coordinate algorithm      triangle, or MVC polygon, or something later
            |
            v
    normalized weight vector  <-- the contract. Nothing above this line is
            |                     visible below it.
            v
    interpretation            curves, spread, blend, interpolate
            |
            v
    mapping / io              targets, aggregators, files

Adding MVC must require **no change** to `interpretation`, `mapping` or `io`.
That is already close to true: those three layers reference no 2D type at all
(§3.1), because a `WeightedNode` is a node id and a float. If an MVC kernel
turned out to need changes there, the contract would be wrong and that would be
the finding, not an inconvenience.

### Why it is not v1

The triangle solve is the foundation and it is stable — 31 vectors including
nine on affine invariance, and every mutation caught. MVC has different
degeneracy behaviour, different cost, and different edge cases at the polygon
boundary, and it needs its own reference implementation and its own vectors
before it is trusted.

Writing the triangle case as though a polygon case were a parameter of it would
produce a worse triangle implementation and would not actually generalize. The
`Region` interface (§7) exists so the second kernel can be added later without
touching `Manifold2D`, which is the whole point of having built it that way.

**`Manifold3D`.** Would be a sibling class taking `glm::vec3`, not a subclass.
Everything downstream of the weight vector is already dimension-free —
`interpretation`, `mapping` and `io` reference no 2D type at all — so it would
share that stack verbatim and belongs in this repository if it is ever built.

May never be. The historical answer to three dimensions was several concurrent
2D maps plus spread, which is cheaper, authorable, and already expressive.

**GPU bake (§21.5).** A manifold baked to a lookup texture: region ID plus three
barycentric coordinates plus three node indices, packing into two RGBA16
attachments. Evaluation becomes free for arbitrarily many points.

Speculative and risky. It is an identity buffer, so no filtering, no blending,
no MSAA — and the failure signature is plausible weights over a corrupted region
ID, identical in both paths, invisible until it is not. If built, it is a
standalone module verified against the CPU evaluator vector-for-vector before
anything depends on it. The only thing owed to it now is keeping `solveRaw()` a
pure function of three positions and a point, which it is.
