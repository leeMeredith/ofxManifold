# ofxManifold

## OpenFrameworks Manifold Interpolation Engine

**Status:** Architectural Proposal — v2
**Primary Environment:** openFrameworks 0.12.1 / C++
**Consumers:** openFrameworks, SuperCollider, OSC, future applications
**Initial Focus:** 2D triangular manifolds and barycentric interpolation

**Revision note:** This version supersedes v1. It was revised after reading Steve
Ellison's *SpaceMap: 20 Years of Audio Origami* (Lighting & Sound America, April
2013) and Zachary Seldess's *MIAP: Manifold-Interface Amplitude Panning in
Max/MSP and Pure Data* (AES 137th Convention, 2014). Both sources changed
structural decisions, not merely details. Changes are marked **[v2]** where they
reverse or extend v1 on the authority of those sources, and **[new]** where they
depart from the prior art entirely — those have no precedent and carry
correspondingly more risk.

---

# 1. Purpose

`ofxManifold` is a general-purpose openFrameworks addon for defining interactive
control spaces and evaluating continuous weighted relationships within those
spaces.

The mathematical foundation is barycentric interpolation over a network of nodes
connected into regions. A point moving through those regions produces a
corresponding set of interpolation weights.

`ofxManifold` is **not a spatial-audio addon**. Spatial audio is one application
of the underlying abstraction.

The abstraction is:

> A point in a control space produces a vector of continuous weights over a set
> of named nodes.

Those weights can control anything: amplitude, synthesis parameters, rhythmic
density, pitch-set selection, reverberation, visual parameters, lighting, OSC
values, data visualization, sonification, routing, or morphing between musical or
visual states.

The goal is a reusable geometric control primitive that becomes infrastructure
for the larger studio.

## 1.1 How to describe this to someone who has never heard of SpaceMap

Most openFrameworks developers do not have twenty-four speakers. They do have a
pile of `ofParameter` values and four or five configurations they like. For that
reader the one-line description is:

> **A continuous preset-morphing interface.**

Place your presets as nodes. Drag a point between them. Get a weighted blend.

Spatial audio is the historically important application and the narrowest one.
The addon should lead with parameter morphing and treat panning as a specialized
case, not the reverse. This governs README order, example order, and the first
screenshot anyone sees.

---

# 2. Lineage

The approach originates with Steve Ellison, who in 1986 devised barycentric
amplitude panning for a 16-channel geodesic dome built by Floating Exceptions in
Canberra. The triangular arrangement of the dome's nodes suggested panning
between triplets of speakers rather than other groupings; barycentric coordinates
turned out to be a computationally cheap way to derive power-preserving gains
from that arrangement. Ellison arrived at the technique intuitively and only
later learned Möbius had introduced the underlying mathematics in 1827.

The software became SpaceNodes, then SpaceMap, and was commercialized through LCS
Audio and later Meyer Sound across four hardware generations. Seldess's MIAP
externals reimplemented the approach for Max/MSP and Pure Data, and generalized
several features in the process.

Two properties of that history matter for this project:

1. Every feature in SpaceMap exists because a production broke without it. This
   is use-case-driven design, not idealized design. The feature set is therefore
   worth treating as evidence rather than as one possible arrangement among many.
2. Seldess's port already generalizes past audio — he added alternative blend
   curves specifically because the resulting weights might drive plugin
   parameters or lighting-board faders. The generalization this document proposes
   is not a novel reading of the technique.

`ofxManifold` is an independent, generic manifold/interpolation library. It is
not a reproduction of a proprietary product, and it carries no Meyer Sound
terminology into its API.

---

# 3. Architectural Principle

> **Geometry should not know what its values mean.**

The manifold answers:

```text
Where am I?
What nodes influence this point?
By how much?
```

It does not answer:

```text
What should the sound do?
What should the light do?
What output should receive signal?
What note should be selected?
```

Those decisions belong to consumers.

