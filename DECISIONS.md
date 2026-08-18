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
