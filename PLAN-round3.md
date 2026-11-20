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
on the one below. (Since 3B-2d, at least as high: levels may touch.) A raise that would reach the next level is **refused**,
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
  cell turns inside out. (Since 3B-2d levels may touch, so a column may pinch to
  zero thickness -- a closed shape's rim -- and the point there is shared.)
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

### J. 3B in small players -- agreed 2026-11-08

Lee: not one player for every possibility, but several small ones, each
useful and testable, until they culminate in a master player -- "this isn't
just a product for me but anyone that's been interested in utilizing this
interface". The pieces they prove collect in one package, `ofxManifoldPlayer`
(no openFrameworks, tested like the editor); the apps stay thin.

The order: **OSC out** (3B-1, the smallest piece, for any receiver); a
**virtual room** on headphones (speakers with positions, rendered binaurally
in SuperCollider, three levels -- floor, middle, top -- with the panners
SuperCollider provides to compare against the map); **movement** (paths,
orbits, wandering, flocking, groups); the **master player**.

(2026-11-13: movement first, at Lee's asking; the room is 3B-3.)

#### 3B-1: OSC out that grows and shrinks with the map

Messages for whatever is listening -- SuperCollider, Max, Pd, TouchDesigner,
a script. The receiver need not know how many outputs there are: the app
says, and says again when that changes. Agreed with Lee (2026-11-08), every
recommendation, and a faster refresh than first proposed ("one second sounds
pretty slow").

    /manifold/outputs   N  name0 ch0  name1 ch1 ...   describe: on the first
                                                      send, on a change, on
                                                      request, every 2 s
    /manifold/levels    f0 f1 ... f(N-1)              packed, in the order
                                                      described (channel order)
    /manifold/out/NAME  f                             per output
    /manifold/values    M  name0 v0  name1 v1 ...     packed
    /manifold/value/NAME f                            per value
    /manifold/position  x y z
    /manifold/describe                                in: send the outputs again
    /manifold/hello     [name]                        in: a receiver is there

- **Packed and per output**, each switchable; packed on by default.
- **Packed sends everything at every send.** Per output sends only what
  changed (by more than 1e-6) at every send, and everything every 0.1 s, so
  a receiver that joins late or drops a packet catches up within a tenth of
  a second. Values follow the same two shapes.
- **Sends at a rate, 60 a second by default**, settable; never two in one
  rate period, and a late frame does not cause a burst.
- **The prefix is settable** (`/manifold`), so two apps can run side by
  side. A name in an address has any of ` #*,/?[]{}` replaced by `_`.
- **The handshake is optional.** A `hello` marks the receiver connected for
  three seconds; nothing is held back without one.
- **OSC 1.0 messages**, one per UDP packet, arguments `i` `f` `s` only --
  what SuperCollider, Max and Pd all read without a bundle parser. The
  encoding is the package's own, checked against python-osc, so the code
  the tests check is the code that sends.
- **`example-osc`**: one map, a point to drag (with height), what is being
  sent and whether anyone answered. With it a SuperCollider patch that
  builds as many channels as `/manifold/outputs` says and pans a test sound
  by the levels over stereo, smoothing with a 20 ms lag; a Python receiver
  with no dependencies; and a protocol page for anyone building their own.


#### 3B-2: movement, first slice -- random jumps and glides (spec, 2026-11-13; clocks added the same night)

The order changed: Lee wants movement before the virtual room ("I would like
to work on movement"). The room waits for 3B-3.

What he asked for: random movement that **jumps** -- "each click might move
it only once, but no space in between" -- or **moves to a destination over a
set amount of time**; bounded; an abstraction that is earned, not
speculative; semantics that know nothing of what is moved; and room for
what comes later -- "spirals, circular movement where it goes on a
horizontal and orbits that can be moved laterally".

And not to rule out his **ofxScPhaseBridge** (github.com/leeMeredith/
ofxScPhaseBridge) as the clock: SuperCollider owns time and sends `/phase`,
0 to 1 across a bar, a hundred times a second; it echoes `/beat <step>` when
an armed step sounds. "This doesn't have to be tied but it can be what's
used in place of time." So a movement is not in seconds: it is in whatever
the clock counts.

##### The one shared idea: where the point is at a given time

    class Motion {                              // src/sources/ofxManifoldMotion.h
    public:
        virtual ~Motion() = default;
        virtual glm::vec3 at(double t) const = 0;   // x, y on the map; z height
        virtual double duration() const;            // infinity unless it ends
    };

That is the whole abstraction for this slice, and it is earned: the
recording, the jump and the glide all fit it today. Nothing else is shared
until a second kind needs it.

- **Asked, not stepped.** A motion is asked a count; it does not advance a
  frame. So it is the same at any frame rate, can be paused, scrubbed or run
  backwards, and is tested as the trajectory is: positions at given times.
- **It knows nothing of maps or sound.** It gives positions in the map's own
  coordinates (x, y in 0..1, z in the map's height units). The player puts
  the point there; evaluation, outputs, smoothing and OSC go on as now. The
  same motion can move a group member later, or play on another map.
- **A count in the clock's units, not seconds.** `t` is whatever the clock
  counts: seconds by default, bars when SuperCollider's phase drives it (see
  Clocks), a fader if someone wires one. Every setting is in the same unit --
  the step interval, the travel, the speed -- so a walk set to a step every
  1/16 and a quarter of travel steps on SuperCollider's sixteenths and
  glides a quarter-bar, and stretches with the tempo. That is what
  `sources/ofxManifoldPointSource.h` asked for from the start: the general
  form of a source is deliberately not time.
- **Not PointSource.** PointSource is a finite extent asked at t in 0..1; a
  random walk has no end. A finite motion is still a PointSource by
  `at(t * duration())`, and a recording is both.
- **double, not float**, for the count: a float second loses milliseconds
  after a few hours of running.
- **Header-only, no openFrameworks**, beside the trajectory.

##### The recording as a motion

`RecordedMotion(const Trajectory&, End)`: `at(t)` is the trajectory at `t`
of its own duration, with its height (`pointAtSeconds`, `heightAtSeconds`).
Before 0, its start. After its end: **Hold** (the last point, the default) or
**Loop**. Unknown duration counts as one unit, as `pointAtSeconds` already
does. A recording is in the units of the clock it was recorded against:
example-osc records against whichever clock is running, so a gesture
recorded in bars plays back in bars, in time with the music at any tempo.

##### Jump and glide are one thing: a random walk

A jump is a glide that takes no time to arrive. So there is one class,
`RandomWalk`, and a jump is `travel = 0`.

    step k happens at time t_k  (the clock, below)
    it starts from WHERE THE POINT IS at t_k  -- mid-travel if a step comes
                                                  before the last arrived
    it picks a destination d_k  (the bounds, below)
    it travels from there to d_k over the travel time, eased, then holds
    before step 1: the start position

- **Travel**: the count per trip (0 = a jump), or a **speed** instead -- map
  units per count, so a long trip takes longer.
- **Easing**: `linear`, or `smooth` (smoothstep, 3u^2 - 2u^3: easing in and
  out).

##### When steps happen

- **Every**: an interval, with optional **jitter** -- each interval drawn
  from interval x (1 +/- jitter), jitter 0 to 0.9.
- **At**: a list of counts -- **clicks**. Each click is stored as the count
  it came at, so a walk driven by clicks is still "where at a given count",
  replays the same, and saves. Clicks are only ever appended, at or after
  the last.
- **Pattern**: offsets within a cycle, repeating -- `{"cycle": 1, "at":
  [0, 0.1875, 0.5]}` is steps 0, 3 and 8 of sixteen, every bar when the
  count is bars. Exact: no network timing in it, the steps fall where the
  count says. Offsets in 0 up to the cycle, increasing.

##### Clocks: what drives the count

Outside the motion, in `ofxManifoldPlayer` (no openFrameworks), tested like
the rest. A clock is asked for the count now; the player asks the motion
where that count puts the point.

- **SecondsClock**: the app's own seconds, from when it started; pausable
  (the count holds while paused, and goes on from there).
- **PhaseClock**: a count built from a phase that wraps -- 0 to 1 once a
  bar, as ofxScPhaseBridge's `/phase` sends it. It is fed each phase as it
  arrives and answers bars: bar 3 at phase 0.25 is 3.25.
  - **A wrap**: a drop of more than half (0.98, then 0.02) is a new bar. The
    bridge keeps a bar to at least 0.1 s and sends a hundred phases a second:
    at least ten readings a bar, plenty to tell.
  - **Never backwards**: a smaller drop -- a packet out of order -- is
    ignored; the count holds until the phase passes it again.
  - **A restart** (bridge.scd run again, the phase back to 0) reads as a new
    bar: the count goes on rising and a walk carries on.
  - **The bar number, if it is sent**: a second argument, `/phase <f> <i
    bar>`, is taken as the bar itself, so two apps on one bridge agree on
    the bar, not only the beat. The bridge does not send it today; without
    it each app counts bars from when it began listening.
  - **Not heard**: no phase for a second and the clock says so; the count
    holds.
  - It takes phases from anything -- the bridge, Max, a script -- and knows
    nothing of OSC: the app reads the message and hands it the number.
- **Not tied.** ofxManifold does not use ofxScPhaseBridge's code and the
  bridge needs nothing from ofxManifold; example-osc speaks the same
  contract: `/phase` on port 9001, so `bridge.scd` drives it unchanged.
  (Only one program can listen on 9001: the bridge's own example app, or
  example-osc.)
- **Delay**: example-osc sees each phase some milliseconds after SuperCollider
  sends it and answers with levels, so movement on the bridge's clock runs a
  frame or two -- some 20 to 30 ms -- behind SuperCollider's own clicks.

##### Bounds: where it may go

- **A rectangle** (default the whole map, 0..1 by 0..1) and a **height range**
  (`z` low to high; low = high keeps one height).
- **An inside test**, given by the player: `std::function<bool(glm::vec2)>`.
  example-osc gives the map's own -- is the point on the map? -- so no
  destination is ever off it. The motion still knows nothing of maps: it is
  handed a yes/no.
- **Step distance**: how far one step may go from where it starts. 0 means
  anywhere in the bounds; 0.1 makes it wander nearby.
- **Straight paths stay inside** (on by default when there is an inside
  test): a glide between two points on an L-shaped map can cut across the
  notch, off the map, and fall silent half way. A destination is refused if
  any of 16 points along the way is outside. Off: any destination inside is
  taken.
- **Choosing**: up to 64 tries; each try draws a candidate (anywhere in the
  rectangle, or within the step distance: a square of that half-width, a
  draw outside the circle counting as a failed try -- no sine or cosine, so
  C++ and Python agree to the last bit), then a height, and keeps the first
  that passes every test. **If none passes, it stays where it is** for that
  step. Every try uses the same number of draws, pass or fail, so the
  sequence never depends on which tests ran.

##### Random that repeats

- **A seed.** The same seed, settings and clock give the same path every
  time: a walk you liked can be saved, and the tests can check one.
- **Our own generator**: splitmix64, a few lines. The standard library's
  distributions differ between compilers, and the Python reference must
  draw exactly the same numbers. A draw in 0..1 is the top 24 bits / 2^24,
  exact in a float. The generator is checked against splitmix64's published
  outputs, which carry outside authority.

##### Memory that stays small

A walk's step k depends on step k - 1 (it starts from there), so asking for
an hour in means walking the hour. To keep that bounded and fast:

- **It keeps where it last got to**, so playing forward costs one step at a
  time.
- **A checkpoint every 256 steps** -- step, time, position, the generator's
  state: about 48 bytes. An hour at ten steps a second is 141 checkpoints,
  7 KB.
  Asking for an earlier time starts from the checkpoint before it.
- **One thread.** The cache is mutable inside a const `at()`; a walk is not
  asked from two threads at once.

##### Saving

A motion file, `"motion": 1`, JSON like the others:

    {"motion": 1, "kind": "walk", "seed": 7, "units": "seconds",
     "start": [0.5, 0.5, 0.0],
     "clock": {"every": 0.5, "jitter": 0.0}      or {"at": [0.8, 1.9, 2.4]}
                                                 or {"cycle": 1, "at": [0, 0.1875, 0.5]},
     "travel": {"duration": 0.4}                 or {"speed": 0.25},
     "easing": "smooth",
     "bounds": {"rect": [0, 0, 1, 1], "z": [0, 0.5], "step": 0.0,
                "inside": "map", "straight": true}}

`"inside": "map"` says the player supplies the map's test; `"none"`, the
rectangle alone. `"units"` -- `"seconds"` or `"bars"` -- says what the
numbers count, so a walk written for bars is not played in seconds without
anyone knowing: a player opening one for the other clock says so. A recording keeps its path file and is referred to by name:
`{"motion": 1, "kind": "recorded", "path": "spin.json", "end": "loop"}`.
Refused: unknown kind, version above 1, unknown units, travel or interval
below 0, jitter outside 0..0.9, a rectangle inside out, z low above high,
clicks going backwards, a pattern's offsets out of order or outside its
cycle.

##### Left room for, not built

- **Circle**: centre, radius, a lap's count (one bar, on the bridge's
  clock: the phase is its angle), starting angle; **spiral**: a
  circle whose radius changes with time; **orbit moved sideways**: an orbit
  whose centre is itself a Motion. The one thing to get right now is that
  nothing in RandomWalk assumes a position is a constant: the start, the
  rectangle's place, are plain data that a later slice can make a Motion.
- **Edges for shapes that can leave the map**: stop at the edge, bounce,
  wrap, or allow (silent outside). A walk never needs one: its destinations
  are inside by construction.
- **Flocking** does not fit "where at a given time": objects that steer by
  each other have to be simulated forward together. It gets its own stepped
  form later; a flock's leader can be any Motion.

##### In example-osc

- `m`: the movement -- your hand (as now), the recording, jump, glide.
- `r`: record your hand's movement (again: stop); the recording loops.
- `,` `.`: the step interval shorter / longer; `<` `>`: the travel time.
- `{` `}`: the step distance -- anywhere, 0.5, 0.25, 0.1, 0.05.
- `c`: when steps happen -- every interval; your clicks: a click on the map
  (or return) is one step, and does not move the point itself; or the
  bridge's beats: each `/beat` SuperCollider echoes -- each armed step that
  sounds -- is one step, so the point moves once on every armed step, as
  with a click. (Its count is the clock's at arrival, a few milliseconds
  late; the Pattern kind is the exact one, for files and the library.)
- `b`: the clock -- the app's own seconds, or SuperCollider's phase on port
  9001 (the panel shows the bar and phase, and "phase not heard" when it
  stops). Recording, steps and travel all follow it.
- `h`: height -- stays where it is, or random across the map's levels.
- `n`: a new seed.
- The panel: the movement, its seed, interval, travel, step distance, when
  steps happen, the clock and its count.
- The bounds are the map's: its inside test, the whole rectangle, and the
  height range of its levels.
- Smoothing still applies to what is sent: a jump sounds like a quick glide
  unless smoothing is off (`s`).

##### Tested by

`tests/ref/reference_motion.py` writes `tests/vectors/motion.vec`;
`make test-motion` runs it; mutation gates on every rule above.

- ANALYTIC: splitmix64's published outputs; a jump holds between steps and
  arrives at once; linear travel is half way at half time; smooth travel is
  0.15625 of the way at a quarter; before the first step, the start; one
  height when low = high; no step longer than the step distance; every
  destination inside the rectangle and the inside test; no straight path
  through an L-shaped map's notch; steps exactly at click times; a click
  mid-travel starting from where the point is; a walk with nowhere to go
  staying put; a recording held and looped.
- CROSS: positions along seeded walks, every kind of setting, against the
  Python reference (1e-6).
- SPEC: jitter, 64 tries, the same draws for a pass and a fail; asking times
  out of order -- backwards, jumping ahead, across checkpoints -- gives the
  same answers as asking in order.
- Pattern steps: exactly at cycle x n + offset, for many cycles.
- PhaseClock (ANALYTIC and SPEC): a wrap at more than half a drop and not
  at less; a packet out of order held, never backwards; a restart a new
  bar; the bar argument taken when there; "not heard" after a second; a
  bar at the bridge's shortest, 0.1 s, at a hundred phases a second, never
  miscounted. SecondsClock: paused and resumed, the count holding.
- example-osc's `/phase` and `/beat` reading: `make osc-live` extended -- a
  stand-in bridge sends phases and beats over UDP, and the walk steps on
  them.
- Files: round trips, and every refusal above.
- example-osc through the drawlog session: each movement for some frames.

Open questions for Lee, each with a recommendation:

1. **Height by default**: stays where it is (`h` for random across levels).
2. **Straight paths stay inside**: on by default.
3. **Recording end**: loop in example-osc; hold by default in the library.
4. **Travel by default**: 0.4, smooth easing; interval 0.5; step distance
   anywhere. On the bridge's clock these are bars: a step every half bar.
5. **The bridge's beats as steps** (`c`): each `/beat` echo moves the point
   once, rather than example-osc keeping its own copy of which steps are
   armed.
6. **A suggestion for ofxScPhaseBridge**, not needed here: an optional bar
   number, `/phase <f> <i bar>`, so several apps agree on the bar. Receivers
   reading only the first argument are unaffected.


#### 3B-2b: the map's shape for SuperCollider (spec, 2026-11-14)

Lee: "is there a method in which we could expand and contract the tones
based on the map and how many levels it's given ... is there a musical
behavior that could deal with that?" -- and, on the options: "trying to not
make openFrameworks decide, and leave that decision as much as possible in
the SuperCollider work ... feed it exactly whatever it needs."

So the split is: **the player says what the map is; SuperCollider decides
what it sounds like.** Nothing musical is chosen in C++.

##### What SuperCollider is missing

The outputs message gives each output's name and channel, nothing more. The
patch climbs a scale in channel order; on the room that happens to put the
floor first, but it cannot tell three levels of four from twelve outputs on
one, and a map numbered otherwise would scramble it.

##### The shape message

Sent with every description, after the outputs message -- the first send,
any change, when asked, every two seconds -- and whenever the shape alone
changes (a level's height, an owner node moved, a binding):

    /manifold/room/shape
        i L, then L x f        the levels' heights, lowest first
        i N, then N x (s name, i channel, i level, i levels, i placed, f x, f y, f z)

For each output, in the outputs message's order:

- **level**: the lowest level whose nodes feed it; -1 when no level does.
- **levels**: how many levels feed it -- 1 for a level's own output, more for
  one shared across levels, 0 for none.
- **placed**, **x y z**: whether it has an owner node -- the node of its name,
  or the node it is named after (FL for FL-2), as the editor's `ownerOf` --
  and if so where: the node's place on the floor plan and its height on that
  level. Unplaced: 0, and x y z are 0.

All of it is already in the map: no new idea, only what the editor knows,
said out loud. A receiver that does not know the message ignores it.

The roll call's protocol becomes **3**: "has a shape message". OSC.md gets
the table and the outline step.

##### Where it is worked out

- **The editor's model**: `Model::outputPlaces()` -- per output, level,
  levels, placed, position -- from the map's bindings per level and
  `ownerOf`. Tested in the editor's vectors (the room; the flat example; an
  output shared by two levels; one fed by none; a node moved).
- **The player** stays map-agnostic: `OscOutput` gains `level`, `levels`,
  `placed`, `position`; `OscFrame` gains the levels' heights. `OscOut` sends
  the shape after the outputs, and treats any change in it as a change to
  describe. Tested as the rest of the OSC: an encoding vector against
  python-osc, send scripts (sent with the description, again on a change of
  height, of owner position, of feeding level; not when only levels move).
- **example-osc** fills them from the model each frame.

##### What SuperCollider does with it -- decided there, switchable there

The patch, at its top: `~manifold[\music]` picks the behaviour, and every
number it uses is a setting beside it.

- **\registers** (the default): each level a chord in its own register, each
  of its outputs a chord note -- 3 a triad, 4 a seventh, 5 and on adding 9,
  11, 13. The levels share a fixed range (`~manifold[\span]`, 30 semitones
  from C3) -- "fit": more levels sit closer -- or each takes an octave
  (`~manifold[\fit] = false`: "spread").
- **\progression**: each level a chord in one key -- I, IV, V, vi, ii, iii --
  so rising through the levels moves through the progression.
- **\series**: each level the next overtone over one root, its outputs the
  notes within it; wide at the bottom, close at the top, as the series is.
- **Brightness**, with any of them (`~manifold[\bright]`, on): the point's
  height from `/position` opens each voice's filter.
- **Panning by place**: a placed output pans by its node's x on the map; an
  unplaced one, by its order, as now.
- **Outputs on no level** (level -1) or **shared** (levels > 1): their own
  voices, a quiet tone at the root -- they are not any level's chord.
- **No shape message** (an older sender): today's climb in channel order.
- `\tone` and `\noise` stay, as the timbre under any behaviour.

##### Tested by

- Editor vectors for `outputPlaces()`; OSC vectors and send scripts for the
  shape; the live UDP run carrying it; gates on each.
- The patch's logic in sclang, without a server, as before: the notes each
  behaviour gives the room, a five-level map, a map with a shared output and
  one with no shape message, printed and checked.
- The sound itself is Lee's to hear.

##### Also in this slice: the map's names, one level at a time

Lee: "the map has overlapping names, so the node has all of the
three-dimensional layers' names over the top of each other. Whatever level
you are on, the left two-dimensional map should display those only."

What is there, from example-osc's drawing record (`make drawlog`): every
node's name is drawn TWICE, 2 pixels apart -- once by the renderer's own
nodes (`ofxManifoldRenderer`, its bitmap font, pale) and once by the
editor's node glyph, in its output's colour. The editor app turns the
renderer's nodes off (`renderer->style.drawNodes = false`, "nodes are drawn
here, not by the renderer"); example-osc never did. The plan view draws the
floor plan once, not once per level -- but two names a hair apart read as
layers stacked. And the glyph's colour does come from the wrong level: a
name takes the colour of the output named after it, FL on every level,
while on level 2 FL sounds through FL-2 (its fader another colour).

- **example-osc**: the renderer's nodes off, as in the editor -- one name
  per node, one dot.
- **A name in its own output's colour on the level you are on**, in both
  apps: `Model::ownOutput(node)` -- of the outputs the node is bound to on
  the current level, the one `ownerOf()` says is its own (FL-2 for FL on
  level 2), else none. The glyph's name and link lines then match the
  fader you hear on that level.
- **Tested by**: editor vectors for `ownOutput` on each level of the room
  and the octahedron, and for a node with no own output; the drawlog shows
  each name once; gates on each.

Open questions, each with a recommendation:

1. **The span for \registers**: 30 semitones from C3 (C3 to F#5).
2. **A shared output's voice**: a quiet tone at the root.
3. **Brightness on by default**: yes.
4. **The name on the map**: the node's name (FL) on every level, in its
   own output's colour there -- not the output's name (FL-2). The level is
   in the colour, the panel ("on level 2 of 3") and the dots; the faders
   carry FL-2.


#### 3B-2c: height on the levels -- riding the bends (spec, 2026-11-14)

Lee: "I'm able to take a node or several nodes and move it down or up with
y or h -- is that shape maintained in a way that movement inside of it can
be restrained and extrapolated? Right now the levels are flat, versus me
moving one node or several."

##### Where things stand

- **The bend is kept and heard.** y and h give a node its own height on a
  level; the map file keeps it; a level is a bent surface, its height at any
  point blended from its nodes' heights as the sound is. `Levels::evaluate`
  places the point between two levels against those surfaces at its x and y,
  and the 3D view draws them bent.
- **Outside is silent.** Below the bottom surface or above the top one, at
  that x and y, the point is outside the map, as off a flat map's edge.
- **Movement does not know the bend.** example-osc's arrows move a plain
  height; the random height draws between the flat bases of the lowest and
  highest levels, so where the floor is raised or the top lowered it can
  land outside -- and the sound cuts out; "level 2 of 3" comes from the
  flat bases.

##### Height measured in levels

A **level position** u: 0 on the floor's surface, 1 on the next level's, 1.5
half way between the second and third -- at the point's own x and y. The
height is worked out from u each frame, from the bent surfaces there. So:

- **It rides the shape**: at a steady u, moving sideways follows the bends.
- **It stays inside** (restrained, the default): u is held to 0..top, so the
  point never leaves the volume. The random height draws u; the arrows move
  u, at a steady rate in levels, whatever the gaps.
- **Past the levels** (extrapolated, an option): beyond the bottom or top,
  the outermost surfaces' spacing carries on -- below the floor by the gap
  from the floor to the next level, above the top by the gap below it -- so
  the bend is followed out there too. Still outside the map: silent, as now.
- **In plain units** (as now), for anyone who wants it.

##### In the kernel: two small additions to `Levels`

    float heightAtPosition(const std::vector<WeightedNode>& floor, float u,
                           bool extrapolate = false) const;
    float positionAt(const std::vector<WeightedNode>& floor, float z) const;

- `heightAtPosition`: k = floor(u) held to 0..L-2, f = u - k; the height is
  h_k + f (h_{k+1} - h_k), each h the level's bent height at the point.
  Restrained: u is first held to 0..L-1. Extrapolated: not, so the same
  line carries on past the ends.
- `positionAt`: its inverse -- the levels never cross (`fits` refuses a
  height that would), so the surfaces are in order and it is exact both ways,
  extrapolated too.
- One level: u is 0 and the height that level's; outside the floor plan (no
  weights), the flat bases.
- Tested in the levels suite: flat bases, under a raised node, under a
  lowered one, the round trip u -> z -> u, extrapolated below and above,
  held when restrained, one level, off the floor plan; gates on each rule.

##### In example-osc

- **`k`**: the height on the levels (restrained, the default), past the
  levels (extrapolated), in plain units. Switching keeps the point where it
  is: u is worked out from the height, or the height from u.
- **The arrows** on the levels: one level a second, shift a quarter.
- **The random height** (`h` jumps or glides) draws u across the levels: a
  RandomWalk whose height range is 0..L-1, in levels.
- **Recordings** made on the levels keep u, so a gesture replays riding the
  shape even after the levels are reshaped in the editor.
- **The panel**: "level 1.50 -- between levels 2 and 3 (k)"; the level dot
  and the 3D view's bright level follow the surfaces under the point.
- **A map opened**: u from the point's height on the new map.

##### For SuperCollider

`/position` gains an optional fourth number, u: f x, f y, f z, f u -- worked
out from the height when in plain units. Receivers reading three numbers
are unaffected. With 3B-2b's shape message SuperCollider has both: which
level each output is on, and where between the levels the point is.

Tested by: the levels vectors above; the OSC encoding and a send script with
four numbers; the drawlog session -- riding over a raised node at a steady
u, the random height under a bend never outside, `k` switching without a
jump.

Open questions, each with a recommendation:

1. **Default**: on the levels, restrained.
2. **Arrow speed on the levels**: one level a second, shift a quarter.
3. **Outside the floor plan**: the flat bases.
4. **Order**: 3B-2c before 3B-2b's build, since the shape message and the
   fourth number go out together, and riding the bends fixes the random
   height dropping out under a bend.


#### 3B-2d: levels that touch -- closed shapes (spec, 2026-11-14)

Lee: "does this solve maps that would otherwise be called deltahedrons, and
does it deal with concave parts as long as they're within the volume?"

What the stacked levels can be: one floor plan, every level a surface with
one height over each point of it, so a vertical line crosses each level once.
Concave floor plans and dents in a surface are inside that; overhangs, caves
and tunnels are not. A closed shape whose top and bottom faces sit over the
same floor-plan triangles -- a bipyramid, the octahedron, a tetrahedron on a
face -- is a bottom level and a top level, apexes moved down and up, **except
at the rim, where the two meet**: levels are kept strictly apart today (`fits`
refuses a height that would reach the next level, and `evaluate` divides by
the gap), so the rim would need a sliver. This slice lets levels touch.

##### Touching, never crossing

- **`Levels::fits`**: at or above the level below, at or below the level
  above. Equal is a touch; passing is still refused.
- **`Levels::evaluate(floor, z)` at a pinch** -- where the gap at the point is
  0, so the levels are one height there: t = 0.5, the point shared equally,
  as it is on both. Elsewhere unchanged; a gap shrinking toward a pinch is
  still divided, never by 0.
- **`Levels::evaluateAtPosition(floor, u)`** (new, with 3B-2c's level
  position): below = floor(u) held to 0..L-2, t = u - below. No division: a
  point riding the levels by position is never ambiguous, pinch or not. The
  player rides by u, so it evaluates by u.
- **`positionAt(floor, z)`** at a pinch, where many u give one z: the lowest.
- **The editor**: y and h may bring a node to the level next to it -- the
  refusal, naming the nodes, is now for passing it. Moving a whole level
  (dragging its dot) likewise. New levels still start half way to the next,
  never touching.
- **Files**: a map with touching levels is written at the next map3 version,
  so an older ofxManifold refuses it by its version, plainly, rather than by a
  height it took for crossing; a map without touches is written as now.

##### An example: the octahedron

`Model::loadOctahedron()`: four rim nodes on a square and a centre node; two
levels; the centre at -0.5 on the bottom and +0.5 on the top, the rim at 0 on
both -- touching. Its own outputs per level (rim and centre, bottom and top),
as the room has. In example-osc, `x`; on the `?` page.

##### Tested by

- Levels vectors: touching allowed, crossing refused; t = 0.5 at a pinch;
  `evaluateAtPosition` against `evaluate` away from pinches; the octahedron's
  inside -- points against the solid's own geometry (inside it exactly when
  between its faces), its rim shared equally; `positionAt` the lowest at a
  pinch. Gates on each.
- Editor vectors: raised to touch, accepted; past, refused; the octahedron
  example; files with touches at the new version, round trips, refused by the
  old version's reader.
- The drawlog session: the octahedron, riding u across the rim.

Open questions, each with a recommendation:

1. **At a pinch, t = 0.5.**
2. **The file version only when levels touch.**
3. **The octahedron as `x` in example-osc**, and in the editor beside cmd-e
   as shift-cmd-e.

---

#### 3B-2f: points moved from outside (spec, 2026-11-16)

Lee (2026-11-16): "having a way to send open sound control to that point
from outside of the app ... keeping the boundaries between what controls the
points inside ... being able to address them equally as they are built ... a
coherent addressing system for several controls within the player app, and
without hurting the communication with SuperCollider." And, the same day,
with his SuperCollider Audio Engine (github.com/leeMeredith/
SuperColliderAudioEngine, "SAE") and its panel (sae-control-panel): make
sure this adapts easily when brought into the engine.

**What SAE does with OSC, read 2026-11-16** (SAE 0.46.0, OSC v4). External
OSC becomes bounded data at the boundary (its slice 009): one adapter per
fixed path or set of paths, fixed when it is built -- no name the sender
chooses ever picks a target; exact argument counts; finite numbers only;
loopback senders only; every rejection counted and kept for inspection.
Commands carry a request id and are answered: `/sae/v1/reply id accepted
command reason`, then state. Versions live in the path (`/sae/v1/...` to
`/sae/v4/...`); capabilities are listed by name. Ports: commands in on
sclang's 57120, replies out to the panel on 9002; ofxScPhaseBridge's
`/phase` and `/beat` on 9001, which the panel listens to. The engine owns
the server, managed exclusive stereo output (its slice 014), and checks for
leaked nodes.

**Where ofxManifold meets it.** No clash in addresses (`/sae/...` and
`/manifold/...` share 57120 by path) or in ports, but one: **9001**, which
example-osc and the panel both listen on, unshared -- whichever starts second
has no bridge (example-osc says so on its panel, "port 9001 taken"). And
`manifold-receive.scd` is a standalone patch: it boots the server, writes to
output 0, and cmd-period frees everything -- the engine's, too. Its musical
rules are already plain functions with no server (3B-2b), which is what
moves into SAE later; the voices would become an SAE source on an
engine-owned bus. Not this slice: recorded in ROADMAP's Later.

##### This slice: one point, moved from outside, to SAE's rules

- **Addresses, the same for every point.** Points are numbered from 1 under
  the map's prefix. Point 1 is example-osc's point today; later points, and
  flocks as declared groups of points, are 2, 3 ... up to a fixed cap
  (`kMaxPoints`, 16) -- never grown by a sender. This slice has one.
- **In on the map's answer port** -- where `hello` and `describe` already
  come in, and which the roll call already says -- so nothing new to set up,
  and nothing going to SuperCollider changes.
- **Commands, answered (SAE's shape):**

  | Address | Arguments | Does |
  |---|---|---|
  | `<prefix>/point/<n>/take` | `i` request id, `i` reply port (0: the sender's own) | the point is driven from outside, by this sender |
  | `<prefix>/point/<n>/release` | `i` request id | back to what drove it before |
  | reply: `<prefix>/point/<n>/reply` | `i` request id, `i` accepted, `s` command, `s` reason | to the sender's IP, at the reply port |

  A second take by another sender while held is refused ("held by
  127.0.0.1:57120"); by the holder, accepted again (a retry). Request ids
  are positive int32; a repeat of the last id gets the same answer again,
  not a second action.
- **Moves, not answered** (a 60-a-second stream, like SAE's meter
  traffic): `<prefix>/point/<n>/xy f x f y`, `/z f`, `/u f`, `/xyz f f f`
  -- the map's own units, as `/position` sends them. Taken only from the
  holder; from anyone else, counted and dropped.
- **The same limits as inside.** z and u are held to the levels as the hand
  holds them; an xy off the floor plan is refused and counted, the point
  staying where it was -- nothing from outside can put a point where its own
  movement could not.
- **Boundaries.** One driver per point: the hand, its own movement (a walk
  or a recording), or outside. Taking it from outside stops its movement;
  releasing gives it back. Silent for 5 seconds, a held point goes back by
  itself. The hand always wins: grabbing the point with the mouse takes it
  back, and the holder is told -- `<prefix>/point/<n>/lost s reason`.
- **Checked at the edge, as SAE checks.** Exact argument counts, int32 ids,
  finite floats, a point number in 1..count, loopback senders only. Every
  refusal counted by reason, the last one kept.
- **Seen.** example-osc's panel: "point 1: from outside, 127.0.0.1:57120",
  its accepted and refused counts, and the last refusal's reason.
- **The roll call** gains a fifth argument, `i points` -- 1 now -- and the
  protocol is 4. Every receiver here reads only the first two, so none
  changes.
- **The bridge port** becomes a setting at the top of example-osc's
  `ofApp.cpp` (`kBridgePort`, 9001), and the panel says which it is and
  whether it is taken -- so it can sit beside the SAE panel on another port.
  Sending the bridge to two ports at once is ofxScPhaseBridge's to add.

**Where it lives**: `ofxManifoldPlayer/src/ofxManifoldPointControl.h`,
header-only and with no networking -- `PointControl`: `receive(now, message,
from)` returns what to apply and what to answer; it keeps the holders, the
timeouts, the request ids and the counts. example-osc owns the socket and
applies the moves through the same bounds as its walks.

**Tested by**: a Python reference and vectors (a new suite, `control`):
takes, releases, retries, a second sender refused, moves from the holder
and from others, timeouts, the hand taking back, every malformed message
refused with its reason; mutation gates on each rule; `make osc-live` sends
the real messages over UDP to the real receiver; the drawlog session takes
the point from outside, moves it, and lets it go.

Answered 2026-11-17, "yes, recommended": take before moving; replies to a
port named in the take (0: the sender's own); an xy off the floor plan
refused and counted; 5 seconds' silence; the hand always wins, the holder
told; loopback only.

##### The control file -- a primer, never a requirement (asked 2026-11-17)

Lee: "a happy medium -- a strict number of inlets for OSC, but a JSON file
as a primer?" and then: "someone wanting to make a map, then open the player
... would have to understand how to select the right amount." So, agreed
the same day:

- **No file needed.** Without one, a map has point 1 taking every inlet --
  `xy`, `z`, `u`, `xyz` -- and `take` and `release`, which are always open.
- **The player writes it** when it saves: `<map>-control.json` beside the
  map pair, as the editor writes the outputs file. Opening the map again
  reads it. Most people never open it.
- **Hand-editing narrows, it never widens**: a point's `inlets` (a subset),
  its `name` (so `/point/lead/xy` reaches it as `/point/1/xy` does -- fixed
  by the file, never by a sender), its `range` per axis (what a controller
  sends, mapped onto the map's own extent: x and y onto 0..1, z onto the
  lowest to highest level start, u onto 0 to the top level), and its
  `silence` (0: held until released).
- **Strict, as map and motion files are**: `"control": 1`; points numbered
  1, 2 ... in order, at most 16; known inlets only; names a letter then
  letters, digits, `-` or `_`, up to 32, unique; a range's low below its
  high, both finite; silence 0 or more. A refused file says why, and the
  player keeps the defaults -- never half of it.
- **Shown and told.** The panel and the `?` page list each point's
  addresses in full, with the port. Each description is followed by
  `<prefix>/points`: `i` N, then per point `i` number, `s` name (empty if
  none), `s` inlets (space-separated) -- so a receiver can find the inlets
  without the file.
- **This slice, one point.** The file, the control and the description are
  written for up to 16; a file declaring more than the player has points
  for is refused ("this player has 1 point"). Adding points with a key --
  and what a second point sends and sounds like -- is 3B-2g.

#### 3B-2g: more than one point, and sections with headers (spec, 2026-11-18)

Lee (2026-11-17), on what a second point should be: "carrier signals as
what the second one creates, and/or it just playing another note that is
there." And (2026-11-18): "differentiation between each one of the controls
with a header -- right now they're blending together in one block, so it's
hard to tell what key bindings do what -- so as we expand capability, things
will have a place to live and will be easily recognizable by a user."

Two parts, built together because the second point is the first new thing
that needs a place to live on the panel.

##### Part 1: sections with headers

**What blends now.** example-osc's panel is one run of about twenty lines:
where it sends, the answer port, the rate, the point, the height, the
movement, smoothing, links, the clock, with the keys in brackets at the end
of lines ("(s, - =, shift)"). Its `?` page has group titles, but in the
rows' font and colour weight with little room between, so they read as
rows. The editor's panel already flows coloured groups into columns
(3A-10, 3A-12); its titles are as faint.

Answered 2026-11-18, "yes, recommended to all":

- **One list of sections, as data.** Each section: a title, a colour, its
  state lines (what is true now) and its keys (what changes it). The panel
  and the `?` page draw the same list, in the same order -- the panel the
  state and the keys that act now, the `?` page every key. New capability
  adds a section, never lines in someone else's.
- **example-osc's sections**, in order:

  | Section | State | Keys |
  |---|---|---|
  | **sending** | OSC to host:port, the prefix, the rate, packed, per output, messages and bytes a second, paused | space p i [ ] |
  | **points** (Part 2) | how many; each point's number, name, colour and who drives it -- yours, moving, from outside | a A, left / right, click a point |
  | **the point** | the chosen point's x y, height and level, how height is measured | drag, up / down, k, shift-click, tab, click a fader |
  | **movement** | what moves the chosen point, step timing, the clock, the recording | m M r , . < > { } c return h n b |
  | **smoothing** | on or off, the half-life | s - = (shift) |
  | **from outside** | the answer port, the chosen point's addresses, accepted and refused, the last reason | -- (OSC, not keys) |
  | **showing** | the 3D view, links | 3 l ? |
  | **maps** | the map's name, its control file | o e x C E |

  The message flash stays where it is, under the sections, belonging to
  none.
- **A header that stands out**, with the bitmap font's one size: room above
  it, its title in the section's colour, and a thin rule in that colour
  across the column under it. Rows: the key in the section's colour on the
  left, in a fixed column; what it does, or its state now, on the right.
  State lines in white, keys' descriptions in grey -- so the panel reads as
  the `?` page does, not as sentences.
- **A colour per section**, fixed by the section, the same wherever the
  section appears; a section the editor and the player share by name
  (none yet) shares its colour.
- **Out of room: flow, never fold.** Sections go on in the next column, a
  section split across columns keeps its title with "(more)" -- the
  editor's flow (`flowKeys`), reused rather than written twice.
- **All three screens alike.** The editor's panel and `?` page get the same
  header (room, colour, rule); its groups and colours are unchanged. `??`
  (keys hidden) is unchanged in both apps.
- **Where it lives.** `KeyGroup` (ofxManifoldEditor's keys, no
  openFrameworks) gains a colour and state lines; the colour moves from the
  key panel's view into the data. One drawing function draws a flowed list
  of groups with headers, for both apps' panels and `?` pages. example-osc
  builds its sections each frame from its state.

##### Part 2: more than one point

**In the player.**

- **Points 1 to 16** (`kMaxPoints`). Point 1 always exists. `a` adds a
  point at the mouse if it is over the map, else in the middle, at the
  chosen point's height; the new one is chosen. `A` removes the last point
  -- only the last, so no point's number ever changes under a receiver.
- **One chosen point.** Left / right choose the previous / next; a press on
  a point's dot (within 12 pixels) chooses it, and dragging moves it; a
  press elsewhere moves the chosen point there, as today. The keys of "the
  point" and "movement" act on the chosen point.
- **Each point is a whole point**: its own x y, height and level, its own
  movement (hand, recording, jump, glide) and height-by, its own walk
  (seeded by the seed and its number) and its own recording. The clock and
  the step timing are shared: a beat steps every walking point.
- **Drawn** in its own colour, with its number beside its dot, on the map
  and in the 3D view; the chosen one ringed. The faders show the chosen
  point's levels, in its colour; the node followed is the chosen point's.
- **The control file decides how many a map opens with.** A map whose
  control file declares three points opens with three -- the map's maker
  decides, the player's user needs to know nothing. Without a file: one.
  `C` writes as many as there are. The refusal "this player has 1 point"
  goes; past 16 is still refused.
- **From outside, each point on its own.** `PointControl` already holds
  sixteen; each point is taken, moved, released and timed out on its own,
  and the hand takes back only the point it grabs.

**What it sends: facts, the same for every point.**

| Address | Arguments | When |
|---|---|---|
| `<prefix>/levels`, `/out/NAME`, `/values`, `/position` | as now | point 1, unchanged |
| `<prefix>/point/<n>/levels` | N × `f`, in the outputs' order | every send, points 2 and up, packed always |
| `<prefix>/point/<n>/position` | `f` x, `f` y, `f` z, and `f` u when sent | every send, points 2 and up |
| `<prefix>/points` | as 3B-2f | on every description, and whenever a point is added or removed |

- **Point 1 stays where it is**, so every receiver written so far hears
  point 1 unchanged and ignores the rest. A receiver for many points reads
  point 1 from the bare addresses and the others from `/point/<n>/`.
- **Smoothed per point**, each with its own smoothers, the same half-life.
- **A point removed** is sent once more with every level 0, then dropped
  from `/points` -- so whatever it fed fades rather than sticks.
- **Node values** stay point 1's in this slice; points 2 and up send levels
  and position only. Per-point values, if a patch needs them, later.
- **Out and in share a tree, not a port**: `/point/<n>/levels` and
  `/position` go out to SuperCollider; `/point/<n>/take`, `/xy` ... come in
  on the answer port. No address is both.
- **Protocol 5**: the roll call's last argument is the number of points now,
  not always 1. A receiver for 4 reads 5 unchanged.

**What the patch does with it: SuperCollider decides.** A setting at the
top of `manifold-receive.scd`, as the musical behaviours are:

- `~manifold[\points] = \together` -- **every point plays.** Shared voices,
  one per output: an output's gain is the sum of the points' levels there,
  held to 1 -- one instrument, two hands, louder where they overlap. Its
  brightness follows the point loudest there.
- `~manifold[\points] = \carrier` -- **the first points play, the rest
  shape them.** `~manifold[\players]` (default 1) points play, as
  `\together`; every point after them makes no sound of its own: its level
  at each output is that output's **modulation index**, summed over the
  shaping points and held to `~manifold[\depth]`. The modulator's ratio is
  set by the level the loudest shaping point is on -- 1 on the first level,
  2 on the second, 3 on the third -- harmonic, so going up a level changes
  the character, not the tuning.
- `~manifold[\carrier] = \fm` (the default) or `\ring`: frequency
  modulation of each voice -- pure to bright to bell-like -- or ring
  modulation, the bolder, metallic one, at the same ratio and depth.
- One SynthDef gains a modulator, index 0 by default, so with one point
  every sound is as today.

**Where it lives**: example-osc's point state becomes a `PlayerPoint` --
position, height, movement, walk, recording, smoothers -- held sixteen at
most, in `ofApp`; OscOut sends a frame's further points; the patch's role
and modulation rules are plain functions beside its musical ones (3B-2b), so
they move into SAE unchanged. No new `.cpp` in the addons; if example-osc's
point state is split into its own file, the slice says so and a Project
Generator Update comes before the build.

**Tested by**: OSC vectors -- several points' frames, an added point
described, a removed point zeroed then dropped, point 1's addresses
unchanged; control vectors -- a file of three opens three, a point taken
while another is held, the hand taking back only its own; music vectors in
Python and sclang -- `\together` sums and holds, `\carrier` index, depth and
ratio by level, `\ring`, one point sounding as before; mutation gates on
each; `make panel` checks the sections flow at many window sizes, headers
never alone at a column's foot; `make osc-live` with three points;
`make sc-live` with `\carrier`; the drawlog session adds two points, moves
each, removes one, and shows the sections.

##### Open questions, with recommendations

1. **The default role**: `\together` or `\carrier`? *Recommended:
   `\together`* -- a point you add is heard at once and plainly, and it
   proves each point's stream; `\carrier` is one line away.
2. **Playing points: shared voices or separate?** *Recommended: shared* --
   one instrument, two hands, louder where they meet; separate voices (a
   second point an interval above) would double the synths and can come
   later as a third role.
3. **The carrier: FM, ring, or both?** *Recommended: both, FM the
   default* -- the synth change is a few lines either way; FM is gentler
   across a chord, ring the bolder choice.
4. **Three or more points under `\carrier`**: *Recommended: a count of
   players* (`\players`, default 1) -- the first that many play, every
   later one shapes them all. With shared voices, pairing (2 shapes 1, 4
   shapes 3) would make no difference to what is heard, so a count says
   it more plainly.
5. **A point's name choosing its role** (a point named `fm` shapes):
   *Recommended: not yet* -- the role is the patch's setting in this
   slice; names stay labels until a patch needs more than one rule at once.
6. **Removing a point**: only the last (`A`), or the chosen one, numbers
   closing up? *Recommended: only the last* -- numbers never shift under
   a receiver or a sender holding a point.
7. **Where `a` puts a new point**: *Recommended: at the mouse over the
   map, else the middle, at the chosen point's height* -- on top of the
   chosen point it would be hidden and sound the same.
8. **Each point's own movement**, or movement for point 1 only?
   *Recommended: each its own* -- point 2 gliding while your hand drives
   point 1 is the reason to have two.

Answered 2026-11-18: 6, 7 and 8 yes; 1 to 5 as recommended -- the patch's
roles a starting sound, tuned by ear once two points can be heard (a line at
the top of the patch, nothing rebuilt). On 8, said to Lee and agreed: one
point is chosen at a time (left / right, or a press on its dot); the one
left keeps doing what it was given. **Each point's own**: `m` `M` (its
movement), `r` (its recording), `h` (its height's movement), `{ }` `< >`
(its step distance and glide time), `n` (its seed). **Shared by all**: the
clock (`b`), when steps happen (`c`), how often (`, .`) and `return` -- a
beat steps every moving point at once, so they keep time together. Dragging
any point's dot takes that point by hand, as `M` does; the others carry on.

#### 3B-2h: the patch's choices, a block at a time (spec, 2026-11-19)

Lee (2026-11-19): someone who wants to hear the work should not have to go
through the dense patch to find a meaningful change -- a separate
SuperCollider file, its parts selected and run with cmd-return to change
between modes, each with a simple explanation of what it changes and what to
expect, grouped so they are easily picked. Nothing on the openFrameworks
side.

- **`manifold-modes.scd`**, beside the patch: START (loads the patch from
  beside it; already playing, it says so), STOP (cmd-period), RESET, then
  four groups -- NOTES, SHARED OUTPUTS, SOUND, POINTS -- of short blocks,
  each headed by what it is, what to do in example-osc, and what to expect.
- **Groups combine**: a block sets every setting of its own group and none
  of another's, so one from each group makes a whole.
- **One helper in the patch**, `~manifold[\choose].(what, settings)`: sets
  them, retunes every map (builds its voices again for another sound;
  neither for the points' settings, read every frame), and posts what was
  chosen and where everything stands. The patch keeps its own settings as
  it starts (`\defaults`) for RESET, and knows whether it is playing.
- **Choices last until the patch starts again**; the patch's file stays
  the place for its defaults.

Answered 2026-11-19: yes to all three recommendations -- groups combine,
START loads the patch, choices last until it starts again.

#### 3B-2i: following, and keeping apart (spec, 2026-11-19)

Lee (2026-11-19): "as I add a new moving node I would like to make it
follow on the time -- if I add something, I would like it to wait for one
movement then go to where the first one was; if I add a third, then it
waits and then goes to where the last one went, so each follow each other.
It would be good if it would be aware of where the other one was, so if it
does get to the point that it's about to be on top of the other, that it
waits a certain distance and then moves to where it was -- then if later on
there is a shape that we create, such as circles or other shapes, they won't
bunch up if there's not enough space on one spot."

**Follow: a movement of its own.** `m` gains a fifth: your hand, the
recording, jump, glide, **follow**. A point following goes, on each step,
to where the point before it was at the step before -- point 2 after point
1, point 3 after point 2: a chain, each one step behind the one ahead. Point
1 has no one ahead, so `m` skips follow for it.

- **On the steps everyone keeps**: the shared clock and step timing (`c`,
  `, .`, the bridge's beats). At each step every point's place is noted; a
  follower then glides, over its own travel (`< >`, at most the step
  interval), to where its leader was at the step before. Added, it waits
  one step -- nothing noted yet -- then goes.
- **Whatever moves the leader**: your hand, a recording, a walk, a sender
  from outside, another follow. The follower goes where it *was*, not
  where it is going.
- **Height too**: where the leader was includes its level position, so a
  chain climbs the levels as its leader did.

**Keeping apart: a rule of its own**, so shapes later use the same one. A
point that moves itself never ends a step within the **gap** of another
point:

- **Measured** on the map, between where the others are as the step starts
  and where the step would end -- and only between points on about the same
  level: a level or more apart is apart, whatever the map says.
- **Too close**: it goes as far along its way as it can and stops at the
  gap -- waiting there -- and takes up the chase at the next step. Already
  within the gap (added on top of another): it waits where it is.
- **Never off the map**: stopping short where that would be off the map,
  it waits instead.
- **Your hand is never held**, nor a sender from outside: the rule holds
  back only points moving themselves.
- **The gap**: `g` steps through 0.04, 0.08 (the start), 0.15 of the
  map's width, and off.

**Where it lives**: `ofxManifoldFollow.h` in ofxManifoldPlayer, header-only,
no openFrameworks -- the notes of each step, where a follower goes, and the
keeping apart, as plain functions on numbers -- so it is checked like the
rest of the player, and shapes reuse it. example-osc only asks it each step.
Nothing new is sent: SuperCollider hears points where they are, as now.

**Tested by**: a Python reference worked out another way, and vectors --
the chain one step behind, the first step waited, height followed, the
travel held to the interval, every leader kind, the gap stopping short,
waiting when already close, a level apart not too close, off the map
waiting, the gap off; gates on each; the drawlog session three points
chained behind your hand.

##### Open questions, with recommendations

1. **Who follows whom**: each the point before it (a chain), or all of them
   point 1, each a step later than the last? *Recommended: the chain* --
   what Lee described; a point removed only ever the last, so no chain is
   ever cut in the middle.
2. **A point added starts following?** *Recommended: yes* -- a point added
   while point 1 moves joins the line at once; `m` to choose another
   movement, `M` for your hand.
3. **One step behind, in steps -- or a smooth trail a set time behind?**
   *Recommended: steps* -- "wait for one movement" is a step; it keeps time
   with the bridge's beats; a smooth trail can come later as its own
   movement.
4. **Keeping apart from every point, or only from its leader?**
   *Recommended: every point* -- so a chain folding back on itself, or a
   shape later, never bunches.
5. **Should jump and glide keep apart too?** *Recommended: not yet* --
   their walks are seeded; held back they would no longer replay the same
   for the same seed. Followers first; shapes next.
6. **Too close: stop at the gap partway, or wait where it is?**
   *Recommended: stop at the gap* -- "waits a certain distance and then
   moves to where it was".
7. **The gap's key**: `g`, through 0.04, 0.08, 0.15 and off? *Recommended:
   yes* -- every point's, like the step timing.

Answered 2026-11-19: "all yes".

#### 3B-3: the virtual room on headphones -- two renderers (plan, 2026-11-20)

Lee (2026-11-20): "Now we are doing the spatial audio. I would like to do it
two versions: one native to SuperCollider, and then the best libraries that
are most used by the community -- so some part is independent of any major
library."

**What the room is.** Every placed output is a loudspeaker in a room: its
node's place on the floor plan and its level's height -- what `/shape`
already sends (3B-2b). The map decides how loud each speaker is, as it does
now; the room decides where each is heard from, by a listener in the room,
on headphones. So nothing new comes from openFrameworks: SuperCollider
places the speakers from the shape, and decides the rest.

**One stage, swappable.** The voices stop panning themselves to stereo:
each plays into a bus of its own, one per output, and a **renderer** turns
those buses, with the speakers' places and the listener's, into the two
headphone channels. The music, the points, the modes file are untouched.
Choosing a renderer is a group in `manifold-modes.scd` -- ROOM -- like the
others.

- **`\stereo`**: what it does today -- each output panned left to right by
  its place. Kept, to compare against.
- **`\head` -- the native one**: core SuperCollider only, no quark, no
  plugin, no data file. Each speaker is placed relative to the listener --
  its direction (azimuth, elevation) and distance -- and heard through a
  model of a head: the far ear later (interaural time, a delay by the
  head's size and the angle) and duller and quieter (interaural level, a
  shelf); the speaker's height as a pinna-like notch that moves with
  elevation; its distance as level (1/r, held near) and as air (a gentle
  low-pass farther off); and one room reverb all the speakers share, more
  of it the farther a speaker is. Good at left and right, near and far;
  front and back, up and down, only roughly -- no model without measured
  ears does them well. It always works, everywhere SuperCollider does.
- **`\atk` -- the library one**: the Ambisonic Toolkit, the SuperCollider
  community's own spatial library (the atk-sc3 quark, with sc3-plugins).
  Each speaker encoded at its direction, the sound field decoded for
  headphones through measured ears (its binaural kernels, CIPIC or Listen
  heads, one chosen). Front and back, up and down, as real as a fixed
  binaural render gets. The room's distance and reverb as in `\head`, so
  the two differ only in the ears.
- **The patch runs without the library.** Asked for `\atk` without the ATK
  installed, it says how to install it and stays on `\head`.

**Comparing against SuperCollider's own panning** -- what 3B's plan asked
of the room. A second choice in ROOM: **the map** (the speakers as the map
drives them, as above) or **one source** (the sound placed directly at the
point's own position in the room, through the same renderer, the map's
weights set aside). Switching between them, the same point in the same
room, is the comparison: what the manifold does against what a panner does.

**Where it lives.** In the patch, beside the music: the room's settings at
its top (size, listener, renderer, ears), the geometry (a speaker's
direction and distance from the listener) and the head model's numbers as
plain functions -- checked against a Python reference, as the music is --
and one SynthDef per renderer. openFrameworks: unchanged.

**Slices.**

- **3B-3a: the stage and the native room** -- the buses, `\stereo` and
  `\head`, the shared reverb, ROOM in the modes file.
- **3B-3b: the ATK room** -- `\atk`, its install noted in the modes file
  and the README; the same room around it.
- **3B-3c: one source against the map** -- the comparison.

**Tested by**: the geometry and the head model's numbers in Python and
sclang, as the music is (`make sc`); on a server (`make sc-live`), a click
from a speaker on the left heard first and loudest on the left, one ahead
the same in both ears, one far quieter than one near, one above duller in
the notch's band -- measured from the headphone channels, not assumed;
`\atk` the same checks when the ATK is installed, skipped and said so when
not. CI has no SuperCollider, as now.

##### Open questions, with recommendations

1. **Where is the listener?** *Recommended: standing still in the middle
   of the room, ears at the middle level's height, facing the front of the
   map* -- every speaker around them, the levels above and below. Riding
   the point (hearing from where it is) could come later, as its own
   choice.
2. **How big is the room?** *Recommended: 8 by 8 metres, the levels
   spread over 4 metres of height* -- a setting at the patch's top.
3. **The room's sound: dry, a shared reverb, or early reflections too?**
   *Recommended: a shared reverb (core FreeVerb), more of it farther off;
   reflections later* -- a room you can hear, without a model of its walls
   yet.
4. **Should the native version also take measured ears from a file (an
   HRIR set, played by core convolution)?** *Recommended: no* -- the native
   one needs nothing but SuperCollider; measured ears are what the library
   brings.
5. **Which library: the ATK, or the IEM plug-ins (BinauralDecoder,
   RoomEncoder) hosted through VSTPlugin?** *Recommended: the ATK* --
   installed as a quark, running inside the server like everything else,
   what SuperCollider users reach for first; the IEM suite is excellent and
   its RoomEncoder models walls, but it means hosting plug-ins -- a later
   choice if the room needs walls.
6. **The ATK's order: first-order with its binaural kernels, or
   higher-order through another decoder?** *Recommended: first order, its
   own kernels* -- the ATK's binaural decoders are first-order; higher
   orders mean another library again.
7. **Outputs with no place** (an aggregator, a mix): *Recommended: heard in
   the middle of the head, as now* -- they have nowhere in the room to be.
8. **Head rotation**: *Recommended: a setting for which way the listener
   faces, nothing more* -- no head tracking.

Answered 2026-11-20: "yes, all recommended".

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

### 3A-8: a ring or a block in one step

As proposed, with Lee's answer (2026-11-07): yes, all as recommended -- the
layout goes where the mouse points, and the triangular grid gets a block
too.

- **`l` on the polar grid** lays out a ring on the ring nearest the mouse:
  one node per spoke, numbered clockwise from the spoke nearest the front
  (map +y, up on screen); of two equally near, the one just right of front.
  The ring is joined as one region.
- **`l` on the square grid** lays out an n x n block centred on the grid
  point nearest the mouse (for an even n, the extra row and column go up and
  right), numbered row by row from the front-left, joined as squares. **On
  the triangular grid** the block is a rhombus of n x n points, each cell
  split into two triangles across its short diagonal.
- **`[ ]`** set n, 2 to 8 (3 to start), on a square or triangular grid; on
  the polar grid they still set the spokes, which is the ring's size. The key
  panel names what `l` would lay out: `lay out a ring of 8`, `lay out a 3 x 3
  block`.
- **Each node is placed as a click places it**, so with auto-output each gets
  its own output on every level; the outputs are made level by level, so a
  ring of 8 on two levels takes channels 0-7 on the first and 8-15 on the
  second. One undo step, labelled `lay out`. The new nodes are left selected,
  so `y`, `m` or a drag acts on the whole layout next.
- **All or nothing:** refused on the free grid, at the polar centre, if any
  node would land off the map, or on a node already there. A ring of radius
  0.5 touches the edges exactly; the spacing is a float, so the edge is
  allowed a hundred-thousandth of slack and the nodes are held on the map.
- **Refactor:** `place` is now `placeAt` (the node) and `giveOutputs` (its
  outputs, level by level), shared with `layout`. The grid gained `spokes()`,
  `angle()` and `centre()`.
- 5 editor scripts (60), 18 checked steps (790); 18 mutation gates (332).
  The drawing session gained four frames: a ring, a 4 x 4 block, a block in
  triangles, `l` refused with the grid off. `make keys` covers `l` (47).

### 3A-9: curves on every level, one key for the layout's size, triangles that grow as triangles

From Lee trying 3A-8 (2026-11-07): the curves vanished on a new level and
never appeared when binding on one; `[ ]` meant spokes on one grid and size
on another; and the triangular block should grow as triangles -- "the
first is three, add another point that would make a diamond, then add two
more points that would make a triangle made up of three upright triangles
and one pointing down". All as proposed, agreed the same day ("ok go").

- **A new level keeps the links between nodes.** It copies them as a
  pattern: a link to node M's own output becomes a link to M's own output on
  the new level, same weight -- A to B is A to B-2. What a node sent to any
  other output still goes to its own (3A-6d), and a node that sent to none
  sends to its own. 3A-6d had folded everything into the node's own output,
  losing A to B.
- **The curves are drawn on every level.** A curve went to the node NAMED
  like the output, and on level 2 there is no node called B-2. It now goes
  to the node the output belongs to (`Model::ownerOf`: the node of that
  name, else the node it is named after).
- **`[ ]` set the size of what `l` lays out, on every grid; `{ }` set the
  polar spokes.** Each grid keeps its own size: rings deep on the polar
  grid (1 to 8, from 1), the block on the square grid (2 to 8, from 3), the
  triangle's stage on the triangular grid (1 to 13, from 3).
- **Rings deep:** from the ring pointed at inward, outermost first, joined as
  four-sided pieces between rings and the innermost ring whole. Reaching
  the centre, the centre comes last, joined to the innermost ring as
  triangles. Refused past the centre: "the rings would pass the centre --
  point further out".
- **The triangle** on the triangular grid is upright, point toward the
  front, numbered row by row from its point, joined as small triangles. Its
  stages fill each new row's inner points first, then its two corners: 3,
  4 (a diamond), 6, 8, 10, 13, 15, 19, 21, 26, 28, 34, 36 nodes. Placed so
  its middle is nearest the mouse. It replaces 3A-8's rhombus.
- **The preview:** after `[` or `]` (or `{ }` on the polar grid) the shape
  `l` would lay out is drawn faintly at the mouse -- its pieces outlined,
  its nodes as rings, numbered when there are 16 or fewer -- until `l`,
  another key or a click. `planLayout` works it out; `layout` does exactly
  the plan, so the preview cannot disagree with the result. The message
  says what `l` will make: "l lays out 2 rings of 8".
- **Found on the way:** the drawing session wrote its log with `fputs`,
  which stops at a NUL byte, so a dangling string cut the log short without
  failing. It writes the whole buffer now, and the dangling string (a key
  entry read from a key map already gone) is fixed.
- 2 editor scripts (62), 31 checked steps (821); 18 new mutation gates, and
  3A-8's 18 rewritten for the new code as 15, the other three now covered
  by new ones (347). The drawing session's
  layout frames now include two previews. `make keys` covers `[ ]` and
  `{ }` (48).

### 3A-10: c and C, and a key panel that fits

From Lee (2026-11-07): `c` cleared a node's bindings and left it silent
with "no way to make them unsilent"; and the key list ran off the screen
again, worse at the text size he needs -- "can we also utilize the right
top of the screen for the outputs column".

- **`c` puts the selected nodes back to their own output** on this level:
  every other binding gone, fill 100%, as when placed. Its own output is
  the one named for this level (P, P-2 ...), else one named after it that
  it feeds on no other level. With none, one is made on the next free
  channel if auto-output is on; if it is off the node is left as it was and
  named: "no output of its own -- p turns auto-output on, or pick a fader
  and press b". **`C` makes them silent** on purpose, which is what `c` did.
  The panel's fill line says how many selected are silent, and the bind
  warning now points at `c`.
- **The key panel flows.** The status sits at the top of the first column;
  the keys run down it and on into the next columns, which start at the top
  of the window -- outputs at the top right, when it all fits that way.
  Columns are at least 32 characters, as many as the room holds. A group
  that does not fit is split, "(more)" on its continuation.
- **When the keys do not fit**, in order: the faders keep to the width under
  the views and the panel runs down to the window's bottom; the keys are
  packed tight; the views give up width, down to half their size; the keys
  that would not act now are left out; and what still does not fit is
  counted on the last line, "+ N more -- ? shows every key".
- **`?` shows every key over the whole window**, at the same text size. Any
  key or click closes it and does nothing else.
- **The window's layout is one function**, `layoutEditor` (with `flowKeys`),
  without openFrameworks; the app draws from it. `make panel` checks it at
  576 window and text sizes (eight windows from 1024 x 640 to 2304 x 1296,
  text 8 to 24 and the bitmap font, 3D view on and off, two grids): every
  line in its room, none on another, every key once and in order or
  counted, each column starting at its top, the levels keys under the 3D
  view above the faders, the views never under half, every key shown at
  1280 x 800 and up with text up to 14. It prints what is left out at each
  size; at 1360 x 884, every key shows up to 14 point, all but 1 at 16, 5
  at 18, 12 at 20 -- `?` shows the rest.
- The drawing session reads where the map and faders are from the app, so
  it clicks the same things whatever the layout; it records `C` and `c`,
  and the panel at 1360 x 884 with text 12, 16 and 20, the key sheet, and
  the 3D view at 14. Its stand-in font can now be measured at any size.
- 1 editor script (63), 24 checked steps (845); 17 mutation gates (364).
  `make keys` covers `C` and `?` (50).

### 3A-11: the keys tidied; L and F; a grid that fits; the lines you line up on

Lee asked to look over the key bindings for crossover and redundancy
(2026-11-07); the review found shift meaning four things, trim the one
down/up control without a pair, no way back through the outputs, and two
unmodified keys that replace the whole map. Agreed: "do all recommended",
and three more -- lay out a group of nodes without joining it, thin it,
then join it ("it can be a little tedious to go around putting them in
place"); a grid with no cut-off tiles at the edges; and the line a node is
about to go on, highlighted, to see what it lines up with.

- **The rules the keys follow now:** two keys side by side are down and up,
  shift a finer step (`q w`, `y h`, `i k`, `; '`, `9 0`, `e r`); one key
  steps on, shift steps back (`tab`, `j`); lowercase makes, uppercase
  removes (`o O`, `g G`); lowercase does it, uppercase the bigger version
  (`b B`, `c C`, `d D`, `f F`, `l L`, `m M`, `n N`).
- **`cmd-n` new empty map, `cmd-e` the example** -- no longer `x` and `e`,
  one stray key from losing the map. Undoing either gives the map back its
  file name (it used to come back untitled, so `cmd-s` asked where).
- **`shift-tab`** picks the output before.
- **Trim on `9 0`**, a dB down and up, a tenth with shift.
- **Rotation on `e r`**, 5 degrees back and on, 1 with shift; pressing the
  grid's number again straightens it. (The review had proposed moving the
  reset to shift-r -- but `R` already turned the grid back, so it went to
  the grid's number instead.) `t T` and `0` are free.
- **`L` lays out the nodes only**, the same shape `l` would, not joined:
  delete what is not wanted, select the rest, and **`F` joins the grid's
  whole cells among them** -- each square, triangle, or polar piece whose
  corners are all selected and on the grid; on the polar grid a ring whose
  every spoke is selected, with nothing joined inside it, whole. Cells
  already joined are skipped. `F` works on any nodes on the grid, not only
  laid-out ones.
- **The grid fits the map.** `- =` step how many cells across (4 to 50,
  20 to start), so the spacing is always 1 / n: the square grid's tiles meet
  every edge; the triangular grid's rows are centred with even bands above
  and below; the polar grid's rings step out from the centre. Only whole
  cells are drawn -- no quarter tiles -- and a turned grid turns about the
  grid point nearest the middle. The panel says "20 across". Nodes placed
  on the old triangular grid, which started at the bottom corner, may sit
  off the new one.
- **The lines a node is going to** -- its row and column, the triangular
  grid's third direction, the polar grid's ring and spoke -- are drawn
  brighter while placing (under the mouse) or moving (through the moved
  node), and the nodes already on them are ringed. `Model::linedUp`.
- `gridCells`, `wholeRings`, `joinCells`, `linedUp` are in the model,
  checked by the reference: 4 editor scripts (67), 28 checked steps (873),
  with a count of the whole cells (`CELLS`) and the nodes in line (`LINED`);
  18 mutation gates (382). The drawing session records `L`, `F`, the lines,
  the grid at 23 across, the triangular and polar grids. `make keys` (52).

### 3A-12: softer hints, a grid without gaps, a panel of what can be done

From Lee trying 3A-11 (2026-11-07): the guide lines were "too dark -- I
just wanted an indication"; drawing only whole cells left gaps ("is it an
either or?"), the polar grid was fine as it was, and the triangular grid
showed "chunks of missing triangles"; the key panel should show only what
can be done, what each key does a little bolder than the key, and each
group's keys in its own colour.

- **The hints are a shade:** the lines through where a node is going are a
  little brighter than the grid, one pixel wide; the nodes on them get a
  thin, faint ring.
- **The grid is drawn to the map's edge again,** lines cut there, so there
  are no gaps -- and straight, it fits: the square grid at 1 / across meets
  every edge in whole tiles; **the triangular grid now fits exactly too**:
  its rows are 1 / rows apart, rows the whole number nearest across / 0.866
  (so its triangles are within a few hundredths of equilateral -- 0.4% at
  20 across), every other row half a step in, so the sides cut only through
  whole half-triangles in a regular zigzag and the top and bottom rows lie
  on the edges. A turned grid is cut at the edges, as any would be. The
  polar grid is drawn as it was before 3A-11. Whole cells still decide what
  `F` joins.
- **The panel shows only the keys that would act now.** The views are sized
  for every key, lit or not, so they keep their size as keys light and
  darken. `?` still shows every key, dim ones dim. A pure `panelKeys` gives
  the panel's keys to both the drawn panel and `layoutEditor`.
- **Each group has its colour** -- its title and its keys in it -- and what
  a key does is near-white, a hair bolder. Each column's keys are as wide as
  its longest key, so none is cut and the actions line up.
- `make panel` now also checks that the panel shows no key that cannot act
  and that the views keep their size with nothing selected. 2 mutation
  gates (384).

### 3B-1: OSC out that grows and shrinks with the map

As section J, 3B-1 says. The first piece of `ofxManifoldPlayer`, and the
first player, `example-osc`.

- **`ofxManifoldPlayer`** is a new addon beside `ofxManifoldEditor`, no
  openFrameworks: `OscMessage`, `encode`, `decode`, `oscName` and `OscOut`,
  which says what to send each frame. The app sends through openFrameworks'
  own ofxOsc (the same addon Lee's other OSC work uses), turning each
  `OscMessage` into an `ofxOscMessage`, unbundled; `encode` and `decode`
  stay as the tests' encoder, checked byte for byte against python-osc, and
  for counting the bytes on the panel. (First built on ofxNetwork, sending
  `encode`'s bytes; changed before the app was first generated.)
- **`tests/ref/reference_osc.py`** writes 35 vectors: 16 encodings, each
  built by python-osc as well and identical byte for byte (CROSS) -- the
  file is the same without python-osc, which CI does not install; 3 packets
  python-osc built, decoded; 9 malformed ones refused; 7 sender scripts
  (packed, described again as outputs come and go and on request and every
  two seconds, per output with its refresh, both shapes with values and the
  position, the rate and a late frame, a prefix and cleaned names, hello).
- **`make osc-live`** sends a run of 56 packets over a real UDP socket on
  the machine; each must be exactly what the reference sends, and python-osc
  must read each the same; then a hello goes back and must be counted.
- **`example-osc`**: drag the point, hold up or down for height; `p`, `i`,
  `[ ]`, space; `o` opens a pair, `e` the example. It shows where it sends,
  how much went out in the last second, whether anyone answered, and the last
  send. With it `supercollider/manifold-receive.scd` -- a voice per output,
  rebuilt as the outputs change, panned left to right, band-passed pink noise
  so each can be told apart, a 20 ms lag, a hello a second -- and
  `receive.py`, a receiver with no dependencies (tried against the test
  sender: it described two outputs, then three, and its hello was counted).
  The protocol for anyone building a receiver: `ofxManifoldPlayer/OSC.md`.
- 18 mutation gates (402). The one that first went uncaught -- a prefix
  keeping its trailing slash -- was because the vectors recorded the prefix
  already cleaned; they record it as given now.

Added before the app was first built (2026-11-09):

- **The room example**, `Model::loadRoom()`: a square of four corners, FL
  FR RL RR, on the floor at 0, the middle at 0.25 and the top at 0.5, each
  level's corners on outputs of their own (FL, FL-2, FL-3 ...), twelve in
  all. Made as a user would make it -- the floor, then a new level above
  twice -- with auto-output on whatever its setting, and the setting left as
  it was. One editor vector script (`room_three_levels`), 5 gates (407).
  It is where the virtual room on headphones (now 3B-3) starts.
- **example-osc shows the levels**: the editor's column of level dots right
  of the map, and its 3D view beside that (`3` shows and hides it). The
  level the point is on or above becomes current, so it is the brighter
  one; that is only for showing -- what is sent is worked out from every
  level at the point's height either way. It opens on the room; `e` is the
  room, `E` the editor's flat example. The window starts at 1280 x 720; the
  map and the 3D view shrink together to leave the panel 44 characters.
- **Smoothing what is sent**, `OutputSmoother` in ofxManifoldPlayer. The
  kernel's WeightSmoother smooths node weights: its state is every node lately
  near the point, so it grows with speed and with how fine the map is, is
  matched up by search every frame (the square of its length), is paid again
  per moving object, and knows only the floor plan. A player sends one level
  per output, and the map fixes how many, so the player smooths those: a
  fixed list, one per output and one per node value, never growing, no
  pruning, no search -- objects x outputs, a plain loop. Same step as the
  kernel's (half-life, then an optional speed limit); within 1e-4 of its
  target a number is its target, so a still point stops sending in
  per-output mode instead of trickling changes for a second; a list of
  another length, or after reset(), is taken at once, not glided into.
  `tests/ref/reference_smooth.py` writes 16 sequence vectors (8 ANALYTIC,
  5 CROSS, 3 SPEC); `make test-smooth` checks them and runs the kernel's
  WeightSmoother alongside on one number, agreeing within 1e-6 until the
  player's first arrives. 8 gates (415). In example-osc both lists are
  smoothed every frame, sent or not; `s` on or off, `-` `=` the half-life
  (0.01 to 2 s, 0.05 to start); a map change resets them. For the master
  player: one per moving object, made when it appears and dropped when it
  goes, under a cap.
- **example-osc shows what the editor shows, without the editing**: the
  editor's `drawMap` with nothing selected and no grid (node shapes by kind,
  halos, values, the current output's nodes ringed, the point and its lines,
  the level dots) and its faders across the bottom (`drawChart`), hover
  labels included. The faders show what is SENT: the smoothed levels, the
  smoother now taking routed, after-trim and silence as one list so a bar,
  its trim tick and the silence bar glide together. Links: the editor draws
  the selection's; `Scene::linked` (null for the editor) lets a player name
  whose -- example-osc's `l` cycles the nodes being heard, every node, none.
  `tab` / shift-`tab` or a click on a fader picks the output. Shift `-` `=`
  steps the half-life by 0.01 s, between the coarse steps. `drawGrid`
  returns before reading the grid when there is none. The window starts at
  1440 x 900.
- **More than one map: protocol 2.** Two copies of example-osc collided two
  ways: the same addresses, so a receiver could not tell the maps apart, and
  the same answer port -- worse than a refusal, since ofxOscReceiver shares
  ports by default (SO_REUSEPORT), so the second would quietly split the
  first's answers. Now:
  - every map's messages go under root/<its name> (`setMapName`, cleaned by
    `oscName`; "untitled" when it has none, not "map", which would read like
    the roll call). A new name says everything again at once.
  - the roll call, root/map: `s` prefix, `i` answer port, `i` outputs, `i`
    protocol (2), leading every description. A receiver listens on one
    address to find every map, where to answer it, and when it has gone.
  - describe and hello are heard on the map's prefix or the root (a receiver
    asking every map on the port at once).
  - example-osc hears answers on the first port free from 57121 (16 tried),
    not shared (reuse off), given in its roll call and on its panel; the map's
    name is its file's (room, example for the built-ins).
  - the receivers follow every map: the SuperCollider patch makes a set of
    voices per prefix (each map's bands a little higher), says hello to each
    at its port, asks ports 57121-57136 to describe at start, lets a map go
    after 5 s without a roll call, and warns once if two maps share a name;
    `receive.py` the same, printing levels as numbers, a line per map when
    they change. The patch's own functions are now called by key
    (`~manifold[\build].(...)`): `~manifold.build.(...)` calls the function
    as a pseudo-method of the Event, with the Event as its first argument.
  - the master player later sends the same from one app: several prefixes,
    several roll calls, one answer port -- the receivers cannot tell it from
    several copies.
  OSC.md is rewritten for protocol 2.
- **Following a node, in place of a selection.** The editor shows a binding
  through the selection: a selected node's share as a brighter band on each
  fader, a dot over every output it feeds (its own and any it is bound to),
  its fill line, its links. example-osc had nothing selected, so none of it
  showed. Now it follows one node -- the loudest at the point, or one pinned
  with shift-click (shift-click off any node: the loudest again; a new map
  unpins) -- and hands it to the views as the selection, so all of that is
  drawn as the editor draws it, with the yellow ring on the map. `l` now
  cycles the followed node's links (the default, as the editor), the nodes
  heard, every node, none. The followed node's part goes through the
  smoother with the rest, always as many numbers as channels (zeros when
  nothing is followed), so changing who is followed never restarts the
  smoother: no jump in the sound. The drawlog session pins FL on the room and
  moves away, and follows O, the flat example's composite, onto its four
  faders.
- **A tone per output in the SuperCollider patch.** The noise bands were
  hard to tell apart by ear; each output is now a note -- a soft sine with
  two quiet overtones -- climbing a major pentatonic scale from C3 in channel
  order, so anything sounding together stays consonant and each output can be
  picked out by pitch (the room: C3 D3 E3 G3 on the floor, A3 C4 D4 E4 the
  middle, G4 A4 C5 D5 the top). Each map starts a fifth above the last. The
  post window lists every output's note. `~manifold.sound = \noise` brings
  the bands back. Tests: an encoding vector for the roll
  call (python-osc agreeing), a send script `maps_named_and_announced`, the
  live UDP run now a named map with its roll call (58 packets); receive.py
  run by hand against two simulated maps, one stopping. 8 new gates and one
  moved (423).

### 3B-2: movement, first slice -- random jumps and glides, clocks

Built as specified (2026-11-14), every recommendation taken. What it came to:

- **`src/sources/ofxManifoldMotion.h`** (header-only, no openFrameworks):
  `Motion`, `RecordedMotion`, `SplitMix64`, `StepTiming` (every, at, pattern),
  `WalkSettings`, `RandomWalk`. Worked in double. The walk keeps where it
  last got to and a checkpoint every 256 steps; `stepsBy`, `stepTime`,
  `addStep` (append only), `checkpoints` for the tests.
- **`src/io/ofxManifoldSerializeMotion.h`**: `saveMotion`, `loadMotion`.
  Numbers as the shorter of %.15g and %.17g that reads back exactly, so
  0.30000000000000004 survives and 0.4 stays 0.4; the Python writes the
  same text, byte for byte.
- **`ofxManifoldPlayer/src/ofxManifoldClocks.h`** (header-only):
  `SecondsClock`, `PhaseClock`, `readPhase`, `readBeat`. One correction to
  the spec as written: a restart reads as a new bar only when it comes late
  in a bar (a drop of more than half); early in one, the count holds until
  the phase passes where it was. Never backwards either way.
- **Tests**: `tests/ref/reference_motion.py` writes `tests/vectors/motion.vec`,
  52 vectors (39 ANALYTIC, 7 CROSS, 6 SPEC); `make test-motion`. The
  reference refuses to write a vector whose yes/no rests on the last bit (a
  candidate within 1e-9 of an edge), so float rounding on another machine
  cannot flip a choice; the suite passes under `-mfma -ffp-contract=fast`.
  splitmix64 against Rosetta Code's published outputs for seed 1234567.
  `make osc-live` adds a stand-in bridge over UDP (`tests/phase_udp.cpp`,
  `tests/check_phase_udp.py`): 121 phases with one late packet, 9 beats,
  read as example-osc reads them, checked against the reference. 38 gates,
  every one caught (461).
- **example-osc**: `m` hand / recording / jump / glide; `r` records (on the
  clock running, so a recording made in bars plays in bars); `,` `.` the
  interval (1/16 to 4); `<` `>` the travel (0.05 to 1.6); `{` `}` the step
  distance (anywhere, 0.5, 0.25, 0.1, 0.05); `c` timer / clicks / the
  bridge's beats; return a step; `h` height stays / across the levels; `n` a
  new seed; `b` seconds / SuperCollider's bars. Bounds are the map's: its
  inside test through an evaluator of the walk's own (the app's keeps track
  of the region the point was in, and the walk's tries must not disturb it),
  the whole rectangle, the levels' heights. Each time a walk starts again --
  a key, a click to put it somewhere -- it draws afresh from the seed and
  the count of starts: the same seed starting again from where its first
  step went would only go there again (found in the drawlog session). The
  seconds clock is the frames' times summed, so the drawlog session moves
  too. Port 9001 not shared; when taken, the panel says so.
- **Not done here**: circles, spirals, orbits with a moving centre, edges for
  shapes that can leave the map, flocking -- the next movement slices.

Lee, trying it (2026-11-14): "I was able to record, but I wasn't able to
re-record or stop", and the keys were hard to find. So:

- **`r` always records your hand.** Before, a second `r` while the recording
  played recorded the playing -- the point was the recording's, and drags were
  ignored. Now `r` stops whatever is moving where it is and hands the point
  back to you; `r` again stops and loops the new one.
- **`M`**: your hand at once, from any movement (`m` cycles through them).
- **The path is drawn**: bright and growing while recording, with REC by the
  map's corner; dim, the whole loop, while it plays.
- **`?`**: every key on one page, in groups -- the point, movement, sending,
  smoothing, showing, maps; `?` again or a click to go back. The panel's
  first line says so. The drawlog session records it, and recording, playing,
  re-recording and `M`.

Then: "add alongside the height a random, so I can use the mouse and it would
randomly move the height, smooth and abrupt ... and the opposite, where height
can be moved smooth by hand with up and down while the other random behaviors
go for x". So the height moves on its own:

- **`h`: the height by hand, jumping, or gliding**, whatever moves the floor.
  Drag the point while the height wanders across the levels; or let the floor
  jump or glide and hold up or down for the height. (`h` was "stays / random
  across the levels", tied to the floor's walk; it is now the height's own.)
- **A second walk for the height**: the floor's timing and travel (one set of
  keys), its own seed stream, the map's levels for its range, x and y held
  still -- a RandomWalk with a rectangle of one point, so nothing new was
  needed in the library. Clicks and beats step both walks. A map with one
  level has no range: the panel says the height stays.
- **By hand, while a recording plays**, means the heights you recorded; the
  arrows move the height in every other case.

### 3B-2c: height on the levels -- riding the bends

Built as specified (2026-11-14), every recommendation taken ("next", after
"yes all recommended covers all three").

- **`Levels::heightAtPosition(w, u, extrapolate)` and `positionAt(w, z,
  extrapolate)`** in the kernel, with `levelHeight` (a level's base off the
  floor plan) and `cellBelow`. Restrained by default. The Python reference
  (`reference_levels.py`) works `positionAt` the other way round -- walking
  the cells from the top down, as its `weigh` does. 54 vectors (UPOS, ZPOS):
  one level; flat cells; on, under, over and away from the bend; a partial
  bend; past the top over the bend; off the floor plan; held and carried on
  at both ends; round trips u -> z -> u in every cell, bent and flat, both
  modes. 5 gates; one anchor line reworded (`!(z < h[k + 1])`) so it is not
  the same text as `evaluate`'s.
- **`/position`'s fourth number**, u: `OscFrame::hasLevelPosition`,
  `levelPosition`; an encoding vector against python-osc and a send script,
  with and without it. 1 gate (467).
- **example-osc**: `k` -- on the levels (default), past them, in units --
  keeping the point where it is (u from the height, held when restrained).
  On the levels the arrows move u, one level a second (shift a quarter), and
  the height follows from the surfaces under the point; the random height
  walks u across 0..L-1; recordings keep u (`recordedInLevels`) and replay
  riding the shape; the floor's evaluation now comes before the height, since
  the height needs it. The panel: "height 0.379, level 1.00" and "on level 2
  of 3", "between 2 and 3", or "past the levels, silent". The level dot and
  the 3D view's bright level follow u, not the flat bases.
- **The drawlog session** opens a room whose middle level is raised at FR
  (saved by the editor's model, opened through the stand-in's load dialog,
  which a session can now point at a file): on level 2 by RL the height is
  0.252; by FR, at the same u, 0.379 -- risen with the bend; past the top
  there, 0.561, the bend's gap carried on; `k` to units and back without a
  jump.
- **Not done**: the editor's own listening point still moves in units.

### 3B-2d: levels that touch -- closed shapes

Built as specified (2026-11-14), every recommendation taken ("next").

- **Touching, never crossing.** `Levels::fits`, `copyLevel`, `moveLevel`,
  `setHeight` and `rebuild` (a file) take at-or-beyond; passing is refused as
  before, naming the nodes. Starts may touch too (`moveLevel`, files); a
  copy still may not start where a level starts. `fits` is now public, and
  the editor's raise, match and flatten ask it rather than each keeping its
  own copy of the rule.
- **Settling, not in the spec.** A touch reached by arithmetic -- 0.5 - 0.3
  - 0.2 lands 1.5e-8 below 0 in float -- would have been refused as passing.
  Within `Levels::kTouch` (1e-6) of the level next to it, a height SETTLES
  on it exactly, so a touch is one height and the pinch is found. Files are
  read exactly. Found by the vector `moved_refused`, which failed in C++ and
  passed in Python's double.
- **The pinch**: `evaluate` finds its cell as before; where the cell has no
  gap, or z sits on its lower level and the level below is the same height
  there, t = 0.5 between those two. Where more than two meet, the top two.
  The reference finds it another way -- every level at z, the top two of
  them. `positionAt` takes the lowest cell whose top reaches z, so at a pinch
  the lowest position; extrapolated through an end cell with no gap, the
  nearer level.
- **`Levels::evaluateAtPosition(floor, u)`**: the cell floor(u), t its
  fraction, no division; outside past 0..L-1 or off the plan. The reference
  is a hat over the levels, 1 - |u - j|. `Levels::touches(nodeCount)`.
- **Files**: the map file is version 4 exactly when levels touch
  (`detail::map3Version`); loadMap3D reads up to 4 and refuses a touch in a
  version 3 file ("levels that touch need version 4"); the old 3D reader --
  `loadFloor` up to 3 -- refuses version 4 by its version, checked by a new
  record, REFUSEOLD.
- **The octahedron**: `Model::loadOctahedron()` -- rim N E S W on a diamond,
  C at its centre, four triangles; the bottom starts at -0.5 and the top at
  0.5, C at the starts, the rim at 0 on both; ten outputs. Taller than a
  regular one (that would be +-0.3). shift-cmd-e in the editor (and its key
  sheet), `x` in example-osc (and its `?` page). The Python reference builds
  it from the finished shape, the C++ the way a user would.
- **example-osc rides by u**: on the levels (and past them) the point is
  heard by `evaluateAtPosition`, in units by `evaluate`. New overloads of
  `chartLevels3D`, `valuesAt` and `dotColumn` take the 3D weights ready.
- **Dragging a level's dot** stops at the neighbour, touching -- or a step
  short where the touch would not land in float -- so letting go still
  always lands.
- **Vectors**: 52 levels (stacks that touch, copies that touch above and
  below, starts that touch, pinches between two and three levels, the
  octahedron's inside against the solid's own faces through the C++ floor
  plan, EVALU against EVAL3 away from pinches, positions at a pinch, the
  touching map's files and refusals); 2 editor scripts (`levels_touch`,
  `octahedron`, saved and reopened). 1070 vectors.
- **Existing vectors changed meaning, by design**: in `levels.vec`,
  `moved_refused`'s last move (a touch, now allowed), `refuse_reach` (its
  first raise now passes by 0.1, not touches) and `set_heights` (0.0 on the
  ground allowed, two crossings added); in `editor.vec`, `levels_raise`,
  `levels_go_and_move`, `levels_drag` (0.45, not 0.44) and `levels_match`,
  each script's note saying so; refusals read "would pass" not "would reach".
  CI then found the one gate left uncaught: `levels_match`'s only refused
  match had become a touch, so "Matching heights is not all or nothing"
  survived. The script now raises E to touch the top and matches N past it,
  refused. A lesson for the slice that turns a refusal into an allowance:
  every gate on the refusals it loosens needs its own case still refused --
  run the gates on the changed files, not only the gates edited.
- **Gates**: 23 new, 12 re-anchored and turned round (a node may pass, where
  it was: may touch). 490.
- **Drawlog**: the editor's session ends on shift-cmd-e; example-osc's rides
  up by the rim at E -- height -0.041 to 0.000 while u goes 0 to 0.5, both
  levels heard -- then over the centre, the same u.
- **Not done**: the one drag branch that backs off a step when a touch would
  not land has no vector -- no drag found makes float miss.

### 3B-2b: the map's shape for SuperCollider; one name per node

Built as specified (2026-11-15), every recommendation taken ("yes all
recommended"): a 30-semitone span from C3, a quiet root for shared and
unfed outputs, brightness on, the node's name on the map.

- **`Model::outputPlaces()`**: per output, the lowest level feeding it, how
  many do, and its owner node's place and height on that level (the ground
  when none feeds it). **`Model::ownOutput(node)`**: the first output the
  node feeds on the current level that `ownerOf` says is its own.
- **The player**: `OscOutput` gains `firstLevel`, `levelCount`, `placed`,
  `place` (not `level`: that was already the sound level); `OscFrame` gains
  `hasShape` and `levelHeights`. `OscOut` sends `<prefix>/shape` right after
  each description and on its own whenever its bytes change -- not when only
  the sound levels move, not at all without `hasShape`. `kProtocol` is 3.
  The heights are each level's start.
- **example-osc** fills the shape from the model every frame; turns the
  renderer's own nodes off, so each name is drawn once (the drawlog shows
  it); names take their own output's colour on the current level, in both
  apps (FL in FL-2's colour on level 2, matching its fader).
- **The patch** (`manifold-receive.scd`): the musical rules are plain
  functions between `// ---- music` and `// ---- end of music`, read by
  key from settings at the top -- `\music` (`\registers`, `\progression`,
  `\series`), `\span`, `\fit`, `\bright`, `\quiet`. Voices are built when
  the outputs change and RETUNED, not rebuilt, when the shape changes --
  freq and pan lag, so dragging a level glides rather than clicks. The tone
  has five partials under a low-pass that `bright` opens (by u when sent,
  else by z between the lowest and highest level). The post window names
  each output's note and which behaviour. Also fixed: a map answering on
  port 0 made the patch throw sending it `/describe`.
- **Checked in sclang, not only by reading**: `make sc` runs the patch's own
  music functions against `tests/vectors/music.vec`, written by
  `tests/ref/reference_music.py` (chord tables written out, the series as
  harmonics of root/m) -- 38 vectors over seven shapes and four behaviours,
  plus the whole patch compiling and the shape message read as sent: 40.
  Breaking the chord stack turns 7 red, the quiet level 4. `make sc-live`
  runs the whole patch on a server -- here on a silent JACK server, on a
  Mac on the sound card for a second -- sends it a roll call, outputs, the
  shape, levels and a position, and reads the synths back: the climb before
  the shape, retuned not rebuilt, levels and brightness followed. 7.
  Neither is in `make test` or CI: they need SuperCollider.
- **receive.py** prints the shape when it changes; checked against the
  real C++ sender over UDP. The live UDP run (`make osc-live`) carries a
  shape, including one sent alone when a level moves.
- **Vectors**: editor 3 scripts (`output_places`, `..._unplaced`,
  `..._octahedron`: PLACES and OWN records); OSC 2 encodings and 2 send
  scripts. 1077. **Gates**: 13 new; every player gate run again. 503.
- **The sound itself is Lee's to hear.** Whether \registers' #11 and 13
  sit well, and whether the span suits, are settings to try.

### 3B-2b, after: a somber progression; chords out of the levels' height

Lee, having heard it: "a somber progression as well as this one ... and on
both of them, take them out of the levels and not equally divide them --
I have 3 to 4 levels, and by the time we make it to the top level it's
very high." What was high was `\registers`, the default: 30 semitones
shared equally, so four levels' top chord reached A5. Agreed (2026-11-15,
"yes all recommended"), built the same day, the patch alone:

- **`\somber`**: the progression in a natural minor key, i iv VI v III VII.
- **Both progressions in one register**: each chord note brought into the
  octave above the root -- the chord as close together as it goes -- so
  going up the levels changes the harmony, not the height; the eighth note
  on a level and after, an octave up. The octave lift every six levels is
  gone too. The room's three levels: C E G B, then F A C E, then G B D F,
  all within C3 to B3.
- **`\progression` is the default**; `\registers` stays, rising, its span
  now 12 -- three levels start on C3, E3, G#3; four top out at G#4.
- **Checked**: `music.vec` 46 (the five behaviours over seven shapes, the
  second map in two), `make sc` 48. The reference spells each chord as
  written up the keyboard and folds it down by octaves; the patch takes
  pitch classes. Undoing the one-register rule turns 9 red, the somber
  degrees swapped for the major ones 3.

### 3B-2e: names by level, the level followed, ? three ways

Lee (2026-11-16), seeing the editor listen on the room's middle level with
FL, FR, RL, RR on the corners and in the readout: "even though I'm at what
should be FL-2, FR-2". That reverses 3B-2b's question 4 (the node's name on
every level), answered before it could be seen. Agreed, "yes all
recommended", built the same day:

- **Names by level**: `Model::levelLabel(node)` -- its own output on the
  current level, else its name. The node glyph's label and the point's
  readout both use it. `ofxManifoldRenderer::drawEvaluation` and
  `drawWeights` take an optional `NodeLabel` (node names without one, so
  every other example is unchanged) and put the number first --
  ` 0.250  FL-2`, a space for a sign -- so the column stays put while the
  names change length.
- **The editor follows its listening point**: `levelReached(model, floor,
  z)` -- the level it is on or above, by the surfaces under it, held to the
  bottom and top. Gliding in audition, that level becomes current and stays,
  as example-osc already followed its point.
- **Found doing it**: `Levels::positionAt`, restrained, answered the top
  where the top touches the level under it -- its "at or above the top"
  shortcut ran before 3B-2d's lowest-at-a-pinch rule. The shortcut is now
  "above the top"; on the top surface itself the walk decides. A stack,
  `pinch_top`, three positions on it, and a gate.
- **? three ways, both apps**: the panel; every key over the window; none --
  no panel, no status, no levels keys, a "?" in the corner and any message
  along the bottom of the map. On the page, ? goes on to none; any other key
  or a click goes back. The editor's bare layout is `layoutEditorBare()`,
  checked by `make panel` at every window and text size: the views never
  smaller than with the panel, inside the window, apart, above the faders,
  which run across. In the drawlog's window the editor's views go from 177
  to 448 pixels; example-osc's map and 3D view grow likewise.
- **Vectors**: levels 4, editor 2 (`labels_by_level`, `level_reached`):
  1083. **Gates**: 4 new, 1 re-anchored: 507.

### 3B-2f: points moved from outside

Built 2026-11-17 as specified, every recommendation taken ("yes,
recommended"), with the control file as agreed; adding points with a key
split off to 3B-2g first, so that what a second point sends and sounds like
is decided rather than improvised -- said to Lee before building.

- **`ofxManifoldPlayer/src/ofxManifoldPointControl.h`**, header-only, no
  sockets: `PointControl::receive(now, message, from)` returns false for a
  message not under `<prefix>/point/`, else handles it; `takeMoves`,
  `takeEvents`, `takeReplies` give what to apply and send; `tick(now)` lets a
  silent hold go; `handTook(n)` gives a point back to the hand. Counts by
  reason and the last refusal. Loopback is checked before the point; a
  command answers when its request id can be read, a move never; an unknown
  point is counted, not answered -- there is no point to answer as.
- **`ofxManifoldControlFile.h`**: `saveControl`, `loadControl(text, spec,
  points)` -- strict, refusing with a reason and changing nothing; unknown
  keys refused, at file and point level, as motion files are.
- **The player's description**: protocol 4, the roll call's fifth argument
  the point count, `<prefix>/points` after the shape, again when it changes.
- **example-osc**: messages on the answer port go to `PointControl` first,
  the rest to `OscOut` as before; taking stops the point's own movement and
  keeps it to give back; a click on the map is the hand taking it back; the
  panel says who has point 1 and what came in; the `?` page lists the
  addresses and the port; `C` writes `<map>-control.json` beside an opened
  map, and opening a map reads it -- refused, the defaults and why. The
  bridge port is `kBridgePort` in `ofApp.h`, its "taken" line names it.
- **Checked**: a new suite, `control`, 101 vectors from
  `tests/ref/reference_control.py` -- holders kept in a dictionary, repeats
  found in a log of every answer, addresses split by a regular expression,
  the file read by Python's json -- every rule, every refusal, the file read,
  refused and written back exactly. `make osc-live` sends from two real UDP
  sockets with python-osc to `tests/control_udp.cpp` and decodes the
  answers that come back. The drawlog session takes point 1 from outside,
  moves it, is refused off the plan and as a second sender, and the hand
  takes it back -- printing what the app sent: the take answered to port
  9002, `held by 127.0.0.1:57120` to the second sender, `lost hand`. The
  ofxOsc stand-in now carries messages both ways for this.
- **Vectors** 1187 (osc 3 more, control 101), sixteen suites. **Gates** 33
  new, two re-anchored: 540; every gate on the player's OSC files run again.
  One got through the first time -- "a move with too many numbers is taken":
  no vector sent one; now one does.

### 3B-2g: more than one point; sections with headers

Built 2026-11-18 as specified: 6, 7 and 8 yes, 1 to 5 as recommended, the
split between each point's keys and every point's as agreed with Lee.

- **Sections** (`ofxManifoldEditorKeys.h`): `KeyEntry::state` (a state line:
  what is true now, its key the one that changes it), `KeyGroup::colour`
  (`KeyColour`, moved from the key panel's view into the data:
  `editorGroupColour`), `keyRows(groups, state)`, `keyGroupGap(lh)` -- three
  quarters of a line above every header, where there was half. The note of
  what did not fit now takes a header's place rather than leave it alone at
  a column's foot -- `make panel` found that at small sizes, in the editor
  as well, once it was asked. `drawKeyGroups(text, groups, flow, more)` in
  the key panel's view draws a flowed list for anyone: each header bolder,
  in its colour, with a thin rule in that colour across the column; a state
  line's words in white. The editor's panel, its levels keys under the 3D
  view and its `?` page draw through it; their groups and colours are as
  they were.
- **example-osc's panel and `?` page** are one list, `sections()`: sending,
  points, the point, movement, smoothing, from outside, showing, maps --
  each its state lines with their keys, and its keys. The panel flows the
  state lines (a second column where the window leaves room), the last send
  in what room is left, the message along the panel's foot; the `?` page
  every key, the from-outside addresses among them. The panel's text is the
  editor's `Text`, as the faders' is, no longer the bitmap font. At the
  stand-in's 1024 x 640 the panel says "+ 3 lines more -- a larger window
  shows them".
- **Points in the player**: `PlayerPoint` in `ofApp.h` -- place, height,
  movement, walks, recording, seed, travel, steps, outside hold, its own
  evaluator, smoothers and what it is heard as. `a` adds at the mouse (the
  middle otherwise) at the chosen point's level position; `A` removes the
  last (a hold on it ends, "removed"); left / right or a press within 12
  pixels of a dot chooses -- only with two or more, so one point is pressed
  and dragged as before; a drag from a moving point's dot takes it by hand.
  The clock, `c`, `, .`, `return`, `b` and `k` are every point's; the rest
  the chosen one's. One recording at a time; choosing another point while
  recording is refused, "r to stop first". Walks are seeded with the
  point's number, point 1's draws unchanged. The faders, the followed node
  and the level shown are the chosen point's. The map draws every point in
  its colour (point 1 green, as the 3D view's ball), numbered, the chosen
  one ringed; the 3D view the others as balls in their colours
  (`Scene::points`, `PointMark`).
- **The control file decides how many**: opening a map whose file has
  three points gives three, the second and third on a small circle round
  the middle; `C` writes as many as there are. `PointControl::addPoint` and
  `removeLast`, every other hold kept. `loadControl` is now asked for 16.
- **Sent** (`OscFrame::more`, `OscPoint`): points 2 and up after point 1's
  position, in number order, `<prefix>/point/<n>/levels` (packed always)
  and `/position` (with u). A number sent before and missing now is sent
  once with every level 0, before the `points` list without it; kept across
  `reset()` (the same map opened again), forgotten with a new prefix.
  Protocol 5. Checked end to end once, outside the suites: the app in the
  stand-in with time running, one point, `a`, `A` -- the messages on the
  wire as the vectors say.
- **The patch**: `\points` (`\together`, `\carrier`), `\players`,
  `\carrier` (`\fm`, `\ring`), `\depth` (3: the modulation at a shaping
  point's full level, held there), `\maxPoints`. `~manifold[\mix]` -- a
  plain function, beside the musical ones -- gives every voice amp,
  brightness (the loudest playing point's there, the lowest number on a
  tie), FM index, ring mix and ratio (the level the loudest shaping point
  there is on). The tone gains a modulator at `ratio` times the note, `fm`
  and `ring` 0 by default. A map keeps every point heard; a frame's burst of
  messages sets the voices once, 4 ms after its first; `/points` lets a
  removed point go. With one point every setting sounds as before.
- **Checked**: osc 6 vectors more (52; the zeroing across a `RESET` among
  them, after a gate showed nothing tried it), control 37 more (138), 1230
  in all; music 20 more (`MIX`: one, two, three and sixteen points, ties,
  no u, below the floor, a silent shaper, short lists -- the reference
  ranks by sorting, the patch by a running maximum): `make sc` 68,
  `make sc-live` 10 -- the second point together, shaping, removed;
  `make osc-live` sends three points over UDP, the third removed: 94
  packets, each the reference's. Eight
  breaks of `mix` by hand, each caught (not gates: CI has no sclang); one
  more -- the floor under u -- caught only after a vector below the floor
  was added. `make panel`: keyRows, the colours, and a player's sections in
  three strip widths at every size, headers never alone and never without
  room above. **Gates** 17 new, one re-anchored: 557; every gate on the
  player's OSC and control files and the editor's keys run again.
- **Drawlog**: example-osc's session adds two points at the mouse, sets
  the third gliding, chooses point 1 and drags it while the third glides,
  takes the third from outside, removes it -- its sender told `lost
  removed` -- and chooses point 2.

---

### 3B-2h: the patch's choices, a block at a time

Built 2026-11-19 as specified.

- **`example-osc/supercollider/manifold-modes.scd`**: twenty blocks. START
  loads `manifold-receive.scd` from beside it (`thisProcess.nowExecutingPath`;
  an unsaved file is told to save first) unless the patch says it is
  playing; STOP runs cmd-period; RESET chooses the patch's own settings.
  NOTES: progression, somber, registers close, wide, an octave each,
  series. SHARED OUTPUTS: soft, faint, full. SOUND: tones, tones even,
  noise bands. POINTS: together, carrier gentle (FM, depth 1), deep (depth
  6), ring, two play and the rest shape. Every choice says what it is, what
  to try in example-osc and what to expect; one run before START warns
  ("run START first") and does nothing.
- **The patch**: `\defaults` (the settings as this run starts), `\summary`
  (a line: notes, sound, points), `\choose` (a setting it does not know is
  warned of and left alone), `\stopped` and `\playing` (set by
  `CmdPeriod.doOnce`). Nothing it played before changes.
- **Checked**: `tests/sc/check_modes_live.scd`, in `make sc-live` -- the
  file read into its blocks as someone selects them, each run from the
  file's own place: a choice before START warns; START starts it and a
  second leaves it be; each choice calls `\choose` once, sets exactly its
  group, every one as the test's own table of what each choice is said to
  be, leaves the other groups alone, builds the voices again only for
  another sound, and the server's voices then play what the patch's
  voicing says; RESET puts back what it started with; STOP stops it -- 26
  checks. Broken by hand, each caught: a block missing a setting of its
  group, a block reaching into another group, a block that plays another
  choice than it says (caught only once the table was added), another
  depth than it says, `\choose` never building, never retuning, always
  building, `\defaults` missing a setting, `\playing` always or never,
  cmd-period not heard.

### 3B-2i: following, and keeping apart

Built 2026-11-19 as specified.

- **`ofxManifoldPlayer/src/ofxManifoldFollow.h`**, header-only, no
  openFrameworks: `Place` (x, y, level position u), `along`,
  `keepApart(from, to, others, gap)` -- for each other point the open
  stretch of the way where an end would be too close, on the map (a
  quadratic) and on the level (within `kLevelApart`, 1), both at once; the
  answer the largest t in [0, 1] in none of them, found stepping back from
  the far end -- `glideAt` (smooth, as the walks' glides) and `FollowChain`:
  `step(now, follows, gap, inside)` notes every place and returns each
  follower's move to where the point before it was at the last step, kept
  apart from every other point where it is now; one added since waits; one
  stopping short off the map waits instead. Included by
  `ofxManifoldPlayer.h`.
- **One reading of the spec made plain**: "already within the gap, it
  waits" is when nowhere along its way is clear. A point may always leave a
  crowd for an end that is clear -- or one added on top of another would
  never move. And a step all the way to where the leader was is not checked
  against the map: the leader was there. The way between may cross a bay
  of the map's outline; only stopping short is held on it.
- **example-osc**: `Moving::Follow` -- `m` after glide, for points 2 and
  up; a point added with `a`, or by a control file, starts following. Each
  `PlayerPoint` keeps the glide of its last step; its height is the glide's
  level position (the arrows and `h` wait while it follows). The chain steps
  with every point's steps: a click, `return`, a beat, or on the timer once
  each interval of the count -- noted every step, follower or not, so a
  point turned to follow goes at the next. A glide takes the point's travel,
  on the timer no longer than the step. `g` steps the gap (0.04, 0.08, 0.15,
  off; every point's); the movement section says it while any point
  follows. A new map forgets what was noted.
- **Checked**: `make test-follow`, 62 vectors from
  `tests/ref/reference_follow.py` (the stretches from the closest approach
  and half the chord; the farthest end the largest candidate no stretch
  covers; the map a polygon by counting crossings) -- seventeen suites,
  1292 vectors. **Gates** 16 new: 573; every one caught -- one, "stepping
  back stops after one look", only once vectors listed the points in an
  order that needs a second look. Once outside the suites: example-osc in
  the stand-in with time running, three points on clicks behind point 1
  dragged a step at a time -- each a step behind; point 1 still, the others
  closing to 0.08 and waiting; the gap off, all on one spot; then on the
  timer. The drawlog session does the same on screen.

### 3B-3a: the stage, and the native room

Built 2026-11-20 as planned.

- **The stage**: every voice now plays into an audio bus of its own;
  a speaker per output reads it, in a group after the voices; the room's
  reverb (core `FreeVerb2`, one for every map) after the speakers, fed by
  a stereo bus. A map's buses, voices and speakers go together when it
  goes or is built again. Choosing another room builds the speakers again,
  never the voices; turning the listener sets the speakers; the reverb's
  amount is the room synth's.
- **`\stereo`** (`\manifoldStereo`): `Pan2` by the voicing's pan, as
  before. **`\head`** (`\manifoldHead`, the default): air (`LPF`), the
  outer ear's notch (`BPeakEQ`, -12 dB), each ear's delay (`DelayC`) and
  shelf (`BHiShelf` at 2.5 kHz) and gain, a send to the reverb -- every
  number from `~manifold[\hear]`, gliding over 50 ms. Core UGens only.
- **The numbers** (`// ---- room`, plain functions): `\speakerAt` -- a
  speaker in metres from the listener: x across the width, y the depth
  (the map's top the front), its height from the lowest level (the floor)
  to the highest (the ceiling), the ears halfway; one level, all at the
  ears; no place, inside the head. `\hear` -- direction and distance
  (held 0.3 m off), the side the sound is on (lateral angle), the far
  ear's delay (Woodworth, a head 17.5 cm across), the far ear 6 dB quieter
  and 12 dB duller at the side, 5 dB duller for a sound behind, the notch
  from about 3.6 kHz below to 10 kHz above, distance as level (1 within
  2 m, then falling as 1/r) and air (a low-pass falling 7% a metre),
  the reverb's share rising with distance.
- **Settings at the patch's top**: `\room` (`\head`), `\facing` (0),
  `\reverb` (0.3), `\roomSize` ([8, 8, 4]). The first three are in
  `\defaults` and in the modes file's new ROOM group: head, head dry, a
  big room, facing right, turned round, stereo. `\summary` says the room.
- **Checked**: `make sc` 103 -- the room's 35 vectors from
  `tests/ref/reference_room.py` (the listener as two unit vectors, the
  speaker's direction as one, the side and "behind" from its components
  along them; angles compared as directions). `make sc-live` 59 -- the
  patch's 10, the modes file's 32 (ROOM among them, every choice's
  speakers the room's), and `check_room_live.scd`'s 17: a click played
  into one voice at a time, the headphones recorded and measured --
  left first by the head's delay (to 3 samples) and loudest on the left,
  right the mirror, ahead even, behind duller than ahead, near louder than
  far, overhead even, no place in the middle, dry with no tail, turned
  round left is right, turned right ahead is on the left, the reverb's
  tail, stereo panned with no delay. The first run of it measured the
  right ear wrong -- every other sample, not every second (`data[1, 2..]`)
  -- and passed two checks it should not have; found when the left side
  failed. Broken by hand, each caught: the ears' delays swapped, a shelf
  gone, no reverb send, the turn the wrong way (by vectors; live only once
  the quarter turn was added -- round is the same either way), no
  distance, nothing behind, the stereo pan in the head's room.
- **The patch's other checks**: `check_patch_live.scd` reads the pans from
  the speakers now, in the stereo room.

### 3B-3b: the ATK's room

Built 2026-11-20 as planned.

- **`\atk`**: when the ATK is there -- its classes, and its kernels
  (`~manifold[\atkThere]`) -- the patch loads the decoder's ears once at
  boot (`FoaDecoderKernel.newCIPIC`, subject `~manifold[\ears]`, 21: the
  KEMAR dummy head), makes a four-channel first-order bus every speaker
  encodes into (`\manifoldAtk`: the head's distance -- level, air, the
  reverb's share -- then `FoaPanB` at the speaker's direction, the ATK's
  azimuth turning left where ours turns right) and one decoder to the
  headphones after the speakers (`\manifoldAtkDecode`). An output with no
  place goes to both ears alike, the middle of the head. What does not
  pass through the ears -- the middle of the head, the reverb's share --
  waits as long as the decoder's convolution does (a kernel: 512 frames at
  48 kHz), so it arrives with the room; found by measuring, the first
  version had it 15 ms early. Choosing another room builds the speakers
  again, as before; the ears stay loaded.
- **The patch runs without it**: the ATK's classes are named by symbol
  (`\FoaPanB.asClass`), so the file compiles where it is not installed;
  `\atk` asked for then -- at the patch's top, or chosen -- plays the head
  and says, once each time, how to install it. `~manifold[\atkTrim]` (1):
  measured, the ATK's level against the head's is already about even.
- **The modes file**: ATK, MEASURED EARS and ATK, DRY in ROOM; at its end,
  INSTALLING THE ATK -- the quark (needs git; restart after), then the ears
  and matrices downloaded once; and where sc3-plugins come from.
- **`~manifold[\hear]`** gains `gain` -- the distance's level alone, what
  the ATK's room uses; the room's vectors carry it.
- **Checked**: here with the ATK installed from its GitHub repositories
  (two of its dependencies, SphericalDesign and PointView, are on GitLab
  for `Quarks.install` and the sandbox cannot reach it; their GitHub copies
  served; MatrixArray, for higher orders, was not needed). `make sc-live`
  73: `check_room_live.scd` 28 -- the ATK's room measured from the
  headphones as the head's is: left on the left and right on the right by
  more than 3 to 1, ahead even within a quarter, behind unlike ahead,
  near louder than far, overhead even, the middle of the head in both ears
  alike and arriving with the room, as loud as the head within half again,
  turned round left is right; and without the ATK, `\atk` playing the
  head. The modes file's 35, ATK's blocks among them, with the ATK and with
  its folder taken away; the patch's 10 both ways. Broken by hand, each
  caught: the azimuth not turned, nothing waiting for the ears, no
  fallback, the decoder a third as loud.

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
| **3A-9** | Editor: curves on every level, layout size and preview, growing triangles | editor scripts, drawlog session extended |
| **3A-10** | Editor: c and C; the key panel flows into columns and fits | editor scripts, `make panel`, drawlog session extended |
| **3A-11** | Editor: keys tidied; L and F; a grid that fits; the lines highlighted | editor scripts, drawlog session extended |
| **3A-12** | Editor: softer hints, a grid without gaps, a panel of what can be done | `make panel`, drawlog session |
| **3B-1** | OSC out that grows and shrinks with the map; `ofxManifoldPlayer`; `example-osc` | osc vectors, `make osc-live` |
| **3B-2** | Movement, first slice: `Motion`, the recording as one, `RandomWalk` (jump and glide), bounds, seeds, step timing (every, clicks, pattern), clocks (seconds, a phase from SuperCollider), motion files; example-osc's movements | motion vectors, gates, drawlog session, `make osc-live` |
| **3B-2b** | The map's shape for SuperCollider: `/shape` (levels, each output's level and place), `Model::outputPlaces`, protocol 3; the patch's musical behaviours, chosen in SuperCollider; each node's name once on the map, in its own output's colour on the current level | editor and OSC vectors, gates, sclang logic run |
| **3B-2c** | Height on the levels: `Levels::heightAtPosition` / `positionAt`, riding the bends, restrained or extrapolated; example-osc's `k`; u on `/position` | levels vectors, OSC vectors, gates, drawlog session |
| **3B-2d** | Levels that touch: `fits` at-or-beyond, a pinch shared at t = 0.5, `evaluateAtPosition`, the editor allowing touches, files versioned when they touch; the octahedron example | levels and editor vectors, gates, drawlog session |
| **3B-2e** | Names by level (`Model::levelLabel`), the readout's numbers first; the editor follows its listening point (`levelReached`); `?` three ways, the views larger with no keys (`layoutEditorBare`) | editor and levels vectors, `make panel`, gates, drawlog session |
| **3B-2f** | Points moved from outside: `PointControl`, take and release answered, moves from the holder, the same bounds as inside, loopback, counted refusals; protocol 4; the bridge port a setting | control vectors, gates, `make osc-live`, drawlog session |
| **3B-2g** | More than one point: `a` / `A`, choosing, each point its own movement; points 2 and up sent under `/point/<n>/`, a removed one zeroed; protocol 5; the control file's count; the patch's `\together` and `\carrier` (FM, ring). Sections with headers in both apps: `KeyEntry::state`, `KeyGroup::colour`, `keyRows`, `drawKeyGroups` | osc and control vectors, music vectors in sclang, `make sc-live`, `make panel`, gates, drawlog session |
| **3B-2h** | The patch's choices a block at a time: `manifold-modes.scd` -- START, STOP, RESET, and NOTES, SHARED OUTPUTS, SOUND, POINTS, each explained; `~manifold[\choose]` in the patch | every block run against the patch on a server (`make sc-live`), choices broken by hand |
| **3B-2i** | Following, and keeping apart: `ofxManifoldFollow.h` (`keepApart`, `FollowChain`, `glideAt`); `m`'s follow, a point added following, `g` the gap | follow vectors, gates, drawlog session |
| **3B-3a** | The stage -- a bus per voice, a speaker per output, the room's reverb -- and the native room: `\head` (a model of a head, core UGens only) and `\stereo`; `\facing`, `\reverb`, `\roomSize`; ROOM in the modes file | room vectors in sclang (`make sc`), clicks measured from the headphone channels (`make sc-live`), choices broken by hand |
| **3B-3b** | The ATK's room: `\atk` -- first order, CIPIC ears, the head's distance and reverb, the middle of the head kept in time; runs without the ATK; install blocks in the modes file | clicks measured from the headphones with the ATK, and asked for without it (`make sc-live`), choices broken by hand |

A slice that adds a `.cpp` file says so, and a Project Generator Update comes
before the build.
