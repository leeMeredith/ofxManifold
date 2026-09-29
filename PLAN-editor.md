# Editor — plan

Splitting the editor out of one `ofApp.cpp` into bounded, reusable modules, then
adding the requested features.

Kept separate from `ROADMAP.md` while in progress; folds into `DECISIONS.md` when
done, and this file is deleted.

---

## Why split first

To check the delete-a-node-and-its-fader rule, the editor's logic had to be
**re-written by hand** in a separate test, because the real logic lived inside
`ofApp.cpp` where nothing can test it. A copy agreeing with itself proves little.
Most of the editor does not need openFrameworks at all, and whatever does not
can be tested the way the kernel is.

---

## Decisions

### A · A package outside the addon, and an addon itself

The editor is a tool built on the manifold, with opinions about keys, layout
and workflow that are still changing. Everything in ofxManifold's `src/` is
compiled into every project that uses it, so the editor stays out of it.

It still has to be an **addon**, because Project Generator compiles only a
project's own `src/` and its addons. A plain folder would never reach
`example-editor`, and relative include paths break the moment an example is
copied into `apps/`. So: **`ofxManifoldEditor/`**, its own `addon_config.mk`,
depending on ofxManifold.

It lives in the same repository for now, sharing the tests and CI, and is
installed beside ofxManifold in `addons/`. Moving it to its own repository later
is a `git mv`, not a restructure.

### B · The boundary rule

Anything that is a property of **a map or its outputs** belongs in the kernel,
whoever needs it first — `clearBindings()`, `removeOutput()` and the coming fill
setter went there, with vectors, because a show-control app with no editor needs
them too. Anything about **editing interaction** belongs in the editor.

### C · Modules

**Logic** — no openFrameworks, glm and the kernel only, tested with vectors,
each a `.h` and a `.cpp`:

| module | responsibility |
|---|---|
| `EditorModel` | map and outputs together, and every editing operation; each returns a result and a message, as data |
| `Selection` | selected nodes, whether their order is meaningful, box select |
| `ChartLayout` | fader positions and widths, labels that fit, slivers for unused channels |
| `EditorHistory` | undo and redo over snapshots (round 2) |
| `KeyMap` | every binding, its group, and when it is available (round 2) |

**Views** — openFrameworks drawing, judged on screen: `NodeGlyph`, `MapView`,
`OutputChart`, `KeyPanel`, `EditorFiles`.

**The app** — a thin `ofApp`: events into model operations, views asked to draw.

### D · Rules that keep it apart

- **Dependencies point one way:** kernel ← mapping ← editor logic ← views ← app.
- **Views only read.** They never change the model.
- **Every change goes through one door** — an `EditorModel` operation. Undo
  snapshots at that door, so nothing can slip past it.

---

## Rounds

### Round 1 — the split, NO change in behaviour — DONE

1. **Slice 1a — the logic. DONE.** `ofxManifoldEditor/`, an addon of its
   own. `Model` and `Selection`, each a `.h` and `.cpp`, no openFrameworks,
   every header compiling alone. 11 scripted scenarios, 80 steps, each with
   its result, message and complete state checked, against a Python reference
   that tracks everything by name and renumbers nothing. 11 of 11 on first
   run; mutation testing then found two gaps, both the one-step-two-checks
   pattern (a box join that hit the duplicate check first; no removal before
   the current output), closed with a script each; 11 mutations caught, 7 as
   gates. Found along the way: `make headers` never compiled `sources/` or
   `authoring/` standalone -- they were self-contained, the check just did not
   look; now it does.

   Until slice 1b the logic exists twice: in the package, and still inside
   `example-editor`. 1b removes the second copy.

   Originally: `EditorModel` and `Selection` extracted as code
   moved, not rewritten; scripted vectors pinning what the editor does TODAY,
   including behaviour round 2 will change on purpose (a quarter-share of
   silence per `q`; a placed node becoming selected). When round 2 changes one,
   its vector changes in the same commit, visibly.
