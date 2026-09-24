# Authoring grids — plan

Snapping for node placement in the editor, with an explicit free-hand mode.

Kept separate from `ROADMAP.md` while the work is in progress. It folds into
`DECISIONS.md` when done, and this file is deleted, as `PLAN-regions.md` was.

---

## Why this before the rest of the editor

Placing a node is the editor's most frequent action. Whether it lands exactly
where the pointer is or on a grid decides what "drag", "add", "already a node
here" and "build a region from these" mean. Settling it first means the rest of
the editor is built on one placement model rather than retrofitted onto a
second.

---

## Decisions settled before writing code

### A · Not a DGGS

A Discrete Global Grid System tiles a **sphere** into near-equal cells with
hierarchical addresses, getting there through an icosahedral projection and
accepting distortion as the price. The manifold is a flat unit square with no
distortion to manage. A DGGS would import the machinery and its cost without
the problem it solves.

What was worth taking from the idea is kept: **one abstraction instead of many
options**, **free hierarchical subdivision**, and **integer cell addresses**.
All three exist in the flat case, more simply.

### B · Three modes: free, lattice, polar

**Decided: one `Grid` value type with three modes, and free-hand is one of
them.**

- **Free** — `snap(p)` returns `p` unchanged. Explicit, not an absence.
- **Lattice** — two basis vectors. One implementation gives square,
  rectangular, triangular, hexagonal and sheared grids by changing those two
  vectors. Subdivision halves the basis, so every level nests in the one above.
- **Polar** — rings and spokes around a centre. The one structure a lattice
  cannot make, and the natural shape of both a star and a surround rig.

Why free is a mode rather than a flag the editor checks: **one code path.** The
editor calls `grid.snap(p)` everywhere, and in free mode that is the identity.
No `if (snapping)` scattered through drag, place, and duplicate handling —
which is how a code path gets missed and free-hand quietly snaps in one place.

### C · Snapping returns the NEAREST point, not the rounded one

Measured before any code, and both grid kinds fail the obvious approach.

Rounding in lattice coordinates is exact only for axis-aligned grids:

    square        non-nearest   0.0%
    rectangular   non-nearest   0.0%
    triangular    non-nearest  16.2%   worst extra distance 0.036
    sheared       non-nearest  11.4%

Rounding radius and angle separately ignores that outer spokes sit further apart
than inner ones:

    rings 0.08,  8 spokes   non-nearest  10.4%
    rings 0.08, 16 spokes   non-nearest   3.1%
    rings 0.05, 12 spokes   non-nearest   8.5%
    rings 0.10,  5 spokes   non-nearest  22.1%   <- a five-pointed star

On a triangular grid, roughly one click in six would snap to a neighbour of the
vertex it landed beside. On a five-spoke polar grid, more than one in five.

The fix is the same for both: round to get a candidate, then check the few
surrounding grid points and take the closest. Cheap — a handful of distance
comparisons — and it has to be pinned by vectors, because it looks right on a
square grid and wrong on exactly the grids a star or a surround rig uses.

### D · Snapping never moves existing nodes

**Decided: a grid applies to placement and dragging only.**

Changing grid, or its spacing, does not re-snap nodes already placed. Doing so
would destroy authored positions, and could invert a region — which
`setNodePosition()` would refuse partway through a batch, leaving a map half
re-snapped. "Snap all nodes to this grid" can exist later as an explicit,
undoable command.

### E · A snapped drag still goes through setNodePosition()

**Decided: snapping chooses the target; `setNodePosition()` decides whether the
move is allowed.**

On a coarse grid a drag moves in jumps, and one jump can carry a node across the
opposite edge of a region where a smooth drag would have stopped at the fold.
The inversion refusal (§8.6) must still apply to the snapped position, not be
bypassed by it. A refused snapped move leaves the node at its last valid grid
point.

### F · Integer addresses, and what free mode does without them

**Decided: lattice and polar give every snapped position an integer address;
free mode does not.**

Lattice gives `(i, j)`; polar gives `(ring, spoke)`. That makes three things
nearly free:

- **"A node is already here"** — placing onto an occupied address is caught
- **Neighbours** — adjacent addresses are adjacent nodes, which is what
  "build a region from these" needs
- **Conforming meshes by default** — on a triangular lattice, T-junctions
  become hard to create by accident

