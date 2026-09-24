# Outputs — plan

Outputs with a channel and trim, bindings that can send a share to silence,
derived outputs, and the editor support to see and hear them.

Kept separate from `ROADMAP.md` while the work is in progress. It folds into
`DECISIONS.md` when done, and this file is deleted, as the regions and
authoring plans were.

---

## Scope

**Outputs alone.** Layers and vias are designed for here and built later.

Outputs, weighted silence and vias all answer one question — where does a
node's share go? — so the destination of a binding records its **kind** from
the start: `output` or `silence` now, with room for `layer`. Nothing about
layers is built. The point is that adding them later does not change the
binding structure or the file format under maps already saved in it.

---

## What exists today

Read from the code before this plan was written, not recalled:

- **An output is a name and nothing else.** No channel, no trim.
- **Link weights normalize within a node**, so a node always passes on its
  entire share. There is no way to send part of it to silence.
- **Aggregators sum node weights with no per-source weight**, and their values
  come back in a separate list — so a derived output has no channel, and does
  not appear in the dense vector an OSC receiver reads.
- **Bindings are keyed by NodeID in memory.** Removal renumbers nodes (D-020).
  The moment the editor holds a mapping, deleting a node would silently re-route
  every binding after it to the wrong node.
- **The mapping file refers to nodes by name**, so saved files already survive
  renumbering. The problem is in memory only.

---

## Decisions settled before writing code

### A · An output has a name, a channel and a trim

Names are unique — `addTarget()` already returns the existing id for a repeated
name. **Channels are unique too**, refused at creation: two outputs on one
channel would make the dense vector ambiguous, and an ambiguity the kernel
accepts and a consumer trips over is the D-018/019/020 pattern again.

Channels need not be contiguous. **The channel number is the index, exactly:**
`dense[channel]`, length highest channel + 1, zeros in the gaps -- what MIAP
does. So channels 1, 2, 5 give six entries, index 0 an unused zero. Nothing is
interpreted; whether the editor DISPLAYS from 0 or 1 stays an open question.

An output made the old way, `addTarget(name)` with no channel, gets **channel =
its id**. So for every existing mapping the new by-channel vector is identical
to today's `toDenseVector()`, which is what lets backward compatibility be a
vector rather than a hope. (Settled while writing the reference.)

### B · A binding's destination has a kind

`output` or `silence`, with `layer` reserved.

A silence binding takes part in the within-node normalization and contributes
nowhere. A node bound to `out.1` at 0.7 and to silence at 0.3 sends 70% of its
share to `out.1` and discards the rest. This is MIAP's virtual-to-silent link,
the partial fade ofxManifold could not express.

**A node with no silence binding must resolve exactly as it does today.** The
35 existing mapping vectors are the proof, unchanged and green.

### C · Derived outputs are aggregators with weights, a channel and a trim

Each source carries a weight, as MIAP's derived links do — a sub fed three
parts front to one part rear. A derived output has a channel and a trim, and
**appears in the dense vector at its channel**, alongside ordinary outputs.

Derived outputs are still not nodes. They have no position and take no part in
the geometry, for the reason the architecture gave: a non-participating object
inside the geometry forces every region operation to filter it out.

### D · Trim is not bias, and they are applied at different stages

- **Bias** (the node `weight`) changes a node's share of the geometry, BEFORE
  renormalization. It is geometry.
- **Trim** is a gain on an output, AFTER routing. It is level.

So there are two readings of the outputs:

- **routed share** — before trim. Outputs plus silence sum to exactly 1.
- **level** — after trim. What actually leaves.

`toDenseVector()` keeps meaning routed share, unchanged; a new `toDenseLevels()`
applies trim. Keeping them apart keeps the invariant every other layer relies
on intact up to the last possible moment.

### E · The mapping follows node renumbering

`Mapping::remapNodes(remap)` takes the table `removeNodes()` returns. Bindings of
removed nodes are dropped; every other binding moves to its node's new id;
aggregator sources the same.

This is required, not optional: D-020 said anything holding NodeIDs across a
removal must pass them through the remap, and the mapping is the first thing
that does.

### F · A node's type is a label computed from its bindings

Null, terminal, composite, or partly silent — computed on demand, never stored.
A stored type that could disagree with the actual bindings would be the
D-018/019/020 pattern a fourth time.

### G · The file format changes only when it has to

**Amended during step 3 (D-022).** The original rule wrote version 1 for any
mapping using no new feature. That preserved a bug: version 1 dropped unbound
outputs and could reorder outputs on reload. Version 1 is now written only when
a version 1 reload reproduces the mapping exactly -- no new feature, every
output bound, outputs first mentioned in the order made.

As with regions in D-017:

- A mapping that is safe as version 1 is written as **version 1,
  byte-identical** to today. Every file an older reader could read correctly,
  it still can.
- A mapping using channels, trims, silence or weighted derived sources is
  written as **version 2**, so an older reader refuses it cleanly — "unsupported
  version 2" — rather than loading it and silently dropping the parts it did not
  understand.
- A binding's kind is written only when it is not `output`, which is what keeps
  version 1 files unchanged.

### H · Node display

One property per visual channel, so they never compete:

| channel | means |
|---|---|
| shape | what the node does with its share |
| fill | how much of that share reaches an output |
| size | bias weight (already in place) |
| colour | reserved for layers |
| outer ring | selected |

