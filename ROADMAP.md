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
| `core` (regions) | 113 | polygons of N ≥ 3 by mean-value coordinates, non-convex regions, batch moves, removal |
| `core` (levels) | 37 | the 3D map's geometry: levels copied from the floor plan, bending, no crossing, weights at (x, y, z) that reproduce the point |
| `interpretation` | 69 | curves, spread, blend, interpolate, coverage, smoothing, negative weights |
| `mapping` | 35 + 130 | bindings, aggregators, dense output, blending by name; outputs with channel and trim, silence, fill, derived outputs before or after the fader, rigs, blending any number of maps, the agreement check |
| `io` | 55 | JSON round trip, rejection, diagnosis, the outputs file versions |
| `sources` | 82 | trajectories, real-time replay, velocity, the touring case, paths with height |
| `authoring` | 102 | grids: free, lattice, polar; nearest-point snapping, hysteresis |
| `ofxManifoldEditor` (logic) | 31 scripts, 428 steps | editing operations, selection, undo, files, fader layout, link lines, keys |

**757 vectors, 210 mutation gates**, on Linux x86_64 and macOS arm64 every push.
`make door` checks the editor's one door; `make drawlog` records every drawing
call of a scripted editing session (DECISIONS.md D-024).

---

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
- **`example-smoothing`** — raw against smoothed over time, and the four kinds
  of discontinuity worth smoothing
- **`example-regions`** — triangle, hexagon, L-shape and star regions
- **`example-editor`** and the **`ofxManifoldEditor`** views — place, select,
  move, join; outputs, bindings, fill and trim; a permanent listening point
  with live faders; link lines; undo; named saves as a pair of files; keys lit
  when they act. Built over two rounds in use (DECISIONS.md D-023, D-024)

---

## Next

**Round 3: several maps, one rig, and a player.** The editor authors one map at
a time and stays that way; working with several maps belongs in a player.

- **3A, kernel and editor.** A **rig**: an outputs file with outputs and no
  bindings, which any map can use, so maps authored against the same rig blend.
  **Blending more than two maps** by output name, with a check that maps
  sharing an output name agree on its channel. **Derived outputs fed before or
  after the fader**: from source nodes, as now, or from outputs as they leave,
  as a sub usually wants. In the editor, **one-step ring and grid layout** of
  nodes, each with its channel. File format changes go first, before more files
  exist.
- **3B, the player example.** Several maps and one rig; one listening point
  evaluated on every map; the blend, possibly controlled by a small map whose
  nodes are the maps; **mute**; trajectory playback; spread; smoothing; OSC
  out. It reuses the editor's views (`ofxManifoldEditorViews.h`).

### Larger mesh — MEASURED

`make bench` runs triangulated grids from 16 to 10000 nodes. Findings in
DECISIONS.md D-015, in short:

- **evaluation is never the problem** — 10000 nodes costs 43 us on x86 and
  15 us on Apple Silicon with no hint at all, well under 1% of a frame. The
  spatial index this item anticipated would optimize something that does not
  cost anything.
- **`validate()` sets the ceiling** at O(regions x 3 x nodes), crossing a frame
  budget around a thousand nodes on both platforms.
- **`example-basic` was revalidating every frame while dragging a node.** Fixed:
  once on release. That was the only real cost found.

Not optimized, deliberately. It is authoring-time work and the practical
ceiling is far past any hand-authored map. `example-basic` fixture 4 is a
144-node grid for judging the renderer at density; press `l` to hide labels,
which are the first thing to fail.

---

## Later

**Trajectory curve format.** Sampled points were the right first choice: it is
what recording produces, it preserves timing exactly including pauses, and it
needed no decision about curve families. A parametric format can be added later
as a second representation. Going the other way — from a fitted curve back to
what actually happened — cannot.

---

## Editor — after round 2

**Halos in the node's own shape.** A triangle node's halo a triangle, a square
node's a square, each scaled so that equal share means equal area -- at the
same size a triangle covers about 40% of a circle's area, and would read as a
smaller share. Asked for, then deferred by the user.

**Layers and vias.** Maps nested within maps: a node that passes its share down
to another map evaluated at the same point, which measured as smooth as a
single map where re-reading a region in its own coordinates jumped by 0.57.
Layer order makes loops impossible to build. After the player shows how
multi-map work feels in use.

**3D stacked view, for viewing only.** Each map as a plane at its own depth;
vias as vertical lines between them. View only: editing stays 2D.

**Node values.** The third part of a node beside geometry and routing: an
attribute bundle interpolated across the map -- reverb send, filter, width --
for parameter maps and authored distance cues.

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
