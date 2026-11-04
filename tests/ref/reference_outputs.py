#!/usr/bin/env python3
"""
ofxManifold — independent reference for outputs (PLAN-outputs.md).

Outputs with a channel and a trim; bindings whose destination is an output or
SILENCE; derived outputs with weighted sources; node renumbering after removal;
and a node's type computed from its bindings.

FORMULATION DIFFERS FROM THE C++ WHERE IT CAN. The C++ accumulates into a
vector indexed by TargetID and walks each node's link list. This works per
DESTINATION instead: for every output it sums the contributions reaching it,
and computes silence independently as whatever never reached an output. The
two only agree if silence really is the complement of routing.

THE EXISTING 35 MAPPING VECTORS ARE NOT TOUCHED. They are the proof that a
mapping with no silence, no channels and no trim resolves exactly as before.
This file adds to that proof rather than replacing it: LEGACY records assert
that for such a mapping, the new by-channel output equals the old dense vector.

Classes:
  ANALYTIC  known by hand -- a 70/30 split gives 0.7 and 0.3; silence alone is
            null; routed plus silence is the total; trim never moves routed
            share; a legacy mapping's channel vector equals its dense vector.
  CROSS     from this reference.
  SPEC      our rules: channels unique, channel is the index, power-mode
            derived applies the source weight to amplitude before summing.
"""

import json
import math
import os
import sys

TOL = 1e-6
FIXTURES = "tests/fixtures"


def fmt(x):
    """Ten significant digits. See DECISIONS.md D-006."""
    return f"{x:.10g}"


# ---------------------------------------------------------------------------
# the model
# ---------------------------------------------------------------------------

class Mapping:
    def __init__(self):
        self.outputs = []        # [name, channel, trim]
        self.links = {}          # node -> [(kind, dest_name_or_None, weight)]
        # [name, channel, trim, mode, sources, feed]. feed "nodes": sources
        # are (node, gain), read from the weight vector -- before every fader.
        # feed "outputs": sources are (output NAME, gain), read from what each
        # output actually sends -- its routed share times its trim.
        self.derived = []
        self.legacy = set()      # outputs made by add_target(), the old way

    # A legacy output: name only, channel = its position. This is what makes
    # an existing mapping's channel vector equal its old dense vector.
    def add_target(self, name):
        for i, o in enumerate(self.outputs):
            if o[0] == name:
                return i
        ch = len(self.outputs)
        self.outputs.append([name, ch, 1.0])
        self.legacy.add(name)
        return len(self.outputs) - 1

    def channels_in_use(self):
        return ({o[1] for o in self.outputs}
                | {d[1] for d in self.derived if d[1] >= 0})

    # Decision A: names unique, channels unique across outputs AND derived.
    def add_output(self, name, channel, trim=1.0):
        if any(o[0] == name for o in self.outputs):
            return None
        if channel < 0 or channel in self.channels_in_use():
            return None
        self.outputs.append([name, channel, trim])
        return len(self.outputs) - 1

    def add_derived(self, name, channel, trim, mode, sources):
        if channel < 0 or channel in self.channels_in_use():
            return None
        self.derived.append([name, channel, trim, mode, list(sources), "nodes"])
        return len(self.derived) - 1

    def add_derived_after(self, name, channel, trim, mode, sources):
        """Sources are output NAMES. Refused for an unknown one, as for a
        channel in use. A derived output is not an output, so it can never be
        a source: no chains, by construction. Channel -1 is NoChannel: a value
        for the program, outside the channel vector."""
        if channel != -1 and (channel < 0 or channel in self.channels_in_use()):
            return None
        names = {o[0] for o in self.outputs}
        if any(n not in names for n, _g in sources):
            return None
        self.derived.append([name, channel, trim, mode, list(sources),
                             "outputs"])
        return len(self.derived) - 1

    def remove_output(self, name):
        """Links here name their destination, so nothing is renumbered; the
        C++ holds ids and must shift them. Agreement is the test of that."""
        self.outputs = [o for o in self.outputs if o[0] != name]
        self.legacy.discard(name)
        for node in list(self.links):
            self.links[node] = [l for l in self.links[node]
                                if not (l[0] == "output" and l[1] == name)]
            if not self.links[node]:
                del self.links[node]
        # A derived output reading the removed output loses that source and
        # its gain, and stays -- the rule removing a node already follows.
        for d in self.derived:
            if d[5] == "outputs":
                d[4] = [(n, g) for n, g in d[4] if n != name]

    def set_fraction(self, node, f):
        """Silence weight s such that out / (out + s) == f, outputs kept."""
        links = self.links.get(node, [])
        out_w = sum(l[2] for l in links if l[0] == "output")
        if not links or f <= 0.0 or out_w <= 0.0:
            return False
        kept = [l for l in links if l[0] != "silence"]
        if f < 1.0:
            kept.append(("silence", None, out_w * (1.0 - f) / f))
        self.links[node] = kept
        return True

    def bind(self, node, dest, weight=1.0):
        self.links.setdefault(node, []).append(("output", dest, weight))

    def bind_silence(self, node, weight):
        self.links.setdefault(node, []).append(("silence", None, weight))

    # ---- resolution, per DESTINATION -----------------------------------

    def routed(self, wv):
        """Share reaching each output, by output name."""
        w = dict(wv)
        out = {o[0]: 0.0 for o in self.outputs}
        for o in self.outputs:
            name = o[0]
            for node, links in self.links.items():
                if node not in w:
                    continue
                total = sum(l[2] for l in links)
                if abs(total) < 1e-12:
                    continue
                for kind, dest, lw in links:
                    if kind == "output" and dest == name:
                        out[name] += w[node] * lw / total
        return out

    def silence(self, wv):
        """
        Everything that never reached an output, computed directly rather than
        as 1 - routed: null nodes' whole share, plus each node's silence links.
        If the two formulations disagree, silence is not the complement of
        routing and the bar chart's total would not read 1.
        """
        s = 0.0
        for node, weight in wv:
            links = self.links.get(node, [])
            total = sum(l[2] for l in links)
            if not links or abs(total) < 1e-12:
                s += weight                      # null: discarded whole
                continue
            for kind, _dest, lw in links:
                if kind == "silence":
                    s += weight * lw / total
        return s

    def derived_values(self, wv):
        w = dict(wv)
        # What each output actually sends: routed share times its trim.
        r = self.routed(wv)
        sent = {o[0]: r[o[0]] * o[2] for o in self.outputs}
        vals = []
        for name, ch, trim, mode, sources, feed in self.derived:
            if feed == "map":
                vals.append(0.0)       # filled by a map; a rig alone has none
                continue
            read = sent if feed == "outputs" else w
            if mode == "linear":
                v = sum(sw * read.get(n, 0.0) for n, sw in sources)
            elif feed == "outputs":
                v = math.sqrt(sum((sw * read.get(n, 0.0)) ** 2
                                  for n, sw in sources))
            else:
                # Source weight applied to AMPLITUDE, then powers summed. With
                # every source weight 1 this is exactly the existing
                # power-preserving aggregator.
                v = math.sqrt(sum((sw * w.get(n, 0.0)) ** 2 for n, sw in sources))
            vals.append(v)
        return vals

    def by_channel(self, wv, levels):
        """Dense, indexed by channel number exactly; zeros in the gaps."""
        used = self.channels_in_use()
        if not used:
            return []
        dense = [0.0] * (max(used) + 1)
        r = self.routed(wv)
        for name, ch, trim in self.outputs:
            dense[ch] = r[name] * (trim if levels else 1.0)
        for (name, ch, trim, mode, _s, _f), v in zip(self.derived,
                                                  self.derived_values(wv)):
            if ch < 0:
                continue                 # NoChannel: not in the vector
            dense[ch] = v * (trim if levels else 1.0)
        return dense

    def legacy_dense(self, wv):
        """The EXISTING toDenseVector(): by TargetID order."""
        r = self.routed(wv)
        return [r[o[0]] for o in self.outputs]

    # ---- node type, computed (decision F) --------------------------------

    def label(self, node):
        links = self.links.get(node, [])
        total = sum(l[2] for l in links)
        outs = {l[1] for l in links if l[0] == "output"}
        sil = sum(l[2] for l in links if l[0] == "silence")
        if not outs or abs(total) < 1e-12:
            return "null", 0.0
        frac = 1.0 - sil / total
        if sil > 1e-12:
            return "partial", frac
        return ("terminal" if len(outs) == 1 else "composite"), 1.0

    # ---- renumbering after removal (decision E) --------------------------

    def remap(self, table):
        """table[old] = new id, or -1 for a removed node."""
        m = Mapping()
        m.outputs = [list(o) for o in self.outputs]
        m.legacy = set(self.legacy)
        for node, links in self.links.items():
            nn = table[node] if node < len(table) else -1
            if nn >= 0:
                m.links[nn] = list(links)
        for name, ch, trim, mode, sources, feed in self.derived:
            if feed == "outputs":
                # Reads outputs, not nodes: renumbering nodes leaves it whole.
                m.derived.append([name, ch, trim, mode, list(sources), feed])
                continue
            kept = [(table[n], sw) for n, sw in sources
                    if n < len(table) and table[n] >= 0]
            m.derived.append([name, ch, trim, mode, kept, feed])
        return m

    # ---- the rig ---------------------------------------------------------

    def rig(self):
        """Every channel of the room. Outputs; derived outputs fed by outputs,
        whole; derived outputs fed by NODES as channels the map fills -- name,
        channel and trim, no sources, since the sources are the map's. No
        bindings. So the channel vector keeps the map's arity: nothing that
        leaves the room is lost by passing through a rig."""
        r = Mapping()
        r.outputs = [list(o) for o in self.outputs]
        for d in self.derived:
            if d[5] == "outputs":
                r.derived.append([d[0], d[1], d[2], d[3], list(d[4]), d[5]])
            else:
                r.derived.append([d[0], d[1], d[2], d[3], [], "map"])
        return r


