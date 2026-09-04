# ROADMAP

What is built, what is next, and what is deliberately parked. Kept separate from
`DECISIONS.md`, which records reversals rather than intentions.

---

## Built and proved

Every layer below `src/ofx` is verified against an independent implementation in
another language — two solves, two containment tests, two JSON parsers, and for
serialization the standard library's own JSON parser against a hand-written one.

| Layer | Suite | What it covers |
|---|---|---|
| `core` | 31 + 48 | triangle solve, bias, degeneracy, affine invariance, containment, topology, inverse evaluation, node movement, evaluator hint |
| `interpretation` | 60 | curves, spread, blend, interpolate, coverage |
| `mapping` | 35 | bindings, aggregators, dense output, cross-manifold blending by name |
| `io` | 42 | JSON round trip, rejection, diagnosis |
| `sources` | 51 | trajectories, real-time replay, velocity, the touring case |

**267 vectors, 53 mutation gates**, on Linux x86_64 and macOS arm64 every push.

## Built, judged on screen

`src/ofx` has no second implementation and no ground truth (DECISIONS.md D-010).
It is checked for typos against a stub and judged by eye.

- **renderer** — nodes, regions, edges, active region, control point, weights,
  topology faults drawn where they are
- **`example-basic`** — drag the point, drag a node and watch a move refused
  when it would invert a region; three fixtures including a deliberate
  T-junction and an overlap showing hysteresis
- **`example-parameter-morphing`** — four presets, every field interpolating
  independently
- **`example-mapping`** — the node taxonomy: null ring, composite node,
  many-to-one, aggregators, dense output
- **`example-trajectory`** — record, replay in real seconds, swap venue
  mid-playback
- **`example-blend`** — two maps stacked, crossfaded by target name
- **`example-spread`** — pinpoint to wash, and the fade that emerges from
  spreading onto null nodes (D-014)

---

## Next

Two items, in no fixed order. Neither blocks anything.

### Smoothing (§12) — the one real work would notice

Named in the architecture since the beginning and still unbuilt. A
`WeightSmoother` with per-frame slew limiting.

Every real control source is jittery. A tracker, a fader, a network message —
all of them produce a point that jumps, and a weight vector that jumps is zipper
noise in audio and popping in visuals. The manifold evaluates instantaneously
and correctly; something downstream has to slew.

It is also **shaped differently from everything else here**, which is the
interesting part. The kernel is stateless by design, so every existing vector is
a single evaluation: one input, one expected output. A smoother has memory, so
its vectors have to be *sequences* — feed a step and assert the approach, feed
noise and assert the reduction, feed silence and assert it settles rather than
drifting. That is a new testing shape for this project and worth getting right
deliberately.

### Larger mesh

Everything so far is five to nine nodes. Thirty would show whether the renderer
stays legible at density and whether the `Evaluator` hint earns its keep.

Deliberately after the tag: it may change the renderer, and the renderer is the
layer with no tests behind it, so a change there wants its own release rather
than riding along.

---

## Later

**Interactive editor (§10).** Create, drag, name, connect, save.
`example-basic` already does half of it by accident — node dragging with
inversion refusal is the hard part and it works. What is missing is adding and
removing nodes and regions, naming them, and saving.

**Trajectory curve format.** Sampled points were the right first choice: it is
what recording produces, it preserves timing exactly including pauses, and it
needed no decision about curve families. A parametric format can be added later
as a second representation. Going the other way — from a fitted curve back to
what actually happened — cannot.

---

## Parked, with reasons

**A second coordinate kernel: mean-value coordinates over polygons.**

Not "triangles but with more vertices". A second generalized-barycentric
algorithm sitting alongside the triangle solve and producing the same weight
vector, so that everything downstream is untouched.

MIAP already works this way: a **polyset** of N ≥ 3 nodes, weights from
**mean-value coordinates**, which reduce exactly to ordinary barycentric
coordinates at N = 3. That reduction is what makes it a clean extension rather
than a replacement — the existing 31 triangle vectors would still have to pass.

References: M. S. Floater (2003), "Mean value coordinates", *CAGD* 20(1), 19–27;
K. Hormann & M. S. Floater (2006), "Mean value coordinates for arbitrary planar
polygons", *ACM TOG* 25(4), 1424–1441.

The invariant this must preserve:

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
That is already close to true: those layers reference no 2D type at all (§3.1),
because a `WeightedNode` is a node id and a float. If an MVC kernel turned out to
need changes there, the contract would be wrong and that would be the finding,
not an inconvenience.

Not v1 because MVC has different degeneracy behaviour, different cost, and
different edge cases at the polygon boundary, and it needs its own reference
implementation and its own vectors before it is trusted. Writing the triangle
case as though a polygon case were a parameter of it would produce a worse
triangle implementation and would not actually generalize. The `Region`
interface (§7) exists so the second kernel can be added without touching
`Manifold2D`.

---

**`evaluateAffine()` — deliberate extrapolation.** Barycentric coordinates
outside a triangle are well defined and go negative, which is extrapolation
rather than interpolation: a point beyond the states that defined the space.
Interesting as a creative primitive.

Parked because it is purely additive — a new method that never touches
`evaluate()`, over arithmetic `solveRaw()` already computes. Nothing about
waiting makes it harder, and no consumer wants it today.

---

**Weight derivatives as a general facility.** `d(weight)/dt` as a reusable
control signal. Correct and useful, and it does not belong in the manifold: the
evaluator is stateless by design and a derivative needs history.

Note the distinction that `Trajectory::velocityAt()` already exercises. A
*recorded path* is history — complete before anyone asks — so a derivative over
it is a pure function of data in hand. A *live* weight stream is not, and
differentiating it needs the same machinery as smoothing. The first is built;
the second waits with smoothing.

---

**Signed areas in the result.** `areaA` is `wA` times the total area, so it is
derivable from what is already returned. API surface with no new information. If
a diagnostic needs it, the renderer can compute it.

---

**`Manifold3D`.** Would be a sibling class taking `glm::vec3`, not a subclass.
Everything downstream of the weight vector is already dimension-free —
`interpretation`, `mapping` and `io` reference no 2D type at all — so it would
share that stack verbatim and belongs in this repository if it is ever built.

May never be. The historical answer to three dimensions was several concurrent
2D maps plus spread, which is cheaper, authorable, and already expressive.
`example-blend` demonstrates exactly that, and it is hard to look at and still
want tetrahedra.

---

**GPU bake.** A manifold baked to a lookup texture: region ID plus three
barycentric coordinates plus three node indices, packing into two RGBA16
attachments. Evaluation becomes free for arbitrarily many points.

Speculative and risky. It is an identity buffer, so no filtering, no blending,
no MSAA — and the failure signature is plausible weights over a corrupted region
ID, identical in both paths, invisible until it is not. If built, it is a
standalone module verified against the CPU evaluator vector-for-vector before
anything depends on it. The only thing owed to it now is keeping `solveRaw()` a
pure function of three positions and a point, which it is.

---

## A note on choosing

Everything since publication has been chosen from this list rather than from
something that came up while making work. That is a reasonable way to fill in an
architecture and a poor way to find out what is actually missing.

The parts-bin approach works best when the next part is the one that was absent
during real use. Four examples and two features in, the most useful next move
may be to build something with it and let the gap announce itself.
