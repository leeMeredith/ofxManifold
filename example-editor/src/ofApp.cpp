#include "ofApp.h"

using namespace ofxManifold;

void ofApp::setup() {
    ofSetWindowTitle("ofxManifold — editor");
    ofSetFrameRate(60);
    ofBackground(18, 18, 22);
    ofEnableAlphaBlending();
    // openFrameworks quits on escape by default. Here escape clears the
    // selection, which is what every editor uses it for.
    ofSetEscapeQuitsApp(false);
    // Reopen the last saved map if there is one, so work carries over between
    // sessions. Otherwise start from the example map.
    if (ofFile::doesFileExist("editor.json")) {
        buildStartingMap();
        load();
    } else {
        buildStartingMap();
    }
    rebuildGrid();
}

void ofApp::buildStartingMap() {
    manifold = Manifold2D();

    // A fan of four triangles...
    const auto O = manifold.addNode("O", {0.30f, 0.50f});
    const auto N = manifold.addNode("N", {0.30f, 0.80f});
    const auto E = manifold.addNode("E", {0.50f, 0.50f});
    const auto S = manifold.addNode("S", {0.30f, 0.20f});
    const auto W = manifold.addNode("W", {0.10f, 0.50f});
    manifold.addTriangle(O, N, E);
    manifold.addTriangle(O, E, S);
    manifold.addTriangle(O, S, W);
    manifold.addTriangle(O, W, N);

    // ...and a quad. Drag one of its corners across the opposite edge and the
    // move is refused: the ring would cross itself (D-019).
    const auto A = manifold.addNode("A", {0.65f, 0.35f});
    const auto B = manifold.addNode("B", {0.90f, 0.35f});
    const auto C = manifold.addNode("C", {0.90f, 0.65f});
    const auto D = manifold.addNode("D", {0.65f, 0.65f});
    manifold.addRegion({A, B, C, D});

    renderer = std::make_unique<ofxManifoldRenderer>(manifold);
    selected.clear();
    topology = manifold.validate();
}

void ofApp::rebuildGrid() {
    const float s = spacing;
    switch (kind) {
    case GridKind::Free:
        grid = Grid::free();
        break;
    case GridKind::Square:
        grid = Grid::lattice({s, 0.0f}, {0.0f, s}, {0.0f, 0.0f}, rotation);
        break;
    case GridKind::Triangular:
        grid = Grid::lattice({s, 0.0f}, {s * 0.5f, s * 0.8660254f},
                             {0.0f, 0.0f}, rotation);
        break;
    case GridKind::Polar:
        grid = Grid::polar({0.5f, 0.5f}, s, spokes, rotation);
        break;
    }
}

void ofApp::update() {
    const float side = ofGetHeight() - 48.0f;
    renderer->setViewport(ofRectangle(24, 24, side, side));
    if (refusedFlash > 0.0f) refusedFlash -= 1.0f / 60.0f;
    if (messageTime > 0.0f) messageTime -= 1.0f / 60.0f;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

bool ofApp::isSelected(NodeID id) const {
    return std::find(selected.begin(), selected.end(), id) != selected.end();
}

NodeID ofApp::nodeAt(glm::vec2 screen) const {
    const float grab = 10.0f;
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        const glm::vec2 p = renderer->toScreen(manifold.node(NodeID(i)).position);
        if (glm::distance(p, screen) <= grab) return NodeID(i);
    }
    return InvalidNode;
}

bool ofApp::occupied(glm::vec2 p, GridAddress addr) const {
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        const glm::vec2 q = manifold.node(NodeID(i)).position;
        if (addr.valid) {
            // A node sits on this address if snapping it lands there.
            if (grid.snap(q).address == addr
                && glm::distance(grid.point(addr), q) < 1e-4f) {
                return true;
            }
        } else if (glm::distance(p, q) < 0.01f) {
            // Free mode has no addresses; a distance tolerance stands in.
            return true;
        }
    }
    return false;
}

