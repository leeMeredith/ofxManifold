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
