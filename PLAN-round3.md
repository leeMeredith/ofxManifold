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

- **The plan view shows the current level.** Keys move up and down between
  levels. (This said the other levels show faintly. That was written before
  every level shared one floor plan: their nodes would sit exactly under the
  current level's. A column of dots, one per level, shows them instead; see
  the 3A-6 proposal.)
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

#### 3A-6 proposal: levels in the editor (agreed 2026-11-05; see Done)

**The model holds a 3D map.** `editor::Model` keeps a `Map3D` and a current
level. `mapping()` returns the current level's `Mapping`, so every view and
every existing script reads exactly as before. Output operations (o, O, t, d,
D, cmd-r) go to every level, as `Map3D` already does; binding operations (b, B,
q w, c) go to the current level. A map with one level is today's editor: the
36 scripts and 477 steps pass unchanged, and `make drawlog` records identical
drawing for the existing session.

**Keys, a new group "levels":**

| Key | Does | Undo step |
|---|---|---|
| ↑ ↓ | the level above or below becomes current; "level 2 of 3, at 0.50" | no, like picking a fader |
| n | new level above: a copy of the current one (heights, bindings, values), halfway to the level above, or 0.25 above the top. It becomes current | yes |
| N | new level below, the same way | yes |
| y h | raise or lower the selected nodes on the current level, 0.05 a step (shift: 0.01), landing on the next multiple as fill does. Refused if a node would reach the level above or below, naming it | yes |
| cmd-delete | remove the current level; refused if it is the only one | yes |
| cmd-3 | the 3D view on or off | no |

y sits above h on the keyboard, for up and down. Moving a whole level is a
then y. Heights are in map units: the floor plan is 0 to 1 across, so 0.25
is a quarter of its width.

**What you see:**

- **A column of dots** at the side of the plan view, one dot per level, top
  level at the top (Lee, 2026-11-05), so a 3D map reads as one in the 2D
  plan view without the 3D view open. The current level's dot is filled, the
  others hollow, with the current level's starting height beside it. Evenly
  spaced, not spaced by height, so two levels close together stay two
  readable dots. Drawn only with two or more levels, so a 2D map shows
  nothing new.
- **A bent node** shows its offset on the current level ("+0.10") beside
  it, only where it differs from the level's starting height.
- **Link lines, node shapes and fills** are the current level's.
- **Audition moves through the 3D space** (Lee, 2026-11-05). The listening
  point has a height. It starts on the current level, at that level's height
  under the cursor, so what you hear first is the level you are editing.
  While auditioning (space held, or locked with v), **↑ ↓ move the listening
  point up and down** instead of switching levels: held, it glides at 0.25 a
  second (shift: 0.05), through the space between levels and on past them, so
  you hear one level crossfade into the next. The mouse still moves it
  across. Above the top level or below the bottom one is outside, so it goes
  silent, as off the edge of the map does. The fader chart shows what the
  point reaches as it moves; in the column of dots it is a small mark that
  moves between the two dots it lies between, as far along as it is between
  those levels (the `t` of the 3D weights), and past the end dots, greyed,
  when it is outside; the 3D view
  shows it as a point with a line dropped to the level below, as the player
  will show a source. When audition ends, the point returns to the current
  level. The key panel says what ↑ ↓ do at the moment: "level up / down", or
  "listen higher / lower" while auditioning.

**The 3D view (cmd-3)** splits the map area: plan on the left, 3D on the
right. Viewing only, no picking; drag to orbit (`ofEasyCam`). Each level is
drawn as its regions' triangles at their node heights, faint, with the
current level brighter; each node has a vertical line up to itself on the
level above; selected nodes are highlighted. It lives in `views/` so the
player reuses it, with a source as a point and a line dropped to the level
below it.

**Files** use `saveManifold3D`, `saveMapping3D` and `loadMap3D`. A one-level
map saves byte-identical, which is already a vector, so opening and saving any
existing pair changes nothing.

**One kernel addition:** using a rig on a `Map3D`, every level at once, all
or nothing. Python reference first, in the levels suite.

