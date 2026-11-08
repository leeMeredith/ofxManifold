#include "ofxManifoldEditorModel.h"

#include "authoring/ofxManifoldRing.h"
#include "mapping/ofxManifoldRig.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace ofxManifold {
namespace editor {

namespace {

Result refusal(const std::string& msg, float seconds) {
    Result r;
    r.refused = true;
    r.message = msg;
    r.seconds = seconds;
    return r;
}

Result changed(const std::string& msg, float seconds) {
    Result r;
    r.changed = true;
    r.message = msg;
    r.seconds = seconds;
    return r;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + (n == 1 ? one : many);
}

// A height in a message: six significant digits, as the vectors write them.
std::string height(float h) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", double(h));
    return b;
}

// Node `nm`'s own output on level k (0-based): nm itself on the first level,
// nm-<level> past it (3A-6d).
std::string levelOutput(const std::string& nm, LevelID k) {
    return k == 0 ? nm : nm + "-" + std::to_string(k + 1);
}

// Is `out` node nm's own output: nm, or nm-<digits>, maybe with _<digits>
// added to keep it unique?
bool owns(const std::string& nm, const std::string& out) {
    if (out == nm) return true;
    if (out.size() <= nm.size() + 1 || out.compare(0, nm.size(), nm) != 0
        || out[nm.size()] != '-') {
        return false;
    }
    std::size_t i = nm.size() + 1;
    auto digits = [&]() {
        const std::size_t from = i;
        while (i < out.size() && out[i] >= '0' && out[i] <= '9') ++i;
        return i > from;
    };
    if (!digits()) return false;
    if (i == out.size()) return true;
    if (out[i] != '_') return false;
    ++i;
    return digits() && i == out.size();
}

// An offset from a level's start, always signed: "+0.05", "-0.1", "+0".
std::string offset(float d) {
    char b[32];
    std::snprintf(b, sizeof b, "%+.3g", double(d));
    return b;
}

} // namespace

NodeID Model::ownerOf(const std::string& output) const {
    const NodeID exact = manifold_.findNode(output);
    if (exact != InvalidNode) return exact;
    for (NodeID n = 0; n < manifold_.nodeCount(); ++n) {
        if (owns(manifold_.node(n).name, output)) return n;
    }
    return InvalidNode;
}

// ---------------------------------------------------------------------------
// whole maps
// ---------------------------------------------------------------------------

void Model::loadExample() {
    manifold_ = Manifold2D();

    // A fan of four triangles...
    const auto O = manifold_.addNode("O", {0.30f, 0.50f});
    const auto N = manifold_.addNode("N", {0.30f, 0.80f});
    const auto E = manifold_.addNode("E", {0.50f, 0.50f});
    const auto S = manifold_.addNode("S", {0.30f, 0.20f});
    const auto W = manifold_.addNode("W", {0.10f, 0.50f});
    manifold_.addTriangle(O, N, E);
    manifold_.addTriangle(O, E, S);
    manifold_.addTriangle(O, S, W);
    manifold_.addTriangle(O, W, N);

    // ...and a quad. Drag one of its corners across the opposite edge and the
    // move is refused: the ring would cross itself (D-019).
    const auto A = manifold_.addNode("A", {0.65f, 0.35f});
    const auto B = manifold_.addNode("B", {0.90f, 0.35f});
    const auto C = manifold_.addNode("C", {0.90f, 0.65f});
    const auto D = manifold_.addNode("D", {0.65f, 0.65f});
    manifold_.addRegion({A, B, C, D});

    // Outputs, and bindings chosen so every node shape appears at once:
    //
    //   N W E S   terminal, one output each      filled circle
    //   O         composite, all four            filled triangle
    //   A         bound to nothing               hollow square
    //   B         right, and half to silence     circle, half filled
    //   C         left and right                 triangle
    //   D         rear, and a quarter silent     circle, three-quarters filled
    //
    // plus a derived "sub" fed by N and O, at its own channel.
    map_ = Map3D();
    level_ = 0;
    currentValue_ = NoValue;
    const TargetID front = map_.addOutput("front", 0);
    const TargetID left  = map_.addOutput("left", 1);
    const TargetID right = map_.addOutput("right", 2);
    const TargetID rear  = map_.addOutput("rear", 3);
    map_.bind(0, N, front);
    map_.bind(0, W, left);
    map_.bind(0, E, right);
    map_.bind(0, S, rear);
    for (TargetID t : {front, left, right, rear}) map_.bind(0, O, t);
    map_.bind(0, B, right);
    map_.bindSilence(0, B, 1.0f);
    map_.bind(0, C, left);
    map_.bind(0, C, right);
    map_.bind(0, D, rear);
    map_.bindSilence(0, D, 1.0f / 3.0f);
    map_.addDerived("sub", 4, 1.0f, SumMode::Linear, {{N, 1.0f}, {O, 1.0f}});
    (void)A;

    selectOnlyOutput(front);
    settleCurrentOutput();
    refreshTopology();
}

void Model::newEmpty() {
    manifold_ = Manifold2D();
    map_ = Map3D();
    level_ = 0;
    currentValue_ = NoValue;
    selectOnlyOutput(InvalidTarget);
    settleCurrentOutput();
    refreshTopology();
}

void Model::adopt(Manifold2D m, Mapping mp) {
    Map3D one;
    std::vector<Mapping> routing;
    routing.push_back(std::move(mp));
    one.adopt(Levels(), std::move(routing));   // one level always agrees
    adopt(std::move(m), std::move(one));
}

void Model::adopt(Manifold2D m, Map3D map) {
    manifold_ = std::move(m);
    map_ = std::move(map);
    level_ = 0;
    currentValue_ = map_.valueSet().empty() ? NoValue : 0;
    selectOnlyOutput(InvalidTarget);
    settleCurrentOutput();
    refreshTopology();
}

// The current output must name an output that exists; with any outputs at all
// and none current, the first becomes current.
void Model::settleCurrentOutput() {
    if (currentOutput_ != InvalidTarget
        && currentOutput_ >= mapping().targetCount()) {
        currentOutput_ = InvalidTarget;
    }
    if (currentOutput_ == InvalidTarget && mapping().targetCount() > 0) {
        currentOutput_ = 0;
    }
    // The selected outputs follow: none that no longer exist, and the
    // current output always among them.
    std::vector<TargetID> kept;
    for (TargetID t : outputSel_) {
        if (t < mapping().targetCount()) kept.push_back(t);
    }
    outputSel_ = kept;
    if (currentOutput_ == InvalidTarget) outputSel_.clear();
    else if (!outputSelected(currentOutput_)) outputSel_.push_back(currentOutput_);
}

void Model::selectOnlyOutput(TargetID t) {
    currentOutput_ = t;
    outputSel_.clear();
    if (t != InvalidTarget) outputSel_.push_back(t);
}

bool Model::outputSelected(TargetID t) const {
    for (TargetID s : outputSel_) if (s == t) return true;
    return false;
}

