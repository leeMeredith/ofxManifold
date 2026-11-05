# Round 3A: the room, the 3D map, and what a node carries

The kernel, formats and editor half of round 3. The player (3B) comes after.

The aim, in the user's words: a map in three dimensions, so a bird on a high
branch, a bird on a middle branch and an animal on the ground each sound where
they are. In a room with **two speakers**, that has to come from the
character of the sound: brightness, distance, reverb. In a room with **many
speakers**, including height, it comes from the speakers, and the same
character adds styling on top. One design has to play in both rooms. That's
the touring case again.

Kept separate from `ROADMAP.md` while the work is in progress. When it's done
it folds into `DECISIONS.md` and this file is deleted, as the earlier plans
were.

---

## Words

Each word means one thing, in code, comments and docs.

- **3D map:** one map in three dimensions. It has one **floor plan** (nodes
  and regions, as a map has today) and one or more **levels**. A 2D map is a
  3D map with one level.
- **level:** the floor plan at a height. Each level has its own heights per
  node, its own bindings and its own values. Between two levels the space is
  filled, so every point inside the volume has an owner.
- **blend:** separate maps side by side, crossfaded by gains. This is what
  example-blend shows, though its comments say "stacked". They get fixed, and
  "stack" is not used.
- **layer:** a map whose nodes pass their share down to another map through
  vias. Designed for, not built in this round.

A **source** is one sound with its own position (x, y, z) and its own levels.
The player has several. The roadmap's "one listening point" becomes "one point
per source".

---

## Boundaries

Meaning comes in only as you go up these levels, never back down.

