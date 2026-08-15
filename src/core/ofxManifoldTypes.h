#pragma once

// ofxManifold — core types.
//
// This header, and everything else in src/core, must never include ofMain.h or
// any other openFrameworks header. glm is the only dependency, and glm is
// header-only and ships with openFrameworks, so core costs a consumer nothing.
//
// The discipline is the same as ofxOrtho: the kernel is the authority, the
// wrapper is translation. No geometry logic outside core, no drawing inside it.

#include <glm/vec2.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ofxManifold {

using NodeID   = std::uint32_t;
using RegionID = std::uint32_t;

static constexpr NodeID   InvalidNode   = std::numeric_limits<NodeID>::max();
static constexpr RegionID InvalidRegion = std::numeric_limits<RegionID>::max();

// Minimum absolute doubled signed area for a triangle to be constructible.
//
// Meaningful as a constant because the manifold lives in normalized coordinates
// (architecture doc §5): in pixel space it would silently change meaning with
// window size.
//
// The VALUE is set by float precision, not by taste. FLT_EPSILON is 1.19e-7,
// so for coordinate differences of order 1 the products in signedArea2() carry
// rounding noise around 1e-8. Any threshold below that is not a tolerance, it
// is noise.
//
// This was 1e-9f and was wrong. On Apple Silicon the compiler contracts
// a*b - c*d into a single FMA: the first product is computed exactly and the
// second is rounded, so a genuinely collinear triangle yields a residual of
// 4.17e-09 rather than zero. That cleared 1e-9 and the triangle was accepted.
// The same source on x86 emitted no FMA, returned exact zero, and passed --
// the Linux green was luck, not correctness.
//
// 1e-6f on the doubled area means a minimum triangle area of 5e-7. A 100x100
// normalized grid has triangles of doubled area 1e-4, two orders of magnitude
// clear, so legitimate fine meshes are unaffected. The ACCEPT vectors guard
// that end; without them, raising this constant could silently start rejecting
// valid topology with nothing to catch it.
static constexpr float kAreaEpsilon = 1e-6f;

// Containment tolerance on region edges, likewise normalized-space.
static constexpr float kEdgeEpsilon = 1e-6f;

// A node is a name, a position, and a scalar bias. Nothing else.
//
// There is deliberately no type field. Terminal, null and composite nodes
// (architecture doc §6.1) differ only in what the mapping layer resolves them
// to — zero, one, or many targets. The kernel cannot tell them apart and does
// not need to.
struct Node {
    std::string name;
    glm::vec2   position{0.0f, 0.0f};

    // Pre-normalization multiplier on this node's barycentric coordinate.
    // Applied before renormalization, so it genuinely alters interpolation and
    // cannot live in the interpretation layer. Carries no meaning here; the
    // convention that null nodes fade gently is an authoring decision.
    float weight = 1.0f;
};

struct WeightedNode {
    NodeID id     = InvalidNode;
    float  weight = 0.0f;
};

// Sparse, identity-retaining. The mapping layer provides the dense fixed-arity
// form that OSC consumers need.
struct Evaluation {
    RegionID regionID = InvalidRegion;
    bool     inside   = false;
    std::vector<WeightedNode> weights;
};

} // namespace ofxManifold