std::string Model::selectedOutputNames() const {
    std::string names;
    for (TargetID t : outputSel_) {
        names += (names.empty() ? "" : ", ") + mapping().targetName(t);
    }
    return names;
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

// A name free as a NODE name and as an OUTPUT name. An output can outlive the
// node it was made for, when other nodes still feed it; a new node given that
// name would find its auto-output already taken and be left unbound without a
// word.
std::string Model::freshName() const {
    for (std::size_t k = manifold_.nodeCount(); ; ++k) {
        const std::string nm = "n" + std::to_string(k);
        if (manifold_.findNode(nm) == InvalidNode
            && mapping().findTarget(nm) == InvalidTarget) {
            return nm;
        }
    }
}

int Model::nextFreeChannel() const {
    int ch = 0;
    while (mapping().channelInUse(ch)) ++ch;
    return ch;
}

std::size_t Model::outputCount(NodeID id) const {
    std::size_t n = 0;
    for (std::size_t k = 0; k < mapping().linkCount(id); ++k) {
        if (mapping().link(id, k).kind == DestKind::Output) ++n;
    }
    return n;
}

bool Model::feeds(NodeID node, TargetID output) const {
    for (std::size_t k = 0; k < mapping().linkCount(node); ++k) {
        const Link& l = mapping().link(node, k);
        if (l.kind == DestKind::Output && l.id == output) return true;
    }
    return false;
}

// On ANY level: deleting a node must not take away an output another level's
// nodes still feed.
bool Model::outputInUse(TargetID t) const {
    for (LevelID k = 0; k < map_.count(); ++k) {
        const Mapping& mp = map_.level(k);
        for (NodeID n : mp.boundNodes()) {
            for (std::size_t i = 0; i < mp.linkCount(n); ++i) {
                const Link& l = mp.link(n, i);
                if (l.kind == DestKind::Output && l.id == t) return true;
            }
        }
    }
    return false;
}

// Duplicate detection: by grid address where the grid has addresses, by a
// distance tolerance in free mode, which has none (authoring decision F).
bool Model::occupied(glm::vec2 p, GridAddress addr, const Grid& grid) const {
    for (std::size_t i = 0; i < manifold_.nodeCount(); ++i) {
        const glm::vec2 q = manifold_.node(NodeID(i)).position;
        if (addr.valid) {
            if (grid.snap(q).address == addr
                && glm::distance(grid.point(addr), q) < 1e-4f) {
                return true;
            }
        } else if (glm::distance(p, q) < 0.01f) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// nodes and regions
// ---------------------------------------------------------------------------

Result Model::place(glm::vec2 p, const Grid& grid, NodeID* placed) {
    const SnapResult s = grid.snap(p);
    if (s.point.x < 0.0f || s.point.x > 1.0f
        || s.point.y < 0.0f || s.point.y > 1.0f) {
        return Result{};                       // off the map: nothing to say
    }
    if (occupied(s.point, s.address, grid)) {
        return refusal("a node is already there", 1.5f);
    }
    const NodeID id = placeAt(s.point);
    if (id == InvalidNode) return Result{};
    giveOutputs({id});
    refreshTopology();
    if (placed) *placed = id;
    Result r;
    r.changed = true;
    return r;
}

NodeID Model::placeAt(glm::vec2 q) {
    return manifold_.addNode(freshName(), q);
}

// The output takes the NODE'S NAME. That is what pairs them, and it survives
// save and reload with no extra bookkeeping -- which is how deleting the node
// later finds its own fader.
//
// With levels, an output of its own on EVERY level (3A-6d): each level's node
// puts out its own number -- P, P-2, P-3 -- each made only if its name is
// free. Level by level, so one level's outputs take one run of channels.
void Model::giveOutputs(const std::vector<NodeID>& ids) {
    if (!autoOutput_) return;
    TargetID first = InvalidTarget;
    for (LevelID k = 0; k < map_.count(); ++k) {
        for (NodeID id : ids) {
            const std::string on = levelOutput(manifold_.node(id).name, k);
            if (mapping().findTarget(on) != InvalidTarget) continue;
            const TargetID t = map_.addOutput(on, nextFreeChannel());
            if (t == InvalidTarget) continue;
            map_.bind(k, id, t);
            if (first == InvalidTarget) first = t;
        }
    }
    // The current output changes only if there was none. It used to follow
    // every new node, so the output `b` acted on was usually the one the
    // newest node already fed -- and b, a toggle, then UNBOUND it, leaving it
    // silent. The current output is the one the user picked, and placing a
    // node must not move it.
    if (first != InvalidTarget && currentOutput_ == InvalidTarget) {
        selectOnlyOutput(first);
    }
}

namespace {

struct TriStage { int k; bool mid; };

TriStage triStage(int stage) {
    // (2), (2 mid), (3), (3 mid) ... (7 mid), (8)
    const int i = std::min(std::max(stage, 1), kTriangleStages) - 1;
    return {2 + i / 2, (i % 2) == 1};
}

// The triangle's points as (row, column), apex first, left to right, and its
// small triangles, each counter-clockwise.
void triShape(int stage, std::vector<std::pair<int, int>>& pts,
              std::vector<std::array<std::pair<int, int>, 3>>& tris) {
    const TriStage st = triStage(stage);
    const int k = st.k;
    pts.clear();
    tris.clear();
    for (int r = 0; r < k; ++r) {
        for (int c = 0; c <= r; ++c) pts.push_back({r, c});
    }
    if (st.mid) {
        for (int c = 1; c < k; ++c) pts.push_back({k, c});
    }
    for (int r = 0; r + 1 < k; ++r) {
        for (int c = 0; c <= r; ++c) {
            tris.push_back({{{r, c}, {r + 1, c}, {r + 1, c + 1}}});
            if (c < r) tris.push_back({{{r, c}, {r + 1, c + 1}, {r, c + 1}}});
        }
    }
    if (st.mid) {
        for (int c = 0; c + 1 < k; ++c) {
            tris.push_back({{{k - 1, c}, {k, c + 1}, {k - 1, c + 1}}});
            if (c >= 1) tris.push_back({{{k - 1, c}, {k, c}, {k, c + 1}}});
        }
    }
}

LayoutPlan notLaidOut(const std::string& why) {
    LayoutPlan p;
    p.message = why;
    return p;
}

} // namespace

int triangleStageNodes(int stage) {
    const TriStage st = triStage(stage);
    return st.k * (st.k + 1) / 2 + (st.mid ? st.k - 1 : 0);
}

LayoutPlan Model::planLayout(glm::vec2 p, const Grid& grid, int size,
                             bool triangles) const {
    if (grid.mode() == GridMode::Free) {
        return notLaidOut("pick a square, triangular or polar grid first "
                          "(2, 3 or 4)");
    }
    const SnapResult snap = grid.snap(p);
    std::vector<GridAddress> addrs;
    std::vector<std::vector<std::size_t>> regions;
    std::string what, msg;
    auto address = [](int a, int b) {
        GridAddress g;
        g.a = a; g.b = b; g.valid = true;
        return g;
    };

    if (grid.mode() == GridMode::Polar) {
        const int ring = snap.address.a;
        if (ring == 0) return notLaidOut("point at a ring, not the centre");
        if (size > ring + 1) {
            return notLaidOut("refused: the rings would pass the centre -- "
                              "point further out");
        }
        // The spoke nearest the front; of two equally near, the one just
        // right of it. Then clockwise -- with up as +y, clockwise is falling
        // angle, so falling spoke number.
        const int S = grid.spokes();
        const double twoPi = 2.0 * 3.14159265358979323846;
        int b0 = 0;
        double bestD = 1e9, bestCw = 1e9;
        for (int b = 0; b < S; ++b) {
            const double t = double(grid.angle()) + b * twoPi / double(S);
            double cw = std::fmod(twoPi * 0.25 - t, twoPi);
            if (cw < 0.0) cw += twoPi;
            const double d = std::min(cw, twoPi - cw);
            if (d < bestD - 1e-5 || (std::fabs(d - bestD) <= 1e-5 && cw < bestCw)) {
                bestD = d; bestCw = cw; b0 = b;
            }
        }
        auto sp = [&](int i) { return ((b0 - i) % S + S) % S; };
        // Outermost ring first, the centre last when the rings reach it.
        std::vector<int> rings;
        for (int k = 0; k < size; ++k) {
            if (ring - k >= 1) rings.push_back(ring - k);
        }
        const bool centre = ring - size + 1 == 0;
        for (int r : rings) {
            for (int i = 0; i < S; ++i) addrs.push_back(address(r, sp(i)));
        }
        if (centre) addrs.push_back(address(0, 0));
        // Ring r's node on spoke sp(i) is rings-index * S + i.
        auto at = [&](std::size_t ri, int i) {
            return ri * std::size_t(S) + std::size_t(((i % S) + S) % S);
        };
        for (std::size_t ri = 0; ri + 1 < rings.size(); ++ri) {
            for (int i = 0; i < S; ++i) {
                regions.push_back({at(ri, i), at(ri, i + 1),
                                   at(ri + 1, i + 1), at(ri + 1, i)});
            }
        }
        const std::size_t last = rings.size() - 1;
        if (centre) {
            const std::size_t c = addrs.size() - 1;
            for (int i = 0; i < S; ++i) {
                regions.push_back({c, at(last, i), at(last, i + 1)});
            }
        } else {
            std::vector<std::size_t> whole;
            for (int i = 0; i < S; ++i) whole.push_back(at(last, i));
            regions.push_back(whole);
        }
        if (size == 1) {
            msg = "ring of " + std::to_string(addrs.size()) + " nodes";
        } else {
            msg = std::to_string(rings.size())
                + (rings.size() == 1 ? " ring" : " rings")
                + (centre ? " and the centre" : "")
                + ", " + std::to_string(addrs.size()) + " nodes";
        }
        what = "ring";
    } else if (triangles) {
        std::vector<std::pair<int, int>> shape;
        std::vector<std::array<std::pair<int, int>, 3>> tris;
        triShape(size, shape, tris);
        // Placed so its middle is nearest the point.
        const glm::vec2 o = grid.point(address(0, 0));
        double ox = 0.0, oy = 0.0;
        for (const auto& rc : shape) {
            const glm::vec2 q = grid.point(address(rc.second, -rc.first));
            ox += double(q.x) - double(o.x);
            oy += double(q.y) - double(o.y);
        }
        ox /= double(shape.size());
        oy /= double(shape.size());
        const GridAddress apex =
            grid.snap({float(p.x - ox), float(p.y - oy)}).address;
        for (const auto& rc : shape) {
            addrs.push_back(address(apex.a + rc.second, apex.b - rc.first));
        }
        auto index = [&](std::pair<int, int> rc) {
            return std::size_t(std::find(shape.begin(), shape.end(), rc)
                               - shape.begin());
        };
        for (const auto& t : tris) {
            regions.push_back({index(t[0]), index(t[1]), index(t[2])});
        }
        const bool mid = triStage(size).mid;
        if (size == 2) {
            msg = "diamond of 4 nodes";
        } else if (mid) {
            msg = std::to_string(addrs.size()) + " nodes in "
                + std::to_string(regions.size()) + " triangles";
        } else {
            msg = "triangle of " + std::to_string(addrs.size()) + " nodes";
        }
        what = "triangle";
    } else {
        const int i0 = snap.address.a - (size - 1) / 2;
        const int j0 = snap.address.b - (size - 1) / 2;
        for (int j = size - 1; j >= 0; --j) {
            for (int i = 0; i < size; ++i) {
                addrs.push_back(address(i0 + i, j0 + j));
            }
        }
        // Rows from the front: (i, j) is at (size - 1 - j) * size + i.
        auto at = [&](int i, int j) {
            return std::size_t((size - 1 - j) * size + i);
        };
        for (int j = 0; j + 1 < size; ++j) {
            for (int i = 0; i + 1 < size; ++i) {
                regions.push_back({at(i, j), at(i + 1, j),
                                   at(i + 1, j + 1), at(i, j + 1)});
            }
        }
        msg = std::to_string(size) + " x " + std::to_string(size) + " block, "
            + std::to_string(size * size) + " nodes";
        what = "block";
    }

    // All or nothing: every spot on the map, and free. On the map within a
    // hair -- the spacing is a float, so a ring touching the edge (radius
    // 0.5 = 5 x 0.1f) lands a few 1e-9 outside it -- and then held on it.
    const float tol = 1e-5f;
    LayoutPlan plan;
    for (const GridAddress& a : addrs) plan.points.push_back(grid.point(a));
    for (glm::vec2& q : plan.points) {
        if (q.x < -tol || q.x > 1.0f + tol || q.y < -tol || q.y > 1.0f + tol) {
            return notLaidOut("refused: the " + what + " would run off the map");
        }
        q = glm::clamp(q, glm::vec2(0.0f), glm::vec2(1.0f));
    }
    for (std::size_t i = 0; i < addrs.size(); ++i) {
        if (occupied(plan.points[i], addrs[i], grid)) {
            return notLaidOut("refused: a node is already in the way");
        }
    }
    plan.ok = true;
    plan.message = msg;
    plan.regions = regions;
    return plan;
}

Result Model::layout(glm::vec2 p, const Grid& grid, int size, bool triangles,
                     std::vector<NodeID>* placed, bool join) {
    if (placed) placed->clear();
    const LayoutPlan plan = planLayout(p, grid, size, triangles);
    if (!plan.ok) return refusal(plan.message, 2.5f);

    // The nodes first, then their outputs level by level, so each level's
    // take one run of channels: a ring of 8 on two levels is 0-7, 8-15.
    std::vector<NodeID> ids;
    for (const glm::vec2& q : plan.points) ids.push_back(placeAt(q));
    giveOutputs(ids);
    if (!join) {
        if (placed) *placed = ids;
        refreshTopology();
        return changed(std::to_string(ids.size()) + " nodes laid out, not "
                       "joined -- F joins the grid's cells", 3.0f);
    }
    for (const auto& r : plan.regions) {
        std::vector<NodeID> region;
        for (std::size_t i : r) region.push_back(ids[i]);
        manifold_.addRegion(region);
    }
    if (placed) *placed = ids;
    refreshTopology();
    return changed(plan.message, 2.5f);
}

namespace {

bool onMap(glm::vec2 q) {
    const float hair = 1e-5f;
    return q.x >= -hair && q.x <= 1.0f + hair && q.y >= -hair && q.y <= 1.0f + hair;
}

GridAddress addr(int a, int b) {
    GridAddress g;
    g.a = a; g.b = b; g.valid = true;
    return g;
}

} // namespace

int wholeRings(const Grid& grid) {
    if (grid.mode() != GridMode::Polar) return 0;
    int r = 0;
    while (r < 1000) {
        bool all = true;
        for (int b = 0; b < grid.spokes() && all; ++b) {
            all = onMap(grid.point(addr(r + 1, b)));
        }
        if (!all) break;
        ++r;
    }
    return r;
}

std::vector<std::vector<GridAddress>> gridCells(const Grid& grid,
                                                bool triangles) {
    std::vector<std::vector<GridAddress>> cells;
    if (grid.mode() == GridMode::Free) return cells;
    if (grid.mode() == GridMode::Polar) {
        const int S = grid.spokes(), R = wholeRings(grid);
        if (R >= 1) {
            for (int b = 0; b < S; ++b) {
                cells.push_back({addr(0, 0), addr(1, b), addr(1, (b + 1) % S)});
            }
        }
        for (int r = 1; r < R; ++r) {
            for (int b = 0; b < S; ++b) {
                cells.push_back({addr(r, b), addr(r, (b + 1) % S),
                                 addr(r + 1, (b + 1) % S), addr(r + 1, b)});
            }
        }
        return cells;
    }
    const int n = int(1.6 / grid.spacing()) + 2;
    auto whole = [&](const std::vector<GridAddress>& c) {
        for (const GridAddress& a : c) {
            if (!onMap(grid.point(a))) return false;
        }
        return true;
    };
    for (int j = -n; j <= n; ++j) {
        for (int i = -n; i <= n; ++i) {
            if (triangles) {
                std::vector<GridAddress> up{addr(i, j), addr(i + 1, j), addr(i, j + 1)};
                std::vector<GridAddress> down{addr(i + 1, j), addr(i + 1, j + 1),
                                              addr(i, j + 1)};
                if (whole(up)) cells.push_back(up);
                if (whole(down)) cells.push_back(down);
            } else {
                std::vector<GridAddress> sq{addr(i, j), addr(i + 1, j),
                                            addr(i + 1, j + 1), addr(i, j + 1)};
                if (whole(sq)) cells.push_back(sq);
            }
        }
    }
    return cells;
}

Result Model::joinCells(const Selection& sel, const Grid& grid,
                        bool triangles) {
    if (grid.mode() == GridMode::Free) {
        return refusal("pick a square, triangular or polar grid first "
                       "(2, 3 or 4)", 2.5f);
    }
    const bool polar = grid.mode() == GridMode::Polar;
    // Each selected node on a grid point, by its address; the centre is one
    // point whatever its spoke.
    std::map<std::pair<int, int>, NodeID> at;
    for (NodeID id : sel.nodes()) {
        const glm::vec2 q = manifold_.node(id).position;
        const SnapResult sr = grid.snap(q);
        if (!sr.address.valid
            || glm::distance(grid.point(sr.address), q) >= 1e-4f) continue;
        std::pair<int, int> k{sr.address.a, sr.address.b};
        if (polar && k.first == 0) k.second = 0;
        at[k] = id;
    }
    std::set<std::vector<NodeID>> have;
    for (std::size_t r = 0; r < manifold_.regionCount(); ++r) {
        std::vector<NodeID> ids = manifold_.region(RegionID(r)).ids();
        std::sort(ids.begin(), ids.end());
        have.insert(ids);
    }
    std::size_t made = 0, skipped = 0;
    auto add = [&](const std::vector<NodeID>& ring) {
        std::vector<NodeID> key = ring;
        std::sort(key.begin(), key.end());
        if (have.count(key)) { ++skipped; return; }
        if (manifold_.addRegion(ring) == InvalidRegion) return;
        have.insert(key);
        ++made;
    };
    for (const auto& c : gridCells(grid, triangles)) {
        std::vector<NodeID> ring;
        for (const GridAddress& a : c) {
            const auto it = at.find({a.a, a.b});
            if (it == at.end()) break;
            ring.push_back(it->second);
        }
        if (ring.size() == c.size()) add(ring);
    }
    if (polar) {
        const int S = grid.spokes();
        auto full = [&](int r) {
            for (int b = 0; b < S; ++b) {
                if (!at.count({r, b})) return false;
            }
            return true;
        };
        for (int r = 1; r <= wholeRings(grid); ++r) {
            const bool inner = r == 1 ? at.count({0, 0}) > 0 : full(r - 1);
            if (!full(r) || inner) continue;
            std::vector<NodeID> ring;
            for (int b = 0; b < S; ++b) ring.push_back(at[{r, b}]);
            add(ring);
        }
    }
    if (made == 0) {
        return refusal(skipped ? "those cells are joined already"
                               : "no whole cell among the selected -- each "
                                 "needs every corner selected and on the grid",
                       3.0f);
    }
    refreshTopology();
    std::string msg = "joined " + plural(made, " grid cell", " grid cells");
    if (skipped) msg += ", " + std::to_string(skipped) + " joined already";
    return changed(msg, 2.5f);
}

std::vector<NodeID> Model::linedUp(glm::vec2 p, const Grid& grid,
                                   bool triangles) const {
    std::vector<NodeID> out;
    if (grid.mode() == GridMode::Free) return out;
    const GridAddress at = grid.snap(p).address;
    const bool polar = grid.mode() == GridMode::Polar;
    for (NodeID id = 0; id < manifold_.nodeCount(); ++id) {
        const glm::vec2 q = manifold_.node(id).position;
        const GridAddress a = grid.snap(q).address;
        if (!a.valid || glm::distance(grid.point(a), q) >= 1e-4f) continue;
        bool hit;
        if (polar) {
            hit = at.a == 0 || a.a == 0 || a.a == at.a || a.b == at.b;
        } else {
            hit = a.a == at.a || a.b == at.b
               || (triangles && a.a + a.b == at.a + at.b);
        }
        if (hit) out.push_back(id);
    }
    std::sort(out.begin(), out.end(), [this](NodeID x, NodeID y) {
        return manifold_.node(x).name < manifold_.node(y).name;
    });
    return out;
}

// One operation, all or nothing, checked against FINAL shapes. On refusal
// nothing changes, so the nodes stay at their last valid positions.
Result Model::move(const std::vector<std::pair<NodeID, glm::vec2>>& moves) {
    if (!manifold_.setNodePositions(moves)) {
        return refusal(
            "move refused: a region would invert, flatten or cross itself",
            1.5f);
    }
    Result r;
    r.changed = true;
    return r;
}

// Join the selected nodes into one region. ORDER decides the shape:
//
//   chosen by individual clicks  -> the order clicked
//   any part chosen by a box     -> angle about the centroid
//
// Angle order recovers every convex shape and every star, but not an L -- for
// an L it makes a different ring that is still a legal region. A click-ordered
// ring that crosses itself is REFUSED with the reason, never quietly re-sorted.
Result Model::join(const Selection& sel) {
    if (sel.size() < 3) {
        return refusal("select at least three nodes to join", 2.0f);
    }
    std::vector<NodeID> ring = sel.nodes();
    if (!sel.ordered()) {
        std::vector<glm::vec2> pts;
        for (NodeID id : sel.nodes()) pts.push_back(manifold_.node(id).position);
        ring.clear();
        for (std::size_t i : orderByAngle(pts)) ring.push_back(sel.nodes()[i]);
    }

    // The same set of nodes as an existing region is a duplicate; refused here
    // rather than created and reported by validate().
    std::vector<NodeID> key = ring;
    std::sort(key.begin(), key.end());
    for (std::size_t r = 0; r < manifold_.regionCount(); ++r) {
        std::vector<NodeID> have = manifold_.region(RegionID(r)).ids();
        std::sort(have.begin(), have.end());
        if (have == key) {
            return refusal("those nodes already form a region", 2.0f);
        }
    }

    if (manifold_.addRegion(ring) == InvalidRegion) {
        return refusal("refused: " + manifold_.lastRegionError()
                       + (sel.ordered() ? " (in click order)" : ""), 3.0f);
    }
    refreshTopology();
    return changed("joined " + std::to_string(ring.size()) + " nodes in "
                   + (sel.ordered() ? "click order" : "angle order"), 2.0f);
}

// Remove every region whose nodes are ALL selected, keeping the nodes.
Result Model::unjoin(const Selection& sel) {
    std::vector<RegionID> doomed;
    for (std::size_t r = 0; r < manifold_.regionCount(); ++r) {
        bool all = true;
        for (NodeID id : manifold_.region(RegionID(r)).ids()) {
            if (!sel.contains(id)) { all = false; break; }
        }
        if (all) doomed.push_back(RegionID(r));
    }
    if (doomed.empty()) {
        return refusal("no region is made only of selected nodes", 2.5f);
    }
    manifold_.removeRegions(doomed);
    refreshTopology();
    return changed("unjoined " + plural(doomed.size(), " region", " regions"),
                   2.5f);
}

Result Model::remove(const Selection& sel) {
    if (sel.empty()) return Result{};
    const std::size_t n = sel.size();
    std::size_t regionsGone = 0;

    // NodeIDs after the first removed one are renumbered. The mapping holds
    // NodeIDs, so it MUST follow, or every binding after the deleted node
    // would silently move to the wrong node (outputs decision E, D-020).
    std::vector<std::string> names;
    for (NodeID id : sel.nodes()) names.push_back(manifold_.node(id).name);
    std::vector<NodeID> remap;
    manifold_.removeNodes(sel.nodes(), &remap, &regionsGone);
    map_.remapNodes(remap);   // every level's heights, bindings and values

    // A node's own output -- the one named after it -- goes with it, but ONLY
    // if nothing else is bound to it. Otherwise deleting a speaker's node
    // would silently take away a fader other nodes still feed. Outputs made
    // by hand never share a node's name, so they are never removed this way.
    // With levels, its own outputs on the other levels too (nm-2, ...).
    std::size_t outsGone = 0;
    for (const std::string& nm : names) {
        std::vector<std::string> own;
        for (TargetID t = 0; t < mapping().targetCount(); ++t) {
            if (owns(nm, mapping().targetName(t))) {
                own.push_back(mapping().targetName(t));
            }
        }
        for (const std::string& on : own) {
            const TargetID t = mapping().findTarget(on);
            if (t != InvalidTarget && !outputInUse(t)) {
                map_.removeOutput(t);
                afterOutputRemoved(t);
                ++outsGone;
            }
        }
    }
    refreshTopology();
    return changed("removed " + plural(n, " node", " nodes")
                   + (regionsGone ? ", and "
                                    + plural(regionsGone, " region", " regions")
                                    + " that used them"
                                  : std::string())
                   + (outsGone ? ", and "
                                 + plural(outsGone, " output", " outputs")
                               : std::string()),
                   3.0f);
}

// ---------------------------------------------------------------------------
// outputs
// ---------------------------------------------------------------------------

Result Model::newOutput() {
    const int ch = nextFreeChannel();
    std::string nm = "out" + std::to_string(ch);
    for (int k = 2; mapping().findTarget(nm) != InvalidTarget; ++k) {
        nm = "out" + std::to_string(ch) + "_" + std::to_string(k);
    }
    selectOnlyOutput(map_.addOutput(nm, ch));
    return changed("new output " + nm + " on channel " + std::to_string(ch)
                   + " -- select nodes and press b", 3.0f);
}

// A derived output summing the selected nodes: the sub under a pair, say. Not
// a node -- it has no position -- so it appears in the panel and the chart.
Result Model::newDerived(const Selection& sel) {
    if (sel.empty()) {
        return refusal("select the nodes a derived output should sum", 2.5f);
    }
    const int ch = nextFreeChannel();
    const std::string nm = "sum" + std::to_string(ch);
    std::vector<std::pair<NodeID, float>> srcs;
    for (NodeID id : sel.nodes()) srcs.emplace_back(id, 1.0f);
    map_.addDerived(nm, ch, 1.0f, SumMode::Linear, srcs);
    return changed("derived " + nm + " on channel " + std::to_string(ch)
                   + ", sum of " + std::to_string(srcs.size()), 3.0f);
}

Result Model::newDerivedAfter() {
    if (outputSel_.empty()) {
        return refusal("no output yet -- press o to make one", 2.5f);
    }
    const int ch = nextFreeChannel();
    const std::string nm = "sum" + std::to_string(ch);
    std::vector<std::pair<TargetID, float>> srcs;
    for (TargetID t : outputSel_) srcs.emplace_back(t, 1.0f);
    map_.addDerivedFromOutputs(nm, ch, 1.0f, SumMode::Linear, srcs);
    return changed("derived " + nm + " on channel " + std::to_string(ch)
                   + ", after " + selectedOutputNames(), 3.0f);
}

Result Model::useRig(const Mapping& rig, const std::string& name) {
    // Outputs may come back in another order: carry the current and picked
    // outputs across by name.
    const std::string current = currentOutput_ == InvalidTarget
        ? std::string() : mapping().targetName(currentOutput_);
    std::vector<std::string> picked;
    for (TargetID t : outputSel_) picked.push_back(mapping().targetName(t));

    // Every level at once, or none (Map3D::useRig).
    std::vector<std::string> refused;
    if (!map_.useRig(rig, &refused)) {
        std::string list;
        for (std::size_t i = 0; i < refused.size(); ++i) {
            list += (i ? ", " : "") + refused[i];
        }
        return refusal("cannot use rig " + name + " -- not in it, or not "
                       "the same kind: " + list, 4.0f);
    }
    currentOutput_ = current.empty() ? InvalidTarget
                                     : mapping().findTarget(current);
    outputSel_.clear();
    for (const std::string& nm : picked) {
        const TargetID t = mapping().findTarget(nm);
        if (t != InvalidTarget) outputSel_.push_back(t);
    }
    settleCurrentOutput();
    return changed("using rig " + name + ": "
                   + std::to_string(mapping().targetCount()) + " outputs", 3.0f);
}

BindPlan Model::planBind(const Selection& sel) const {
    BindPlan p;
    if (outputSel_.empty()) {
        p.why = "no output yet -- press o to make one";
        return p;
    }
    if (sel.empty()) {
        p.why = "select the nodes to bind";
        return p;
    }
    p.possible = true;
    p.outputs = selectedOutputNames();
    p.unbind = true;
    for (NodeID id : sel.nodes()) {
        for (TargetID t : outputSel_) {
            if (!feeds(id, t)) p.unbind = false;
        }
    }
    for (NodeID id : sel.nodes()) {
        // A node changes if any selected output is not yet as wanted.
        bool changes = false;
        for (TargetID t : outputSel_) {
            if (feeds(id, t) == p.unbind) changes = true;
        }
        if (!changes) continue;
        ++p.count;
        // Unbinding leaves a node silent when every output it feeds is one
        // of the selected outputs, so none survives.
        if (p.unbind) {
            bool survives = false;
            for (std::size_t k = 0; k < mapping().linkCount(id); ++k) {
                const Link& l = mapping().link(id, k);
                if (l.kind != DestKind::Output) continue;
                if (!outputSelected(l.id)) survives = true;
            }
            if (!survives) p.silenced.push_back(manifold_.node(id).name);
        }
    }
    return p;
}

namespace {
std::string silencedNames(const std::vector<std::string>& s) {
    return s.size() == 1 ? s[0] : std::to_string(s.size()) + " nodes";
}
} // namespace

std::string BindPlan::preview() const {
    if (!possible) return "bind or unbind selected";
    std::string t = std::string(unbind ? "unbind " : "bind ")
                  + std::to_string(count) + (unbind ? " from " : " to ")
                  + outputs;
    if (!silenced.empty()) {
        t += " -- " + silencedNames(silenced)
           + (silenced.size() == 1 ? " goes silent" : " go silent");
    }
    return t;
}

std::string BindPlan::outcome() const {
    if (!possible) return why;
    std::string t = std::string(unbind ? "unbound " : "bound ")
                  + std::to_string(count) + (unbind ? " from " : " to ")
                  + outputs;
    if (!silenced.empty()) {
        t += " -- " + silencedNames(silenced) + " now silent; c gives back its own output";
    }
    return t;
}

Result Model::bind(const Selection& sel) {
    if (outputSel_.empty()) {
        return refusal("no output yet -- press o to make one", 2.5f);
    }
    if (sel.empty()) return Result{};

    // One output selected, or several (shift-click on faders): the toggle
    // works across all of them. If every selected node already feeds every
    // selected output, unbind them all; otherwise bind what is missing.
    bool all = true;
    for (NodeID id : sel.nodes()) {
        for (TargetID t : outputSel_) {
            if (!feeds(id, t)) { all = false; break; }
        }
        if (!all) break;
    }

    std::size_t n = 0;
    std::vector<std::string> silenced;
    for (NodeID id : sel.nodes()) {
        // The fill a node had, kept through the change: removing a binding
        // would otherwise make the node louder or quieter as a side effect.
        const float fill = outputCount(id) > 0 ? mapping().outputFraction(id)
                                               : 1.0f;
        bool touched = false;
        for (TargetID t : outputSel_) {
            if (all == !feeds(id, t)) continue;   // already as wanted
            if (all) map_.unbind(level_, id, t);
            else     map_.bind(level_, id, t);
            touched = true;
        }
        if (!touched) continue;
        if (outputCount(id) == 0) {
            // Its last output gone: null, with no silence binding left behind
            // -- a stray one behaves the same but pushes the outputs file to
            // version 2 for nothing.
            map_.clearBindings(level_, id);
            silenced.push_back(manifold_.node(id).name);
        } else if (fill < 1.0f) {
            map_.setOutputFraction(level_, id, fill);
        }
        ++n;
    }
    Result r = changed(std::string(all ? "unbound " : "bound ")
                       + std::to_string(n) + (all ? " from " : " to ")
                       + selectedOutputNames(), 2.0f);
    // An unbind that leaves a node with no output at all makes it silent,
    // which is easy to do without meaning to. Say so, by name when it is one
    // node, and flag it so an editor shows it as a warning.
    if (!silenced.empty()) {
        r.message += " -- "
                   + (silenced.size() == 1 ? silenced[0]
                                           : plural(silenced.size(), " node",
                                                    " nodes"))
                   + " now silent; c gives back its own output";
        r.refused = true;
        r.seconds = 3.5f;
    }
    return r;
}

Result Model::stepFill(const Selection& sel, int direction, bool fine) {
    if (sel.empty()) return Result{};
    const float unit = fine ? 0.01f : 0.05f;
    bool any = false, moved = false;
    float lo = 2.0f, hi = -1.0f;
    for (NodeID id : sel.nodes()) {
        if (outputCount(id) == 0) continue;
        any = true;
        const float before = mapping().outputFraction(id);

        // Position in steps. A value already on a step -- within float noise
        // -- moves a whole step; one between steps moves to the next step in
        // its direction. Without the snap, 70% read back as 69.99999% and a
        // step down would land on 65%... from 69.99999, skipping 70.
        float k = before / unit;
        const float kr = std::round(k);
        if (std::fabs(k - kr) < 1e-3f) k = kr;
        const float next = (k == kr) ? kr + float(direction)
                         : (direction > 0 ? std::ceil(k) : std::floor(k));
        const float f = std::min(1.0f, std::max(0.01f, next * unit));

        map_.setOutputFraction(level_, id, f);
        const float after = mapping().outputFraction(id);
        if (std::fabs(after - before) > 1e-6f) moved = true;
        lo = std::min(lo, after);
        hi = std::max(hi, after);
    }
    if (!any) {
        return refusal("no output to fill -- bind the node first", 2.5f);
    }
    auto pct = [](float v) { return std::to_string(int(std::lround(v * 100.0f))); };
    Result r;
    r.changed = moved;
    r.message = "fill " + pct(lo) + "%"
              + (pct(lo) == pct(hi) ? std::string() : " to " + pct(hi) + "%");
    r.seconds = 2.0f;
    return r;
}

// Binding ADDS weight, so binding a node already feeding one output to every
// output would feed that one double. This REPLACES the node's output bindings
// with one equal binding per output, then restores its fill -- "this node
// feeds everything, equally, as loudly as before".
Result Model::bindAll(const Selection& sel) {
    if (mapping().targetCount() == 0) {
        return refusal("no output yet -- press o to make one", 2.5f);
    }
    if (sel.empty()) return Result{};
    for (NodeID id : sel.nodes()) {
        const float fill = outputCount(id) > 0 ? mapping().outputFraction(id)
                                               : 1.0f;
        map_.clearBindings(level_, id);
        for (TargetID t = 0; t < mapping().targetCount(); ++t) {
            map_.bind(level_, id, t);
        }
        if (fill < 1.0f) map_.setOutputFraction(level_, id, fill);
    }
    return changed("bound " + plural(sel.size(), " node", " nodes")
                   + " to every output (" + std::to_string(mapping().targetCount())
                   + ")", 2.5f);
}

Result Model::silence(const Selection& sel) {
    for (NodeID id : sel.nodes()) map_.clearBindings(level_, id);
    if (sel.empty()) return Result{};
    const std::string who = sel.size() == 1
        ? manifold_.node(sel.nodes().front()).name
        : plural(sel.size(), " node", " nodes");
    return changed(who + " now silent -- c gives back its own output", 2.5f);
}

Result Model::clear(const Selection& sel) {
    if (sel.empty()) return Result{};
    std::size_t made = 0;
    std::vector<std::string> done, without;
    for (NodeID id : sel.nodes()) {
        const std::string nm = manifold_.node(id).name;
        // Its own output on this level: the one named for the level ...
        TargetID own = InvalidTarget;
        const std::string named = levelOutput(nm, level_);
        const TargetID t0 = mapping().findTarget(named);
        if (t0 != InvalidTarget && ownerOf(named) == id) own = t0;
        // ... else one named after it that it feeds on no other level.
        if (own == InvalidTarget) {
            for (TargetID t = 0; t < mapping().targetCount(); ++t) {
                const std::string on = mapping().targetName(t);
                if (ownerOf(on) != id) continue;
                bool elsewhere = false;
                for (LevelID k = 0; k < map_.count() && !elsewhere; ++k) {
                    if (k == level_) continue;
                    const Mapping& lv = map_.level(k);
                    for (std::size_t i = 0; i < lv.linkCount(id); ++i) {
                        const Link& l = lv.link(id, i);
                        if (l.kind == DestKind::Output
                            && lv.targetName(l.id) == on) {
                            elsewhere = true;
                            break;
                        }
                    }
                }
                if (!elsewhere) { own = t; break; }
            }
        }
        if (own == InvalidTarget) {
            if (!autoOutput_) { without.push_back(nm); continue; }
            std::string on = named;
            int j = 2;
            while (mapping().findTarget(on) != InvalidTarget) {
                on = named + "_" + std::to_string(j++);
            }
            own = map_.addOutput(on, nextFreeChannel());
            if (own == InvalidTarget) { without.push_back(nm); continue; }
            ++made;
        }
        map_.clearBindings(level_, id);
        map_.bind(level_, id, own);
        done.push_back(nm);
    }
    auto who = [](const std::vector<std::string>& v) {
        return v.size() == 1 ? v[0] : plural(v.size(), " node", " nodes");
    };
    if (done.empty()) {
        return refusal(who(without) + ": no output of its own -- p turns "
                       "auto-output on, or pick a fader and press b", 3.5f);
    }
    std::string msg = done.size() == 1
        ? done[0] + " back to its own output"
        : std::to_string(done.size()) + " nodes back to their own outputs";
    if (made) msg += ", " + plural(made, " output", " outputs") + " made";
    Result r = changed(msg, 2.5f);
    if (!without.empty()) {
        r.message += " -- " + who(without)
                   + " left: no output of its own, p turns auto-output on";
        r.refused = true;
        r.seconds = 3.5f;
    }
    return r;
}

Result Model::cycleOutput(int step) {
    const long n = long(mapping().targetCount());
    if (n == 0) return Result{};
    const long at = currentOutput_ == InvalidTarget
        ? (step > 0 ? -1 : 0) : long(currentOutput_);
    selectOnlyOutput(TargetID(((at + step) % n + n) % n));
    Result r;
    r.message = "current output: " + mapping().targetName(currentOutput_);
    r.seconds = 2.0f;
    return r;
}

Result Model::pickOutput(TargetID t) {
    if (t == InvalidTarget || t >= mapping().targetCount()) return Result{};
    selectOnlyOutput(t);
    Result r;
    r.message = "current output: " + mapping().targetName(t);
    r.seconds = 2.0f;
    return r;
}

Result Model::toggleOutput(TargetID t) {
    if (t == InvalidTarget || t >= mapping().targetCount()) return Result{};
    if (outputSelected(t)) {
        if (outputSel_.size() == 1) return Result{};   // keep at least one
        std::vector<TargetID> kept;
        for (TargetID s : outputSel_) if (s != t) kept.push_back(s);
        outputSel_ = kept;
        if (currentOutput_ == t) currentOutput_ = outputSel_.front();
    } else {
        outputSel_.push_back(t);
        currentOutput_ = t;
    }
    Result r;
    r.message = (outputSel_.size() == 1 ? "current output: " : "outputs: ")
              + selectedOutputNames();
    r.seconds = 2.0f;
    return r;
}

// Trim in dB, stored as a linear gain, capped at +12 dB.
Result Model::trimCurrent(float dB) {
    if (outputSel_.empty()) return Result{};
    // Every selected output, by the same number of dB.
    for (TargetID t : outputSel_) {
        const float g = mapping().targetTrim(t) * std::pow(10.0f, dB / 20.0f);
        map_.setTrim(t, std::min(g, 3.9810717f));
    }
    Result r;
    r.changed = true;
    return r;
}

// TargetIDs after a removed output shift down by one, so the current output
// has to follow -- the same rule removeNodes() taught for NodeIDs.
void Model::afterOutputRemoved(TargetID t) {
    // Selected outputs after t shift down by one; t itself leaves the set.
    std::vector<TargetID> kept;
    for (TargetID s : outputSel_) {
        if (s == t) continue;
        kept.push_back(s > t ? s - 1 : s);
    }
    outputSel_ = kept;
    if (currentOutput_ == t) {
        currentOutput_ = outputSel_.empty() ? InvalidTarget : outputSel_.front();
    } else if (currentOutput_ != InvalidTarget && currentOutput_ > t) {
        --currentOutput_;
    }
    settleCurrentOutput();
}

// Nodes that fed it and others renormalize onto the others; nodes that fed
// only it go silent. The message says how many.
Result Model::removeCurrentOutput() {
    if (currentOutput_ == InvalidTarget) return Result{};
    // Removing several speakers in one keypress is too easy to do by
    // accident, so it takes exactly one selected output.
    if (outputSel_.size() > 1) {
        return refusal("select one output to remove -- "
                       + std::to_string(outputSel_.size())
                       + " are selected", 2.5f);
    }
    // Each node that fed it on ANY level, counted once.
    std::set<NodeID> fed;
    for (LevelID lv = 0; lv < map_.count(); ++lv) {
        const Mapping& mp = map_.level(lv);
        for (NodeID n : mp.boundNodes()) {
            for (std::size_t k = 0; k < mp.linkCount(n); ++k) {
                const Link& l = mp.link(n, k);
                if (l.kind == DestKind::Output && l.id == currentOutput_) {
                    fed.insert(n);
                    break;
                }
            }
        }
    }
    const std::size_t feeders = fed.size();
    const std::string nm = mapping().targetName(currentOutput_);
    const TargetID t = currentOutput_;
    map_.removeOutput(t);
    afterOutputRemoved(t);
    Result r;
    r.changed = true;
    r.refused = feeders > 0;          // shown as a warning, as before
    r.message = "removed output " + nm
              + (feeders ? " -- " + plural(feeders, " node", " nodes")
                           + " no longer reach it"
                         : std::string());
    r.seconds = 3.5f;
    return r;
}

Result Model::toggleAutoOutput() {
    autoOutput_ = !autoOutput_;
    Result r;
    r.message = autoOutput_
        ? "auto-output on: each new node gets its own output"
        : "auto-output off: new nodes start unbound";
    r.seconds = 3.0f;
    return r;
}

// ---------------------------------------------------------------------------
// levels (round 3A-6)
// ---------------------------------------------------------------------------

Result Model::switchLevel(int direction) {
    Result r;
    r.seconds = 2.0f;
    const LevelID n = map_.count();
    if (n == 1) {
        r.message = "one level -- n makes another";
        return r;
    }
    if (direction < 0 && level_ == 0) {
        r.message = "already the bottom level";
        return r;
    }
    if (direction > 0 && level_ + 1 >= n) {
        r.message = "already the top level";
        return r;
    }
    level_ = direction < 0 ? level_ - 1 : level_ + 1;
    r.message = "level " + std::to_string(level_ + 1) + " of "
              + std::to_string(n) + ", at "
              + height(map_.levels().base(level_));
    return r;
}

Result Model::newLevel(bool above) {
    const Levels& lv = map_.levels();
    const LevelID n = lv.count();
    const float b = lv.base(level_);
    // Halfway to the neighbour, or a quarter of the map beyond the end: the
    // starting heights stay sums of halves and quarters, exact in float.
    float base;
    if (above) {
        base = (level_ + 1 == n) ? b + 0.25f : (b + lv.base(level_ + 1)) * 0.5f;
    } else {
        base = (level_ == 0) ? b - 0.25f : (lv.base(level_ - 1) + b) * 0.5f;
    }
    std::vector<NodeID> blocked;
    const LevelID at = map_.copyLevel(level_, base, manifold_.nodeCount(),
                                      &blocked);
    if (at >= map_.count()) {
        std::string who;
        for (std::size_t i = 0; i < blocked.size(); ++i) {
            if (blocked[i] >= manifold_.nodeCount()) continue;
            who += (who.empty() ? "" : ", ") + manifold_.node(blocked[i]).name;
        }
        return refusal(std::string("no room for a new level ")
                       + (above ? "above" : "below") + " -- "
                       + (who.empty() ? std::string("a level starts there")
                                      : who)
                       + " would reach the level next to it", 3.0f);
    }
    level_ = at;
    std::string msg = "new level " + std::to_string(at + 1) + " of "
                    + std::to_string(n + 1) + ", at " + height(base);

    // Its own outputs (3A-6d): with auto-output on, each node of the new
    // level is bound to an output of its own, named after it and the level,
    // on the next free channel. What it sent to outputs goes to that one and
    // its silence stays, so its fill carries over. With auto-output off the
    // level shares the outputs it was copied from.
    if (autoOutput_) {
        // The links between nodes are copied as a pattern (3A-9): a link to
        // node M's own output becomes a link to M's own output on the new
        // level, same weight -- A to B is A to B-2. What a node sent to any
        // other output goes to its own, and a node that sent to no output
        // sends to its own.
        std::size_t made = 0;
        std::vector<TargetID> own(manifold_.nodeCount(), InvalidTarget);
        for (NodeID id = 0; id < manifold_.nodeCount(); ++id) {
            const std::string stem = manifold_.node(id).name + "-"
                                   + std::to_string(at + 1);
            std::string on = stem;
            for (int j = 2; mapping().findTarget(on) != InvalidTarget; ++j) {
                on = stem + "_" + std::to_string(j);
            }
            own[id] = map_.addOutput(on, nextFreeChannel());
            if (own[id] != InvalidTarget) ++made;
        }
        for (NodeID id = 0; id < manifold_.nodeCount(); ++id) {
            if (own[id] == InvalidTarget) continue;
            std::vector<std::pair<TargetID, float>> sends;
            std::vector<float> silence;
            {
                const Mapping& lv = map_.level(at);
                for (std::size_t i = 0; i < lv.linkCount(id); ++i) {
                    const Link& l = lv.link(id, i);
                    if (l.kind != DestKind::Output) {
                        silence.push_back(l.weight);
                        continue;
                    }
                    const NodeID m = ownerOf(lv.targetName(l.id));
                    const TargetID to = (m != InvalidNode
                                         && own[m] != InvalidTarget)
                                      ? own[m] : own[id];
                    sends.emplace_back(to, l.weight);
                }
            }
            if (sends.empty()) sends.emplace_back(own[id], 1.0f);
            map_.clearBindings(at, id);
            // bind adds to a target already bound, so two links that land
            // on one output merge.
            for (const auto& sw : sends) map_.bind(at, id, sw.first, sw.second);
            for (float w : silence) map_.bindSilence(at, id, w);
        }
        msg += ", with " + plural(made, " output", " outputs") + " of its own";
    }
    return changed(msg, 2.5f);
}

Result Model::raise(const Selection& sel, int direction, bool fine) {
    if (sel.empty()) return Result{};
    const Levels& lv = map_.levels();
    const float unit = fine ? 0.01f : 0.05f;
    const float base = lv.base(level_);

    // Each node's offset from the level's start, in steps; a value already
    // on a step -- within float noise -- moves a whole step, one between
    // steps moves to the next in its direction. As stepFill.
    std::vector<std::pair<NodeID, float>> moves;
    for (NodeID id : sel.nodes()) {
        float k = (lv.height(level_, id) - base) / unit;
        const float kr = std::round(k);
        if (std::fabs(k - kr) < 1e-3f) k = kr;
        const float next = (k == kr) ? kr + float(direction)
                         : (direction > 0 ? std::ceil(k) : std::floor(k));
        moves.emplace_back(id, base + next * unit);
    }
    // All or nothing: strictly above the level below, strictly below the
    // level above, for every node, before any moves.
    std::string blocked;
    for (const auto& mv : moves) {
        const bool under = level_ + 1 >= lv.count()
                        || mv.second < lv.height(level_ + 1, mv.first);
        const bool over = level_ == 0
                       || mv.second > lv.height(level_ - 1, mv.first);
        if (!under || !over) {
            blocked += (blocked.empty() ? "" : ", ")
                     + manifold_.node(mv.first).name;
        }
    }
    if (!blocked.empty()) {
        return refusal(blocked + " would reach the level "
                       + (direction > 0 ? "above" : "below"), 2.5f);
    }
    float lo = 1e9f, hi = -1e9f;
    for (const auto& mv : moves) {
        map_.setHeight(level_, mv.first, mv.second);
        lo = std::min(lo, mv.second - base);
        hi = std::max(hi, mv.second - base);
    }
    const std::string a = offset(lo), b = offset(hi);
    return changed("height " + a + (a == b ? std::string() : " to " + b), 2.0f);
}

// ---------------------------------------------------------------------------
// node values (round 3A-7)
// ---------------------------------------------------------------------------

namespace {

// "name" or "name lo hi" -> true, or false with the reason.
bool parseValueLine(const std::string& line, std::string& name, float& lo,
                    float& hi, std::string& why) {
    std::istringstream in(line);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    const std::string form =
        "type a name, or a name and its range: distance 0 10";
    if (words.empty()) { why = "a value needs a name"; return false; }
    name = words[0];
    if (words.size() == 1) { lo = 0.0f; hi = 1.0f; return true; }
    if (words.size() != 3) { why = form; return false; }
    auto number = [](const std::string& w, float& x) {
        char* end = nullptr;
        x = std::strtof(w.c_str(), &end);
        return end && *end == '\0' && end != w.c_str() && std::isfinite(x);
    };
    if (!number(words[1], lo) || !number(words[2], hi)) { why = form; return false; }
    if (!(lo < hi)) { why = "the low end must be below the high end"; return false; }
    return true;
}

std::string describeValue(const ValueDef& d) {
    return d.name + ", " + height(d.lo) + " to " + height(d.hi);
}

} // namespace

Result Model::newValue(const std::string& line) {
    std::string name, why;
    float lo = 0.0f, hi = 1.0f;
    if (!parseValueLine(line, name, lo, hi, why)) return refusal(why, 3.0f);
    if (map_.findValue(name) != NoValue) {
        return refusal("a value named " + name + " already exists", 3.0f);
    }
    currentValue_ = map_.addValue(name, lo, lo, hi);
    return changed("value " + describeValue(map_.valueSet()[currentValue_]),
                   2.5f);
}

std::string Model::valueLine() const {
    if (currentValue_ == NoValue) return "";
    const ValueDef& d = map_.valueSet()[currentValue_];
    return d.name + " " + height(d.lo) + " " + height(d.hi);
}

Result Model::editValue(const std::string& line) {
    if (currentValue_ == NoValue) {
        return refusal("no values yet -- g makes one", 2.5f);
    }
    std::string name, why;
    float lo = 0.0f, hi = 1.0f;
    if (!parseValueLine(line, name, lo, hi, why)) return refusal(why, 3.0f);
    const ValueDef d = map_.valueSet()[currentValue_];
    const std::size_t other = map_.findValue(name);
    if (other != NoValue && other != currentValue_) {
        return refusal("a value named " + name + " already exists", 3.0f);
    }
    if (name == d.name && lo == d.lo && hi == d.hi) {
        Result r;
        r.message = "value " + describeValue(d) + ", unchanged";
        return r;
    }
    map_.renameValue(currentValue_, name);
    map_.setValueRange(currentValue_, lo, hi);
    return changed("value " + describeValue(map_.valueSet()[currentValue_]),
                   2.5f);
}

Result Model::pickValue(int direction) {
    Result r;
    r.seconds = 2.0f;
    const std::size_t n = map_.valueSet().size();
    if (n == 0) {
        r.message = "no values yet -- g makes one";
        return r;
    }
    currentValue_ = (currentValue_ + n + (direction < 0 ? n - 1 : 1)) % n;
    r.message = "current value: " + describeValue(map_.valueSet()[currentValue_]);
    return r;
}

Result Model::stepValue(const Selection& sel, int direction, bool fine) {
    if (currentValue_ == NoValue) {
        return refusal("no values yet -- g makes one", 2.5f);
    }
    if (sel.empty()) return Result{};
    const ValueDef d = map_.valueSet()[currentValue_];
    const float unit = (d.hi - d.lo) / (fine ? 100.0f : 20.0f);
    // The ends are exact: within a millionth of the range is the end itself,
    // so the vectors' double and this float agree on when the top is reached.
    const float eps = 1e-6f * (d.hi - d.lo);
    std::vector<std::pair<NodeID, float>> moves;
    float lo = 1e30f, hi = -1e30f;
    for (NodeID n : sel.nodes()) {
        const float now = map_.value(level_, n, currentValue_);
        float q = (now - d.lo) / unit;
        const float qr = std::round(q);
        if (std::fabs(q - qr) < 1e-3f) q = qr;
        const float next = (q == qr) ? qr + float(direction)
                         : (direction > 0 ? std::ceil(q) : std::floor(q));
        float x = d.lo + next * unit;
        x = (x >= d.hi - eps) ? d.hi : (x <= d.lo + eps) ? d.lo : x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
        if (std::fabs(x - now) > eps) moves.emplace_back(n, x);
    }
    if (moves.empty()) {
        Result r;
        r.message = std::string("already at the ")
                  + (direction > 0 ? "top" : "bottom") + " of " + d.name
                  + "'s range";
        return r;
    }
    for (const auto& mv : moves) map_.setValue(level_, mv.first, currentValue_, mv.second);
    const std::string a = height(lo), b = height(hi);
    return changed(d.name + " " + a + (a == b ? std::string() : " to " + b), 2.0f);
}

Result Model::removeValue() {
    if (currentValue_ == NoValue) {
        return refusal("no values yet -- g makes one", 2.5f);
    }
    const std::string name = map_.valueSet()[currentValue_].name;
    map_.removeValue(currentValue_);
    const std::size_t n = map_.valueSet().size();
    currentValue_ = n == 0 ? NoValue : std::min(currentValue_, n - 1);
    return changed("removed value " + name, 2.5f);
}

Result Model::goToLevel(LevelID k) {
    if (k >= map_.count()) return Result{};
    level_ = k;
    Result r;
    r.seconds = 2.0f;
    r.message = "level " + std::to_string(k + 1) + " of "
              + std::to_string(map_.count()) + ", at "
              + height(map_.levels().base(k));
    return r;
}

Result Model::moveLevel(LevelID k, float base) {
    if (k >= map_.count()) return Result{};
    const float dz = base - map_.levels().base(k);
    if (dz == 0.0f) return Result{};
    const std::string where = dz > 0.0f ? "above" : "below";
    std::vector<NodeID> blocked;
    if (!map_.moveLevel(k, dz, manifold_.nodeCount(), &blocked)) {
        std::string who;
        for (NodeID n : blocked) {
            if (n >= manifold_.nodeCount()) continue;
            who += (who.empty() ? "" : ", ") + manifold_.node(n).name;
        }
        return refusal("level " + std::to_string(k + 1) + " cannot start at "
                       + height(base) + " -- "
                       + (who.empty() ? "it would pass the level " + where
                                      : who + " would reach the level " + where),
                       3.0f);
    }
    level_ = k;
    return changed("level " + std::to_string(k + 1) + " now starts at "
                   + height(base), 2.0f);
}

Result Model::stepLevel(int direction, bool fine) {
    const float unit = fine ? 0.01f : 0.05f;
    float q = map_.levels().base(level_) / unit;
    const float qr = std::round(q);
    if (std::fabs(q - qr) < 1e-3f) q = qr;
    const float next = (q == qr) ? qr + float(direction)
                     : (direction > 0 ? std::ceil(q) : std::floor(q));
    return moveLevel(level_, next * unit);
}

Result Model::matchHeights(const Selection& sel) {
    if (sel.size() < 2) {
        return refusal("select the node to match first, then the others", 2.5f);
    }
    if (!sel.ordered()) {
        return refusal("a box has no first node -- click the one to match "
                       "first, then the others", 3.0f);
    }
    const Levels& lv = map_.levels();
    const NodeID first = sel.nodes().front();
    const float h0 = lv.height(level_, first);
    // Fits strictly between the same node on the levels either side.
    auto fits = [&](NodeID n, float h) {
        return (level_ == 0 || h > lv.height(level_ - 1, n))
            && (level_ + 1 >= lv.count() || h < lv.height(level_ + 1, n));
    };
    std::string blocked;
    std::vector<NodeID> moved;
    for (std::size_t i = 1; i < sel.nodes().size(); ++i) {
        const NodeID n = sel.nodes()[i];
        if (!fits(n, h0)) {
            blocked += (blocked.empty() ? "" : ", ") + manifold_.node(n).name;
        } else if (lv.height(level_, n) != h0) {
            moved.push_back(n);
        }
    }
    const std::string name = manifold_.node(first).name;
    if (!blocked.empty()) {
        return refusal(blocked + " cannot be at " + name + "'s height -- it "
                       "would reach a level next to it", 3.0f);
    }
    if (moved.empty()) {
        Result r;
        r.message = "already at the height of " + name;
        return r;
    }
    for (NodeID n : moved) map_.setHeight(level_, n, h0);
    return changed(plural(moved.size(), " node", " nodes") + " to the height of "
                   + name + " (" + offset(h0 - lv.base(level_)) + ")", 2.5f);
}

Result Model::flattenHeights(const Selection& sel) {
    if (sel.empty()) return Result{};
    const Levels& lv = map_.levels();
    const float base = lv.base(level_);
    std::string blocked;
    std::vector<NodeID> moved;
    for (NodeID n : sel.nodes()) {
        const bool fits = (level_ == 0 || base > lv.height(level_ - 1, n))
                       && (level_ + 1 >= lv.count()
                           || base < lv.height(level_ + 1, n));
        if (!fits) {
            blocked += (blocked.empty() ? "" : ", ") + manifold_.node(n).name;
        } else if (lv.height(level_, n) != base) {
            moved.push_back(n);
        }
    }
    if (!blocked.empty()) {
        return refusal(blocked + " cannot go back to the level's start -- it "
                       "would reach a level next to it", 3.0f);
    }
    if (moved.empty()) {
        Result r;
        r.message = "already at the level's start";
        return r;
    }
    for (NodeID n : moved) map_.setHeight(level_, n, base);
    return changed(plural(moved.size(), " node", " nodes")
                   + " back to the level's start", 2.5f);
}

Result Model::removeLevel() {
    const LevelID n = map_.count();
    if (n == 1) return refusal("a map has at least one level", 2.5f);
    const LevelID gone = level_;
    std::vector<std::string> used;          // the outputs that level fed
    {
        const Mapping& lv = map_.level(gone);
        for (NodeID id : lv.boundNodes()) {
            for (std::size_t i = 0; i < lv.linkCount(id); ++i) {
                const Link& l = lv.link(id, i);
                if (l.kind == DestKind::Output) used.push_back(lv.targetName(l.id));
            }
        }
    }
    map_.removeLevel(gone);
    level_ = gone > 0 ? gone - 1 : 0;

    // The outputs only that level used go with it (3A-6d), in output order.
    std::vector<std::string> doomed;
    for (TargetID t = 0; t < mapping().targetCount(); ++t) {
        const std::string& nm = mapping().targetName(t);
        if (std::find(used.begin(), used.end(), nm) != used.end()
            && !outputInUse(t)) {
            doomed.push_back(nm);
        }
    }
    for (const std::string& nm : doomed) {
        const TargetID t = mapping().findTarget(nm);
        map_.removeOutput(t);
        afterOutputRemoved(t);
    }
    return changed("removed level " + std::to_string(gone + 1)
                   + (doomed.empty() ? std::string()
                      : " and " + plural(doomed.size(), " output", " outputs"))
                   + "; now on level " + std::to_string(level_ + 1) + " of "
                   + std::to_string(n - 1), 3.0f);
}

namespace {

// The arc a link is drawn along: a quadratic curve from p to q, p the node
// with the lower id. Its control point sits kArcBend of the line's length out
// from the middle, on side +1 or -1; the curve's peak is half that.
constexpr float kArcBend = 0.25f;

glm::vec2 arcControl(glm::vec2 p, glm::vec2 q, int side) {
    const glm::vec2 d = q - p;
    return (p + q) * 0.5f + glm::vec2(-d.y, d.x) * (kArcBend * float(side));
}

glm::vec2 arcPoint(glm::vec2 p, glm::vec2 c, glm::vec2 q, float t) {
    const float u = 1.0f - t;
    return p * (u * u) + c * (2.0f * u * t) + q * (t * t);
}

float distanceToSegment(glm::vec2 x, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    float t = len2 > 0.0f ? glm::dot(x - a, ab) / len2 : 0.0f;
    t = std::min(1.0f, std::max(0.0f, t));
    return glm::length(x - (a + ab * t));
}

// Sampled as 16 straight pieces: plenty at the size arcs are drawn.
float distanceToArc(glm::vec2 x, glm::vec2 p, glm::vec2 c, glm::vec2 q) {
    float best = 1e9f;
    glm::vec2 prev = p;
    for (int k = 1; k <= 16; ++k) {
        const glm::vec2 cur = arcPoint(p, c, q, float(k) / 16.0f);
        best = std::min(best, distanceToSegment(x, prev, cur));
        prev = cur;
    }
    return best;
}

} // namespace

std::vector<LinkLine> linkLines(const Model& model, const Selection& sel) {
    const Manifold2D& m = model.manifold();
    const Mapping& mp = model.mapping();
    std::vector<LinkLine> lines;
    for (NodeID n = 0; n < m.nodeCount(); ++n) {
        float total = 0.0f;
        for (std::size_t k = 0; k < mp.linkCount(n); ++k) {
            total += mp.link(n, k).weight;
        }
        for (std::size_t k = 0; k < mp.linkCount(n); ++k) {
            const Link& l = mp.link(n, k);
            if (l.kind != DestKind::Output) continue;
            // To the node the output belongs to: on a level past the first,
            // B's output is B-2 (3A-9).
            const NodeID to = model.ownerOf(mp.targetName(l.id));
            if (to == InvalidNode || to == n) continue;   // no node, or itself
            const bool out = sel.contains(n), in = sel.contains(to);
            if (!out && !in) continue;
            LinkLine ln;
            ln.from = n;
            ln.to = to;
            ln.output = l.id;
            ln.share = total > 0.0f ? l.weight / total : 0.0f;
            ln.heavy = out;
            lines.push_back(ln);
        }
    }
    // Two nodes feeding each other: each line takes a side.
    for (LinkLine& a : lines) {
        for (const LinkLine& b : lines) {
            if (a.from == b.to && a.to == b.from) {
                a.lane = (a.from < a.to) ? +1 : -1;
            }
        }
    }
    for (LinkLine& l : lines) {
        if (l.lane != 0) { l.bend = l.lane; continue; }
        const NodeID lo = std::min(l.from, l.to), hi = std::max(l.from, l.to);
        const glm::vec2 p = m.node(lo).position, q = m.node(hi).position;
        float clearance[2] = {1e9f, 1e9f};
        for (int k = 0; k < 2; ++k) {
            const glm::vec2 c = arcControl(p, q, k == 0 ? +1 : -1);
            for (NodeID o = 0; o < m.nodeCount(); ++o) {
                if (o == l.from || o == l.to) continue;
                clearance[k] = std::min(clearance[k],
                    distanceToArc(m.node(o).position, p, c, q));
            }
        }
        l.bend = (clearance[1] > clearance[0] + 1e-4f) ? -1 : +1;
    }
    return lines;
}

glm::vec2 linkPoint(const Model& model, const LinkLine& line, float t) {
    const Manifold2D& m = model.manifold();
    const NodeID lo = std::min(line.from, line.to);
    const NodeID hi = std::max(line.from, line.to);
    const glm::vec2 p = m.node(lo).position, q = m.node(hi).position;
    const glm::vec2 c = arcControl(p, q, line.bend);
    return arcPoint(p, c, q, line.from < line.to ? t : 1.0f - t);
}

} // namespace editor
} // namespace ofxManifold