**Tested by** editor scripts, Python reference first: a new level above and
below, switching, binding on one level leaving the others alone, raising
refused at the next level and naming the node, removing a level, undo across
all of these, placing and removing nodes on a three-level map, a save and
open round trip of three levels, and using a rig on three levels. The key
panel's availability for each new key. Mutation gates for each rule. The door
check covers the new operations. Where each dot and the listening mark sit
is computed by a pure function and checked by script; how they look, the
offsets and the 3D view are judged on screen.

**Placing a node with auto-output on binds it on every level** (Lee,
2026-11-05) -- *superseded by 3A-6d (2026-11-06): each level's node gets an
output of its own, P, P-2, P-3.* b, B, q w and c still act on the current
level only.

**Audition is tested too,** not only judged on screen: the chart is handed
what `Map3D` resolves at (x, y, z) -- checked against the resolve directly,
at a level, between two, and above the top -- and the listening point's
height is a small value the editor steps, so a script can hold ↑ for a second
and check where it got to, and that it glides through a level without
catching on it.

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

### 3A-4b: what the 3D map drives

`src/mapping/ofxManifoldMap3D.h`: `Map3D` holds the levels, one `Mapping` per
level and the node values, and is the only way to change them, so they stay
in step. Outputs go to every level at once; bindings and values to one.
Reading resolves each level with its own share of the weights and sums; a
derived output fed by outputs is computed once from that sum, one fed by nodes
from the floor plan's weights. 26 vectors (63 in the levels suite), 14
mutation gates.

- **Two properties checked on every reading,** from the C++ alone: every
  level agrees about the room (`checkAgreement` over all levels, notes
  included); and weights on one level read exactly as that level's own
  `Mapping` reads them. A Map3D with one level is today's map.
- **Two gaps found by the gates, both in the fixtures.** Every output was
  added while there was one level, then copied up, so adding an output to the
  first level only passed. And the node-removal case weighed the two
  renumbered nodes equally, so values left at their OLD ids summed to the
  same answer. An output added after the levels, and unequal weights, closed
  them.
- **A binding to an output that doesn't exist is now ignored.**
  `Mapping::bind` doesn't check the id, and a link past the end would be read
  out of bounds on every evaluation. The first gap above would have reached
  it.

### 3A-4c: files for the 3D map

`src/io/ofxManifoldSerialize3D.h`: `saveManifold3D`, `saveMapping3D`,
`loadMap3D`. The map file gains `values` and `levels` (version 3); bindings
above the first level carry `"level"` (outputs file version 3). A map with
one flat level at 0 and no values saves byte-identical to `saveManifold` and
`saveMapping`. `loadManifold` refuses a version 3 map file and `loadMapping` a
binding with a level, each naming `loadMap3D`, rather than dropping the
levels. `Levels::rebuild` checks a whole file's heights at once, and
`Map3D::adopt` checks that every level agrees about the room. 21 vectors (84
in the levels suite), 17 mutation gates.

- **The fixtures are written by the reference, with `json.dumps`,** not by
  the C++ writer, so loading them proves the reader and saving then
  reloading proves the writer.
- **Heights from a file are checked whole, not one at a time.** Set one by
  one, a valid file could be refused part way: a node raised on the ground
  before the same node on the level above.
- **A missing build dependency hid every serializer mutation locally.** The
  levels runner wasn't rebuilt when the serializer changed, so all fifteen
  file mutations first came back "missed". CI was never affected, since every
  gate starts from `make clean`, but a local `make test` after editing a
  serializer would have tested the old one. Fixed in the Makefile; all
  seventeen are caught.
- **One expectation of mine was wrong:** a one-level map with values saves
  its outputs file as version 3, not 2, because its sub reads outputs (3A-1).
  The C++ was right.

### 3A-5: the editor -- after the fader, rigs, the chart handed its levels

- **`d`** makes a derived output from the picked faders, after their faders
  (`Model::newDerivedAfter`). **`D`** makes one from the selected nodes, as
  `d` did. **cmd-r** uses a rig (`useRigFile`, `Model::useRig`), one undo
  step; **shift-cmd-r** saves the outputs as `name-rig.json`
  (`saveRigFile`). The current output and the picked outputs carry across a
  rig by name.
