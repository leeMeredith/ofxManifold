#include "ofApp.h"

using namespace ofxManifold;
using namespace ofxManifold::editor;

// ===========================================================================
// app -- assembling the pieces
// ===========================================================================

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
    model.loadExample();
    adopt();
    // Reopen the pair open last time. Failing that, the save an earlier
    // version of this editor made, editor.json -- its outputs file,
    // editor-mapping.json, is found by the pair's old-name fallback.
    const std::string lastFile = ofToDataPath("editor-last.txt");
    std::string last;
    if (ofFile::doesFileExist(lastFile)) {
        last = ofBufferFromFile(lastFile).getText();
        while (!last.empty() && (last.back() == '\n' || last.back() == '\r')) {
            last.pop_back();
        }
    }
    if (!last.empty() && ofFile::doesFileExist(last)) openAt(last);
    else if (ofFile::doesFileExist(ofToDataPath("editor.json"))) {
        openAt(ofToDataPath("editor.json"));
    }
    unsaved = false;
    // Undo starts from here. Reopening the last save above records an "open",
    // and without this, command-Z straight after launch would swap the saved
    // map for the example that preceded it.
    history.clear();
    rebuildGrid();
}

void ofApp::update() {
    // The map keeps a square; the band under it holds the output chart.
    const float chartH = 130.0f;
    const float side = std::max(160.0f, ofGetHeight() - 48.0f - chartH);
    renderer->setViewport(ofRectangle(24, 24, side, side));
    chartTop = 24.0f + side + 16.0f;

    // The faders' geometry, laid out by the package from the model. Drawing
    // and clicking both read this one layout, so they cannot disagree.
    chart = layoutChart(mapping, 24.0f, chartTop, ofGetWidth() - 48.0f,
                        ofGetHeight() - chartTop - 20.0f);

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

    // The window title names the open pair, with a dot for unsaved changes.
    const std::string title = "ofxManifold editor — "
        + (currentPath.empty() ? std::string("untitled")
                               : pairFor(currentPath).name)
        + (unsaved ? "  •" : "");
    if (title != shownTitle) {
        ofSetWindowTitle(title);
        shownTitle = title;
    }
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

    // Nodes feeding the CURRENT output: a green ring, outside the selection
    // ring so both can show at once. With the fader marks below, this is how
    // a binding can be seen before and after pressing b.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(1.5f);
    if (currentOutput != InvalidTarget) {
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            if (!model.feeds(NodeID(i), currentOutput)) continue;
            const glm::vec2 p = renderer->toScreen(manifold.node(NodeID(i)).position);
            ofSetColor(120, 200, 160);
            ofDrawCircle(p.x, p.y, 18.0f);
        }
    }
    ofPopStyle();

    // Selection rings, over the nodes.
    ofPushStyle();
    ofNoFill();
    ofSetLineWidth(2.0f);
    for (NodeID id : selection.nodes()) {
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

// ===========================================================================
// controller -- mouse and keys, turned into Model operations
//
// Every change to the map or its outputs is a call on `model`. The references
// the views read through are const, so this section is the only place that
// CAN change anything, and it can only do so through the model.
// ===========================================================================

// A Result on screen: its message for its time, and a red flash if refused.
void ofApp::show(const Result& r) {
    if (!r.message.empty()) {
        message = r.message;
        messageTime = r.seconds;
    }
    if (r.refused) refusedFlash = 0.35f;
}

void ofApp::act(const std::string& label,
                const std::function<Result(Model&)>& op) {
    const Result r = history.apply(model, label, op);
    markChanged(r);
    show(r);
}

// Undo and redo restore a whole snapshot. NodeIDs in the selection may name
// different nodes afterwards, so it clears, as after a delete; any drag in
// progress is dropped.
void ofApp::undo() {
    const Result r = history.undo(model);
    markChanged(r);
    show(r);
    selection.clear();
    dragging = false;
    anchor = InvalidNode;
}

void ofApp::redo() {
    const Result r = history.redo(model);
    markChanged(r);
    show(r);
    selection.clear();
    dragging = false;
    anchor = InvalidNode;
}

// After the map is replaced wholesale -- the example, a new map, a file. The
// renderer and evaluator are rebuilt, and the selection cleared, since it
// holds NodeIDs of a map that no longer exists.
void ofApp::adopt() {
    renderer = std::make_unique<ofxManifoldRenderer>(manifold);
    // Nodes are drawn here, not by the renderer: their shape and fill come
    // from the MAPPING, which the renderer deliberately knows nothing about.
    renderer->style.drawNodes = false;
    evaluator = std::make_unique<Evaluator>(manifold);
    evaluation = Evaluation{};
    selection.clear();
    dragging = false;
    anchor = InvalidNode;
}

// Place a node where the click landed. Placement leaves nothing selected --
// so laying out a row of speakers is click, click, click, and selecting is a
// separate act: click a node, or drag a box.
void ofApp::placeAt(glm::vec2 p) {
    act("place", [&](Model& m) { return m.place(p, grid); });
}

NodeID ofApp::nodeAt(glm::vec2 screen) const {
    const float grab = 10.0f;
    for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
        const glm::vec2 p = renderer->toScreen(manifold.node(NodeID(i)).position);
        if (glm::distance(p, screen) <= grab) return NodeID(i);
    }
    return InvalidNode;
}

