#include "ofxManifoldEditorKeys.h"

namespace ofxManifold {
namespace editor {

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
        entry("spokes", "[ ]", "polar spokes",
              !drag && ctx.grid == GridShape::Polar,
              unlessDrag("polar grid only (4)")),
        entry("deadzone", ", .", "snap dead zone", !drag && gridOn,
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
        entry("clear", "c", "clear bindings", !drag && anyBound,
              unlessDrag("the selection has no bindings")),
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

} // namespace editor
} // namespace ofxManifold