void ofApp::placeNode(glm::vec2 p) {
    const SnapResult s = grid.snap(p);
    if (s.point.x < 0.0f || s.point.x > 1.0f
        || s.point.y < 0.0f || s.point.y > 1.0f) {
        return;
    }
    if (occupied(s.point, s.address)) {
        refusedFlash = 0.35f;
        message = "a node is already there";
        messageTime = 1.5f;
        return;
    }
    const NodeID id = manifold.addNode(freshName(), s.point);
    if (id == InvalidNode) return;
    // The renderer holds a pointer to the manifold, so it sees the new node
    // without being rebuilt. A node on its own is an orphan -- in no region --
    // which validate() reports; making regions is the next editor step.
    selected = {id};
    selectionOrdered = true;
    topology = manifold.validate();
}

// ---------------------------------------------------------------------------
// mouse
// ---------------------------------------------------------------------------

void ofApp::mousePressed(int x, int y, int) {
    const glm::vec2 screen(x, y);
    const bool shift = ofGetKeyPressed(OF_KEY_SHIFT);
    const NodeID hit = nodeAt(screen);

    if (hit == InvalidNode) {
        // Empty space. Whether this is a click (place a node) or a drag (box
        // select) is not known until release.
        pressingEmpty = true;
        pressScreen = boxScreen = screen;
        if (!shift) { selected.clear(); selectionOrdered = true; }
        return;
    }

    if (shift) {
        // Toggle membership and stop: shift-click edits the selection, it
        // does not start a move.
        auto it = std::find(selected.begin(), selected.end(), hit);
        if (it != selected.end()) selected.erase(it);
        else selected.push_back(hit);
        return;
    }

    // Plain click on a node: select it, unless it is already part of a
    // selection -- then keep the selection, so the whole group can be dragged.
    if (!isSelected(hit)) { selected = {hit}; selectionOrdered = true; }

    dragging = true;
    anchor = hit;
    const glm::vec2 anchorPos = manifold.node(anchor).position;
    // Where on the node it was grabbed, so it does not jump to the cursor.
    grabOffset = anchorPos - renderer->toManifold(screen);
    dragFrom.clear();
    for (NodeID id : selected) dragFrom.push_back(manifold.node(id).position);
    anchorAddr = grid.snap(anchorPos).address;
}

void ofApp::mouseDragged(int x, int y, int) {
    const glm::vec2 screen(x, y);

    if (pressingEmpty) {
        boxScreen = screen;
        return;
    }
    if (!dragging || anchor == InvalidNode) return;

    const glm::vec2 target = renderer->toManifold(screen) + grabOffset;

    // Snap the ANCHOR, then move the whole selection by the anchor's offset.
    // Snapping each node independently would distort the selection's shape:
    // evenly spaced nodes could land unevenly (decision J).
    //
    // Alt forces free-hand for this drag without changing the grid.
    glm::vec2 snapped = target;
    if (!ofGetKeyPressed(OF_KEY_ALT)) {
        const SnapResult s = grid.snapHysteresis(target, anchorAddr, deadZone);
        snapped = s.point;
        anchorAddr = s.address;
    } else {
        // Free-hand for this stretch of the drag. Forget the grid point the
        // anchor was holding: otherwise releasing alt mid-drag would let
        // hysteresis pull the node back to where it snapped BEFORE the
        // free-hand part, rather than snapping from where it now is.
        anchorAddr = GridAddress{};
    }

    // Where the anchor started, from dragFrom.
    std::size_t ai = 0;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (selected[i] == anchor) { ai = i; break; }
    }
    const glm::vec2 delta = snapped - dragFrom[ai];

    std::vector<std::pair<NodeID, glm::vec2>> moves;
    moves.reserve(selected.size());
    for (std::size_t i = 0; i < selected.size(); ++i) {
        moves.emplace_back(selected[i], dragFrom[i] + delta);
    }

    // One operation, all or nothing, checked against FINAL shapes. On refusal
    // nothing changes, so the selection stays at its last valid position.
    if (!manifold.setNodePositions(moves)) {
        refusedFlash = 0.35f;
        message = "move refused: a region would invert, flatten or cross itself";
        messageTime = 1.5f;
    }
}

