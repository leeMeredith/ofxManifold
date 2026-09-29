#pragma once

// ofxManifoldEditor — the model.
//
// A map and its outputs, and every operation an editor performs on them. The
// ONE DOOR every change goes through (PLAN-editor.md decision D): views read
// the model and never change it, and undo -- round 2 -- snapshots here, so
// nothing can slip past it.
//
// No openFrameworks: glm and the kernel only, so it is tested with vectors like
// the kernel is. That is the point of the split. The rule that deleting a node
// takes its own output with it could previously be checked only by rewriting
// the editor's logic by hand in a separate test.
//
// Each operation returns a Result: whether anything changed, whether it was
// refused, and the message an editor would show -- as data, so a test can read
// it and any front end can display it however it likes.

#include "ofxManifoldEditorSelection.h"

#include "authoring/ofxManifoldGrid.h"
#include "core/ofxManifold2D.h"
#include "mapping/ofxManifoldMapping.h"

#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace editor {

struct Result {
    bool        changed = false;   // the map or its outputs changed
    bool        refused = false;   // refused; the message says why
    std::string message;           // empty: nothing to say
    float       seconds = 0.0f;    // how long an editor shows the message
};

class Model {
public:
    // The model's state is PRIVATE, readable from outside only through the
    // const accessors below. Its own operations are the only code that can
    // change it -- the one door, enforced by the compiler rather than by
    // convention. An app holding the model could otherwise write
    // model.manifold.addNode() and skip every rule an operation applies; with
    // these members public, it did compile.
    const Manifold2D& manifold() const { return manifold_; }
    const Mapping&    mapping() const { return mapping_; }
    const TargetID&   currentOutput() const { return currentOutput_; }

    // Placing a node also makes an output named after it, on the next free
    // channel, and binds the node to it. On by default: speaker layout is the
    // common case. Changed only through toggleAutoOutput().
    const bool&       autoOutput() const { return autoOutput_; }

    const TopologyReport& topology() const { return topology_; }

    // ---- whole maps --------------------------------------------------------

    // A fan of four triangles and a quad, with outputs chosen so every node
    // shape appears at once.
    void loadExample();
    void newEmpty();

    // Take over a map and outputs read from files.
    void adopt(Manifold2D m, Mapping mp);

    // ---- nodes and regions -------------------------------------------------

    // Place a node at p, snapped by `grid`. `placed` receives its id.
    Result place(glm::vec2 p, const Grid& grid, NodeID* placed = nullptr);

    // Move nodes as ONE operation, all or nothing (setNodePositions).
    Result move(const std::vector<std::pair<NodeID, glm::vec2>>& moves);

    Result join(const Selection& sel);
    Result unjoin(const Selection& sel);

    // Remove the selected nodes. NodeIDs are renumbered afterwards, so the
    // caller must clear any selection it holds.
    Result remove(const Selection& sel);

    // ---- outputs -----------------------------------------------------------

    Result newOutput();
    Result newDerived(const Selection& sel);
    // Toggle the selected nodes' binding to the CURRENT output.
    //
    //   if every selected node already feeds it  -> unbind them all from it
    //   otherwise                                -> bind the ones that don't
    //
    // Nodes already bound are left alone, so pressing it twice never feeds an
    // output double -- binding ADDS weight, and the old key did exactly that.
    // Each node keeps its fill through the change. A node whose last output
    // is unbound becomes cleanly null, with no stray silence binding left.
    Result bind(const Selection& sel);
    // Step each selected node's fill: how much of its share reaches its
    // outputs. `direction` +1 up, -1 down; 5% a step, or 1% when `fine`.
    // A step lands on the next multiple of its size in its direction, so a
    // node fine-tuned to 73% goes to 70% on a coarse step down, not 68%. The
    // floor is 1%: fully silent is clear(). The same step for every shape --
    // a node feeding four outputs moves exactly as far as one feeding one.
    Result stepFill(const Selection& sel, int direction, bool fine);

    // Bind each selected node to EVERY output, equally, keeping its fill.
    Result bindAll(const Selection& sel);
    Result clear(const Selection& sel);
    Result cycleOutput();

    // Make `t` the current output, as clicking its fader does. Through the
    // model like every other change, so nothing edits its state from outside.
    Result pickOutput(TargetID t);
    Result trimCurrent(float dB);
    Result removeCurrentOutput();
    Result toggleAutoOutput();

    // ---- queries -----------------------------------------------------------

    std::string freshName() const;
    int         nextFreeChannel() const;
    std::size_t outputCount(NodeID id) const;
    bool        feeds(NodeID node, TargetID output) const;
    bool        outputInUse(TargetID t) const;
    bool        occupied(glm::vec2 p, GridAddress addr,
                         const Grid& grid) const;

    void refreshTopology() { topology_ = manifold_.validate(); }

private:
    void afterOutputRemoved(TargetID t);
    void settleCurrentOutput();

    Manifold2D     manifold_;
    Mapping        mapping_;
    TargetID       currentOutput_ = InvalidTarget;
    bool           autoOutput_ = true;
    TopologyReport topology_;
};

} // namespace editor
} // namespace ofxManifold
