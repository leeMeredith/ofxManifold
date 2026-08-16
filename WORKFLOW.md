# WORKFLOW

Rules of the road for working on ofxManifold. This file is the durable version
of setup and daily process, so nothing important lives only in a chat log.

---

## 1. Where this repo lives

```
<OF_ROOT>/addons/ofxManifold/
```

It sits in `addons/` from the first commit even though the kernel does not need
openFrameworks. Project Generator only scans `addons/`, and relocating a repo
after it has a remote produces broken clones for anyone who already pulled it.

---

## 2. Repository shape, and why the kernel is not a separate repo

`src/core` is a **build boundary**, not a separate repository.

The rule it enforces: nothing in `src/core` includes `ofMain.h` or any other
openFrameworks header. glm is the only dependency, and glm is header-only.

This is checked mechanically on every run. `make test` compiles the kernel with
`-Ilibs` and no openFrameworks include path at all, so an oF include smuggled
into core does not produce a warning or a style complaint — it fails the build.

### Why not a separate repo, as with ortho-kernel

`ortho-kernel` was separate because it had three consumers: `ortho`,
`ortho-max`, and `ofxOrtho`. Three wrappers cannot each own a copy of the
language logic, so separation was a necessity rather than a principle.

ofxManifold has one consumer today. The second — a SuperCollider-side manifold
runtime, per architecture document section 16 Mode B — is speculative.

Splitting later is cheap *now* and expensive later. Core is two headers, a
Python reference, and a vector file, with no dependencies to untangle. Lifting
it into a `manifold-kernel` repo and vendoring it back with a `VENDORED.md`
would be an afternoon.

**Tripwire:** split before tagging v1.0 if a second non-oF consumer becomes
real. After third parties have vendored ofxManifold, moving the kernel out from
under them is a breaking change.

---

## 3. Toolchain

```bash
g++ --version && python3 --version && make --version | head -1
```

On macOS, `g++` is clang. That is fine; the kernel is plain C++17. If it is
missing, run `xcode-select --install`.

---

## 4. Vendored glm

`libs/glm` is committed to the repository. This is deliberate and is the
opposite of the usual instinct to ignore dependencies.

openFrameworks 0.12.1 already ships glm at `libs/glm/include`, so a consumer
building inside an oF project uses oF's copy. The vendored copy exists so the
kernel test suite builds on a machine with no openFrameworks checkout, including
CI.

It is vendored rather than submoduled because GitHub ZIP downloads and Project
Generator users receive empty directories where submodules should be. See
`libs/VENDORED.md` for the pinned version and update procedure.

To restore it if missing:

```bash
cd libs
curl -sSL -o glm.tar.gz https://codeload.github.com/g-truc/glm/tar.gz/refs/tags/1.0.1
tar xzf glm.tar.gz && mv glm-1.0.1/glm ./glm && rm -rf glm-1.0.1 glm.tar.gz
cd ..
ls libs/glm/vec2.hpp
```

---

## 5. The daily loop

```
edit src/core/*.h  →  make test  →  green?  →  commit  →  next step
```

| Command | Does |
|---|---|
| `make test` | rebuild and run all vectors |
| `make vectors` | regenerate `tests/vectors/triangle.vec` from the Python reference |
| `make clean` | remove `build/` |

`make vectors` is run only when cases are added or changed. The regenerated
`.vec` file is committed in the **same commit** as the code change that
motivated it. A reference that has drifted from its own committed output is
worse than no reference, because it reads as authority while asserting nothing.

---

## 6. What green means

Vectors are classed, and the classes do not carry equal weight.

| Class | Authority | A failure means |
|---|---|---|
| `ANALYTIC` | geometry itself | the arithmetic is wrong |
| `CROSS` | independent Python reference | the two implementations disagree |
| `SPEC` | a rule we invented | we are inconsistent with ourselves |

There is no external reference implementation the way ofxOrtho had a JavaScript
one. The Python reference is therefore written from a *different formulation* —
Cramer's rule on the linear system, where the C++ uses signed sub-triangle cross
products. Agreement between them is evidence. A Python file that mirrored the
C++ line for line would be a tautology and would prove nothing.

A green run that is mostly `SPEC` has proved much less than a green run that is
mostly `ANALYTIC`. The runner prints the split for exactly that reason.

---

## 6.1 Precision in emitted vectors

References emit **10 significant digits**, via the shared `fmt()` in each file.
Not 17.

`sqrt` is correctly rounded per IEEE-754 and reproduces bit-for-bit anywhere.
`sin` and `cos` are not, and platform libm implementations differ by about one
unit in the last place — enough to break byte-exact regeneration at 17 digits,
which is exactly how it was found (DECISIONS.md D-006).