- **The chart draws levels it is handed.** `chartLevels()` computes what
  `drawChart` used to compute for itself; `drawChart` takes a `ChartLevels`,
  so the player can hand it a blend. A pure move: the recorded drawing of the
  30-frame session, built with only this change, is identical, 29,461 calls.
  The runner also checks `chartLevels` against the map's own reading at every
  CONTRIB record, inside and outside.
- 5 editor scripts (36), 50 checked steps (478), 10 mutation gates.

- **An understood change to old vectors.** The 15 KEYS records list every
  key, so each gains `derivedafter`, `userig` and `saverig`. Nothing else in
  the old editor vectors changed. This is the key panel growing, not the
  simple path changing.
- **Two gaps found by the gates.** Using a rig with only the current output
  picked could not show picked outputs being dropped, since settling puts the
  current one back; the script now picks two. And "outside" was tested with an
  empty weight vector, which hid a chart that ignored the inside flag; the
  check now leaves weights in an outside evaluation.

### Between 3A-5 and 3A-6: CI gates run only what they reach

`tests/gate.py FILE` asks the compiler (`-MM`) which suites compile the
mutated file, deletes those suites' binaries, and builds and runs only them,
plus the door check for an editor file. Every gate calls it instead of
`make clean` and all of `make test`. It fails safe: a file no suite reaches
runs everything, and a suite the Makefile runs but the script doesn't know
stops it with an error. The final "restored tree" step is still a clean full
run.

- **Measured here:** a clean full `make test` is 112 s; a gate is 10 to 52 s,
  about 13 s on average over all 251. The CI run should fall from about an
  hour to a fraction of that.
- **Every gate was run the new way before it was handed over,** and all 251
  are caught.
- **That run found a gate that had gone quiet.** In 3A-5, a second fader was
  toggled into the rig script to catch "using a rig drops the picked
  outputs". Toggling a fader makes it current, so the current output became
  the first one, and "using a rig loses the current output" -- which falls
  back to the first -- could no longer fail. The script now picks n0 first
  and toggles n1, so n1 is current but not first. Both gates are caught.

### Between 3A-5 and 3A-6: the gates split across six CI jobs

The 251 gates moved out of the workflow into a plain list, `tests/gates.txt`,
four lines a gate instead of eight lines of YAML, and `tests/run_gates.py`
runs them. CI's mutation job is now a matrix of six shards, each taking every
sixth gate, all running at once (GitHub's free plan allows 20 jobs at a time
on a public repository; this uses 8). The workflow went from 2,136 lines to
126.

- **The list was checked against the workflow it replaced:** every name,
  file, anchor and replacement read back identical before the YAML steps were
  removed.
- **`make workflow` guards the split:** the matrix must run shards 0 to N-1
  with the run line dividing by the same N, so a shard cannot be dropped
  without it failing; every anchor must match once; names must be unique; and
  no gate may be left written in the workflow.
- **Each gate restores its file's exact bytes,** caught or not, and the
  restored-tree step now also checks `git diff` is empty before the clean run.
- **Every shard was run as CI will run it before it was handed over,** and all
  251 are caught.

### 3A-6: levels in the editor

`editor::Model` holds a `Map3D` and a current level, and `mapping()` is the
current level's, so everything written before levels reads what it did.
Output operations reach every level; binding, fill, clear and heights the
current one. Keys: ↑ ↓, `n` `N`, `y` `h`, ⌘⌫, ⌘3. Placing a node with
auto-output bound it on every level (changed in 3A-6d). Saving and opening go through
`saveManifold3D`, `saveMapping3D` and `loadMap3D`, and a one-level map still
saves byte-identical. Listening in 3D is pure and tested
(`ofxManifoldEditorListening.h`): where audition starts, how far a held key
glides it, what the chart is handed at (x, y, z), and where the column of dots
puts each level and the listening mark. The views draw the dots, each bent
node's offset, and the 3D view (`views/ofxManifoldEditorLevelView.h`), and
draw nothing new for a flat map. One kernel addition: `Map3D::useRig`, every
level at once or none.

- **The earlier 36 scripts pass,** their only change the five new keys in
  their key lists, as 3A-5's keys did. The existing drawing session records
  identically outside the key panel's new group; the session gained seven
  frames at its end (levels, an offset, gliding, outside, the 3D view, a level
  removed).
