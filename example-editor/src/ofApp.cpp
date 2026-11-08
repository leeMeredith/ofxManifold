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
    // The 3D view starts hidden; its camera takes the mouse only when shown.
    cam.disableMouseInput();
}

void ofApp::update() {
    text.fit();
    // The window laid out by the package (3A-10): the map, the level dots,
    // the 3D view, the panel above the faders. If the keys do not fit, the
    // views give up width to the panel.
    const EditorLayout L = layoutEditor(ofGetWidth(), ofGetHeight(),
                                        text.charWidth(), text.lineHeight(),
                                        model.map3d().count() > 1, show3D,
                                        !model.map3d().valueSet().empty(),
                                        keyMap(keyContext()));
    const float plan = L.plan;
    renderer->setViewport(ofRectangle(24, 24, plan, plan));
    if (show3D) {
        view3D = ofRectangle(L.view3DX, 24.0f, plan, plan);
        if (!aimed) { aimLevelView(cam, view3D); aimed = true; }
        cam.setControlArea(view3D);
        levelsKeysAt = glm::vec2(L.levelsKeysX, L.levelsKeysY);
        levelsKeysWidth = L.levelsKeysWidth;
    } else {
        levelsKeysAt = glm::vec2(-1.0f, -1.0f);
    }
    panelLeft = L.panelLeft;
    panelBottom = L.panelBottom;
    chartTop = L.chartTop;
    chartRight = L.chartRight;

    // The faders' geometry, laid out by the package from the model. Drawing
    // and clicking both read this one layout, so they cannot disagree.
    chart = layoutChart(model.mapping(), 24.0f, chartTop, chartRight - 24.0f,
                        ofGetHeight() - chartTop - 20.0f, ChartText{text.charWidth(), text.lineHeight()});

    // The listening point is permanent, so the levels are always live: an
    // edit -- a moved node, a fill step, a binding -- shows on the faders at
    // once, without holding space. Audition only decides what a drag does.
    // The hint is forgotten every frame, since an edit between frames can
    // renumber regions (D-015: at editor sizes the scan costs nothing).
    evaluator->reset();
    evaluation = evaluator->evaluate(auditionPoint);

    // The listening point's height: gliding while up or down is held in
    // audition, otherwise on the current level under the point.
    if (!auditioning()) { zFree = false; glideDir = 0; }
    if (glideDir != 0) {
        auditionZ = glide(auditionZ, glideDir, ofGetKeyPressed(OF_KEY_SHIFT),
                          float(ofGetLastFrameTime()));
        zFree = true;
        message = "listening at " + ofToString(auditionZ, 2);
        messageTime = 1.0f;
    }
    if (!zFree) auditionZ = listenStart(model, evaluation);
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
    sc.listeningZ = auditionZ;
    sc.auditioning = auditioning();
    sc.panelLeft = panelLeft;
    sc.panelBottom = panelBottom;
    sc.levelsKeysAt = levelsKeysAt;
    sc.levelsKeysWidth = levelsKeysWidth;
    sc.typing = typing;
    sc.typingPrompt = typingNew ? "new value" : "value";
    sc.typingLine = typingLine;
    sc.levelDrag = levelMoved ? levelPress : -1;
    sc.levelDragBase = levelDragBase;
    sc.mouse = mouse;
    // The layout l would make, after [ or ], while the mouse is on the map.
    LayoutPlan preview;
    const ofRectangle mapArea = renderer->viewport();
    if (previewLayout && mapArea.inside(mouse.x, mouse.y)) {
        preview = model.planLayout(renderer->toManifold(mouse), grid,
                                   layoutSize(), kind == GridKind::Triangular);
        sc.layoutPreview = &preview;
    }

    GridLook look;
    look.kind = kind;
    look.grid = &grid;
    look.spacing = spacing;
    look.rotation = rotation;
    look.spokes = spokes;
    // The lines through where a node is going: the grid point under the
    // mouse while placing, or the moved node's while dragging (3A-11).
    if (kind != GridKind::Free && !auditioning()) {
        glm::vec2 at(-1.0f, -1.0f);
        if (dragging && anchor != InvalidNode) {
            at = model.manifold().node(anchor).position;
        } else if (renderer->viewport().inside(mouse.x, mouse.y)
                   && !pressingEmpty) {
            at = renderer->toManifold(mouse);
        }
        if (at.x >= 0.0f) {
            look.lineOn = true;
            look.lineAt = grid.snap(at).address;
            for (NodeID n : model.linedUp(at, grid, kind == GridKind::Triangular)) {
                if (dragging && selection.contains(n)) continue;
                look.lined.push_back(n);
            }
        }
    }

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
    if (show3D) drawLevelView(sc, cam, view3D);
    // What reaches the room at the listening point, height included; with
    // one level exactly the 2D reading.
    drawChart(sc, chart, chartLevels3D(model, selection, evaluation, auditionZ));
    drawPanel(sc, look, st, keyContext());
    if (keySheet) drawKeySheet(sc, keyContext());
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
    const std::size_t depth = history.undoCount();
    const Result r = history.apply(model, label, op);
    if (r.changed) {
        // A new step ends the redo branch, and its name marks with it; and
        // when the history is full its oldest step went, so the marks shift.
        if (history.undoCount() == depth) {
            for (PathMark& pm : pathMarks) --pm.depth;
        }
        pathMarks.erase(std::remove_if(pathMarks.begin(), pathMarks.end(),
            [&](const PathMark& pm) {
                return pm.depth == 0 || pm.depth >= history.undoCount();
            }), pathMarks.end());
    }
    markChanged(r);
    show(r);
}