void ofApp::mouseReleased(int x, int y, int) {
    const glm::vec2 screen(x, y);

    if (pressingEmpty) {
        pressingEmpty = false;
        // Barely moved: it was a click, so place a node.
        if (glm::distance(screen, pressScreen) < 4.0f) {
            placeNode(renderer->toManifold(screen));
            return;
        }
        // Otherwise a box: select every node inside it.
        const float x0 = std::min(pressScreen.x, screen.x);
        const float x1 = std::max(pressScreen.x, screen.x);
        const float y0 = std::min(pressScreen.y, screen.y);
        const float y1 = std::max(pressScreen.y, screen.y);
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            const NodeID id = NodeID(i);
            const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1
                && !isSelected(id)) {
                selected.push_back(id);
                selectionOrdered = false;   // a box has no order
            }
        }
        return;
    }

    dragging = false;
    anchor = InvalidNode;
}

// ---------------------------------------------------------------------------
// keys
// ---------------------------------------------------------------------------

void ofApp::keyPressed(int key) {
    bool regrid = true;
    if      (key == '1') kind = GridKind::Free;
    else if (key == '2') kind = GridKind::Square;
    else if (key == '3') kind = GridKind::Triangular;
    else if (key == '4') kind = GridKind::Polar;
    else if (key == 'r') rotation += ofDegToRad(5.0f);
    else if (key == 'R') rotation -= ofDegToRad(5.0f);
    else if (key == '0') rotation = 0.0f;
    else if (key == '-') spacing = std::max(0.02f, spacing * 0.8f);
    else if (key == '=' || key == '+') spacing = std::min(0.25f, spacing * 1.25f);
    else if (key == '[') spokes = std::max(3, spokes - 1);
    else if (key == ']') spokes = std::min(24, spokes + 1);
    else regrid = false;
    if (regrid) rebuildGrid();

    // Dead zone for drag hysteresis, as a fraction of grid spacing. An open
    // question in the plan, settled by feel rather than on paper.
    if (key == ',') deadZone = std::max(0.0f, deadZone - 0.05f);
    if (key == '.') deadZone = std::min(0.50f, deadZone + 0.05f);

    if (key == 'a') {
        selected.clear();
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            selected.push_back(NodeID(i));
        }
    }
    if (key == OF_KEY_ESC) { selected.clear(); selectionOrdered = true; }
    if (key == 'x') newEmptyMap();
    if (key == 'e') buildStartingMap();
    if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) removeSelected();
    if (key == 'u') unjoinSelected();
    if (key == 'f') joinSelection();
    if (key == 's') save();
    if (key == 'l') load();
}

// ---------------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------------

// A segment in manifold space, clipped to the unit square before drawing, so
// grid lines stop at the map's edge instead of running into the panel.
// Liang-Barsky: clip the parameter range against each of the four edges.
void ofApp::drawSegment(glm::vec2 a, glm::vec2 b) const {
    float t0 = 0.0f, t1 = 1.0f;
    const glm::vec2 d = b - a;
    const float p[4] = {-d.x, d.x, -d.y, d.y};
    const float q[4] = {a.x, 1.0f - a.x, a.y, 1.0f - a.y};
    for (int k = 0; k < 4; ++k) {
        if (std::fabs(p[k]) < 1e-12f) {
            if (q[k] < 0.0f) return;           // parallel and outside
            continue;
        }
        const float t = q[k] / p[k];
        if (p[k] < 0.0f) t0 = std::max(t0, t);
        else             t1 = std::min(t1, t);
        if (t0 > t1) return;
    }
    const glm::vec2 s0 = renderer->toScreen(a + d * t0);
    const glm::vec2 s1 = renderer->toScreen(a + d * t1);
    ofDrawLine(s0.x, s0.y, s1.x, s1.y);
}

