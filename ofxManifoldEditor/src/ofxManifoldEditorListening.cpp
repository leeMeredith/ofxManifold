#include "ofxManifoldEditorListening.h"

#include <cmath>

namespace ofxManifold {
namespace editor {

float listenStart(const Model& model, const Evaluation& floor) {
    const Levels& lv = model.map3d().levels();
    const LevelID k = model.currentLevel();
    return floor.inside ? lv.heightAt(k, floor.weights) : lv.base(k);
}

LevelID levelReached(const Model& model, const Evaluation& floor, float z) {
    const Levels& lv = model.map3d().levels();
    const std::size_t n = lv.count();
    if (n < 2) return 0;
    const float u = lv.positionAt(floor.inside ? floor.weights
                                               : std::vector<WeightedNode>{}, z);
    const float at = std::floor(u + 1e-6f);
    if (!(at > 0.0f)) return 0;
    return std::min(LevelID(at), LevelID(n - 1));
}

float glide(float z, int direction, bool fine, float seconds) {
    return z + float(direction)
             * (fine ? kGlidePerSecondFine : kGlidePerSecond) * seconds;
}

ChartLevels chartLevels3D(const Model& model, const Selection& sel,
                          const Evaluation& floor, float z) {
    const Map3D& map = model.map3d();
    // One level: the 2D reading itself, not a copy of it.
    if (map.count() == 1) return chartLevels(model, sel, floor);
    return chartLevels3D(model, sel, floor, map.levels().evaluate(floor, z));
}

ChartLevels chartLevels3D(const Model& model, const Selection& sel,
                          const Evaluation& floor, const Evaluation3D& e) {
    const Map3D& map = model.map3d();
    if (map.count() == 1) return chartLevels(model, sel, floor);

    ChartLevels lv;
    lv.inside = e.inside;
    lv.routed = map.toChannels(e);
    lv.levels = map.toChannelLevels(e);
    lv.silence = map.silenceShare(e);
    if (sel.empty()) return lv;

    // The selected nodes' part, each read on the level its weight is on.
    lv.fromSelection.assign(map.toChannels(Evaluation3D{}).size(), 0.0f);
    for (const LevelWeight& w : e.weights) {
        if (!sel.contains(w.node)) continue;
        const Mapping& mp = map.level(w.level);
        float total = 0.0f;
        for (std::size_t k = 0; k < mp.linkCount(w.node); ++k) {
            total += mp.link(w.node, k).weight;
        }
        if (total <= 0.0f) continue;
        for (std::size_t k = 0; k < mp.linkCount(w.node); ++k) {
            const Link& l = mp.link(w.node, k);
            if (l.kind != DestKind::Output) continue;
            const int ch = mp.targetChannel(l.id);
            if (ch >= 0 && std::size_t(ch) < lv.fromSelection.size()) {
                lv.fromSelection[std::size_t(ch)] += w.weight * l.weight / total;
            }
        }
    }
    return lv;
}

DotColumn dotColumn(const Model& model, float top, float bottom,
                    bool listening, const Evaluation& floor, float z) {
    const Levels& lv = model.map3d().levels();
    if (lv.count() < 2 || !listening || !floor.inside) {
        return dotColumn(model, top, bottom, listening, floor, Evaluation3D{},
                         false);
    }
    return dotColumn(model, top, bottom, listening, floor,
                     lv.evaluate(floor, z),
                     z > lv.heightAt(lv.count() - 1, floor.weights));
}

DotColumn dotColumn(const Model& model, float top, float bottom,
                    bool listening, const Evaluation& floor,
                    const Evaluation3D& e, bool above) {
    DotColumn c;
    const Map3D& map = model.map3d();
    const std::size_t n = map.count();
    if (n < 2) return c;
    c.shown = true;
    c.current = model.currentLevel();
    const float gap = (bottom - top) / float(n - 1);
    for (std::size_t k = 0; k < n; ++k) c.dots.push_back(bottom - float(k) * gap);
    if (!listening || !floor.inside) return c;

    c.marked = true;
    if (e.inside) {
        const float a = c.dots[e.below], b = c.dots[e.below + 1];
        c.mark = a + e.t * (b - a);
        return c;
    }
    c.outside = true;
    c.mark = above ? c.dots[n - 1] - gap * 0.5f : c.dots[0] + gap * 0.5f;
    return c;
}

std::vector<std::pair<std::string, float>>
valuesAt(const Model& model, const Evaluation& floor, float z) {
    const Map3D& map = model.map3d();
    return valuesAt(model, floor.inside ? map.levels().evaluate(floor, z)
                                        : Evaluation3D{});
}

std::vector<std::pair<std::string, float>>
valuesAt(const Model& model, const Evaluation3D& e) {
    const Map3D& map = model.map3d();
    std::vector<std::pair<std::string, float>> out;
    for (std::size_t v = 0; v < map.valueSet().size(); ++v) {
        out.emplace_back(map.valueSet()[v].name, map.valueAt(v, e));
    }
    return out;
}

int dotAt(const DotColumn& c, float columnX, float x, float y) {
    if (!c.shown || std::fabs(x - columnX) > 8.0f) return -1;
    int hit = -1;
    for (std::size_t k = 0; k < c.dots.size(); ++k) {
        if (std::fabs(y - c.dots[k]) <= 8.0f) hit = int(k);
    }
    return hit;
}

float levelDragTo(const Model& model, LevelID k, float pixels, float gap) {
    const Levels& lv = model.map3d().levels();
    const float base = lv.base(k);
    float nb = std::round((base - pixels / gap * 0.25f) / 0.01f) * 0.01f;
    // Stopped AT a neighbour, touching (3B-2d) -- or a step short of it,
    // where the touch, in float, would not land: letting go always lands.
    const std::size_t nodes = model.manifold().nodeCount();
    const auto room = lv.room(k, nodes);
    auto lands = [&](float at) {
        Levels trial = lv;
        return trial.moveLevel(k, at - base, nodes);
    };
    if (nb >= base + room.second) {
        nb = base + room.second;
        if (!lands(nb)) nb -= 0.01f;
    }
    if (nb <= base - room.first) {
        nb = base - room.first;
        if (!lands(nb)) nb += 0.01f;
    }
    return nb;
}

} // namespace editor
} // namespace ofxManifold
