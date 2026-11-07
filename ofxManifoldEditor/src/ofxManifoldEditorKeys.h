#pragma once

// ofxManifoldEditor — the keys: what each one does, grouped by intention, and
// whether it would do anything right now.
//
// ONE function decides both what the panel shows lit and what a key press is
// allowed to do: an editor asks keyAvailable() before acting, and a dimmed key
// says why instead of acting. So "lit" cannot drift from "works" -- the
// D-018 pattern of two parts of a system holding different rules for the
// same state. It also closes a real hole: delete pressed in the middle of a
// drag used to remove nodes the drag still pointed at.
//
// While a node is being dragged, only alt -- free-hand -- and the window keys
// are available. While auditioning, only the MOUSE editing actions are dimmed:
// keys still act, so a fill step or a binding can be heard as it is made.
//
// No openFrameworks. Tested with vectors.

#include "ofxManifoldEditorModel.h"
#include "ofxManifoldEditorSelection.h"

#include <string>
#include <vector>

namespace ofxManifold {
namespace editor {

enum class GridShape { Free, Lattice, Polar };

// Everything availability depends on beyond the model and the selection.
struct KeyContext {
    const Model*     model     = nullptr;
    const Selection* selection = nullptr;
    bool      canUndo     = false;
    bool      canRedo     = false;
    bool      dragging    = false;   // a node drag in progress
    bool      auditioning = false;
    GridShape grid        = GridShape::Lattice;
};

struct KeyEntry {
    std::string id;          // stable name, used by an editor to ask
    std::string keys;        // as shown: "f", "cmd-z", "click empty"
    std::string action;      // what it does -- for b, what it WOULD do now
    bool        available = false;
    std::string why;         // why not, when unavailable
};

struct KeyGroup {
    std::string           title;
    std::vector<KeyEntry> entries;
};

std::vector<KeyGroup> keyMap(const KeyContext& ctx);

// The entry with this id, or nullptr.
const KeyEntry* findKey(const std::vector<KeyGroup>& map, const std::string& id);

// Whether the key with this id would act now; `why` receives the reason when
// it would not.
bool keyAvailable(const KeyContext& ctx, const std::string& id,
                  std::string* why = nullptr);

// The typing line (round 3A-7), opened by g and z to name a value. What one
// key does to it: printable keys add, up to 40 characters; backspace and
// delete take the last away; return ends it to be made, escape unmade;
// anything else is ignored.
enum class Typed { Open, Enter, Escape };
Typed typeKey(std::string& line, int key);

} // namespace editor
} // namespace ofxManifold
