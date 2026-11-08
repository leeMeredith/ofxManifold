#pragma once

// ofxManifoldEditor — the model.
//
// A map and its outputs, and every operation an editor performs on them. The
// ONE DOOR every change goes through (DECISIONS.md D-024 D): views read
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
#include "mapping/ofxManifoldMap3D.h"
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

// What pressing b would do, worked out without doing it: the key panel shows
// it before the key is pressed. bind() does not use it -- bind() builds its
// message from what it actually changed -- so the tests can check, for every
// b in every script, that the plan foretold exactly what happened.
// What l would lay out, worked out without doing it (3A-9): the preview
// draws it, and layout() does exactly it.
struct LayoutPlan {
    bool                                  ok = false;
    std::string                           message;   // what it is, or why not
    std::vector<glm::vec2>                points;    // in numbering order
    std::vector<std::vector<std::size_t>> regions;   // indices into points
};

// The triangular grid's layout sizes, 1 to kTriangleStages: an upright
// triangle of side k, and between sides k and k + 1 the next row's inner
// points -- 3, 4 (a diamond), 6, 8, 10, 13, 15 ... 36 nodes.
constexpr int kTriangleStages = 13;
int triangleStageNodes(int stage);

struct BindPlan {
    bool        possible = false;   // false: b would do nothing
    std::string why;                // why not, when it is not possible
    bool        unbind   = false;   // true: every pair exists, b removes them
    std::size_t count    = 0;       // nodes that would change
    std::vector<std::string> silenced;   // nodes it would leave with no output
    std::string outputs;            // the selected outputs, by name

    // "bind 2 to front"; "unbind 1 from n2 -- n2 goes silent"
    std::string preview() const;
    // The message bind() should return when it carries this plan out.
    std::string outcome() const;
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
    // The CURRENT level's bindings, with the outputs every level shares.
    // Everything written before levels reads this and sees exactly what it
    // did: a map with one level is today's map.
    const Mapping&    mapping() const { return map_.level(level_); }
    const TargetID&   currentOutput() const { return currentOutput_; }

    // The whole 3D map, and which of its levels is being edited (round
    // 3A-6). Output operations reach every level; binding operations, fill
    // and heights reach the current one.
    const Map3D&      map3d() const { return map_; }

    // The value being edited (round 3A-7), or NoValue when the map has none.
    static constexpr std::size_t NoValue = static_cast<std::size_t>(-1);
    const std::size_t& currentValue() const { return currentValue_; }
    const LevelID&    currentLevel() const { return level_; }

    // The selected outputs: shift-click on faders selects several. The
    // current output is always one of them. b binds to all of them, t and T
    // trim all of them; removing an output needs exactly one selected.
    const std::vector<TargetID>& selectedOutputs() const { return outputSel_; }

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

    // Take over a map and outputs read from files: a 2D pair, as one level,
    // or a 3D map. The first level becomes current.
    void adopt(Manifold2D m, Mapping mp);
    void adopt(Manifold2D m, Map3D map);

    // ---- nodes and regions -------------------------------------------------

    // Place a node at p, snapped by `grid`. `placed` receives its id.
    Result place(glm::vec2 p, const Grid& grid, NodeID* placed = nullptr);

    // l (rounds 3A-8, 3A-9): many nodes in one step, from the grid point
    // nearest p. On the polar grid, `size` rings from the one nearest p
    // inward: one node per spoke on each, numbered clockwise from the spoke
    // nearest the front (up), outermost first; joined as four-sided pieces
    // between rings and the innermost ring whole -- or, when they reach it,
    // the centre last, joined to the innermost ring as triangles. On the
    // square grid a size x size block centred there, numbered row by row
    // from the front-left, joined as squares. With `triangles` (the
    // triangular grid) an upright triangle of stage `size`, placed so its
    // middle is nearest p, numbered row by row from its point. Each node
    // placed as a click places it, its outputs taking one run of channels
    // per level. All or nothing: refused on the free grid, at the polar
    // centre, past it, off the map, or on a node. `placed` receives the new
    // nodes, in their order.
    LayoutPlan planLayout(glm::vec2 p, const Grid& grid, int size,
                          bool triangles) const;
    Result layout(glm::vec2 p, const Grid& grid, int size, bool triangles,
                  std::vector<NodeID>* placed = nullptr);

