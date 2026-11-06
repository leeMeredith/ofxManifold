#include "ofxManifoldEditorListening.h"

#include <cmath>

namespace ofxManifold {
namespace editor {

float listenStart(const Model& model, const Evaluation& floor) {
    const Levels& lv = model.map3d().levels();
    const LevelID k = model.currentLevel();
    return floor.inside ? lv.heightAt(k, floor.weights) : lv.base(k);
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

    const Evaluation3D e = map.levels().evaluate(floor, z);
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
    const Evaluation3D e = map.levels().evaluate(floor, z);
    if (e.inside) {
        const float a = c.dots[e.below], b = c.dots[e.below + 1];
        c.mark = a + e.t * (b - a);
        return c;
    }
    c.outside = true;
    const bool above = z > map.levels().heightAt(n - 1, floor.weights);
    c.mark = above ? c.dots[n - 1] - gap * 0.5f : c.dots[0] + gap * 0.5f;
    return c;
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
    const auto room = lv.room(k, model.manifold().nodeCount());
    if (nb >= base + room.second) nb = base + room.second - 0.01f;
    if (nb <= base - room.first)  nb = base - room.first + 0.01f;
    return nb;
}

} // namespace editor
} // namespace ofxManifold
