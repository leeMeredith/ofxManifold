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
    // The text size chosen last time, if any.
    const std::string sizeFile = ofToDataPath("editor-text-size.txt");
    if (ofFile::doesFileExist(sizeFile)) {
        const int sz = ofToInt(ofBufferFromFile(sizeFile).getText());
        if (sz >= 8 && sz <= 24) text.setSize(sz);
    }
    // Nine-tenths of the screen, centred, rather than a fixed 1024 x 640:
    // everything is laid out from the window's size, so it fits any screen.
    const int sw = ofGetScreenWidth(), sh = ofGetScreenHeight();
    if (sw > 0 && sh > 0) {
        ofSetWindowShape(int(sw * 0.9f), int(sh * 0.9f));
        ofSetWindowPosition(int(sw * 0.05f), int(sh * 0.05f));
    }
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
    text.fit();
    // The map keeps a square; the band under it holds the output chart,
    // taller on a taller window, and never too short for its two label rows.
    const float chartH = std::max(130.0f, ofGetHeight() * 0.2f);
    const float side = std::max(160.0f, ofGetHeight() - 48.0f - chartH);
    renderer->setViewport(ofRectangle(24, 24, side, side));
    chartTop = 24.0f + side + 16.0f;

    // The faders' geometry, laid out by the package from the model. Drawing
    // and clicking both read this one layout, so they cannot disagree.
    chart = layoutChart(mapping, 24.0f, chartTop, ofGetWidth() - 48.0f,
                        ofGetHeight() - chartTop - 20.0f, ChartText{text.charWidth(), text.lineHeight()});

    // The listening point is permanent, so the levels are always live: an
    // edit -- a moved node, a fill step, a binding -- shows on the faders at
    // once, without holding space. Audition only decides what a drag does.
    // The hint is forgotten every frame, since an edit between frames can
    // renumber regions (D-015: at editor sizes the scan costs nothing).
    evaluator->reset();
    evaluation = evaluator->evaluate(auditionPoint);
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
    // Everything drawn lives in the package's views (ofxManifoldEditorViews.h);
    // the app only gathers what they read. They take it all as const.
    Scene sc;
    sc.model = &model;
    sc.selection = &selection;
    sc.renderer = renderer.get();
    sc.evaluation = &evaluation;
    sc.text = &text;
    sc.listeningPoint = auditionPoint;
    sc.auditioning = auditioning();
    sc.mouse = mouse;

    GridLook look;
    look.kind = kind;
    look.grid = &grid;
    look.spacing = spacing;
    look.rotation = rotation;
    look.spokes = spokes;

    MapOverlay over;
    over.refused = refusedFlash > 0.0f;
    over.boxing = pressingEmpty;
    over.boxFrom = pressScreen;
    over.boxTo = boxScreen;

    PanelStatus st;
    st.auditionLocked = auditionLocked;
    st.name = currentPath.empty() ? std::string("untitled")
                                  : pairFor(currentPath).name;
    st.unsaved = unsaved;
    st.message = message;
    st.messageShown = messageTime > 0.0f;
    st.warning = refusedFlash > 0.0f;

    drawMap(sc, look, over);
    drawChart(sc, chart);
    drawPanel(sc, look, st, keyContext());
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
        // Click picks one output; shift-click adds or removes one, as with
        // nodes.
        const TargetID hit = chart.outputAt(float(x), float(y));
        if (ofGetKeyPressed(OF_KEY_SHIFT)) show(model.toggleOutput(hit));
        else show(model.pickOutput(hit));
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
    mouse = screen;
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

void ofApp::mouseMoved(int x, int y) {
    mouse = glm::vec2(float(x), float(y));
}

// What each key IS, by the key map's id. Command combinations come first:
// command-O must never also count as a plain o.
std::string ofApp::keyId(int key) const {
    if (ofGetKeyPressed(OF_KEY_COMMAND)) {
        const bool shift = ofGetKeyPressed(OF_KEY_SHIFT);
        // With control held, f may arrive as its control code, 6.
        if (ofGetKeyPressed(OF_KEY_CONTROL) && (key == 'f' || key == 'F' || key == 6)) {
            return "fullscreen";
        }
        if (key == '=' || key == '+' || key == '-' || key == '_') return "textsize";
        if (key == 'z' || key == 'Z' || key == 26) {
            return (key == 'Z' || shift) ? "redo" : "undo";
        }
        if (key == 's' || key == 'S' || key == 19) {
            return (key == 'S' || shift) ? "saveas" : "save";
        }
        if (key == 'o' || key == 'O' || key == 15) return "open";
        return "";
    }
    switch (key) {
    case '1': case '2': case '3': case '4':        return "gridkind";
    case 'r': case 'R': case '0':                   return "rotate";
    case '-': case '=': case '+':                   return "spacing";
    case '[': case ']':                             return "spokes";
    case ',': case '.':                             return "deadzone";
    case 'a':                                       return "all";
    case OF_KEY_ESC:                                return "none";
    case 'x':                                       return "empty";
    case 'e':                                       return "example";
    case OF_KEY_BACKSPACE: case OF_KEY_DEL:         return "remove";
    case 'u':                                       return "unjoin";
    case 'f':                                       return "join";
    case 'o':                                       return "newoutput";
    case 'd':                                       return "derived";
    case 'b':                                       return "bind";
    case 'B':                                       return "bindall";
    case 'q': case 'Q': case 'w': case 'W':         return "fill";
    case 'c':                                       return "clear";
    case OF_KEY_TAB:                                return "nextoutput";
    case 't': case 'T':                             return "trim";
    case 'O':                                       return "removeoutput";
    case 'p':                                       return "autooutput";
    case ' ':                                       return "audition";
    case 'v':                                       return "lock";
    default:                                        return "";
    }
}

