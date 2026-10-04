#include "ofxManifoldEditorChartLayout.h"

#include <algorithm>

namespace ofxManifold {
namespace editor {

ChartLayout layoutChart(const Mapping& mapping, float x0, float top,
                        float width, float height) {
    ChartLayout L;
    L.x0 = x0;
    L.top = top;
    L.width = width;
    L.height = height;
    L.barArea = height - 30.0f;
    L.base = top + L.barArea;
    L.gap = 8.0f;
    L.empty = (mapping.targetCount() == 0 && mapping.aggregators().empty());
    if (L.empty) return L;

    // The channel vector's length: highest channel + 1. Read from the mapping
    // with no weights, since only its length matters here.
    const std::size_t channels = mapping.toChannels(WeightVector{}).size();
    const std::size_t slots = channels + 1;                  // + silence
    L.barWidth = std::min(46.0f,
        (width - L.gap * float(slots + 1))
            / float(std::max<std::size_t>(slots, 1)));

    for (std::size_t ch = 0; ch < channels; ++ch) {
        ChartBar b;
        b.channel = int(ch);
        b.x = x0 + L.gap + float(ch) * (L.barWidth + L.gap);
        for (TargetID t = 0; t < mapping.targetCount(); ++t) {
            if (mapping.targetChannel(t) == int(ch)) {
                b.kind = ChartBar::Kind::Output;
                b.output = t;
                b.name = mapping.targetName(t);
            }
        }
        if (b.kind == ChartBar::Kind::Unused) {
            for (const auto& a : mapping.aggregators()) {
                if (a.channel == int(ch)) {
                    b.kind = ChartBar::Kind::Derived;
                    b.name = a.name;
                }
            }
        }
        L.bars.push_back(b);
    }
    L.silenceX = x0 + L.gap + float(channels) * (L.barWidth + L.gap) + L.gap;
    L.totalX = L.silenceX + L.barWidth + 16.0f;
    return L;
}

std::vector<FillMark> fillMarks(const Model& model, const Selection& sel) {
    std::vector<FillMark> marks;
    const Mapping& mp = model.mapping();
    for (NodeID id : sel.nodes()) {
        const float fill = mp.outputFraction(id);
        for (std::size_t k = 0; k < mp.linkCount(id); ++k) {
            const Link& l = mp.link(id, k);
            if (l.kind == DestKind::Output) marks.push_back({l.id, id, fill});
        }
    }
    return marks;
}

std::vector<float> contributions(const Model& model, const Selection& sel,
                                 const WeightVector& weights) {
    const Mapping& mp = model.mapping();
    std::vector<float> out(mp.toChannels(WeightVector{}).size(), 0.0f);
    for (const auto& wn : weights) {
        if (!sel.contains(wn.id)) continue;
        float total = 0.0f;
        for (std::size_t k = 0; k < mp.linkCount(wn.id); ++k) {
            total += mp.link(wn.id, k).weight;
        }
        if (total <= 0.0f) continue;
        for (std::size_t k = 0; k < mp.linkCount(wn.id); ++k) {
            const Link& l = mp.link(wn.id, k);
            if (l.kind != DestKind::Output) continue;
            const int ch = mp.targetChannel(l.id);
            if (ch >= 0 && std::size_t(ch) < out.size()) {
                out[std::size_t(ch)] += wn.weight * l.weight / total;
            }
        }
    }
    return out;
}

std::vector<std::size_t> tapsOf(const Model& model, const Selection& sel) {
    std::vector<std::size_t> taps;
    const auto& ags = model.mapping().aggregators();
    for (std::size_t a = 0; a < ags.size(); ++a) {
        for (NodeID src : ags[a].sources) {
            if (sel.contains(src)) { taps.push_back(a); break; }
        }
    }
    return taps;
}

TargetID ChartLayout::outputAt(float x, float y) const {
    for (const ChartBar& b : bars) {
        if (b.kind != ChartBar::Kind::Output) continue;
        if (x >= b.x && x <= b.x + barWidth && y >= top && y <= top + height) {
            return b.output;
        }
    }
    return InvalidTarget;
}

} // namespace editor
} // namespace ofxManifold