- **`loadMap3D` refused every version 1 outputs file,** "binding to undeclared
  output": version 1 declares nothing, and the 3D reader only bound to
  declared outputs. Found by the editor's old-pair script, once opening went
  through it. An output a version 1 binding makes is now made on every level,
  so the levels still agree; two vectors in the levels suite.
- **The app held a reference that could dangle.** `ofApp` kept
  `const Mapping& mapping = model.mapping()`. With the mapping inside the 3D
  map's list of levels, a new level or an undo moves it; the drawing session
  crashed, and the address sanitizer named the line. The app now reads
  `model.mapping()` each time.
- **My reference forgot that a node with no bindings is silent whole.** The
  kernel had it right; the listening script's quad, half unbound, showed it.
- **A gate found the levels suite comparing too little.** Making a version 1
  output on the first level only went uncaught: comparing two 3D maps checked
  heights, values and readings, but not that every level holds the room's
  outputs. A level missing one reads the same until something is bound to it
  there, and then the binding is dropped. The comparison now checks every
  level's outputs. `Map3D::adopt` lets such levels through too (its agreement
  check looks for clashes, not absences); left as it is for now, noted here.
- 10 editor scripts (46), 126 checked steps (603); 6 vectors in the levels
  suite (90); 24 new mutation gates (275), and 10 earlier ones moved onto the
  new code.
- **Not checked here:** a real openFrameworks build; the 3D view's starting
  angle and its drag; how the dots, the offsets and the view look. Those are
  judged on screen.

### 3A-6b: the dots answer the mouse; a level moved whole; the keys beneath

Lee, on screen (2026-11-06): click a dot to go to its level; hold a dot and
drag it to bring a level closer to or further from its neighbours; and with
the 3D view open the key text ran off the window -- put it beneath the maps.
The 3D view itself stays where it is.

- **A click on a dot** makes that level current (`Model::goToLevel`), not an
  undo step.
- **Dragging a dot moves the whole level** -- its start and every node on
  it, bends kept -- and makes it current; one undo step on letting go
  (`Model::moveLevel`). A gap between dots is a quarter of the map, in steps
  of 0.01, and the drag stops 0.01 short of where a node or its start would
  reach a neighbour, so letting go always lands (`levelDragTo`). The dot
  follows the drag, yellow, with where it would land beside it.
- **`i` and `k`** move the current level 0.05 (shift: 0.01), landing on the
  next multiple as heights do (`Model::stepLevel`). i sits above k. They need
  two levels.
- **Kernel:** `Levels::moveLevel`, all or nothing, refused where any node or
  the level's start would reach a neighbour; `Levels::room`, how far a level
  can go each way. Three stacks of vectors.
- **With the 3D view open the keys go beneath the views,** in columns across
  the window, each group whole. The status lines stay beside the views when
  the space the views leave holds them, otherwise go beneath on the left. The
  views give up as much height as the keys need at that width. A window too
  small for all of it keeps the views at their least and lets the last
  column run on to the right rather than over the faders -- the drawing
  session's 1024 by 640 window is one such.
- The two unused references the Mac build warned about (`currentOutput`,
  `autoOutput` in `ofApp.h`) are gone.
- 3 editor scripts (49), 37 checked steps (640); 3 vectors in the levels
  suite (93); 11 mutation gates (286). The drawing session gained four
  frames: a dot clicked, a dot dragged, let go, `i` then shift-`k`.

### 3A-6c: keys split, every level's nodes glowing, heights matched

Lee, on screen (2026-11-06): the keys beneath the views still ran off to the
side; split them -- the usual ones where they always are, the 3D ones beneath
the 3D view. A glow around the nodes being heard in 3D, as the 2D map has.
A way to bring nodes to the same height: select the one to match first, then
the others. And every level's nodes made when it is copied, so moving through
3D space they light up as a connected group.

- **The keys, split.** With the 3D view open, the levels group sits beneath
  the 3D view and the rest of the panel stays beside the views, as always.
  The views are sized so the panel keeps room for its two columns and the
  levels keys fit between the 3D view and the faders. The beneath-the-views
  layout of 3A-6b is gone.