**[v2]** The layer diagram gains a fourth stage. Interpretation and mapping are
distinct: interpretation reshapes the weight vector while it is still a weight
vector; mapping resolves it onto named targets.

```text
              Point Source
                    │
                    ▼
            ┌───────────────┐
            │   Manifold    │   geometry only
            └───────┬───────┘
                    │
              Weight Vector
                    │
            ┌───────▼───────┐
            │Interpretation │   curves, spread, blending
            └───────┬───────┘
                    │
              Weight Vector
                    │
            ┌───────▼───────┐
            │    Mapping    │   node → targets, aggregation
            └───────┬───────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
      Audio       Rhythm      Visuals
```

The signature consequence: interpretation is closed over weight vectors. Its
input and output are the same type. That is what allows curves, spread, and
inter-manifold blending to compose in any order.

---

# 4. Core Concept

A manifold consists of:

1. **Nodes** — named locations in control space
2. **Regions** — topology connecting those nodes
3. **A point** being evaluated
4. **A weight vector** describing the point's relationship to the nodes

For a triangular region:

```text
                 A
                / \
               /   \
              /  P  \
             /       \
            B---------C
```

Barycentric coordinates give:

```text
wA + wB + wC = 1
```

The manifold reports:

```text
A → 0.20
B → 0.50
C → 0.30
```

It does not know whether A, B, and C are speakers, rhythmic fields, filter
cutoffs, or shader uniforms.

---

# 5. Coordinate Space

**[v2] Decided: the manifold lives in normalized abstract coordinates. The
renderer owns the transform to screen.**

v1 serialized node positions as screen pixels. That is wrong, for a reason the
source material makes concrete.

In SpaceMap, a recorded trajectory survives a change of venue. The move renders
in real time, so a touring production builds a new map matching the new rig and
replays existing cues and trajectories unchanged. That portability requires that
trajectories live in the manifold's own coordinate space and that node positions
are the only thing that varies between venues.

Two further consequences:

- **Tolerances become meaningful.** An edge-containment epsilon of `1e-5` denotes
  something fixed in normalized space. In pixel space its meaning changes when
  the window resizes.
- **The map is not a projection of real space.** Nodes adjacent on the map may be
  the two most distant speakers in the room. Nothing in the geometry should
  encourage the author to believe otherwise.

The renderer applies design-space-to-screen transform at draw time. Same pattern
as the Juggler play field.

---

# 6. Node Taxonomy

**[v2] This section is substantially new and reverses a v1 assumption.**

SpaceMap's node taxonomy has four types. Under generalization they are not four
kinds of one thing. Three are participation modes in the geometry; the fourth is
a different object entirely.

## 6.1 Participation modes

All three are the *same struct* in the kernel. They differ only in what the
mapping layer resolves them to.

| Generalized term | SpaceMap term | Mapping cardinality |
|---|---|---|
| Terminal node | Speaker node | one target |
| Null node | Silent node | zero targets |
| Composite node | Virtual node | many targets, weighted |

Because the three collapse to `node → weighted target set` with cardinality zero,
one, or many, **the kernel needs no node type field at all.** A node is a name, a
position, and a scalar weight.

Two facts from the sources constrain the mapping layer:

- Multiple nodes in one manifold may bind to the same target. Node identity and
  target identity are independent. The relation is many-to-many.
- Composite nodes distribute proportionally to their linked targets, weighted
  equally by default, with per-link bias available.

## 6.2 Aggregators — not nodes

SpaceMap's Derived node runs the routing logic in reverse: rather than
distributing a signal among linked speaker nodes, it receives their linear sum.
It is not typically part of a triset, and its position on the map exists only for
visual clarity.

Generalized, this is an **aggregator**: a named reducer over a subset of the
weight vector, evaluated after the manifold. Sum or power-preserving sum, per
choice.

```text
        Weight Vector
              │
     ┌────────┴────────┐
     ▼                 ▼
  Mapping          Aggregator
     │                 │
     ▼                 ▼
  Targets         Derived value
```