void ofApp::drawGrid() const {
    if (kind == GridKind::Free) return;
    ofPushStyle();
    ofSetLineWidth(1.0f);

    // Faint lines first, so the points sit on top of them.
    ofSetColor(70, 76, 92, 70);
    if (kind == GridKind::Polar) {
        const glm::vec2 c = grid.point({0, 0, true});
        const int rings = int(0.75f / spacing) + 1;
        // Rings, as polylines through many points, each piece clipped.
        const int steps = 96;
        for (int ring = 1; ring <= rings; ++ring) {
            const float r = ring * spacing;
            for (int k = 0; k < steps; ++k) {
                const float a0 = rotation + k * TWO_PI / steps;
                const float a1 = rotation + (k + 1) * TWO_PI / steps;
                drawSegment({c.x + r * std::cos(a0), c.y + r * std::sin(a0)},
                            {c.x + r * std::cos(a1), c.y + r * std::sin(a1)});
            }
        }
        // Spokes, from the centre out past the last ring.
        for (int k = 0; k < spokes; ++k) {
            drawSegment(c, grid.point({rings, k, true}));
        }
    } else {
        // Lattice lines: one family along each basis direction, and for a
        // triangular grid a third along their difference, which is what makes
        // the triangles visible rather than a skewed square mesh.
        const int n = int(1.6f / spacing) + 2;
        for (int i = -n; i <= n; ++i) {
            drawSegment(grid.point({i, -n, true}), grid.point({i, n, true}));
            drawSegment(grid.point({-n, i, true}), grid.point({n, i, true}));
            if (kind == GridKind::Triangular) {
                // Points (c - t, t): the direction v - u.
                drawSegment(grid.point({i + n, -n, true}),
                            grid.point({i - n,  n, true}));
            }
        }
    }

    // Points.
    ofFill();
    ofSetColor(90, 96, 112, 140);
    auto dot = [&](glm::vec2 p) {
        if (p.x < -0.001f || p.x > 1.001f || p.y < -0.001f || p.y > 1.001f) {
            return;
        }
        const glm::vec2 s = renderer->toScreen(p);
        ofDrawCircle(s.x, s.y, 1.6f);
    };
    if (kind == GridKind::Polar) {
        dot(grid.point({0, 0, true}));
        const int rings = int(0.75f / spacing) + 1;
        for (int ring = 1; ring <= rings; ++ring) {
            for (int k = 0; k < spokes; ++k) dot(grid.point({ring, k, true}));
        }
    } else {
        const int n = int(1.6f / spacing) + 2;
        for (int i = -n; i <= n; ++i) {
            for (int j = -n; j <= n; ++j) dot(grid.point({i, j, true}));
        }
    }
    ofPopStyle();
}

// ---------------------------------------------------------------------------
// joining, saving
// ---------------------------------------------------------------------------

// Join the selected nodes into one region.
//
// ORDER decides the shape, so it matters which order is used:
//
//   chosen by individual clicks  -> the order clicked. The author's order.
//   any part chosen by a box     -> angle about the centroid.
//
// Angle order recovers every convex shape and every star, but NOT an L: for
// an L it makes a different ring that is still a legal region, so it would be
// accepted without complaint. Measured 0 of 200. Shapes like that are made by
// shift-clicking the corners around in order.
//
// A click-ordered ring that crosses itself is REFUSED with the reason. It is
// not quietly re-sorted into angle order, because that would be the same
// silent substitution in the other direction.
void ofApp::joinSelection() {
    if (selected.size() < 3) {
        refusedFlash = 0.35f;
        message = "select at least three nodes to join";
        messageTime = 2.0f;
        return;
    }

    std::vector<NodeID> ring = selected;
    if (!selectionOrdered) {
        std::vector<glm::vec2> pts;
        for (NodeID id : selected) pts.push_back(manifold.node(id).position);
        ring.clear();
        for (std::size_t i : orderByAngle(pts)) ring.push_back(selected[i]);
    }

    // The same set of nodes as a region that already exists is a duplicate
    // (validate() reports those); refuse it here rather than create one.
    std::vector<NodeID> key = ring;
    std::sort(key.begin(), key.end());
    for (std::size_t r = 0; r < manifold.regionCount(); ++r) {
        std::vector<NodeID> have = manifold.region(RegionID(r)).ids();
        std::sort(have.begin(), have.end());
        if (have == key) {
            refusedFlash = 0.35f;
            message = "those nodes already form a region";
            messageTime = 2.0f;
            return;
        }
    }

    if (manifold.addRegion(ring) == InvalidRegion) {
        refusedFlash = 0.35f;
        message = "refused: " + manifold.lastRegionError()
                + (selectionOrdered ? " (in click order)" : "");
        messageTime = 3.0f;
        return;
    }
    topology = manifold.validate();
    message = std::string("joined ") + ofToString(ring.size()) + " nodes in "
            + (selectionOrdered ? "click order" : "angle order");
    messageTime = 2.0f;
}

