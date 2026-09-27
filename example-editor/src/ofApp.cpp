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
    // Reopen the last saved map, and its outputs, if there is one, so work
    // carries over between sessions. Otherwise start from the example.
    buildStartingMap();
    if (ofFile::doesFileExist("editor.json")) load();
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
    mapping = Mapping();
    const TargetID front = mapping.addOutput("front", 0);
    const TargetID left  = mapping.addOutput("left", 1);
    const TargetID right = mapping.addOutput("right", 2);
    const TargetID rear  = mapping.addOutput("rear", 3);
    mapping.bind(N, front);
    mapping.bind(W, left);
    mapping.bind(E, right);
    mapping.bind(S, rear);
    for (TargetID t : {front, left, right, rear}) mapping.bind(O, t);
    mapping.bind(B, right);
    mapping.bindSilence(B, 1.0f);
    mapping.bind(C, left);
    mapping.bind(C, right);
    mapping.bind(D, rear);
    mapping.bindSilence(D, 1.0f / 3.0f);
    mapping.addDerived("sub", 4, 1.0f, SumMode::Linear, {{N, 1.0f}, {O, 1.0f}});
    currentOutput = front;
    (void)A;

    adopt();
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
    // The map keeps a square; the band under it holds the output chart.
    const float chartH = 130.0f;
    const float side = std::max(160.0f, ofGetHeight() - 48.0f - chartH);
    renderer->setViewport(ofRectangle(24, 24, side, side));
    chartTop = 24.0f + side + 16.0f;

    if (auditioning()) {
        // The hint is forgotten every frame: an edit made between frames can
        // renumber regions (unjoin, delete), and a stale hint would name a
        // region that no longer means what it did. At editor sizes the scan
        // costs nothing (D-015).
        evaluator->reset();
        evaluation = evaluator->evaluate(auditionPoint);
    }
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

    // The output takes the NODE'S NAME. That is what pairs them, and it
    // survives save and reload with no extra bookkeeping -- which is how
    // deleting the node later finds its own fader.
    if (autoOutput && mapping.findTarget(manifold.node(id).name)
                          == InvalidTarget) {
        const TargetID t = mapping.addOutput(manifold.node(id).name,
                                             nextFreeChannel());
        if (t != InvalidTarget) {
            mapping.bind(id, t);
            currentOutput = t;
        }
    }
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

    // A click on the chart picks an output. It never edits the map.
    pressInChart = (y >= chartTop);
    if (pressInChart) {
        for (const BarHit& b : barHits) {
            if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) {
                currentOutput = b.id;
                message = "current output: " + mapping.targetName(b.id);
                messageTime = 2.0f;
            }
        }
        return;
    }

    // Audition: the press moves the listening point and can never move a
    // node. The mode is fixed for the whole press, so letting go of space
    // mid-drag does not turn a listening drag into an editing one.
    pressAudition = auditioning();
    if (pressAudition) {
        auditionPoint = renderer->toManifold(screen);
        return;
    }

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
    if (pressInChart) return;
    if (pressAudition) {
        auditionPoint = renderer->toManifold(screen);
        return;
    }

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
    if (pressInChart) { pressInChart = false; return; }
    if (pressAudition) { pressAudition = false; return; }

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

    // outputs
    if (key == 'o') newOutput();
    if (key == 'd') newDerived();
    if (key == 'b') bindSelection();
    if (key == 'q') silenceSelection();
    if (key == 'c') clearSelection();
    if (key == OF_KEY_TAB) cycleOutput();
    if (key == 't') trimCurrent(-1.0f);
    if (key == 'T') trimCurrent(+1.0f);
    if (key == 'O') removeCurrentOutput();
    if (key == 'p') {
        autoOutput = !autoOutput;
        message = autoOutput ? "auto-output on: each new node gets its own output"
                             : "auto-output off: new nodes start unbound";
        messageTime = 3.0f;
    }

    // audition
    if (key == ' ') auditionHeld = true;
    if (key == 'v') {
        auditionLocked = !auditionLocked;
        message = auditionLocked ? "audition locked on -- v to return to editing"
                                 : "editing";
        messageTime = 2.5f;
    }
}