**Aggregators do not belong in `Manifold2D`.** Modelling them as a node type
would put a non-participating object into the geometry and force every region
operation to filter it out. They live in the mapping layer and read the
manifold's output.

If the editor wants to draw an aggregator at a position, the editor stores that
position. The kernel never sees it.

## 6.3 Per-node weight

**[v2] New kernel field.**

SpaceMap biases the barycentric weight of silent nodes substantially so that
fades toward them are gradual rather than abrupt, and does not expose the value.
MIAP exposes it, defaulting to 1.

This multiplier is applied before renormalization and genuinely alters the
interpolation, so it cannot live in the interpretation layer. It generalizes
without carrying any meaning:

```cpp
struct Node {
    std::string name;
    glm::vec2   position;
    float       weight = 1.0f;   // pre-normalization bias
};
```

"Null nodes should fade gently" is then an authoring convention, not a special
case in the math. Add the field in v1 — it is free now and a signature change
later.

---

# 7. Regions

**[v2] Region becomes an interface. Triangle is one implementation.**

Seldess's stated future work goes beyond triangles: barycentric panning need not
be limited to three nodes, and he identified map centers and edges as places
where sets of four or more nodes would help. He also noted the extension to
three-dimensional convex subdivisions.

The important technical point is that this is **a different algorithm, not an
extension of the triangle solve.** Generalized barycentric coordinates over an
n-gon — Wachspress, mean value, or similar — have different degeneracy behavior
and different cost. Writing the triangle case as though a polygon case were a
parameter of it will produce a worse triangle implementation and will not
actually generalize.

So the requirement is interface, not generality:

```cpp
class Region {
public:
    virtual ~Region() = default;
    virtual bool contains(glm::vec2 p, float eps) const = 0;
    virtual void evaluate(glm::vec2 p, std::vector<WeightedNode>& out) const = 0;
    virtual const std::vector<NodeID>& nodes() const = 0;
};
```

`Triangle` implements it in v1. A polygon or tetrahedron can implement it later
without touching `Manifold2D`.

## 7.1 Triangle evaluation

Barycentric coordinates via signed sub-triangle areas. For point `P` in triangle
`ABC`, the three sub-triangles `PBC`, `PCA`, `PAB` are formed; normalized so
their areas sum to 1; each normalized area becomes the coordinate of the vertex
*not* contained in that sub-triangle.

```text
                 A
                / \
               / | \
              /  P  \
             / /   \ \
            B---------C

        area(PBC) → wA
        area(PCA) → wB
        area(PAB) → wC
```

Per-node weights multiply the coordinates before normalization.

## 7.2 Degeneracy

A triangle of zero or near-zero area divides by zero in the solve. This is a
construction error, not a runtime condition. **Reject at `addTriangle()`.** Do
not check per-evaluate.

## 7.3 Conforming topology

Weight continuity across a shared edge holds only if the edge is shared *whole*.
A T-junction — where one triangle's edge meets the midpoint of another's — makes
weights jump discontinuously as the point crosses it.

The kernel must validate this and report it, in the same spirit as the Turing
regime health reporting. A silent discontinuity in a control signal is the audio
equivalent of a corrupted identity buffer: correct-looking output over a broken
foundation, discovered during a performance.

```cpp
struct TJunction {
    NodeID   node;                  // the offending node
    RegionID region;
    NodeID   edgeA, edgeB;          // the edge it sits on
};

struct TopologyReport {
    std::vector<TJunction> tJunctions;
    std::vector<NodeID>    orphans;      // in no region
    std::vector<RegionID>  duplicates;   // same three nodes as an earlier region
    bool clean() const;
};
```

**[v2]** `degenerate` is gone: degeneracy is refused at `addTriangle()`, so a
constructed manifold cannot contain one and a field reporting them would always
be empty. `duplicates` replaces it. Two regions over the same three nodes make
first-hit order the only thing distinguishing them, which is a coin flip dressed
as a decision.

A T-junction record names the node, the region, and the edge, rather than an
opaque edge reference. The author needs to know which node to move.

---