# ---------------------------------------------------------------------------
# emitting
# ---------------------------------------------------------------------------

def wv_s(v):
    return " ".join(f"{n}={fmt(w)}" for n, w in v)


def dense_s(v):
    return " ".join(fmt(x) for x in v)


def emit_map(out, name, m):
    """A mapping as a block the runner rebuilds through the C++ API."""
    out.append(f"MAP {name}")
    for oname, ch, trim in m.outputs:
        # An output made the old way is rebuilt the old way, with addTarget(),
        # so the LEGACY records test the path existing code actually takes.
        if oname in m.legacy:
            out.append(f"TARGET {oname}")
        else:
            out.append(f"OUT {oname} {ch} {fmt(trim)}")
    for node in sorted(m.links):
        for kind, dest, w in m.links[node]:
            if kind == "output":
                out.append(f"BIND {node} OUTPUT {dest} {fmt(w)}")
            else:
                out.append(f"BIND {node} SILENCE {fmt(w)}")
    for dname, ch, trim, mode, sources, feed in m.derived:
        src = " ".join(f"{n}:{fmt(w)}" for n, w in sources)
        rec = "DERIVEDOUT" if feed == "outputs" else "DERIVED"
        out.append(f"{rec} {dname} {ch} {fmt(trim)} {mode} SRC {src}")
    out.append("ENDMAP")
    out.append("")


def resolve_record(out, name, cls, mapname, m, wv):
    routed = m.by_channel(wv, levels=False)
    levels = m.by_channel(wv, levels=True)
    sil = m.silence(wv)
    out.append(f"RESOLVE {name} {cls} MAP {mapname} IN {wv_s(wv)} "
               f"ROUTED {dense_s(routed)} SILENCE {fmt(sil)} "
               f"LEVELS {dense_s(levels)}")


def write_fixture(name, obj):
    os.makedirs(FIXTURES, exist_ok=True)
    with open(os.path.join(FIXTURES, name), "w") as f:
        f.write(obj if isinstance(obj, str) else json.dumps(obj, indent=2) + "\n")
    return name


# ---------------------------------------------------------------------------
# vectors
# ---------------------------------------------------------------------------