    // Move nodes as ONE operation, all or nothing (setNodePositions).
    Result move(const std::vector<std::pair<NodeID, glm::vec2>>& moves);

    Result join(const Selection& sel);
    Result unjoin(const Selection& sel);

    // Remove the selected nodes. NodeIDs are renumbered afterwards, so the
    // caller must clear any selection it holds.
    Result remove(const Selection& sel);

    // ---- outputs -----------------------------------------------------------

    Result newOutput();
    // A derived output summing the selected NODES -- before every fader,
    // following the map (D).
    Result newDerived(const Selection& sel);
    // A derived output summing what the selected OUTPUTS send -- after their
    // faders, following the mix (d; PLAN-round3.md decision A). Refused with
    // no output picked.
    Result newDerivedAfter();

    // Move the map onto a rig (cmd-r): the rig's outputs, channels, trims and
    // after-fed derived outputs; bindings following by name. Refused, naming
    // what the rig lacks or holds as a different kind, changing nothing. The
    // current output stays current if the rig has it; the picked outputs
    // keep those it has.
    Result useRig(const Mapping& rig, const std::string& name);
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
    BindPlan planBind(const Selection& sel) const;
    // Step each selected node's fill: how much of its share reaches its
    // outputs. `direction` +1 up, -1 down; 5% a step, or 1% when `fine`.
    // A step lands on the next multiple of its size in its direction, so a
    // node fine-tuned to 73% goes to 70% on a coarse step down, not 68%. The
    // floor is 1%: fully silent is clear(). The same step for every shape --
    // a node feeding four outputs moves exactly as far as one feeding one.
    Result stepFill(const Selection& sel, int direction, bool fine);

    // Bind each selected node to EVERY output, equally, keeping its fill.
    Result bindAll(const Selection& sel);

    // c (round 3A-10): the selected nodes back to their own output on this
    // level -- every other binding gone, fill 100%, as when placed. Its own
    // output is the one named for this level (P, P-2 ...), else one named
    // after it that it feeds on no other level. With none, one is made if
    // auto-output is on; if it is off the node is left as it was, and named.
    Result clear(const Selection& sel);

    // C (round 3A-10): the selected nodes silent on this level, on purpose.
    Result silence(const Selection& sel);
    Result cycleOutput();

    // Make `t` the current output, as clicking its fader does. Through the
    // model like every other change, so nothing edits its state from outside.
    Result pickOutput(TargetID t);

    // Shift-click on a fader: add it to the selected outputs, or take it
    // out. The last output selected can't be taken out, so there is always
    // a current output once any exist.
    Result toggleOutput(TargetID t);
    Result trimCurrent(float dB);
    Result removeCurrentOutput();
    Result toggleAutoOutput();

    // ---- levels (round 3A-6) -------------------------------------------------

    // Up (+1) or down (-1) a level. Not an undo step, like picking a fader:
    // nothing about the map changes. Says where it landed, or that there is
    // nowhere to go.
    Result switchLevel(int direction);

    // n and N: a new level above or below the current one, a COPY of it --
    // heights, bindings, values. Halfway to the next level, or a quarter of
    // the map beyond the end one. It becomes current. Refused, naming the
    // nodes, if any copy would reach a level next to it.
    Result newLevel(bool above);

    // The node whose own output this is: the node of that name, else the
    // node it is named after (P for P-2, P-3_2), else InvalidNode. Link lines
    // land on it, and a new level redirects links to it (3A-9).
    NodeID ownerOf(const std::string& output) const;

    // y and h: each selected node's height on the current level steps by
    // 0.05, or 0.01 when `fine`, landing on the next multiple of the step
    // above the level's start, as fill does. All or nothing: refused, naming
    // the nodes, if any would reach the level above or below.
    Result raise(const Selection& sel, int direction, bool fine);

    // A click on a level's dot (round 3A-6b): that level becomes current.
    // Not an undo step, like up and down.
    Result goToLevel(LevelID k);

    // Move a whole level -- its start and every node on it, bends kept -- so
    // that it starts at `base`; it becomes current. Dragging its dot ends in
    // this. All or nothing: refused, naming the nodes, if any would reach the
    // level above or below, or if its start would pass a neighbour's.
    Result moveLevel(LevelID k, float base);

    // i and k: the current level's start steps by 0.05, or 0.01 when
    // `fine`, landing on the next multiple as heights do; every node with it.
    Result stepLevel(int direction, bool fine);