void ofApp::keyReleased(int key) {
    if (key == ' ') auditionHeld = false;
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
    // Free as a NODE name and as an OUTPUT name. An output can outlive the
    // node it was made for, when other nodes still feed it; a new node given
    // that name would find its auto-output already taken and be left unbound
    // without a word.
    for (std::size_t k = manifold.nodeCount(); ; ++k) {
        const std::string nm = "n" + ofToString(k);
        if (manifold.findNode(nm) == InvalidNode
            && mapping.findTarget(nm) == InvalidTarget) {
            return nm;
        }
    }
}

void ofApp::adopt() {
    renderer = std::make_unique<ofxManifoldRenderer>(manifold);
    // Nodes are drawn here, not by the renderer: their shape and fill come
    // from the MAPPING, which the renderer deliberately knows nothing about.
    renderer->style.drawNodes = false;
    evaluator = std::make_unique<Evaluator>(manifold);
    evaluation = Evaluation{};
    selected.clear();
    selectionOrdered = true;
    dragging = false;
    anchor = InvalidNode;
    if (currentOutput != InvalidTarget
        && currentOutput >= mapping.targetCount()) {
        currentOutput = InvalidTarget;
    }
    if (currentOutput == InvalidTarget && mapping.targetCount() > 0) {
        currentOutput = 0;
    }
    topology = manifold.validate();
}

void ofApp::newEmptyMap() {
    manifold = Manifold2D();
    mapping = Mapping();
    currentOutput = InvalidTarget;
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
    // NodeIDs after the first removed one are renumbered. The mapping holds
    // NodeIDs, so it MUST follow, or every binding after the deleted node
    // would silently move to the wrong node (decision E, D-020).
    std::vector<std::string> names;
    for (NodeID id : selected) names.push_back(manifold.node(id).name);
    std::vector<NodeID> remap;
    manifold.removeNodes(selected, &remap, &regionsGone);
    mapping.remapNodes(remap);

    // A node's own output -- the one named after it -- goes with it, but ONLY
    // if nothing else is bound to it. Otherwise deleting a speaker's node
    // would silently take away a fader other nodes still feed. Outputs made
    // by hand never share a node's name, so they are never removed this way.
    std::size_t outsGone = 0;
    for (const std::string& nm : names) {
        const TargetID t = mapping.findTarget(nm);
        if (t != InvalidTarget && !outputInUse(t)) {
            mapping.removeOutput(t);
            afterOutputRemoved(t);
            ++outsGone;
        }
    }
    // The renderer holds a pointer to the manifold, which is the same object,
    // so it does not need rebuilding; the selection and report do.
    selected.clear();
    selectionOrdered = true;
    topology = manifold.validate();
    message = "removed " + ofToString(n) + (n == 1 ? " node" : " nodes")
            + (regionsGone ? ", and " + ofToString(regionsGone)
                             + (regionsGone == 1 ? " region" : " regions")
                             + " that used them"
                           : std::string())
            + (outsGone ? ", and " + ofToString(outsGone)
                          + (outsGone == 1 ? " output" : " outputs")
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
    ofBuffer a, b;
    a.set(io::saveManifold(manifold));
    b.set(io::saveMapping(mapping, manifold));
    ofBufferToFile("editor.json", a);
    ofBufferToFile("editor-mapping.json", b);
    message = "saved editor.json and editor-mapping.json";
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

    // The mapping refers to nodes by NAME, so it is loaded against the map
    // just read. A missing file is fine -- a map with no outputs yet.
    Mapping mp;
    std::string note;
    if (ofFile::doesFileExist("editor-mapping.json")) {
        const ofBuffer mb = ofBufferFromFile("editor-mapping.json");
        const io::LoadResult mr = io::loadMapping(mb.getText(), loaded, mp);
        if (!mr.ok) {
            mp = Mapping();
            note = " -- outputs file refused: " + mr.error;
        }
    }
    manifold = std::move(loaded);
    mapping = std::move(mp);
    currentOutput = InvalidTarget;
    adopt();
    message = "loaded " + ofToString(manifold.nodeCount()) + " nodes, "
            + ofToString(manifold.regionCount()) + " regions, "
            + ofToString(mapping.targetCount()) + " outputs" + note;
    messageTime = note.empty() ? 3.0f : 6.0f;
    if (!note.empty()) refusedFlash = 0.35f;
}

void ofApp::draw() {
    drawGrid();
    renderer->draw(auditioning() && evaluation.inside ? evaluation.regionID
                                                      : InvalidRegion);

    // Halos first, so they sit behind the nodes.
    if (auditioning()) {
        for (const auto& wn : evaluation.weights) drawHalo(wn.id, wn.weight);
    }
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        drawNode(NodeID(i));
    }

    // Selection rings, over the nodes.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.0f);
    for (NodeID id : selected) {
        const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
        ofSetColor(refusedFlash > 0.0f ? ofColor(232, 96, 88)
                                       : ofColor(255, 214, 90));
        ofDrawCircle(p.x, p.y, 13.0f);
    }
    if (pressingEmpty && glm::distance(boxScreen, pressScreen) >= 4.0f) {
        ofSetColor(255, 214, 90, 160);
        ofSetLineWidth(1.0f);
        ofDrawRectangle(std::min(pressScreen.x, boxScreen.x),
                        std::min(pressScreen.y, boxScreen.y),
                        std::fabs(boxScreen.x - pressScreen.x),
                        std::fabs(boxScreen.y - pressScreen.y));
    }
    ofPopStyle();

    if (auditioning()) renderer->drawEvaluation(evaluation, auditionPoint);

    drawChart();
    drawPanel();
}