def build():
    out = []
    out.append("# ofxManifold outputs conformance vectors")
    out.append("# GENERATED by tests/ref/reference_outputs.py -- do not hand edit")
    out.append("#")
    out.append("# Resolution here is computed per DESTINATION, and silence")
    out.append("# directly rather than as 1 - routed. The C++ accumulates per")
    out.append("# node. The two only agree if silence really is the complement")
    out.append("# of routing.")
    out.append("")
    out.append(f"TOL {fmt(TOL)}")
    out.append("")

    # ---- legacy: the backward-compatibility claim, extended -------------
    legacy = Mapping()
    for t in ("out.1", "out.2", "out.3", "out.sub"):
        legacy.add_target(t)
    legacy.bind(0, "out.1")
    legacy.bind(1, "out.2")
    legacy.bind(2, "out.1"); legacy.bind(2, "out.2"); legacy.bind(2, "out.3")
    legacy.bind(3, "out.sub", 3.0); legacy.bind(3, "out.3", 1.0)
    # node 4 bound to nothing: null
    emit_map(out, "legacy", legacy)
    out.append("# A mapping using none of the new features. Outputs made the")
    out.append("# old way get channel = their id, so the new by-channel vector")
    out.append("# must EQUAL the existing toDenseVector(). The 35 vectors in")
    out.append("# mapping.vec prove the old behaviour; these prove the new")
    out.append("# view of it is the same thing.")
    for k, wv in enumerate([[(0, 0.5), (1, 0.3), (2, 0.2)],
                            [(2, 0.6), (3, 0.4)],
                            [(0, 0.2), (4, 0.8)]]):
        assert legacy.by_channel(wv, False) == legacy.legacy_dense(wv)
        out.append(f"LEGACY legacy_equal_{k} ANALYTIC MAP legacy IN {wv_s(wv)} "
                   f"DENSE {dense_s(legacy.legacy_dense(wv))}")
    out.append("")

    # ---- silence --------------------------------------------------------
    fade = Mapping()
    fade.add_output("out.1", 0)
    fade.add_output("out.2", 1)
    fade.bind(0, "out.1", 0.7); fade.bind_silence(0, 0.3)   # partial fade
    fade.bind(1, "out.2")                                    # ordinary
    fade.bind_silence(2, 1.0)                                # silence alone
    fade.bind(3, "out.1"); fade.bind(3, "out.2")             # composite
    fade.bind_silence(3, 2.0)                                # half to silence
    emit_map(out, "fade", fade)
    out.append("# PARTIAL FADE: node 0 bound 0.7 to out.1 and 0.3 to silence.")
    out.append("# Its whole weight arriving gives exactly 0.7 on out.1 and 0.3")
    out.append("# silent -- MIAP's virtual-to-silent link, which ofxManifold")
    out.append("# could not express before.")
    resolve_record(out, "silence_partial_fade", "ANALYTIC", "fade", fade,
                   [(0, 1.0)])
    out.append("# a genuinely UNBOUND node -- no links at all -- in the input.")
    out.append("# Its whole share is silence. Until this existed no resolution")
    out.append("# contained one, and silenceShare() could forget null nodes")
    out.append("# entirely with every vector still green.")
    resolve_record(out, "silence_counts_null_nodes", "ANALYTIC", "fade", fade,
                   [(0, 0.5), (4, 0.5)])
    out.append("# silence alone behaves exactly as a null node")
    resolve_record(out, "silence_alone_is_null", "ANALYTIC", "fade", fade,
                   [(2, 1.0)])
    out.append("# composite bound to two outputs at 1 each and silence at 2:")
    out.append("# a quarter to each output, half silent")
    resolve_record(out, "silence_composite_half", "ANALYTIC", "fade", fade,
                   [(3, 1.0)])
    out.append("# a mixed weight vector: routed plus silence totals exactly 1")
    for k, wv in enumerate([[(0, 0.4), (1, 0.35), (3, 0.25)],
                            [(0, 0.1), (2, 0.6), (3, 0.3)],
                            [(1, 0.5), (2, 0.5)]]):
        resolve_record(out, f"silence_mixed_{k}", "CROSS", "fade", fade, wv)
    out.append("")

    # ---- channels, trim, derived ----------------------------------------
    rig = Mapping()
    rig.add_output("L", 1, 1.0)
    rig.add_output("R", 2, 0.8)          # trimmed
    rig.add_output("C", 5, 0.5)          # a gap: channels 3 and 4 unused
    rig.bind(0, "L"); rig.bind(1, "R"); rig.bind(2, "C")
    rig.bind(3, "L"); rig.bind(3, "R")
    rig.add_derived("sub", 7, 0.9, "linear", [(0, 3.0), (1, 1.0)])
    rig.add_derived("front", 8, 1.0, "power", [(0, 1.0), (1, 1.0)])
    emit_map(out, "rig", rig)
    out.append("# channels 1, 2, 5, 7, 8: the vector is indexed by channel")
    out.append("# EXACTLY, length highest + 1, zeros at 0, 3, 4 and 6")
    resolve_record(out, "channels_with_gaps", "ANALYTIC", "rig", rig,
                   [(0, 0.5), (1, 0.3), (2, 0.2)])
    out.append("# TRIM changes levels and NEVER routed share: R is trimmed to")
    out.append("# 0.8 and C to 0.5, and their routed values are untouched")
    resolve_record(out, "trim_levels_only", "ANALYTIC", "rig", rig,
                   [(1, 0.6), (2, 0.4)])
    out.append("# DERIVED with weighted sources, at its channel: a sub fed three")
    out.append("# parts from node 0 to one part from node 1 -- source weights are")
    out.append("# gains, as MIAP's derived links are")
    resolve_record(out, "derived_weighted", "ANALYTIC", "rig", rig,
                   [(0, 0.6), (1, 0.4)])
    out.append("# power-mode derived: source weight on amplitude, powers summed")
    resolve_record(out, "derived_power", "SPEC", "rig", rig,
                   [(0, 0.8), (1, 0.6)])
    for k, wv in enumerate([[(0, 0.25), (1, 0.25), (2, 0.25), (3, 0.25)],
                            [(3, 0.7), (2, 0.3)]]):
        resolve_record(out, f"rig_mixed_{k}", "CROSS", "rig", rig, wv)
    out.append("")

    # ---- refusals -------------------------------------------------------
    out.append("# A CHANNEL may be used once, across outputs and derived alike.")
    out.append("# Two outputs on one channel would make the vector ambiguous --")
    out.append("# the kernel accepting a state a consumer trips over, which is")
    out.append("# the D-018/019/020 pattern")
    out.append("REFUSE refuse_duplicate_channel ANALYTIC OUTPUT a 3 OUTPUT b 3")
    out.append("REFUSE refuse_channel_taken_by_derived ANALYTIC "
               "DERIVEDFIRST d 4 OUTPUT a 4")
    out.append("REFUSE refuse_duplicate_name ANALYTIC OUTPUT a 3 OUTPUT a 6")
    out.append("REFUSE refuse_negative_channel ANALYTIC OUTPUT a -1")
    out.append("")

    # ---- type labels ----------------------------------------------------
    out.append("# A node's type is COMPUTED from its bindings, never stored")
    out.append("# (decision F), and FRACTION is how much of its share reaches")
    out.append("# an output -- what the node's fill shows in the editor")
    for node in range(5):
        lab, frac = fade.label(node)
        out.append(f"LABEL label_fade_{node} ANALYTIC MAP fade NODE {node} "
                   f"EXPECT {lab} FRACTION {fmt(frac)}")
    for node in range(5):
        lab, frac = legacy.label(node)
        out.append(f"LABEL label_legacy_{node} ANALYTIC MAP legacy NODE {node} "
                   f"EXPECT {lab} FRACTION {fmt(frac)}")
    out.append("")

    out.append("# CLEARING a node removes every binding it has, silence included,")
    out.append("# and leaves every OTHER node exactly as it was. Checked on the")
    out.append("# partial node, whose silence share nothing else could remove,")
    out.append("# and on a composite.")
    for node in (0, 3):
        others = [(n, *fade.label(n)) for n in range(5) if n != node]
        exp = " ".join(f"{n}:{lab}:{fmt(fr)}" for n, lab, fr in others)
        out.append(f"CLEARED cleared_fade_{node} ANALYTIC MAP fade NODE {node} "
                   f"OTHERS {exp}")
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# REMOVING AN OUTPUT")
    out.append("#")
    out.append("# Other outputs keep their channels. A node that fed the removed")
    out.append("# output and others renormalizes onto the others; a node that")
    out.append("# fed ONLY it becomes null and its share silence -- exactly what")
    out.append("# unbinding already does, so one rule, not two.")
    out.append("#" + "-" * 68)
    import copy as _copy
    for nm, src, mapname, gone, wv in [
            ("fade_out2", fade, "fade", "out.2", [(1, 0.5), (3, 0.5)]),
            ("fade_out1", fade, "fade", "out.1", [(0, 0.4), (3, 0.6)]),
            ("rig_first", rig, "rig", "L", [(0, 0.3), (1, 0.3), (3, 0.4)]),
            ("rig_middle", rig, "rig", "R", [(1, 0.5), (3, 0.5)]),
            ("legacy_first", legacy, "legacy", "out.1",
             [(0, 0.3), (2, 0.4), (3, 0.3)])]:
        m = _copy.deepcopy(src)
        m.remove_output(gone)
        rest = " ".join(f"{o[0]}:{o[1]}" for o in m.outputs)
        out.append(f"REMOVEOUT removeout_{nm} ANALYTIC MAP {mapname} "
                   f"OUTPUT {gone} IN {wv_s(wv)} "
                   f"ROUTED {dense_s(m.by_channel(wv, False))} "
                   f"SILENCE {fmt(m.silence(wv))} "
                   f"LEVELS {dense_s(m.by_channel(wv, True))} REST {rest}")
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# SETTING A NODE'S FILL")
    out.append("#")
    out.append("# Only the silence share changes, so the balance between a")
    out.append("# node's outputs is kept: the composite below feeds out.1 and")
    out.append("# out.2 equally before and after. Refused for a node with no")
    out.append("# output, and for a fill of zero -- fully silent is clearing.")
    out.append("#" + "-" * 68)
    for nm, node, f, wv in [("partial_down", 0, 0.25, [(0, 1.0)]),
                            ("composite_up", 3, 0.9, [(3, 1.0)]),
                            ("terminal_full", 1, 1.0, [(1, 1.0)]),
                            ("partial_to_full", 0, 1.0, [(0, 1.0)]),
                            ("terminal_to_one_percent", 1, 0.01, [(1, 1.0)]),
                            ("silence_only_refused", 2, 0.5, [(2, 1.0)]),
                            ("unbound_refused", 4, 0.5, [(4, 1.0)]),
                            ("zero_refused", 0, 0.0, [(0, 1.0)])]:
        m = _copy.deepcopy(fade)
        ok = m.set_fraction(node, f)
        _lab, frac = m.label(node)
        # LINKS: how many bindings the node has afterwards. A full fill must
        # REMOVE the silence binding, not leave one of weight zero -- which
        # behaves identically but still forces the mapping file to version 2,
        # since any silence binding needs it.
        out.append(f"SETFILL setfill_{nm} ANALYTIC MAP fade NODE {node} "
                   f"TO {fmt(f)} RESULT {1 if ok else 0} "
                   f"LINKS {len(m.links.get(node, []))} "
                   f"FRACTION {fmt(frac)} IN {wv_s(wv)} "
                   f"ROUTED {dense_s(m.by_channel(wv, False))} "
                   f"SILENCE {fmt(m.silence(wv))}")
    out.append("")

    # ---- remapping after removal ----------------------------------------
    out.append("#" + "-" * 68)
    out.append("# REMAPPING AFTER NODE REMOVAL (decision E)")
    out.append("#")
    out.append("# removeNodes() renumbers nodes (D-020). Bindings held by NodeID")
    out.append("# must follow, or deleting a node silently re-routes every")
    out.append("# binding after it to the wrong node. Checked as D-020 checked")
    out.append("# the geometry: the remapped mapping must resolve IDENTICALLY to")
    out.append("# one built fresh from the survivors.")
    out.append("#" + "-" * 68)
    for nm, gone, src in [("drop_first", {0}, rig),
                          ("drop_middle", {2}, rig),
                          ("drop_derived_source", {1}, rig),
                          ("drop_partial", {0}, fade),
                          ("drop_nothing", set(), fade)]:
        n_nodes = 6
        table, nxt = [], 0
        for i in range(n_nodes):
            if i in gone:
                table.append(-1)
            else:
                table.append(nxt)
                nxt += 1
        after = src.remap(table)
        mapname = "rig" if src is rig else "fade"
        # probe with every surviving node present, in NEW ids
        survivors = [table[i] for i in range(n_nodes)
                     if table[i] >= 0 and i in src.links]
        wv = [(n, 1.0 / max(1, len(survivors))) for n in survivors]
        routed = after.by_channel(wv, False)
        levels = after.by_channel(wv, True)
        out.append(f"REMAP remap_{nm} ANALYTIC MAP {mapname} "
                   f"TABLE {' '.join(str(t) for t in table)} "
                   f"IN {wv_s(wv) or 'NONE'} ROUTED {dense_s(routed)} "
                   f"SILENCE {fmt(after.silence(wv))} LEVELS {dense_s(levels)}")
    out.append("")

    # ---- what must survive save and reload ------------------------------
    reorder = Mapping()
    reorder.add_target("L"); reorder.add_target("R"); reorder.add_target("spare")
    reorder.bind(0, "R")          # node 0 -> R first...
    reorder.bind(1, "L")          # ...then node 1 -> L. 'spare' bound to nothing.
    emit_map(out, "reorder", reorder)
    out.append("#" + "-" * 68)
    out.append("# WHAT MUST SURVIVE SAVE AND RELOAD")
    out.append("#")
    out.append("# The version 1 file lists bindings only; outputs are recreated")
    out.append("# as bindings mention them. So an output bound to nothing was")
    out.append("# DROPPED, and outputs came back in the order bindings mention")
    out.append("# them rather than the order they were made. Measured on this")
    out.append("# mapping before any code changed: 'spare' vanished and L and R")
    out.append("# swapped indices -- an OSC receiver reading argument 1 as the")
    out.append("# right speaker got the left one after reopening the file.")
    out.append("#")
    out.append("# So version 1 is written ONLY when a version 1 reload would")
    out.append("# reproduce the mapping exactly. This one must become version 2,")
    out.append("# which lists outputs explicitly (D-022).")
    out.append("#" + "-" * 68)
    out.append("MAPPRESERVE preserve_order_and_unbound ANALYTIC MAP reorder "
               "MANIFOLD outputs_manifold.json VERSION 2")
    out.append("")
    out.append("# ONE fault per fixture. The mapping above has both faults at")
    out.append("# once, so removing EITHER check left the other forcing version")
    out.append("# 2 -- each masked the other, and mutation testing removed each")
    out.append("# check with the suite still green. The bowtie lesson again:")
    out.append("# one case exercising two checks proves neither on its own.")
    reorder_only = Mapping()
    reorder_only.add_target("L"); reorder_only.add_target("R")
    reorder_only.bind(0, "R"); reorder_only.bind(1, "L")
    emit_map(out, "reorder_only", reorder_only)
    out.append("# every output bound, but first mentioned out of order")
    out.append("MAPPRESERVE preserve_reorder_only ANALYTIC MAP reorder_only "
               "MANIFOLD outputs_manifold.json VERSION 2")
    unbound_only = Mapping()
    unbound_only.add_target("L"); unbound_only.add_target("R")
    unbound_only.add_target("spare")
    unbound_only.bind(0, "L"); unbound_only.bind(1, "R")
    emit_map(out, "unbound_only", unbound_only)
    out.append("# in order, but one output bound to nothing")
    out.append("MAPPRESERVE preserve_unbound_only ANALYTIC MAP unbound_only "
               "MANIFOLD outputs_manifold.json VERSION 2")
    out.append("# the legacy mapping IS safe as version 1: every output bound,")
    out.append("# first mentioned in the order made -- so it stays version 1,")
    out.append("# byte-identical, and still round-trips exactly")
    out.append("MAPPRESERVE preserve_legacy ANALYTIC MAP legacy "
               "MANIFOLD outputs_manifold.json VERSION 1")
    out.append("MAPPRESERVE preserve_rig ANALYTIC MAP rig "
               "MANIFOLD outputs_manifold.json VERSION 2")
    out.append("MAPPRESERVE preserve_fade ANALYTIC MAP fade "
               "MANIFOLD outputs_manifold.json VERSION 2")
    out.append("")

    # ---- files ----------------------------------------------------------
    write_fixture("outputs_manifold.json", {
        "version": 1, "space": "normalized",
        "nodes": [{"id": f"n{i}", "position": p, "weight": 1.0}
                  for i, p in enumerate([[0.2, 0.2], [0.8, 0.2], [0.5, 0.8],
                                         [0.5, 0.45], [0.1, 0.9], [0.9, 0.9]])],
        "triangles": [["n0", "n1", "n3"], ["n1", "n2", "n3"],
                      ["n2", "n0", "n3"]],
    })
    write_fixture("outputs_v2.json", {
        "version": 2,
        "outputs": [{"name": "L", "channel": 1, "trim": 1.0},
                    {"name": "R", "channel": 2, "trim": 0.8},
                    {"name": "C", "channel": 5, "trim": 0.5}],
        "bindings": [
            {"node": "n0", "target": "L", "weight": 0.7},
            {"node": "n0", "kind": "silence", "weight": 0.3},
            {"node": "n1", "target": "R", "weight": 1.0},
            {"node": "n2", "target": "C", "weight": 1.0},
            {"node": "n3", "target": "L", "weight": 1.0},
            {"node": "n3", "target": "R", "weight": 1.0}],
        "aggregators": [
            {"name": "sub", "channel": 7, "trim": 0.9, "mode": "linear",
             "sources": [{"node": "n0", "weight": 3.0},
                         {"node": "n1", "weight": 1.0}]}],
    })
    out.append("# FILES. A version 2 mapping carries channels, trims, silence")
    out.append("# and weighted derived sources, and round-trips byte-stable.")
    out.append("MAPFILE file_v2_loads ANALYTIC outputs_manifold.json "
               "outputs_v2.json OK")
    out.append("MAPROUNDTRIP file_v2_roundtrip ANALYTIC outputs_manifold.json "
               "outputs_v2.json")
    out.append("# a mapping using NO new feature is written as version 1,")
    out.append("# byte-identical to before (decision G). Checked against every")
    out.append("# existing mapping fixture in the serialize suite as well.")
    out.append("MAPVERSION file_legacy_stays_v1 ANALYTIC outputs_manifold.json "
               "LEGACYMAP 1")
    out.append("MAPVERSION file_new_features_v2 ANALYTIC outputs_manifold.json "
               "outputs_v2.json 2")

    bad = {
        "bad_out_dup_channel.json": (
            {"version": 2, "outputs": [{"name": "a", "channel": 3},
                                       {"name": "b", "channel": 3}],
             "bindings": []}, "channel"),
        "bad_out_unknown_kind.json": (
            {"version": 2, "outputs": [{"name": "a", "channel": 0}],
             "bindings": [{"node": "n0", "kind": "layer", "weight": 1.0}]},
            "kind"),
        "bad_out_silence_with_target.json": (
            {"version": 2, "outputs": [{"name": "a", "channel": 0}],
             "bindings": [{"node": "n0", "kind": "silence", "target": "a",
                           "weight": 1.0}]}, "silence"),
        # A version newer than this reader knows. It was 3 until version 3
        # existed (round 3A); the vector is unchanged, the fixture moved on.
        "bad_out_future_version.json": (
            {"version": 4, "bindings": []}, "version"),
    }
    out.append("# refused, each for its OWN reason (D-007). 'layer' is refused")
    out.append("# as a kind until layers exist -- reserved, not accepted and")
    out.append("# ignored, which would silently drop a binding")
    for fname, (obj, reason) in bad.items():
        write_fixture(fname, obj)
        out.append(f"MAPFILE {fname[:-5]} ANALYTIC outputs_manifold.json "
                   f"{fname} REFUSE {reason}")
    out.append("")
    build_after(out)
    return out