    // m (round 3A-6c): the selected nodes take the height, on the current
    // level, of the node selected FIRST -- so click the one to match, then
    // the others. A box has no first node and is refused. All or nothing:
    // refused, naming them, if any would reach a level next to it.
    Result matchHeights(const Selection& sel);

    // M: the selected nodes back to the current level's start, unbent. All
    // or nothing, as m.
    Result flattenHeights(const Selection& sel);

    // ---- node values (round 3A-7) --------------------------------------------
    //
    // A value is a named number each node carries on each level; what it
    // means is up to what reads it. No names are built in.

    // g, then a typed line: "name", or "name lo hi" with lo below hi (0 to 1
    // if no range is given). Every node starts at the bottom of the range.
    // It becomes the current value. Refused for no name, a name taken, or a
    // range that is not two numbers low then high.
    Result newValue(const std::string& line);

    // z, then the line: the current value renamed and re-ranged, every number
    // it holds -- and its fallback -- brought inside the new range. Refused
    // as newValue is. What z opens the line with: "name lo hi".
    Result editValue(const std::string& line);
    std::string valueLine() const;

    // j and J: the next or previous value becomes current. Not an undo step.
    Result pickValue(int direction);

    // ; and ': the selected nodes' current value on the current level, by a
    // twentieth of its range (a hundredth when `fine`), landing on the next
    // multiple as fill does, and held inside the range.
    Result stepValue(const Selection& sel, int direction, bool fine);

    // G: the current value goes, from every node on every level.
    Result removeValue();

    // cmd-delete: the current level goes, and the one below becomes current.
    // Refused for the only level.
    Result removeLevel();

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
    void selectOnlyOutput(TargetID t);
    bool outputSelected(TargetID t) const;
    std::string selectedOutputNames() const;

    Manifold2D     manifold_;
    // A node at q exactly, and -- with auto-output -- each node its own
    // outputs, level by level.
    NodeID placeAt(glm::vec2 q);
    void   giveOutputs(const std::vector<NodeID>& ids);

    Map3D          map_;
    LevelID        level_ = 0;
    std::size_t    currentValue_ = NoValue;
    TargetID       currentOutput_ = InvalidTarget;
    std::vector<TargetID> outputSel_;
    bool           autoOutput_ = true;
    TopologyReport topology_;
};

// A line on the map from one node to another: `from` feeds the output named
// after `to`. Only links between two NODES are drawn -- an output with no node
// of its own is shown by its fader, not by a line.
//
//   heavy   the sender is selected; light when only the receiver is
//   share   this link's part of the sender's whole share, silence included.
//           A node's lines, plus its own output (which draws no line, since
//           it would join the node to itself), plus its silence, add up to
//           100%.
//   lane    0 alone; +1 or -1 when the two nodes also feed each other the
//           other way
//   bend    which side the line arcs to, +1 or -1, in the frame running from
//           the lower node id to the higher. Two nodes feeding each other
//           arc to opposite sides. Otherwise the side with more CLEARANCE:
//           the one whose nearest other node is farther away, so a line never
//           looks as if it connects to a node it merely passes. What makes a
//           line look connected is how close it comes to the nearest node,
//           not how many it passes -- counting nodes would send an arc to
//           within a hair of one node to avoid two at a comfortable distance.
//           +1 when the two sides are equal.
struct LinkLine {
    NodeID   from   = InvalidNode;
    NodeID   to     = InvalidNode;
    TargetID output = InvalidTarget;
    float    share  = 0.0f;
    bool     heavy  = false;
    int      lane   = 0;
    int      bend   = +1;
};

// The lines to draw for a selection: every link out of a selected node, and
// every link into one, each ONCE -- a link between two selected nodes is both
// and is drawn once, heavy.
std::vector<LinkLine> linkLines(const Model& model, const Selection& sel);

// A point on a link's arc, in MAP coordinates, `t` measured from the SENDING
// node (0) to the receiving one (1). The arc is a quadratic curve whose
// control point sits a quarter of the line's length out from its middle. An
// editor draws the line by sampling this, and puts the percentage near the
// sender, where lines leaving one node fan apart, instead of at the middle.
glm::vec2 linkPoint(const Model& model, const LinkLine& line, float t);

} // namespace editor
} // namespace ofxManifold
