#!/usr/bin/env python3
"""
ofxManifoldEditor — independent reference for the editor model.

SCRIPTS, NOT SINGLE CALLS. Each scenario is a sequence of operations with the
complete state checked after every one. An editing mistake often only shows on
the NEXT operation -- a delete that renumbers badly is visible when the
following bind lands on the wrong node.

FORMULATION DIFFERS WHERE IT MATTERS. This tracks nodes, outputs, bindings and
regions BY NAME, so a removal renumbers nothing. The C++ holds numeric ids and
must renumber on every delete. Agreement is the evidence the renumbering is
right.

CHARACTERIZATION. Round 1 of the editor work (DECISIONS.md D-023) changed no behaviour, so these pin
what the editor does TODAY -- including things round 2 will change on purpose,
such as a quarter-share of silence per press. When round 2 changes one, its
vector changes in the same commit, visibly.
"""

import math
import sys

sys.path.insert(0, "tests/ref")
import reference_grids as G       # noqa: E402  snapping, independently
import reference_regions as R     # noqa: E402  region validity, independently


def fmt(x):
    return f"{x:.6g}"


# ---------------------------------------------------------------------------
# the model, by name
# ---------------------------------------------------------------------------

class Editor:
    def __init__(self):
        self.new_empty()
        self.auto = True

    def new_empty(self):
        self.nodes = []          # [name, (x, y)] in id order
        self.regions = []        # [tuple of names] in id order, ring order
        self.outputs = []        # [name, channel, trim] in id order
        # Levels (round 3A-6), bottom first. Each has its starting height,
        # the heights of its bent nodes BY NAME (absolute, and only where they
        # differ from the base), and its own bindings. `links` is the current
        # level's: every operation written before levels reads and writes
        # exactly what it did.
        self.levels = [dict(base=0.0, heights={}, links={})]
        self.cur = 0
        self.derived = []        # [name, channel, [source names]]
        self.current = None
        self.out_sel = []        # selected outputs, by name; current among them

    # node name -> [(kind, output name|None, w)], on the current level
    @property
    def links(self):
        return self.levels[self.cur]["links"]

    @links.setter
    def links(self, value):
        self.levels[self.cur]["links"] = value

    def all_links(self):
        return [lv["links"] for lv in self.levels]

    def height(self, k, name):
        lv = self.levels[k]
        return lv["heights"].get(name, lv["base"])

    def set_height(self, k, name, h):
        lv = self.levels[k]
        if h == lv["base"]:
            lv["heights"].pop(name, None)
        else:
            lv["heights"][name] = h

    def fits(self, k, name, h):
        if k > 0 and not h > self.height(k - 1, name):
            return False
        if k + 1 < len(self.levels) and not h < self.height(k + 1, name):
            return False
        return True

    def three_d(self):
        """Anything only a 3D map has: what the dump shows past 'A'."""
        lv = self.levels
        return len(lv) > 1 or lv[0]["base"] != 0.0 or bool(lv[0]["heights"])

    # ---- helpers -------------------------------------------------------
    def names(self):
        return [n[0] for n in self.nodes]

    def pos(self, name):
        return dict(self.nodes)[name]

    def out_names(self):
        return [o[0] for o in self.outputs]

    def fresh(self):
        k = len(self.nodes)
        while True:
            nm = f"n{k}"
            if nm not in self.names() and nm not in self.out_names():
                return nm
            k += 1

    def next_channel(self):
        used = {o[1] for o in self.outputs} | {d[1] for d in self.derived}
        ch = 0
        while ch in used:
            ch += 1
        return ch

    def output_count(self, node):
        return sum(1 for l in self.links.get(node, []) if l[0] == "out")

    def in_use(self, out):
        # on ANY level: deleting a node must not take an output another
        # level's nodes still feed
        return any(l[0] == "out" and l[1] == out
                   for links in self.all_links()
                   for ls in links.values() for l in ls)

    def settle(self):
        if self.current not in self.out_names():
            self.current = None
        if self.current is None and self.outputs:
            self.current = self.outputs[0][0]
        self.out_sel = [o for o in self.out_sel if o in self.out_names()]
        if self.current is None:
            self.out_sel = []
        elif self.current not in self.out_sel:
            self.out_sel.append(self.current)

    def select_only(self, name):
        self.current = name
        self.out_sel = [] if name is None else [name]

    def bind_one(self, node, out, w=1.0):
        ls = self.links.setdefault(node, [])
        for i, l in enumerate(ls):
            if l[0] == "out" and l[1] == out:
                ls[i] = ("out", out, l[2] + w)
                return
        ls.append(("out", out, w))

    def silence_one(self, node, w):
        ls = self.links.setdefault(node, [])
        for i, l in enumerate(ls):
            if l[0] == "sil":
                ls[i] = ("sil", None, l[2] + w)
                return
        ls.append(("sil", None, w))

    def remove_output(self, name):
        self.outputs = [o for o in self.outputs if o[0] != name]
        # an after-fed derived output loses the source, and stays (3A-1)
        self.derived = [[d[0], d[1], [x for x in d[2] if x != ">" + name]]
                        + d[3:] for d in self.derived]
        for links in self.all_links():
            for n in list(links):
                links[n] = [l for l in links[n]
                            if not (l[0] == "out" and l[1] == name)]
                if not links[n]:
                    del links[n]
        self.out_sel = [o for o in self.out_sel if o != name]
        if self.current == name:
            self.current = self.out_sel[0] if self.out_sel else None
        self.settle()

    # ---- operations ----------------------------------------------------
    def occupied(self, p, addr, grid):
        for _nm, q in self.nodes:
            if addr is not None:
                if grid.snap(q)[1] == addr and \
                        math.dist(grid.point(addr), q) < 1e-4:
                    return True
            elif math.dist(p, q) < 0.01:
                return True
        return False

    def place(self, p, grid):
        q, addr = grid.snap(p)
        if not (0.0 <= q[0] <= 1.0 and 0.0 <= q[1] <= 1.0):
            return (0, 0, "")
        if self.occupied(q, addr, grid):
            return (0, 1, "a node is already there")
        nm = self.fresh()
        self.nodes.append([nm, (q[0], q[1])])
        if self.auto and nm not in self.out_names():
            self.outputs.append([nm, self.next_channel(), 1.0])
            # on every level: a speaker is a speaker at every height
            keep = self.cur
            for k in range(len(self.levels)):
                self.cur = k
                self.bind_one(nm, nm)
            self.cur = keep
            if self.current is None:        # only if there was none
                self.select_only(nm)
        return (1, 0, "")

    def move(self, name, p):
        # Single-node moves only in these scripts, and only valid ones.
        for n in self.nodes:
            if n[0] == name:
                n[1] = (p[0], p[1])
        return (1, 0, "")

    def join(self, sel, ordered):
        if len(sel) < 3:
            return (0, 1, "select at least three nodes to join")
        ring = list(sel)
        if not ordered:
            pts = [self.pos(n) for n in sel]
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            idx = sorted(range(len(pts)),
                         key=lambda i: (math.atan2(pts[i][1] - cy,
                                                   pts[i][0] - cx),
                                        (pts[i][0] - cx) ** 2
                                        + (pts[i][1] - cy) ** 2, i))
            ring = [sel[i] for i in idx]
        if any(sorted(r) == sorted(ring) for r in self.regions):
            return (0, 1, "those nodes already form a region")
        why = R.why_invalid([self.pos(n) for n in ring], ids=ring)
        if why is not None:
            return (0, 1, "refused: " + why
                    + (" (in click order)" if ordered else ""))
        self.regions.append(tuple(ring))
        return (1, 0, f"joined {len(ring)} nodes in "
                + ("click order" if ordered else "angle order"))

    def unjoin(self, sel):
        doomed = [r for r in self.regions if all(n in sel for n in r)]
        if not doomed:
            return (0, 1, "no region is made only of selected nodes")
        self.regions = [r for r in self.regions if r not in doomed]
        k = len(doomed)
        return (1, 0, f"unjoined {k} region" + ("" if k == 1 else "s"))

    def remove(self, sel):
        if not sel:
            return (0, 0, "")
        gone = set(sel)
        self.nodes = [n for n in self.nodes if n[0] not in gone]
        before = len(self.regions)
        self.regions = [r for r in self.regions
                        if not any(n in gone for n in r)]
        rg = before - len(self.regions)
        for n in gone:
            for lv in self.levels:
                lv["links"].pop(n, None)
                lv["heights"].pop(n, None)
        self.derived = [[d[0], d[1], [s for s in d[2] if s not in gone]]
                        + d[3:] for d in self.derived]
        og = 0
        for nm in sel:
            if nm in self.out_names() and not self.in_use(nm):
                self.remove_output(nm)
                og += 1
        k = len(sel)
        msg = f"removed {k} node" + ("" if k == 1 else "s")
        if rg:
            msg += f", and {rg} region" + ("" if rg == 1 else "s") \
                + " that used them"
        if og:
            msg += f", and {og} output" + ("" if og == 1 else "s")
        return (1, 0, msg)

    def new_output(self):
        ch = self.next_channel()
        nm = f"out{ch}"
        k = 2
        while nm in self.out_names():
            nm = f"out{ch}_{k}"
            k += 1
        self.outputs.append([nm, ch, 1.0])
        self.select_only(nm)
        return (1, 0, f"new output {nm} on channel {ch} "
                "-- select nodes and press b")

    def new_derived(self, sel):
        if not sel:
            return (0, 1, "select the nodes a derived output should sum")
        ch = self.next_channel()
        nm = f"sum{ch}"
        self.derived.append([nm, ch, list(sel)])
        return (1, 0, f"derived {nm} on channel {ch}, sum of {len(sel)}")

    # ---- round 3A-5: after the fader, and rigs -----------------------------
    #
    # A derived output fed by outputs is [name, channel, [">L", ">R"],
    # "after"]: its sources are output names, marked so they never match a
    # node's. One fed by nodes keeps its three fields, as before.

    @staticmethod
    def kind(d):
        return d[3] if len(d) > 3 else "filled"

    def new_derived_after(self):
        if not self.out_sel:
            return (0, 1, "no output yet -- press o to make one")
        ch = self.next_channel()
        nm = f"sum{ch}"
        self.derived.append([nm, ch, [">" + o for o in self.out_sel], "after"])
        return (1, 0, f"derived {nm} on channel {ch}, after "
                + ", ".join(self.out_sel))

    def rig(self):
        """The room: outputs, after-fed derived outputs, and the channels the
        map fills -- listed, with no sources."""
        return ([list(o) for o in self.outputs],
                [[d[0], d[1], list(d[2]) if self.kind(d) == "after" else [],
                  self.kind(d)] for d in self.derived])

    def use_rig(self, rig, name):
        routs, rder = rig
        rk = {o[0]: "output" for o in routs}
        rk.update({d[0]: d[3] for d in rder})
        mine = [(o[0], "output") for o in self.outputs] + \
               [(d[0], self.kind(d)) for d in self.derived]
        refused = sorted({n for n, k in mine if rk.get(n) != k})
        if refused:
            return (0, 1, f"cannot use rig {name} -- not in it, or not the "
                    f"same kind: {', '.join(refused)}")
        fills = {d[0]: d for d in self.derived if self.kind(d) == "filled"}
        self.outputs = [list(o) for o in routs]
        new = []
        for d in rder:
            if d[3] == "after":
                new.append([d[0], d[1], list(d[2]), "after"])
            else:
                new.append([d[0], d[1],
                            list(fills[d[0]][2]) if d[0] in fills else []])
        self.derived = new
        self.settle()
        return (1, 0, f"using rig {name}: {len(self.outputs)} outputs")

    def feeds(self, node, out):
        return any(l[0] == "out" and l[1] == out
                   for l in self.links.get(node, []))

    def unbind_one(self, node, out):
        ls = [l for l in self.links.get(node, [])
              if not (l[0] == "out" and l[1] == out)]
        if ls:
            self.links[node] = ls
        else:
            self.links.pop(node, None)

    def toggle_output(self, name):
        if name not in self.out_names():
            return (0, 0, "")
        if name in self.out_sel:
            if len(self.out_sel) == 1:
                return (0, 0, "")
            self.out_sel = [o for o in self.out_sel if o != name]
            if self.current == name:
                self.current = self.out_sel[0]
        else:
            self.out_sel.append(name)
            self.current = name
        head = "current output: " if len(self.out_sel) == 1 else "outputs: "
        return (0, 0, head + ", ".join(self.out_sel))

    def bind(self, sel):
        if not self.out_sel:
            return (0, 1, "no output yet -- press o to make one")
        if not sel:
            return (0, 0, "")
        every = all(self.feeds(n, t) for n in sel for t in self.out_sel)
        count, silenced = 0, []
        for n in sel:
            fill = self.fraction(n) if self.output_count(n) > 0 else 1.0
            touched = False
            for t in self.out_sel:
                if every != self.feeds(n, t):
                    continue
                if every:
                    self.unbind_one(n, t)
                else:
                    self.bind_one(n, t)
                touched = True
            if not touched:
                continue
            if self.output_count(n) == 0:
                self.links.pop(n, None)
                silenced.append(n)
            elif fill < 1.0:
                self.set_fraction(n, fill)
            count += 1
        msg = (("unbound " if every else "bound ") + str(count)
               + (" from " if every else " to ") + ", ".join(self.out_sel))
        if silenced:
            who = silenced[0] if len(silenced) == 1 \
                else f"{len(silenced)} nodes"
            return (1, 1, msg + f" -- {who} now silent; cmd-z undoes")
        return (1, 0, msg)

    def fraction(self, node):
        ls = self.links.get(node, [])
        out_w = sum(l[2] for l in ls if l[0] == "out")
        tot = sum(l[2] for l in ls)
        return 0.0 if out_w <= 0 or tot <= 0 else out_w / tot

    def set_fraction(self, node, f):
        ls = self.links.get(node, [])
        out_w = sum(l[2] for l in ls if l[0] == "out")
        kept = [l for l in ls if l[0] != "sil"]
        if f < 1.0:
            kept.append(("sil", None, out_w * (1.0 - f) / f))
        self.links[node] = kept

    def step_fill(self, sel, direction, fine):
        """Restated from the rule: land on the next multiple of the step in
        its direction; a value already on a step moves a whole step."""
        if not sel:
            return (0, 0, "")
        unit = 0.01 if fine else 0.05
        vals, moved, any_ = [], False, False
        for n in sel:
            if self.output_count(n) == 0:
                continue
            any_ = True
            before = self.fraction(n)
            k = before / unit
            kr = round(k)
            if abs(k - kr) < 1e-3:
                nxt = kr + direction
            else:
                nxt = math.ceil(k) if direction > 0 else math.floor(k)
            f = min(1.0, max(0.01, nxt * unit))
            self.set_fraction(n, f)
            after = self.fraction(n)
            if abs(after - before) > 1e-6:
                moved = True
            vals.append(after)
        if not any_:
            return (0, 1, "no output to fill -- bind the node first")
        lo, hi = round(min(vals) * 100), round(max(vals) * 100)
        return (1 if moved else 0, 0,
                f"fill {lo}%" + ("" if lo == hi else f" to {hi}%"))

    def bind_all(self, sel):
        if not self.outputs:
            return (0, 1, "no output yet -- press o to make one")
        if not sel:
            return (0, 0, "")
        for n in sel:
            fill = self.fraction(n) if self.output_count(n) > 0 else 1.0
            self.links.pop(n, None)
            for o in self.out_names():
                self.bind_one(n, o)
            if fill < 1.0:
                self.set_fraction(n, fill)
        k = len(sel)
        return (1, 0, f"bound {k} node" + ("" if k == 1 else "s")
                + f" to every output ({len(self.outputs)})")

    def clear(self, sel):
        for n in sel:
            self.links.pop(n, None)
        if not sel:
            return (0, 0, "")
        return (1, 0, f"cleared bindings on {len(sel)}")

    def cycle(self):
        if not self.outputs:
            return (0, 0, "")
        names = self.out_names()
        i = -1 if self.current is None else names.index(self.current)
        self.select_only(names[(i + 1) % len(names)])
        return (0, 0, f"current output: {self.current}")

    def pick(self, name):
        if name not in self.out_names():
            return (0, 0, "")
        self.select_only(name)
        return (0, 0, f"current output: {name}")

    def trim(self, db):
        if not self.out_sel:
            return (0, 0, "")
        for o in self.outputs:
            if o[0] in self.out_sel:
                o[2] = min(o[2] * 10 ** (db / 20.0), 3.9810717)
        return (1, 0, "")

    def remove_current(self):
        if self.current is None:
            return (0, 0, "")
        if len(self.out_sel) > 1:
            return (0, 1, "select one output to remove -- "
                    f"{len(self.out_sel)} are selected")
        nm = self.current
        feeders = len({n for links in self.all_links()
                       for n, ls in links.items()
                       if any(l[0] == "out" and l[1] == nm for l in ls)})
        self.remove_output(nm)
        msg = f"removed output {nm}"
        if feeders:
            msg += f" -- {feeders} node" + ("" if feeders == 1 else "s") \
                + " no longer reach it"
        return (1, 1 if feeders else 0, msg)

    # ---- round 3A-6: levels ----------------------------------------------
    #
    # Restated from the plan, by name. New levels are placed halfway to the
    # neighbour, or a quarter of the map beyond the end, so starting heights
    # stay sums of halves and quarters -- exact in float and in double alike.

    def switch_level(self, d):
        n = len(self.levels)
        if n == 1:
            return (0, 0, "one level -- n makes another")
        k = self.cur + d
        if k < 0:
            return (0, 0, "already the bottom level")
        if k >= n:
            return (0, 0, "already the top level")
        self.cur = k
        return (0, 0, f"level {k + 1} of {n}, at "
                f"{fmt(self.levels[k]['base'])}")

    def new_level(self, above):
        import copy
        n, k = len(self.levels), self.cur
        b = self.levels[k]["base"]
        if above:
            base = b + 0.25 if k == n - 1 else (b + self.levels[k + 1]["base"]) * 0.5
            at = k + 1
        else:
            base = b - 0.25 if k == 0 else (self.levels[k - 1]["base"] + b) * 0.5
            at = k
        where = "above" if above else "below"
        shift = base - b
        made = {nm: self.height(k, nm) + shift for nm in self.names()}
        blocked = [nm for nm in self.names()
                   if not ((at == 0 or made[nm] > self.height(at - 1, nm))
                           and (at == n or made[nm] < self.height(at, nm)))]
        if blocked or any(lv["base"] == base for lv in self.levels):
            return (0, 1, f"no room for a new level {where} -- "
                    + (", ".join(blocked) or "a level starts there")
                    + " would reach the level next to it")
        level = dict(base=base, heights={}, links=copy.deepcopy(self.links))
        for nm, h in made.items():
            if h != base:
                level["heights"][nm] = h
        self.levels.insert(at, level)
        self.cur = at
        return (1, 0, f"new level {at + 1} of {n + 1}, at {fmt(base)}")

    def raise_nodes(self, sel, direction, fine):
        """Each node's offset from the level's start steps to the next
        multiple of the step, as fill does. All or nothing."""
        if not sel:
            return (0, 0, "")
        unit = 0.01 if fine else 0.05
        k = self.cur
        base = self.levels[k]["base"]
        new = {}
        for nm in sel:
            q = (self.height(k, nm) - base) / unit
            qr = round(q)
            if abs(q - qr) < 1e-3:
                nxt = qr + direction
            else:
                nxt = math.ceil(q) if direction > 0 else math.floor(q)
            new[nm] = base + nxt * unit
        blocked = [nm for nm in sel if not self.fits(k, nm, new[nm])]
        if blocked:
            return (0, 1, ", ".join(blocked) + " would reach the level "
                    + ("above" if direction > 0 else "below"))
        for nm in sel:
            self.set_height(k, nm, new[nm])
        offs = [new[nm] - base for nm in sel]
        lo, hi = f"{min(offs):+.3g}", f"{max(offs):+.3g}"
        return (1, 0, f"height {lo}" + ("" if lo == hi else f" to {hi}"))

    def remove_level(self):
        n = len(self.levels)
        if n == 1:
            return (0, 1, "a map has at least one level")
        gone = self.cur
        del self.levels[gone]
        self.cur = max(0, gone - 1)
        return (1, 0, f"removed level {gone + 1}; now on level "
                f"{self.cur + 1} of {n - 1}")

    # ---- round 3A-6: listening in three dimensions ------------------------
    #
    # Given the floor plan's weights at a point, as the CONTRIB records give
    # them, and a height z. Restated from the plan, not from Levels.

    def heights_at(self, weights):
        return [sum(w * self.height(k, n) for n, w in weights)
                for k in range(len(self.levels))]

    def where(self, weights, z):
        """(inside, below, t), as Levels::evaluate decides them."""
        n = len(self.levels)
        if n == 1:
            return True, 0, 0.0
        h = self.heights_at(weights)
        if z < h[0] - 1e-6 or z > h[-1] + 1e-6:
            return False, 0, 0.0
        k = 0
        while k + 2 < n and h[k + 1] <= z:
            k += 1
        t = min(1.0, max(0.0, (z - h[k]) / (h[k + 1] - h[k])))
        return True, k, t

    def level_share(self, weights, z):
        """[(level, node, weight)], the 3D weights."""
        inside, k, t = self.where(weights, z)
        if not inside:
            return None
        if len(self.levels) == 1:
            return [(0, nm, w) for nm, w in weights]
        out = []
        for nm, w in weights:
            if t < 1.0:
                out.append((k, nm, w * (1 - t)))
            if t > 0.0:
                out.append((k + 1, nm, w * t))
        return out

    def hear(self, sel, weights, z):
        """What the chart is handed at (weights, z): routed by channel
        before trim, silence, and the selection's part by channel."""
        used = [o[1] for o in self.outputs] + [d[1] for d in self.derived]
        size = (max(used) + 1) if used else 0
        routed, mine = [0.0] * size, [0.0] * size
        share = self.level_share(weights, z)
        if share is None:
            return False, routed, 0.0, (mine if sel else None)
        chan = {o[0]: o[1] for o in self.outputs}
        trim = {o[0]: o[2] for o in self.outputs}
        to_out = {o[0]: 0.0 for o in self.outputs}
        silence = 0.0
        for k, nm, w in share:
            ls = self.levels[k]["links"].get(nm, [])
            total = sum(l[2] for l in ls)
            if total <= 0:
                silence += w          # a null node is silent whole
                continue
            for l in ls:
                part = w * l[2] / total
                if l[0] == "out":
                    to_out[l[1]] += part
                    if nm in sel:
                        mine[chan[l[1]]] += part
                else:
                    silence += part
        for o, v in to_out.items():
            routed[chan[o]] = v
        floor = dict(weights)
        for d in self.derived:
            if self.kind(d) == "after":
                routed[d[1]] = sum(to_out[s[1:]] * trim[s[1:]] for s in d[2])
            else:
                routed[d[1]] = sum(floor.get(s, 0.0) for s in d[2])
        return True, routed, silence, (mine if sel else None)

    def dots(self, weights, z, top, bottom, listening):
        """The column of dots: one per level, top level at the top, evenly
        spaced; and the listening mark, between the dots it lies between,
        or half a space past an end dot when outside."""
        n = len(self.levels)
        if n == 1:
            return None
        gap = (bottom - top) / (n - 1)
        ys = [bottom - k * gap for k in range(n)]
        if not listening:
            return ys, None, False
        inside, k, t = self.where(weights, z)
        if inside:
            return ys, ys[k] + t * (ys[k + 1] - ys[k]), False
        above = z > self.heights_at(weights)[-1]
        return ys, (ys[-1] - gap / 2) if above else (ys[0] + gap / 2), True

    def toggle_auto(self):
        self.auto = not self.auto
        return (0, 0, "auto-output on: each new node gets its own output"
                if self.auto else "auto-output off: new nodes start unbound")

    def example(self):
        self.new_empty()
        for nm, p in [("O", (0.30, 0.50)), ("N", (0.30, 0.80)),
                      ("E", (0.50, 0.50)), ("S", (0.30, 0.20)),
                      ("W", (0.10, 0.50)), ("A", (0.65, 0.35)),
                      ("B", (0.90, 0.35)), ("C", (0.90, 0.65)),
                      ("D", (0.65, 0.65))]:
            self.nodes.append([nm, p])
        self.regions = [("O", "N", "E"), ("O", "E", "S"), ("O", "S", "W"),
                        ("O", "W", "N"), ("A", "B", "C", "D")]
        for i, nm in enumerate(["front", "left", "right", "rear"]):
            self.outputs.append([nm, i, 1.0])
        for n, o in [("N", "front"), ("W", "left"), ("E", "right"),
                     ("S", "rear")]:
            self.bind_one(n, o)
        for o in ["front", "left", "right", "rear"]:
            self.bind_one("O", o)
        self.bind_one("B", "right")
        self.silence_one("B", 1.0)
        self.bind_one("C", "left")
        self.bind_one("C", "right")
        self.bind_one("D", "rear")
        self.silence_one("D", 1.0 / 3.0)
        self.derived = [["sub", 4, ["N", "O"]]]
        self.select_only("front")

    # ---- files -----------------------------------------------------------
    # A pair on "disk" is a snapshot of the map and, depending on the case, of
    # its outputs. The kernel's own suites pin the file FORMATS; this only has
    # to know what a correct round trip must restore.
    def snapshot(self, flat=False):
        """flat: as a file written the 2D way -- the floor plan, and the
        current level's bindings -- by an earlier editor or a test."""
        import copy
        levels = self.levels
        if flat:
            levels = [dict(base=0.0, heights={}, links=self.links)]
        return copy.deepcopy((self.nodes, self.regions, self.outputs,
                              levels, self.derived))

    def restore(self, snap, with_outputs):
        import copy
        n, r, o, lv, d = copy.deepcopy(snap)
        self.nodes, self.regions = n, r
        self.levels, self.cur = lv, 0
        if with_outputs:
            self.outputs, self.derived = o, d
        else:
            self.outputs, self.derived = [], []
            for level in self.levels:
                level["links"] = {}
        self.select_only(None)
        self.settle()

    def link_lines(self, sel):
        names = self.names()
        lines = []
        for n in names:
            ls = self.links.get(n, [])
            total = sum(l[2] for l in ls)
            for l in ls:
                if l[0] != "out" or l[1] not in names or l[1] == n:
                    continue
                if n not in sel and l[1] not in sel:
                    continue
                lines.append([n, l[1], l[2] / total if total > 0 else 0.0,
                              n in sel, 0])
        for a in lines:
            if any(b[0] == a[1] and b[1] == a[0] for b in lines):
                a[4] = 1 if names.index(a[0]) < names.index(a[1]) else -1
        for a in lines:
            a.append(a[4] if a[4] else self.arc_side(a[0], a[1]))
        return lines

    # ---- arcs, restated: distance by DENSE POINT SAMPLING, where the C++
    # measures to sixteen straight pieces. Agreement means the side chosen
    # does not hang on how the arc is measured.
    def canon(self, a, b):
        names = self.names()
        return (a, b) if names.index(a) < names.index(b) else (b, a)

    @staticmethod
    def arc(p, q, side, t):
        cx = (p[0] + q[0]) / 2 - (q[1] - p[1]) * 0.25 * side
        cy = (p[1] + q[1]) / 2 + (q[0] - p[0]) * 0.25 * side
        u = 1 - t
        return (u * u * p[0] + 2 * u * t * cx + t * t * q[0],
                u * u * p[1] + 2 * u * t * cy + t * t * q[1])

    def arc_side(self, a, b):
        lo, hi = self.canon(a, b)
        p, q = self.pos(lo), self.pos(hi)
        clear = {}
        for side in (+1, -1):
            pts = [self.arc(p, q, side, k / 400) for k in range(401)]
            clear[side] = min((min(math.dist(x, s) for s in pts)
                               for n, x in self.nodes if n not in (a, b)),
                              default=1e9)
        return -1 if clear[-1] > clear[+1] + 1e-4 else +1

    def link_point(self, a, b, bend, t):
        lo, hi = self.canon(a, b)
        return self.arc(self.pos(lo), self.pos(hi), bend,
                        t if lo == a else 1 - t)

    def contributions(self, sel, weights):
        used = [o[1] for o in self.outputs] + [d[1] for d in self.derived]
        out = [0.0] * ((max(used) + 1) if used else 0)
        chan = {o[0]: o[1] for o in self.outputs}
        for n, w in weights:
            if n not in sel:
                continue
            ls = self.links.get(n, [])
            total = sum(l[2] for l in ls)
            if total <= 0:
                continue
            for l in ls:
                if l[0] == "out":
                    out[chan[l[1]]] += w * l[2] / total
        return out

    def taps(self, sel):
        return [d[0] for d in self.derived if any(s in sel for s in d[2])]

    def fill_marks(self, sel):
        marks = []
        for n in sel:
            f = self.fraction(n)
            for l in self.links.get(n, []):
                if l[0] == "out":
                    marks.append((l[1], n, f))
        return marks

    # ---- the keys --------------------------------------------------------
    def bind_preview(self, sel):
        """What b would do, found by DOING it on a copy and describing the
        difference. The C++ predicts without doing; agreement checks the
        prediction."""
        import copy
        if not self.out_sel or not sel:
            return False, "bind or unbind selected"
        trial = copy.deepcopy(self)
        before = {n: self.output_count(n) for n in sel}
        res = trial.bind(sel)
        unbind = res[2].startswith("unbound")
        changed = [n for n in sel
                   if sorted(self.links.get(n, [])) != sorted(trial.links.get(n, []))]
        silenced = [n for n in sel if before[n] > 0 and trial.output_count(n) == 0]
        t = (("unbind " if unbind else "bind ") + str(len(changed))
             + (" from " if unbind else " to ") + ", ".join(self.out_sel))
        if silenced:
            who = silenced[0] if len(silenced) == 1 else f"{len(silenced)} nodes"
            t += f" -- {who} " + ("goes silent" if len(silenced) == 1
                                   else "go silent")
        return True, t

    def keymap(self, sel, can_undo, can_redo, drag, aud, grid):
        """[(id, available)], in panel order, and b's action. The rules,
        restated from the plan: while dragging only free-hand and the window
        keys; while auditioning only the mouse editing actions dim."""
        nodes, outs, picked = len(self.nodes), len(self.outputs), len(self.out_sel)
        mouse = not aud and not drag
        any_out = any(self.output_count(n) > 0 for n in sel)
        any_bound = any(self.links.get(n) for n in sel)
        region_sel = any(all(n in sel for n in r) for r in self.regions)
        grid_on = grid != "free"
        possible, preview = self.bind_preview(sel)
        nd = not drag
        rules = [
            ("place", mouse), ("box", mouse and nodes > 0),
            ("toggle", mouse and nodes > 0), ("all", nd and nodes > 0),
            ("none", nd and bool(sel)),
            ("move", mouse and nodes > 0), ("freehand", drag and grid_on),
            ("join", nd and len(sel) >= 3), ("unjoin", nd and region_sel),
            ("remove", nd and bool(sel)),
            ("gridkind", nd), ("rotate", nd and grid_on),
            ("spacing", nd and grid_on), ("spokes", nd and grid == "polar"),
            ("deadzone", nd and grid_on),
            ("newoutput", nd), ("pickoutput", nd and outs > 0),
            ("addoutput", nd and outs >= 2), ("nextoutput", nd and outs >= 2),
            ("bind", nd and possible), ("bindall", nd and outs > 0 and bool(sel)),
            ("fill", nd and any_out), ("clear", nd and bool(any_bound)),
            ("derivedafter", nd and picked > 0),
            ("derived", nd and bool(sel)), ("trim", nd and picked > 0),
            ("removeoutput", nd and picked == 1), ("autooutput", nd),
            # round 3A-6: while auditioning, up and down move the listening
            # point, so they work with one level too
            ("levelswitch", nd and (aud or len(self.levels) > 1)),
            ("newlevel", nd), ("raise", nd and bool(sel)),
            ("removelevel", nd and len(self.levels) > 1), ("view3d", True),
            ("audition", nd), ("lock", nd),
            ("undo", nd and can_undo), ("redo", nd and can_redo),
            ("save", nd), ("saveas", nd), ("open", nd),
            ("userig", nd), ("saverig", nd and outs > 0), ("empty", nd),
            ("example", nd), ("fullscreen", True), ("textsize", True),
        ]
        return rules, preview

    # ---- the fader layout ----------------------------------------------
    def chart(self, x0, top, w, h, cw=8.0, lh=13.0):
        """Fader geometry, restated from the rules. Every used channel and
        silence get a full bar; an unused channel a 4-pixel sliver; a gap
        before each item, after the last, and once more before silence; a
        column 16 characters wide kept at the right for the total. Gaps are
        8 until a bar would be narrower than 32, then a quarter of a bar."""
        if not self.outputs and not self.derived:
            return {"empty": True}
        owner = {}
        for o in self.outputs:
            owner[o[1]] = ("output", o[0], o[0])
        for d in self.derived:
            owner.setdefault(d[1], ("derived", d[0], None))
        channels = max(owner) + 1
        kinds = [owner.get(ch, ("unused", "", None)) for ch in range(channels)]
        room = w - 16 * cw
        full = 1 + sum(k[0] != "unused" for k in kinds)
        thin = channels - (full - 1)
        ngaps = full + thin + 2
        bw, gap = (room - 4 * thin - 8 * ngaps) / full, 8.0
        if bw < 32:
            bw = (room - 4 * thin) / (full + ngaps / 4)
            gap = max(1.0, bw / 4)
        bw = min(46.0, max(2.0, bw))
        room_for_label = bw + gap - 1
        x, bars = x0 + gap, []
        for ch, (kind, name, out) in enumerate(kinds):
            width = min(4.0, bw) if kind == "unused" else bw
            bars.append(dict(ch=ch, kind=kind, x=x, w=width, name=name,
                             out=out, label="", sub=""))
            x += width + gap
        sx = x + gap
        # Labels for the whole row: names on all, else numbers on all, else
        # a ruler -- the smallest step whose marks each fit before the next.
        used = [b for b in bars if b["kind"] != "unused"]
        fit = lambda t, room=room_for_label: len(t) * cw <= room
        all_names = all(fit(b["name"]) for b in used)
        all_numbers = all(fit(str(b["ch"])) for b in used)
        step = 0
        if all_names or all_numbers:
            for b in used:
                if all_names:
                    b["label"] = b["name"]
                    b["sub"] = str(b["ch"]) if all_numbers else ""
                else:
                    b["label"] = str(b["ch"])
        else:
            for k in (2, 5, 10, 20, 50, 100, 200, 500, 1000):
                marks = [i for i, b in enumerate(bars) if b["ch"] % k == 0]
                def mark_room(i):
                    end = bars[i + k]["x"] if i + k < len(bars) else sx
                    return end - bars[i]["x"] - 1
                if all(fit(str(bars[i]["ch"]), mark_room(i)) for i in marks):
                    step = k
                    for i in marks:
                        bars[i]["label"] = str(bars[i]["ch"])
                    break
        sl = next((o for o in ("silent", "sil")
                   if len(o) * cw <= room_for_label), "")
        lay = dict(empty=False, bw=bw, gap=gap, sx=sx, sl=sl, step=step,
                   tx=x0 + room + 8, bars=bars, top=top, h=h, x0=x0, w=w,
                   cw=cw, base=top + h - (2 * lh + 4))
        # The point of the reserved column: whatever the outputs, the bars
        # end before it and the total ends inside the chart. Asserted here,
        # so a layout that broke it would not even generate its vectors.
        if bw > 2.0:
            assert sx + bw <= x0 + room + 1e-3, ("bars overrun", sx + bw)
        assert lay["tx"] + 16 * cw - 8 <= x0 + w + 1e-3
        return lay

    @staticmethod
    def hover(lay, x, y):
        if lay["empty"] or not (lay["top"] <= y <= lay["top"] + lay["h"]):
            return "none"
        half = lay["gap"] / 2
        for b in lay["bars"]:
            if b["x"] - half <= x <= b["x"] + b["w"] + half:
                return f"bar:{b['ch']}"
        if lay["sx"] - half <= x <= lay["sx"] + lay["bw"] + half:
            return "silence"
        return "none"

    def output_at(self, lay, x, y):
        h = self.hover(lay, x, y)
        if not h.startswith("bar:"):
            return None
        b = lay["bars"][int(h[4:])]
        return b["out"] if b["kind"] == "output" else None

    # ---- the state, canonically ----------------------------------------
    def dump(self):
        parts = ["N " + " ".join(f"{n}@{fmt(p[0])},{fmt(p[1])}"
                                 for n, p in self.nodes),
                 "R " + " ".join(",".join(r) for r in self.regions),
                 "O " + " ".join(f"{o[0]}:{o[1]}:{fmt(o[2])}"
                                 for o in self.outputs),
                 "L " + " ".join(
                     f"{n}>{l[1] if l[0] == 'out' else '~'}:{fmt(l[2])}"
                     for n in self.names() for l in self.links.get(n, [])),
                 "D " + " ".join(f"{d[0]}:{d[1]}:{','.join(d[2])}"
                                 for d in self.derived),
                 "C " + (self.current or "-"),
                 "S " + (",".join(self.out_sel) or "-"),
                 "A " + ("1" if self.auto else "0")]
        if self.three_d():
            # Round 3A-6: the current level, then every level bottom first:
            # its starting height, its bent nodes' heights, its bindings.
            parts.append(f"V {self.cur}")
            for k, lv in enumerate(self.levels):
                parts.append(
                    f"Z{k} {fmt(lv['base'])}"
                    + "".join(f" {n}={fmt(lv['heights'][n])}"
                              for n in self.names() if n in lv["heights"])
                    + " K" + "".join(
                        f" {n}>{l[1] if l[0] == 'out' else '~'}:{fmt(l[2])}"
                        for n in self.names() for l in lv["links"].get(n, [])))
        return " | ".join(parts)


