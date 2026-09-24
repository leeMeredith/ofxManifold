#pragma once

// ofxManifold — example-editor.
//
// The first piece of the editor: placing, selecting and moving nodes, on a
// grid or free-hand.
//
//   click empty space        place a node, snapped to the active grid
//   drag across empty space  box-select (shift adds to the selection)
//   click a node             select it
//   shift-click a node       add it to, or remove it from, the selection
//   drag a selected node     move the whole selection as ONE operation
//   hold alt while dragging  free-hand for that drag, whatever the grid
//
// Every move goes through Manifold2D::setNodePositions(), all or nothing,
// checked against the FINAL shape of every region it touches. A move that
// would invert, flatten or self-intersect a region is refused and nothing
// moves -- shown as a red flash rather than silence.
//
// The quad in the starting map is there on purpose. Drag one of its corners
// across the opposite edge: the ring would cross itself while keeping its
// winding sign, which the kernel used to accept (DECISIONS.md D-019).
//
// Grid settings live here, in the editing session, and are never written into
// a manifold file: which grid an author used is a tool preference, not part of
// a map meant to travel between venues.

#include "ofMain.h"
#include "ofxManifold.h"

class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void mousePressed(int x, int y, int button) override;
    void mouseDragged(int x, int y, int button) override;
    void mouseReleased(int x, int y, int button) override;
    void keyPressed(int key) override;

private:
    enum class GridKind { Free, Square, Triangular, Polar };

    void buildStartingMap();
    void rebuildGrid();
    void drawGrid() const;
    void drawSegment(glm::vec2 a, glm::vec2 b) const;
    void joinSelection();
    void save();
    void load();
    void removeSelected();
    void unjoinSelected();
    void newEmptyMap();
    void adopt();                       // after the manifold is replaced
    std::string freshName() const;
    void placeNode(glm::vec2 p);
    bool isSelected(ofxManifold::NodeID id) const;
    ofxManifold::NodeID nodeAt(glm::vec2 screen) const;

    // Duplicate detection: by grid address when the grid has addresses, by
    // distance in free mode, which has none (decision F).
    bool occupied(glm::vec2 p, ofxManifold::GridAddress addr) const;

    ofxManifold::Manifold2D                 manifold;
    std::unique_ptr<ofxManifoldRenderer>    renderer;
    ofxManifold::Grid                       grid;

    GridKind kind       = GridKind::Square;
    float    spacing    = 0.05f;
    float    rotation   = 0.0f;     // radians
    int      spokes     = 8;
    float    deadZone   = 0.25f;    // fraction of grid spacing

    // selection, in the order nodes were chosen
    std::vector<ofxManifold::NodeID> selected;

    // True while every node in the selection was chosen by an individual
    // click, so the order is the author's. A box select has no order, and
    // clears this. Decides how joinSelection() orders the ring.
    bool selectionOrdered = true;

    // an in-progress drag of the selection
    bool                                   dragging = false;
    ofxManifold::NodeID                    anchor   = ofxManifold::InvalidNode;
    glm::vec2                              grabOffset{0.0f, 0.0f};
    std::vector<glm::vec2>                 dragFrom;   // per selected node
    ofxManifold::GridAddress               anchorAddr;

    // an in-progress press on empty space: a click places, a drag boxes
    bool      pressingEmpty = false;
    glm::vec2 pressScreen{0.0f, 0.0f};
    glm::vec2 boxScreen{0.0f, 0.0f};

    // Refreshed only when the node or region list changes. Calling
    // validate() every frame is the cost D-015 measured and removed from
    // example-basic -- O(regions x 3 x nodes), a dropped frame at a thousand
    // nodes. Moving nodes cannot change the orphan count, so a drag need not
    // refresh it.
    ofxManifold::TopologyReport topology;

    float       refusedFlash = 0.0f;
    std::string message;
    float       messageTime = 0.0f;
};
