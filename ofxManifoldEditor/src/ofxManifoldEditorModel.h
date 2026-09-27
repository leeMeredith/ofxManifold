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
    Manifold2D manifold;
    Mapping    mapping;
    TargetID   currentOutput = InvalidTarget;

    // Placing a node also makes an output named after it, on the next free
    // channel, and binds the node to it. On by default: speaker layout is the
    // common case. The model is unchanged by it -- a convenience over outputs
    // that stay separate from nodes.
    bool autoOutput = true;

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
    Result bind(const Selection& sel);
    Result silence(const Selection& sel);
    Result clear(const Selection& sel);
    Result cycleOutput();
    Result trimCurrent(float dB);
    Result removeCurrentOutput();
    Result toggleAutoOutput();

    // ---- queries -----------------------------------------------------------

    std::string freshName() const;
    int         nextFreeChannel() const;
    std::size_t outputCount(NodeID id) const;
    bool        outputInUse(TargetID t) const;
    bool        occupied(glm::vec2 p, GridAddress addr,
                         const Grid& grid) const;

    void refreshTopology() { topology_ = manifold.validate(); }

private:
    void afterOutputRemoved(TargetID t);
    void settleCurrentOutput();

    TopologyReport topology_;
};

} // namespace editor
} // namespace ofxManifold