# 8. Evaluation and Concurrency

**[v2] Evaluation state moves out of the manifold.**

SpaceMap installations run many concurrent sources through one map. KÀ used more
than thirty maps as purpose-built panners, with multiple sources moving
simultaneously.

The obvious optimization — cache the last containing region and test it first —
is correct and is the entire performance story for realistic node counts. But
that cache cannot live on the manifold if N sources share it. It also provides
free hysteresis where regions overlap, keeping a point in the region it was
already in.

Therefore:

```cpp
class Manifold2D {            // shared, const during evaluation
public:
    NodeID   addNode(const std::string& name, glm::vec2 pos, float w = 1.0f);
    RegionID addTriangle(NodeID a, NodeID b, NodeID c);
    void     removeNode(NodeID);        // cascades to regions
    void     removeRegion(RegionID);

    TopologyReport validate() const;

    Evaluation evaluate(glm::vec2 p, RegionID hint = InvalidRegion) const;
};

class Evaluator {             // one per source, holds the hint
public:
    explicit Evaluator(const Manifold2D& m);
    Evaluation evaluate(glm::vec2 p);
private:
    const Manifold2D& manifold;
    RegionID lastRegion = InvalidRegion;
};
```

`Manifold2D::evaluate` stays available and stateless for one-shot use. `Evaluator`
is the performance path.

## 8.1 Result shape

The kernel returns sparse results retaining node identity:

```cpp
struct WeightedNode { NodeID id; float weight; };

struct Evaluation {
    RegionID regionID;              // InvalidRegion if outside
    bool     inside;
    std::vector<WeightedNode> weights;
};
```

The mapping layer provides `toDenseVector()` for consumers — OSC in particular —
that need fixed arity so array indices stay put across frames.

## 8.2 Outside the hull

**[v2] This is no longer a policy decision.**

v1 treated fall-off-the-map as an open question. The historical answer is an
authoring convention: ring the map with null nodes so power distributes to
nothing and fades are ordinary barycentric behavior. SpaceMap added silent nodes
precisely because sound dragged off the map edge cut out abruptly.

So: return `inside == false` with an empty weight vector, document the ringing
convention, and write no clamping policy. A well-authored map rarely reaches the
outside case.

## 8.3 Overlapping regions

Arbitrary node networks permit overlapping regions by design. First hit wins,
with the `Evaluator` hint tested first — deterministic, and hysteretic in
practice.

On a shared edge both regions report identical weights, since the opposite
vertex's coordinate is zero. That case is free.

## 8.4 Handle stability

Integer-index IDs break every saved map and every region reference on removal.
Use generation-counted handles internally and stable string names in the
serialized format, resolved to handles at load. Node removal cascades to regions
referencing it; the cascade is reported, not silent.

## 8.5 Inverse evaluation

**[new] No precedent in the prior art.**

Nothing in SpaceMap or MIAP exposes weights → position, because a sound designer
never needs it: the point *is* the interface, and it comes from a fader, a
trajectory, or a tracker.

When the manifold is used for morphing rather than panning, the mixture is often
known before the position is.

**[v2] It is not simply the weighted sum of node positions.** Forward evaluation
applies per-node bias (§6.3) and renormalizes, so the emitted weights are no
longer the barycentric coordinates of the point. Summing directly against them
lands elsewhere, silently, on any biased manifold. The bias is exactly
invertible — `raw` is proportional to `b / nodeWeight` — so `positionOf()` undoes
it before combining. See DECISIONS.md D-002.

```cpp
struct InversePosition {
    glm::vec2 position;
    bool      wellPosed;   // false if weights span disjoint regions
};

InversePosition Manifold2D::positionOf(const WeightVector&) const;
```

It is not always well-posed. Weights spanning nodes in no common region produce a
centroid that may lie inside a different region entirely, or outside the hull.
Hence the flag rather than a bare `glm::vec2`.

What it enables:

* named mixtures the control point can snap to, defined in weight space
* trajectories authored as weight-space endpoints rather than screen coordinates
* editor affordance: type the blend you want, see where it lives