// A name no node has.
//
// The obvious "n" plus the node count collides as soon as a node is deleted:
// delete n3 from five nodes, add one, and it is named n4 -- which exists. The
// kernel now refuses duplicate names (D-020), but only because a duplicate
// made a map that saved and would not reopen.
std::string ofApp::freshName() const {
    for (std::size_t k = manifold.nodeCount(); ; ++k) {
        const std::string nm = "n" + ofToString(k);
        if (manifold.findNode(nm) == InvalidNode) return nm;
    }
}

void ofApp::adopt() {
    renderer = std::make_unique<ofxManifoldRenderer>(manifold);
    selected.clear();
    selectionOrdered = true;
    dragging = false;
    anchor = InvalidNode;
    topology = manifold.validate();
}

void ofApp::newEmptyMap() {
    manifold = Manifold2D();
    adopt();
    message = "new empty map -- click to place nodes";
    messageTime = 3.0f;
}

// Remove the selected nodes, and every region that uses any of them.
//
// NodeIDs are renumbered by removal, so the selection -- which holds NodeIDs --
// is cleared rather than left pointing at different nodes.
void ofApp::removeSelected() {
    if (selected.empty()) return;
    const std::size_t n = selected.size();
    std::size_t regionsGone = 0;
    manifold.removeNodes(selected, nullptr, &regionsGone);
    // The renderer holds a pointer to the manifold, which is the same object,
    // so it does not need rebuilding; the selection and report do.
    selected.clear();
    selectionOrdered = true;
    topology = manifold.validate();
    message = "removed " + ofToString(n) + (n == 1 ? " node" : " nodes")
            + (regionsGone ? ", and " + ofToString(regionsGone)
                             + (regionsGone == 1 ? " region" : " regions")
                             + " that used them"
                           : std::string());
    messageTime = 3.0f;
}

// Remove every region whose nodes are ALL selected, keeping the nodes. The
// counterpart of joining: unjoin, then join differently.
void ofApp::unjoinSelected() {
    std::vector<RegionID> doomed;
    for (std::size_t r = 0; r < manifold.regionCount(); ++r) {
        bool all = true;
        for (NodeID id : manifold.region(RegionID(r)).ids()) {
            if (!isSelected(id)) { all = false; break; }
        }
        if (all) doomed.push_back(RegionID(r));
    }
    if (doomed.empty()) {
        refusedFlash = 0.35f;
        message = "no region is made only of selected nodes";
        messageTime = 2.5f;
        return;
    }
    manifold.removeRegions(doomed);
    topology = manifold.validate();
    message = "unjoined " + ofToString(doomed.size())
            + (doomed.size() == 1 ? " region" : " regions");
    messageTime = 2.5f;
}

void ofApp::save() {
    ofBuffer buf;
    buf.set(io::saveManifold(manifold));
    ofBufferToFile("editor.json", buf);
    message = "saved " + ofToDataPath("editor.json");
    messageTime = 3.0f;
}

void ofApp::load() {
    const ofBuffer buf = ofBufferFromFile("editor.json");
    Manifold2D loaded;
    const io::LoadResult r = io::loadManifold(buf.getText(), loaded);
    if (!r.ok) {
        refusedFlash = 0.35f;
        message = "load failed: " + r.error;
        messageTime = 3.0f;
        return;
    }
    manifold = std::move(loaded);
    adopt();
    message = "loaded " + ofToString(manifold.nodeCount()) + " nodes, "
            + ofToString(manifold.regionCount()) + " regions";
    messageTime = 3.0f;
}