KeyContext ofApp::keyContext() const {
    KeyContext ctx;
    ctx.model = &model;
    ctx.selection = &selection;
    ctx.canUndo = history.canUndo();
    ctx.canRedo = history.canRedo();
    ctx.dragging = dragging;
    ctx.auditioning = auditioning();
    ctx.grid = kind == GridKind::Free ? GridShape::Free
             : kind == GridKind::Polar ? GridShape::Polar
             : GridShape::Lattice;
    return ctx;
}

bool ofApp::allowed(const std::string& id) {
    std::string why;
    if (keyAvailable(keyContext(), id, &why)) return true;
    if (!why.empty()) show(Result{false, true, why, 2.0f});
    return false;
}

void ofApp::keyPressed(int key) {
    // Every key asks the key map first: a dim key does not act, it says why.
    const std::string id = keyId(key);
    if (id.empty() || !allowed(id)) return;

    // ---- window ----
    if (id == "fullscreen") { ofToggleFullscreen(); return; }
    if (id == "textsize") {
        setTextSize(text.size() + ((key == '-' || key == '_') ? -1 : 1));
        return;
    }

    // ---- file ----
    if (id == "undo")   { undo(); return; }
    if (id == "redo")   { redo(); return; }
    if (id == "save")   { save(); return; }
    if (id == "saveas") { saveAs(); return; }
    if (id == "open")   { open(); return; }
    if (id == "empty") {
        currentPath.clear();
        act("new map", [](Model& m) {
            m.newEmpty();
            return Result{true, false,
                          "new empty map -- click to place nodes", 3.0f};
        });
        adopt();
        return;
    }
    if (id == "example") {
        currentPath.clear();
        act("example map", [](Model& m) {
            m.loadExample();
            return Result{true, false, "", 0.0f};
        });
        adopt();
        return;
    }

    // ---- grid tool ----
    if (id == "gridkind" || id == "rotate" || id == "spacing" || id == "spokes") {
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
        rebuildGrid();
        return;
    }
    // Dead zone for drag hysteresis, as a fraction of grid spacing.
    if (id == "deadzone") {
        if (key == ',') deadZone = std::max(0.0f, deadZone - 0.05f);
        else            deadZone = std::min(0.50f, deadZone + 0.05f);
        return;
    }

    // ---- selection ----
    if (id == "all")  { selection.selectAll(manifold.nodeCount()); return; }
    if (id == "none") { selection.clear(); return; }

    // ---- the map ----
    if (id == "remove") {
        // Removal renumbers NodeIDs; the selection holds them, so it clears.
        act("delete", [&](Model& m) { return m.remove(selection); });
        selection.clear();
        return;
    }
    if (id == "unjoin") { act("unjoin", [&](Model& m) { return m.unjoin(selection); }); return; }
    if (id == "join")   { act("join", [&](Model& m) { return m.join(selection); }); return; }

    // ---- outputs ----
    if (id == "newoutput") { act("new output", [](Model& m) { return m.newOutput(); }); return; }
    if (id == "derived") {
        act("derived output", [&](Model& m) { return m.newDerived(selection); });
        return;
    }
    if (id == "bind") { act("binding", [&](Model& m) { return m.bind(selection); }); return; }
    if (id == "bindall") {
        act("bind to every output", [&](Model& m) { return m.bindAll(selection); });
        return;
    }
    // Fill: q down, w up, 5% a step; shift for 1%. Shift arrives as the
    // capital letter, so the capital means "fine" and never "up".
    if (id == "fill") {
        const int  dir  = (key == 'w' || key == 'W') ? +1 : -1;
        const bool fine = (key == 'Q' || key == 'W');
        act("fill", [&, dir, fine](Model& m) {
            return m.stepFill(selection, dir, fine); });
        return;
    }
    if (id == "clear") {
        act("clear bindings", [&](Model& m) { return m.clear(selection); });
        return;
    }
    if (id == "nextoutput") { show(model.cycleOutput()); return; }
    if (id == "trim") {
        const float dB = (key == 'T') ? +1.0f : -1.0f;
        act("trim", [dB](Model& m) { return m.trimCurrent(dB); });
        return;
    }
    if (id == "removeoutput") {
        act("remove output", [](Model& m) { return m.removeCurrentOutput(); });
        return;
    }
    if (id == "autooutput") { show(model.toggleAutoOutput()); return; }

    // ---- listening ----
    if (id == "audition") { auditionHeld = true; return; }
    if (id == "lock") {
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
// Text -- the size is the app's to choose and remember; the drawing is the
// package's (editor::Text).
// ===========================================================================

void ofApp::setTextSize(int size) {
    text.setSize(std::max(8, std::min(24, size)));
    ofBuffer b;
    b.set(ofToString(text.size()));
    ofBufferToFile(ofToDataPath("editor-text-size.txt"), b);
    message = "text size " + ofToString(text.size());
    messageTime = 1.5f;
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