// Undo and redo restore a whole snapshot. NodeIDs in the selection may name
// different nodes afterwards, so it clears, as after a delete; any drag in
// progress is dropped.
void ofApp::undo() {
    const std::size_t depth = history.undoCount();
    const Result r = history.undo(model);
    // Undoing cmd-n or cmd-e gives the map back its name (3A-11).
    if (r.changed) {
        for (const PathMark& pm : pathMarks) {
            if (pm.depth == depth) currentPath = pm.before;
        }
    }
    markChanged(r);
    show(r);
    selection.clear();
    dragging = false;
    anchor = InvalidNode;
}

void ofApp::redo() {
    const Result r = history.redo(model);
    if (r.changed) {
        for (const PathMark& pm : pathMarks) {
            if (pm.depth == history.undoCount()) currentPath.clear();
        }
    }
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
    previewLayout = false;
    // A click that closes the key sheet does nothing else, nor do its drag
    // and release.
    if (keySheet) { keySheet = false; sheetClick = true; return; }

    // The 3D view's area belongs to its camera: a press there turns the view
    // and never reaches the map.
    pressIn3D = show3D && x >= view3D.getX() && x <= view3D.getRight()
             && y >= view3D.getY() && y <= view3D.getBottom();
    if (pressIn3D) return;

    // A level's dot: a click goes to that level, a drag moves the level.
    levelPress = dotAt(columnNow(), columnX(), float(x), float(y));
    if (levelPress >= 0) {
        levelPressY = float(y);
        levelDragBase = model.map3d().levels().base(LevelID(levelPress));
        levelMoved = false;
        return;
    }

    // A click on the chart picks an output. It never edits the map.
    pressInChart = (y >= chartTop && x <= chartRight);
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
    if (sheetClick) return;
    if (pressIn3D) return;
    if (levelPress >= 0) {
        const DotColumn c = columnNow();
        const float gap = c.dots.size() > 1 ? c.dots[0] - c.dots[1] : 1.0f;
        const float px = float(y) - levelPressY;
        if (std::fabs(px) > 3.0f) levelMoved = true;
        if (levelMoved) {
            levelDragBase = levelDragTo(model, LevelID(levelPress), px, gap);
        }
        return;
    }
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
    if (sheetClick) { sheetClick = false; return; }
    if (pressIn3D) { pressIn3D = false; return; }
    if (levelPress >= 0) {
        const LevelID k = LevelID(levelPress);
        const float base = levelDragBase;
        if (levelMoved) {
            act("move level", [k, base](Model& m) { return m.moveLevel(k, base); });
        } else {
            show(model.goToLevel(k));
        }
        levelPress = -1;
        levelMoved = false;
        return;
    }
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
        if (key == 'r' || key == 'R' || key == 18) {
            return (key == 'R' || shift) ? "saverig" : "userig";
        }
        if (key == 'o' || key == 'O' || key == 15) return "open";
        if (key == 'n' || key == 'N' || key == 14) return "empty";
        if (key == 'e' || key == 'E' || key == 5)  return "example";
        if (key == '3' || key == '#') return "view3d";
        if (key == OF_KEY_BACKSPACE || key == OF_KEY_DEL) return "removelevel";
        return "";
    }
    switch (key) {
    case '1': case '2': case '3': case '4':        return "gridkind";
    case 'e': case 'E': case 'r': case 'R':         return "rotate";
    case '-': case '_': case '=': case '+':         return "spacing";
    case '{': case '}':                             return "spokes";
    case '[': case ']':                             return "layoutsize";
    case ',': case '.':                             return "deadzone";
    case 'l':                                       return "layout";
    case 'L':                                       return "layoutnodes";
    case 'a':                                       return "all";
    case OF_KEY_ESC:                                return "none";
    case OF_KEY_BACKSPACE: case OF_KEY_DEL:         return "remove";
    case 'u':                                       return "unjoin";
    case 'f':                                       return "join";
    case 'F':                                       return "joincells";
    case 'o':                                       return "newoutput";
    case 'd':                                       return "derivedafter";
    case 'D':                                       return "derived";
    case 'b':                                       return "bind";
    case 'B':                                       return "bindall";
    case 'q': case 'Q': case 'w': case 'W':         return "fill";
    case 'c':                                       return "clear";
    case 'C':                                       return "silence";
    case OF_KEY_TAB:                                return "nextoutput";
    case '9': case '(': case '0': case ')':         return "trim";
    case 'O':                                       return "removeoutput";
    case 'p':                                       return "autooutput";
    case ' ':                                       return "audition";
    case 'v':                                       return "lock";
    case OF_KEY_UP: case OF_KEY_DOWN:               return "levelswitch";
    case 'n': case 'N':                             return "newlevel";
    case 'y': case 'Y': case 'h': case 'H':         return "raise";
    case 'i': case 'I': case 'k': case 'K':         return "movelevel";
    case 'm': case 'M':                             return "match";
    case 'g':                                       return "newvalue";
    case 'G':                                       return "removevalue";
    case 'j': case 'J':                             return "pickvalue";
    case ';': case ':': case '\'': case '"':        return "stepvalue";
    case 'z':                                       return "editvalue";
    case '?':                                       return "keysheet";
    default:                                        return "";
    }
}

