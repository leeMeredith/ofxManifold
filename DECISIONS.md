# DECISIONS

A living log of decisions that were reversed, and why. Entries are appended, not
edited. The value is in the reversals: a decision that was never wrong teaches
nothing, and a reversal whose reason is lost gets made again.

---

## D-001 — `kAreaEpsilon` raised from 1e-9 to 1e-6

**Date:** 2026-08-15
**Status:** reversed and shipped
**Files:** `src/core/ofxManifoldTypes.h`, `tests/ref/reference.py`,
`tests/run_vectors.cpp`

### What was decided originally

`kAreaEpsilon = 1e-9f`, the minimum doubled signed area for a triangle to be
constructible. Chosen to be "small enough not to reject anything real."

### What went wrong

The kernel passed 19/19 on Linux x86 and failed 17/19 on macOS ARM. Both
degenerate-rejection vectors failed on the Mac: `Triangle::make` accepted
triangles it should have refused.

The cause is FMA contraction. `signedArea2()` computes `a*b - c*d`. Apple
Silicon fuses this into a single fused-multiply-add, which computes the first
product exactly and subtracts an already-rounded second product. For the
collinear triangle (0.1,0.1) (0.4,0.4) (0.7,0.7) the residual is **4.17e-09**
rather than zero. That cleared 1e-9, so the triangle was accepted.

The same source on x86 emitted no FMA instruction at the baseline target,
returned exact zero, and passed.

### Why the original value was wrong regardless of platform

`FLT_EPSILON` is 1.19e-7. For coordinate differences of order 1, the products
inside `signedArea2()` carry rounding noise around 1e-8. A threshold of 1e-9 sits
**below the noise floor of the arithmetic it is thresholding**. It was not a
tolerance; it was a number smaller than the error it was meant to absorb.

The Linux green was a platform accident, not a correct result. It is worth being
precise about this: the suite did not "work on Linux and break on macOS." It was
wrong on both, and only one of them said so.

### What was changed

1. `kAreaEpsilon` is now `1e-6f` on the doubled area — a minimum triangle area
   of 5e-7. A cell of a 100x100 normalized grid has doubled area 1e-4, two
   orders of magnitude clear.

2. The `sliver_below_eps` vector previously used `0.7000000001` against `0.7`.
   **Those are the same float.** The row was a duplicate of the collinear row and
   asserted nothing additional. It is now a unit-base triangle of height 1e-7.

3. **New `ACCEPT` record type.** The construction check was one-sided: an
   implementation that rejected every triangle would have passed every
   `DEGENERATE` row. Three `ACCEPT` rows now guard the other end — a thin but
   valid triangle at ten times epsilon, a fine-mesh grid cell, and the canonical
   test triangle. `ACCEPT` also asserts `constructionSign() != 0`, since a zero
   sign would leave the section 8.6 flip test with nothing to compare against.

### Verification

| Configuration | Result |
|---|---|
| default flags | 22/22 green |
| `-mfma -ffp-contract=fast` (simulates Apple Silicon) | 22/22 green |
| epsilon reverted to 1e-9 | 21/22 red — `sliver_below_eps` |
| epsilon raised to 1e-3 | 20/22 red — `thin_but_valid`, `fine_mesh_cell` |

The suite now fails in **both** directions. That is the property that was missing.

### Rejected alternative

Adding `-ffp-contract=off` to the Makefile would make results bit-identical
across platforms and would have made the original epsilon "work."

Rejected because consumers build this kernel inside an openFrameworks project
with oF's own flags, where contraction is on. Testing a configuration nobody
ships would hide the failure rather than fix it. The epsilon must be robust under
contraction, and now is — verified above.

### Pattern

This is the empirical-over-geometric rule again, in a new place. The epsilon was
reasoned about rather than measured. `FLT_EPSILON` is a fact that was available
the whole time and was not consulted.

Second pattern, worth naming separately: **a passing test on one platform is one
data point, not a proof.** The x86 pass and the ARM fail came from identical
source. Cross-platform CI is not bureaucracy here; it is the only thing that
would have caught this before hardware did.


---

## D-002 — `positionOf()` must undo the per-node bias before combining

**Date:** 2026-08-15
**Status:** decided at implementation time
**Files:** `src/core/ofxManifold2D.h`, `tests/ref/reference_manifold.py`

### The trap

Architecture document section 8.5 describes the inverse as "the weighted sum of
node positions." That description is correct only for a manifold whose per-node
biases are all 1.

Forward evaluation applies the bias and renormalizes:

    b_i = raw_i * nw_i / SUM_j (raw_j * nw_j)

so the weights that come out of `evaluate()` are **not** the barycentric
coordinates of the point. Combining node positions against them directly lands
somewhere else, and does so silently — the result is a plausible position inside
the region, just the wrong one.

### The fix

The bias is exactly invertible. `raw_i` is proportional to `b_i / nw_i`, so
dividing through and renormalizing recovers the true coordinates before the
positions are combined. `positionOf()` does that first.

This is why the round-trip vector on the biased manifold (`weighted_nodes /
rt_biased_centroid`) is classed ANALYTIC rather than CROSS: forward-then-inverse
must return the input point, and that is knowable without either implementation.

### Where it is not invertible

A bias at or near zero genuinely destroys its component — `b_i` is zero
regardless of `raw_i`, and no inverse exists. `positionOf()` returns
`wellPosed == false` and falls back to the naive combination, so the caller
still gets a position and is told not to trust it.

### The `wellPosed` flag is load-bearing, not decorative

`fan / inv_spans_disjoint_regions` is the case that proves it. Weights of 0.5 on
N and 0.5 on S sum to one and recover a position of exactly (0.5, 0.5) — the
centre node O, a real point in the manifold that looks entirely reasonable.

But N and S share no region: Q0 holds N, Q2 holds S. The blend describes a
mixture across disjoint parts of the manifold, and the position it recovers
means nothing. **Only the flag distinguishes it from a valid result.**

That vector was added after a mutation test showed the `sharesRegion()` check
could be deleted with the suite still green. Nothing else in the suite had
weights that summed correctly while spanning disjoint regions. A check with no
vector behind it is decoration, and this one nearly stayed decoration.

### Pattern

Same shape as D-001 in a different place: the architecture document stated a
behaviour in the simple case and the simple case was not the general one. Worth
reading section 8.5 as a description of intent rather than a specification.


---

## D-003 — Curves must not renormalize, and normalize() stays explicit

**Date:** 2026-08-16
**Status:** decided at implementation time
**Files:** `src/interpretation/ofxManifoldCurves.h`

### The decision

`curve::apply()` reshapes each weight and returns. It does not restore partition
of unity, and there is no option to make it.

### Why it is tempting to do the opposite

Every other stage in the pipeline preserves the sum. Barycentric coordinates sum
to one. `spread()` sums to one. A linear `blend()` of two vectors that each sum
to one sums to one. `curve::apply()` is the only operation that breaks the
invariant, which makes it look like a bug.

It is not. Equal-power gains preserve the sum of SQUARES, not the sum. For a
two-node vector summing to one, `sqrt` produces gains whose squares sum to one
and whose plain sum is about 1.414. Renormalizing would divide that away and
leave weights that sum to one again — which is exactly the property the curve
existed to replace.

