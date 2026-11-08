// The editor window's layout (round 3A-10), checked at many window and text
// sizes without openFrameworks: layoutEditor() and flowKeys() are what the
// app draws from, so what holds here holds on screen.
//
// Every key line inside the panel and above the faders; no two lines on top
// of each other; every key placed once, in order, or counted as left out;
// the views never below six tenths of their size; the levels keys under the
// 3D view above the faders. And at the sizes an editor is used at, every key
// shown -- the panel's job.
//
//   make panel        (part of make test)

#include "ofxManifoldEditor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ofxManifold;
using namespace ofxManifold::editor;

namespace {

int failures = 0;

void fail(const std::string& where, const std::string& what) {
    if (++failures <= 20) std::printf("  FAIL  %s: %s\n", where.c_str(), what.c_str());
}

std::size_t entryCount(const std::vector<KeyGroup>& g) {
    std::size_t n = 0;
    for (const KeyGroup& k : g) n += k.entries.size();
    return n;
}

// The flow's promises, for any room.
void checkFlow(const std::string& where, const std::vector<KeyGroup>& groups,
               const KeyFlow& f, float x, float right, float top,
               float keysTop, float bottom, float cw, float lh) {
    const float row = lh + 1.0f;
    for (const KeyLine& l : f.lines) {
        if (l.x < x - 0.5f || l.x + float(f.chars) * cw > right + 0.5f) {
            fail(where, "a line runs past the panel's sides");
            return;
        }
        if (l.y > bottom + 0.5f) { fail(where, "a line below the panel's bottom"); return; }
        const bool firstColumn = l.x < x + 0.5f;
        if (l.y < (firstColumn ? keysTop : top) - 0.5f) {
            fail(where, "a line above its column's top");
            return;
        }
    }
    // The first column starts under the status; every other one at the top.
    for (std::size_t c = 0; c < f.columns; ++c) {
        const float cx = x + float(c) * f.columnWidth;
        float first = 1e9f;
        for (const KeyLine& l : f.lines) {
            if (std::fabs(l.x - cx) < 0.5f) first = std::min(first, l.y);
        }
        if (first > 1e8f) continue;
        if (std::fabs(first - (c == 0 ? keysTop : top)) > 0.5f) {
            fail(where, "column " + std::to_string(c) + " starts at "
                 + std::to_string(first) + ", not its top");
            return;
        }
    }
    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        for (std::size_t j = i + 1; j < f.lines.size(); ++j) {
            const KeyLine& a = f.lines[i];
            const KeyLine& b = f.lines[j];
            if (std::fabs(a.x - b.x) < 0.5f && std::fabs(a.y - b.y) < row - 0.01f) {
                fail(where, "two lines on top of each other");
                return;
            }
        }
    }
    // Once each, in order.
    std::size_t placed = 0;
    long last = -1;
    for (const KeyLine& l : f.lines) {
        if (l.entry < 0) continue;
        long at = 0;
        for (std::size_t g = 0; g < l.group; ++g) at += long(groups[g].entries.size());
        at += l.entry;
        if (at <= last) { fail(where, "keys out of order or twice"); return; }
        last = at;
        ++placed;
    }
    if (placed + f.hidden + f.dimLeftOut != entryCount(groups)) {
        fail(where, "keys placed + left out != keys: "
             + std::to_string(placed) + " + " + std::to_string(f.hidden) + " + "
             + std::to_string(f.dimLeftOut) + " != "
             + std::to_string(entryCount(groups)));
    }
    if ((f.hidden > 0 || f.dimLeftOut > 0)
        && (f.moreY < 0.0f || f.moreY > bottom + 0.5f)) {
        fail(where, "keys left out, and no note of it in the panel");
    }
}

}  // namespace