2. **Slice 1b — the app. DONE.** `example-editor` hands every edit to
   `editor::Model`: 1,260 lines to 811, its own copy of the logic gone.
   `ChartLayout` in the package, 12 scripts now, 113 steps, fader geometry and
   clicks included. Checked statically: 36 keys handled before and after, all
   43 messages still exist. The view code sits in marked sections of `ofApp`,
   ready to lift into files.

   **The door is enforced by the compiler.** The app holds the map and outputs
   only as const references, and the model's state is private behind const
   accessors. The first version left the model's members public, and
   `model.manifold.addNode()` compiled -- found by trying it rather than
   assuming the const references were enough. `make door` now tries eight ways
   in, each of which must fail to compile, plus a control that must compile.
   Its first version, a Makefile rule, failed every snippet through a quoting
   mistake: the check would have passed for the wrong reason, which is exactly
   what the control is there to catch.

   Found by mutation testing: a pick-output step that picked the output
   already current, so it could not fail. Two further misses were EQUIVALENT
   mutants -- a derived bar carries no output id, and the kernel refuses two
   outputs on one channel -- recorded as such rather than "fixed".

   The chart vectors pin one current defect deliberately: with thirty outputs
   the running total is drawn at x = 1016 in a 1024-wide window, off screen.
   Round 2's fader narrowing fixes it and changes that vector.

   Originally: `example-editor` rewired onto the package; views
   grouped into marked sections of `ofApp`, lifted into files next round;
   `ChartLayout` extracted with its drawing.

### Round 2 — the features

**Fill keys, amended.** Shift was to mean both "up" (`Q`) and, as requested
later, "finer" -- but `Q` IS shift held with `q`. So direction gets its own
keys and shift means one thing everywhere: `q` down and `w` up by 5%, shifted
by 1%. Arrows stay free, since in most editors they nudge the selection.
Coarse steps land on multiples of 5 in their direction (73% then `q` gives
70%, not 68%); the floor is 1%, one fine step above silent -- fully silent is
`c`, which is what a null node is.

**Slices:**

| slice | what |
|---|---|
| 2a | fill steps, ⇧B, undo and redo, placement leaves nothing selected, a click on empty space clears a selection — **DONE** |
| 2b | named saves as a pair through the Mac dialogs |
| 2c | window at 90% of the screen, full screen, fader narrowing, scaled text |
| 2d | keys grouped and lit when usable; view sections lifted into files |

**Slice 2a, done.** Kernel: `Mapping::setOutputFraction()` sets a node's fill
exactly by changing only its silence share, so the balance between its outputs
is kept. Package: `Model::stepFill()` and `bindAll()` replace the quarter-share
`silence()`, and the vectors that pinned it changed in the same commit, as
round 1 promised. `History`, snapshots of the model, with `apply()` recording
at the one door. 17 scripts, 192 steps, all passing on the first run; 11 of 11
mutations caught; the app wired through one `act()` helper.

Found along the way:

- **Float noise skipped round numbers.** 70% reads back as 69.99999%, so a
  plain "floor to the next 5%" step went to 65%. Values within 1e-3 of a step
  are treated as on it. Caught by mutation, both ways.
- **A full fill left a zero-weight silence binding.** It behaves identically
  but forced the mapping file to version 2, since any silence binding needs it.
  The fill vectors checked behaviour and not bindings; they now count bindings.
- **⇧B had to REPLACE bindings.** Binding adds weight, so binding a node
  already feeding one output to every output would feed that one double.
- **Startup would have been undoable.** Reopening the last save records an
  "open", so command-Z straight after launch would swap the saved map for the
  example. History is cleared once the app is ready.

Every model call in the app not wrapped in `act()` was listed and accounted
for: the drag (one step, recorded at release), opening a file (recorded just
before), and three that change no map data.

Originally listed:

Placement leaves nothing selected; a click on empty space with a selection
clears it; undo and redo; `q`/`Q` in 5% fill steps for every shape; ⇧B binds to
every output; named saves as a pair through the Mac dialogs; faders narrowing
with adaptive labels, hover, slivers; window at 90% of the screen and ⌃⌘F full
screen; keys grouped by intention and lit when usable; text scaled with the
window.
