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
| 2a+ | `b` toggles the binding to the current output; binding marks on faders and map — **DONE** |
| 2b | named saves as a pair through the Mac dialogs, and the fader fill line — **DONE** |
| 2b+ | binding fixes: placing a node leaves the current output alone; an unbind that silences a node says so — **DONE** |
| 2c | listening: permanent listening point and levels, halos always, a selected node's contribution in each fader, shift-click several faders, link lines between nodes — **DONE** |
| 2d | the screen: window at 90%, full screen, fader narrowing, labels that fit, hover, slivers, scaled text — **DONE** |
| 2e | keys grouped and lit when usable, `b` previewing its action; views lifted into files |

The slices from 2c on were reordered after using 2b, around three kinds of
request: binding, seeing what is heard, choosing outputs. Mute was considered
and left for the PLAYER example (round 3B), where performing happens.

Previously planned for 2c and 2d:
| 2c | window at 90% of the screen, full screen, fader narrowing, scaled text |
| 2d | keys grouped and lit when usable; view sections lifted into files |

**Slice 2b, done.** `ofxManifoldEditorFiles` -- a NEW source file, so
Project Generator must re-scan the addon. A save writes `name.json` and
`name-outputs.json`; choosing either, or the bare name, opens the pair, and an
earlier editor's `-mapping.json` still opens. Pure C++ file I/O, tested in a
scratch folder emptied at every run so a leftover file cannot pass a
missing-file step. Command-S, shift-command-S, command-O; plain `s` and `l`
retired. The title names the pair and marks unsaved changes; the last pair
reopens at launch, and the old `editor.json` still opens when there is none.

Asked for during the slice: a **fader line** at each selected node's fill, on
each output it feeds, so the line and the node's shape move together on q and
w. `fillMarks()` computes it, tested; the app only draws it.

21 scripts, 236 steps, first run. Mutation testing: 5 of 6 caught. The sixth
was EQUIVALENT: the kernel's mapping loader builds into a local and writes its
output only on success, so a refused file can never leave half-read outputs
behind. One mutation was caught by a CRASH rather than a clean failure -- a
silence binding reaching the fader line was looked up as an output name and
ran off the end -- so the runner now reports an id that names no output
instead of looking it up. Undefined behaviour that fails on one machine can
pass on another.

**Slice 2d+, after first use.** Three reports: the text far too big; the key
help pushed off the window as outputs were added; fader labels where some
numbers stayed and others vanished.

- **Text at a fixed size,** 12 by default, chosen with command-plus and
  command-minus and kept between launches. 2d sized it from the window's
  height, which was wrong in principle -- a bigger window is for more room,
  not bigger letters -- and doubly so because openFrameworks loads fonts at
  96 dots per inch, a third larger than the Mac's 72. Loaded at 72 now.
- **The panel in fixed zones:** seven status lines always in the same
  places, one message line, three lines for the output being acted on, then
  the keys. The full output list is gone -- it grew with every output and
  pushed the keys off the bottom. The faders show every output; hover gives
  any one's details. Over-long lines are shortened, never run off the panel.
- **Fader labels for the whole row at once:** names on every bar if every
  name fits, else channel numbers on every bar, else a ruler -- every 2nd,
  5th, 10th... channel, unused ones included so the count stays even. The
  bar-by-bar rule gave a patchwork: a short name like `n2` fitted where
  `out14` did not, and `9` where `10` did not.

Mutation testing found no row where every name fits but not every channel
number, which happens when a short name sits on a high channel (`n0` on
channel 100) -- reachable through editing only in hundreds of steps. A
`CHARTMAP` record lays out a chart for an outputs list given directly. One
equivalent mutant: a ruler step of 1 has exactly the room of the
every-number test before it, so it can never succeed there.

30 scripts. No new source file.

**Slice 2d, done.** The window opens at nine-tenths of the screen, centred;
control-command-F toggles full screen. Text is the system's monospaced font,
sized to about a fiftieth of the window's height (12 to 22 points) and reloaded
when that changes, with the bitmap font as a fallback; every line step and
column in the panel follows the font, and the help drops to one column when
two will not fit.

The fader layout, tested: unused channels are 4-pixel slivers; gaps are 8
until a bar would be narrower than 32, then a quarter of a bar; a label is the
name if it fits, else the channel number, else nothing; a column 16 characters
wide is kept at the right for the total, which with thirty outputs used to be
drawn past the window's edge; each bar's hit area takes half the gap either
side, the same for hover and click. Hover shows the full name, share, level
and trim. The chart vectors changed on purpose, as round 1 said they would.

Found along the way: the runner compared chart positions to within 0.001,
but the vectors carry six significant digits, so past 1000 pixels only two
decimals survive -- a tolerance bug in the runner, now relative. No vector
recorded where the bars' baseline sits, so a font-size change to the label
rows could not fail anything; it is recorded now. The stand-in for
openFrameworks returned float from ofGetWidth and ofGetHeight, where the real
ones return int: corrected, as a stub must never be more permissive than the
library. One equivalent mutant: a derived bar carries no output id.