int main() {
    // A busy editor: the example map, three levels, a value, everything
    // selected -- the most keys lit at once.
    Model m;
    m.loadExample();
    m.newLevel(true);
    m.newLevel(true);
    m.newValue("brightness 0.5 0 1");
    Selection sel;
    for (NodeID n = 0; n < m.manifold().nodeCount(); ++n) sel.toggle(n);

    struct Win { float w, h; };
    const Win wins[] = {{1024, 640}, {1280, 800}, {1323, 860}, {1360, 884},
                        {1555, 1005}, {1728, 1117}, {1920, 1080}, {2304, 1296}};
    // Menlo at 72 dpi: 0.602 of the size across, 1.17 of it down. And the
    // bitmap font openFrameworks falls back on.
    struct Font { float size, cw, lh; };
    std::vector<Font> fonts;
    for (int s = 8; s <= 24; ++s) fonts.push_back({float(s), 0.602f * s, 1.17f * s});
    fonts.push_back({0.0f, 8.0f, 13.0f});

    std::size_t configs = 0;
    std::printf("  keys left out (lit + dim) -- views at least half their size:\n");
    std::printf("  %-11s", "window");
    for (int s : {12, 14, 16, 18, 20, 22, 24}) std::printf("  %4dpt", s);
    std::printf("\n");
    for (const Win& w : wins) {
        std::string row;
        for (const Font& f : fonts) {
            for (int view = 0; view < 2; ++view) {
                for (GridShape gs : {GridShape::Lattice, GridShape::Polar}) {
                    KeyContext ctx;
                    ctx.model = &m;
                    ctx.selection = &sel;
                    ctx.canUndo = true;
                    ctx.grid = gs;
                    const std::vector<KeyGroup> all = keyMap(ctx);
                    const EditorLayout L = layoutEditor(w.w, w.h, f.cw, f.lh, true,
                                                        view == 1, true, all);
                    const std::string where = std::to_string(int(w.w)) + "x"
                        + std::to_string(int(w.h)) + " text "
                        + (f.size > 0 ? std::to_string(int(f.size)) : "bitmap")
                        + (view ? " 3D" : "");
                    const std::vector<KeyGroup> shown = panelGroups(all, view == 1);
                    checkFlow(where, shown, L.keys, L.panelLeft, w.w - 16.0f,
                              panelTop(f.lh), panelKeysTop(f.lh, true),
                              L.panelBottom, f.cw, f.lh);
                    ++configs;
                    // The status column above the faders.
                    if (panelKeysTop(f.lh, true) - f.lh > L.panelBottom + 0.5f
                        && w.h >= 800 && f.size <= 18) {
                        fail(where, "the status runs into the faders");
                    }
                    if (L.panelBottom >= L.chartTop
                        && L.panelLeft < L.chartRight + 0.5f) {
                        fail(where, "the panel runs into the faders");
                    }
                    if (L.chartRight - 24.0f < L.plan - 0.5f) {
                        fail(where, "the faders narrower than the map");
                    }
                    if (view) {
                        float levelsRows = 0.0f;
                        for (const KeyGroup& g : all) {
                            if (g.title == "levels") levelsRows = 1.0f + g.entries.size();
                        }
                        if (L.levelsKeysY + (levelsRows - 1.0f) * (f.lh + 1.0f)
                            > L.chartTop + 0.5f && L.panelBottom < L.chartTop) {
                            fail(where, "the levels keys run into the faders");
                        }
                    }
                    // The views give way, but not past six tenths.
                    const float chartH = std::max(130.0f, w.h * 0.2f);
                    const float side = std::max(160.0f, w.h - 48.0f - chartH);
                    if (!view && L.plan < std::max(160.0f, side * 0.5f) - 0.5f) {
                        fail(where, "the map shrank past half");
                    }
                    // At the sizes an editor is used at, every key shows.
                    const bool everything = L.keys.hidden == 0 && L.keys.dimLeftOut == 0;
                    if (!everything && w.w >= 1280 && w.h >= 800 && f.size > 0
                        && f.size <= 14) {
                        fail(where, "keys left out: " + std::to_string(L.keys.hidden)
                             + " lit, " + std::to_string(L.keys.dimLeftOut) + " dim");
                    }
                    // Lee's ask: the outputs keys in the right-hand columns,
                    // which start at the top, when everything fits that way.
                    const KeyFlow onTop = flowKeys(shown, L.panelLeft,
                        w.w - 16.0f, panelTop(f.lh), panelKeysTop(f.lh, true),
                        L.panelBottom, f.cw, f.lh, false);
                    if (onTop.hidden == 0 && onTop.columns > 1) {
                        bool top = false;
                        for (const KeyLine& l : L.keys.lines) {
                            if (l.entry < 0 && shown[l.group].title == "outputs"
                                && !l.more) {
                                // In a column of its own from the top, or,
                                // when the groups before it run over into
                                // the second column, after them there.
                                top = l.x > L.panelLeft + 0.5f;
                            }
                        }
                        if (!top) fail(where, "outputs not at the top right: hidden "
                            + std::to_string(L.keys.hidden) + "+" + std::to_string(L.keys.dimLeftOut)
                            + " cols " + std::to_string(L.keys.columns) + "/" + std::to_string(onTop.columns)
                            + " plan " + std::to_string(L.plan));
                    }
                    // The key sheet over the whole window.
                    const float first = 24.0f + f.lh + 2.0f * (f.lh + 1.0f);
                    const KeyFlow sheet = flowKeys(all, 24.0f, w.w - 24.0f, first,
                                                   first, w.h - 16.0f, f.cw, f.lh);
                    checkFlow(where + " sheet", all, sheet, 24.0f, w.w - 24.0f,
                              first, first, w.h - 16.0f, f.cw, f.lh);
                    if (sheet.hidden + sheet.dimLeftOut > 0 && w.w >= 1280
                        && w.h >= 800 && f.size > 0 && f.size <= 20) {
                        fail(where + " sheet", "the key sheet leaves keys out");
                    }
                    if (view == 0 && gs == GridShape::Lattice && f.size > 0
                        && int(f.size) % 2 == 0 && f.size >= 12) {
                        char cell[16];
                        std::snprintf(cell, sizeof cell, " %2zu+%zu/%-2zu",
                                      L.keys.hidden, L.keys.dimLeftOut,
                                      sheet.hidden + sheet.dimLeftOut);
                        row += cell;
                    }
                }
            }
        }
        std::printf("  %4dx%-6d%s\n", int(w.w), int(w.h), row.c_str());
    }
    if (failures) {
        std::printf("  %d failures over %zu layouts\n", failures, configs);
        return 1;
    }
    std::printf("  ok  the editor's layout holds at %zu window and text sizes\n", configs);
    return 0;
}