# ---------------------------------------------------------------------------
# scripts
# ---------------------------------------------------------------------------

S = 0.05


LABELS = {"place": "place", "join": "join", "unjoin": "unjoin",
          "remove": "delete", "newoutput": "new output",
          "derived": "derived output", "bind": "binding",
          "bindall": "bind to every output", "fill": "fill",
          "clear": "clear bindings", "trim": "trim",
          "removeoutput": "remove output", "example": "example map",
          "empty": "new map", "move": "move", "openpair": "open",
          "derivedafter": "derived output", "userig": "use rig",
          "newlevel": "new level", "raise": "height",
          "removelevel": "remove level"}


class Script:
    def __init__(self, out, name, note, grid=None, spec="FREE", limit=200):
        self.out, self.ed = out, Editor()
        self.past, self.future, self.limit = [], [], limit
        self.disk = {}           # base name -> (snapshot, outputs state)
        self.rigs = {}           # base name -> rig
        self.grid = grid or G.Free()
        self.sel, self.ordered = [], True
        out.append(f"# {note}")
        out.append(f"EDIT {name} ANALYTIC")
        out.append(f"GRID {spec}")
        if limit != 200:
            out.append(f"HISTORY {limit}")

    def do(self, op, *args):
        if op in ("undo", "redo"):
            return self.history(op)
        import copy
        before = copy.deepcopy(self.ed)
        res = self._do(op, *args)
        # Recorded only if something changed, exactly as History::apply.
        if res[0]:
            self.past.append((before, LABELS[op]))
            if len(self.past) > self.limit:
                self.past.pop(0)
            self.future = []
        return res

    def history(self, op):
        import copy
        src, dst = (self.past, self.future) if op == "undo" \
            else (self.future, self.past)
        if not src:
            res = (0, 1, f"nothing to {op}")
        else:
            state, label = src.pop()
            dst.append((copy.deepcopy(self.ed), label))
            self.ed = state
            res = (1, 0, f"{'undid' if op == 'undo' else 'redid'} {label}")
        self.out.append(f"DO {op}")
        self.out.append(f'EXPECT {res[0]} {res[1]} "{res[2]}"')
        self.out.append(f"STATE {self.ed.dump()}")
        return res

    def _do(self, op, *args):
        e, s = self.ed, self.sel
        res = {
            "place":    lambda: e.place(args[0], self.grid),
            "join":     lambda: e.join(s, self.ordered),
            "unjoin":   lambda: e.unjoin(s),
            "remove":   lambda: e.remove(s),
            "newoutput": e.new_output, "derived": lambda: e.new_derived(s),
            "derivedafter": e.new_derived_after,
            "saverig":  lambda: self.save_rig(args[0]),
            "userig":   lambda: self.use_rig(args[0]),
            "bind":     lambda: e.bind(s),
            "bindall":  lambda: e.bind_all(s),
            "fill":     lambda: e.step_fill(s, args[0], args[1]),
            "move":     lambda: e.move(args[0], args[1]),
            "savepair": lambda: self.save_pair(args[0]),
            "openpair": lambda: self.open_pair(args[0]),
            "legacypair": lambda: self.write_as(args[0], "legacy"),
            "maponly":  lambda: self.write_as(args[0], None),
            "badoutputs": lambda: self.write_as(args[0], "bad"),
            "clear":    lambda: e.clear(s), "cycle": e.cycle,
            "trim":     lambda: e.trim(args[0]),
            "pick":     lambda: e.pick(args[0]),
            "toggleout": lambda: e.toggle_output(args[0]),
            "removeoutput": e.remove_current, "auto": e.toggle_auto,
            "levelup":  lambda: e.switch_level(+1),
            "leveldown": lambda: e.switch_level(-1),
            "newlevel": lambda: e.new_level(args[0] > 0),
            "raise":    lambda: e.raise_nodes(s, args[0], args[1]),
            "removelevel": e.remove_level,
            "example":  lambda: (e.example(), (1, 0, ""))[1],
            "empty":    lambda: (e.new_empty(), (1, 0, ""))[1],
        }[op]()
        argtxt = ""
        if op == "place":
            argtxt = f" {fmt(args[0][0])} {fmt(args[0][1])}"
        elif op == "trim":
            argtxt = f" {fmt(args[0])}"
        elif op in ("pick", "toggleout"):
            argtxt = f" {args[0]}"
        elif op in ("fill", "raise"):
            argtxt = f" {args[0]} {1 if args[1] else 0}"
        elif op == "newlevel":
            argtxt = f" {args[0]}"
        elif op in ("savepair", "openpair", "legacypair", "maponly",
                    "badoutputs", "saverig", "userig"):
            argtxt = f" {args[0]}"
        elif op == "move":
            argtxt = f" {args[0]} {fmt(args[1][0])} {fmt(args[1][1])}"
        self.out.append(f"DO {op}{argtxt}")
        self.out.append(f'EXPECT {res[0]} {res[1]} "{res[2]}"')
        self.out.append(f"STATE {e.dump()}")
        if op == "remove":           # the app clears after a removal
            self.sel, self.ordered = [], True
            self.out.append("SEL clear")
        return res

    def chart(self, x0, top, w, h, probes=(), cw=8.0, lh=13.0):
        lay = self.ed.chart(x0, top, w, h, cw, lh)
        self.out.append(f"CHART {fmt(x0)} {fmt(top)} {fmt(w)} {fmt(h)} "
                        f"{fmt(cw)} {fmt(lh)}")
        if lay["empty"]:
            self.out.append("EXPECTCHART EMPTY")
        else:
            d = lambda v: v if v else "-"
            bars = " ".join(f"{b['ch']}:{b['kind']}:{fmt(b['x'])}:"
                            f"{fmt(b['w'])}:{d(b['label'])}:{d(b['sub'])}"
                            for b in lay["bars"])
            self.out.append(f"EXPECTCHART {fmt(lay['gap'])} {fmt(lay['bw'])} "
                            f"{fmt(lay['sx'])} {d(lay['sl'])} "
                            f"{fmt(lay['tx'])} {fmt(lay['base'])} "
                            f"{lay['step']} {bars}")
        # Probes name a place in the reference's OWN layout -- the middle of
        # channel 3, the edge of its hit area -- and become coordinates. The
        # C++ lays out independently, so a disagreement lands a probe wrong.
        y_in = top + h / 2
        for p in probes:
            if lay["empty"]:
                x, y = x0 + 20, y_in
            else:
                bar = lambda ch: lay["bars"][ch]
                half = lay["gap"] / 2
                x, y = {
                    "mid": lambda: (bar(p[1])["x"] + bar(p[1])["w"] / 2, y_in),
                    "edge": lambda: (bar(p[1])["x"] + bar(p[1])["w"]
                                     + half - 0.25, y_in),
                    "below": lambda: (bar(p[1])["x"] + bar(p[1])["w"] / 2,
                                      top + h + 5),
                    "silence": lambda: (lay["sx"] + lay["bw"] / 2, y_in),
                    "before": lambda: (x0 + half - 1, y_in),
                }[p[0]]()
            hit = None if lay["empty"] else self.ed.output_at(lay, x, y)
            hov = "none" if lay["empty"] else self.ed.hover(lay, x, y)
            self.out.append(f"CLICK {fmt(x)} {fmt(y)} {hit or '-'}")
            self.out.append(f"HOVER {fmt(x)} {fmt(y)} {hov}")

    def chart_of(self, outputs, x0, top, w, h, cw=8.0, lh=13.0):
        """A chart for an outputs list given directly, as name:channel --
        for rows the editing operations reach only in hundreds of steps."""
        e = Editor()
        e.outputs = [[n, ch, 1.0] for n, ch in outputs]
        lay = e.chart(x0, top, w, h, cw, lh)
        d = lambda v: v if v else "-"
        self.out.append(f"CHARTMAP {fmt(x0)} {fmt(top)} {fmt(w)} {fmt(h)} "
                        f"{fmt(cw)} {fmt(lh)} OUT "
                        + " ".join(f"{n}:{ch}" for n, ch in outputs))
        bars = " ".join(f"{b['ch']}:{b['kind']}:{fmt(b['x'])}:"
                        f"{fmt(b['w'])}:{d(b['label'])}:{d(b['sub'])}"
                        for b in lay["bars"])
        self.out.append(f"EXPECTCHART {fmt(lay['gap'])} {fmt(lay['bw'])} "
                        f"{fmt(lay['sx'])} {d(lay['sl'])} {fmt(lay['tx'])} "
                        f"{fmt(lay['base'])} {lay['step']} {bars}")

    @staticmethod
    def base(chosen):
        for tail in ("-outputs.json", "-mapping.json", ".json"):
            if chosen.endswith(tail):
                return chosen[:-len(tail)]
        return chosen

    @staticmethod
    def rig_base(chosen):
        for tail in ("-rig.json", ".json"):
            if chosen.endswith(tail):
                return chosen[:-len(tail)]
        return chosen

    def save_rig(self, chosen):
        import copy
        b = self.rig_base(chosen)
        self.rigs[b] = copy.deepcopy(self.ed.rig())
        name = b.replace("\\", "/").split("/")[-1]
        return (0, 0, f"saved rig {name}-rig.json")

    def use_rig(self, chosen):
        b = self.rig_base(chosen)
        name = b.replace("\\", "/").split("/")[-1]
        if b not in self.rigs:
            return (0, 1, f"cannot open {name}-rig.json")
        return self.ed.use_rig(self.rigs[b], name)

    def save_pair(self, chosen):
        b = self.base(chosen)
        self.disk[b] = (self.ed.snapshot(), "ok")
        return (0, 0, f"saved {b}.json and {b}-outputs.json")

    def write_as(self, chosen, outputs):
        """A file written some other way: by an earlier editor ("legacy"),
        with no outputs file (None), or with an outputs file that is
        refused ("bad")."""
        self.disk[self.base(chosen)] = (self.ed.snapshot(flat=True), outputs)
        return (0, 0, "")

    def open_pair(self, chosen):
        b = self.base(chosen)
        if b not in self.disk:
            return (0, 1, f"cannot open {b}.json")
        snap, outs = self.disk[b]
        good = outs in ("ok", "legacy")
        self.ed.restore(snap, good)
        e = self.ed
        pl = lambda k, one, many: f"{k} {one if k == 1 else many}"
        msg = (f"opened {b}: {pl(len(e.nodes), 'node', 'nodes')}, "
               f"{pl(len(e.regions), 'region', 'regions')}, "
               f"{pl(len(e.outputs), 'output', 'outputs')}"
               + (f", {len(e.levels)} levels" if len(e.levels) > 1 else ""))
        if outs is None:
            return (1, 0, msg + " (no outputs file)")
        if outs == "bad":
            # The parser's own words follow; the runner matches the prefix.
            return (1, 1, msg + " -- outputs file refused: *")
        return (1, 0, msg)

    def pair(self, chosen):
        b = self.base(chosen)
        name = b.replace("\\", "/").split("/")[-1]
        self.out.append(f"PAIR {chosen} MAP {b}.json OUT {b}-outputs.json "
                        f"LEGACY {b}-mapping.json NAME {name}")

    def keys(self, drag=False, aud=False, grid="lattice"):
        rules, preview = self.ed.keymap(self.sel, bool(self.past),
                                        bool(self.future), drag, aud, grid)
        self.out.append(f"KEYS drag={int(drag)} aud={int(aud)} grid={grid} "
                        "EXPECT " + " ".join(f"{i}:{int(a)}" for i, a in rules)
                        + " BIND " + preview)

    def lines(self):
        ls = self.ed.link_lines(self.sel)
        def at(a, b, bd):
            x, y = self.ed.link_point(a, b, bd, 0.25)
            return f"{fmt(x)},{fmt(y)}"
        self.out.append("LINKS " + (" ".join(
            f"{a}>{b}:{fmt(sh)}:{1 if hv else 0}:{ln}:{bd}:{at(a, b, bd)}"
            for a, b, sh, hv, ln, bd in ls) or "NONE"))

    def contrib(self, weights):
        c = self.ed.contributions(self.sel, weights)
        self.out.append("CONTRIB " + " ".join(f"{n}={fmt(w)}"
                                              for n, w in weights)
                        + " EXPECT " + (" ".join(fmt(x) for x in c) or "NONE"))

    def hear(self, weights, z, top=0.0, bottom=90.0, listening=True):
        """Round 3A-6: what the chart is handed at the floor weights and
        height z, and where the column of dots puts each level and the
        listening mark."""
        e = self.ed
        inside, routed, sil, mine = e.hear(self.sel, weights, z)
        col = e.dots(weights, z, top, bottom, listening)
        if col is None:
            dots = "NONE"
        else:
            ys, mark, outside = col
            dots = (" ".join(fmt(y) for y in ys) + f" CUR {e.cur} MARK "
                    + ("-" if mark is None else
                       f"{fmt(mark)} {1 if outside else 0}"))
        self.out.append(
            "HEAR " + " ".join(f"{n}={fmt(w)}" for n, w in weights)
            + f" Z {fmt(z)} COL {fmt(top)} {fmt(bottom)} {int(listening)}"
            + f" EXPECT {int(inside)} ROUTED "
            + (" ".join(fmt(x) for x in routed) or "-")
            + f" SILENCE {fmt(sil)} SEL "
            + ("-" if mine is None else (" ".join(fmt(x) for x in mine) or "-"))
            + f" DOTS {dots}")

    def start_z(self, weights):
        """Where audition starts: the current level, under the point."""
        z = self.ed.heights_at(weights)[self.ed.cur]
        self.out.append("STARTZ " + " ".join(f"{n}={fmt(w)}" for n, w in weights)
                        + f" EXPECT {fmt(z)}")
        return z

    def glide(self, z, direction, fine, seconds):
        """Holding up or down while auditioning: 0.25 a second, or 0.05
        with shift. Passing a level is nothing special."""
        z2 = z + direction * (0.05 if fine else 0.25) * seconds
        self.out.append(f"GLIDE {fmt(z)} {direction} {int(fine)} {fmt(seconds)}"
                        f" EXPECT {fmt(z2)}")
        return z2

    def taps(self):
        self.out.append("TAPS " + (" ".join(self.ed.taps(self.sel)) or "NONE"))

    def marks(self):
        m = self.ed.fill_marks(self.sel)
        self.out.append("FILLMARKS " + (" ".join(
            f"{o}:{n}:{fmt(f)}" for o, n, f in m) or "NONE"))

    def select(self, *names):
        self.sel, self.ordered = [names[0]], True
        self.out.append(f"SEL only {names[0]}")
        for n in names[1:]:
            self.toggle(n)

    def toggle(self, n):
        if n in self.sel:
            self.sel.remove(n)
        else:
            self.sel.append(n)
        self.out.append(f"SEL toggle {n}")

    def box(self, *names):
        self.sel, self.ordered = [], True
        self.out.append("SEL clear")
        for n in names:
            if n not in self.sel:
                self.sel.append(n)
                self.ordered = False
        self.out.append("SEL box " + " ".join(names))

    def clear_sel(self):
        self.sel, self.ordered = [], True
        self.out.append("SEL clear")

    def end(self):
        self.out.append("ENDEDIT")
        self.out.append("")


