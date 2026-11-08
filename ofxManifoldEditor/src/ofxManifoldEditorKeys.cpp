#include "ofxManifoldEditorKeys.h"

#include <algorithm>

namespace ofxManifold {
namespace editor {

namespace {

// What l would lay out, in the panel's words.
std::string layoutAction(const KeyContext& ctx) {
    const int n = ctx.layoutSize;
    if (ctx.grid == GridShape::Polar) {
        return n == 1 ? "lay out a ring of " + std::to_string(ctx.spokes)
                      : "lay out " + std::to_string(n) + " rings of "
                            + std::to_string(ctx.spokes);
    }
    if (ctx.triangles) {
        return n == 2 ? std::string("lay out a diamond of 4")
                      : "lay out " + std::to_string(triangleStageNodes(n))
                            + " in triangles";
    }
    return "lay out a " + std::to_string(n) + " x " + std::to_string(n)
         + " block";
}

} // namespace

std::vector<KeyGroup> keyMap(const KeyContext& ctx) {
    const Model& m = *ctx.model;
    const Selection& sel = *ctx.selection;
    const std::size_t nodes = m.manifold().nodeCount();
    const std::size_t outputs = m.mapping().targetCount();
    const std::size_t picked = m.selectedOutputs().size();
    const std::size_t levels = m.map3d().count();
    const bool drag = ctx.dragging;
    const bool mouse = !ctx.auditioning && !drag;   // mouse editing allowed

    // Facts about the selection the rules below need.
    bool anyWithOutput = false, anyBound = false;
    for (NodeID id : sel.nodes()) {
        if (m.outputCount(id) > 0) anyWithOutput = true;
        if (m.mapping().linkCount(id) > 0) anyBound = true;
    }
    bool regionAllSelected = false;
    for (std::size_t r = 0; r < m.manifold().regionCount(); ++r) {
        bool all = true;
        for (NodeID id : m.manifold().region(RegionID(r)).ids()) {
            if (!sel.contains(id)) { all = false; break; }
        }
        if (all) { regionAllSelected = true; break; }
    }
    const BindPlan plan = m.planBind(sel);

    // A rule: available when `ok`; otherwise the first reason that applies.
    const std::string dragging = "finish the drag first";
    auto entry = [&](const std::string& id, const std::string& keys,
                     const std::string& action, bool ok,
                     const std::string& why) {
        KeyEntry e;
        e.id = id;
        e.keys = keys;
        e.action = action;
        e.available = ok;
        if (!ok) e.why = why;
        return e;
    };
    auto unlessDrag = [&](const std::string& why) {
        return drag ? dragging : why;
    };
    const std::string listening = "release space to edit with the mouse";
    auto mouseWhy = [&](const std::string& why) {
        return drag ? dragging : ctx.auditioning ? listening : why;
    };
    const bool gridOn = ctx.grid != GridShape::Free;

    std::vector<KeyGroup> g;

    g.push_back({"place and select", {
        entry("place", "click empty",
              sel.empty() ? "place a node" : "clear the selection",
              mouse, mouseWhy("")),
        entry("box", "drag empty", "box select", mouse && nodes > 0,
              mouseWhy("no nodes yet")),
        entry("toggle", "shift-click", "add or remove a node",
              mouse && nodes > 0, mouseWhy("no nodes yet")),
        entry("all", "a", "select every node", !drag && nodes > 0,
              unlessDrag("no nodes yet")),
        entry("none", "esc", "clear the selection", !drag && !sel.empty(),
              unlessDrag("nothing is selected")),
    }});

    g.push_back({"shape the map", {
        entry("move", "drag a node", "move the selection",
              mouse && nodes > 0, mouseWhy("no nodes yet")),
        entry("freehand", "alt + drag", "free-hand, off the grid",
              drag && gridOn,
              !drag ? "while dragging a node" : "the grid is off (1)"),
        entry("join", "f", "join into a region", !drag && sel.size() >= 3,
              unlessDrag("select at least three nodes to join")),
        entry("unjoin", "u", "unjoin regions", !drag && regionAllSelected,
              unlessDrag("no region is made only of selected nodes")),
        entry("remove", "delete", "remove nodes", !drag && !sel.empty(),
              unlessDrag("select the nodes to remove")),
    }});

    g.push_back({"grid", {
        entry("gridkind", "1 2 3 4", "free square triangle polar", !drag,
              dragging),
        entry("rotate", "r R 0", "rotate the grid", !drag && gridOn,
              unlessDrag("the grid is off (1)")),
        entry("spacing", "- =", "grid spacing", !drag && gridOn,
              unlessDrag("the grid is off (1)")),
        entry("spokes", "{ }", "polar spokes",
              !drag && ctx.grid == GridShape::Polar,
              unlessDrag("polar grid only (4)")),
        entry("deadzone", ", .", "snap dead zone", !drag && gridOn,
              unlessDrag("the grid is off (1)")),
        // [ ] set the size of what l lays out, on every grid (3A-9).
        entry("layoutsize", "[ ]", "layout size", !drag && gridOn,
              unlessDrag("the grid is off (1)")),
        entry("layout", "l", layoutAction(ctx), !drag && gridOn,
              unlessDrag("the grid is off (1)")),
    }});

    g.push_back({"outputs", {
        entry("newoutput", "o", "new output", !drag, dragging),
        entry("pickoutput", "click a fader", "pick an output",
              !drag && outputs > 0, unlessDrag("no outputs yet")),
        entry("addoutput", "shift-click", "pick several faders",
              !drag && outputs >= 2, unlessDrag("only one output")),
        entry("nextoutput", "tab", "next output", !drag && outputs >= 2,
              unlessDrag(outputs == 0 ? "no outputs yet" : "only one output")),
        entry("bind", "b", plan.preview(), !drag && plan.possible,
              unlessDrag(plan.why)),
        entry("bindall", "B", "bind to every output",
              !drag && outputs > 0 && !sel.empty(),
              unlessDrag(outputs == 0 ? "no outputs yet"
                                      : "select the nodes to bind")),
        entry("fill", "q w", "fill -/+ 5% (shift 1%)",
              !drag && anyWithOutput,
              unlessDrag("select a node that has an output")),
        // c and C (round 3A-10): a node's two plain states.
        entry("clear", "c", "back to its own output", !drag && !sel.empty(),
              unlessDrag("select the nodes to put back")),
        entry("silence", "C", "silent", !drag && anyBound,
              unlessDrag("the selection is already silent")),
        entry("derivedafter", "d", "derived output after the picked faders",
              !drag && picked > 0, unlessDrag("no outputs yet")),
        entry("derived", "D", "derived output from the selected nodes",
              !drag && !sel.empty(),
              unlessDrag("select the nodes a derived output should sum")),
        entry("trim", "t T", "trim -/+ 1 dB", !drag && picked > 0,
              unlessDrag("no outputs yet")),
        entry("removeoutput", "O", "remove the output",
              !drag && picked == 1,
              unlessDrag(picked == 0 ? "no outputs yet"
                         : "select one output to remove -- "
                           + std::to_string(picked) + " are selected")),
        entry("autooutput", "p",
              std::string("auto-output ") + (m.autoOutput() ? "on" : "off"),
              !drag, dragging),
    }});

    // Round 3A-6. While auditioning, up and down move the listening point
    // through the levels and past them -- so they work with one level too.
    g.push_back({"levels", {
        entry("levelswitch", "up down",
              ctx.auditioning ? "listen higher / lower" : "level up / down",
              !drag && (ctx.auditioning || levels > 1),
              unlessDrag("one level -- n makes another")),
        entry("newlevel", "n N", "new level above / below", !drag, dragging),
        entry("raise", "y h", "raise / lower 0.05 (shift 0.01)",
              !drag && !sel.empty(),
              unlessDrag("select the nodes to raise or lower")),
        entry("movelevel", "i k", "move the level 0.05 (shift 0.01)",
              !drag && levels > 1,
              unlessDrag("one level -- nothing to move it toward")),
        entry("match", "m M", "match the first's height / flatten",
              !drag && !sel.empty(),
              unlessDrag("select the node to match, then the others")),
        entry("removelevel", "cmd-delete", "remove the level",
              !drag && levels > 1, unlessDrag("the only level")),
        entry("view3d", "cmd-3", "3D view on or off", true, ""),
    }});

    // Round 3A-7: node values, named numbers each node carries per level.
    const std::size_t values = m.map3d().valueSet().size();
    const char* noValues = "no values yet -- g makes one";
    g.push_back({"values", {
        entry("newvalue", "g", "new value -- type its name", !drag, dragging),
        entry("pickvalue", "j J", "next / previous value",
              !drag && values > 0, unlessDrag(noValues)),
        entry("stepvalue", "; '", "value down / up 5% (shift 1%)",
              !drag && values > 0 && !sel.empty(),
              unlessDrag(values == 0 ? noValues
                                     : "select the nodes to change")),
        entry("editvalue", "z", "rename or re-range the value",
              !drag && values > 0, unlessDrag(noValues)),
        entry("removevalue", "G", "remove the value",
              !drag && values > 0, unlessDrag(noValues)),
    }});

    g.push_back({"listen", {
        entry("audition", "space", "hold to audition", !drag, dragging),
        entry("lock", "v", "lock audition on or off", !drag, dragging),
    }});

    g.push_back({"file", {
        entry("undo", "cmd-z", "undo", !drag && ctx.canUndo,
              unlessDrag("nothing to undo")),
        entry("redo", "shift-cmd-z", "redo", !drag && ctx.canRedo,
              unlessDrag("nothing to redo")),
        entry("save", "cmd-s", "save", !drag, dragging),
        entry("saveas", "shift-cmd-s", "save as", !drag, dragging),
        entry("open", "cmd-o", "open a pair", !drag, dragging),
        entry("userig", "cmd-r", "use a rig", !drag, dragging),
        entry("saverig", "shift-cmd-r", "save the outputs as a rig",
              !drag && outputs > 0, unlessDrag("no outputs yet")),
        entry("empty", "x", "new empty map", !drag, dragging),
        entry("example", "e", "the example map", !drag, dragging),
        entry("fullscreen", "ctrl-cmd-f", "full screen", true, ""),
        entry("textsize", "cmd-+ cmd--", "text size", true, ""),
        entry("keysheet", "?", "every key, over the whole window", true, ""),
    }});
    return g;
}

const KeyEntry* findKey(const std::vector<KeyGroup>& map,
                        const std::string& id) {
    for (const KeyGroup& g : map) {
        for (const KeyEntry& e : g.entries) {
            if (e.id == id) return &e;
        }
    }
    return nullptr;
}

bool keyAvailable(const KeyContext& ctx, const std::string& id,
                  std::string* why) {
    const std::vector<KeyGroup> map = keyMap(ctx);
    const KeyEntry* e = findKey(map, id);
    if (!e) {
        if (why) *why = "no such key: " + id;
        return false;
    }
    if (!e->available && why) *why = e->why;
    return e->available;
}

Typed typeKey(std::string& line, int key) {
    if (key == 13 || key == 10) return Typed::Enter;
    if (key == 27) return Typed::Escape;
    if (key == 8 || key == 127) {
        if (!line.empty()) line.pop_back();
    } else if (key >= 32 && key <= 126 && line.size() < 40) {
        line += char(key);
    }
    return Typed::Open;
}

namespace {

// One pass of the flow; with `litOnly`, the keys that would not act now are
// left out.
KeyFlow flowPass(const std::vector<KeyGroup>& groups, float x, float right,
                 float top, float keysTop, float bottom, float cw, float lh,
                 bool litOnly, bool outputsOnTop) {
    KeyFlow f;
    const float room = std::max(cw * 12.0f, right - x);
    f.columns = std::max<std::size_t>(
        1, std::size_t(room / (kKeyColumnChars * cw)));
    f.columnWidth = room / float(f.columns);
    f.chars = std::size_t(std::max(12.0f, f.columnWidth / cw - 2.0f));
    const float row = lh + 1.0f;

    std::size_t col = 0;
    float y = keysTop;
    auto columnX = [&](std::size_t c) { return x + float(c) * f.columnWidth; };
    auto nextColumn = [&]() {
        if (col + 1 >= f.columns) return false;
        ++col;
        y = top;
        return true;
    };
    bool full = false;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const KeyGroup& grp = groups[g];
        std::vector<int> shown;
        for (std::size_t e = 0; e < grp.entries.size(); ++e) {
            if (litOnly && !grp.entries[e].available) { ++f.dimLeftOut; continue; }
            shown.push_back(int(e));
        }
        if (full) { f.hidden += shown.size(); continue; }
        if (litOnly && shown.empty()) continue;
        // outputs starts a column of its own, at the top, when the first
        // column has held everything before it.
        if (outputsOnTop && grp.title == "outputs" && col == 0
            && f.columns > 1) {
            nextColumn();
        }
        std::size_t e = 0;
        bool more = false;
        while (e < shown.size() || (shown.empty() && !more)) {
            // Room for the title and at least one row, or move on.
            const std::size_t fit = y + row > bottom + 0.5f ? 0
                : std::size_t((bottom + 0.5f - y) / row);
            const std::size_t want = 1 + (shown.size() - e);
            if (fit < std::min<std::size_t>(2, want)) {
                if (!nextColumn()) { full = true; break; }
                continue;
            }
            f.lines.push_back({g, -1, more, columnX(col), y});
            y += row;
            const std::size_t n = std::min(fit - 1, shown.size() - e);
            for (std::size_t k = 0; k < n; ++k, ++e) {
                f.lines.push_back({g, shown[e], false, columnX(col), y});
                y += row;
            }
            if (shown.empty()) break;
            if (e < shown.size()) {
                more = true;
                if (!nextColumn()) { full = true; break; }
            }
        }
        if (full) f.hidden += shown.size() - e;
        y += lh * 0.5f;
    }
    if (f.hidden > 0 || f.dimLeftOut > 0) {
        // The last line becomes the note of what is not shown: the entry it
        // held is counted.
        while (!f.lines.empty()) {
            const KeyLine last = f.lines.back();
            f.lines.pop_back();
            if (last.entry >= 0) ++f.hidden;
            f.moreX = last.x;
            f.moreY = last.y;
            if (last.entry >= 0) break;
        }
    }
    return f;
}

} // namespace

KeyFlow flowKeys(const std::vector<KeyGroup>& groups, float x, float right,
                 float top, float keysTop, float bottom, float cw, float lh,
                 bool tight) {
    if (!tight) {
        return flowPass(groups, x, right, top, keysTop, bottom, cw, lh, false,
                        true);
    }
    // Everything, outputs at the top of a column, if that fits; then
    // everything packed tight; then the keys that would act now, the same
    // two ways.
    for (const bool lit : {false, true}) {
        for (const bool onTop : {true, false}) {
            KeyFlow f = flowPass(groups, x, right, top, keysTop, bottom, cw, lh,
                                 lit, onTop);
            if (f.hidden == 0) return f;
            if (lit && !onTop) return f;
        }
    }
    return KeyFlow{};
}

float keyGroupHeight(const KeyGroup& g, float lh) {
    return (1.0f + float(g.entries.size())) * (lh + 1.0f) + lh * 0.5f;
}

float panelTop(float lh) { return 24.0f + lh; }

float panelKeysTop(float lh, bool values) {
    const float step = lh + 2.0f;
    float y = panelTop(lh);
    y += 7.0f * step + lh * 0.5f;           // status
    y += 2.0f * step + lh * 0.5f;           // message
    y += 3.0f * step + lh * 0.5f;           // the output acted on
    if (values) y += step + lh * 0.5f;      // node values
    return y;
}

std::vector<KeyGroup> panelGroups(const std::vector<KeyGroup>& groups,
                                  bool show3D) {
    std::vector<KeyGroup> out;
    for (const KeyGroup& g : groups) {
        if (show3D && g.title == "levels") continue;
        out.push_back(g);
    }
    return out;
}

EditorLayout layoutEditor(float width, float height, float cw, float lh,
                          bool levels, bool show3D, bool values,
                          const std::vector<KeyGroup>& groups) {
    EditorLayout L;
    // The map keeps a square; the band under it holds the faders, taller on
    // a taller window, and never too short for its two label rows.
    const float chartH = std::max(130.0f, height * 0.2f);
    const float side = std::max(160.0f, height - 48.0f - chartH);
    L.column = levels ? 64.0f : 0.0f;
    L.view3D = show3D;
    L.plan = side;
    float levelsH = 0.0f;
    if (show3D) {
        for (const KeyGroup& g : groups) {
            if (g.title == "levels") levelsH = keyGroupHeight(g, lh);
        }
        const float across = (width - 48.0f - L.column - 16.0f - 30.0f
                              - kPanelChars * cw) * 0.5f;
        L.plan = std::max(160.0f, std::min(side - levelsH - 16.0f, across));
    }
    L.chartTop = 24.0f + side + 16.0f;

    const std::vector<KeyGroup> shown = panelGroups(groups, show3D);
    const float views = show3D ? 2.0f : 1.0f;
    const float smallest = std::max(160.0f, L.plan * 0.5f);
    auto right = [&]() {
        return 24.0f + views * L.plan + (show3D ? 16.0f : 0.0f) + L.column;
    };
    // The faders across the window, the panel above them; or the faders
    // under the views only, the panel down to the bottom.
    auto flow = [&](bool narrow, bool tight) {
        L.panelLeft = right() + 30.0f;
        L.panelBottom = narrow ? height - 16.0f : L.chartTop - 10.0f;
        L.chartRight = narrow ? right() : width - 24.0f;
        L.keys = flowKeys(shown, L.panelLeft, width - 16.0f, panelTop(lh),
                          panelKeysTop(lh, values), L.panelBottom, cw, lh,
                          tight);
        return L.keys.hidden == 0 && L.keys.dimLeftOut == 0;
    };
    if (!flow(false, false) && !flow(true, false) && !flow(true, true)) {
        while (L.plan > smallest) {
            L.plan = std::max(smallest, L.plan - 8.0f);
            if (flow(true, false) || flow(true, true)) break;
        }
    }
    if (show3D) {
        L.view3DX = 24.0f + L.plan + L.column + 16.0f;
        L.levelsKeysX = L.view3DX;
        L.levelsKeysY = 24.0f + L.plan + 16.0f + lh;
        L.levelsKeysWidth = L.plan;
    }
    return L;
}

} // namespace editor
} // namespace ofxManifold
