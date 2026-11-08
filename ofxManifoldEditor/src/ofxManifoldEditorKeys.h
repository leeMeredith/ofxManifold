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
    bool      triangles   = false;   // the lattice is the triangular one
    int       spokes      = 8;       // the polar grid's spokes
    int       layoutSize  = 1;       // what l lays out on this grid (3A-9):
                                     // rings deep, block across, or the
                                     // triangle's stage
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

// ---------------------------------------------------------------------------
// The keys laid out in the panel (round 3A-10)
// ---------------------------------------------------------------------------
// The panel's room is split into as many columns as fit, each at least
// kKeyColumnChars wide, sharing the room evenly. The status sits at the top
// of the first column; every other column starts at the top of the panel,
// so the top right is used. The groups go in order, down a column and on to
// the next; outputs starts a new column when the first holds everything
// before it. A group that does not fit is split, the rest under its title
// with "(more)". If everything still does not fit, the keys that would not
// act now are left out, and if the lit ones do not fit either, the rest are
// counted; either way the last line says so, and ? shows every key over the
// whole window. No openFrameworks: checked by tests/check_panel.cpp at many
// window and text sizes.
constexpr float kKeyColumnChars = 32.0f;

struct KeyLine {
    std::size_t group = 0;
    int         entry = -1;    // -1: the group's title
    bool        more  = false; // a title continued from the column before
    float       x = 0.0f, y = 0.0f;
};

struct KeyFlow {
    std::size_t          columns = 1;
    std::size_t          chars = 12;       // per column
    float                columnWidth = 0.0f;
    std::vector<KeyLine> lines;
    std::size_t          hidden = 0;       // lit entries no column holds
    std::size_t          dimLeftOut = 0;   // dim entries left out for room
    float                moreY = -1.0f;    // where the note goes, if any
    float                moreX = 0.0f;
};

// x and right: the panel's sides. top: the first line's baseline in columns
// after the first; keysTop: in the first, under the status. bottom: no
// line's baseline below it. cw, lh: the text's character width and line
// height.
// `tight` false: only the first way -- every key, outputs at the top of a
// column -- whatever it leaves out.
KeyFlow flowKeys(const std::vector<KeyGroup>& groups, float x, float right,
                 float top, float keysTop, float bottom, float cw, float lh,
                 bool tight = true);

// How tall a group of keys is drawn, title and rows: for an app placing one.
float keyGroupHeight(const KeyGroup& g, float lh);

// The panel's first line, at the top of every column.
float panelTop(float lh);

// Where the keys start under the status, which is always the same height:
// seven status lines, two for the message, three for the output being acted
// on, and one for node values when there are any (3A-10).
float panelKeysTop(float lh, bool values);

// The room the panel asks for beside the views: two key columns.
constexpr float kPanelChars = 2.0f * kKeyColumnChars;

// The editor window laid out (3A-10), from its size and the text's. The map
// keeps a square at the top left; with levels a column of dots beside it;
// with the 3D view on, the view the same size beside that, and the levels
// keys beneath it. The faders take the band along the bottom; the panel
// takes the rest of the right side, above the faders. If the keys do not
// fit with outputs at the top of a column, first the faders keep to the
// width under the views and the panel runs down to the window's bottom;
// then the keys are packed tight; then the views give up width a step at a
// time, down to half their size. The app draws from this;
// tests/check_panel.cpp checks it.
struct EditorLayout {
    float plan = 0.0f;              // the map's side, and the 3D view's
    float column = 0.0f;            // the level dots' column, or 0
    bool  view3D = false;
    float view3DX = 0.0f;           // the 3D view's left, when shown
    float panelLeft = 0.0f;
    float panelBottom = 0.0f;       // no panel line below this
    float chartTop = 0.0f;          // the faders' band
    float chartRight = 0.0f;        // its right edge
    float levelsKeysX = -1.0f;      // the levels keys under the 3D view
    float levelsKeysY = -1.0f;
    float levelsKeysWidth = 0.0f;
    KeyFlow keys;                   // the panel's keys (levels apart, with 3D)
};
EditorLayout layoutEditor(float width, float height, float cw, float lh,
                          bool levels, bool show3D, bool values,
                          const std::vector<KeyGroup>& groups);

// The groups the panel shows: all of them, or with the 3D view open all but
// levels, which go beneath it.
std::vector<KeyGroup> panelGroups(const std::vector<KeyGroup>& groups,
                                  bool show3D);

// The typing line (round 3A-7), opened by g and z to name a value. What one
// key does to it: printable keys add, up to 40 characters; backspace and
// delete take the last away; return ends it to be made, escape unmade;
// anything else is ignored.
enum class Typed { Open, Enter, Escape };
Typed typeKey(std::string& line, int key);

} // namespace editor
} // namespace ofxManifold