def build():
    out = ["# ofxManifoldEditor model conformance vectors",
           "# GENERATED by tests/ref/reference_editor.py -- do not hand edit",
           "#",
           "# Scripts: every operation followed by the complete state. The",
           "# reference tracks everything by NAME and renumbers nothing; the",
           "# C++ tracks ids and renumbers on every delete.",
           "", "TOL 1e-5", ""]

    t = Script(out, "place_auto_output",
               "placing nodes, free-hand: each gets its own output, on the "
               "next free channel, and becomes the current output")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("place", (0.5, 0.8))
    t.do("place", (0.2004, 0.2))           # within 0.01 of n0: occupied
    t.do("place", (1.3, 0.5))              # off the map: nothing at all
    t.end()

    sq = G.Lattice((S, 0), (0, S))
    t = Script(out, "place_on_grid",
               "on a square grid the node lands on the grid point, and a "
               "second click at the same address is refused", sq, sq.spec())
    t.do("place", (0.123, 0.337)); t.do("place", (0.11, 0.34))
    t.do("place", (0.3, 0.3))
    t.end()

    t = Script(out, "auto_output_off",
               "with auto-output off a node starts unbound and no output "
               "appears")
    t.do("auto"); t.do("place", (0.3, 0.3)); t.do("bind")
    t.do("auto"); t.do("place", (0.6, 0.3))
    t.end()

    t = Script(out, "join_orders",
               "joining: fewer than three refused; click order kept; a box "
               "joins in angle order; a set already joined is refused; a "
               "click order that crosses itself is refused WITH the reason")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.8, 0.8), (0.2, 0.8)]:
        t.do("place", p)
    t.select("n0", "n1"); t.do("join")
    t.select("n0", "n2", "n1", "n3"); t.do("join")
    t.select("n0", "n1", "n2", "n3"); t.do("join")
    t.box("n3", "n1", "n0", "n2"); t.do("join")
    t.box("n0", "n1", "n2"); t.do("join")
    t.end()

    t = Script(out, "box_join_orders_by_angle",
               "a box over four fresh corners, caught in CROSSING order. "
               "Angle order makes the square; the selection's own order would "
               "cross itself. The earlier box join landed on a set already "
               "joined, so the duplicate check answered before ordering was "
               "consulted, and ordering went untested -- one step exercising "
               "two checks proves neither, as with the bowties and D-022")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.8, 0.8), (0.2, 0.8)]:
        t.do("place", p)
    t.box("n0", "n2", "n1", "n3"); t.do("join")
    t.select("n0", "n2", "n1", "n3"); t.do("unjoin")
    t.select("n0", "n2", "n1", "n3"); t.do("join")
    t.end()

    t = Script(out, "current_output_follows_renumbering",
               "removing an output that comes BEFORE the current one: output "
               "ids after it shift down, so the current output must shift "
               "with them and still name the same output. Every earlier "
               "removal hit the current output or one after it")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.do("pick", "n2")                      # explicit: the current output is n2
    t.select("n0"); t.do("remove")          # removes output n0, before n2
    t.select("n1"); t.do("bind")            # binds to n2, the same output
    t.end()

    t = Script(out, "remove_and_outputs",
               "deleting a node takes its own output only if nothing else "
               "feeds it; renumbering must leave every other binding on its "
               "own node")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8), (0.5, 0.45)]:
        t.do("place", p)
    t.select("n0", "n1", "n2"); t.do("join")
    # Pick the output explicitly. This step used "cycle", relying on the newest
    # node's output being current; when placement stopped moving the current
    # output, cycle landed elsewhere, b UNBOUND instead, and the script still
    # passed while no longer testing a surviving output at all.
    t.do("pick", "n0")
    t.select("n1"); t.do("bind")            # n1 now also feeds n0's output
    t.select("n0"); t.do("remove")          # n0's output survives
    t.select("n2"); t.do("remove")          # n2's output goes with it
    t.select("n3"); t.do("bind")            # must land on n3, renumbered
    t.do("place", (0.3, 0.3))               # name must avoid output n0
    t.end()

    t = Script(out, "fresh_name_avoids_outputs",
               "an output that outlives its node keeps its name reserved: "
               "a new node skips it rather than being left unbound")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("pick", "n1")                     # explicit, not a placement side effect
    t.select("n0"); t.do("bind")           # n0 now also feeds n1's output
    t.select("n1"); t.do("remove")         # output n1 survives
    t.do("place", (0.5, 0.8))              # must be n2, not n1
    t.end()

    t = Script(out, "unjoin",
               "unjoin removes only regions made entirely of selected nodes, "
               "keeping the nodes")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8), (0.9, 0.9)]:
        t.do("place", p)
    t.select("n0", "n1", "n2"); t.do("join")
    t.select("n0", "n1", "n3"); t.do("join")
    t.select("n0", "n3"); t.do("unjoin")
    t.select("n0", "n1", "n2"); t.do("unjoin")
    t.end()

    t = Script(out, "outputs",
               "outputs: new, derived, bind, silence, clear, cycle, trim, "
               "remove. Round 2 replaced the quarter share of silence per press "
               "with fill steps, and this vector changed with it")
    t.do("auto")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.select("n0"); t.do("bind")            # no outputs yet: refused
    t.do("newoutput"); t.do("bind")
    t.do("newoutput"); t.select("n0", "n1"); t.do("bind")
    t.select("n0"); t.do("fill", -1, False); t.do("fill", -1, False)
    t.do("fill", -1, True); t.do("fill", +1, True); t.do("fill", +1, False)
    t.select("n1"); t.do("clear"); t.do("fill", -1, False)
    t.clear_sel(); t.do("derived")          # nothing selected: refused
    t.select("n0", "n1"); t.do("derived")
    t.do("cycle"); t.do("trim", -1.0); t.do("trim", 20.0)
    # Pick an output that is NOT already current. The first version picked
    # out0 while out0 was current, so a pick that did nothing left the state
    # exactly as expected and the step could not fail.
    t.do("pick", "out1"); t.do("pick", "nothing")
    t.do("removeoutput")
    t.do("newoutput")
    t.end()

    t = Script(out, "example",
               "the example map, and its outputs, exactly")
    t.do("example")
    # The window as it was: 1024 x 640, chart 118 high from y 502.
    t.chart(24, 502, 976, 118,
            probes=[("mid", 1),         # left's bar
                    ("mid", 4),         # the derived sub: hovered, not picked
                    ("silence",),       # hovered, not picked
                    ("below", 0),       # under the chart: nothing
                    ("edge", 0),        # in front's half of the gap: front
                    ("before",)])       # left of the first bar: nothing
    t.select("B"); t.do("fill", +1, False)
    t.do("empty")
    t.chart(24, 502, 976, 118, probes=[("mid", 0)])
    t.end()

    t = Script(out, "fill_same_step_every_shape",
               "THE REPORTED PROBLEM: a quarter share of silence stepped a "
               "triangle feeding four outputs far less than a circle feeding "
               "one. A fill step now moves every shape by exactly 5%")
    t.do("example")
    t.select("O"); t.do("fill", -1, False)     # the triangle: 100% -> 95%
    t.select("N"); t.do("fill", -1, False)     # a circle:     100% -> 95%
    t.select("C"); t.do("fill", -1, False)     # two outputs:  100% -> 95%
    t.end()

    t = Script(out, "fill_steps_land_on_round_numbers",
               "fine steps of 1%, then a coarse step lands on the next "
               "multiple of 5 in its direction: 97% down gives 95%, not 92%. "
               "A step from exactly 95% moves a whole step. The floor is 1% "
               "and the ceiling 100%; a step at either end changes nothing")
    t.do("place", (0.5, 0.5))
    t.select("n0")
    t.do("fill", -1, True); t.do("fill", -1, True); t.do("fill", -1, True)
    t.do("fill", -1, False)                     # 97% -> 95%
    t.do("fill", -1, False)                     # 95% -> 90%
    t.do("fill", +1, True); t.do("fill", +1, False)   # 91% -> 95%
    t.do("fill", +1, False); t.do("fill", +1, False)  # 100%, then 100% again
    for _ in range(22):
        t.do("fill", -1, False)                 # down past 5% to the floor
    t.do("fill", -1, True)                      # 1% stays 1%
    t.end()

    t = Script(out, "bind_to_every_output",
               "shift-B binds a node to EVERY output equally, replacing its "
               "output bindings rather than adding to them -- adding would "
               "feed the output it already had double -- and keeps its fill")
    t.select("n0"); t.do("bindall")             # nothing to bind to: refused
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n0"); t.do("bindall")
    t.do("fill", -1, False); t.do("fill", -1, False)   # 90%
    t.do("bindall")                             # still 90%, still equal
    t.select("n1", "n2"); t.do("bindall")
    t.end()

    t = Script(out, "outputs_selected_together",
               "shift-click selects several faders: b binds or unbinds every "
               "selected node to EVERY selected output, t and T trim them "
               "all, and removing an output needs exactly one selected. The "
               "set follows removals like the current output does")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8), (0.5, 0.45)]:
        t.do("place", p)                    # outputs n0..n3, current n0
    t.do("pick", "n1"); t.do("toggleout", "n2")
    t.select("n3"); t.do("bind")            # n3 -> n1 and n2 together
    t.do("bind")                            # every pair present: unbinds
    t.do("bind")
    t.do("trim", -3.0)                      # n1 and n2 both
    t.do("removeoutput")                    # two selected: refused
    t.do("toggleout", "n2")                 # back to n1 alone
    t.do("toggleout", "n1")                 # the last one stays
    t.do("toggleout", "n2"); t.do("pick", "n2"); t.do("toggleout", "n1")
    t.select("n2"); t.do("bind")            # n2 -> n1 as well (n2 has n2)
    t.select("n1"); t.do("remove")          # n1's output survives: n2 feeds it
    t.select("n0"); t.do("remove")          # output n0 goes; the set shifts
    t.end()

    t = Script(out, "link_lines",
               "the lines drawn on the map: only between two nodes, each "
               "link ONCE, heavy when its sender is selected and light when "
               "only its receiver is, side by side when two nodes feed each "
               "other. A line's share counts silence: a node's lines, plus "
               "its own output (no line), plus its silence, add up to 100%")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8), (0.9, 0.9)]:
        t.do("place", p)
    t.do("pick", "n1"); t.select("n0"); t.do("bind")      # n0 -> n1
    t.do("pick", "n0"); t.select("n1"); t.do("bind")      # n1 -> n0: mutual
    t.do("pick", "n2"); t.select("n0"); t.do("bind")      # n0 -> n2
    t.select("n0"); t.do("fill", -1, False); t.do("fill", -1, False)
    t.select("n0"); t.lines()               # out of n0, and n1 into it
    t.select("n0", "n1"); t.lines()         # the mutual pair, both heavy
    t.select("n2"); t.lines()               # only into n2: light
    t.select("n3"); t.lines()               # its own output only: none
    t.clear_sel(); t.lines()
    t.select("n0")
    t.contrib([("n0", 0.5), ("n1", 0.3), ("n2", 0.2)])
    t.select("n0", "n1")
    t.contrib([("n0", 0.5), ("n1", 0.3), ("n2", 0.2)])
    t.end()

    t = Script(out, "link_arcs_avoid_nodes",
               "a link arcs to the side with more CLEARANCE from other "
               "nodes, so a line never looks as if it connects to a node it "
               "passes. n2 sits just above the straight path from n0 to n1, "
               "so that line arcs BELOW; n2 to n3 has nothing near either "
               "side and takes +1; a mutual pair arcs to opposite sides. The "
               "point a quarter of the way from the sender is where its "
               "percentage goes")
    for p in [(0.2, 0.5), (0.8, 0.5), (0.5, 0.52), (0.9, 0.9)]:
        t.do("place", p)
    t.do("pick", "n1"); t.select("n0"); t.do("bind")      # n0 -> n1, past n2
    t.select("n0"); t.lines()
    t.do("pick", "n0"); t.select("n1"); t.do("bind")      # n1 -> n0: mutual
    t.select("n0"); t.lines()
    t.do("pick", "n3"); t.select("n2"); t.do("bind")      # n2 -> n3, clear
    t.select("n2"); t.lines()
    t.end()

    t = Script(out, "link_arcs_nearest_not_count",
               "TWO nodes a comfortable 0.028 from the upper arc, ONE just "
               "0.01 from the lower. Clearance sends the line ABOVE, past the "
               "pair: counting nodes would have sent it within a hair of the "
               "single one. Built so the two rules disagree, which the first "
               "fixture did not -- there both sides counted zero, and the "
               "count rule was never reached")
    for p in [(0.2, 0.5), (0.8, 0.5), (0.35, 0.585), (0.65, 0.585),
              (0.5, 0.415)]:
        t.do("place", p)
    t.do("pick", "n1"); t.select("n0"); t.do("bind")
    t.select("n0"); t.lines()
    t.end()

    t = Script(out, "keys_lit_when_they_act",
               "which keys are lit: the same rule an editor asks before "
               "acting, so a lit key acts and a dim one does not. While a "
               "node is dragged only free-hand and the window keys; while "
               "auditioning only the mouse editing dims. b's action is what "
               "it WOULD do -- worked out here by doing it on a copy, in "
               "the C++ by predicting")
    t.keys()                                    # an empty map
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)                        # current output n0
    t.keys()
    t.select("n0", "n1"); t.keys()              # two: join dim; b binds n1
    t.select("n0", "n1", "n2"); t.keys()        # three: join lit
    t.do("join"); t.keys()                      # unjoin lit
    t.keys(drag=True)                           # dragging: almost all dim
    t.keys(drag=True, grid="free")              # free-hand dims too
    t.keys(grid="polar")                        # spokes lit
    t.keys(grid="free")                         # grid keys dim
    t.keys(aud=True)                            # mouse editing dims
    t.do("pick", "n2"); t.select("n2"); t.keys()    # b: n2 would go silent
    t.do("toggleout", "n1"); t.keys()           # two outputs: O dims
    t.select("n0", "n1", "n2"); t.do("pick", "n0"); t.do("bind")
    # n1 and n2 feeding every output, and every output picked: b would
    # unbind them from all of them, leaving both silent.
    t.select("n1", "n2"); t.do("bindall")
    t.do("pick", "n0"); t.do("toggleout", "n1"); t.do("toggleout", "n2")
    t.keys()                                    # b: 2 nodes would go silent
    t.do("undo"); t.do("undo"); t.keys()        # redo lit
    t.clear_sel(); t.keys()
    t.end()

    t = Script(out, "derived_taps",
               "a selected node that a derived output reads from is marked "
               "on that fader: a tap, a copy the node loses nothing to")
    t.do("example")
    t.select("N"); t.taps()
    t.select("O", "A"); t.taps()
    t.select("A"); t.taps()
    t.end()

    t = Script(out, "current_output_stays_put",
               "placing a node no longer moves the current output. It used "
               "to follow every new node, so b on the newest node unbound "
               "it from its own output and left it silent. An unbind that "
               "leaves a node with no output says so: by name for one node, "
               "by count for several, and as a warning")
    for p in [(0.2, 0.2), (0.8, 0.2)]:
        t.do("place", p)                    # current output: n0, the first
    t.do("pick", "n1")
    t.do("place", (0.5, 0.8))               # current STAYS n1
    t.select("n2"); t.do("bind")            # n2 -> n1 as well: no surprise
    t.do("bind")                            # back off n1: n2 still has n2
    t.do("pick", "n2"); t.do("bind")        # n2's ONLY output: silent, named
    t.do("undo")
    # Two nodes whose only output is n0, unbound together: counted.
    t.do("pick", "n0"); t.select("n1"); t.do("bind")     # n1 -> n1, n0
    t.do("pick", "n1"); t.do("bind")                     # n1 -> n0 only
    t.do("pick", "n0"); t.select("n0", "n1"); t.do("bind")
    t.end()

    t = Script(out, "bind_toggles",
               "b toggles each selected node's binding to the CURRENT output. "
               "Pressing it twice unbinds rather than feeding the output "
               "double; a mixed selection binds only the nodes not yet bound; "
               "fill is kept through every change; a node losing its last "
               "output becomes cleanly null")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)                        # outputs n0, n1, n2
    t.do("pick", "n0")
    t.select("n1"); t.do("bind")                # n1 feeds n1 and n0
    t.do("bind")                                # again: unbinds, never doubles
    t.select("n0", "n1"); t.do("bind")          # mixed: binds n1 only
    t.do("bind")                                # all feed n0: unbinds both
    t.select("n1"); t.do("fill", -1, False); t.do("fill", -1, False)   # 90%
    t.do("pick", "n2"); t.do("bind")            # n1 -> n1 and n2, still 90%
    t.do("bind")                                # back to n1 alone, still 90%
    t.do("pick", "n1"); t.do("bind")            # its LAST output: null, clean
    t.do("undo")                                # "undid binding", 90% again
    t.end()

    t = Script(out, "fill_line_on_the_faders",
               "the fader line: for each output a selected node feeds, that "
               "node's fill -- the percentage its shape shows -- so the line "
               "and the shape move together on q and w")
    t.do("example")
    t.select("O"); t.marks()                    # four outputs, full
    t.select("B"); t.marks()                    # right, half
    t.select("B", "D"); t.marks()               # two nodes, two fills
    t.select("B"); t.do("fill", +1, False); t.marks()     # 55%
    t.select("A"); t.marks()                    # no outputs: no line
    t.end()

    t = Script(out, "files_save_and_open",
               "a named save writes the pair; opening either file of the pair "
               "restores it exactly, outputs and all; a missing file changes "
               "nothing; opening is one undo step")
    t.pair("sets/stage-left.json"); t.pair("sets/stage-left-outputs.json")
    t.pair("sets/stage-left-mapping.json"); t.pair("stage-left")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n0", "n1", "n2"); t.do("join")
    t.do("trim", -3.0)
    t.select("n1"); t.do("fill", -1, False)
    t.do("savepair", "stage-left")
    t.do("empty")
    t.do("openpair", "stage-left.json")
    t.do("empty")
    t.do("openpair", "stage-left-outputs.json")   # the outputs name: the pair
    t.do("openpair", "nowhere.json")               # missing: nothing changes
    t.do("undo")                                   # back to the empty map
    t.end()

    t = Script(out, "files_old_partial_and_refused",
               "an earlier editor's -mapping.json pair still opens; a map "
               "with no outputs file opens with none; an outputs file that is "
               "refused opens the map, says why, and is flagged")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n0", "n1", "n2"); t.do("join")
    t.do("legacypair", "old"); t.do("maponly", "bare")
    t.do("badoutputs", "broken")
    t.do("empty"); t.do("openpair", "old.json")
    t.do("empty"); t.do("openpair", "bare.json")
    t.do("empty"); t.do("openpair", "broken.json")
    t.end()

    t = Script(out, "undo_redo",
               "undo and redo over whole-state snapshots. Refusals and cursor "
               "moves are never steps; a new action discards the redo steps; "
               "undoing a delete restores the node, its output and its "
               "bindings, renumbering and all")
    t.do("undo")                                # nothing yet
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n0", "n1", "n2"); t.do("join")
    t.do("undo"); t.do("undo"); t.do("redo")
    t.do("place", (0.2004, 0.2))               # refused: not a step
    t.do("cycle")                               # not a step
    t.do("undo")                                # undoes the redone place
    t.do("place", (0.9, 0.9))                   # new action: redo is gone
    t.do("redo")
    t.select("n0"); t.do("remove")
    t.do("undo")                                # n0, its output, back
    t.select("n1"); t.do("move", "n1", (0.7, 0.3))
    t.do("undo")
    t.do("empty"); t.do("undo")                 # even a new map undoes
    t.end()

    t = Script(out, "history_limit",
               "beyond the limit the OLDEST step goes, never the newest",
               limit=3)
    for p in [(0.1, 0.1), (0.3, 0.1), (0.5, 0.1), (0.7, 0.1), (0.9, 0.1)]:
        t.do("place", p)
    for _ in range(4):
        t.do("undo")                            # three undo, the fourth can't
    t.end()

    t = Script(out, "chart_layout",
               "the fader layout. Changed ON PURPOSE in round 2: unused "
               "channels are slivers; bars narrow, and their gaps with them, "
               "to fit however many outputs; a label is the name if it fits, "
               "else the channel number, else nothing; a column at the right "
               "keeps the total on screen -- with thirty outputs it used to "
               "be drawn past the window's edge. Every bar's hit area takes "
               "in half the gap each side, for hover and click alike")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n1"); t.do("remove")          # channel 1 left unused
    t.chart(24, 502, 976, 118,
            probes=[("mid", 1),             # the sliver: hovered, not picked
                    ("mid", 2), ("edge", 2)])
    t.do("auto")
    for _ in range(28):
        t.do("newoutput")
    t.chart(24, 502, 976, 118, probes=[("mid", 17), ("edge", 29)])
    # The same thirty outputs in a larger font, as text scaled to a big
    # window would be: names give way to channel numbers.
    t.chart(24, 900, 1700, 220, probes=[("mid", 17)], cw=13.0, lh=26.0)
    t.end()

    t = Script(out, "chart_labels_ruler",
               "a hundred channels: too narrow for any label of its own, so "
               "the row is marked like a ruler, every fifth channel. Under "
               "the old bar-by-bar rule nothing at all was written; with "
               "thirty, some bars kept a name, others a number, others "
               "nothing -- the patchwork reported in use")
    t.do("auto")
    for _ in range(100):
        t.do("newoutput")
    t.chart(24, 502, 976, 118, probes=[("mid", 55)])
    t.end()

    t = Script(out, "chart_labels_names_without_numbers",
               "names that fit where channel numbers do not: n0 on channel "
               "100, reachable by making a hundred outputs, then a node, "
               "then removing the hundred. Every name fits, so names are "
               "shown; not every number does, so no number line beneath. "
               "The second row is the reverse: numbers fit, names do not")
    t.chart_of([("n0", 100)], 24, 502, 976, 118)
    t.chart_of([("a", 3), ("loudspeaker", 4)], 24, 502, 260, 118)
    t.end()

    t = Script(out, "chart_slivers",
               "ten nodes, the middle eight deleted: outputs on channels 0 "
               "and 9 only. Eight unused channels become slivers, so the two "
               "real bars keep their full width")
    for k in range(10):
        t.do("place", (0.05 + 0.09 * k, 0.5))
    t.select(*[f"n{k}" for k in range(1, 9)]); t.do("remove")
    t.chart(24, 502, 976, 118,
            probes=[("mid", 0), ("mid", 5), ("edge", 5), ("mid", 9),
                    ("silence",)])
    t.end()
    build_round3(out)
    build_levels(out)
    return out