# ---------------------------------------------------------------------------
# round 3A-1: derived outputs AFTER the fader, and rig files
# ---------------------------------------------------------------------------

def build_after(out):
    import copy as _copy
    out.append("#" + "-" * 68)
    out.append("# DERIVED OUTPUTS AFTER THE FADER (PLAN-round3.md decision A)")
    out.append("#")
    out.append("# A derived output fed by OUTPUTS reads what each one actually")
    out.append("# sends: its routed share times its trim. One fed by NODES reads")
    out.append("# the weight vector, ahead of every fader, exactly as before.")
    out.append("# Here both sit on one map: 'tap' reads nodes 0 and 1, 'sub'")
    out.append("# reads outputs L and R. R is trimmed to half.")
    out.append("#" + "-" * 68)
    room = Mapping()
    room.add_output("L", 1, 1.0)
    room.add_output("R", 2, 0.5)
    room.add_output("C", 3, 1.0)
    room.bind(0, "L"); room.bind(1, "R"); room.bind(2, "C")
    room.bind(3, "L"); room.bind(3, "R")
    room.bind(5, "L", 0.6); room.bind_silence(5, 0.4)
    room.add_derived("tap", 8, 1.0, "linear", [(0, 1.0), (1, 1.0)])
    assert room.add_derived_after("sub", 9, 0.8, "linear",
                                  [("L", 1.0), ("R", 1.0)]) is not None
    assert room.add_derived_after("subp", 10, 1.0, "power",
                                  [("L", 1.0), ("R", 2.0)]) is not None
    emit_map(out, "room", room)

    out.append("# THE POINT OF IT: node 1 feeds R alone, and R is trimmed to")
    out.append("# half. The tap, before the fader, reads all of node 1 (1.0).")
    out.append("# The sub, after it, reads what R sends (0.5) -- pull a speaker")
    out.append("# down and its sub follows. Then the sub's own trim, 0.8, gives")
    out.append("# its level 0.4. subp: 2 x 0.5, power-summed, is 1.")
    resolve_record(out, "after_follows_trim", "ANALYTIC", "room", room,
                   [(1, 1.0)])
    out.append("# L sends 0.6 and R 0.4 x 0.5 = 0.2: the sub is their sum, 0.8,")
    out.append("# and its level 0.64. The tap still reads 1.0.")
    resolve_record(out, "after_sums_levels", "ANALYTIC", "room", room,
                   [(0, 0.6), (1, 0.4)])
    out.append("# a node whose fill sends 60% to L and 40% to silence: the sub")
    out.append("# reads only what reached L, never the silent part")
    resolve_record(out, "after_reads_no_silence", "ANALYTIC", "room", room,
                   [(5, 1.0)])
    out.append("# a null node: nothing is sent, so an after-derived output")
    out.append("# reads nothing")
    resolve_record(out, "after_null_reads_nothing", "ANALYTIC", "room", room,
                   [(4, 1.0)])
    out.append("# C feeds neither derived output")
    resolve_record(out, "after_unread_output", "ANALYTIC", "room", room,
                   [(2, 1.0)])
    for k, wv in enumerate([[(0, 0.25), (1, 0.25), (2, 0.25), (3, 0.25)],
                            [(3, 0.5), (5, 0.3), (2, 0.2)],
                            [(1, 0.7), (4, 0.3)]]):
        resolve_record(out, f"after_mixed_{k}", "CROSS", "room", room, wv)
    out.append("")

    out.append("# REFUSED: a source that is not an output; a channel already")
    out.append("# taken, in either direction. DERIVEDOUTBAD names output id 999.")
    out.append("REFUSE refuse_after_unknown_source ANALYTIC "
               "OUTPUT a 1 DERIVEDOUTBAD d 2")
    out.append("REFUSE refuse_after_channel_taken ANALYTIC "
               "OUTPUT a 1 DERIVEDOUT d 1")
    out.append("REFUSE refuse_output_on_after_channel ANALYTIC "
               "DERIVEDOUT d 4 OUTPUT a 4")
    out.append("")

    out.append("# REMOVING AN OUTPUT a derived output reads: that source goes,")
    out.append("# with its gain; the derived output stays. Removing L, the")
    out.append("# FIRST output, shifts every id after it -- the sub must still")
    out.append("# read R, not whatever moved into R's old place.")
    for nm, gone, wv in [("room_R", "R", [(0, 0.5), (1, 0.5)]),
                         ("room_L", "L", [(0, 0.3), (1, 0.4), (2, 0.3)]),
                         ("room_C", "C", [(1, 0.6), (2, 0.4)])]:
        m = _copy.deepcopy(room)
        m.remove_output(gone)
        rest = " ".join(f"{o[0]}:{o[1]}" for o in m.outputs)
        out.append(f"REMOVEOUT removeout_{nm} ANALYTIC MAP room "
                   f"OUTPUT {gone} IN {wv_s(wv)} "
                   f"ROUTED {dense_s(m.by_channel(wv, False))} "
                   f"SILENCE {fmt(m.silence(wv))} "
                   f"LEVELS {dense_s(m.by_channel(wv, True))} REST {rest}")
    out.append("")

    out.append("# REMOVING A NODE renumbers nodes; it must leave an")
    out.append("# after-derived output untouched -- it reads no node. The tap,")
    out.append("# which does, loses node 0.")
    table = [-1, 0, 1, 2, 3, 4]
    after = room.remap(table)
    wv = [(0, 0.5), (2, 0.5)]
    out.append(f"REMAP remap_room_keeps_after ANALYTIC MAP room "
               f"TABLE {' '.join(str(t) for t in table)} "
               f"IN {wv_s(wv)} ROUTED {dense_s(after.by_channel(wv, False))} "
               f"SILENCE {fmt(after.silence(wv))} "
               f"LEVELS {dense_s(after.by_channel(wv, True))}")
    out.append("")

    out.append("# FILES. An after-derived output needs version 3, which names")
    out.append("# its sources as outputs. Everything else keeps the version it")
    out.append("# had: the earlier MAPPRESERVE records are unchanged.")
    out.append("MAPPRESERVE preserve_room ANALYTIC MAP room "
               "MANIFOLD outputs_manifold.json VERSION 3")
    out.append("# Everything else about this map is version 1: outputs made the")
    out.append("# old way, all bound, in order, and a derived output with no")
    out.append("# channel and no gains. Only its reading OUTPUTS needs version 3.")
    out.append("# Without that check it was written as version 1, which cannot")
    out.append("# name an output source, and the sub came back reading nothing.")
    la = Mapping()
    la.add_target("out.1"); la.add_target("out.2")
    la.bind(0, "out.1"); la.bind(1, "out.2")
    la.add_derived_after("s", -1, 1.0, "linear", [("out.1", 1.0),
                                                  ("out.2", 1.0)])
    emit_map(out, "legacyafter", la)
    out.append("MAPPRESERVE preserve_legacy_after ANALYTIC MAP legacyafter "
               "MANIFOLD outputs_manifold.json VERSION 3")
    write_fixture("outputs_v3.json", {
        "version": 3,
        "outputs": [{"name": "L", "channel": 1, "trim": 1.0},
                    {"name": "R", "channel": 2, "trim": 0.5},
                    {"name": "C", "channel": 3, "trim": 1.0}],
        "bindings": [
            {"node": "n0", "target": "L", "weight": 1.0},
            {"node": "n1", "target": "R", "weight": 1.0},
            {"node": "n2", "target": "C", "weight": 1.0}],
        "aggregators": [
            {"name": "tap", "mode": "linear", "channel": 8, "trim": 1.0,
             "sources": [{"node": "n0", "weight": 1.0},
                         {"node": "n1", "weight": 1.0}]},
            {"name": "sub", "mode": "linear", "channel": 9, "trim": 0.8,
             "sources": [{"output": "L", "weight": 1.0},
                         {"output": "R", "weight": 1.0}]}],
    })
    out.append("MAPFILE file_v3_loads ANALYTIC outputs_manifold.json "
               "outputs_v3.json OK")
    out.append("MAPROUNDTRIP file_v3_roundtrip ANALYTIC outputs_manifold.json "
               "outputs_v3.json 3")
    out.append("MAPVERSION file_after_v3 ANALYTIC outputs_manifold.json "
               "outputs_v3.json 3")
    bad = {
        # an output source in a version 2 file: written by nothing, and an
        # older reader could not know what it means
        "bad_out_output_source_v2.json": (
            {"version": 2, "outputs": [{"name": "L", "channel": 1}],
             "bindings": [],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear",
                              "sources": [{"output": "L", "weight": 1.0}]}]},
            "version 3"),
        "bad_out_mixed_sources.json": (
            {"version": 3, "outputs": [{"name": "L", "channel": 1}],
             "bindings": [],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear",
                              "sources": [{"output": "L", "weight": 1.0},
                                          {"node": "n0", "weight": 1.0}]}]},
            "mixes"),
        "bad_out_unknown_output_source.json": (
            {"version": 3, "outputs": [{"name": "L", "channel": 1}],
             "bindings": [],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear",
                              "sources": [{"output": "Q", "weight": 1.0}]}]},
            "unknown output"),
        "bad_out_unknown_file_kind.json": (
            {"version": 3, "kind": "scene", "outputs": [], "bindings": []},
            "kind"),
    }
    for fname, (obj, reason) in bad.items():
        write_fixture(fname, obj)
        out.append(f"MAPFILE {fname[:-5]} ANALYTIC outputs_manifold.json "
                   f"{fname} REFUSE {reason}")
    out.append("")

    out.append("#" + "-" * 68)
    out.append("# THE RIG (decision B): every channel of the room. rigOf(room)")
    out.append("# keeps L, R and C on their channels with their trims, keeps")
    out.append("# sub and subp whole, and keeps the tap as a channel the MAP")
    out.append("# fills: name, channel, trim, no sources -- those are the map's.")
    out.append("# No bindings. Its channel vector has the room's arity, so a")
    out.append("# receiver's argument 9 is the sub in both.")
    out.append("#" + "-" * 68)
    rig = room.rig()
    outs = " ".join(f"{o[0]}:{o[1]}:{fmt(o[2])}" for o in rig.outputs)
    ders = " ".join(f"{d[0]}:{d[5]}" for d in rig.derived)
    arity = len(room.by_channel([], False))
    out.append(f"RIGOF rigof_room ANALYTIC MAP room OUTS {outs} "
               f"DERIVED {ders} ARITY {arity}")
    out.append("# A node-fed derived output on the HIGHEST channel. Measured")
    out.append("# before this rule: a rig that dropped it had 5 channels where")
    out.append("# the map had 7, and channel 6 vanished on the way out.")
    rig2 = fade_like_rig()
    emit_map(out, "plainrig", rig2)
    r2 = rig2.rig()
    outs2 = " ".join(f"{o[0]}:{o[1]}:{fmt(o[2])}" for o in r2.outputs)
    ders2 = " ".join(f"{d[0]}:{d[5]}" for d in r2.derived)
    assert len(r2.by_channel([], False)) == len(rig2.by_channel([], False))
    out.append(f"RIGOF rigof_plain ANALYTIC MAP plainrig OUTS {outs2} "
               f"DERIVED {ders2} ARITY {len(rig2.by_channel([], False))}")

    write_fixture("rig_room.json", {
        "version": 3, "kind": "rig",
        "outputs": [{"name": "L", "channel": 1, "trim": 1.0},
                    {"name": "R", "channel": 2, "trim": 0.5},
                    {"name": "C", "channel": 3, "trim": 1.0}],
        "aggregators": [
            {"name": "tap", "mode": "linear", "channel": 8, "trim": 1.0,
             "feed": "map"},
            {"name": "sub", "mode": "linear", "channel": 9, "trim": 0.8,
             "sources": [{"output": "L", "weight": 1.0},
                         {"output": "R", "weight": 1.0}]}],
    })
    out.append("# a rig file loads, round-trips byte-stable as a rig, and a")
    out.append("# map's outputs file may also open it -- outputs, no bindings")
    out.append("RIGFILE rig_loads ANALYTIC rig_room.json OK")
    out.append("RIGROUNDTRIP rig_roundtrip ANALYTIC rig_room.json")
    out.append("MAPFILE rig_opens_as_map ANALYTIC outputs_manifold.json "
               "rig_room.json OK")
    bad_rigs = {
        "bad_rig_bindings.json": (
            {"version": 3, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1}],
             "bindings": [{"node": "n0", "target": "L", "weight": 1.0}]},
            "bindings"),
        "bad_rig_node_source.json": (
            {"version": 3, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1}],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear",
                              "sources": [{"node": "n0", "weight": 1.0}]}]},
            "node"),
        # a channel the map fills names no sources: they are the map's
        "bad_rig_map_fed_sources.json": (
            {"version": 3, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1}],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear", "feed": "map",
                              "sources": [{"output": "L", "weight": 1.0}]}]},
            "map"),
        "bad_rig_unknown_feed.json": (
            {"version": 3, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1}],
             "aggregators": [{"name": "s", "channel": 5, "trim": 1.0,
                              "mode": "linear", "feed": "air"}]},
            "feed"),
        "bad_rig_version.json": (
            {"version": 2, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1}]}, "version"),
        "bad_rig_dup_channel.json": (
            {"version": 3, "kind": "rig",
             "outputs": [{"name": "L", "channel": 1},
                         {"name": "R", "channel": 1}]}, "channel"),
    }
    out.append("# refused, each for its own reason; and a map's outputs file")
    out.append("# is not a rig, so loadRig refuses it rather than dropping its")
    out.append("# bindings without a word")
    for fname, (obj, reason) in bad_rigs.items():
        write_fixture(fname, obj)
        out.append(f"RIGFILE {fname[:-5]} ANALYTIC {fname} REFUSE {reason}")
    # Refused for NOT SAYING it is a rig -- "kind", not any other reason.
    # A map's outputs file also has bindings, which a rig refuses on their
    # own; with only that fixture, removing the kind check was MASKED by the
    # bindings check and the suite stayed green (mutation run, 3A-1). The
    # second file has outputs and nothing a rig refuses: only "kind" can
    # stop it.
    out.append("RIGFILE map_is_not_a_rig ANALYTIC outputs_v3.json REFUSE kind")
    write_fixture("outputs_only_v3.json", {
        "version": 3,
        "outputs": [{"name": "L", "channel": 1, "trim": 1.0}],
        "aggregators": [
            {"name": "sub", "mode": "linear", "channel": 9, "trim": 1.0,
             "sources": [{"output": "L", "weight": 1.0}]}],
    })
    out.append("RIGFILE unmarked_is_not_a_rig ANALYTIC outputs_only_v3.json "
               "REFUSE kind")


def fade_like_rig():
    m = Mapping()
    m.add_output("A", 0, 1.0)
    m.add_output("B", 4, 0.25)
    m.bind(0, "A"); m.bind(1, "B")
    m.add_derived("tapA", 6, 1.0, "linear", [(0, 1.0)])
    return m


def main():
    out = build()
    path = "tests/vectors/outputs.vec"
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")
    counts = {}
    for line in out:
        p = line.split()
        if p and p[0] in ("LEGACY", "RESOLVE", "REFUSE", "LABEL", "REMAP",
                          "MAPFILE", "MAPROUNDTRIP", "MAPVERSION",
                          "MAPPRESERVE", "CLEARED", "REMOVEOUT",
                          "SETFILL", "RIGOF", "RIGFILE", "RIGROUNDTRIP"):
            counts[p[2]] = counts.get(p[2], 0) + 1
    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
