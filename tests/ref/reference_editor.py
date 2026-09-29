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

CHARACTERIZATION. Round 1 of PLAN-editor.md changes no behaviour, so these pin
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
        self.links = {}          # node name -> [(kind, output name|None, w)]
        self.derived = []        # [name, channel, [source names]]
        self.current = None

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
        return any(l[0] == "out" and l[1] == out
                   for ls in self.links.values() for l in ls)

    def settle(self):
        if self.current not in self.out_names():
            self.current = None
        if self.current is None and self.outputs:
            self.current = self.outputs[0][0]

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
        for n in list(self.links):
            self.links[n] = [l for l in self.links[n]
                             if not (l[0] == "out" and l[1] == name)]
            if not self.links[n]:
                del self.links[n]
        if self.current == name:
            self.current = None
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
            self.bind_one(nm, nm)
            self.current = nm
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
            self.links.pop(n, None)
        self.derived = [[d[0], d[1], [s for s in d[2] if s not in gone]]
                        for d in self.derived]
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
        self.current = nm
        return (1, 0, f"new output {nm} on channel {ch} "
                "-- select nodes and press b")

    def new_derived(self, sel):
        if not sel:
            return (0, 1, "select the nodes a derived output should sum")
        ch = self.next_channel()
        nm = f"sum{ch}"
        self.derived.append([nm, ch, list(sel)])
        return (1, 0, f"derived {nm} on channel {ch}, sum of {len(sel)}")

    def bind(self, sel):
        if self.current is None:
            return (0, 1, "no output yet -- press o to make one")
        if not sel:
            return (0, 0, "")
        for n in sel:
            self.bind_one(n, self.current)
        return (1, 0, f"bound {len(sel)} to {self.current}")

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
        self.current = names[(i + 1) % len(names)]
        return (0, 0, f"current output: {self.current}")

    def pick(self, name):
        if name not in self.out_names():
            return (0, 0, "")
        self.current = name
        return (0, 0, f"current output: {name}")

    def trim(self, db):
        if self.current is None:
            return (0, 0, "")
        for o in self.outputs:
            if o[0] == self.current:
                o[2] = min(o[2] * 10 ** (db / 20.0), 3.9810717)
        return (1, 0, "")

    def remove_current(self):
        if self.current is None:
            return (0, 0, "")
        nm = self.current
        feeders = sum(1 for ls in self.links.values()
                      if any(l[0] == "out" and l[1] == nm for l in ls))
        self.remove_output(nm)
        msg = f"removed output {nm}"
        if feeders:
            msg += f" -- {feeders} node" + ("" if feeders == 1 else "s") \
                + " no longer reach it"
        return (1, 1 if feeders else 0, msg)

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
        self.current = "front"

    # ---- the fader layout ----------------------------------------------
    def chart(self, x0, top, w, h):
        """Geometry only, as a list; restated from the rules, not the C++."""
        empty = not self.outputs and not self.derived
        if empty:
            return {"empty": True}
        used = [o[1] for o in self.outputs] + [d[1] for d in self.derived]
        channels = max(used) + 1
        gap = 8.0
        slots = channels + 1
        bw = min(46.0, (w - gap * (slots + 1)) / max(slots, 1))
        bars = []
        for ch in range(channels):
            x = x0 + gap + ch * (bw + gap)
            kind, name, out = "unused", "", None
            for o in self.outputs:
                if o[1] == ch:
                    kind, name, out = "output", o[0], o[0]
            if kind == "unused":
                for d in self.derived:
                    if d[1] == ch:
                        kind, name = "derived", d[0]
            bars.append((ch, kind, x, name, out))
        sx = x0 + gap + channels * (bw + gap) + gap
        return {"empty": False, "bw": bw, "sx": sx, "tx": sx + bw + 16.0,
                "bars": bars, "top": top, "h": h}

    def output_at(self, lay, x, y):
        if lay["empty"]:
            return None
        for ch, kind, bx, name, out in lay["bars"]:
            if kind == "output" and bx <= x <= bx + lay["bw"] \
                    and lay["top"] <= y <= lay["top"] + lay["h"]:
                return out
        return None

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
                 "A " + ("1" if self.auto else "0")]
        return " | ".join(parts)


# ---------------------------------------------------------------------------
# scripts
# ---------------------------------------------------------------------------

S = 0.05