A curve that renormalized would be an expensive identity function on any input
that already summed to one. That is the whole failure, and it would look correct
in a weight readout.

### What the vectors assert

- `sum_after_equalpower` requires the sum to be ~1.414 and not 1. Classed SPEC,
  because "do not renormalize" is our rule.
- `power_equalpower_two`, `power_cosine_two`, `power_equalpower_asym` require the
  sum of squares to be exactly 1. Classed ANALYTIC — this is the definition of
  constant power, not an opinion.
- `power_cosine_three` records that cosine does NOT hold power for three nodes
  while equalPower does. The two curves are different instruments rather than
  two spellings of one, and a triangular manifold is in the three-node case most
  of the time.

`normalize()` exists for callers who want the sum back, and it returns its input
unchanged rather than emitting NaN when the sum is too near zero to divide by. A
silently NaN weight vector downstream is the identity-buffer failure again:
everything looks structured, nothing is valid.

---

## D-004 — Identity vectors need non-contiguous node IDs

**Date:** 2026-08-16
**Status:** gap found by mutation testing, closed
**Files:** `tests/ref/reference_interpretation.py`

### The gap

Every interpretation vector originally used node IDs 0, 1, 2. Mutation testing
then showed that `curve::apply()` could be changed to **reindex its output** —
emitting `0, 1, 2` regardless of what came in — with all 42 vectors still green.

The same hole existed in `blend()`.

Node identity is the property the entire architecture is built to retain. It is
what separates this from an anonymous array of floats, and it was the one thing
the suite could not see.

### The cause

Sequential IDs starting at zero are indistinguishable from positional indices. A
vector written with them cannot tell the two apart, however many of them there
are. Adding more vectors of the same shape would not have helped.

### The fix

Vectors with sparse, non-zero-based IDs: `identity_sparse_ids` (5, 2, 9),
`equalpower_sparse_ids`, `spread_sparse_ids` (7, 3 within 12 nodes), and
`blend_sparse_ids` (4, 8 against 8, 1, sharing node 8 at a non-zero index).

Both reindexing mutations are now caught, and both are CI gates.

### Pattern

A test suite can be uniformly wrong in a way that no amount of the same kind of
test will reveal. The mutation was not caught by adding cases; it was caught by
asking what a broken implementation could still pass. That question is the only
reliable way to find this class of gap, and it should be asked of every new
suite before the suite is trusted.


---

## D-005 — A vector generator must replay calls, not results

**Date:** 2026-08-16
**Status:** gap found by mutation testing, closed
**Files:** `tests/ref/reference_mapping.py`

### The gap

A vector was written to cover the rule that binding the same node to the same
target twice **accumulates** the link weight rather than replacing it. The
mutation that changes `+=` to `=` still passed with the suite green.

The vector was not weak. It did not exist.

### The cause

The Python fixture accumulated the duplicate bind into its own link table as the
fixture was built, and the emitter then wrote out the **accumulated table**:

    BIND 0 out.1 1.0

instead of the two calls that produced it:

    BIND 0 out.1 0.5
    BIND 0 out.1 0.5

So the vector file contained no duplicate, the C++ runner never called `bind()`
twice on the same pair, and the rule under test was never reached. The file
described a fixture that had already had the interesting operation applied to it.

### The fix

The fixture keeps a raw log of `bind()` calls in order, and the emitter replays
the log rather than serializing the resulting table. All eleven mapping
mutations were then re-run, since the change affects every fixture in the file.

### Pattern

This is a different failure from D-004 and worth separating.

D-004 was a suite that could not **see** a class of bug: the vectors were real,
they simply could not distinguish identity from position.

D-005 was a suite that quietly did not **run** the operation at all. The vector
had a name, a class, a comment explaining what it guarded, and expected values
that were correct — and it exercised nothing.

The general rule: **a generator that records the state of a fixture rather than
the operations performed on it will silently omit any operation whose effect is
idempotent in the recording.** Duplicate binds, repeated inserts, and
order-dependent accumulation are all invisible this way.

Only mutation testing distinguishes a vector that passes from a vector that runs.
A green suite reports both identically.


---

## D-006 — Vector files emit 10 significant digits, not 17

**Date:** 2026-08-16
**Status:** CI failure on macOS, fixed
**Files:** all four references in `tests/ref/`

### What happened

CI went red on `macos-arm64` only. All four suites passed on both platforms.
What failed was the drift check: the committed `interpretation.vec` did not
regenerate byte-for-byte when the reference was re-run on macOS.

The values were correct. The file was not reproducible.

### Cause

IEEE-754 requires `sqrt` to be **correctly rounded**, so it returns bit-identical
results on every conforming platform. It requires nothing of the sort for `sin`
and `cos`. Those come from the platform's libm, and implementations differ by
about one unit in the last place.

    sin(pi/4) on this machine   0.70710678118654746
    one ULP away                0.70710678118654757

At 17 significant digits those are different strings. At 10 they are the same.

Only `reference_interpretation.py` emits trigonometric results, which is why it
alone failed. The other three use arithmetic and `sqrt`, both exactly
reproducible — so they were correct by luck, not by design.

### Fix

All four references now emit 10 significant digits. Applied to all four rather
than only the one that failed, because the next reference to reach for a
transcendental function should not have to rediscover this.

Ten digits resolve to about 1e-10. The comparison tolerance is 1e-6, so there
are four orders of headroom: the precision loss cannot affect any assertion.

Regeneration is now idempotent on this platform, and all 26 mutation gates were
re-run afterwards to confirm the reduced precision had not blunted any check.

### Residual risk

Byte-exact comparison of rounded values can still diverge if a value sits within
one ULP of a rounding boundary at the tenth digit. That is roughly a one in a
million chance per emitted value, and about a hundred values are emitted.

Accepted for now, because byte-exact comparison is a much simpler and stronger
statement of "you forgot to run `make vectors`" than a numeric diff would be. If
it ever fires, the fix is to compare the files numerically with a tolerance
rather than to reduce precision further.

### Pattern

Second platform divergence in this project, and the same shape as D-001: an
assumption about floating-point behaviour that was never measured. There the
epsilon was chosen below the noise floor; here the output precision was chosen
above the reproducibility floor.

Also worth noting what the dual-platform matrix bought. The failure was not in
the mathematics and would never have appeared on a single-platform run. It says
something narrow but real: **a file is only reproducible to the precision its
least reproducible function supports.**


---

## D-007 — Three serialization vectors that passed without testing anything

**Date:** 2026-08-16
**Status:** gaps found by mutation testing, closed
**Files:** `tests/ref/reference_serialize.py`, `tests/run_serialize.cpp`

Serialization went green at 35/35 on the first run. Mutation testing then found
four checks that could be deleted with the suite still green. Three of them are
distinct failure modes worth naming separately.

### 1. Passing on a coincidence

`bad_duplicate_key.json` was `{ "version": 1, "version": 2, "nodes": [] }`,
written to prove that duplicate keys are refused rather than resolved last-wins.

A last-wins parser reads `version` as 2, which is an unsupported version, and
refuses the file anyway. The vector asserted only that the file was refused, so
it passed identically with the check present or absent. It was testing the
version guard while claiming to test duplicate-key handling.

