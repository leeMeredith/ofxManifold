#pragma once

// ofxManifoldEditor — undo and redo.
//
// Linear history over SNAPSHOTS of the model. Undo and redo move freely; any
// new action discards the steps ahead of it.
//
// Snapshots rather than inverse operations because the model is a plain
// copyable value -- regions were made a value type for exactly this
// (DECISIONS.md D-016b, D-A) -- and because inverses would have to undo
// renumbering: deleting a node renumbers everything after it, and reversing
// that correctly is the kind of bookkeeping D-020 and D-022 were about. A copy
// sidesteps it entirely.
//
// Complete by construction: every change goes through a Model operation (the
// one door, enforced by `make door`), and apply() records at that door. There
// is no way to change the model that history does not see.
//
// No openFrameworks. Tested with scripted vectors.

#include "ofxManifoldEditorModel.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace editor {

class History {
public:
    explicit History(std::size_t limit = 200) : limit_(limit) {}

    // Run `op` on `model` as ONE undoable step, labelled for the message.
    // The state before is recorded only if the operation changed something,
    // so refusals, cursor moves and mode toggles never become undo steps.
    template <class Op>
    Result apply(Model& model, const std::string& label, Op op) {
        Model before = model;
        Result r = op(model);
        if (r.changed) record(std::move(before), label);
        return r;
    }

    // Record a state directly: for an action whose changes arrive in pieces,
    // like a drag -- snapshot at the press, record at the release if
    // anything moved. A drag is one step, not one per frame.
    void record(Model before, const std::string& label);

    Result undo(Model& model);
    Result redo(Model& model);

    bool        canUndo() const { return !past_.empty(); }
    bool        canRedo() const { return !future_.empty(); }
    std::size_t undoCount() const { return past_.size(); }
    std::size_t redoCount() const { return future_.size(); }
    void        clear() { past_.clear(); future_.clear(); }

private:
    struct Entry {
        Model       state;
        std::string label;
    };
    std::vector<Entry> past_, future_;
    std::size_t limit_;
};

} // namespace editor
} // namespace ofxManifold
