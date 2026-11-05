#pragma once

// ofxManifoldEditor — saving and opening a map with its outputs, as a pair.
//
// A map and its outputs are separate files on purpose: the map is portable,
// the outputs belong to one installation, and touring is keeping the map and
// changing the outputs. So a save writes two files side by side:
//
//     stage-left.json            the map
//     stage-left-outputs.json    its outputs
//
// Choosing EITHER file, or the bare name, opens the pair -- opening the
// outputs on their own is not something an editor should let happen by
// accident. Files written by the editor before this existed are named
// "-mapping.json"; opening a map looks for that name when there is no
// "-outputs.json", so earlier saves still open.
//
// Plain C++ file I/O, no openFrameworks, tested with vectors. The native
// save and open dialogs are the app's; everything after the path is here.

#include "ofxManifoldEditorModel.h"

#include <string>

namespace ofxManifold {
namespace editor {

struct FilePair {
    std::string base;      // the chosen path without its suffix
    std::string map;       // base + ".json"
    std::string outputs;   // base + "-outputs.json"
    std::string legacy;    // base + "-mapping.json", from earlier editors
    std::string name;      // base without its directories, for messages
};

// The pair a chosen path belongs to: "stage-left.json",
// "stage-left-outputs.json", "stage-left-mapping.json" and "stage-left" all
// name the same pair.
FilePair pairFor(const std::string& chosen);

// Write both files. The model is only read.
Result savePair(const Model& model, const std::string& chosen);

// Read the pair and hand it to the model through Model::adopt -- through the
// door, like every other change. A map that cannot be read changes nothing.
// A missing outputs file opens the map with no outputs; an outputs file that
// is refused opens the map, says why, and flags the result as refused.
Result openPair(Model& model, const std::string& chosen);

// ---- rigs (PLAN-round3.md decision B) ----------------------------------------
//
// A rig is one file, the room: "stage-rig.json". Choosing "stage",
// "stage.json" or "stage-rig.json" all name it.
struct RigFile {
    std::string path;      // base + "-rig.json"
    std::string name;      // base without its directories, for messages
};
RigFile rigFor(const std::string& chosen);

// Write the map's outputs as a rig. The model is only read.
Result saveRigFile(const Model& model, const std::string& chosen);

// Read a rig and move the map onto it, through Model::useRig. A rig that
// cannot be read, or that the map does not fit, changes nothing.
Result useRigFile(Model& model, const std::string& chosen);

} // namespace editor
} // namespace ofxManifold