Free mode has no addresses, so duplicate detection there falls back to a
distance tolerance. That is a genuine difference in behaviour between modes, and
it is stated here rather than discovered.

### G · Where it lives

**Decided: `src/authoring/`, glm only, no openFrameworks.**

Snapping is how a map is **made**, not how one **evaluates**, so it is not core.
But it is pure arithmetic, so it gets the same treatment as the kernel: a Python
reference, vectors, mutation gates, and the header must compile standalone.

Grid settings are **not** written into the manifold file. A manifold describes
relationships; which grid its author happened to use is a property of the
editing session. Saving it would put a tool preference into a file meant to be
portable across venues.

### H · Rotation is a parameter of every grid

**Decided: every grid takes a rotation angle about its origin.**

Measured, rotation changes nothing about a grid's properties — spacing,
neighbour count, and even the rounding error are unchanged at every angle
(square stays at 0% non-nearest, triangular near 17%). It changes only how the
grid sits against the square's edges. In the lattice model it is a
multiplication of the two basis vectors; for polar it is the spoke offset.

This also **settles the star question without deciding it.** Which way spoke
zero points is no longer a default anyone has to choose: turn the grid until a
spoke is where you want it.

Flip is offered but secondary. These grids are symmetric — a triangular lattice
rotated 60 degrees is the identical point set, measured — so most reflections
equal some rotation. The exception is a sheared lattice, where a flip gives
something no rotation does.

### I · Ties resolve by staying put

**Decided: hysteresis during a drag, lowest address on a fresh placement.**

Exact ties almost never occur in floating point. Near-ties occur constantly: a
drag hovering on the boundary between two grid points, where pointer jitter
would flip the snap back and forth every frame. So the useful rule is not a
tiebreak but **hysteresis** — keep the current snapped point until the pointer
is clearly closer to another, by a small dead zone. Same idea as the
Evaluator's region hint (section 8), applied to snapping.

A first click has no current point, so it falls back to a fixed rule: the
lowest address. Any deterministic rule would do; this one is written down so
the reference and the C++ agree.

Free mode has no addresses and no ties, so neither rule applies there.

### J · Moving several nodes is one operation, not many

**Decided: a batch move in the kernel, `setNodePositions()`, all or nothing.**

Moving a selection one node at a time through `setNodePosition()` checks each
move against positions partway through the group's move. Moving A before B can
briefly invert a region that the finished move would not, and the per-node
refusal then leaves the selection half moved.

The batch version checks every region touched by any moved node against the
**final** positions, then applies every move or none. It works in all three grid
modes, including free.

With snapping on, the group's **offset** is snapped, not each node. Snapping
each node independently would distort the shape of the selection — nodes that
were evenly spaced could land unevenly.

This is a kernel change, `Manifold2D`, so it gets vectors and gates like the
rest of the kernel.

### K · Moving a node enforces the same rules as building a region

**Decided: `setNodePosition()` and `setNodePositions()` refuse any move that
would produce a region construction would refuse.**

Found while working out the batch move, and it is a bug in the kernel as it
already stands, not only in the plan. `setNodePosition()` checks one thing:
that a region's winding sign has not flipped. For a triangle that is enough —
three edges cannot cross each other, so the only way to break one is to invert
it. For a region of four or more it is not.

Measured: a legal convex quad, one corner dragged across. Construction refuses
the result as self-intersecting. `setNodePosition()` accepted it, because the
signed area went from 0.72 to 0.15 without changing sign. The region was left
self-intersecting.

Same shape as D-018: the rule applied when a region is built and the rule
applied when it is edited had drifted apart, and the regions work introduced
the gap without anything catching it. A move now runs the full construction
check — self-intersection and degeneracy — plus the winding sign.

### L · Two rules the C++ must not leave to the language

Both measured while building the reference.

**Reduce the lattice basis once, then search one step each way.** Checking the
eight neighbours of the rounded point is enough for every sensible grid. A
strongly sheared lattice still misses — 43 times in 6,000 — because a skewed
basis puts the true nearest point further out. Reducing the basis first
(Lagrange-Gauss: replace a skewed pair of basis vectors with the shortest
equivalent pair) makes one step each way always sufficient. It is done once when
the grid is made; snapping stays at nine candidates.