The fixture now duplicates `"triangles"` with two valid values, so a last-wins
parser loads it happily and only the real check refuses it.

### 2. Right verdict, wrong reason

Removing the dangling-node check from triangle loading did not turn the suite
red. The file was still refused — the invalid `NodeID` fell through to
`addTriangle()`, which rejected it as a construction error.

Correct verdict, wrong diagnosis. The error message changed from "triangle
references unknown node: B" to "triangle rejected: degenerate or repeated
vertex", and the person reading that message would go looking for a geometry
problem in a file whose actual fault is a typo in a node name.

Every rejection vector now asserts a **substring of the error**, not just the
verdict. The message is what someone actually reads when a map will not load
half an hour before a cue.

### 3. Values that survived the wrong precision by accident

`json::number` writes `%.9g` because nine significant digits is the minimum that
round-trips a 32-bit float exactly. Cutting it to six, seven, or eight did not
turn the suite red.

Every position in every fixture was a short decimal — 0.2, 0.45, 0.85 — and all
of those survive six digits. The precision guarantee was untested because no
fixture value needed it.

`precision.json` now uses positions verified by round-trip to survive nine
significant digits and to be **lost at seven and eight**: 0.103641056,
0.906023036, 0.505853565 and similar. All three precision mutations are now
caught.

### 4. Counting instead of naming

`MAPPING_TARGETS` asserted the target count. Writing numeric IDs in place of
target names produced a file that reloaded into four targets called "0" through
"3": count correct, byte stability intact, and every OSC address in the show
wrong.

`MAPPING_NAMES` now asserts the names themselves, after a save/load cycle so it
covers the writer as well as the reader.

### One mutation left uncaught on purpose

Returning a partial string on EOF instead of failing is unobservable **inside**
any object or array, because the enclosing container fails first regardless.
The only place it shows is an unterminated string as the whole document, which
`bad_unterminated_string.json` now covers. Worth recording that the reachability
of a fault can depend on where in the grammar it sits.

### Pattern

D-004 was a suite that could not see a class of bug. D-005 was a suite that did
not run the code. D-007 is a third kind: **vectors that ran the code, observed
the right outcome, and were satisfied by the wrong cause.**

All three are invisible in a green run. The only question that finds any of them
is what a broken implementation could still pass, asked deliberately, of every
check, before the suite is trusted.


---

## D-008 — The CI workflow is hand-written, not round-tripped through YAML

**Date:** 2026-08-17
**Status:** CI rejected the workflow entirely, fixed
**Files:** `.github/workflows/kernel.yml`, `tests/check_workflow.py`

### What happened

GitHub refused to run the workflow at all:

    Invalid workflow file
    (Line: 15, Col: 1): Unexpected value 'true'

Not a failing job. The file would not parse.

### Cause

YAML 1.1 reads the bare word `on` as the boolean **true**. GitHub Actions parses
with YAML 1.2, where it stays a string, so a hand-written `on:` trigger is
correct and works.

PyYAML is a 1.1 parser. Regenerating the mutation gates by loading the file with
`yaml.safe_load`, editing the structure, and writing it back with
`yaml.safe_dump` turned the trigger key into `true:`. Every job, every gate,
every step was intact — and the file was worthless, because it no longer had a
trigger.

### The trap inside the trap

The obvious check fails a good file.

`yaml.safe_load` reports `True` as a key for a **perfectly valid** workflow,
because that is what a 1.1 parser does with `on`. So asking the parsed structure
whether the trigger survived cannot work: it answers `True` either way. Only the
raw text distinguishes them.

The first validation written here made exactly that mistake and reported FAIL on
the repaired file.

### What changed

The trigger block is hand-written and carries a comment saying so. The mutation
steps below it are still generated, but the generator now splices rather than
round-trips.

`tests/check_workflow.py` asserts the literal characters `on:` at column zero,
that no literal `true:` key exists, that every job has steps, and that every
mutation gate names a file that actually exists. It runs as part of `make test`,
so a broken workflow is caught before a push rather than by GitHub.

### Follow-up: two more faults in the same repair

The first fix repaired the trigger but left two problems, both found by CI
rather than by me.

**PyYAML is not installed on the GitHub macOS runner.** The check written to
guard against this trap imported `yaml` and failed to run at all. It no longer
imports anything outside the standard library — which is also the right answer
on principle, since a parser is the wrong instrument here. PyYAML reports `True`
as a key for a perfectly valid workflow, so the parsed structure cannot
distinguish a good file from a broken one. Only the raw text can.

**The dumped file was unreadable.** `yaml.safe_dump` collapsed every `run` block
into a single escaped string:

    run: "python3 tests/mutate.py src/... \\\n  \"const float sB = ...\" ..."

Valid YAML, and impossible to read or diff. The workflow is now generated as
plain text with block scalars, so a gate can be read at a glance and a change to
one shows up as a change to one.

### Pattern

Every other decision in this log is about the manifold. This one is about the
tooling around it, and it is the same shape as all of them: a representation
that looked equivalent was not, and the difference was invisible until something
downstream refused it.

Worth stating plainly, since it is now the third time in this project: **a
generated artifact should be diffed against what it replaced, not assumed
equivalent because the generator ran without error.** D-005 was a generator that
serialized state instead of calls. D-006 was a generator emitting more precision
than was reproducible. This is a generator that silently dropped the one key the
file existed for.


---

## D-009 — interpolate() was missing for five layers

**Date:** 2026-08-17
**Status:** gap found by review, closed
**Files:** `src/interpretation/ofxManifoldInterpolate.h`

### The omission

§1.1 states the addon's purpose in one line: *place your presets as nodes, drag
a point, get a weighted blend.*

Core, interpretation, mapping and io were all complete and green, with 182
vectors and 32 mutation gates, and **no function anywhere blended weights with
values.** `resolve()` maps weights onto targets. Nothing produced a blended
value. The headline use case had no implementation, and the
`example-parameter-morphing` example could not have been written.

### Why it stayed hidden

The architecture named one consumer of a weight vector. Mapping was built,
tested, and the layer was called done.

There are three:

    relationship   point -> weights            core
    interpolation  weights + values -> value   missing
    mapping        weights -> targets          built

Plus motion — `weights(t) -> d/dt` — which correctly stays outside the kernel,
because the evaluator is stateless and a derivative needs history.

No test could have found this. Every vector checked that implemented behaviour
was correct, and the missing product had no code to be wrong. It took reading
the top-line description against the file list.

### The design decision inside it

`interpolate()` does **not** normalize. Three cases, one rule:

- from `evaluate()`, weights sum to one and the result is a true affine
  combination
- with a null node holding weight, the sum is below one and the result scales
  toward zero — that shortfall **is** the fade, and normalizing would remove it
- after a curve, the sum exceeds one and the result is scaled up, which is
  almost never wanted

`coverage()` reports how much weight landed on a value, so a consumer that wants
to fade to silence rather than to the zero value has the gain it needs. The
curve case is recorded as a SPEC vector rather than prevented: the layer's job
is to be predictable, not to guess.

### Also added: affine invariance vectors

Nine ANALYTIC vectors asserting that weights survive an affine transform of the
triangle and point together. Free, since it follows from the definition, and it
exercises geometry nothing else in the suite visits — awkward rotations, shears,
a negative determinant, scales three orders either side of the normalized range.