- **Kernel (`src/`):** mechanisms with no meaning. No openFrameworks, no
  audio, no default that assumes audio. It stores names and numbers and never
  looks at what a name says. Where a choice has a meaning (a curve, a
  derived output's kind), the caller makes it explicitly, or the default is
  the plain one: linear.
- **Editor (`ofxManifoldEditor`):** making one map. Conveniences live here,
  such as `d` defaulting to after the fader.
- **Player (example):** the audio meaning. Sources, equal power, the value
  names it agrees on with its SuperCollider patch, OSC, the 3D view. Someone
  who wants none of it never compiles it.

SuperCollider is the first home for this, not the only one. An output can be
a speaker, a DMX dimmer or a shader uniform. A value can be reverb, a hue or
a frequency. A 3D map's z can be height, time of day or intensity.

**The one-map user sees nothing new.** One map feeding several outputs needs
no rig, no second level (a map with one level is today's map), no values,
and no sources. Rule:

> **No existing vector changes in round 3A.** The 623 vectors pass
> unmodified at every slice; everything new is added beside them. If an old
> vector has to change, something in the simple path changed, and the slice
> stops until that's understood.

---

## What exists today

Read from the code before this plan was written:

- **A map's outputs live inside its outputs file** (v2). Two maps meant for
  one room each carry a copy, and nothing checks that the copies agree.
- **A derived output reads nodes**, so it follows the map, ahead of every
  output's trim.
- **`blendByName` takes exactly two maps** and returns shares by name,
  before trim.
- **`interpolate()` blends any type** across a weight vector (shown in
  example-parameter-morphing), but nodes have nowhere to keep values, and no
  file stores them.
- **A path is (x, y) only.** `loadTrajectory` refuses a sample whose `p`
  doesn't have exactly two numbers.
- **The fader chart computes its own levels** from the scene's single map, so
  it can't show a blend.
- **`d` in the editor** makes a derived output from the selected nodes.
  Nothing places a ring or grid of nodes in one step.

---

## Decisions

### A. Derived outputs can read outputs (after the fader), and that's the default

Agreed, as the **editor's** default (`d`). The kernel has no default: whoever
makes a derived output says which kind. A derived output's sources are either
all nodes (before, as now) or all outputs (after), never a mix. After reads an output's **level**, its share
times its trim. The derived output's own trim applies on top, in
`toChannelLevels` only.

- **No chains:** an after-derived output can't read another derived output.
- **Removing an output** drops it from every derived output that reads it,
  with its gain. The derived output itself stays.
- Power-preserving mode works the same as before.

### B. The rig is every channel of the room

A **rig** is the room: every channel that leaves it. Outputs with name,
channel and trim; derived outputs fed by outputs (a sub belongs to the room,
and every map shares it); and **channels the map fills**: a derived output fed
by nodes, listed by name, channel and trim, with its sources left in the map
(decided 2026-11-03, after 3A-1's tests showed a rig losing such a channel).
That is SpaceMap's arrangement fitted to the split: a derived node's channel is
one of the system's speakers, and its links live in the map.

There's **no new class**. A rig is a `Mapping` with outputs and no bindings;
a channel the map fills is a node-fed derived output with no sources. So
`layoutChart` lays out a rig unchanged, and the rig's channel vector has the
map's arity. `rigOf(map)` takes a map's rig. It's saved as `name-rig.json`
with the outputs file's schema (G), a channel the map fills written with
`"feed": "map"` and no sources, and read by `loadRig`, which refuses anything
a rig can't hold.

### C. Each map keeps its own copy of the rig, and a check catches drift

**Recommended:** copy, not reference. A map that pointed at a rig file would
break when that file moved or changed, which is the D-018 to D-022 shape. The
agreement check (E) catches drift between copies.

### D. Node values: what a node carries besides its share

A **value** is a named number on a node, such as `distance` or `brightness`,
interpolated across the map by the same weights as the levels. A map declares
its **value set**: each value's name and default. A node without a value set
uses the default.

- **Values belong to the map file, not the outputs file.** They're design,
  not room. They travel with the geometry, so a 3D map taken to a new room
  keeps its character untouched.
- **Values are plain numbers.** The value set may declare a range for each
  value, 0 to 1 if none is given. The editor uses it for stepping and
  display. The kernel never clamps. What a number means, in dB, Hz, degrees
  or seconds, is the consumer's business.
- **A null node still carries values.** Values are about the place, not the
  routing. A silent corner can still be dark and distant.
- **Outside the map, every value is its default.** Coverage (`coverage()`)
  already measures how much of a point is inside.
- **Names are free-form**, and neither the kernel nor the editor suggests
  any. The player's starting names, `distance` (0 near, 1 far),
  `brightness` (0 dark, 1 bright) and `reverb` (0 dry, 1 wet), are a
  convention between the player and its SuperCollider patch, documented in
  the player example (I).

### E. Blending any number of maps, and the agreement check

`blendMaps` takes a list of (mapping, weights, gain) and returns a `Blended`:
shares by output name before trim, the values of the channels maps fill (a
derived output fed by nodes, decided in 3A-1) by name, and the silence. In
`src/mapping/ofxManifoldRig.h`, with `rigChannels`, `rigLevels`,
`checkAgreement` and `useRig`.

- **Gains come from the caller.** With a curve, each gain goes through it.
  The map of maps produces gains this way. A 3D map (F) does not: it is
  geometry, and gives weights, as a 2D map does.
- **Two maps give exactly what `blendByName` gives.** Checked by vector, so
  the old function becomes a special case.
- **The rig turns shares into channels and levels**, applying trim and then
  the after-derived outputs once, on the blended result. Trims come from the
  rig.
- **One map through the rig equals that map's own `toChannelLevels`.**
  Checked by vector.
- **Values blend by name** with the same gains, but **always linearly**.
  Values aren't power: equal power is for levels only.

**The agreement check** compares maps against each other and against a rig. It
reports:

- the same output name on different channels (disagreement),
- different names on the same channel (disagreement),
- a different trim for the same output (a note, since the rig's trim is used),
- one name computed from outputs in one map and filled by the map in another,
  or an output in one and a derived output in another (disagreement),
- with a rig, a channel a map sends that the rig doesn't have (disagreement),
- the same value name with a different default (disagreement; with values,
  in 3A-4).

Checking is separate from blending. Whatever loads maps together refuses one
that disagrees, and names where.

### F. The 3D map

One **floor plan** (the nodes and regions, edited as today) and one or more
**levels**. The levels are the same nodes at different heights, so each node is
connected straight up to itself on the level above. Each level has:

- **a height for every node.** A level starts flat at the height it was made.
  Raising selected nodes **bends** it (decided): only those nodes move up,
  and the triangles using them tilt to meet them, so the level stays one
  connected surface. The ground can rise toward a hill, a branch can lean.
- **its own bindings.** The canopy level can bind to ceiling speakers and the
  ground level to the floor. In a two-speaker room every level binds to the
  same two.
- **its own values,** from the map's one value set (D). The canopy can be
  bright and the ground dark.

The outputs (and rig) belong to the whole 3D map, once. So its levels can't
disagree about channels, and need no agreement check among themselves.

**Evaluating a point (x, y, z):**

1. Evaluate (x, y) on the floor plan, as today, to get its weights.
2. Each level's height at (x, y) is its node heights blended by those
   weights.
3. Find the two levels whose heights at (x, y) are just below and above z, and
   how far z is between them.
4. Resolve both levels with the same weights, and blend the shares by that
   distance. Levels use the caller's curve, linear by default; the player
   chooses equal power. Values always blend linearly.

**Properties, each checked by vector:**

- **At a level, the point is exactly that level.** Between two, shares still
  sum to one.
- **Flat levels make exact prisms:** barycentric across, linear up and down.
- **Bent levels stay continuous.** The weights are continuous across region
  edges, so the heights at (x, y) are too, and the vertical blend is linear
  between them.
- **One level means z is ignored,** so every map that exists today behaves
  exactly as it does now.

**Outside is outside.** Above the top level, below the bottom one or beyond
the floor plan's edge, the point is outside, exactly as off the edge of a 2D
map: no weights, and the consumer decides what that means. Nothing is clamped,
because clamping would invent an owner. A soft edge is built, not assumed: a
level of null nodes above the top fades to silence, the way a null ring does
in 2D (D-014).

**Levels can't cross.** Every node must be strictly higher on each level than
on the one below. A raise that would reach the next level is **refused**,
naming the node, the same way a 2D move that would invert a region is
refused. Because each node's heights are in order and the weights are never
negative, the levels' heights at any (x, y) are in order too, so step 3
always has one answer.

**Floor-plan edits reach every level.** Placing, moving, joining or removing a
node changes the floor plan, so it changes every level. A new node takes each
level's starting height and has no bindings or values until given them.

**How it's held (revised 2026-11-04: geometry first, as 2D is).** The kernel
already separates the shape from what it drives: `Manifold2D` turns a point
into weights over nodes, and only then does `Mapping` turn weights into
outputs. The 3D map keeps that split. Its first half is geometry alone, with
no outputs, gains or audio in it:

- **The floor plan is today's `Manifold2D`, unchanged.**
- **`Levels`** (`src/core/ofxManifoldLevels.h`) holds each level's starting
  height and the heights of nodes raised or lowered from it. Nothing else.
- **A level is made by copying one.** Every node of the floor plan is copied
  up (or down) in z, bends included. Each copied node sits directly above its
  original, so every column is one node at several heights. Refused, naming
  the node, if any copy would not be strictly between the levels around it.
- **Evaluating (x, y, z) gives 3D weights,** one per node per level: the
  floor plan's weight for the node, times how near z is to that level. They
  sum to one, are never negative, and are nonzero only on the two levels
  around z.
- **The test that proves it's a 3D map:** the weights reproduce the point.
  Each node on each level, at (x, y, its height), times its weight, summed,
  is exactly (x, y, z), bent levels included. That holds because the floor
  plan's weights already reproduce (x, y), and the vertical blend is linear
  between the two levels' heights. It's the 3D form of the property every 2D
  vector already relies on.
- **No hourglass, no two pyramids.** Every level has the same floor plan, so
  every horizontal slice of the volume is the same shape: nothing can narrow
  to a waist or a point. Levels can't cross or touch (a raise that reaches
  the next level is refused), so no column pinches to zero thickness and no
  cell turns inside out.
- **Floor-plan edits follow `Mapping`'s rule.** A new node needs nothing: on
  every level it has that level's starting height. Removing nodes is
  `levels.remapNodes(table)` with the table `Manifold2D::removeNodes()`
  returns.

The second half comes after the geometry is proven, and changes nothing in
it: bindings per level and node values per level, read through the 3D
weights exactly as a 2D map's bindings and values are read through its
weights. The outputs belong to every level at once, so two levels can't
disagree about the room. A derived output fed by nodes reads the floor plan's
weights, so height doesn't change it. Then files: the map file gains `values`
and `levels`, written only when needed; a binding on a level other than the
first carries `"level": k`, which makes the outputs file version 3.

**Later, not now:**

- **Different floor plans per level,** such as a canopy shaped unlike the
  ground. The space between would be only partly owned and would fade. It's
  legitimate, but it's the harder case, worth building once this one is in
  use.
- **A full lattice,** maps at grid points in all three directions. It would
  be the same rule in each direction, and nothing here prevents it.

### G. Formats first

Same rule as D-022: write the lowest version that reloads exactly. Every file
that doesn't use something new stays byte-identical.

- **Outputs file v3:** only when a derived output reads outputs. Its sources
  are written `{ "output": "L", "weight": 1 }`. A rig file is always v3
  and says `"kind": "rig"`, so it is never mistaken for a map's outputs
  file. It has outputs and output-fed derived outputs, and nothing else: a
  rig with bindings, or with a derived output that reads nodes, is refused.
  A map's outputs file may still open a rig, as a map with outputs and no
  bindings.
- **Map file:** a `values` section (the value set) once, and a `levels`
  list. Each level has its starting height, the node heights that differ
  from it, and its node values. Written only when the map has values or more
  than one level.
- **Outputs file:** a binding on a level other than the first carries
  `"level": k`. Absent means the first level, so existing files are
  unchanged. Any `level` makes the file v3.
- **Path file v2:** `p` has three numbers, written only when the path leaves
  z = 0. A flat path saves exactly as now.

**No stack file.** A 3D map is one map, in its usual pair of files, so there's
nothing that refers to other files and nothing that can go missing.

### H. The editor

- **`d`** makes a derived output from the **selected faders** (after). **`D`**
  makes one from the **selected nodes** (before), as `d` does now.
- **shift-cmd-r** saves the outputs as a rig. **cmd-r** uses a rig: the
  map's outputs are replaced, and bindings follow by name. If the map has
  outputs the rig doesn't, it's **refused**, naming them.
- **Node values** are set the way fill is: select nodes, pick a value, step
  it up or down within its declared range. The editor lets you name a new
  value. It suggests no names. Each node shows its value as a small mark. The exact keys
  get decided in that slice's plan, after using the slices before it.
- **`l`** places a ring on the polar grid (one node per spoke, on the outer
  ring) or an n × n block on the square grid (n from 2 to 8, set with
  `[ ]`). Each node gets a new output at the next free channel, the nodes are
  joined, and the whole layout is one undo step. A ring is numbered
  clockwise from the front. It's refused if any position already holds a
  node.
- **The fader chart draws levels it's handed** instead of computing them
  from one map, so the player can show a blend. It's a pure move: `make
  drawlog` must record identical drawing before and after.

**Levels.** The editor still edits one map, now a 3D one. Editing stays in
the 2D plan view, with height set by keys, so there's no picking in 3D:

- **The plan view shows the current level.** The other levels show faintly,
  for context. Keys move up and down between levels.
- **New level:** copies the current level (heights, bindings, values) to a
  chosen height above or below it. Each node is connected to itself
  vertically by construction. One undo step.
- **Raise or lower the selected nodes** on the current level, in steps like
  fill (shift for fine steps). Refused if it would reach the next level,
  naming the node.
- **Remove a level.**
- **A 3D view, for viewing only** (`ofEasyCam`): each level as a surface, each
  node's vertical connections as lines. It sits beside the plan view, and
  the player reuses it.

Sources belong to the player.

### I. What 3B builds on this

So the player is designed for it now:

- several sources, each with a position (x, y, z), an optional path, and live
  take-over,
- one 3D map, and later a map of maps blending between 3D maps,
- maps held **by role** (3D map, map of maps, and later layer), so layers
  drop in without a redesign,
- the editor's 3D view, with each source as a point and a line dropped to the
  level below,
- OSC out per source: levels at the rig's fixed arity, values, and position,
  so a sound engine can also render binaural 3D for two speakers or
  headphones,
- equal power between levels and for blends,
- its starting value names, `distance`, `brightness` and `reverb`, documented
  as the convention it shares with its SuperCollider patch.

---

## Done

### 3A-1: derived outputs after the fader, outputs v3, rig files

`addDerivedFromOutputs`, `Feed`, `rigOf`, `saveRig`, `loadRig`; outputs files
written as version 3 only when a derived output reads outputs. 37 vectors
(101 in the outputs suite), 15 mutation gates.

- **A test caught a false claim in the plan.** "The rig keeps the map's
  channel count" failed for a map whose highest channel was a derived output
  fed by nodes: the rig dropped it, five channels where the map had seven.
  Decided: a rig lists such a channel as one the map fills (B).
- **A refusal masked by another.** A map's outputs file is refused as a rig
  for having no `"kind": "rig"`, but also for having bindings, so removing the
  kind check left the suite green. A second fixture with outputs only, and the
  reason narrowed to `kind`, closed it. The bowtie lesson once more.
- **Node renumbering would have wiped an after-derived output's gains.**
  `remapNodes` rebuilt every derived output's gains from its node sources, and
  one fed by outputs has none. Found writing the code, held by a vector and a
  gate.
- **Every existing vector line is unchanged.** One fixture moved: the
  "future version" file said 3, which now exists, so it says 4. Its vector
  line, and what it tests, are the same.

### 3A-2: paths with height

A sample carries a height `z`; `heightAt`, `point3At`, the same in seconds,
`velocity3At`, `length3`, `hasHeight`, and `addSample(t, vec3)`. Everything 2D
is untouched: every 3D check also asserts that x and y equal what `pointAt` and
`velocityAt` give. Path files are version 2, every sample with three numbers,
only when some sample leaves the ground; flat paths are still version 1,
byte-identical. 31 vectors (82 in the trajectory suite), 12 mutation gates.

- **Height is judged on every sample, not the ends.** A path that starts and
  ends on the ground can hop in the middle; judged by its ends it would be
  saved flat and the hop lost. A vector holds exactly that path.
- **Masking again, caught before it shipped.** The "future version" path file
  first had three numbers per sample, so a version check that let version 3
  through was still refused, by the height rule. The fixture now has two.
- **One branch is unreachable,** and has no gate: two samples at one instant
  can't bracket a time between them, so `heightAt`'s coincident-sample return
  never runs. It is kept as a guard, as `pointAt`'s is.

### 3A-3: blending maps, the rig's channels, agreement, using a rig

`ofxManifoldRig.h`: `blendMaps`, `Blended`, `rigChannels`, `rigLevels`,
`checkAgreement`, `useRig`. Header-only, in the kernel. 29 vectors (130 in the
outputs suite), 19 mutation gates.

- **One map through its own rig gives exactly the map.** Every one-map blend
  record checks the rig path against the map's own `toChannels` and
  `toChannelLevels`, so the two paths cannot drift.
- **Two maps are `blendByName`.** Checked against the old function itself,
  not against numbers, so it is a special case rather than a second copy.
- **A sub is computed once, from the blend,** not blended from each map's own
  sub. Checked by a case where the two differ.
- **The pairs case was first written wrong.** It was meant to show a check
  that compares every map with the first alone missing a disagreement between
  the second and third. But channels are transitive: if the first agrees with
  both, they agree with each other. It only happens when the first map has no
  such output at all. The fixture now has a first map with no R.
- **Coverage gap closed before the gates.** The rig's power-summed sub was
  only ever reached through a map's own methods, never through the rig, so
  removing its square root in the rig would have passed. A blend through a
  rig with a power-summed sub closed it.

### 3A-4: the 3D map's geometry

`src/core/ofxManifoldLevels.h`: `Levels`, `copyLevel`, `raise`, `setHeight`,
`removeLevel`, `remapNodes`, `evaluate`, giving `Evaluation3D` with weights
over (node, level). Geometry only: nothing about outputs or sound. A new suite,
`levels`: 37 vectors, 16 mutation gates.

- **Every set of weights is checked to reproduce its point,** in the runner,
  from the C++ alone: weight times (x, y, height) summed over every node on
  every level is (x, y, z). Bent levels included. That is what makes it a 3D
  map rather than a stack of 2D answers.
- **A point exactly on the top level can read as above it.** Its height there
  is a weighted sum, and in single precision (0.108, 0.124) on a level at 1.0
  sums to 0.99999994. Found by searching the square for such a point; without
  the small tolerance the top of the map has holes. A vector holds it.
- **Two gaps closed before the gates.** Raising was tested all-or-nothing only
  when every node was blocked, so moving the unblocked ones would have passed;
  and a copied level was only ever refused by the level below it. A case for
  each, and both mutations are caught.
- **A runner mistake, not a kernel one,** on the first run: the continuity
  sweep compared weights including exact zeros, which the floor plan reports
  on the square's diagonal. The runner now drops them, as its other checks do.
- **CI on macOS arm64 failed where Linux passed.** An arm64 compiler fuses
  multiply-adds, so a weight that is exactly zero on a region's edge comes
  out as +-1e-10, and the runner counted it as a weight, or as a negative
  one. The kernel was right: it passes the floor plan's weights through.
  The runner now treats anything within its tolerance of zero as zero.
  `make fma` reproduces an arm64 build on x86 Linux with clang; every round 3
  mutation was re-run under it, and all are still caught.

---

## Answered

All as recommended (2026-11-03): the rig is copied into each map; a ring is
numbered clockwise from the front; `[ ]` sets the grid size on the square
grid; node values live in the map file; the player starts with `distance`,
`brightness` and `reverb`; a node placed on the floor plan appears on every
level at that level's starting height. Raising selected nodes bends their
level.

---

## Slices

Each slice is one green step before the next: Python reference first,
vectors, then C++, then mutation gates. Each feature's format is settled in
the slice that builds the feature, before any file of the new kind exists.

| Slice | What | Tested by |
|---|---|---|
| **3A-1** | Derived outputs after the fader, end to end: kernel, removal rule, outputs file v3, rig files | outputs vectors, round trips, existing files byte-identical, gates |
| **3A-2** | Paths with z: `Trajectory` in 3D, path file v2 | trajectory vectors, flat paths byte-identical |
| **3A-3** | `blendMaps`, the rig turning shares into channels, the agreement check, using a rig | two-map and one-map properties, gates |
| **3A-4** | The 3D map's geometry: levels, copying a level, raising nodes, no crossing, 3D weights at (x, y, z) | partition, reproduction of the point, continuity, prisms, gates |
| **3A-4b** | What the 3D map drives: bindings per level, node values, map file levels, bindings by level | resolution through 3D weights, round trips, existing files byte-identical, gates |
| **3A-5** | Editor: `d` / `D`, use and save rig, chart drawing levels it's handed | editor scripts, door check, drawlog identical |
| **3A-6** | Editor: levels (new, switch, remove), raise and lower, the 3D view | editor scripts, judged on screen |
| **3A-7** | Editor: setting and showing node values | editor scripts, judged on screen |
| **3A-8** | Editor: ring and grid layout | editor scripts, drawlog session extended |

A slice that adds a `.cpp` file says so, and a Project Generator Update comes
before the build.
