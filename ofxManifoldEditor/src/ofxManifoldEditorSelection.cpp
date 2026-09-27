#include "ofxManifoldEditorSelection.h"

#include <algorithm>

namespace ofxManifold {
namespace editor {

bool Selection::contains(NodeID id) const {
    return std::find(nodes_.begin(), nodes_.end(), id) != nodes_.end();
}

void Selection::clear() {
    nodes_.clear();
    ordered_ = true;
}

void Selection::only(NodeID id) {
    nodes_ = {id};
    ordered_ = true;
}

void Selection::toggle(NodeID id) {
    auto it = std::find(nodes_.begin(), nodes_.end(), id);
    if (it != nodes_.end()) nodes_.erase(it);
    else nodes_.push_back(id);
}

void Selection::addFromBox(NodeID id) {
    if (contains(id)) return;
    nodes_.push_back(id);
    ordered_ = false;
}

void Selection::selectAll(std::size_t nodeCount) {
    nodes_.clear();
    for (std::size_t i = 0; i < nodeCount; ++i) {
        nodes_.push_back(static_cast<NodeID>(i));
    }
}

} // namespace editor
} // namespace ofxManifold
