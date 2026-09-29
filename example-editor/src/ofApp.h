#pragma once

// ofxManifold — example-editor.
//
// A thin app over ofxManifoldEditor. It owns the window, the mouse and the
// keys, and draws; every change to the map or its outputs goes through
// editor::Model, the ONE DOOR (PLAN-editor.md decision D).
//
// That rule is enforced, not just intended: this class holds the map and its
// outputs only as CONST references for drawing. A line here that tried to
// change them directly would not compile.
//
// The code falls into sections that become separate files in round 2:
//
//   app          setup, update, draw -- assembling the pieces
//   controller   mouse and keys, turned into Model operations
//   grid tool    the snapping grid, a preference of the session
//   MapView      grid lines, regions, the drag box, selection rings
//   NodeGlyph    one node's shape, fill and audition halo
//   OutputChart  the faders, drawn from editor::ChartLayout
//   KeyPanel     the side panel and help
//   EditorFiles  saving and opening the map and its outputs
//
// Each view section only READS the model.

#include "ofMain.h"
#include "ofxManifold.h"
#include "ofxManifoldEditor.h"

class ofApp : public ofBaseApp {
public:
    // ---- app --------------------------------------------------------------
    void setup() override;
    void update() override;
    void draw() override;

    // ---- controller -------------------------------------------------------
    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void keyPressed(int key) override;
    void keyReleased(int key) override;

private:
    // ---- controller -------------------------------------------------------
    void show(const ofxManifold::editor::Result& r);  // a Result on screen

    // Run a Model operation as one undoable step: recorded under `label` if
    // it changed anything, and its Result shown. Every edit comes through
    // here, so undo sees every edit.
    void act(const std::string& label,
             const std::function<ofxManifold::editor::Result(
                 ofxManifold::editor::Model&)>& op);
    void undo();
    void redo();
    void adopt();                     // after the map is replaced wholesale
    void placeAt(glm::vec2 p);
    ofxManifold::NodeID nodeAt(glm::vec2 screen) const;
    bool auditioning() const { return auditionHeld || auditionLocked; }

    // ---- grid tool --------------------------------------------------------
    enum class GridKind { Free, Square, Triangular, Polar };
    void rebuildGrid();

    // ---- MapView ----------------------------------------------------------
    void drawGrid() const;
    void drawSegment(glm::vec2 a, glm::vec2 b) const;

    // ---- NodeGlyph --------------------------------------------------------
    void  drawNode(ofxManifold::NodeID id) const;
    void  drawHalo(ofxManifold::NodeID id, float weight) const;
    float nodeRadius(ofxManifold::NodeID id) const;

    // ---- OutputChart ------------------------------------------------------
    void drawChart() const;

    // ---- KeyPanel ---------------------------------------------------------
    void drawPanel() const;

    // ---- EditorFiles ------------------------------------------------------
    void save();
    void load();

    // ---- state: the model, and read-only views of it ----------------------
    //
    // The model and the selection own all editing state. The references below
    // are CONST: drawing and hit-testing read through them, and nothing here
    // can change the map, its outputs, or the current output except through a
    // Model operation. Declared after the model, so they bind to it.
    ofxManifold::editor::Model     model;
    ofxManifold::editor::Selection selection;
    ofxManifold::editor::History   history;
    const ofxManifold::Manifold2D& manifold      = model.manifold();
    const ofxManifold::Mapping&    mapping       = model.mapping();
    const ofxManifold::TargetID&   currentOutput = model.currentOutput();
    const bool&                    autoOutput    = model.autoOutput();

    // ---- state: drawing and listening --------------------------------------
    std::unique_ptr<ofxManifoldRenderer>    renderer;
    std::unique_ptr<ofxManifold::Evaluator> evaluator;
    ofxManifold::Evaluation                 evaluation;
    ofxManifold::editor::ChartLayout        chart;
    float chartTop = 0.0f;

    // Audition: dragging moves the listening point and can NEVER move a node.
    glm::vec2 auditionPoint{0.5f, 0.5f};
    bool      auditionHeld   = false;
    bool      auditionLocked = false;
    bool      pressAudition  = false;   // this press began in audition
    bool      pressInChart   = false;   // this press began on the chart

    // ---- state: the grid tool -----------------------------------------------
    //
    // A preference of the editing session, never written into a map file.
    ofxManifold::Grid grid;
    GridKind kind     = GridKind::Square;
    float    spacing  = 0.05f;
    float    rotation = 0.0f;           // radians
    int      spokes   = 8;
    float    deadZone = 0.25f;          // fraction of grid spacing

    // ---- state: an in-progress drag or press -------------------------------
    bool                     dragging = false;
    ofxManifold::NodeID      anchor   = ofxManifold::InvalidNode;
    glm::vec2                grabOffset{0.0f, 0.0f};
    std::vector<glm::vec2>   dragFrom;         // per selected node
    ofxManifold::GridAddress anchorAddr;
    bool      pressingEmpty = false;           // a click places, a drag boxes
    bool      pressHadSelection = false;       // ...unless it only deselects
    ofxManifold::editor::Model dragBefore;     // the state a drag began from
    bool      dragMoved = false;               // did any part of it land
    glm::vec2 pressScreen{0.0f, 0.0f};
    glm::vec2 boxScreen{0.0f, 0.0f};

    // ---- state: the message line -------------------------------------------
    float       refusedFlash = 0.0f;
    std::string message;
    float       messageTime = 0.0f;
};
