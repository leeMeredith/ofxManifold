#include "ofxManifoldEditorFiles.h"

#include "io/ofxManifoldSerialize.h"

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
    if (!writeFile(p.map, io::saveManifold(model.manifold()))) {
        r.refused = true;
        r.message = "cannot write " + p.name + ".json";
        return r;
    }
    if (!writeFile(p.outputs,
                   io::saveMapping(model.mapping(), model.manifold()))) {
        r.refused = true;
        r.message = "saved " + p.name + ".json, but cannot write "
                  + p.name + "-outputs.json";
        return r;
    }
    r.message = "saved " + p.name + ".json and " + p.name + "-outputs.json";
    return r;
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
    Manifold2D m;
    const io::LoadResult lr = io::loadManifold(text, m);
    if (!lr.ok) {
        r.refused = true;
        r.message = "open failed: " + lr.error;
        return r;
    }

    // The outputs refer to nodes by NAME, so they are read against the map
    // just loaded. The current name first, then the one earlier editors used.
    Mapping mp;
    std::string note;
    std::string otext;
    if (readFile(p.outputs, otext) || readFile(p.legacy, otext)) {
        const io::LoadResult mr = io::loadMapping(otext, m, mp);
        if (!mr.ok) {
            mp = Mapping();
            note = " -- outputs file refused: " + mr.error;
            r.refused = true;
            r.seconds = 6.0f;
        }
    } else {
        note = " (no outputs file)";
    }

    const std::size_t nodes = m.nodeCount();
    const std::size_t regions = m.regionCount();
    const std::size_t outs = mp.targetCount();
    model.adopt(std::move(m), std::move(mp));
    r.changed = true;
    r.message = "opened " + p.name + ": " + plural(nodes, " node", " nodes")
              + ", " + plural(regions, " region", " regions") + ", "
              + plural(outs, " output", " outputs") + note;
    return r;
}

} // namespace editor
} // namespace ofxManifold