- **Every level's nodes in the 3D view,** the current level's brighter and
  larger, its selected ones yellow. Each level is a copy of the floor plan, so
  each has its own nodes -- they were always there in the map; the view drew
  only the current level's.
- **The glow,** as the 2D halos: each node's live share on each level it is
  heard on, area in proportion to the share, a grey rim for silence and a
  green core for what reaches outputs, from the 3D weights at the listening
  point. Judged on screen.
- **`m`** gives the selected nodes the height, on the current level, of the
  node selected FIRST; a box has no first node and is refused. **`M`** puts
  them back at the level's start. Both all or nothing, refused where a node
  would reach a level next to it (`Model::matchHeights`,
  `Model::flattenHeights`).
- 1 editor script (50), 34 checked steps (674); 5 mutation gates (291). The
  drawing session gained a frame auditioning between levels with the view
  open.

### 3A-6d: each level puts out its own numbers

Lee (2026-11-06): "What is the use of having them if they can't produce any
number by itself… if I wanted a flat map I wouldn't add levels." And, on what
the numbers are for: "the point of it isn't to be speakers, but to be capable
of putting out information as an individual -- that is what makes it a 3-D
map. That it could be used for speakers, or for modelling 3-D space around two
speakers or several, is determined by use and what you hook it up to."

So an output is a channel of information, and what it drives belongs to
whatever it is connected to (the boundary of decision B, kept).

- **A new level gets outputs of its own.** With auto-output on, `n` and `N`
  give each node of the new level an output named after it and the level it
  was made as -- `N-2`, `E-2` -- on the next free channels, kept unique with
  `_2`. What the node sent to outputs goes to its own; its silence stays, so
  its fill carries over. With auto-output off the new level shares the
  outputs it was copied from, as before -- for levels that are positions
  mixed over one fixed set of channels.
- **Placing a node** with auto-output on gives it an output of its own on
  every level: `P`, `P-2`, `P-3`, each made if its name is free. This
  replaces 3A-6's one output shared by every level.
- **Deleting a node** takes its own outputs on every level (`P`, `P-2`,
  `P-2_2`), each only if nothing still feeds it.
- **Removing a level** takes the outputs only that level used.
- Names keep the number the level had when it was made; a level inserted
  below later shifts the numbers shown, not the names.
- 1 editor script (51), 14 checked steps (688); 7 mutation gates (298), and
  three earlier ones moved onto the new code. The 3A-6 scripts' expectations
  changed with the rule -- new levels now report their outputs -- and the rig
  script turns auto-output off to keep its shared-output case.
- **A gate killed mid-run leaves its break in the file.** The machine running
  the gates restarted while one had `ofxManifoldEditorModel.cpp` mutated;
  `run_gates.py` restores in a `finally`, which a killed process never
  reaches. `make workflow` caught it at once -- the gate's anchor no longer
  matched -- and the line was put back by hand. After any interrupted gate
  run, run `make workflow` (or `git diff`) before anything else.

#### 3A-7 proposal: setting and showing node values (agreed 2026-11-06; see Done)

A node value is a named number each node carries on each level -- what it
means is up to what reads it. The kernel has them already (3A-4b): declared
with a name and a range, set per node per level, read at any point through
the 3D weights, saved in the map file. The editor has no way to touch them.

**Keys, a new group "values":**

| Key | Does | Undo step |
|---|---|---|
| g | new value: a line opens in the panel to type its name, and optionally its range -- `reverb`, or `distance 0 10`. Enter makes it, Esc doesn't. Range 0 to 1 if none is given; a node's value starts at the bottom of the range. It becomes the current value | yes |
| j J | the next / previous value becomes current | no |
| ; ' | the selected nodes' current value down / up by a twentieth of its range (shift: a hundredth), landing on the next multiple, as fill does, and held inside the range | yes |
| G | remove the current value, from every node on every level | yes |

`;` and `'` sit side by side, right of `l`. No names are offered or built in:
the player, not the editor, knows `distance`, `brightness` and `reverb`.

**What you see:**

- **Beside each node, the current value** on the current level, as a number,
  when the map has values. Below-left of the node, so it clears the height
  offset above-right.