A solve using unsigned areas passes every other vector in the project and fails
the reflection case alone.

### Pattern

Different from every other entry here. D-004, D-005 and D-007 were tests that
looked like they worked. This was a **feature that was never written**, in a
codebase whose tests all passed, found by comparing the README's first sentence
against the source tree.

Tests verify what exists. Nothing in a test suite asks whether the thing it
tests is the thing that was promised.


---

## D-010 — The wrapper cannot be proved, only checked

**Date:** 2026-08-18
**Status:** boundary reached, recorded
**Files:** `src/ofx/`, `tests/stub/ofMain.h`, `example-*/`

### What changed at this layer

Every layer below `src/ofx` is proved against an independent implementation:
two solves, two containment tests, two JSON parsers, agreeing to a tolerance.
182 vectors, 36 mutation gates, two architectures.

A renderer has none of that. There is no second implementation of "does this
manifold look right", and no reference to disagree with. The wrapper carries
design weight instead of test weight, and it is judged on screen.

### What the stub does and does not do

`tests/stub/ofMain.h` declares the thirty or so openFrameworks calls the wrapper
uses, so `make wrapper` can syntax-check the renderer and both examples on a
machine with no openFrameworks install.

**It has already been wrong.** It declared `ofBuffer(const std::string&)`, which
oF 0.12.1 does not have. The wrapper syntax-checked clean and failed to compile
on a real machine at `ofApp.cpp:119`. The stub was corrected FROM that failure,
not before it.

So the stub asserts a guess about every signature until a real compile says
otherwise. `make wrapper` green means "no typos". It does not mean "this
builds", and treating it as more than that is how the next `ofBuffer` ships.

### What screen testing caught that no test could

- A weight bar drawn behind the readout text overlapped it. The number is the
  truth and the point-to-node line already carries magnitude, so the bar was
  removed.
- The fan fixture was offered as a demonstration of hysteresis, and has no
  overlapping regions, so there was nothing to see. A dedicated overlap fixture
  was added.
- The T-junction fixture showed the fault to the detector but not to the eye:
  crossing near the offending node reads as an ordinary region change unless
  you know to watch one node's weight jump from about 0.5 to 0. Per-fixture
  guidance text now says what to look for.

None of these are bugs a vector could express. Two of them were demonstrations
that demonstrated nothing, which is the same failure as D-005 in a different
medium: the thing ran, and it showed the wrong thing.

### Pattern

The project's discipline was: prove it, then look at it. That holds until the
proving runs out. Past this line the order reverses -- look at it, and encode
whatever the looking teaches, knowing the encoding is weaker than what came
before.

Worth being explicit rather than letting the test count imply a confidence the
wrapper has not earned.


---

## D-011 — The recording path had no vectors at all

**Date:** 2026-08-24
**Status:** gap found by mutation testing, closed
**Files:** `tests/ref/reference_trajectory.py`, `tests/run_trajectory.cpp`

### The gap

Trajectories went green at 28/28. Mutation testing then found three faults that
the suite passed straight through:

- `finalize()` resampling uniformly instead of preserving spacing
- `finalize()` not subtracting the first sample's time
- `addSample()` letting time run backwards

All three are in the two functions a recording actually uses. None of the 28
vectors called either one: every trajectory in the file was built by handing
`setSamples()` a finished list, which is the PLAYBACK path.

So the suite tested replay thoroughly and recording not at all, while reading as
complete.

### Why the first one matters most

Uniform resampling looks harmless. It is not.

If a performer holds still for six seconds of a ten-second move, the finalized
times must be 0, 0.2, 0.8, 1. Resampled uniformly they become 0, 0.333, 0.667,
1 — and the replay turns a held position into a slow drift. The path visits the
same points in the same order, so a plot of it looks correct. Only the timing is
destroyed, and timing is most of what a recorded performance is.

### The fix

`RECORD` and `RECORDBACK` records replay the CALLS: raw times in, finalized
times asserted out. Plus a standing check that any finalized path of more than
one sample runs exactly 0 to 1 — a recording that began at t=17.5 and still
starts at 17.5 is carrying a stopwatch reading around instead of a portable
path.

All eight trajectory mutations are now caught, and six are CI gates.

### Pattern

Third time in this project, and the same shape each time.

D-005: the generator serialized fixture state instead of replaying `bind()`
calls, so duplicate binds were never exercised. D-011: the vectors constructed
trajectories from finished samples instead of replaying `addSample()` calls, so
recording was never exercised.

**A suite that builds its fixtures by assignment tests only the code downstream
of the assignment.** Any invariant established while a structure is being built
— accumulation, ordering, normalization, clamping — is invisible to it.

Worth asking of every future suite before it is trusted: which functions does
setting up the fixture skip?


---

## D-012 — An ambiguous mutation anchor, refused rather than obeyed

**Date:** 2026-08-25
**Status:** caught by tooling working as designed
**Files:** `.github/workflows/kernel.yml`, `tests/ref/reference_trajectory.py`

### What happened

CI failed:

    mutate: anchor appears 2 times in src/io/ofxManifoldSerialize.h;
    it must be unique

Trajectory serialization added a second `"unsupported space"` guard, so the gate
written for the manifold loader now matched both.

### Why this is the good outcome

`mutate.py` refuses a non-unique anchor and exits 2. `sed` would have mutated
the first occurrence and exited 0.

