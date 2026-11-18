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
    if (f.hidden > 0 && (f.moreY < 0.0f || f.moreY > bottom + 0.5f)) {
        fail(where, "keys left out, and no note of it in the panel");
    }
    // A header is never alone at a column's foot (3B-2g): the next line in
    // its column is one of its own rows, and so is the line below it.
    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        const KeyLine& h = f.lines[i];
        if (h.entry >= 0) continue;
        if (groups[h.group].entries.empty()) continue;   // nothing to put under it
        const bool next = i + 1 < f.lines.size() && f.lines[i + 1].entry >= 0
            && f.lines[i + 1].group == h.group && std::fabs(f.lines[i + 1].x - h.x) < 0.5f;
        if (!next) { fail(where, "a header alone at a column's foot"); return; }
    }
    // The room above every header but a column's first (3B-2g): a section is
    // told apart by the space before it.
    for (std::size_t i = 1; i < f.lines.size(); ++i) {
        const KeyLine& h = f.lines[i];
        const KeyLine& before = f.lines[i - 1];
        if (h.entry >= 0 || std::fabs(before.x - h.x) > 0.5f) continue;
        if (h.y - before.y < row + keyGroupGap(lh) - 0.01f) {
            fail(where, "a header without room above it");
            return;
        }
    }
}

// A player's sections (3B-2g), made up as example-osc makes them: each with
// state lines (what is true now, with the key that changes it) and keys.
std::vector<KeyGroup> playerSections() {
    const int state[] = {4, 3, 4, 5, 1, 3, 1, 2};
    const int keys[]  = {4, 5, 6, 11, 3, 6, 3, 5};
    const char* titles[] = {"sending", "points", "the point", "movement",
                            "smoothing", "from outside", "showing", "maps"};
    std::vector<KeyGroup> g;
    for (int s = 0; s < 8; ++s) {
        KeyGroup k{titles[s], {}, KeyColour{static_cast<unsigned char>(100 + 10 * s), 200, 150}};
        for (int i = 0; i < state[s] + keys[s]; ++i) {
            KeyEntry e;
            e.keys = i % 3 ? "k" : "";
            e.action = std::string(titles[s]) + " " + std::to_string(i);
            e.available = true;
            e.state = i < state[s];
            k.entries.push_back(e);
        }
        g.push_back(k);
    }
    return g;
}