And, usefully, a **round-trip conformance vector**: a point inside a single
region, evaluated forward and then inverted, must return to itself within
tolerance. That is a free check on the forward path.

## 8.6 Animated node positions

**[new] No precedent in the prior art.** Speakers do not move, so nobody asked.

Nodes here have no physical referent at all — the Yantra map (§17) already
demonstrated that. So there is no reason the map must be static. A manifold could
be generated from a Go board position, a flock's current formation, or ridge
extraction on a Turing field. The control space itself becomes something the
existing systems produce.

The invariant that makes this safe:

> **Topology is discrete and edited. Positions are continuous and animated.**

If the region set does not change and the point holds still, weights vary
continuously as nodes move. There is exactly one failure mode: a node moving far
enough to invert a triangle drives the signed area through zero, and weights blow
up on the way.

That is checkable. The animated counterpart to construction-time degeneracy
rejection is a per-frame assertion:

```cpp
bool Manifold2D::setNodePosition(NodeID, glm::vec2);   // false if any
                                                       // incident region's
                                                       // signed area flips
```

Two obligations this places on v1, both free if honored now:

1. `Manifold2D` must cache nothing derived from node positions that
   `setNodePosition()` would not invalidate.
2. Signed area is computed, not assumed, and its sign is retained per region at
   construction so the flip test has something to compare against.

The generative sources themselves are later work. The setter and the invariant
are v1, because retrofitting them means auditing every cache.

---

# 9. Interpretation Layer

**[v2] Section substantially expanded. Previously this was equal-power only.**

Interpretation reshapes a weight vector into another weight vector. Everything
here is optional and composable.

## 9.1 Response curves

Equal-power gain is one curve:

```text
gain = sqrt(w)
```

It should not be in the evaluator. MIAP made this explicit by offering cosine,
square root, and linear curves — Seldess added the alternatives because the
weights might be driving plugin parameters or lighting faders rather than audio,
where constant-power is meaningless or wrong.

```cpp
namespace curve {
    float linear(float);      // identity, for parameter positions
    float equalPower(float);  // sqrt, constant acoustic power
    float cosine(float);      // SpaceMap's original shape

    WeightVector apply(const WeightVector&, Fn);
}

WeightVector normalize(const WeightVector&);   // explicit, never automatic
float power(const WeightVector&);              // sum of squares
float sum(const WeightVector&);
```

**[v2] A curve does not preserve partition of unity and must not renormalize.**
Barycentric weights sum to one; equal-power gains preserve the sum of *squares*
instead. Renormalizing after a curve would undo precisely the property the curve
exists to create, turning it into an expensive identity function. `normalize()`
is available for callers who want the sum back and accept losing the power
property. See DECISIONS.md D-003.

## 9.2 Spread

SpaceMap's "divergence" distributes power to all nodes in a map, power-preserving,
by a controllable amount. Generalized: a spread parameter interpolating between
the evaluated weight vector and a uniform vector over all nodes.

At spread 0, the point is localized. At spread 1, it is everywhere. This is the
mechanism by which multiple concurrent 2D maps produce three-dimensional
behavior, and it is a useful morphing control in its own right.

## 9.3 Blending manifolds

MIAP objects hold two maps and crossfade their weight vectors. This is worth
designing for from the start, and it is why interpretation is closed over weight
vectors.

```cpp
WeightVector blend(const WeightVector& a,
                   const WeightVector& b,
                   float t,
                   CurveFn curve);
```

The natural unit of composition is the weight vector, not the manifold. Several
manifolds evaluated concurrently and blended downstream is the design.

**[v2]** This replaces v1 §17's proposal to reach three dimensions via tetrahedra.
The historical solution to 3D was layered 2D maps plus spread, which is cheaper,
authorable, and already expressive. Tetrahedra remain possible through the
`Region` interface but are explicitly not on the roadmap.

---

# 10. Mapping Layer

Mapping resolves node identity onto external targets. It is the only layer that
knows what anything means.