That difference decides what happens next. With `sed`, the gate would have kept
passing while testing only half of what its name claims — and the day someone
broke the trajectory guard, the gate named for it would have stayed green. The
refusal turned a silent weakening into a build failure, which is exactly the
argument made when the helper replaced `sed` (D-008's predecessor).

### What it exposed

The trajectory loader's space check had **no vector behind it**. It was written
by symmetry with the manifold loader and never asserted. The CI gate went
looking for something to mutate and found a guard nobody was testing.

Both are now covered: the manifold gate is disambiguated by trailing context,
a `bad_traj_space.json` fixture was added, and the trajectory guard has its own
gate. 43 gates, all anchors verified unique.

### Follow-on

Anchor uniqueness is now worth checking whenever code is added near an existing
gate. Duplicating a guard for a new type is normal and good; it just quietly
makes an old anchor ambiguous. The check is a few lines and could move into
`check_workflow.py` if this recurs.

### Pattern

Most entries here are about a test that looked like it worked. This one is about
a tool that refused to let a test stop working, and the refusal being loud enough
to act on. Worth recording as evidence that the exit-code decision earned its
keep rather than as a fault.


---

## D-013 — Duration alongside the normalized parameter, not instead of it

**Date:** 2026-08-25
**Status:** decided, prompted by prior art
**Files:** `src/sources/ofxManifoldTrajectory.h`, `src/io/ofxManifoldSerialize.h`

### Where it came from

Zach Lieberman's `timePointRecorder` (drawingWithTime, 2010) stores each sample
in seconds and exposes `getDuration()` and `getVelocityForTime()`. Reading it
made two omissions obvious.

**Duration was being thrown away.** `finalize()` rescaled recorded times to
[0, 1] and discarded the span, so "replay at the speed it was performed" — the
default a performer actually wants — was not expressible without the caller
remembering the number separately.

The fix keeps both. Normalized times make a path portable between maps and
replayable at any rate; the duration makes the original tempo recoverable.
Lieberman keeps seconds *instead of* a normalized parameter, which is right for
a drawing that never leaves its canvas and wrong for a path that has to replay
in another room.

`duration` is optional in the file. Absent means unknown rather than one, so a
path assembled programmatically does not claim a tempo it never had, and files
written before the field existed still load.

### Velocity, and why it does not break the layering

Section 3.1 keeps MOTION outside the kernel: the evaluator is stateless and a
derivative needs history.

A trajectory **is** history — a stored path, complete before anyone asks — so a
derivative over it is a pure function of data already in hand rather than a
stateful accumulator. `velocityAt()` belongs on `Trajectory`; `d(weight)/dt` on
a live source still does not, and still has no home in the kernel.

### The endpoint correction

Lieberman looks *backward* by a fixed 0.05 seconds, which is what a performer
feels: where did I just come from. For a stored path there is no reason to
prefer the past, so this uses a central difference.

That introduces a trap. At t = 0 the window clips to half its width. Dividing by
the **requested** window would report half the true speed — a stroke that began
fast reading as beginning slowly, which is precisely backwards. Dividing by the
span **actually used** gives the right answer at both ends, and `vel_start` and
`vel_end` assert exactly 1.0 on an evenly traversed path to hold it there.

### Two gaps mutation testing found

Dropping the duration on save, and dropping it on load, both passed the whole
suite. The `ROUNDTRIP` record compared bytes and positions — and a save/load
pair that agrees on discarding a field is byte-stable and position-perfect. The
duration is now asserted explicitly, plus a `FILEDURATION` record for a file
that carries one and a file that does not.

### One equivalent mutant, recorded rather than papered over

Removing the `window <= 0.0f` guard changes nothing: with a zero or negative
window the computed span is zero or negative and the span check returns zero
anyway. Verified directly rather than assumed. The guard stays because it states
the intent at the top of the function, but no vector can distinguish it and none
pretends to.


---

## D-013 — blend() cannot blend two manifolds

**Date:** 2026-09-01
**Status:** design gap found while building the feature, closed
**Files:** `src/interpretation/ofxManifoldBlend.h`,
`src/mapping/ofxManifoldMapping.h`

### The gap

`blend()` has existed since the interpretation layer, with vectors covering
endpoints, disjoint sets, overlap, curves, clamping and sparse ids. Section 9.3
names it as the mechanism for combining two manifolds.

It cannot do that.

`blend()` merges by NodeID, and NodeIDs are per-manifold indices. Two manifolds
built independently both start at zero:

    manifold A   front.L=0  front.R=1  front.C=2
    manifold B   rear.L =0  rear.R =1  rear.C =2

    blend(A.weights, B.weights, 0.5f)
        -> three entries, not six
        -> id 0 = 0.5625, which is front.L and rear.L added together

The result sums to one and looks entirely reasonable.

### Why no vector caught it

Every blend vector picks its ids by hand: A on 0 and 1, B on 2 and 3. There is
even a deliberate overlap case where both share id 1 — but that models *one*
manifold's weight vector overlapping another view of itself, which is a real
and different thing.

The suite tested blend() thoroughly against inputs that never collide, because
the person writing the inputs knew what each id meant. Two independently built
manifolds always collide.

### The fix, and why it lives in the mapping layer

`blendByName()` resolves each side through its own `Mapping` first, then merges
by target NAME.

That is not a workaround, it is where the operation belongs. Two maps have no
nodes in common — if they did they would be one map — so there is nothing to
merge at the node level. What they share is **outputs**, and outputs are named.

Resolving first also makes something expressible that the node level cannot: a
composite node in map A and a terminal node in map B can both feed `out.3`, and
their contributions add correctly.

Merging by name rather than by TargetID matters for the same reason again:
TargetIDs are per-Mapping indices. The fixtures declare the same target names in
DIFFERENT ORDERS on purpose, so an implementation merging by id passes only if
the orders happen to agree.

### blend() is not deprecated

Its precondition is now documented loudly: both vectors must come from the same
manifold. Valid uses remain — two evaluators at different points in one map, a
vector against a curved copy of itself, successive frames of one source.

### Pattern

Different from the earlier gaps. D-004 was a suite that could not see a bug;
D-005 and D-011 were suites that did not run the code. This is a function that
worked exactly as tested and could not do the job the architecture assigned it.

The vectors were written by someone who knew what each id meant. The failure
only appears when the ids come from somewhere that does not know.

**Worth asking of any identifier-keyed operation: where do the identifiers come
from, and can two callers legitimately produce the same one for different
things?**


---

## D-014 — Two correct layers, one untested interaction

**Date:** 2026-09-01
**Status:** gap closed while building the spread example
**Files:** `tests/ref/reference_mapping.py`, `tests/run_mapping.cpp`

### What was missing

`spread()` had ten vectors: endpoints, sparsity preserved at zero, uniform at
one, partition of unity at four intermediate amounts, clamping, sparse ids.
`Mapping::resolve()` had its own. Both correct, both thorough.

Nothing composed them, and their composition is not obvious.

Spread hands part of the weight to EVERY node in the map, null nodes included. A
null node discards its share. So spreading a map that is ringed with null nodes
**fades the output**, and nothing was told to fade:

    spread   node weights sum   resolved targets
    0.0      1.000              1.000
    0.5      1.000              0.750
    1.0      1.000              0.500      <- three of six nodes are null

The node weights are right. The target total is right. The relationship between
them is a consequence neither layer states.

### Why it matters more than it looks

This is the shape of thing that reads as a bug in a rehearsal room. An operator
raises spread expecting a wash, the level drops, and the obvious conclusion is
that something is broken. It is not broken — it is correct, and arguably it is
what you want, since spreading toward the edge of a map should approach the
same silence that moving toward the edge does.

But nobody can reason their way to that at short notice, which is exactly the
argument for writing it down as a vector rather than leaving it to be
rediscovered.

### The fix

`SPREADRESOLVE` records assert both totals separately at four spread amounts,
because they legitimately differ. Full spread over three bound and three null
nodes lands the target total at exactly 0.5, which is hand-checkable.

`example-spread` shows the same thing on screen with the two totals side by
side and the null nodes coloured differently.

### Pattern

Every previous gap here was inside one function or one suite. This one is
between two of them.

Each layer was tested against its own contract and neither contract mentions
the other, because the layering is exactly what makes them independent. That
independence is the architecture working — and it means **composition is a
third thing that has to be tested on purpose**, since no amount of testing the
parts implies it.

Worth asking wherever two layers meet: what is true of the pair that is stated
by neither?


---

## D-015 — Measured the mesh ceiling, and did not optimize

**Date:** 2026-09-01
**Status:** measured; one example fixed, no kernel change
**Files:** `tests/bench/bench_mesh.cpp`, `example-basic/src/ofApp.cpp`

### The question

Every map in the suite and every example is five to nine nodes. The roadmap has
carried a "larger mesh sanity check" since publication on the suspicion that
containment search would need a spatial index.

### The measurement

Triangulated square grids, 2000 evaluations each, release build, on both
platforms the project already tests against.

                    Linux x86_64                  macOS arm64
    nodes  regions   scan    hinted  validate()    scan    hinted  validate()
       16       18   0.11us  0.06us     0.01ms   0.18us  0.09us     0.01ms
      100      162   0.45us  0.06us     0.29ms   0.45us  0.06us     0.26ms
      324      578   1.34us  0.10us     3.21ms   1.10us  0.09us     2.45ms
     1024     1922   4.16us  0.34us    33.55ms   2.34us  0.18us    14.30ms
     3136     6050  13.25us  1.94us   328.01ms   4.67us  0.61us    129.22ms
    10000    19602  42.98us  9.71us  3557.01ms  14.79us  3.32us   1351.20ms

Apple Silicon is 2.6 to 2.9 times faster at the top end and marginally SLOWER
at the bottom. The crossover sits somewhere near a hundred nodes.

That shape is the usual one for a linear scan over a growing array: at small
sizes everything is in cache on both machines and raw clock decides it; at large
sizes memory bandwidth decides it, and the M-series wins comfortably. Worth
recording because the ceiling below is hardware-dependent, and quoting one
platform's number as though it were the number would be wrong.

### What it says

**Evaluation is never the problem.** Ten thousand nodes cost 43 microseconds
with no hint at all — 0.3% of a frame at 60fps. The linear scan over regions is
fine at every size anyone will author by hand, and the spatial index the
roadmap anticipated would optimize something that does not cost anything.

The `Evaluator` hint still earns its keep, four times faster at the top end,
but for a different reason than expected: it is not O(1), because a point that
leaves its region pays a full scan. It amortizes those scans across the frames
between region changes.

**`validate()` sets the ceiling, at O(regions x 3 x nodes).** It crosses a
frame budget around a thousand nodes on both machines -- 33 ms on x86, 14 ms on
Apple Silicon, either of which is a dropped frame -- and reaches seconds at ten
thousand.

### What was actually broken

`example-basic` called `validate()` on every `mouseDragged` while a node was
being dragged. At 324 nodes that is 2 to 3 ms per frame, tolerable; at 1024 it
is 14 to 34 ms depending on the machine, which is a dropped frame every frame on
either.

And it was unnecessary. Moving a node cannot change the region list, and the
one thing a move CAN break — an inverted region — is refused by
`setNodePosition()` before it happens. A move can create or clear a T-junction,
so the report is now refreshed once on mouse release rather than continuously.

### Why validate() was not optimized

It is authoring-time work. A bounding-box reject or a spatial grid would cut it
substantially, and nobody has a map where it matters: the practical ceiling is
somewhere past a thousand nodes, and a hand-authored control surface is tens.

Optimizing it now would add a spatial structure that must be kept correct
against node movement, in exchange for making a fast thing faster on maps that
do not exist. The measurement is recorded here so whoever does hit the ceiling
starts from evidence rather than repeating the investigation.

### Pattern

This is the Debug-to-Release lesson again, in the other direction. There the
measurement eliminated a planned optimization track; here it eliminated a
different one and pointed at a real per-frame cost nobody had suspected, in an
example rather than in the kernel.

The suspicion was containment search. The problem was validation. Neither
would have been found by reasoning about the code, and `make bench` now makes
the question cheap to re-ask.


---

## D-016 — The first stateful component, and a different testing shape

**Date:** 2026-09-07
**Status:** built
**Files:** `src/interpretation/ofxManifoldSmoother.h`,
`tests/ref/reference_smoother.py`, `tests/run_smoother.cpp`

### What smoothing is actually for

Not ordinary region crossings. On a conforming mesh the weights are already
continuous there — walk a point across a shared edge and the departing node
decays to exactly zero as the arriving one rises from zero:

    y=0.56  region 0 : O=0.550 N=0.150 E=0.300
    y=0.50  region 0 : O=0.700 N=0.000 E=0.300
    y=0.44  region 1 : O=0.550 E=0.300 S=0.150

The node SET changes; the numbers do not jump. That was worth checking before
building anything, and it narrowed the job considerably.

Smoothing is for the cases that genuinely step: a jittery source, leaving or
entering the hull, the Evaluator's hysteresis in overlapping regions, a
T-junction, or a point that jumps because a cue fired or a map was swapped.

### Weights, not the point

Smoothing the input position would handle jitter and nothing else. A hull exit,
an overlap pop and a map swap are discontinuities in the OUTPUT that a perfectly
smooth input still produces. Smoothing where the discontinuity is means one
component handles all of them.

### The finding worth keeping

**Exponential smoothing preserves partition of unity exactly**, even across two
weight vectors that share no nodes at all. The smoothed vector is a lerp between
two vectors that each sum to one, taken over their union with absent nodes at
zero, so the sum is a lerp of 1 and 1.

Measured across a fully disjoint transition, every tick: 1.000000.

That matters because it means a smoother can sit anywhere in the chain without
disturbing the invariant every other layer relies on.

**A slew limit does not**, and that is not a bug. Each component is clamped
independently, so while three nodes fall and two rise the totals do not balance.
The sum dips to 0.8, holds while the rates happen to match, and recovers to one
once the departing nodes reach zero. Renormalizing would hide it and would break
the bound the caller asked for — the point of a slew limit is that nothing moves
faster than the stated rate. Recorded as vectors rather than corrected.

### The testing shape

Every other suite tests a pure function: one input, one output, order
irrelevant. A smoother has memory, so a single call proves nothing — an
implementation right on the first tick and wrong on the fourth passes any
single-shot check.

Its vectors are sequences. Snap to a state, then tick, and tick, asserting the
output at every step, with the block failing on the first tick that disagrees.

Half-life and dt rather than a per-frame coefficient, so the result is
frame-rate independent — six ticks at 60fps and three at 30fps cover the same
0.1 seconds and must land in the same place. A coefficient tuned in rehearsal on
one machine would behave differently on another, which is a bad surprise on an
opening night. That equivalence is its own vector.

### One mutation that took a third vector to catch

Removing the early return for a disabled smoother did not turn the suite red.
With half-life 0 the general path computes alpha = 1 and lands on the target
anyway, so every disabled vector passed either way.

The early return sits BEFORE the dt check, deliberately: if smoothing is off,
elapsed time is irrelevant. `disabled_zero_dt` is the only case that
distinguishes them, and until it existed the early return looked like an
untested optimization.

Ten mutations, all now caught, five as CI gates.

### A gap found while writing the example

`ofxManifold.h`, the umbrella header a consumer includes, did not list
`ofxManifoldSmoother.h`. Nor did it list either of the `sources/` headers.

`example-trajectory` compiled anyway, because `ofxManifoldSerialize.h` includes
`Trajectory.h` for its own use. Two features were reachable only by accident,
through a transitive include that could have been removed at any time for
unrelated reasons.

The same class of bug as the missing `<cstdio>` (D-010's neighbour): it compiles
for whoever wrote it and breaks for the next person. `make headers` now asserts
that every header under `src/` appears in the umbrella, which is four lines and
catches a whole category.



---

## D-016b — Regions: the four decisions settled before any code

**Date:** 2026-09-15
**Status:** settled, then built (D-017, D-018)

Written into a working plan, D-016b, while the regions work was in
progress, and moved here when it was done. The plan is deleted; these are the
parts that were decisions rather than steps.

Why regions came before the editor: the editor's central interaction is
"create a region from selected nodes". A triangle-only model makes that a
three-node pick; an N-node model makes it a different interaction and a
different data model. Building the editor first would have meant rebuilding it.

#### A · Value type, not an interface

**Decided: one value type holding an ordered ring of `NodeID`, dispatching on
`size()`.**

§7 committed to `Region` as an interface with `Triangle` implementing it. That is
reversed.

The interface was written as an escape hatch for exactly this moment. Now that
the moment is here, a `std::vector<NodeID>` does the same job with less
machinery: `Manifold2D` stays copyable, no heap allocation per region, no
virtual call, no `unique_ptr` in a class that gets moved in `loadManifold`.

The argument that settles it: an interface here would be a **one-implementation
abstraction that stays that way**. A tetrahedron lives in `Manifold3D` because it
takes a `vec3` point, not here. An abstraction with two members and no prospect
of a third is a struct wearing a costume.

Value types also serialize obviously, copy obviously, and read as their contents
in a debugger.

#### B · No new noun

**Decided: `Region` is the concept. No `Polyset`, no `Polygon`, no `Cell`.**

Given D-A there is one struct holding an ordered ring. A region with three nodes
and one with seven are the same type differing in `size()`. A separate name would
name a thing that is not a separate thing.

- `Manifold2D::addRegion(std::vector<NodeID>)` is the general call
- `addTriangle(a, b, c)` stays as a convenience that forwards to it
- `Triangle` stops being a public type and becomes the fast path inside the
  solve, which is where an optimization belongs

This is the same shape of finding as the node taxonomy: terminal, null and
composite nodes turned out to be one struct differing in cardinality (§6.1).
Regions differ in arity. Neither needed a type hierarchy.

#### C · Non-convex regions are ACCEPTED

**Decided: accept any simple polygon. Stars and L-shapes included.**

An earlier draft of this plan proposed rejecting non-convex regions on the
grounds that MVC can produce negative weights inside them. That was wrong, and
measuring it is what showed so:

    shape                    interior samples with a negative   most negative
    star, inner radius 0.19        0 of 6356                      0.000000
    star, inner radius 0.07     1384 of 2336                     -0.000416
    L-shape, one reflex vertex  6288 of 9984                     -0.012705

A moderate star never goes negative at all. Negatives appear only in deep
notches and around a strong reflex vertex, and are tiny when they do.

Floater and Hormann's 2006 paper is titled *Mean value coordinates for arbitrary
planar polygons*. Arbitrary is the achievement. Rejecting non-convex would throw
away the feature the algorithm exists to provide.

The reasoning that led to the wrong answer is worth recording: **an
audio-specific concern was allowed to decide a geometry question.** A negative
gain is a phase inversion, which is usually wrong — for a panner. This is not a
panner. For parameter interpolation a negative weight is extrapolation, pulling
slightly beyond a node's value, which is sometimes exactly what is wanted.

The manifold describes relationships. A negative weight is a relationship.
Whether it is acceptable is interpretation.

#### D · Negative weights get help, in the interpretation layer

**Decided: a clamp-and-renormalize policy beside the curves, plus diagnostics.**

`clampNegative()` zeroes negative weights and renormalizes the rest. Cheap — the
worst measured case redistributes about 1% of the vector.

It must be documented as **breaking affinity**: after clamping the result is no
longer a true affine combination, so a blend of two parameter values will not
land where the geometry says. Right for gains, wrong for positions. The same
distinction as D-003, so it sits beside the curves with the same warning.

Not applied automatically anywhere.

---

---

## D-017 — Generalized regions, and three things the work turned up

**Date:** 2026-09-15
**Status:** kernel, vectors and serialization built; example pending
**Files:** `src/core/ofxManifoldRegion.h`, `src/core/ofxManifold2D.h`,
`src/io/ofxManifoldSerialize.h`, `tests/ref/reference_regions.py`,
`tests/run_regions.cpp`. Settled decisions in D-016b.

A region is now an ordered ring of N >= 3 nodes. Three take the barycentric
solve; more take mean-value coordinates after Hormann & Floater (2006). MVC
reduces to barycentric at N = 3 to 3.3e-16 over a full interior sweep, which is
what makes it an extension rather than a replacement.

### 1. The contract test passed, and needed a caveat

Step 4 of the plan asserted that `interpretation/`, `mapping/`, `io/` and
`sources/` would show an empty diff: nothing below the weight-vector line should
change when a second coordinate algorithm is added.

After the kernel change, it did show an empty diff. That was true, and it was
also hiding a bug.

`io/` is two things. Mapping and trajectory files sit below the weight line and
genuinely needed no change. The **manifold** file describes geometry, which is
above it — and the writer emitted exactly three node names per region. A quad
saved as a triangle, silently dropped its fourth node, and reloaded without
error as a different shape.

So the empty diff under `io/` was not evidence the change was unnecessary. It
was a change nobody had made. **An empty diff is a necessary condition for the
layering claim, not a sufficient one**, and the directory boundary was too
coarse: the contract is about what flows downstream of the weight vector, and
one file in `io/` describes what flows into it.

The claim itself holds. Nothing in `interpretation/`, `mapping/`, `sources/`,
or the mapping and trajectory halves of `io/`, needed to change.

Manifold files keep their old form where they can: a triangle-only map is still
written as version 1 under `"triangles"`, byte-identical to v1.0.0, so every
file an older reader could read it still can. A map containing any larger region
is written as version 2 under `"regions"`, so an older reader **fails cleanly**
with "unsupported version 2" rather than finding no `"triangles"` key and
silently loading a map with no regions at all.

### 2. MVC is not affine invariant, and the reference said it was

The reference asserted affine invariance for polygons, by analogy with the
triangle case in §7.0, and wrote the untransformed weights as the expected answer
under six maps. The C++ disagreed on two of them.

It was right. Python and C++ agreed on the transformed result to about 1e-7; the
expected values were an assumption, not a computation.

    transform    kind          max change in weights
    translate    similarity    5.6e-17
    scale        similarity    5.6e-17
    rotate       similarity    5.6e-17
    reflect      similarity    0
    nonuniform   affine only   0.19
    shear        affine only   0.12

Barycentric coordinates are ratios of **areas**, preserved by any affine map. MVC
is built from **lengths and angles**, preserved only by similarities.

This is a real tradeoff, made without knowing it. Wachspress coordinates are
affine invariant but require strictly convex polygons. MVC handles stars and
L-shapes but is only similarity invariant. Stars were an explicit requirement
(D-016b (D-C)), so MVC stays — and that choice gave affine invariance up.

What it does not break: the renderer's screen transform never touches the
weights, since evaluation happens in normalized space, so §5 holds. What it does:
an author who **stretches the node positions themselves** — squashing a map onto
a wide stage for a new venue — keeps the relationships in triangle regions and
shifts them in polygon regions.

The vectors now assert what is true: invariance under similarities, and
**non-invariance** under the other two, as its own ANALYTIC record — an
implementation that was affine invariant there would not be computing MVC.

This is the two-implementation discipline catching a false claim in the
reference rather than in the code, which is the less common and more valuable
direction.

### 3. A bug found by hand, fixed, and pinned by nothing

The truncation bug in (1) was found by writing a quad and looking at the file.
Mutation testing then put it straight back — and the suite stayed green, because
no vector round-tripped a region of more than three nodes. `COUNTS` checked the
region count, which truncation preserves.

`ARITY` records now assert every region's node count after a round trip.

Per-node bias had the same hole: every bias vector in the project was on a
triangle, so a polygon path that ignored bias entirely passed all of them.

### Also

- Check order in construction matters. A bowtie's lobes cancel to zero signed
  area, so testing area first reports `degenerate` for a ring that is really
  self-intersecting. The reference found this in itself before any C++ existed.
  A second bowtie with non-cancelling area (0.6655) proves the self-intersection
  check runs at all.
- `TopologyReport::nonConvex` lists non-convex regions as information. `clean()`
  ignores it.
- Anchor uniqueness moved into `check_workflow.py`. D-012 said to do this if it
  recurred; the regions work made two existing anchors stale in one pass, so a
  refactor now fails the local build rather than CI.
- **The renderer had the same three-node assumption as the serializer**, and
  nothing could have caught it: `make wrapper` checks syntax only. It filled
  each region with `ofDrawTriangle` and drew three edges, so a quad would have
  compiled and rendered as a triangle missing a corner. Found by grepping the
  wrapper for `ids[2]` before calling the work done, because the kernel had just
  shown that every file touching regions needed the same audit. Regions are now
  filled as tessellated shapes, which also fills a non-convex star correctly.
  `example-blend` had its own copy of the loop.
- My own patch to `TopologyReport` was silently skipped: a plain string replace
  matched nothing because the comment read "in no region at all" and the patch
  looked for "in no region". Every later edit in the pass asserted its anchor.

### Pattern

Three findings, three directions. The contract test was too coarse to see a real
gap. The reference asserted a property the mathematics does not have. And a bug
fixed by hand was left unguarded.

The common thread is that each check answered the question it was asked, and
the question was slightly wrong.


---

## D-018 — Non-convex regions were accepted, then refused

**Date:** 2026-09-21
**Status:** found while writing the example, fixed
**Files:** `src/core/ofxManifoldRegion.h`, `tests/ref/reference_regions.py`,
`tests/run_regions.cpp`

### The bug

Decision D-C accepted non-convex regions at construction, because a star is a
legitimate control surface and MVC handles it. Containment then refused most of
their interior.

`Region::contains()` treated a region as containing a point when no weight was
negative. For a triangle, and for any convex polygon, that is exactly geometric
containment. For a non-convex polygon it is not: MVC gives legitimate negative
weights at points genuinely inside the ring.

Measured on an L-shape, 3,900 interior points:

    reported inside by evaluate()      1,446
    reported OUTSIDE                   2,454   -- every one carrying a negative weight

So 63% of the region was silently treated as outside, and the points dropped
were precisely the ones D-C was about. A consumer could never receive a negative
weight from a non-convex region. `anyNegative()` and `clampNegative()` would have
had nothing to act on.

### Why no vector caught it

Every region vector called the solver directly — `solveRing()` or
`Region::evaluate()` — rather than going through `Manifold2D::evaluate()`. That
was deliberate: several vectors test points exactly on a vertex or an edge, and
containment has its own tolerance, so bypassing it kept those vectors about the
solve.

It also meant containment was never tested on a non-convex region at all. The
solver was right; the step that decides whether to call it threw the answer
away.

The comment in `Region.h` even said so — "stricter than geometric containment
... that is deliberate" — and pointed at a `containsRing()` that did not exist.
The trade-off was noticed, described, and not acted on.

### How it was found

By asking, before handing the example over, whether it could show what it
claimed to. The example tells you to drag into the L-shape's inner corner and
watch a negative weight appear. With this bug, it would have shown "outside
every region" at exactly that spot.

### The fix

Three nodes keep barycentric non-negativity, unchanged — for a triangle that is
geometric containment, and 31 vectors depend on it exactly. Four or more use a
genuine point-in-ring test: crossing number, plus an explicit boundary check.

The boundary check is not decoration. A bare crossing-number test is ambiguous
exactly on an edge; measured, it calls 6 of 12 edge points of a quad and 12 of
18 of the L-shape outside. A point dragged along an edge would flicker between
the region and nothing.

### What pins it

`CONTAINS` records go through `Manifold2D::evaluate()`: interior points of the
L-shape and deep star that carry negative weights must be reported inside with
those weights; every edge point of the quad and L-shape must be inside; points
in the L's notch and between the star's arms must stay outside, so a test that
simply said yes fails too.

Four mutations, all caught: reverting to weight sign, always-inside, an inverted
crossing test, and dropping the boundary check — the last caught by 17 vectors.

### Pattern

D-017 found that the contract test's directory boundary was too coarse. This is
the same lesson one level down: **testing each part correctly is not testing the
path a consumer takes through them.** The solver had 70 green vectors. None of
them walked through the door every real call uses.


---

## D-019 — Editing a region could reach a shape building it could not

**Date:** 2026-09-22
**Status:** fixed in the kernel; found while planning the editor's batch move
**Files:** `src/core/ofxManifold2D.h`, `tests/ref/reference_grids.py`,
`tests/run_grids.cpp`

### The bug

`setNodePosition()` refused a move that would invert or flatten a region, by
comparing its winding sign against the one recorded at construction. For a
triangle that is complete: three edges cannot cross each other, so inverting
and flattening are the only ways to break one.

For four or more nodes it is not. Measured: a legal convex quad, one corner
dragged across so the ring crosses itself. Construction refuses that ring as
self-intersecting. `setNodePosition()` **accepted** it, because the signed area
went from 0.72 to 0.15 without changing sign, and left the region
self-intersecting.

Regions of more than three nodes arrived with D-017, and nothing caught that the
movement check had been written for triangles.

### Same shape as D-018

D-018: non-convex regions were accepted at construction and refused at
evaluation. D-019: a self-intersecting region was refused at construction and
reachable by editing. **Both times the rule applied when a region is built and
the rule applied when it is used or changed had drifted apart**, and both times
it was the move from three nodes to N that exposed it.

Worth asking of any invariant a constructor enforces: is every other way of
reaching that state held to the same rule?

### The fix

`setNodePositions()` moves several nodes as one operation, all or nothing,
checking every touched region against its FINAL shape: winding sign, area, and
now self-intersection for four or more nodes. `setNodePosition()` is a batch of
one, so a single move and a group move are held to the same rule by the same
code — and the runner checks that the two paths reach the same verdict, so they
keep sharing it.

Checking final rather than intermediate shapes also changes what is allowed.
Sliding a triangle right by more than its width, one node at a time, drags its
first vertex past its second and inverts it partway; the sequence is refused
though the finished shape is fine. As a batch it is accepted.

### Two measurements that were not pinned

The same round measured two things the snapping code needed — reducing a
lattice basis before searching, and searching every polar ring rather than one
either side — implemented both, and pinned neither. The vectors targeted plain
rounding only. Mutation testing removed each fix and the suite stayed green.

D-017 recorded a bug found by hand and fixed without a vector. This is the same
failure one step earlier: **a measurement that justifies code is not a test of
that code.** Each now has traps aimed at its specific shortcut, found by
searching.