def build_round3(out):
    t = Script(out, "derived_after_and_before",
               "d makes a derived output from the PICKED FADERS, after their "
               "faders; D one from the selected NODES, before them, as d "
               "did. Three nodes, each its own output; pick n0 and shift-"
               "pick n1, press d; select n2, press D")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("place", (0.5, 0.8))
    t.keys()
    t.do("pick", "n0"); t.do("toggleout", "n1")
    t.do("derivedafter")
    t.select("n2"); t.do("derived")
    t.keys()
    t.taps()
    t.contrib([("n0", 0.5), ("n2", 0.5)])
    out.append("# n1 removed with its node: the after-fed sum loses that")
    out.append("# source and stays, reading n0 alone")
    t.select("n1"); t.do("remove")
    t.do("undo"); t.do("undo")
    t.end()

    t = Script(out, "derived_after_needs_an_output",
               "an empty map has no faders to follow: d is refused and says "
               "why; D with nothing selected likewise")
    t.do("empty")
    t.keys()
    t.do("derivedafter")
    t.do("derived")
    t.end()

    t = Script(out, "rig_save_and_use",
               "save the outputs as a rig; make a second map with two of the "
               "speakers, placed differently, n0 trimmed differently; use the rig "
               "on it. Outputs, channels and trims come from the rig, in its "
               "order; bindings follow by name; the after-fed sum comes with "
               "the rig. One undo step")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("place", (0.5, 0.8))
    t.do("pick", "n0"); t.do("toggleout", "n1"); t.do("derivedafter")
    t.do("pick", "n1"); t.do("trim", -6.0)
    t.do("saverig", "room")
    t.do("saverig", "room-rig.json")
    t.do("empty")
    t.do("place", (0.5, 0.8)); t.do("place", (0.2, 0.2))
    # n1 picked LAST, so it is current but not first among the picked:
    # settling would make the first current, which is how a rig that lost
    # the current output once passed unseen (CI gate run, 3A-5)
    t.do("pick", "n0"); t.do("trim", -3.0); t.do("toggleout", "n1")
    out.append("# the rig brings n2 and the sum, n0's trim back to 0 dB, and")
    out.append("# n1's -6 dB; the bindings here stay as they are, and n1 --")
    out.append("# not the rig's first output -- stays the current one, with")
    out.append("# n0 still picked beside it")
    t.do("userig", "room")
    t.keys()
    t.do("undo")
    t.do("redo")
    t.end()

    t = Script(out, "rig_refused",
               "a rig that lacks an output of this map is refused, naming "
               "it, and nothing changes; so is a rig file that is not there")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("saverig", "pair")
    t.do("place", (0.5, 0.8))
    t.do("userig", "pair")
    t.do("userig", "nowhere")
    t.do("undo")
    t.end()

    t = Script(out, "rig_kind_mismatch",
               "the rig computes sum2 from outputs; this map fills sum2 from "
               "its nodes. Same name, same channel, different meaning: "
               "refused rather than quietly replaced")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("pick", "n0"); t.do("derivedafter")
    t.do("saverig", "kinds")
    t.do("empty")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.select("n0"); t.do("derived")
    t.do("userig", "kinds")
    t.end()



