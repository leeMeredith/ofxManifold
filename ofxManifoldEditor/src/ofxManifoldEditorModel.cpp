#include "ofxManifoldEditorModel.h"

#include "authoring/ofxManifoldRing.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ofxManifold {
namespace editor {

namespace {

Result refusal(const std::string& msg, float seconds) {
    Result r;
    r.refused = true;
    r.message = msg;
    r.seconds = seconds;
    return r;
}

Result changed(const std::string& msg, float seconds) {
    Result r;
    r.changed = true;
    r.message = msg;
    r.seconds = seconds;
    return r;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + (n == 1 ? one : many);
}

} // namespace

// ---------------------------------------------------------------------------
// whole maps
// ---------------------------------------------------------------------------

void Model::loadExample() {
    manifold_ = Manifold2D();

    // A fan of four triangles...
    const auto O = manifold_.addNode("O", {0.30f, 0.50f});
    const auto N = manifold_.addNode("N", {0.30f, 0.80f});
    const auto E = manifold_.addNode("E", {0.50f, 0.50f});
    const auto S = manifold_.addNode("S", {0.30f, 0.20f});
    const auto W = manifold_.addNode("W", {0.10f, 0.50f});
    manifold_.addTriangle(O, N, E);
    manifold_.addTriangle(O, E, S);
    manifold_.addTriangle(O, S, W);
    manifold_.addTriangle(O, W, N);

    // ...and a quad. Drag one of its corners across the opposite edge and the
    // move is refused: the ring would cross itself (D-019).
    const auto A = manifold_.addNode("A", {0.65f, 0.35f});
    const auto B = manifold_.addNode("B", {0.90f, 0.35f});
    const auto C = manifold_.addNode("C", {0.90f, 0.65f});
    const auto D = manifold_.addNode("D", {0.65f, 0.65f});
    manifold_.addRegion({A, B, C, D});

    // Outputs, and bindings chosen so every node shape appears at once:
    //
    //   N W E S   terminal, one output each      filled circle
    //   O         composite, all four            filled triangle
    //   A         bound to nothing               hollow square
    //   B         right, and half to silence     circle, half filled
    //   C         left and right                 triangle
    //   D         rear, and a quarter silent     circle, three-quarters filled
    //
    // plus a derived "sub" fed by N and O, at its own channel.
    mapping_ = Mapping();
    const TargetID front = mapping_.addOutput("front", 0);
    const TargetID left  = mapping_.addOutput("left", 1);
    const TargetID right = mapping_.addOutput("right", 2);
    const TargetID rear  = mapping_.addOutput("rear", 3);
    mapping_.bind(N, front);
    mapping_.bind(W, left);
    mapping_.bind(E, right);
    mapping_.bind(S, rear);
    for (TargetID t : {front, left, right, rear}) mapping_.bind(O, t);
    mapping_.bind(B, right);
    mapping_.bindSilence(B, 1.0f);
    mapping_.bind(C, left);
    mapping_.bind(C, right);
    mapping_.bind(D, rear);
    mapping_.bindSilence(D, 1.0f / 3.0f);
    mapping_.addDerived("sub", 4, 1.0f, SumMode::Linear, {{N, 1.0f}, {O, 1.0f}});
    (void)A;

    currentOutput_ = front;
    settleCurrentOutput();
    refreshTopology();
}

void Model::newEmpty() {
    manifold_ = Manifold2D();
    mapping_ = Mapping();
    currentOutput_ = InvalidTarget;
    settleCurrentOutput();
    refreshTopology();
}

void Model::adopt(Manifold2D m, Mapping mp) {
    manifold_ = std::move(m);
    mapping_ = std::move(mp);
    currentOutput_ = InvalidTarget;
    settleCurrentOutput();
    refreshTopology();
}

// The current output must name an output that exists; with any outputs at all
// and none current, the first becomes current.
void Model::settleCurrentOutput() {
    if (currentOutput_ != InvalidTarget
        && currentOutput_ >= mapping_.targetCount()) {
        currentOutput_ = InvalidTarget;
    }
    if (currentOutput_ == InvalidTarget && mapping_.targetCount() > 0) {
        currentOutput_ = 0;
    }
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

// A name free as a NODE name and as an OUTPUT name. An output can outlive the
// node it was made for, when other nodes still feed it; a new node given that
// name would find its auto-output already taken and be left unbound without a
// word.
std::string Model::freshName() const {
    for (std::size_t k = manifold_.nodeCount(); ; ++k) {
        const std::string nm = "n" + std::to_string(k);
        if (manifold_.findNode(nm) == InvalidNode
            && mapping_.findTarget(nm) == InvalidTarget) {
            return nm;
        }
    }
}

int Model::nextFreeChannel() const {
    int ch = 0;
    while (mapping_.channelInUse(ch)) ++ch;
    return ch;
}

std::size_t Model::outputCount(NodeID id) const {
    std::size_t n = 0;
    for (std::size_t k = 0; k < mapping_.linkCount(id); ++k) {
        if (mapping_.link(id, k).kind == DestKind::Output) ++n;
    }
    return n;
}

bool Model::outputInUse(TargetID t) const {
    for (NodeID n : mapping_.boundNodes()) {
        for (std::size_t k = 0; k < mapping_.linkCount(n); ++k) {
            const Link& l = mapping_.link(n, k);
            if (l.kind == DestKind::Output && l.id == t) return true;
        }
    }
    return false;
}

// Duplicate detection: by grid address where the grid has addresses, by a
// distance tolerance in free mode, which has none (authoring decision F).
bool Model::occupied(glm::vec2 p, GridAddress addr, const Grid& grid) const {
    for (std::size_t i = 0; i < manifold_.nodeCount(); ++i) {
        const glm::vec2 q = manifold_.node(NodeID(i)).position;
        if (addr.valid) {
            if (grid.snap(q).address == addr
                && glm::distance(grid.point(addr), q) < 1e-4f) {
                return true;
            }
        } else if (glm::distance(p, q) < 0.01f) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// nodes and regions
// ---------------------------------------------------------------------------

Result Model::place(glm::vec2 p, const Grid& grid, NodeID* placed) {
    const SnapResult s = grid.snap(p);
    if (s.point.x < 0.0f || s.point.x > 1.0f
        || s.point.y < 0.0f || s.point.y > 1.0f) {
        return Result{};                       // off the map: nothing to say
    }
    if (occupied(s.point, s.address, grid)) {
        return refusal("a node is already there", 1.5f);
    }
    const NodeID id = manifold_.addNode(freshName(), s.point);
    if (id == InvalidNode) return Result{};

    // The output takes the NODE'S NAME. That is what pairs them, and it
    // survives save and reload with no extra bookkeeping -- which is how
    // deleting the node later finds its own fader.
    if (autoOutput_ && mapping_.findTarget(manifold_.node(id).name)
                          == InvalidTarget) {
        const TargetID t = mapping_.addOutput(manifold_.node(id).name,
                                             nextFreeChannel());
        if (t != InvalidTarget) {
            mapping_.bind(id, t);
            currentOutput_ = t;
        }
    }
    refreshTopology();
    if (placed) *placed = id;
    Result r;
    r.changed = true;
    return r;
}

// One operation, all or nothing, checked against FINAL shapes. On refusal
// nothing changes, so the nodes stay at their last valid positions.
Result Model::move(const std::vector<std::pair<NodeID, glm::vec2>>& moves) {
    if (!manifold_.setNodePositions(moves)) {
        return refusal(
            "move refused: a region would invert, flatten or cross itself",
            1.5f);
    }
    Result r;
    r.changed = true;
    return r;
}

// Join the selected nodes into one region. ORDER decides the shape:
//
//   chosen by individual clicks  -> the order clicked
//   any part chosen by a box     -> angle about the centroid
//
// Angle order recovers every convex shape and every star, but not an L -- for
// an L it makes a different ring that is still a legal region. A click-ordered
// ring that crosses itself is REFUSED with the reason, never quietly re-sorted.
Result Model::join(const Selection& sel) {
    if (sel.size() < 3) {
        return refusal("select at least three nodes to join", 2.0f);
    }
    std::vector<NodeID> ring = sel.nodes();
    if (!sel.ordered()) {
        std::vector<glm::vec2> pts;
        for (NodeID id : sel.nodes()) pts.push_back(manifold_.node(id).position);
        ring.clear();
        for (std::size_t i : orderByAngle(pts)) ring.push_back(sel.nodes()[i]);
    }

    // The same set of nodes as an existing region is a duplicate; refused here
    // rather than created and reported by validate().
    std::vector<NodeID> key = ring;
    std::sort(key.begin(), key.end());
    for (std::size_t r = 0; r < manifold_.regionCount(); ++r) {
        std::vector<NodeID> have = manifold_.region(RegionID(r)).ids();
        std::sort(have.begin(), have.end());
        if (have == key) {
            return refusal("those nodes already form a region", 2.0f);
        }
    }

    if (manifold_.addRegion(ring) == InvalidRegion) {
        return refusal("refused: " + manifold_.lastRegionError()
                       + (sel.ordered() ? " (in click order)" : ""), 3.0f);
    }
    refreshTopology();
    return changed("joined " + std::to_string(ring.size()) + " nodes in "
                   + (sel.ordered() ? "click order" : "angle order"), 2.0f);
}

// Remove every region whose nodes are ALL selected, keeping the nodes.
Result Model::unjoin(const Selection& sel) {
    std::vector<RegionID> doomed;
    for (std::size_t r = 0; r < manifold_.regionCount(); ++r) {
        bool all = true;
        for (NodeID id : manifold_.region(RegionID(r)).ids()) {
            if (!sel.contains(id)) { all = false; break; }
        }
        if (all) doomed.push_back(RegionID(r));
    }
    if (doomed.empty()) {
        return refusal("no region is made only of selected nodes", 2.5f);
    }
    manifold_.removeRegions(doomed);
    refreshTopology();
    return changed("unjoined " + plural(doomed.size(), " region", " regions"),
                   2.5f);
}

Result Model::remove(const Selection& sel) {
    if (sel.empty()) return Result{};
    const std::size_t n = sel.size();
    std::size_t regionsGone = 0;

    // NodeIDs after the first removed one are renumbered. The mapping holds
    // NodeIDs, so it MUST follow, or every binding after the deleted node
    // would silently move to the wrong node (outputs decision E, D-020).
    std::vector<std::string> names;
    for (NodeID id : sel.nodes()) names.push_back(manifold_.node(id).name);
    std::vector<NodeID> remap;
    manifold_.removeNodes(sel.nodes(), &remap, &regionsGone);
    mapping_.remapNodes(remap);

    // A node's own output -- the one named after it -- goes with it, but ONLY
    // if nothing else is bound to it. Otherwise deleting a speaker's node
    // would silently take away a fader other nodes still feed. Outputs made
    // by hand never share a node's name, so they are never removed this way.
    std::size_t outsGone = 0;
    for (const std::string& nm : names) {
        const TargetID t = mapping_.findTarget(nm);
        if (t != InvalidTarget && !outputInUse(t)) {
            mapping_.removeOutput(t);
            afterOutputRemoved(t);
            ++outsGone;
        }
    }
    refreshTopology();
    return changed("removed " + plural(n, " node", " nodes")
                   + (regionsGone ? ", and "
                                    + plural(regionsGone, " region", " regions")
                                    + " that used them"
                                  : std::string())
                   + (outsGone ? ", and "
                                 + plural(outsGone, " output", " outputs")
                               : std::string()),
                   3.0f);
}

// ---------------------------------------------------------------------------
// outputs
// ---------------------------------------------------------------------------

Result Model::newOutput() {
    const int ch = nextFreeChannel();
    std::string nm = "out" + std::to_string(ch);
    for (int k = 2; mapping_.findTarget(nm) != InvalidTarget; ++k) {
        nm = "out" + std::to_string(ch) + "_" + std::to_string(k);
    }
    currentOutput_ = mapping_.addOutput(nm, ch);
    return changed("new output " + nm + " on channel " + std::to_string(ch)
                   + " -- select nodes and press b", 3.0f);
}

// A derived output summing the selected nodes: the sub under a pair, say. Not
// a node -- it has no position -- so it appears in the panel and the chart.
Result Model::newDerived(const Selection& sel) {
    if (sel.empty()) {
        return refusal("select the nodes a derived output should sum", 2.5f);
    }
    const int ch = nextFreeChannel();
    const std::string nm = "sum" + std::to_string(ch);
    std::vector<std::pair<NodeID, float>> srcs;
    for (NodeID id : sel.nodes()) srcs.emplace_back(id, 1.0f);
    mapping_.addDerived(nm, ch, 1.0f, SumMode::Linear, srcs);
    return changed("derived " + nm + " on channel " + std::to_string(ch)
                   + ", sum of " + std::to_string(srcs.size()), 3.0f);
}

Result Model::bind(const Selection& sel) {
    if (currentOutput_ == InvalidTarget) {
        return refusal("no output yet -- press o to make one", 2.5f);
    }
    if (sel.empty()) return Result{};
    for (NodeID id : sel.nodes()) mapping_.bind(id, currentOutput_);
    return changed("bound " + std::to_string(sel.size()) + " to "
                   + mapping_.targetName(currentOutput_), 2.0f);
}

Result Model::stepFill(const Selection& sel, int direction, bool fine) {
    if (sel.empty()) return Result{};
    const float unit = fine ? 0.01f : 0.05f;
    bool any = false, moved = false;
    float lo = 2.0f, hi = -1.0f;
    for (NodeID id : sel.nodes()) {
        if (outputCount(id) == 0) continue;
        any = true;
        const float before = mapping_.outputFraction(id);

        // Position in steps. A value already on a step -- within float noise
        // -- moves a whole step; one between steps moves to the next step in
        // its direction. Without the snap, 70% read back as 69.99999% and a
        // step down would land on 65%... from 69.99999, skipping 70.
        float k = before / unit;
        const float kr = std::round(k);
        if (std::fabs(k - kr) < 1e-3f) k = kr;
        const float next = (k == kr) ? kr + float(direction)
                         : (direction > 0 ? std::ceil(k) : std::floor(k));
        const float f = std::min(1.0f, std::max(0.01f, next * unit));

        mapping_.setOutputFraction(id, f);
        const float after = mapping_.outputFraction(id);
        if (std::fabs(after - before) > 1e-6f) moved = true;
        lo = std::min(lo, after);
        hi = std::max(hi, after);
    }
    if (!any) {
        return refusal("no output to fill -- bind the node first", 2.5f);
    }
    auto pct = [](float v) { return std::to_string(int(std::lround(v * 100.0f))); };
    Result r;
    r.changed = moved;
    r.message = "fill " + pct(lo) + "%"
              + (pct(lo) == pct(hi) ? std::string() : " to " + pct(hi) + "%");
    r.seconds = 2.0f;
    return r;
}

// Binding ADDS weight, so binding a node already feeding one output to every
// output would feed that one double. This REPLACES the node's output bindings
// with one equal binding per output, then restores its fill -- "this node
// feeds everything, equally, as loudly as before".
Result Model::bindAll(const Selection& sel) {
    if (mapping_.targetCount() == 0) {
        return refusal("no output yet -- press o to make one", 2.5f);
    }
    if (sel.empty()) return Result{};
    for (NodeID id : sel.nodes()) {
        const float fill = outputCount(id) > 0 ? mapping_.outputFraction(id)
                                               : 1.0f;
        mapping_.clearBindings(id);
        for (TargetID t = 0; t < mapping_.targetCount(); ++t) {
            mapping_.bind(id, t);
        }
        if (fill < 1.0f) mapping_.setOutputFraction(id, fill);
    }
    return changed("bound " + plural(sel.size(), " node", " nodes")
                   + " to every output (" + std::to_string(mapping_.targetCount())
                   + ")", 2.5f);
}

Result Model::clear(const Selection& sel) {
    for (NodeID id : sel.nodes()) mapping_.clearBindings(id);
    if (sel.empty()) return Result{};
    return changed("cleared bindings on " + std::to_string(sel.size()), 2.0f);
}

Result Model::cycleOutput() {
    if (mapping_.targetCount() == 0) return Result{};
    currentOutput_ = (currentOutput_ == InvalidTarget)
                  ? 0 : (currentOutput_ + 1) % mapping_.targetCount();
    Result r;
    r.message = "current output: " + mapping_.targetName(currentOutput_);
    r.seconds = 2.0f;
    return r;
}

Result Model::pickOutput(TargetID t) {
    if (t == InvalidTarget || t >= mapping_.targetCount()) return Result{};
    currentOutput_ = t;
    Result r;
    r.message = "current output: " + mapping_.targetName(t);
    r.seconds = 2.0f;
    return r;
}

// Trim in dB, stored as a linear gain, capped at +12 dB.
Result Model::trimCurrent(float dB) {
    if (currentOutput_ == InvalidTarget) return Result{};
    const float t = mapping_.targetTrim(currentOutput_)
                  * std::pow(10.0f, dB / 20.0f);
    mapping_.setTargetTrim(currentOutput_, std::min(t, 3.9810717f));
    Result r;
    r.changed = true;
    return r;
}

// TargetIDs after a removed output shift down by one, so the current output
// has to follow -- the same rule removeNodes() taught for NodeIDs.
void Model::afterOutputRemoved(TargetID t) {
    if (currentOutput_ == InvalidTarget) return;
    if (currentOutput_ == t) currentOutput_ = InvalidTarget;
    else if (currentOutput_ > t) --currentOutput_;
    settleCurrentOutput();
}

// Nodes that fed it and others renormalize onto the others; nodes that fed
// only it go silent. The message says how many.
Result Model::removeCurrentOutput() {
    if (currentOutput_ == InvalidTarget) return Result{};
    std::size_t feeders = 0;
    for (NodeID n : mapping_.boundNodes()) {
        for (std::size_t k = 0; k < mapping_.linkCount(n); ++k) {
            const Link& l = mapping_.link(n, k);
            if (l.kind == DestKind::Output && l.id == currentOutput_) {
                ++feeders;
                break;
            }
        }
    }
    const std::string nm = mapping_.targetName(currentOutput_);
    const TargetID t = currentOutput_;
    mapping_.removeOutput(t);
    afterOutputRemoved(t);
    Result r;
    r.changed = true;
    r.refused = feeders > 0;          // shown as a warning, as before
    r.message = "removed output " + nm
              + (feeders ? " -- " + plural(feeders, " node", " nodes")
                           + " no longer reach it"
                         : std::string());
    r.seconds = 3.5f;
    return r;
}

Result Model::toggleAutoOutput() {
    autoOutput_ = !autoOutput_;
    Result r;
    r.message = autoOutput_
        ? "auto-output on: each new node gets its own output"
        : "auto-output off: new nodes start unbound";
    r.seconds = 3.0f;
    return r;
}

} // namespace editor
} // namespace ofxManifold