// ---------------------------------------------------------------------------
// nodes
// ---------------------------------------------------------------------------

std::size_t ofApp::outputCount(NodeID id) const {
    std::size_t n = 0;
    for (std::size_t k = 0; k < mapping.linkCount(id); ++k) {
        if (mapping.link(id, k).kind == DestKind::Output) ++n;
    }
    return n;
}

// Keep the part of a polygon at or below screen height `cut` (screen y grows
// downward, so "below" is a larger y). One edge of Sutherland-Hodgman.
static std::vector<glm::vec2> keepBelow(const std::vector<glm::vec2>& poly,
                                        float cut) {
    std::vector<glm::vec2> out;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = poly[i];
        const glm::vec2 b = poly[(i + 1) % n];
        const bool ina = a.y >= cut, inb = b.y >= cut;
        if (ina) out.push_back(a);
        if (ina != inb) {
            const float t = (cut - a.y) / (b.y - a.y);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

// One property per visual channel (decision H):
//
//   shape  what the node does with its share
//            no output      hollow square
//            one output     circle
//            several        triangle
//   fill   how much of that share reaches an output, from the bottom up
//   size   the node's bias weight, as the renderer draws it
//
// All of it computed from the mapping every frame, never stored: a stored
// type that could disagree with the bindings would be the D-018/019/020
// pattern again. Shape carries the meaning, not colour, so it still reads on
// a projector or a monochrome printout.
void ofApp::drawNode(NodeID id) const {
    const Node& n = manifold.node(id);
    const glm::vec2 p = renderer->toScreen(n.position);
    const float r = nodeRadius(id);
    const std::size_t outs = outputCount(id);
    const float frac = mapping.outputFraction(id);

    std::vector<glm::vec2> shape;
    if (outs == 0) {
        const float h = r * 0.95f;
        shape = {{p.x - h, p.y - h}, {p.x + h, p.y - h},
                 {p.x + h, p.y + h}, {p.x - h, p.y + h}};
    } else if (outs == 1) {
        for (int k = 0; k < 24; ++k) {
            const float a = k * TWO_PI / 24.0f;
            shape.push_back({p.x + r * std::cos(a), p.y + r * std::sin(a)});
        }
    } else {
        // Pointing up, as if distributing outward.
        const float R = r * 1.3f;
        for (int k = 0; k < 3; ++k) {
            const float a = -HALF_PI + k * TWO_PI / 3.0f;
            shape.push_back({p.x + R * std::cos(a), p.y + R * std::sin(a)});
        }
    }

    ofPushStyle();
    const ofColor ink(215, 220, 232);
    if (outs > 0 && frac > 0.0f) {
        float top = shape[0].y, bottom = shape[0].y;
        for (const glm::vec2& v : shape) {
            top = std::min(top, v.y);
            bottom = std::max(bottom, v.y);
        }
        const std::vector<glm::vec2> part =
            keepBelow(shape, bottom - frac * (bottom - top));
        ofFill();
        ofSetColor(ink);
        ofBeginShape();
        for (const glm::vec2& v : part) ofVertex(v.x, v.y);
        ofEndShape(true);
    }
    ofNoFill();
    ofSetLineWidth(1.5f);
    ofSetColor(ink);
    for (std::size_t k = 0; k < shape.size(); ++k) {
        const glm::vec2 a = shape[k], b = shape[(k + 1) % shape.size()];
        ofDrawLine(a.x, a.y, b.x, b.y);
    }
    ofSetColor(180, 186, 200);
    ofDrawBitmapString(n.name, p.x + r + 6.0f, p.y - 6.0f);
    ofPopStyle();
}

// ---------------------------------------------------------------------------
// the chart along the bottom (decision J)
// ---------------------------------------------------------------------------

// One bar per OUTPUT, not per node, in channel order -- outputs are what is
// heard. Derived outputs drawn in their own colour. A silence bar at the end,
// so outputs plus silence always total exactly 1. Trim as a tick: the bar is
// the routed share, the tick is the level that actually leaves.
void ofApp::drawChart() {
    barHits.clear();
    const float x0 = 24.0f;
    const float w = ofGetWidth() - 48.0f;
    const float h = ofGetHeight() - chartTop - 20.0f;
    const float barArea = h - 30.0f;
    const float base = chartTop + barArea;

    ofPushStyle();
    ofSetColor(40, 43, 52);
    ofFill();
    ofDrawRectangle(x0, chartTop, w, h);

    if (mapping.targetCount() == 0 && mapping.aggregators().empty()) {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("no outputs yet -- press o to make one",
                           x0 + 12.0f, chartTop + 24.0f);
        ofPopStyle();
        return;
    }

    const WeightVector none;
    const WeightVector& wv = auditioning() ? evaluation.weights : none;
    const std::vector<float> routed = mapping.toChannels(wv);
    const std::vector<float> levels = mapping.toChannelLevels(wv);
    const float silence = auditioning() ? mapping.silenceShare(wv) : 0.0f;

    const std::size_t slots = routed.size() + 1;
    const float gap = 8.0f;
    const float bw = std::min(46.0f,
        (w - gap * float(slots + 1)) / float(std::max<std::size_t>(slots, 1)));

    float outputsTotal = 0.0f;
    for (std::size_t ch = 0; ch < routed.size(); ++ch) {
        const float bx = x0 + gap + float(ch) * (bw + gap);

        // Which output or derived output owns this channel, if any.
        TargetID owner = InvalidTarget;
        for (TargetID t = 0; t < mapping.targetCount(); ++t) {
            if (mapping.targetChannel(t) == int(ch)) owner = t;
        }
        std::string name;
        bool derived = false;
        if (owner != InvalidTarget) {
            name = mapping.targetName(owner);
            outputsTotal += routed[ch];
        } else {
            for (const auto& a : mapping.aggregators()) {
                if (a.channel == int(ch)) { name = a.name; derived = true; }
            }
        }

        ofSetColor(58, 62, 74);
        ofDrawRectangle(bx, chartTop + 6.0f, bw, barArea - 6.0f);
        if (name.empty()) {
            ofSetColor(90, 96, 110);
            ofDrawBitmapString(ofToString(ch), bx + 2.0f, base + 14.0f);
            continue;                              // an unused channel
        }

        const float v = ofClamp(routed[ch], 0.0f, 1.0f);
        ofSetColor(derived ? ofColor(180, 140, 230) : ofColor(120, 200, 160));
        ofDrawRectangle(bx, base - v * (barArea - 6.0f), bw,
                        v * (barArea - 6.0f));
        // The level after trim, as a tick across the bar.
        const float lv = ofClamp(levels[ch], 0.0f, 1.0f);
        ofSetColor(255, 255, 255, 200);
        const float ty = base - lv * (barArea - 6.0f);
        ofDrawLine(bx - 2.0f, ty, bx + bw + 2.0f, ty);

        if (owner != InvalidTarget && owner == currentOutput) {
            ofNoFill();
            ofSetColor(255, 214, 90);
            ofSetLineWidth(2.0f);
            ofDrawRectangle(bx - 2.0f, chartTop + 4.0f, bw + 4.0f, barArea - 2.0f);
            ofFill();
            ofSetLineWidth(1.0f);
        }
        ofSetColor(200, 205, 215);
        ofDrawBitmapString(name.substr(0, 6), bx, base + 14.0f);
        ofSetColor(140, 146, 160);
        ofDrawBitmapString(ofToString(ch), bx, base + 26.0f);
        if (owner != InvalidTarget) {
            barHits.push_back({bx, chartTop, bw, h, owner});
        }
    }

    // Silence, so the whole share is accounted for.
    const float sx = x0 + gap + float(routed.size()) * (bw + gap) + gap;
    ofSetColor(58, 62, 74);
    ofDrawRectangle(sx, chartTop + 6.0f, bw, barArea - 6.0f);
    const float sv = ofClamp(silence, 0.0f, 1.0f);
    ofSetColor(120, 126, 140);
    ofDrawRectangle(sx, base - sv * (barArea - 6.0f), bw, sv * (barArea - 6.0f));
    ofSetColor(200, 205, 215);
    ofDrawBitmapString("silent", sx, base + 14.0f);

    ofSetColor(auditioning() ? ofColor(235, 238, 245) : ofColor(110, 116, 130));
    const std::string total = auditioning()
        ? "outputs " + ofToString(outputsTotal, 3) + " + silence "
          + ofToString(silence, 3) + " = "
          + ofToString(outputsTotal + silence, 3)
        : "hold space to audition";
    ofDrawBitmapString(total, sx + bw + 16.0f, chartTop + 22.0f);
    ofPopStyle();
}

// ---------------------------------------------------------------------------
// the panel
// ---------------------------------------------------------------------------

void ofApp::drawPanel() {
    const float x = renderer->viewport().getRight() + 30.0f;
    float y = 40.0f;
    static const char* names[] = {"free", "square", "triangular", "polar"};

    ofSetColor(auditioning() ? ofColor(120, 200, 160) : ofColor(235, 238, 245));
    ofDrawBitmapString(auditioning()
        ? (auditionLocked ? "AUDITION  (locked -- v to edit)"
                          : "AUDITION  (release space to edit)")
        : "EDIT", x, y);
    y += 20.0f;

    ofSetColor(180, 186, 200);
    std::string g = std::string("grid ") + names[int(kind)];
    if (kind != GridKind::Free) {
        g += "  " + ofToString(spacing, 3) + "  "
           + ofToString(ofRadToDeg(rotation), 0) + " deg";
        if (kind == GridKind::Polar) g += "  " + ofToString(spokes) + " spokes";
    }
    ofDrawBitmapString(g, x, y);                                  y += 15.0f;
    ofDrawBitmapString(std::string("auto-output ")
                       + (autoOutput ? "on" : "off") + "  (p)", x, y);
    y += 15.0f;
    ofDrawBitmapString("nodes " + ofToString(manifold.nodeCount())
                       + "   regions " + ofToString(manifold.regionCount())
                       + "   selected " + ofToString(selected.size()), x, y);
    y += 15.0f;
    if (!topology.orphans.empty()) {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("orphans " + ofToString(topology.orphans.size())
                           + "  (in no region yet)", x, y);
        y += 15.0f;
    }
    y += 8.0f;

    ofSetColor(235, 238, 245);
    ofDrawBitmapString("outputs", x, y);                          y += 16.0f;
    for (TargetID t = 0; t < mapping.targetCount(); ++t) {
        const float tr = mapping.targetTrim(t);
        const std::string dB = tr > 0.0f
            ? ofToString(20.0f * std::log10(tr), 1) + " dB" : "off";
        ofSetColor(t == currentOutput ? ofColor(255, 214, 90)
                                      : ofColor(180, 186, 200));
        ofDrawBitmapString(std::string(t == currentOutput ? "> " : "  ")
                           + mapping.targetName(t) + "  ch "
                           + ofToString(mapping.targetChannel(t)) + "  " + dB,
                           x, y);
        y += 14.0f;
    }
    for (const auto& a : mapping.aggregators()) {
        ofSetColor(180, 140, 230);
        ofDrawBitmapString("  " + a.name + "  ch "
                           + (a.channel >= 0 ? ofToString(a.channel)
                                             : std::string("-"))
                           + "  sum of " + ofToString(a.sources.size()),
                           x, y);
        y += 14.0f;
    }
    y += 8.0f;
    if (messageTime > 0.0f) {
        ofSetColor(refusedFlash > 0.0f ? ofColor(232, 96, 88)
                                       : ofColor(120, 200, 160));
        ofDrawBitmapString(message, x, y);
    }
    y += 24.0f;

    // Help, in two columns: editing the map, and its outputs.
    const char* left[] = {
        "click empty  place node",   "drag empty   box select",
        "shift        add to select", "drag node    move selection",
        "alt+drag     free-hand",    "f  u         join  unjoin",
        "delete       remove nodes", "1-4          grid kind",
        "r R 0        rotate grid",  "- = [ ]      spacing spokes",
        "s  l         save  load",   "x  e         empty  example",
    };
    const char* right[] = {
        "o     new output",         "d     derived from selection",
        "tab   next output",        "b     bind to output",
        "q     add silence share",  "c     clear bindings",
        "t T   trim -/+ 1 dB",      "O     remove current output",
        "p     auto-output on/off", "space audition (hold)",
        "v     lock audition",      "click a bar: pick output",
    };
    ofSetColor(140, 146, 160);
    float yl = y;
    for (const char* hl : left) { ofDrawBitmapString(hl, x, yl); yl += 14.0f; }
    float yr = y;
    for (const char* hr : right) {
        ofDrawBitmapString(hr, x + 232.0f, yr);
        yr += 14.0f;
    }
}

// ---------------------------------------------------------------------------
// outputs
// ---------------------------------------------------------------------------

int ofApp::nextFreeChannel() const {
    int ch = 0;
    while (mapping.channelInUse(ch)) ++ch;
    return ch;
}

void ofApp::newOutput() {
    const int ch = nextFreeChannel();
    std::string nm = "out" + ofToString(ch);
    for (int k = 2; mapping.findTarget(nm) != InvalidTarget; ++k) {
        nm = "out" + ofToString(ch) + "_" + ofToString(k);
    }
    currentOutput = mapping.addOutput(nm, ch);
    message = "new output " + nm + " on channel " + ofToString(ch)
            + " -- select nodes and press b";
    messageTime = 3.0f;
}

// A derived output summing the selected nodes: the sub under a pair, say.
// Not a node -- it has no position -- so it appears in the panel and the
// chart, not on the map.
void ofApp::newDerived() {
    if (selected.empty()) {
        refusedFlash = 0.35f;
        message = "select the nodes a derived output should sum";
        messageTime = 2.5f;
        return;
    }
    const int ch = nextFreeChannel();
    std::string nm = "sum" + ofToString(ch);
    std::vector<std::pair<NodeID, float>> srcs;
    for (NodeID id : selected) srcs.emplace_back(id, 1.0f);
    mapping.addDerived(nm, ch, 1.0f, SumMode::Linear, srcs);
    message = "derived " + nm + " on channel " + ofToString(ch) + ", sum of "
            + ofToString(srcs.size());
    messageTime = 3.0f;
}

void ofApp::bindSelection() {
    if (currentOutput == InvalidTarget) {
        refusedFlash = 0.35f;
        message = "no output yet -- press o to make one";
        messageTime = 2.5f;
        return;
    }
    if (selected.empty()) return;
    for (NodeID id : selected) mapping.bind(id, currentOutput);
    message = "bound " + ofToString(selected.size()) + " to "
            + mapping.targetName(currentOutput);
    messageTime = 2.0f;
}

// A quarter-share of silence per press, so each press visibly lowers the
// node's fill. On a node with no output it changes nothing it would output --
// silence alone is null -- so that case says so instead.
void ofApp::silenceSelection() {
    if (selected.empty()) return;
    bool anyOutput = false;
    for (NodeID id : selected) {
        mapping.bindSilence(id, 0.25f);
        if (outputCount(id) > 0) anyOutput = true;
    }
    message = anyOutput ? "added a silence share -- the fill drops"
                        : "silence on a node with no output: still silent";
    messageTime = 2.5f;
}

void ofApp::clearSelection() {
    for (NodeID id : selected) mapping.clearBindings(id);
    if (!selected.empty()) {
        message = "cleared bindings on " + ofToString(selected.size());
        messageTime = 2.0f;
    }
}

void ofApp::cycleOutput() {
    if (mapping.targetCount() == 0) return;
    currentOutput = (currentOutput == InvalidTarget)
                  ? 0 : (currentOutput + 1) % mapping.targetCount();
    message = "current output: " + mapping.targetName(currentOutput);
    messageTime = 2.0f;
}

// Trim in dB on screen, stored as a linear gain. Capped at +12 dB; there is
// no floor above zero, so a trim can be taken far down but never to off.
void ofApp::trimCurrent(float dB) {
    if (currentOutput == InvalidTarget) return;
    const float t = mapping.targetTrim(currentOutput)
                  * std::pow(10.0f, dB / 20.0f);
    mapping.setTargetTrim(currentOutput, std::min(t, 3.9810717f));
}

// ---------------------------------------------------------------------------
// output removal, and the audition halo
// ---------------------------------------------------------------------------

bool ofApp::outputInUse(TargetID t) const {
    for (NodeID n : mapping.boundNodes()) {
        for (std::size_t k = 0; k < mapping.linkCount(n); ++k) {
            const Link& l = mapping.link(n, k);
            if (l.kind == DestKind::Output && l.id == t) return true;
        }
    }
    return false;
}

// TargetIDs after a removed output shift down by one, so the current output
// has to follow -- the same rule removeNodes() taught for NodeIDs.
void ofApp::afterOutputRemoved(TargetID t) {
    if (currentOutput == InvalidTarget) return;
    if (currentOutput == t) currentOutput = InvalidTarget;
    else if (currentOutput > t) --currentOutput;
    if (currentOutput == InvalidTarget && mapping.targetCount() > 0) {
        currentOutput = 0;
    }
}

// Remove the current output outright. Nodes that fed it and others
// renormalize onto the others; nodes that fed only it go silent -- the
// message says how many, since there is no undo yet.
void ofApp::removeCurrentOutput() {
    if (currentOutput == InvalidTarget) return;
    std::size_t feeders = 0;
    for (NodeID n : mapping.boundNodes()) {
        for (std::size_t k = 0; k < mapping.linkCount(n); ++k) {
            const Link& l = mapping.link(n, k);
            if (l.kind == DestKind::Output && l.id == currentOutput) {
                ++feeders;
                break;
            }
        }
    }
    const std::string nm = mapping.targetName(currentOutput);
    const TargetID t = currentOutput;
    mapping.removeOutput(t);
    afterOutputRemoved(t);
    refusedFlash = feeders ? 0.35f : 0.0f;
    message = "removed output " + nm
            + (feeders ? " -- " + ofToString(feeders)
                         + (feeders == 1 ? " node" : " nodes")
                         + " no longer reach it"
                       : std::string());
    messageTime = 3.5f;
}

float ofApp::nodeRadius(NodeID id) const {
    return 6.0f * ofClamp(std::sqrt(std::max(manifold.node(id).weight, 0.0f)),
                          0.35f, 2.5f);
}

// A node's LIVE share while auditioning, as a halo behind it -- the glow from
// example-spread, brought into the editor.
//
// Its own element, so it does not compete with the node's shape, fill or
// size (decision H). Two tones, matching the chart:
//
//   green core   the share that reaches outputs
//   grey rim     the share that goes to silence
//
// AREA grows in proportion to the share, not radius, because area is what
// the eye compares: radius sqrt(r^2 + K w), so a node with twice the share
// has twice the halo. A negative weight -- possible inside a non-convex
// region -- draws no halo; the renderer already draws it as a red line.
void ofApp::drawHalo(NodeID id, float weight) const {
    if (weight <= 0.0f || id >= manifold.nodeCount()) return;
    const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
    const float r = nodeRadius(id);
    const float K = (r + 30.0f) * (r + 30.0f) - r * r;   // full share: +30 px
    const float frac = mapping.outputFraction(id);
    const float total = std::sqrt(r * r + K * weight);
    const float core  = std::sqrt(r * r + K * weight * frac);

    ofPushStyle();
    ofFill();
    ofSetColor(120, 126, 140, 110);
    ofDrawCircle(p.x, p.y, total);
    if (frac > 0.0f) {
        ofSetColor(120, 200, 160, 130);
        ofDrawCircle(p.x, p.y, core);
    }
    ofPopStyle();
}