DEFERRED by the user: halos in the node's own shape -- a triangle node's halo
a triangle -- each shape scaled so equal share gives equal area.

28 scripts, 313 steps. No new source file.

**Slice 2c+, after first use.** Three asks: colour, curves, and percentages
that overlapped.

- **Colour per output**, by channel so it survives removals, from a
  colour-blind-safe set minus the yellow (selected) and purple (derived)
  already in use. A line, its socket, its output's fader and the name of the
  node that output belongs to share one colour. An added cue only: lines,
  plugs and labels carry the meaning without it.
- **Lines are arcs**, decided in the package and tested. A mutual pair arcs
  to opposite sides; otherwise the side with more CLEARANCE from other nodes.
  The first rule counted nodes near each side, and mutation testing showed
  the count was never reached -- in the fixture both sides counted zero and a
  tie-break decided. Building a fixture where counting and clearance DISAGREE
  showed counting was the wrong rule: it sent an arc within 0.01 of one node
  to avoid two at 0.028. What makes a line look connected is the nearest node,
  so clearance alone is the rule now. Fixing the test changed the design --
  the fifth appearance of one step reaching a different check than intended.
- **Percentages a quarter of the way from the sender**, where lines leaving a
  node fan apart, instead of at the middle where they crossed.

`linkPoint()` gives any point along a link from the sender; 27 scripts, 302
steps. The test stand-in for openFrameworks lacked `ofColor`'s public `a`;
added, matching the real class.

Waiting on the user: whether a node's SHAPE should show what it is (round if
it has its own output, however many others it feeds) or something else.

**Slice 2c, done.** The listening point is permanent and the levels always
live, so an edit shows on the faders at once; halos always. Shift-click
selects several faders: `b` binds or unbinds every selected node to every
selected output, `t` and `T` trim them all, and removing an output needs
exactly one selected. The model's single current output became a SET with the
current always in it, kept in step at every place it changes -- seven entry
points converted deliberately, since one missed would leave the two
disagreeing, D-018's pattern.

Link lines, designed in text first: only between two NODES (an output with no
node is shown by its fader); plug at the sending end, socket at the receiving
end, as gear has outputs male and inputs female; heavy and sized by share when
the sender is selected, light when only the receiver is; side by side when two
nodes feed each other; percentages for a single selection. Silence and taps
draw no line: fill shows silence, a purple fader dot shows a tap.

`linkLines()`, `contributions()`, `tapsOf()` and the output set are tested;
25 scripts, 285 steps; 11 of 11 mutations caught first time; four existing
gates repointed at rewritten lines and shown to still catch. A comment claimed
a node's heavy lines plus silence make 100%; the vectors showed its own output,
which draws no line, is the rest -- the wording was corrected, not the numbers.

**Slice 2b+, done.** Reported in use: selecting a node and pressing `b` made
it SILENT, and binding seemed not to work once nodes were bound. Reproduced:
auto-output made each new node's output current, so `b` -- a toggle -- acted on
the very output the node already fed, and unbound it. Placing a node now
changes the current output only if there was none; an unbind that leaves a
node with no output says so, by name or count, as a warning.

**Three scripts passed while no longer testing their purpose.** Each relied on
the newest node's output being current; under the new rule a surviving-output
case, a reserved-name case and a removal-before-the-current-output case were
simply never reached, and the suite stayed GREEN. Confirmed rather than
assumed: with the scripts unrepaired, two of three existing gates went DEAD --
their faults passed. Each script now picks its output explicitly instead of
relying on a default. CI would have caught it, since a gate demands its fault
make the suite fail; but only faults WITH gates are protected, so the affected
scripts were also read by hand. A behaviour change is checked by what its
scripts still reach, not by whether they pass.

22 scripts, 252 steps. No new source file.

**2b, first build on the Mac:** three errors -- `getPath()` called on a const
`ofFileDialogResult`. Real openFrameworks declares it non-const; the test stub
had declared it const, so the stub accepted what the real library rejects.
Fixed both: the code, and the stub, which now reproduces the error before the
fix. A stub must be no MORE permissive than the library it stands in for --
D-010's warning, met in practice. The stub's other const functions were
checked against openFrameworks and are accurate.

**Slice 2a+, done.** Asked for after using 2a: bindings could be added one at a
time but removed only all at once, and pressing `b` twice fed an output DOUBLE,
since binding adds weight. `b` is now a toggle for the current output -- every
selected node already feeding it unbinds, otherwise the ones not yet bound bind
-- keeping each node's fill, and leaving a node that loses its last output
cleanly null. `Model::feeds()` added; the undo label became "binding" so
undoing an unbind does not say "undid bind". 18 scripts, 208 steps; 5 of 5
mutations caught, 3 as gates. The faders mark what the selection feeds, and
the map rings what feeds the current output. No new source file, so no
Project Generator update needed.

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