| node | shape |
|---|---|
| terminal | filled circle — the most common node, the simplest shape |
| silent | hollow square |
| composite | filled triangle |
| partly silent | its shape, filled in proportion to what reaches an output |
| via (later) | a ring, as PCB tools draw a via |

Why only three distinct shapes: a regular polygon's flat edges sit
`1 - cos(180 / sides)` of the radius inside its circle. On a 6-pixel node an
octagon's edges pull in 0.46 px — it reads as a circle. A square pulls in
1.8 px, a triangle 3 px.

Shape carries the meaning, not colour, so the display still reads on a
projector, a monochrome printout, or to anyone who does not separate colours
easily. Derived outputs get no shape on the map; they are not nodes, and appear
in the outputs panel.

### I · Audition is a separate mode

Dragging in the editor always edits. Audition is a mode in which dragging moves
the evaluation point and **can never move a node** — the reason it is separate.
Hold space for a temporary audition, release to return; a key to lock it on.

ofxManifold makes no sound, so auditioning here means **seeing** the outputs
change. **Hearing** them means sending levels to SuperCollider over OSC, which
belongs to the editor, not the addon, and is an optional step once the rest
works.

### J · A bar chart of outputs along the bottom

- **One bar per output, not per node**, in channel order, labelled by name.
  Outputs are what is heard; node weights are an intermediate step.
- **A bar for silence**, so outputs plus silence always total exactly 1.000.
- **Derived outputs drawn distinctly.**
- **Trim as a tick** on each bar: routed share and level, both visible.

---

## Work, in order

1. **Python reference — DONE.** `tests/ref/reference_outputs.py`, 42 vectors
   (36 ANALYTIC, 5 CROSS, 1 SPEC) in `tests/vectors/outputs.vec`, plus six
   fixtures. Resolution is computed per DESTINATION, and silence directly
   rather than as `1 - routed`, so agreement shows silence really is the
   complement of routing.

   Self-checked over 2,000 random weight vectors per mapping: routed plus
   silence equals the total to 2e-16; a legacy mapping's by-channel vector
   equals its old dense vector every time; trim never moves routed share.

   **Cross-checked against the existing, independently written
   `reference_mapping.py`** on 3,000 random mappings using no new feature:
   routing agrees every time and aggregators agree when source weights are 1,
   both to about 1e-16. Two independent implementations -- one pulling per
   output from a prebuilt index, one summing per destination -- reaching the
   same answer.

   One self-check is weaker than it looks: the remap check compares two Python
   versions built the same way. The C++ is what tests it (step 3). For the
   same reason, the C++ runner's LEGACY records should compare the new
   by-channel vector against the C++ `toDenseVector()` directly, not only
   against the reference's numbers.
2. **Vectors — DONE**, 49 in `tests/vectors/outputs.vec`.
   - **the existing 35 mapping vectors, untouched and still green** — the
     backward-compatibility claim, proven rather than asserted
   - silence: a partial fade resolves to exactly the stated fraction
   - silence alone: a node bound only to silence behaves as null
   - routed share plus silence sums to 1 at every probe point
   - trim changes levels and never routed share
   - derived output with weighted sources appears at its channel
   - channels with gaps give zeros in the gaps
   - duplicate channel refused
   - remap after removal: surviving bindings on the right nodes, removed
     nodes' bindings gone, aggregator sources remapped — checked by resolving
     against a mapping built fresh from the survivors, as D-020 did for the
     geometry
   - type label computed from bindings in every combination
   - file: version 1 byte-identical when no new feature is used; version 2
     otherwise; a version 2 file round-trips byte-stable
3. **C++ — DONE.** 46/46 on the first run. The 35 existing mapping vectors
   pass untouched, and the 55 serialization vectors -- byte-stable round trips
   included -- confirm version 1 files are unchanged. Found D-022 before
   changing a line: version 1 dropped unbound outputs and could swap output
   order on reload.
4. **Mutation gates — DONE**, 11 caught, 10 as CI gates. Three missed on the
   first pass: two because one fixture carried two faults and each masked the
   other, one because no vector had ever put an unbound node into a
   resolution. Split and added; all caught. Originally listed: silence ignored in normalization, silence share routed
   somewhere, trim applied to routed share, derived output missing from the
   dense vector, remap not applied, duplicate channel accepted, version 1 files
   no longer byte-identical
5. **Editor — NEXT.** Outputs panel, binding selected nodes, shapes, audition, bar
   chart, saving the mapping as the second file

---

## Deliberately not in scope

- **Building layers and vias.** Designed for by decision B; built later.
- **OSC inside the addon.** The editor may send it; the addon never does.
- **Undo/redo and create-ring/grid.** The two most valuable conveniences MIAP
  has that we lack, and the next rounds after this one.

---

## Open, to settle during the work

1. **Channel numbering shown to the user.** SuperCollider counts from 0; most
   hardware labels count from 1. The stored channel is a number either way;
   what the editor displays is a preference.
2. **Trim units.** Decibels read naturally for speakers; linear for
   parameters. Store one, display either.
3. **Setting binding weights in the editor.** MIAP uses a right-click menu
   with a text entry. Keys are quicker; a menu is more discoverable.
4. **Audition keys.** Space to hold is standard; which key locks it wants
   trying.
