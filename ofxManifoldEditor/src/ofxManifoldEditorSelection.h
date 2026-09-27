#pragma once

// ofxManifoldEditor — the selection.
//
// Which nodes are selected, and whether their ORDER means anything. Order
// matters because joining nodes into a region uses it: nodes clicked one at a
// time are joined in click order, and a box selection -- which has no order --
// is joined in angle order about its centroid (DECISIONS.md, ring ordering).
//
// No openFrameworks. Holds NodeIDs, so anything that renumbers nodes -- a
// removal -- must be followed by clear().

#include "core/ofxManifoldTypes.h"

#include <cstddef>
#include <vector>

namespace ofxManifold {
namespace editor {

class Selection {
public:
    const std::vector<NodeID>& nodes() const { return nodes_; }
    bool        ordered() const { return ordered_; }
    bool        empty() const { return nodes_.empty(); }
    std::size_t size() const { return nodes_.size(); }
    bool        contains(NodeID id) const;

    // Nothing selected. Order is meaningful again from the next click.
    void clear();

    // Just this node, as a plain click on it does.
    void only(NodeID id);

    // Shift-click: add it, or remove it if already there. Order is kept.
    void toggle(NodeID id);

    // A node caught by a box. A box has no order, so the selection loses its.
    void addFromBox(NodeID id);

    // Every node, in index order. Leaves the ordered flag as it was.
    void selectAll(std::size_t nodeCount);

private:
    std::vector<NodeID> nodes_;
    bool ordered_ = true;
};

} // namespace editor
} // namespace ofxManifold