void ofApp::mousePressed(int x, int y, int) {
    const glm::vec2 screen(x, y);

    // A click on the chart picks an output. It never edits the map.
    pressInChart = (y >= chartTop);
    if (pressInChart) {
        show(model.pickOutput(chart.outputAt(float(x), float(y))));
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
        // With nodes selected, a click on empty space only DESELECTS; it does
        // not also place a node. Noted now, acted on at release, since a drag
        // from here is still a box select.
        pressHadSelection = !selection.empty();
        if (!shift) selection.clear();
        return;
    }

    // Shift-click edits the selection; it does not start a move.
    if (shift) {
        selection.toggle(hit);
        return;
    }

    // A plain click on a node selects it, unless it is already part of a
    // selection -- then the selection is kept, so the group can be dragged.
    if (!selection.contains(hit)) selection.only(hit);

    dragging = true;
    anchor = hit;
    const glm::vec2 anchorPos = manifold.node(anchor).position;
    // Where on the node it was grabbed, so it does not jump to the cursor.
    grabOffset = anchorPos - renderer->toManifold(screen);
    dragFrom.clear();
    for (NodeID id : selection.nodes()) {
        dragFrom.push_back(manifold.node(id).position);
    }
    anchorAddr = grid.snap(anchorPos).address;
    // A drag is one undo step, not one per frame: the state it began from is
    // kept here and recorded at release, if any part of the drag landed.
    dragBefore = model;
    dragMoved = false;
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
    // Snapping each node independently would distort the selection's shape
    // (authoring decision J). Alt forces free-hand for this stretch of the
    // drag, and forgets the grid point held, so releasing alt mid-drag snaps
    // from where the node now is rather than where it last snapped.
    glm::vec2 snapped = target;
    if (!ofGetKeyPressed(OF_KEY_ALT)) {
        const SnapResult s = grid.snapHysteresis(target, anchorAddr, deadZone);
        snapped = s.point;
        anchorAddr = s.address;
    } else {
        anchorAddr = GridAddress{};
    }

    const std::vector<NodeID>& sel = selection.nodes();
    std::size_t ai = 0;
    for (std::size_t i = 0; i < sel.size(); ++i) {
        if (sel[i] == anchor) { ai = i; break; }
    }
    const glm::vec2 delta = snapped - dragFrom[ai];

    std::vector<std::pair<NodeID, glm::vec2>> moves;
    moves.reserve(sel.size());
    for (std::size_t i = 0; i < sel.size(); ++i) {
        moves.emplace_back(sel[i], dragFrom[i] + delta);
    }
    // One operation, all or nothing, checked against FINAL shapes. On refusal
    // nothing changes, so the selection stays at its last valid position.
    const Result r = model.move(moves);
    if (r.changed) dragMoved = true;
    show(r);
}

void ofApp::mouseReleased(int x, int y, int) {
    const glm::vec2 screen(x, y);
    if (pressInChart) { pressInChart = false; return; }
    if (pressAudition) { pressAudition = false; return; }

    if (pressingEmpty) {
        pressingEmpty = false;
        // Barely moved: a click. It places a node only if nothing was
        // selected when it began -- otherwise it was a deselect.
        if (glm::distance(screen, pressScreen) < 4.0f) {
            if (!pressHadSelection) placeAt(renderer->toManifold(screen));
            return;
        }
        // Otherwise a box: every node inside it joins the selection.
        const float x0 = std::min(pressScreen.x, screen.x);
        const float x1 = std::max(pressScreen.x, screen.x);
        const float y0 = std::min(pressScreen.y, screen.y);
        const float y1 = std::max(pressScreen.y, screen.y);
        for (std::size_t i = 0; i < manifold.nodeCount(); ++i) {
            const NodeID id = NodeID(i);
            const glm::vec2 p = renderer->toScreen(manifold.node(id).position);
            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) {
                selection.addFromBox(id);
            }
        }
        return;
    }

    if (dragging && dragMoved) {
        history.record(dragBefore, "move");
        unsaved = true;
    }
    dragging = false;
    anchor = InvalidNode;
}