```cpp
struct TargetLink { TargetID target; float weight; };

class Mapping {
public:
    void bind(NodeID, TargetID, float weight = 1.0f);   // repeatable
    void unbind(NodeID, TargetID);

    void addAggregator(const std::string& name,
                       std::vector<NodeID> sources,
                       SumMode mode);

    Resolved resolve(const WeightVector&) const;
    std::vector<float> toDenseVector(const WeightVector&) const;
};
```

`bind` being repeatable per node covers terminal, null, and composite nodes with
no type switch. Zero binds is a null node. One bind is terminal. Many binds is
composite.

Mapping is serialized separately from the manifold. A manifold is portable; a
mapping is installation-specific. This is what makes one map reusable across
venues, and what lets the same map drive audio in one application and shader
uniforms in another.

---

# 11. Point Sources and Trajectories

**[v2] New section. v1 mentioned trajectories only as a visualization feature.**

Trajectories are first-class in SpaceMap: recordable, storable, attachable to
cues, mappable to timecode, rendered in real time. This is the feature that makes
the system usable in a theater rather than in a demo.

But the general form is not time-based. In KÀ, a bubble effect's position was
bound to a console fader so the operator could track a performer swimming
onstage. In *Soldaat van Oranje*, rotational coordinates from the audience's
rotating platform were fed in as the control input.

So the abstraction is a point generator:

```cpp
class PointSource {
public:
    virtual ~PointSource() = default;
    virtual glm::vec2 pointAt(float t) = 0;
};
```

where `t` is time, a fader value, a normalized phase, a blob centroid's
parameterization, or a platform angle. Recorded trajectories, LFOs, tracking
input, and OSC input are all implementations.

This is a **sibling module, not kernel**. The requirement on the kernel is only
that evaluation be cheap and stateless enough to be driven from a playback engine
at control rate, and that the point be pure data.

---

# 12. Smoothing

**[v2] New section. Named in neither v1 nor the source documents' architecture,
but present in MIAP's implementation.**

MIAP splits a control-rate panner from an audio-rate panner; the audio-rate
version applies sample-rate interpolation to the panned signal.

That is smoothing, and it is a real requirement. A point that jumps
discontinuously produces a weight vector that jumps: zipper noise in audio,
popping in visuals.

The manifold evaluates instantaneously and correctly. **Something downstream must
slew.** This belongs to the consumer, but it is named here so it is not
discovered during a performance.

A `WeightSmoother` with per-frame slew limiting is a reasonable utility to ship
alongside the addon without putting it in the core.

---

# 13. Visualization

The renderer is a first-class part of the addon and strictly separate from the
mathematical core.

**[v2] The renderer references a manifold; it does not own one.** MIAP shares map
data between panner and UI objects through a common namespace, in the manner of
`buffer~` and `groove~`. A manifold is a shared resource with many independent
readers — some evaluating, some drawing.

```cpp
class ManifoldRenderer {
public:
    explicit ManifoldRenderer(const Manifold2D& m);
    void draw(const ofRectangle& viewport) const;
    void drawEvaluation(const Evaluation&, const ofRectangle&) const;
};
```

Features:

* nodes, labels, edges, filled regions
* active region highlight
* control point
* live weight readout
* weight magnitude visualization
* topology warnings drawn in place (T-junctions, degenerate regions, orphans)
* mapped targets
* trajectories

```text
                 A
                / \
               /   \
              /  P  \
             /       \
            B---------C

          A   0.18
          B   0.47
          C   0.35
```

Making interpolation behavior inspectable is the main argument for building this
in openFrameworks rather than as a headless library.

---

# 14. Why openFrameworks

The manifold is fundamentally graphical: nodes are displayed, regions drawn,
points moved, weights inspected, maps edited, trajectories displayed, tracking
input supplied.

SuperCollider remains responsible for the audio engine.

```text
                 openFrameworks
              authoring / geometry
                       │
                       │ OSC / map data
                       ▼
                 SuperCollider
                    audio DSP
```

