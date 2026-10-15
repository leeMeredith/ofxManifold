#include "ofxManifoldEditorChartLayout.h"

#include <algorithm>

namespace ofxManifold {
namespace editor {

namespace {

// The longest of `options` that fits in `room`, or "".
std::string fitted(const std::vector<std::string>& options, float room,
                   float cw) {
    for (const std::string& o : options) {
        if (float(o.size()) * cw <= room) return o;
    }
    return std::string();
}

} // namespace

ChartLayout layoutChart(const Mapping& mapping, float x0, float top,
                        float width, float height, ChartText text) {
    ChartLayout L;
    L.x0 = x0;
    L.top = top;
    L.width = width;
    L.height = height;
    L.barArea = height - (2.0f * text.lineHeight + 4.0f);
    L.base = top + L.barArea;
    L.empty = (mapping.targetCount() == 0 && mapping.aggregators().empty());
    if (L.empty) return L;

    // Who owns each channel. The channel vector's length is highest + 1;
    // read with no weights, since only the length matters here.
    const std::size_t channels = mapping.toChannels(WeightVector{}).size();
    for (std::size_t ch = 0; ch < channels; ++ch) {
        ChartBar b;
        b.channel = int(ch);
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

    // Fit: `full` bars (every used channel, plus silence) of width bw, `thin`
    // slivers for unused channels, and a gap before every item, after the
    // last, and an extra one before silence.
    const float room = width - 16.0f * text.charWidth;   // the total's column
    std::size_t full = 1, thin = 0;
    for (const ChartBar& b : L.bars) {
        if (b.kind == ChartBar::Kind::Unused) ++thin; else ++full;
    }
    const float gaps = float(full + thin + 2);
    const float sliver = 4.0f;
    // Gaps of 8 while bars are wide; once a bar would be narrower than 32,
    // the gaps shrink with it, to a quarter of a bar, so bars do not vanish
    // into gaps.
    float bw = (room - float(thin) * sliver - gaps * 8.0f) / float(full);
    float gap = 8.0f;
    if (bw < 32.0f) {
        bw = (room - float(thin) * sliver) / (float(full) + gaps * 0.25f);
        gap = std::max(1.0f, bw * 0.25f);
    }
    bw = std::min(46.0f, std::max(2.0f, bw));
    L.barWidth = bw;
    L.gap = gap;

    const float labelRoom = bw + gap - 1.0f;   // a label may use the gap
    float x = x0 + gap;
    for (ChartBar& b : L.bars) {
        b.x = x;
        b.w = (b.kind == ChartBar::Kind::Unused) ? std::min(sliver, bw) : bw;
        x += b.w + gap;
    }
    const float silenceX = x + gap;

    // Labels, for the whole row at once.
    auto fits = [&](const std::string& t) {
        return float(t.size()) * text.charWidth <= labelRoom;
    };
    bool allNames = true, allNumbers = true;
    for (const ChartBar& b : L.bars) {
        if (b.kind == ChartBar::Kind::Unused) continue;
        if (!fits(b.name)) allNames = false;
        if (!fits(std::to_string(b.channel))) allNumbers = false;
    }
    if (allNames || allNumbers) {
        for (ChartBar& b : L.bars) {
            if (b.kind == ChartBar::Kind::Unused) continue;
            const std::string ch = std::to_string(b.channel);
            if (allNames) {
                b.label = b.name;
                if (allNumbers) b.sub = ch;
            } else {
                b.label = ch;
            }
        }
    } else {
        // A ruler: the smallest step at which every marked channel's number
        // fits in the room up to the next mark (or to silence).
        for (int step : {2, 5, 10, 20, 50, 100, 200, 500, 1000}) {
            bool ok = true;
            for (std::size_t i = 0; i < L.bars.size() && ok; ++i) {
                if (L.bars[i].channel % step != 0) continue;
                const std::size_t j = i + std::size_t(step);
                const float end = j < L.bars.size() ? L.bars[j].x : silenceX;
                const std::string n = std::to_string(L.bars[i].channel);
                if (float(n.size()) * text.charWidth > end - L.bars[i].x - 1.0f) {
                    ok = false;
                }
            }
            if (ok) {
                L.labelStep = step;
                for (ChartBar& b : L.bars) {
                    if (b.channel % step == 0) b.label = std::to_string(b.channel);
                }
                break;
            }
        }
    }
    L.silenceX = silenceX;
    L.silenceLabel = fitted({"silent", "sil"}, labelRoom, text.charWidth);
    L.totalX = x0 + room + 8.0f;
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

int ChartLayout::hoverAt(float x, float y) const {
    if (empty || y < top || y > top + height) return kNothing;
    const float half = gap * 0.5f;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const ChartBar& b = bars[i];
        if (x >= b.x - half && x <= b.x + b.w + half) return int(i);
    }
    if (x >= silenceX - half && x <= silenceX + barWidth + half) return kSilence;
    return kNothing;
}

TargetID ChartLayout::outputAt(float x, float y) const {
    const int i = hoverAt(x, y);
    if (i < 0) return InvalidTarget;
    const ChartBar& b = bars[std::size_t(i)];
    return b.kind == ChartBar::Kind::Output ? b.output : InvalidTarget;
}

} // namespace editor
} // namespace ofxManifold