// keyRows: only the rows asked for, in order, each section's title and
// colour kept, a section left with none dropped.
void checkRows() {
    std::vector<KeyGroup> g = playerSections();
    g.push_back(KeyGroup{"keys only", {}, KeyColour{}});
    KeyEntry e;
    e.keys = "q";
    e.action = "a key";
    e.available = true;
    g.back().entries.push_back(e);
    for (bool state : {true, false}) {
        const std::vector<KeyGroup> r = keyRows(g, state);
        if (r.size() != (state ? 8u : 9u)) { fail("keyRows", "the wrong sections kept"); return; }
        for (std::size_t s = 0; s < r.size(); ++s) {
            const KeyGroup& from = g[s];
            if (r[s].title != from.title || r[s].colour.r != from.colour.r) {
                fail("keyRows", "a section's title or colour lost");
                return;
            }
            std::size_t k = 0;
            for (const KeyEntry& x : from.entries) {
                if (x.state != state) continue;
                if (k >= r[s].entries.size() || r[s].entries[k].action != x.action) {
                    fail("keyRows", "rows lost, added or out of order");
                    return;
                }
                ++k;
            }
            if (k != r[s].entries.size()) { fail("keyRows", "rows of the other kind kept"); return; }
        }
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
    checkRows();
    // Every editor section in its own colour, carried by the data (3B-2g):
    // the same wherever it is drawn, no two alike.
    {
        KeyContext ctx;
        ctx.model = &m;
        ctx.selection = &sel;
        const std::vector<KeyGroup> all = keyMap(ctx);
        for (std::size_t a = 0; a < all.size(); ++a) {
            const KeyColour c = editorGroupColour(all[a].title);
            if (all[a].colour.r != c.r || all[a].colour.g != c.g || all[a].colour.b != c.b)
                fail("colours", all[a].title + " not in its own colour");
            for (std::size_t b = 0; b < a; ++b) {
                if (all[a].colour.r == all[b].colour.r && all[a].colour.g == all[b].colour.g
                    && all[a].colour.b == all[b].colour.b)
                    fail("colours", all[a].title + " and " + all[b].title + " alike");
            }
        }
    }
    // A player's panel and ? page (3B-2g): its sections' state lines in a
    // strip beside the views, its keys over the window -- the flow's
    // promises, headers never alone, at every size.
    {
        const std::vector<KeyGroup> all = playerSections();
        const std::vector<KeyGroup> state = keyRows(all, true), keysOnly = keyRows(all, false);
        for (const Win& w : wins) {
            for (const Font& f : fonts) {
                for (float strip : {44.0f, 72.0f, 120.0f}) {
                    const std::string where = "player " + std::to_string(int(w.w)) + "x"
                        + std::to_string(int(w.h)) + " text "
                        + (f.size > 0 ? std::to_string(int(f.size)) : "bitmap")
                        + " strip " + std::to_string(int(strip));
                    const float x = std::max(24.0f, w.w - 16.0f - strip * f.cw);
                    const float top = 40.0f + f.lh + keyGroupGap(f.lh);
                    const float bottom = w.h - std::max(130.0f, std::min(220.0f, w.h * 0.24f))
                                       - 12.0f - f.lh - 8.0f;
                    const KeyFlow p = flowKeys(state, x, w.w - 16.0f, top, top, bottom,
                                               f.cw, f.lh, true, false);
                    checkFlow(where, state, p, x, w.w - 16.0f, top, top, bottom, f.cw, f.lh);
                    const float first = 50.0f + 2.0f * (f.lh + 1.0f);
                    const KeyFlow s = flowKeys(keysOnly, 40.0f, w.w - 40.0f, first, first,
                                               w.h - 30.0f, f.cw, f.lh, true, false);
                    checkFlow(where + " sheet", keysOnly, s, 40.0f, w.w - 40.0f, first,
                              first, w.h - 30.0f, f.cw, f.lh);
                    if (s.hidden > 0 && w.w >= 1280 && w.h >= 800 && f.size > 0 && f.size <= 16)
                        fail(where + " sheet", "the player's ? page leaves keys out");
                    configs += 2;
                }
            }
        }
    }
    std::printf("  lit keys the panel leaves out / keys the ? sheet leaves out,\n"
                "  a busy editor, views at least half their size:\n");
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
                    // The views keep their size whatever is lit (3A-12): the
                    // same window with nothing selected lays out the same.
                    {
                        Selection none;
                        KeyContext quiet = ctx;
                        quiet.selection = &none;
                        quiet.canUndo = false;
                        const EditorLayout Q = layoutEditor(w.w, w.h, f.cw, f.lh,
                            true, view == 1, true, keyMap(quiet));
                        if (std::fabs(Q.plan - L.plan) > 0.01f
                            || std::fabs(Q.panelLeft - L.panelLeft) > 0.01f
                            || std::fabs(Q.chartRight - L.chartRight) > 0.01f) {
                            fail(where, "the views change size as keys light");
                        }
                    }
                    // The panel shows only what can be done now (3A-12).
                    for (const KeyLine& l : L.keys.lines) {
                        if (l.entry >= 0
                            && !shown[l.group].entries[std::size_t(l.entry)].available) {
                            fail(where, "the panel shows a key that cannot act");
                            break;
                        }
                    }
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
                    // The panel shows the lit keys (3A-12): all of them.
                    const bool everything = L.keys.hidden == 0;
                    if (!everything && w.w >= 1280 && w.h >= 800 && f.size > 0
                        && f.size <= 14) {
                        fail(where, "lit keys left out: " + std::to_string(L.keys.hidden));
                    }
                    // Lee's ask: the outputs keys in the right-hand columns,
                    // which start at the top, when everything fits that way.
                    const KeyFlow onTop = flowKeys(shown, L.panelLeft,
                        w.w - 16.0f, panelTop(f.lh), panelKeysTop(f.lh, true),
                        L.panelBottom, f.cw, f.lh, false, true);
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
                    // The keys hidden (3B-2e): the views no smaller than with
                    // the panel, inside the window, side by side, above the
                    // faders, which run across it.
                    {
                        const EditorLayout B = layoutEditorBare(w.w, w.h, true, view == 1);
                        const float right = view ? B.view3DX + B.plan : 24.0f + B.plan + B.column;
                        if (B.plan < L.plan - 0.5f) fail(where, "the views shrink with the keys hidden");
                        if (right > w.w - 24.0f + 0.5f) fail(where, "a view past the window, keys hidden");
                        if (24.0f + B.plan > B.chartTop + 0.5f) fail(where, "a view over the faders, keys hidden");
                        if (view && B.view3DX < 24.0f + B.plan + B.column - 0.5f)
                            fail(where, "the views overlap, keys hidden");
                        if (std::fabs(B.chartRight - (w.w - 24.0f)) > 0.5f)
                            fail(where, "the faders not across the window, keys hidden");
                        ++configs;
                    }
                    // The key sheet over the whole window.
                    const float first = 24.0f + f.lh + 2.0f * (f.lh + 1.0f);
                    const KeyFlow sheet = flowKeys(all, 24.0f, w.w - 24.0f, first,
                                                   first, w.h - 16.0f, f.cw, f.lh);
                    checkFlow(where + " sheet", all, sheet, 24.0f, w.w - 24.0f,
                              first, first, w.h - 16.0f, f.cw, f.lh);
                    if (sheet.hidden > 0 && w.w >= 1280
                        && w.h >= 800 && f.size > 0 && f.size <= 20) {
                        fail(where + " sheet", "the key sheet leaves keys out");
                    }
                    if (view == 0 && gs == GridShape::Lattice && f.size > 0
                        && int(f.size) % 2 == 0 && f.size >= 12) {
                        char cell[16];
                        std::snprintf(cell, sizeof cell, "  %3zu/%-3zu",
                                      L.keys.hidden, sheet.hidden);
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