Neither environment takes on the other's job.

---

# 15. Serialization

JSON. The manifold serializes independently of any application, and independently
of its mapping.

```json
{
  "version": 1,
  "space": "normalized",
  "nodes": [
    { "id": "A", "position": [0.20, 0.20], "weight": 1.0 },
    { "id": "B", "position": [0.80, 0.20], "weight": 1.0 },
    { "id": "C", "position": [0.50, 0.80], "weight": 1.0 }
  ],
  "triangles": [
    ["A", "B", "C"]
  ]
}
```

Mapping is a separate file:

```json
{
  "version": 1,
  "bindings": [
    { "node": "A", "target": "out.1", "weight": 1.0 },
    { "node": "B", "target": "out.2", "weight": 1.0 },
    { "node": "C", "target": "out.3", "weight": 1.0 }
  ],
  "aggregators": [
    { "name": "sub.house", "sources": ["A", "B"], "mode": "linear" }
  ]
}
```

The `version` field is present from the first release. The `space` field is
present so that a future non-normalized variant is detectable rather than
silently misread.

Neither file embeds SuperCollider-specific or openFrameworks-specific behavior.

---

# 16. Integration Modes

## Mode A — openFrameworks evaluates

```text
tracking input → oF → manifold → weights → OSC → SuperCollider → audio
```

```text
/manifold/weights 0.12 0.64 0.38 0.00
```

Dense fixed-arity vector so indices stay put. Useful when openFrameworks already
owns tracking or visual interaction.

## Mode B — openFrameworks sends the point

```text
oF → OSC point → SuperCollider manifold runtime → weights → audio
```

Useful when SuperCollider should own the complete audio-side mapping.

Both modes coexist. The serialization format is what makes them interchangeable.

---

# 17. Application Sketches

The manifold does not know about any of these.

**Barlow fields.** Nodes reference fields; weights interpolate between them.

```text
Control Position → Manifold → Weights → Fields → Musical Material
```

**Blob tracking.** A centroid becomes the control point.

```text
Blob → Centroid → glm::vec2 → Manifold → Weights
```

Those weights can simultaneously drive speaker gains, rhythmic density, pitch
selection, brightness, reverb, and visual parameters — one continuous gesture,
several independent dimensions, no hardcoded relationships in the tracking
system.

**Abstract geometry as instrument.** Gresham-Lancaster mapped the intersections
of a Tibetan Yantra: some intersections were real channels, every other
intersection virtual. The result was not smooth A-to-B movement but sheets and
planes of sound collapsing into points. This is the strongest existing evidence
that the geometry is genuinely independent of spatial meaning, and it is the mode
of use closest to the myEyePrimitive/stokes aesthetic.

---

# 18. Project Structure

```text
ofxManifold/
│
├── src/
│   ├── core/                          no oF dependency
│   │   ├── ofxManifoldNode.h
│   │   ├── ofxManifoldRegion.h        interface
│   │   ├── ofxManifoldTriangle.h
│   │   ├── ofxManifold2D.h
│   │   ├── ofxManifoldEvaluator.h
│   │   ├── ofxManifoldEvaluation.h
│   │   └── ofxManifoldGeometry.h
│   │
│   ├── interpretation/
│   │   ├── ofxManifoldCurves.h
│   │   ├── ofxManifoldSpread.h
│   │   └── ofxManifoldBlend.h
│   │
│   ├── mapping/
│   │   ├── ofxManifoldMapping.h
│   │   └── ofxManifoldAggregator.h
│   │
│   ├── sources/
│   │   ├── ofxManifoldPointSource.h
│   │   └── ofxManifoldTrajectory.h
│   │
│   ├── io/
│   │   └── ofxManifoldJSON.h
│   │
│   └── ofx/                           oF-dependent wrapper
│       ├── ofxManifoldRenderer.h
│       └── ofxManifoldSmoother.h
│
├── tests/
│   └── vectors/                       conformance vectors
│
├── examples/
│   ├── basicManifold/          point in, weights out
│   ├── parameterMorphing/      the one that explains the addon
│   ├── interactiveEditor/
│   ├── oscOutput/
│   └── equalPowerPanning/      the specialized case, last
│
└── README.md
```

