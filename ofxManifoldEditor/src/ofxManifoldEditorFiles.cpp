#include "ofxManifoldEditorFiles.h"

#include "io/ofxManifoldSerialize.h"
#include "io/ofxManifoldSerialize3D.h"

#include <fstream>
#include <sstream>

namespace ofxManifold {
namespace editor {

namespace {

bool endsWith(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size()
        && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return bool(f);
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + (n == 1 ? one : many);
}

} // namespace

FilePair pairFor(const std::string& chosen) {
    std::string b = chosen;
    for (const char* tail : {"-outputs.json", "-mapping.json", ".json"}) {
        if (endsWith(b, tail)) {
            b.resize(b.size() - std::string(tail).size());
            break;
        }
    }
    FilePair p;
    p.base = b;
    p.map = b + ".json";
    p.outputs = b + "-outputs.json";
    p.legacy = b + "-mapping.json";
    const std::size_t slash = b.find_last_of("/\\");
    p.name = (slash == std::string::npos) ? b : b.substr(slash + 1);
    return p;
}

Result savePair(const Model& model, const std::string& chosen) {
    const FilePair p = pairFor(chosen);
    Result r;
    r.seconds = 3.0f;
    // A map with one flat level and no values writes exactly what the 2D
    // writers do (D-022, checked by vector); anything more is version 3.
    if (!writeFile(p.map, io::saveManifold3D(model.manifold(),
                                             model.map3d()))) {
        r.refused = true;
        r.message = "cannot write " + p.name + ".json";
        return r;
    }
    if (!writeFile(p.outputs,
                   io::saveMapping3D(model.map3d(), model.manifold()))) {
        r.refused = true;
        r.message = "saved " + p.name + ".json, but cannot write "
                  + p.name + "-outputs.json";
        return r;
    }
    r.message = "saved " + p.name + ".json and " + p.name + "-outputs.json";
    return r;
}

RigFile rigFor(const std::string& chosen) {
    std::string b = chosen;
    for (const char* tail : {"-rig.json", ".json"}) {
        if (endsWith(b, tail)) {
            b.resize(b.size() - std::string(tail).size());
            break;
        }
    }
    RigFile f;
    f.path = b + "-rig.json";
    const std::size_t slash = b.find_last_of("/\\");
    f.name = (slash == std::string::npos) ? b : b.substr(slash + 1);
    return f;
}

Result saveRigFile(const Model& model, const std::string& chosen) {
    const RigFile f = rigFor(chosen);
    Result r;
    r.seconds = 3.0f;
    if (!writeFile(f.path, io::saveRig(model.mapping()))) {
        r.refused = true;
        r.message = "cannot write " + f.name + "-rig.json";
        return r;
    }
    r.message = "saved rig " + f.name + "-rig.json";
    return r;
}

Result useRigFile(Model& model, const std::string& chosen) {
    const RigFile f = rigFor(chosen);
    std::string text;
    if (!readFile(f.path, text)) {
        Result r;
        r.refused = true;
        r.seconds = 3.0f;
        r.message = "cannot open " + f.name + "-rig.json";
        return r;
    }
    Mapping rig;
    const io::LoadResult lr = io::loadRig(text, rig);
    if (!lr.ok) {
        Result r;
        r.refused = true;
        r.seconds = 5.0f;
        r.message = "rig refused: " + lr.error;
        return r;
    }
    return model.useRig(rig, f.name);
}

Result openPair(Model& model, const std::string& chosen) {
    const FilePair p = pairFor(chosen);
    Result r;
    r.seconds = 3.0f;

    std::string text;
    if (!readFile(p.map, text)) {
        r.refused = true;
        r.message = "cannot open " + p.name + ".json";
        return r;
    }
    // The map file alone first, with no outputs: its floor plan and levels.
    // A refusal here is the map file's own.
    const std::string noOutputs = io::saveMapping(Mapping(), Manifold2D());
    Manifold2D m;
    Map3D map;
    {
        const io::LoadResult lr = io::loadMap3D(text, noOutputs, m, map);
        if (!lr.ok) {
            r.refused = true;
            r.message = "open failed: " + lr.error;
            return r;
        }
    }

    // The outputs refer to nodes by NAME, so they are read against the map
    // just loaded. The current name first, then the one earlier editors used.
    // Refused, the map is kept with no outputs, and the reason is shown.
    std::string note;
    std::string otext;
    if (readFile(p.outputs, otext) || readFile(p.legacy, otext)) {
        Manifold2D m2;
        Map3D withOutputs;
        const io::LoadResult mr = io::loadMap3D(text, otext, m2, withOutputs);
        if (mr.ok) {
            m = std::move(m2);
            map = std::move(withOutputs);
        } else {
            note = " -- outputs file refused: " + mr.error;
            r.refused = true;
            r.seconds = 6.0f;
        }
    } else {
        note = " (no outputs file)";
    }

    const std::size_t nodes = m.nodeCount();
    const std::size_t regions = m.regionCount();
    const std::size_t outs = map.level(0).targetCount();
    const std::size_t levels = map.count();
    model.adopt(std::move(m), std::move(map));
    r.changed = true;
    r.message = "opened " + p.name + ": " + plural(nodes, " node", " nodes")
              + ", " + plural(regions, " region", " regions") + ", "
              + plural(outs, " output", " outputs")
              + (levels > 1 ? ", " + std::to_string(levels) + " levels"
                            : std::string())
              + note;
    return r;
}

} // namespace editor
} // namespace ofxManifold