void ofApp::keyPressed(int key) {
    // ---- undo, redo: command-Z and shift-command-Z ----
    const bool command = ofGetKeyPressed(OF_KEY_COMMAND);
    if (command) {
        const bool shift = ofGetKeyPressed(OF_KEY_SHIFT);
        if (key == 'z' || key == 'Z' || key == 26) {
            if (key == 'Z' || shift) redo(); else undo();
        } else if (key == 's' || key == 'S' || key == 19) {
            if (key == 'S' || shift) saveAs(); else save();
        } else if (key == 'o' || key == 'O' || key == 15) {
            open();
        }
        // Nothing else acts with command held: command-O must never also
        // make a new output, as a plain o would.
        return;
    }

    // ---- grid tool ----
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
    // Dead zone for drag hysteresis, as a fraction of grid spacing.
    if (key == ',') deadZone = std::max(0.0f, deadZone - 0.05f);
    if (key == '.') deadZone = std::min(0.50f, deadZone + 0.05f);

    // ---- selection ----
    if (key == 'a') selection.selectAll(manifold.nodeCount());
    if (key == OF_KEY_ESC) selection.clear();

    // ---- the map ----
    if (key == 'x') {
        currentPath.clear();
        act("new map", [](Model& m) {
            m.newEmpty();
            return Result{true, false,
                          "new empty map -- click to place nodes", 3.0f};
        });
        adopt();
    }
    if (key == 'e') {
        currentPath.clear();
        act("example map", [](Model& m) {
            m.loadExample();
            return Result{true, false, "", 0.0f};
        });
        adopt();
    }
    if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) {
        // Removal renumbers NodeIDs; the selection holds them, so it clears.
        act("delete", [&](Model& m) { return m.remove(selection); });
        selection.clear();
    }
    if (key == 'u') act("unjoin", [&](Model& m) { return m.unjoin(selection); });
    if (key == 'f') act("join", [&](Model& m) { return m.join(selection); });

    // ---- outputs ----
    if (key == 'o') act("new output", [](Model& m) { return m.newOutput(); });
    if (key == 'd') act("derived output",
                        [&](Model& m) { return m.newDerived(selection); });
    if (key == 'b') act("bind", [&](Model& m) { return m.bind(selection); });
    if (key == 'B') act("bind to every output",
                        [&](Model& m) { return m.bindAll(selection); });
    // Fill: q down, w up, 5% a step; shift for 1%. Shift arrives as the
    // capital letter, so the capital means "fine" and never "up".
    if (key == 'q' || key == 'Q' || key == 'w' || key == 'W') {
        const int  dir  = (key == 'w' || key == 'W') ? +1 : -1;
        const bool fine = (key == 'Q' || key == 'W');
        act("fill", [&, dir, fine](Model& m) {
            return m.stepFill(selection, dir, fine); });
    }
    if (key == 'c') act("clear bindings",
                        [&](Model& m) { return m.clear(selection); });
    if (key == OF_KEY_TAB) show(model.cycleOutput());
    if (key == 't') act("trim", [](Model& m) { return m.trimCurrent(-1.0f); });
    if (key == 'T') act("trim", [](Model& m) { return m.trimCurrent(+1.0f); });
    if (key == 'O') act("remove output",
                        [](Model& m) { return m.removeCurrentOutput(); });
    if (key == 'p') show(model.toggleAutoOutput());


    // ---- listening ----
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

// ===========================================================================
// grid tool -- a preference of the session, never saved into a map
// ===========================================================================

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

// ===========================================================================
// MapView -- grid lines. Regions, selection rings and the drag box are drawn
// in draw() above and move here in round 2. Reads only.
// ===========================================================================

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

// ===========================================================================
// NodeGlyph -- one node's shape, fill and audition halo. Reads only.
// ===========================================================================

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

