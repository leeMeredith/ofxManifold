# ofxManifold

Continuous preset morphing for openFrameworks.

Place your presets as nodes. Drag a point between them. Get a weighted blend.

Spatial audio panning is one application, and historically the first, but it is
the narrowest one. See `ofxManifold.md` for the full architecture.

## Status

Kernel only. `src/core` implements triangle barycentric evaluation with per-node
bias and construction-time degeneracy rejection. No `Manifold2D`, no containment
search, no `Evaluator`, no renderer, no serialization — those come after the
solve is proved.

## Testing the kernel

`src/core` includes no openFrameworks header, so there is nothing to link:

```
make test
```

Expected:

```
  ANALYTIC  13/13
  CROSS     5/5
  SPEC      4/4

22/22  GREEN
```

`make vectors` regenerates `tests/vectors/triangle.vec` from the independent
Python reference in `tests/ref/reference.py`.

## What green means

The vectors are classed, and the classes carry different weight:

| Class | Authority | A failure means |
|---|---|---|
| `ANALYTIC` | geometry itself | the arithmetic is wrong |
| `CROSS` | independent Python reference | the two implementations disagree |
| `SPEC` | a rule we invented | we are inconsistent with ourselves |

Construction-time validation uses two record types, and both are required.
`DEGENERATE` rows must be rejected; `ACCEPT` rows must be accepted. A suite with
only the first is one-sided — an implementation that rejected every triangle
would pass it. See `DECISIONS.md` D-001.

There is no external reference implementation for this addon the way ofxOrtho
had a JavaScript one. The Python reference is therefore written from a different
formulation — Cramer's rule on the linear system, where the C++ uses signed
sub-triangle cross products — so that agreement is evidence rather than a
tautology.

`SPEC` vectors cover the per-node weight bias, which is our own rule. Green
there means self-consistent. It does not mean correct.

## Layout

```
src/core/       kernel, glm only, no ofMain.h
tests/ref/      Python reference implementation and vector generator
tests/vectors/  generated conformance vectors
libs/glm/       vendored, see libs/VENDORED.md
```
