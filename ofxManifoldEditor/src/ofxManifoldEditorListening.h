#pragma once

// ofxManifoldEditor — listening in three dimensions, and the column of dots
// (PLAN-round3.md, round 3A-6).
//
// Audition moves through the 3D map. It starts on the level being edited, at
// that level's height under the point; holding up or down glides the point
// through the space between levels and on past them, where it is outside and
// silent, as off the edge of the map. These are the pure parts: where the
// point starts, how far a held key moves it, what the fader chart is handed
// there, and where the column of dots puts each level and the listening mark.
// The app holds the point and draws; everything it draws from is here, so it
// is tested by script like the model.
//
// No openFrameworks.

#include "ofxManifoldEditorChartLayout.h"
#include "ofxManifoldEditorModel.h"
#include "ofxManifoldEditorSelection.h"

#include <string>
#include <utility>
#include <vector>

namespace ofxManifold {
namespace editor {

// How fast a held key moves the listening point, in map units a second.
constexpr float kGlidePerSecond     = 0.25f;
constexpr float kGlidePerSecondFine = 0.05f;   // with shift

// Where audition starts: the CURRENT level's height under the point, so what
// is heard first is the level being edited. Off the floor plan, that level's
// starting height.
float listenStart(const Model& model, const Evaluation& floor);

// The listening point's height after holding up (+1) or down (-1) for
// `seconds`. Nothing special happens at a level: the point glides through.
float glide(float z, int direction, bool fine, float seconds);

// What the fader chart is handed with the listening point at the floor
// plan's evaluation and height z: the 3D map's channels, levels and silence
// there, and the selected nodes' part, from every level they are heard on.
// With one level, exactly chartLevels(): z changes nothing.
ChartLevels chartLevels3D(const Model& model, const Selection& sel,
                          const Evaluation& floor, float z);

// The column of dots at the side of the plan view: one dot per level, the
// top level at the top, EVENLY spaced -- not by height, so two levels close
// together stay two readable dots. Shown only with two or more levels.
//
// The listening mark sits between the two dots whose levels the point lies
// between, as far along as it is between those levels (the t of the 3D
// weights). Outside -- above the top level or below the bottom one -- it
// sits half a space past the end dot, and `outside` says to grey it.
struct DotColumn {
    bool               shown   = false;   // two or more levels
    std::vector<float> dots;              // y of each level, bottom first
    LevelID            current = 0;       // the filled dot
    bool               marked  = false;   // listening, on the floor plan
    float              mark    = 0.0f;
    bool               outside = false;
};

DotColumn dotColumn(const Model& model, float top, float bottom,
                    bool listening, const Evaluation& floor, float z);

// Every node value at the listening point (round 3A-7), in the order the
// values were made: read through the 3D weights, so it blends as the point
// glides between levels; off the map or outside the levels, each value's
// fallback.
std::vector<std::pair<std::string, float>>
valuesAt(const Model& model, const Evaluation& floor, float z);

// Where the column sits in a map viewport: this far in from its top and its
// bottom, as a fraction of its height; 15 pixels right of it. The view draws
// it and the app hit-tests it from these, so they cannot drift apart.
constexpr float kColumnInset = 0.15f;
constexpr float kColumnOffset = 15.0f;

// Which level's dot a click at (x, y) is on -- within 8 pixels of it, in the
// column at `columnX` -- or -1.
int dotAt(const DotColumn& c, float columnX, float x, float y);

// Where dragging level k's dot by `pixels` (down positive) would put the
// level's start, with `gap` pixels between neighbouring dots: a gap is a
// quarter of the map, in steps of 0.01, stopping 0.01 short of where a node
// or its start would reach a neighbour -- so letting go always lands.
float levelDragTo(const Model& model, LevelID k, float pixels, float gap);

} // namespace editor
} // namespace ofxManifold