**Ties are broken explicitly, never by `round()`.** Python rounds halves to
even; C++ rounds them away from zero. `round(0.5)` is 0 in one and 1 in the
other. A reference built on the language's rounding would disagree with the
C++ at exactly the tie points decision I exists for. Both compare distances
and apply the lowest-address rule in their own code.

---

## Work, in order

1. **Python reference — DONE.** `tests/ref/reference_grids.py`, 73 vectors
   (58 ANALYTIC, 15 CROSS). Brute-force nearest point over a wide window, so
   it cannot share the rounding shortcut's mistake. Self-checked: widening the
   window changes nothing; every rounding trap genuinely misleads naive
   rounding; an exact tie resolves to the lowest address where Python's
   `round()` and C++'s `std::round()` would disagree; the slide-right batch
   move is accepted as a batch and refused one node at a time; the quad drag
   that keeps its winding but self-intersects is refused.

   Found along the way: decisions K (a live kernel bug) and L (two rules the
   C++ must not leave to the language).

   Note for the C++: the tie tolerance here is 1e-9 on doubles. On floats it
   needs to sit above the float noise floor (D-001), so around 1e-6.
2. **Vectors — DONE**, 81 in `tests/vectors/grids.vec`.
   - free mode is the identity, exactly
   - square and rectangular: snapped point equals rounded point
   - triangular, hex, sheared and polar: snapped point is the TRUE nearest,
     including sampled points where plain rounding is known to be wrong —
     found by searching, not hand-picked
   - polar centre: angle undefined, snaps to the centre
   - subdivision: every point of level n is a point of level n+1
   - integer addresses round-trip: address to position to address
   - a point exactly between two grid points resolves deterministically
   - rotation: snapped points of a rotated grid are the rotated snapped
     points of the unrotated one, and the nearest-point property survives
   - hysteresis: a point inside the dead zone keeps its current snap
   - moving a node so a region self-intersects is REFUSED, single and batch,
     including a move that keeps the winding sign
   - batch move: a move that inverts a region partway through a one-by-one
     sequence but not at the end is ACCEPTED as a batch; a move that inverts
     at the end is refused with nothing applied
3. **C++ — DONE.** `Grid` in `src/authoring/ofxManifoldGrid.h`, double
   precision internally. `Manifold2D::setNodePositions()`, with
   `setNodePosition()` now a batch of one. 81/81 on first full run against
   the reference, including the sheared address (-11, 6) reported in the
   original basis after searching in the reduced one.
4. **Mutation gates — DONE**, 9 caught, 8 as CI gates. Two were missed on
   the first pass: basis reduction and the full polar ring search. Both had
   been MEASURED as necessary this round and neither was pinned by a vector,
   because the traps targeted plain rounding only. Traps aimed at each
   shortcut now catch both, four vectors apiece. Originally listed: naive rounding restored, neighbour search removed,
   polar centre case removed, free mode snapping anyway, rotation ignored,
   hysteresis removed, batch move checking intermediate rather than final
   positions, batch move applying partially
5. **Editor integration — BUILT, awaiting the screen.** `example-editor`.
   Selection is both: shift-click toggles, a drag across empty space boxes,
   and a click on empty space that does not drag places a node, so no mode
   switch is needed. Two faults caught before handover: a per-frame
   `validate()` (the D-015 cost, reintroduced) and hysteresis memory surviving
   an alt free-hand stretch of a drag. Originally listed: — placement and drag through `grid.snap()`, snapped
   drags through `setNodePosition()`, multi-select moves through
   `setNodePositions()`, duplicate detection by address or by tolerance

---

## Open, to settle during the work

1. **A temporary override.** Holding a modifier to place free-hand while a grid
   is active is standard in drawing tools and costs little. Leaning yes, as an
   editor feature rather than a grid one.
2. **Triangular and hexagonal as one type or two.** They share lattice points
   and differ in the cell drawn around them. For placing nodes only the points
   matter, so one type — but the editor may want to draw the cells differently.
3. **Hysteresis dead zone size.** A fraction of grid spacing is the natural
   unit. Wants trying on screen rather than choosing on paper.
4. **Selection model.** Click to add, drag a box, or both. An editor question
   rather than a kernel one, and it does not affect the batch move.

Settled since the first draft: which way polar spoke zero points (decision H —
rotate the grid) and how ties break (decision I — stay put).