float ofApp::nodeRadius(NodeID id) const {
    return 6.0f * ofClamp(std::sqrt(std::max(manifold.node(id).weight, 0.0f)),
                          0.35f, 2.5f);
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
    const std::size_t outs = model.outputCount(id);
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

// ===========================================================================
// OutputChart -- the faders, drawn from the layout the package computed.
// Reads only.
// ===========================================================================

// One bar per OUTPUT, not per node, in channel order -- outputs are what is
// heard. Derived outputs in their own colour. A silence bar at the end, so
// outputs plus silence always total exactly 1. Trim as a tick: the bar is the
// routed share, the tick is the level that actually leaves.
void ofApp::drawChart() const {
    ofPushStyle();
    ofSetColor(40, 43, 52);
    ofFill();
    ofDrawRectangle(chart.x0, chart.top, chart.width, chart.height);

    if (chart.empty) {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("no outputs yet -- press o to make one",
                           chart.x0 + 12.0f, chart.top + 24.0f);
        ofPopStyle();
        return;
    }

    const WeightVector none;
    const WeightVector& wv = auditioning() ? evaluation.weights : none;
    const std::vector<float> routed = mapping.toChannels(wv);
    const std::vector<float> levels = mapping.toChannelLevels(wv);
    const float silence = auditioning() ? mapping.silenceShare(wv) : 0.0f;
    const float bw = chart.barWidth;
    const float rise = chart.barArea - 6.0f;

    float outputsTotal = 0.0f;
    for (const ChartBar& b : chart.bars) {
        const std::size_t ch = std::size_t(b.channel);
        ofSetColor(58, 62, 74);
        ofDrawRectangle(b.x, chart.top + 6.0f, bw, rise);
        if (b.kind == ChartBar::Kind::Unused) {
            ofSetColor(90, 96, 110);
            ofDrawBitmapString(ofToString(ch), b.x + 2.0f, chart.base + 14.0f);
            continue;
        }
        const bool derived = (b.kind == ChartBar::Kind::Derived);
        if (!derived) outputsTotal += routed[ch];

        const float v = ofClamp(routed[ch], 0.0f, 1.0f);
        ofSetColor(derived ? ofColor(180, 140, 230) : ofColor(120, 200, 160));
        ofDrawRectangle(b.x, chart.base - v * rise, bw, v * rise);
        // The level after trim, as a tick across the bar.
        const float lv = ofClamp(levels[ch], 0.0f, 1.0f);
        ofSetColor(255, 255, 255, 200);
        const float ty = chart.base - lv * rise;
        ofDrawLine(b.x - 2.0f, ty, b.x + bw + 2.0f, ty);

        // A dot above each output a selected node feeds -- the other half of
        // seeing a binding: which faders this selection reaches.
        if (!derived) {
            bool fed = false;
            for (NodeID id : selection.nodes()) {
                if (model.feeds(id, b.output)) { fed = true; break; }
            }
            if (fed) {
                ofSetColor(255, 214, 90);
                ofDrawCircle(b.x + bw * 0.5f, chart.top + 1.0f, 3.0f);
            }
        }

        if (!derived && b.output == currentOutput) {
            ofNoFill();
            ofSetColor(255, 214, 90);
            ofSetLineWidth(2.0f);
            ofDrawRectangle(b.x - 2.0f, chart.top + 4.0f, bw + 4.0f,
                            chart.barArea - 2.0f);
            ofFill();
            ofSetLineWidth(1.0f);
        }
        ofSetColor(200, 205, 215);
        ofDrawBitmapString(b.name.substr(0, 6), b.x, chart.base + 14.0f);
        ofSetColor(140, 146, 160);
        ofDrawBitmapString(ofToString(ch), b.x, chart.base + 26.0f);
    }

    // The fader line: each selected node's fill, on each output it feeds --
    // the same percentage its shape shows, so pressing q or w moves the line
    // and the shape together. Yellow, as the selection is; the white tick is
    // the level after trim, a different thing.
    ofSetLineWidth(2.0f);
    ofSetColor(255, 214, 90);
    for (const FillMark& m : fillMarks(model, selection)) {
        for (const ChartBar& b : chart.bars) {
            if (b.kind != ChartBar::Kind::Output || b.output != m.output) continue;
            const float y = chart.base - ofClamp(m.fill, 0.0f, 1.0f) * rise;
            ofDrawLine(b.x, y, b.x + bw, y);
        }
    }
    ofSetLineWidth(1.0f);

    // Silence, so the whole share is accounted for.
    ofSetColor(58, 62, 74);
    ofDrawRectangle(chart.silenceX, chart.top + 6.0f, bw, rise);
    const float sv = ofClamp(silence, 0.0f, 1.0f);
    ofSetColor(120, 126, 140);
    ofDrawRectangle(chart.silenceX, chart.base - sv * rise, bw, sv * rise);
    ofSetColor(200, 205, 215);
    ofDrawBitmapString("silent", chart.silenceX, chart.base + 14.0f);

    ofSetColor(auditioning() ? ofColor(235, 238, 245) : ofColor(110, 116, 130));
    const std::string total = auditioning()
        ? "outputs " + ofToString(outputsTotal, 3) + " + silence "
          + ofToString(silence, 3) + " = "
          + ofToString(outputsTotal + silence, 3)
        : "hold space to audition";
    ofDrawBitmapString(total, chart.totalX, chart.top + 22.0f);
    ofPopStyle();
}

// ===========================================================================
// KeyPanel -- the side panel and help. Reads only.
// ===========================================================================

// ---------------------------------------------------------------------------
// the panel
// ---------------------------------------------------------------------------
void ofApp::drawPanel() const {
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
    ofDrawBitmapString((currentPath.empty() ? std::string("untitled")
                                            : pairFor(currentPath).name)
                       + (unsaved ? "   (unsaved)" : ""), x, y);
    y += 15.0f;
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
                       + "   selected " + ofToString(selection.size()), x, y);
    y += 15.0f;
    // The selection's fill, so a q or w press can be read before and after.
    if (!selection.empty()) {
        float lo = 2.0f, hi = -1.0f;
        for (NodeID id : selection.nodes()) {
            if (model.outputCount(id) == 0) continue;
            lo = std::min(lo, mapping.outputFraction(id));
            hi = std::max(hi, mapping.outputFraction(id));
        }
        const std::string fill = hi < 0.0f ? std::string("- (no output)")
            : ofToString(int(std::lround(lo * 100))) + "%"
              + (std::lround(lo * 100) == std::lround(hi * 100) ? std::string()
                 : " to " + ofToString(int(std::lround(hi * 100))) + "%");
        ofDrawBitmapString("fill " + fill, x, y);
        y += 15.0f;
    }
    if (!model.topology().orphans.empty()) {
        ofSetColor(140, 146, 160);
        ofDrawBitmapString("orphans " + ofToString(model.topology().orphans.size())
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
        "x  e         empty  example", "cmd-s  save   (+shift: as)",
        "cmd-o  open a pair",
    };
    const char* right[] = {
        "o     new output",         "d     derived from selection",
        "tab   next output",        "b     bind/unbind current",
        "B     bind to every output", "q w   fill -/+ 5% (shift 1%)",
        "c     clear bindings",     "t T   trim -/+ 1 dB",
        "O     remove current output", "p     auto-output on/off",
        "space audition (hold)",    "v     lock audition",
        "cmd-z undo, shift redo",   "click a bar: pick output",
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

// ===========================================================================
// EditorFiles -- the map and its outputs, as a pair of files
// ===========================================================================

void ofApp::markChanged(const Result& r) {
    if (r.changed) unsaved = true;
}

void ofApp::remember() const {
    ofBuffer b;
    b.set(currentPath);
    ofBufferToFile(ofToDataPath("editor-last.txt"), b);
}

void ofApp::save() {
    if (currentPath.empty()) { saveAs(); return; }
    const Result r = savePair(model, currentPath);
    show(r);
    if (!r.refused) {
        unsaved = false;
        remember();
    }
}

void ofApp::saveAs() {
    const std::string suggested = currentPath.empty()
        ? std::string("untitled") : pairFor(currentPath).name;
    ofFileDialogResult d =
        ofSystemSaveDialog(suggested, "Save the map and its outputs");
    if (!d.bSuccess) return;
    const Result r = savePair(model, d.getPath());
    show(r);
    if (!r.refused) {
        currentPath = pairFor(d.getPath()).map;
        unsaved = false;
        remember();
    }
}

void ofApp::open() {
    ofFileDialogResult d =
        ofSystemLoadDialog("Open a map -- either file of a pair");
    if (d.bSuccess) openAt(d.getPath());
}

// Opening is one undo step, like any edit: history.apply records it only if
// the map was actually replaced, so a file that cannot be read leaves both
// the map and the history as they were.
void ofApp::openAt(const std::string& path) {
    const Result r = history.apply(model, "open",
        [&](Model& m) { return openPair(m, path); });
    show(r);
    if (r.changed) {
        adopt();
        currentPath = pairFor(path).map;
        unsaved = false;
        remember();
    }
}