Ten digits resolve to about 1e-10 against a 1e-6 comparison tolerance, so the
precision loss cannot affect any assertion. Any new reference should use the
same `fmt()`.

---

## 7. Adding a new capability

The order is fixed, and it is the Gray-Scott order: simulate, then implement.

1. Write the behaviour into the architecture document first, including which
   layer owns it.
2. Extend `tests/ref/reference.py` with the new cases, classed honestly.
3. `make vectors` — the expected values now exist and are committed.
4. Write the C++ against them.
5. `make test` until green.
6. Commit code and vectors together.

Never step 4 before step 2. Writing the implementation first and then writing
vectors that agree with it produces a suite that can only confirm what was
already believed.

---

## 8. Proving the suite can still fail

A test suite that has never gone red is an assertion, not a check. Periodically,
and after any change to the runner itself, introduce a fault and confirm it is
caught:

```bash
sed -i '' 's|signedArea2(p, c, a)|signedArea2(p, a, c)|' src/core/ofxManifoldTriangle.h
make test
sed -i '' 's|signedArea2(p, a, c)|signedArea2(p, c, a)|' src/core/ofxManifoldTriangle.h
make test
```

Expected: `6/19 RED`, then `19/19 GREEN`.

Note `sed -i ''` is the macOS form. On Linux and in CI it is `sed -i` with no
argument.

Three faults have been verified as caught:

| Fault | Result |
|---|---|
| transposed sub-area argument | 6/19 |
| bias not renormalized | 16/19, caught by partition-of-unity |
| degenerate triangle accepted at construction | 17/19 |

The first is the important one. It produced weights of 3.49 and −2.49 for a
point genuinely inside the triangle — a fault that in a live system would look
like a tracking bug rather than a maths bug.

---

## 8.1 Continuous integration

`.github/workflows/kernel.yml` runs on every push and pull request.

**Job `test`** — matrix over `ubuntu-latest` (x86_64) and `macos-latest`
(arm64), `fail-fast: false` so one platform never masks the other. Steps:

1. record the toolchain and architecture
2. record floating-point characteristics: `FLT_EPSILON`, the collinear doubled
   area, and whether the compiler contracted the expression into an FMA
3. assert the committed `.vec` reproduces byte-for-byte from `make vectors`
4. `make test`

Step 2 exists because D-001 was a floating-point difference between platforms.
If a future failure has that shape, the evidence is already in the log rather
than needing a reproduction.

Step 3 exists because nothing else stops `reference.py` being edited without
regenerating. The suite would keep passing against stale expected values, which
is the same failure as a reference that agrees with the implementation by
construction.

**Job `mutation`** — introduces four known faults and requires each to be
caught: transposed sub-area argument, unrenormalized bias, `kAreaEpsilon` too
small, `kAreaEpsilon` too large. Linux only, since it tests the suite rather
than the platform.

The last two are a pair on purpose. Too small accepts degenerate triangles; too
large eats legitimate fine-mesh topology. Checking only one direction leaves a
threshold that can drift the other way unopposed.

---

## 9. Commit discipline

- Never commit a red kernel as a baseline. If `make test` is red, fix or revert.
- Code and its vectors go in one commit.
- `build/` is ignored. `libs/glm/` is not.
- Architecture document changes that reverse a prior decision keep their `[v2]`
  or `[new]` marker so the reasoning stays auditable.

---

## 10. Publication — not yet

The repository is private. No release, no tag, no topics, no ofxaddons
submission until, at minimum:

- `Manifold2D` exists with containment search and a topology report
- an `Evaluator` exists
- a renderer exists
- at least the `basicManifold` and `parameterMorphing` examples build and run

Publishing an addon whose README describes an architecture that is not yet
implemented is worse than publishing nothing.

---

## 11. Current state

Kernel only.

- `src/core/ofxManifoldTypes.h` — Node, WeightedNode, Evaluation, epsilons
- `src/core/ofxManifoldTriangle.h` — signed-area barycentric solve, per-node
  bias, construction-time degeneracy rejection, retained winding sign

19/19 green: 10 ANALYTIC, 5 CROSS, 4 SPEC.

Two contract commitments are already honoured in code:

- `solveRaw()` is a free function of three positions and a point, with no object
  state, so it transcribes to GLSL unchanged if the section 21.5 lookup-texture
  bake is ever built.
- `Triangle` retains its construction winding sign, so the section 8.6 animated
  node position flip test has something to compare against.

### Next step

`Manifold2D` — node storage, region list, containment search, topology report
including T-junction detection. Its Python reference and vectors come first.
`Evaluator` goes on top of that, not before it.
