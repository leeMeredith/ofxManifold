#include "ofxManifoldEditorHistory.h"

namespace ofxManifold {
namespace editor {

void History::record(Model before, const std::string& label) {
    past_.push_back(Entry{std::move(before), label});
    // Beyond the limit the OLDEST step goes, never the newest.
    if (limit_ > 0 && past_.size() > limit_) past_.erase(past_.begin());
    // A new action makes the steps ahead of it unreachable.
    future_.clear();
}

Result History::undo(Model& model) {
    Result r;
    if (past_.empty()) {
        r.refused = true;
        r.message = "nothing to undo";
        r.seconds = 1.5f;
        return r;
    }
    Entry e = std::move(past_.back());
    past_.pop_back();
    future_.push_back(Entry{model, e.label});
    model = std::move(e.state);
    r.changed = true;
    r.message = "undid " + e.label;
    r.seconds = 2.0f;
    return r;
}

Result History::redo(Model& model) {
    Result r;
    if (future_.empty()) {
        r.refused = true;
        r.message = "nothing to redo";
        r.seconds = 1.5f;
        return r;
    }
    Entry e = std::move(future_.back());
    future_.pop_back();
    past_.push_back(Entry{model, e.label});
    model = std::move(e.state);
    r.changed = true;
    r.message = "redid " + e.label;
    r.seconds = 2.0f;
    return r;
}

} // namespace editor
} // namespace ofxManifold