def build_levels(out):
    out.append("#" + "-" * 68)
    out.append("# ROUND 3A-6: LEVELS IN THE EDITOR")
    out.append("#")
    out.append("# The model holds a 3D map and a current level. Output keys act")
    out.append("# on every level; binding keys on the current one. Every state")
    out.append("# past the first level also lists each level: its starting")
    out.append("# height, its bent nodes, its bindings (Z0, Z1, ...).")
    out.append("#" + "-" * 68)

    t = Script(out, "levels_new_and_switch",
               "the example map: n copies the level above, halfway to the "
               "next or a quarter beyond the top; N below. The new level "
               "becomes current. Up and down move between levels and stop "
               "at the ends; they are not undo steps, new levels are")
    t.do("example")
    t.do("levelup")
    t.keys()
    t.do("newlevel", 1)
    t.keys()
    t.do("newlevel", 1)
    t.do("levelup")
    t.do("leveldown")
    out.append("# N from the middle level: halfway down to the ground")
    t.do("newlevel", 0)
    t.do("leveldown")
    out.append("# N from the ground: a quarter below it")
    t.do("newlevel", 0)
    t.do("leveldown")
    t.do("undo"); t.do("undo"); t.do("redo")
    t.end()

    t = Script(out, "levels_bind_one_level",
               "three levels of the example map: binding, fill and clear on "
               "the middle level leave the others alone, and the link lines "
               "are the current level's")
    t.do("example")
    t.do("newlevel", 1); t.do("newlevel", 1)
    t.do("leveldown")
    t.select("N")
    t.do("pick", "rear"); t.do("bind")
    t.lines()
    t.do("fill", -1, False)
    t.marks()
    t.do("levelup")
    t.lines()
    t.marks()
    t.do("leveldown"); t.do("leveldown")
    t.select("O"); t.do("clear")
    t.keys()
    t.do("bindall")
    t.end()

    t = Script(out, "levels_raise",
               "y and h raise and lower the selected nodes on the current "
               "level, 0.05 a step or 0.01 fine, each landing on the next "
               "multiple of its step as fill does. Refused, all or nothing, "
               "naming the nodes, at the level above or below")
    t.do("example")
    t.select("O")
    t.keys()
    t.do("raise", 1, False)
    out.append("# one level: nothing to reach, so a raise is allowed")
    t.do("raise", -1, False)
    t.do("newlevel", 1)
    t.do("leveldown")
    t.do("raise", 1, True); t.do("raise", 1, False)
    t.do("raise", 1, False); t.do("raise", 1, False); t.do("raise", 1, False)
    out.append("# O now at 0.2, and the next coarse step would touch 0.25")
    t.do("raise", 1, False)
    t.do("raise", 1, True); t.do("raise", 1, True); t.do("raise", 1, True)
    t.do("raise", 1, True)
    t.select("O", "N", "E")
    out.append("# O at 0.24, N and E at the start: O refuses, nobody moves")
    t.do("raise", 1, True)
    t.do("raise", -1, False)
    t.do("levelup")
    t.do("raise", -1, False)
    t.select("W"); t.do("raise", -1, False)
    t.do("undo")
    t.end()

    t = Script(out, "levels_new_level_blocked",
               "a new level is a copy of the current one, bends and all; one "
               "that would reach a level next to it is refused, naming the "
               "nodes")
    t.do("example")
    t.do("newlevel", 1)
    t.do("leveldown")
    t.select("O")
    for _ in range(4):
        t.do("raise", 1, False)
    out.append("# O at 0.2 on the ground; a copy at 0.125 puts it at 0.325,")
    out.append("# past the level at 0.25")
    t.do("newlevel", 1)
    t.do("newlevel", 0)
    t.do("levelup")
    t.do("newlevel", 1)
    t.end()

    t = Script(out, "levels_remove",
               "cmd-delete removes the current level; the one below becomes "
               "current. The only level cannot be removed")
    t.do("example")
    t.do("removelevel")
    t.do("newlevel", 1); t.do("newlevel", 1)
    t.select("N"); t.do("pick", "rear"); t.do("bind")
    t.do("leveldown")
    t.do("removelevel")
    t.keys()
    t.do("leveldown")
    t.do("removelevel")
    t.keys()
    t.do("undo"); t.do("undo")
    t.end()

    t = Script(out, "levels_nodes",
               "placing a node on a map with levels: its output is bound on "
               "every level, and it starts at each level's own height. "
               "Removing nodes takes their heights and bindings from every "
               "level; a node's own output stays while ANY level still feeds "
               "it")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("newlevel", 1); t.do("newlevel", 1)
    t.do("place", (0.5, 0.8))
    t.select("n2"); t.do("raise", -1, False)
    t.do("leveldown")
    t.select("n0"); t.do("pick", "n1"); t.do("bind")
    t.do("leveldown")
    t.select("n1"); t.do("remove")
    out.append("# n1's own output stays: n0 still feeds it on the middle level")
    t.select("n2"); t.do("remove")
    t.do("undo")
    t.end()

    t = Script(out, "levels_outputs",
               "outputs belong to every level: a new output, a trim, a "
               "derived output and a removal reach all of them; the removal "
               "counts each node that fed it on any level once")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("newlevel", 1)
    t.do("newoutput")
    t.select("n0"); t.do("bind")
    t.do("leveldown")
    t.select("n0", "n1"); t.do("bind")
    t.do("trim", -6.0)
    t.do("derivedafter")
    out.append("# removed from the upper level, where only n0 feeds it: n1 feeds")
    out.append("# it on the ground, so two nodes no longer reach it")
    t.do("levelup")
    t.do("removeoutput")
    t.end()

    t = Script(out, "levels_files",
               "a map with three levels, one bent, saved and opened again: "
               "every level, its heights and its bindings come back; the "
               "first level is current")
    t.do("example")
    t.do("newlevel", 1); t.do("newlevel", 1)
    t.select("O"); t.do("raise", 1, False)
    t.select("N"); t.do("pick", "rear"); t.do("bind")
    t.do("savepair", "forest")
    t.do("empty")
    t.do("openpair", "forest.json")
    t.end()

    t = Script(out, "levels_rig",
               "a rig used on a map with levels moves every level onto it: "
               "each keeps its own bindings, by name")
    t.do("place", (0.2, 0.2)); t.do("place", (0.8, 0.2))
    t.do("place", (0.5, 0.8))
    t.do("pick", "n0"); t.do("trim", -6.0)
    t.do("saverig", "room")
    t.do("empty")
    t.do("place", (0.8, 0.2)); t.do("place", (0.2, 0.2))
    t.do("newlevel", 1)
    t.select("n0"); t.do("pick", "n1"); t.do("bind")
    t.do("userig", "room")
    t.do("undo")
    t.end()

    t = Script(out, "levels_listening",
               "audition in three dimensions: it starts on the current "
               "level under the point; up and down glide it through the "
               "levels and past them. The chart is handed what reaches the "
               "room there, and the column of dots places the listening "
               "mark between the levels it lies between, or past the ends, "
               "greyed, outside")
    t.do("example")
    w = [("O", 0.5), ("N", 0.25), ("E", 0.25)]
    out.append("# one level: z changes nothing, and there are no dots")
    t.hear(w, 0.0); t.hear(w, 3.0)
    t.keys(aud=True)
    t.do("newlevel", 1); t.do("newlevel", 1)
    t.do("leveldown")
    t.select("N"); t.do("pick", "rear"); t.do("bind")
    t.do("levelup")
    t.select("O"); t.do("raise", -1, False)
    t.do("leveldown")
    t.clear_sel()
    z = t.start_z(w)
    t.hear(w, z)
    for _ in range(6):
        z = t.glide(z, +1, False, 0.25)
        t.hear(w, z)
    out.append("# past the top level: outside, silent, the mark greyed above")
    z = t.glide(z, +1, False, 1.0)
    t.hear(w, z)
    t.hear(w, -0.1)
    t.hear(w, 0.3, listening=False)
    out.append("# the selection's part, from every level it is heard on")
    t.select("N", "O")
    t.hear(w, 0.3)
    t.hear([("A", 0.25), ("B", 0.25), ("C", 0.25), ("D", 0.25)], 0.4,
           top=10.0, bottom=130.0)
    t.keys(aud=True)
    t.keys(aud=False)
    z = t.glide(0.6, -1, True, 2.0)
    out.append("# on the top level O is lowered: audition starts under the point,")
    out.append("# on the level as bent, not at its starting height")
    t.do("levelup")
    t.start_z(w)
    t.end()


def main():
    out = build()
    path = "tests/vectors/editor.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    scripts = sum(1 for l in out if l.startswith("EDIT "))
    steps = sum(1 for l in out if l.startswith("DO "))
    print(f"wrote {path}")
    print(f"  {scripts} scripts, {steps} checked steps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