LABELS = {"place": "place", "join": "join", "unjoin": "unjoin",
          "remove": "delete", "newoutput": "new output",
          "derived": "derived output", "bind": "bind",
          "bindall": "bind to every output", "fill": "fill",
          "clear": "clear bindings", "trim": "trim",
          "removeoutput": "remove output", "example": "example map",
          "empty": "new map", "move": "move"}


class Script:
    def __init__(self, out, name, note, grid=None, spec="FREE", limit=200):
        self.out, self.ed = out, Editor()
        self.past, self.future, self.limit = [], [], limit
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
            "bind":     lambda: e.bind(s),
            "bindall":  lambda: e.bind_all(s),
            "fill":     lambda: e.step_fill(s, args[0], args[1]),
            "move":     lambda: e.move(args[0], args[1]),
            "clear":    lambda: e.clear(s), "cycle": e.cycle,
            "trim":     lambda: e.trim(args[0]),
            "pick":     lambda: e.pick(args[0]),
            "removeoutput": e.remove_current, "auto": e.toggle_auto,
            "example":  lambda: (e.example(), (1, 0, ""))[1],
            "empty":    lambda: (e.new_empty(), (1, 0, ""))[1],
        }[op]()
        argtxt = ""
        if op == "place":
            argtxt = f" {fmt(args[0][0])} {fmt(args[0][1])}"
        elif op == "trim":
            argtxt = f" {fmt(args[0])}"
        elif op == "pick":
            argtxt = f" {args[0]}"
        elif op == "fill":
            argtxt = f" {args[0]} {1 if args[1] else 0}"
        elif op == "move":
            argtxt = f" {args[0]} {fmt(args[1][0])} {fmt(args[1][1])}"
        self.out.append(f"DO {op}{argtxt}")
        self.out.append(f'EXPECT {res[0]} {res[1]} "{res[2]}"')
        self.out.append(f"STATE {e.dump()}")
        if op == "remove":           # the app clears after a removal
            self.sel, self.ordered = [], True
            self.out.append("SEL clear")
        return res

    def chart(self, x0, top, w, h, clicks=()):
        lay = self.ed.chart(x0, top, w, h)
        self.out.append(f"CHART {fmt(x0)} {fmt(top)} {fmt(w)} {fmt(h)}")
        if lay["empty"]:
            self.out.append("EXPECTCHART EMPTY")
        else:
            bars = " ".join(f"{c}:{k}:{fmt(x)}:{n or '-'}"
                            for c, k, x, n, _o in lay["bars"])
            self.out.append(f"EXPECTCHART {fmt(lay['bw'])} {fmt(lay['sx'])} "
                            f"{fmt(lay['tx'])} {bars}")
        for x, y in clicks:
            hit = None if lay["empty"] else self.ed.output_at(lay, x, y)
            self.out.append(f"CLICK {fmt(x)} {fmt(y)} {hit or '-'}")

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
        t.do("place", p)                    # current output is n2
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
    t.do("cycle")                           # current: n0
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
    t.select("n0"); t.do("bind")           # current is n1: n0 feeds n1's output
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
    # The window as it is today: 1024 x 640, chart 118 high from y 502.
    t.chart(24, 502, 976, 118,
            clicks=[(24 + 8 + 1 * 54 + 20, 560),     # inside left's bar
                    (24 + 8 + 4 * 54 + 20, 560),     # the derived sub: no
                    (24 + 8 + 5 * 54 + 8 + 20, 560), # the silence bar: no
                    (24 + 8 + 0 * 54 + 20, 700),     # below the chart: no
                    (24 + 8 + 0 * 54 + 50, 560)])    # in the gap: no
    t.select("B"); t.do("fill", +1, False)
    t.do("empty")
    t.chart(24, 502, 976, 118, clicks=[(60, 560)])
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
               "the fader layout: a gap left by a deleted node's output shows "
               "as an unused channel, and thirty outputs narrow every bar. "
               "Round 2 changes this layout on purpose, and this vector with "
               "it")
    for p in [(0.2, 0.2), (0.8, 0.2), (0.5, 0.8)]:
        t.do("place", p)
    t.select("n1"); t.do("remove")          # channel 1 left unused
    t.chart(24, 502, 976, 118,
            clicks=[(24 + 8 + 1 * 54 + 20, 560),     # unused: no
                    (24 + 8 + 2 * 54 + 20, 560)])    # n2, channel 2
    t.do("auto")
    for _ in range(28):
        t.do("newoutput")
    t.chart(24, 502, 976, 118)
    t.end()
    return out


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