// The column of dots as the map view draws it, for clicks on it.
DotColumn ofApp::columnNow() const {
    const ofRectangle vp = renderer->viewport();
    const float inset = vp.getHeight() * kColumnInset;
    return dotColumn(model, vp.getY() + inset, vp.getBottom() - inset, false,
                     evaluation, 0.0f);
}

float ofApp::columnX() const {
    return renderer->viewport().getRight() + kColumnOffset;
}

int ofApp::layoutSize() const {
    return kind == GridKind::Polar      ? ringDepth
         : kind == GridKind::Triangular ? triStage
         : blockSize;
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
    ctx.triangles = kind == GridKind::Triangular;
    ctx.spokes = spokes;
    ctx.layoutSize = layoutSize();
    return ctx;
}

bool ofApp::allowed(const std::string& id) {
    std::string why;
    if (keyAvailable(keyContext(), id, &why)) return true;
    if (!why.empty()) show(Result{false, true, why, 2.0f});
    return false;
}

void ofApp::keyPressed(int key) {
    // The typing line takes every key while it is open (3A-7).
    if (typing) {
        const Typed t = typeKey(typingLine, key);
        if (t == Typed::Enter) {
            typing = false;
            const std::string line = typingLine;
            if (typingNew) {
                act("new value", [line](Model& m) { return m.newValue(line); });
            } else {
                act("value", [line](Model& m) { return m.editValue(line); });
            }
        } else if (t == Typed::Escape) {
            typing = false;
            message = typingNew ? "no value made" : "value unchanged";
            messageTime = 2.0f;
        }
        return;
    }
    // The key sheet (3A-10): any key closes it, and does nothing else.
    if (keySheet) {
        if (key != OF_KEY_SHIFT && key != OF_KEY_COMMAND) keySheet = false;
        return;
    }
    // Every key asks the key map first: a dim key does not act, it says why.
    const std::string id = keyId(key);
    // The layout preview shows after [ or ] (or { } on the polar grid, which
    // change the ring) and goes at any other key.
    if (!id.empty()) {
        previewLayout = id == "layoutsize"
                     || (id == "spokes" && kind == GridKind::Polar);
    }
    if (id.empty() || !allowed(id)) return;

    // ---- window ----
    if (id == "fullscreen") { ofToggleFullscreen(); return; }
    if (id == "keysheet") { keySheet = true; return; }
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
    if (id == "userig") { useRig(); return; }
    if (id == "saverig") { saveRig(); return; }
    if (id == "empty") {
        const std::string before = currentPath;
        act("new map", [](Model& m) {
            m.newEmpty();
            return Result{true, false,
                          "new empty map -- click to place nodes", 3.0f};
        });
        pathMarks.push_back({history.undoCount(), before});
        currentPath.clear();
        adopt();
        return;
    }
    if (id == "example") {
        const std::string before = currentPath;
        act("example map", [](Model& m) {
            m.loadExample();
            return Result{true, false, "", 0.0f};
        });
        pathMarks.push_back({history.undoCount(), before});
        currentPath.clear();
        adopt();
        return;
    }

    // ---- grid tool ----
    if (id == "gridkind" || id == "rotate" || id == "spacing" || id == "spokes") {
        // The grid's number again straightens it (3A-11).
        const GridKind was = kind;
        if      (key == '1') kind = GridKind::Free;
        else if (key == '2') kind = GridKind::Square;
        else if (key == '3') kind = GridKind::Triangular;
        else if (key == '4') kind = GridKind::Polar;
        if (key >= '1' && key <= '4' && kind == was && rotation != 0.0f) {
            rotation = 0.0f;
            show(Result{false, false, "grid straightened", 1.5f});
        }
        // e r: rotate back and on, 5 degrees, 1 with shift (3A-11).
        if (key == 'e') rotation -= ofDegToRad(5.0f);
        else if (key == 'r') rotation += ofDegToRad(5.0f);
        else if (key == 'E') rotation -= ofDegToRad(1.0f);
        else if (key == 'R') rotation += ofDegToRad(1.0f);
        // - and =: one more or one fewer across, so the tiles always fit.
        else if (key == '-' || key == '_') across = std::min(50, across + 1);
        else if (key == '=' || key == '+') across = std::max(4, across - 1);
        else if (key == '{') spokes = std::max(3, spokes - 1);
        else if (key == '}') spokes = std::min(24, spokes + 1);
        rebuildGrid();
        return;
    }
    // [ ] set the size of what l lays out, on every grid (3A-9). The panel
    // names it; the message says it too.
    if (id == "layoutsize") {
        const int d = key == '[' ? -1 : 1;
        if (kind == GridKind::Polar) {
            ringDepth = std::min(8, std::max(1, ringDepth + d));
        } else if (kind == GridKind::Triangular) {
            triStage = std::min(kTriangleStages, std::max(1, triStage + d));
        } else {
            blockSize = std::min(8, std::max(2, blockSize + d));
        }
        const std::vector<KeyGroup> keys = keyMap(keyContext());
        const KeyEntry* e = findKey(keys, "layout");
        if (e) show(Result{false, false, "l lays " + e->action.substr(4), 2.0f});
        return;
    }
    // l: a ring or a block in one step, at the grid point nearest the mouse.
    // The new nodes are left selected, so the next key -- y to lift them, m
    // to match, f is not needed -- acts on the whole layout.
    if (id == "layout") {
        std::vector<NodeID> placed;
        const glm::vec2 p = renderer->toManifold(mouse);
        const int size = layoutSize();
        const bool tri = kind == GridKind::Triangular;
        act("lay out", [&](Model& m) {
            return m.layout(p, grid, size, tri, &placed);
        });
        if (!placed.empty()) {
            selection.clear();
            for (NodeID n : placed) selection.toggle(n);
        }
        return;
    }
    // L (3A-11): the same nodes, not joined -- thin them out, then F.
    if (id == "layoutnodes") {
        std::vector<NodeID> placed;
        const glm::vec2 p = renderer->toManifold(mouse);
        const int size = layoutSize();
        const bool tri = kind == GridKind::Triangular;
        act("lay out the nodes", [&](Model& m) {
            return m.layout(p, grid, size, tri, &placed, false);
        });
        if (!placed.empty()) {
            selection.clear();
            for (NodeID n : placed) selection.toggle(n);
        }
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
    if (id == "joincells") {
        act("join the grid cells", [&](Model& m) {
            return m.joinCells(selection, grid, kind == GridKind::Triangular);
        });
        return;
    }

    // ---- outputs ----
    if (id == "newoutput") { act("new output", [](Model& m) { return m.newOutput(); }); return; }
    if (id == "derived") {
        act("derived output", [&](Model& m) { return m.newDerived(selection); });
        return;
    }
    if (id == "derivedafter") {
        act("derived output", [](Model& m) { return m.newDerivedAfter(); });
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
        act("back to own output", [&](Model& m) { return m.clear(selection); });
        return;
    }
    if (id == "silence") {
        act("silence", [&](Model& m) { return m.silence(selection); });
        return;
    }
    if (id == "nextoutput") {
        show(model.cycleOutput(ofGetKeyPressed(OF_KEY_SHIFT) ? -1 : 1));
        return;
    }
    if (id == "trim") {
        // 9 0: down and up a dB; with shift, ( and ), a tenth (3A-11).
        const float dB = key == '9' ? -1.0f : key == '0' ? +1.0f
                       : key == '(' ? -0.1f : +0.1f;
        act("trim", [dB](Model& m) { return m.trimCurrent(dB); });
        return;
    }
    if (id == "removeoutput") {
        act("remove output", [](Model& m) { return m.removeCurrentOutput(); });
        return;
    }
    if (id == "autooutput") { show(model.toggleAutoOutput()); return; }

    // ---- levels (round 3A-6) ----
    if (id == "levelswitch") {
        const int dir = (key == OF_KEY_UP) ? +1 : -1;
        // In audition, up and down move the listening point instead: held,
        // it glides (update()).
        if (auditioning()) { glideDir = dir; return; }
        show(model.switchLevel(dir));
        return;
    }
    if (id == "newlevel") {
        const bool above = (key == 'n');
        act("new level", [above](Model& m) { return m.newLevel(above); });
        return;
    }
    // y up, h down: y sits above h on the keyboard. Shift for fine steps,
    // arriving as the capital, as fill's does.
    if (id == "raise") {
        const int  dir  = (key == 'y' || key == 'Y') ? +1 : -1;
        const bool fine = (key == 'Y' || key == 'H');
        act("height", [&, dir, fine](Model& m) {
            return m.raise(selection, dir, fine); });
        return;
    }
    // ---- node values (round 3A-7) ----
    if (id == "newvalue") {
        typing = true;
        typingNew = true;
        typingLine.clear();
        return;
    }
    if (id == "editvalue") {
        if (model.currentValue() == Model::NoValue) {
            show(model.editValue(""));       // says there are none yet
            return;
        }
        typing = true;
        typingNew = false;
        typingLine = model.valueLine();
        return;
    }
    if (id == "pickvalue") {
        show(model.pickValue(key == 'J' ? -1 : +1));
        return;
    }
    // ; down, ' up, side by side; shift (: and ") for fine steps.
    if (id == "stepvalue") {
        const int  dir  = (key == ';' || key == ':') ? -1 : +1;
        const bool fine = (key == ':' || key == '"');
        act("value", [&, dir, fine](Model& m) {
            return m.stepValue(selection, dir, fine); });
        return;
    }
    if (id == "removevalue") {
        act("remove value", [](Model& m) { return m.removeValue(); });
        return;
    }

    // m: the selected nodes to the height of the one selected first; M:
    // back to the level's start.
    if (id == "match") {
        if (key == 'm') {
            act("height", [&](Model& m) { return m.matchHeights(selection); });
        } else {
            act("height", [&](Model& m) { return m.flattenHeights(selection); });
        }
        return;
    }
    // i up, k down, for the whole level: i sits above k. Shift for fine.
    if (id == "movelevel") {
        const int  dir  = (key == 'i' || key == 'I') ? +1 : -1;
        const bool fine = (key == 'I' || key == 'K');
        act("move level", [dir, fine](Model& m) {
            return m.stepLevel(dir, fine); });
        return;
    }
    if (id == "removelevel") {
        act("remove level", [](Model& m) { return m.removeLevel(); });
        return;
    }
    if (id == "view3d") {
        show3D = !show3D;
        if (show3D) cam.enableMouseInput();
        else        cam.disableMouseInput();
        message = show3D ? "3D view on -- drag in it to turn it; cmd-3 to hide"
                         : "3D view off";
        messageTime = 2.5f;
        return;
    }

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
    if (key == OF_KEY_UP || key == OF_KEY_DOWN) glideDir = 0;
}

// ===========================================================================
// grid tool -- a preference of the session, never saved into a map
// ===========================================================================

// Rounds 3A-11, 3A-12: the grid fits the map. The spacing is 1 / across, so
// a square grid's tiles meet the edges exactly. The triangular grid's rows
// are 1 / rows apart, rows the whole number nearest across / 0.866 -- so its
// triangles are within a few hundredths of equilateral -- and every other
// row starts half a step in: straight, the grid meets every edge, the sides
// cutting only through whole half-triangles. A grid turns about its point
// nearest the middle. The polar grid's rings step out from the centre.
void ofApp::rebuildGrid() {
    const float s = 1.0f / float(across);
    spacing = s;
    const float mid = float(across / 2) * s;     // a grid point near 0.5
    switch (kind) {
    case GridKind::Free:
        grid = Grid::free();
        break;
    case GridKind::Square:
        grid = Grid::lattice({s, 0.0f}, {0.0f, s}, {mid, mid}, rotation);
        break;
    case GridKind::Triangular: {
        const int rows = std::max(1, int(std::lround(float(across) / 0.8660254f)));
        const float h = 1.0f / float(rows);
        const int jc = rows / 2;                 // the row the grid turns on
        // Row j's points are at i / across + j / (2 across).
        const int i0 = (across - jc) / 2;
        const float ox = float(i0) * s + float(jc) * 0.5f * s;
        grid = Grid::lattice({s, 0.0f}, {s * 0.5f, h},
                             {ox, float(jc) * h}, rotation);
        break;
    }
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

// Using a rig is one undo step, like any edit. The rig file stays as it was.
void ofApp::useRig() {
    ofFileDialogResult d =
        ofSystemLoadDialog("Use a rig -- its outputs replace this map's");
    if (!d.bSuccess) return;
    const std::string path = d.getPath();
    act("use rig", [path](Model& m) { return useRigFile(m, path); });
}

void ofApp::saveRig() {
    const std::string suggested = currentPath.empty()
        ? std::string("room") : pairFor(currentPath).name;
    ofFileDialogResult d =
        ofSystemSaveDialog(suggested + "-rig.json", "Save the outputs as a rig");
    if (!d.bSuccess) return;
    show(saveRigFile(model, d.getPath()));
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