void ofApp::draw() {
    drawGrid();
    renderer->draw(InvalidRegion);

    // Selection rings, drawn over the nodes.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.0f);
    for (NodeID id : selected) {
        const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
        ofSetColor(refusedFlash > 0.0f ? ofColor(232, 96, 88)
                                       : ofColor(255, 214, 90));
        ofDrawCircle(p.x, p.y, 11.0f);
    }
    // The box being dragged out.
    if (pressingEmpty && glm::distance(boxScreen, pressScreen) >= 4.0f) {
        ofSetColor(255, 214, 90, 160);
        ofSetLineWidth(1.0f);
        ofDrawRectangle(std::min(pressScreen.x, boxScreen.x),
                        std::min(pressScreen.y, boxScreen.y),
                        std::fabs(boxScreen.x - pressScreen.x),
                        std::fabs(boxScreen.y - pressScreen.y));
    }
    ofPopStyle();

    // ---- panel ------------------------------------------------------------
    const float x = renderer->viewport().getRight() + 40.0f;
    float y = 48.0f;
    static const char* names[] = {"free", "square", "triangular", "polar"};

    ofSetColor(235, 238, 245);
    ofDrawBitmapString(std::string("grid       ") + names[int(kind)], x, y);
    y += 16.0f;
    ofSetColor(180, 186, 200);
    if (kind != GridKind::Free) {
        ofDrawBitmapString("spacing    " + ofToString(spacing, 3), x, y);
        y += 16.0f;
        ofDrawBitmapString("rotation   "
                           + ofToString(ofRadToDeg(rotation), 0) + " deg", x, y);
        y += 16.0f;
        if (kind == GridKind::Polar) {
            ofDrawBitmapString("spokes     " + ofToString(spokes), x, y);
            y += 16.0f;
        }
        ofDrawBitmapString("dead zone  " + ofToString(deadZone, 2)
                           + " x spacing", x, y);
        y += 16.0f;
    }
    y += 12.0f;
    ofDrawBitmapString("nodes      " + ofToString(manifold.nodeCount()), x, y);
    y += 16.0f;
    ofDrawBitmapString("regions    " + ofToString(manifold.regionCount()), x, y);
    y += 16.0f;
    ofSetColor(selected.empty() ? ofColor(140, 146, 160)
                                : ofColor(255, 214, 90));
    ofDrawBitmapString("selected   " + ofToString(selected.size()), x, y);
    y += 16.0f;
    if (!topology.orphans.empty()) {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("orphans    " + ofToString(topology.orphans.size())
                           + "  (in no region yet)", x, y);
        y += 16.0f;
    }
    y += 12.0f;

    if (messageTime > 0.0f) {
        ofSetColor(refusedFlash > 0.0f ? ofColor(232, 96, 88)
                                       : ofColor(120, 200, 160));
        ofDrawBitmapString(message, x, y);
    }
    y += 28.0f;

    ofSetColor(140, 146, 160);
    const char* help[] = {
        "click empty     place a node",
        "drag empty      box select",
        "shift           add to selection",
        "drag selected   move as one",
        "alt + drag      free-hand this drag",
        "",
        "1 2 3 4         free square tri polar",
        "r R 0           rotate grid, reset",
        "- =             grid spacing",
        "[ ]             polar spokes",
        ", .             hysteresis dead zone",
        "a  esc          select all, none",
        "f               join selection into a region",
        "s  l            save, load  (bin/data/editor.json)",
        "delete          remove selected nodes",
        "u               unjoin: remove regions made",
        "                only of selected nodes",
        "x               new empty map",
        "e               the example map",
    };
    for (const char* h : help) {
        ofDrawBitmapString(h, x, y);
        y += 15.0f;
    }
    y += 14.0f;
    ofSetColor(150, 190, 220);
    ofDrawBitmapString("Drag a corner of the quad across", x, y);  y += 15.0f;
    ofDrawBitmapString("the opposite edge. The ring would", x, y); y += 15.0f;
    ofDrawBitmapString("cross itself, so the move is refused.", x, y);
}
