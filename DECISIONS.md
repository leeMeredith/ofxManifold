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