`core/` uses only glm, which ships with openFrameworks and is header-only.
Nothing in `core/` includes `ofMain.h`. This is the same discipline as ofxOrtho:
translation in the wrapper, not interpretation.

---

# 19. Version 1 Scope

**[v2] Cut tighter than v1.**

```text
Nodes with position and weight
     ↓
Triangles, validated at construction
     ↓
Topology report
     ↓
Point in → containing region
     ↓
Barycentric weights, per-node bias applied
     ↓
Sparse Evaluation with node identity
     ↓
Conformance vectors green
```

No JSON. No drawing. No openFrameworks. Just the kernel and its test vectors.

Then, in order:

1. `Evaluator` and the region hint
2. Renderer and the interactive point
3. Mapping and dense vector output
4. Serialization — last, once nothing about the format will need migrating

## 19.1 Conformance vectors

Following the ofxOrtho pattern: point in, expected region and weights out, checked
against tolerance. Cases to cover from the start:

* centroid of a single triangle → equal thirds
* each vertex → 1.0 at that node
* edge midpoint → 0.5 / 0.5 / 0.0
* shared edge between two triangles → identical weights from both
* point outside the hull → `inside == false`, empty
* per-node weight ≠ 1 → verified renormalization
* degenerate triangle → rejected at construction
* T-junction topology → reported by `validate()`
* round trip — point → weights → `positionOf()` → same point, within tolerance
* weights spanning disjoint regions → `wellPosed == false`
* `setNodePosition()` inverting an incident triangle → rejected, sign retained

These must be green before any wrapper code exists.

---

# 20. Non-Goals for v1

* audio DSP
* SuperCollider-specific or Max-specific classes
* polygonal or tetrahedral regions
* automatic triangulation
* sophisticated graphical editing
* arbitrary-dimensional manifolds
* persistence of application state
* performance-specific behavior

The first milestone proves the mathematical and architectural core. Nothing else.

---

# 21. Open Questions

Not blocking v1, but unresolved:

1. **Aggregator sum modes.** Linear and power-preserving are both wanted. Whether
   a general reducer function is worth the interface, or two named modes suffice.
2. **Editor topology assistance.** Whether the editor should prevent T-junctions
   at authoring time or only report them. Reporting is the v1 answer by default.
3. **Trajectory storage format.** Sampled points versus parametric curves.
   Deferred until the point-source module.
4. **Smoother placement.** Whether `WeightSmoother` ships in the addon or in the
   consuming application.
5. **GPU bake. [new], speculative, not roadmapped.** Everything in the lineage
   evaluates one point at a time on the CPU, because sources are countable. A
   manifold baked to a lookup texture — region ID, three barycentric
   coordinates, three node indices, packing into two RGBA16 attachments — would
   make evaluation free for arbitrarily many points and give the renderer its
   fill for nothing.

   This is an identity buffer, with all that implies: **no filtering, no
   blending, no MSAA.** The failure signature is the familiar one — plausible
   weights over a corrupted region ID, identical in both paths, invisible until
   it isn't. If built, it is a standalone module verified against the CPU
   evaluator vector-for-vector before anything depends on it.

   The only thing v1 owes this idea is not foreclosing it: keep `Triangle`'s
   solve a pure function of three positions and a point, so the same arithmetic
   transcribes to GLSL without restructuring.

---

# 22. Guiding Principle

> **The manifold describes relationships, not meanings.**

A point has a location.
A node has an identity and a weight.
A region defines interpolation.
The evaluator produces a weight vector.

Everything after that is interpretation.

That separation is what allows one mathematical engine to become a spatial audio
system, a musical morphing system, a visual control system, or something not yet
conceived — and it is the reason this is an openFrameworks addon rather than a
panner buried inside a SuperCollider project.