- **The value at the listening point,** every value, on a line under the
  status: `distance 4.2  reverb 0.35` -- read through the 3D weights, so it
  blends as the point glides between levels, and outside the map it is each
  value's starting number.
- Nothing new on a map without values.

**Other levels:** a new level copies its values with everything else (it
already does, in the kernel). Values are set on the current level only, like
bindings.

**Tested by** editor scripts, Python reference first: declaring (with and
without a range, a name taken twice refused, a malformed range refused),
cycling, stepping (landing, held in range, fine), removing, undo, values
carried to a new level and kept per level, a save and open round trip, and
the value at the listening point checked against the 3D weights directly, on
a level, between two, and outside. Typing a name is pure too -- what each key
does to the line -- so it is scripted. `make keys` covers the new keys.
Mutation gates for each rule.

**Questions:** the keys -- `g` `G` `j` `J` `;` `'`; and whether the typed
range (`distance 0 10`) is enough, or a value's range should be changeable
after it is made.

### Between 3A-6d and 3A-7: m and M were never wired

`m` and `M` (3A-6c) were in the model, tested by script, and listed lit in the
key panel -- and pressing them did nothing: `ofApp::keyId` had no case for
them. The editor scripts test the model, not the app's keys, and the drawing
session never pressed them. Now wired, and `make keys` (part of `make test`,
`tests/check_keys.py`) checks that every keyboard entry in the panel is
returned by `keyId()` and answered by `keyPressed()`; mouse entries are left
out. It fails on the code as shipped in 3A-6c, naming `match`. One mutation
gate (299) takes the key away again.

### 3A-7: setting and showing node values

As proposed, with Lee's answers (2026-11-06): the keys as offered, and a
value's range changeable after it is made -- so `z` reopens the typing line
on the current value, filled in as `distance 0 10`, to rename it or change
its range or both.

- **Kernel:** `Map3D::setValueRange` (every number the value holds, and its
  fallback, brought inside; refused unless low is below high),
  `renameValue` (refused for a name another value has), `removeValue`. One
  map of vectors, `forest_values`, four value readings, and a save.
- **Model:** `newValue`, `editValue` and `valueLine`, `pickValue`,
  `stepValue`, `removeValue`, and the current value, held through undo. A new
  value starts every node at the bottom of its range. Steps are a twentieth of
  the range (a hundredth fine), landing on the next multiple, held inside it;
  within a millionth of the range is the end itself, so the reference's
  double and the editor's float agree on when the top is reached.
- **The typing line** is pure (`typeKey`, in the keys file) and scripted:
  printable keys add, up to 40; backspace and delete take away; return makes
  it, escape doesn't; anything else is ignored. While it is open, every key
  goes to it.
- **Seen:** each node's number on the current level, below-left of it; a
  `values` line under the status with every value at the listening point
  (`valuesAt`, through the 3D weights), the current one marked `>`; the
  typing line in the message's place. Nothing new on a map without values.
- **The dump's separator** between a value and its node is `/`, not `.`: the
  runner reads a lone `.` as the start of a number.
- 4 editor scripts (55), 84 checked steps (772); 5 vectors in the levels
  suite (98); 15 mutation gates (314). The drawing session gained three
  frames: typing a name, a value raised, `z`'s line. `make keys` covers the
  five new keys (46).
- In the drawing session's 1024 by 640 window the key panel was already
  taller than the window before this round (it is one column there); the
  values group adds five rows to it. On a wider window the panel is two
  columns.

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
| **3A-4b** | What the 3D map drives: bindings per level, node values per level, read through the 3D weights | one level equals its own Mapping, levels agree about the room, gates |
| **3A-4c** | Files for the 3D map: map file `values` and `levels`, bindings by level | round trips, existing files byte-identical, refusals, gates |
| **3A-5** | Editor: `d` / `D`, use and save rig, chart drawing levels it's handed | editor scripts, door check, drawlog identical |
| **3A-6** | Editor: levels (new, switch, remove), raise and lower, the 3D view | editor scripts, judged on screen |
| **3A-7** | Editor: setting and showing node values | editor scripts, judged on screen |
| **3A-8** | Editor: ring and grid layout | editor scripts, drawlog session extended |

A slice that adds a `.cpp` file says so, and a Project Generator Update comes
before the build.
