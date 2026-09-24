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
        self.derived = []        # [name, channel, trim, mode, [(node, w)]]
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
        return {o[1] for o in self.outputs} | {d[1] for d in self.derived}

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
        self.derived.append([name, channel, trim, mode, list(sources)])
        return len(self.derived) - 1

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
        vals = []
        for name, ch, trim, mode, sources in self.derived:
            if mode == "linear":
                v = sum(sw * w.get(n, 0.0) for n, sw in sources)
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
        for (name, ch, trim, mode, _s), v in zip(self.derived,
                                                  self.derived_values(wv)):
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
        for name, ch, trim, mode, sources in self.derived:
            kept = [(table[n], sw) for n, sw in sources
                    if n < len(table) and table[n] >= 0]
            m.derived.append([name, ch, trim, mode, kept])
        return m


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
    for dname, ch, trim, mode, sources in m.derived:
        src = " ".join(f"{n}:{fmt(w)}" for n, w in sources)
        out.append(f"DERIVED {dname} {ch} {fmt(trim)} {mode} SRC {src}")
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
        "bad_out_future_version.json": (
            {"version": 3, "bindings": []}, "version"),
    }
    out.append("# refused, each for its OWN reason (D-007). 'layer' is refused")
    out.append("# as a kind until layers exist -- reserved, not accepted and")
    out.append("# ignored, which would silently drop a binding")
    for fname, (obj, reason) in bad.items():
        write_fixture(fname, obj)
        out.append(f"MAPFILE {fname[:-5]} ANALYTIC outputs_manifold.json "
                   f"{fname} REFUSE {reason}")
    return out


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
                          "MAPPRESERVE"):
            counts[p[2]] = counts.get(p[2], 0) + 1
    print(f"wrote {path}")
    for k in ("ANALYTIC", "CROSS", "SPEC"):
        print(f"  {k:9s} {counts.get(k, 0)}")
    print(f"  {'TOTAL':9s} {sum(counts.values())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
