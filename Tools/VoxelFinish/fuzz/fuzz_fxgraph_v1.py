"""Property and fuzz harness for VoxelFinish effect graphs.

Stdlib random only. Limits are selected with VF_FUZZ_PROFILE:
  current — flattened 32 nodes / 64 op cost (fxgraph_eval_v2)
  target  — 32 authored nodes, flattened 64 nodes / 128 op cost (fxgraph_eval_v3)
  both    — default; structured cases run on each profile

The independent oracle covers subeffect-free graphs. Combined-effect cases
(depth, cycles, flat limits, bundles) carry an expected verdict per profile.
"""
from __future__ import annotations

import copy
import json
import os
import random
import sys
import time
import tracemalloc
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REF = ROOT / "vf_reference"
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

# Package imports: a bare `import fxgraph_eval_v2` could pick up another copy on sys.path
# (for example the fixture copy under Plugins/VoxelFinishTests).
from vf_reference import fxgraph_eval_v2 as eval_v2
from vf_reference import fxgraph_eval_v3 as eval_v3

MODULES = {"current": eval_v2, "target": eval_v3}
CREATED = "2026-10-30T00:00:00Z"
NOTE = "fuzz"
CASE_BUDGET_S = 2.0
MEMORY_BUDGET = 512 * 1024 * 1024

INPUTS = {
    "time": "f", "uv": "v2", "block_world_pos": "v3", "noise_value": "f",
    "noise_perlin": "f", "palette_a": "v3", "palette_b": "v3", "pattern_mask": "f",
}
SAMPLED = {"noise_value", "noise_perlin", "pattern_mask"}
OPS = {
    "add": (2, 1), "mul": (2, 1), "lerp": (3, 2), "sin": (1, 1), "step": (2, 1),
    "smoothstep": (3, 3), "clamp": (3, 2), "hue_shift": (2, 4), "scroll_uv": (3, 2),
    "rotate_uv": (2, 4),
}
OUTPUTS = {"base_color": "v3", "emissive_strength": "f", "alpha_mask": "f"}
DIM = {"f": 1, "v2": 2, "v3": 3}
SCHEMAS = ("gfgame.effect_graph/1", "voxelfinish.effect_graph/2")

# Every brief category. Xfail findings are counted by the pytest module, not here.
REQUIRED_CATEGORIES = (
    "seed", "valid_mutation", "random_accept", "random_reject", "oracle_seed",
    "cycle_self", "cycle_pair", "cycle_long", "depth_3", "depth_4", "depth_longer",
    "authored_at", "authored_over", "flat_nodes_at", "flat_nodes_over", "flat_nodes_between",
    "cost_at", "cost_over", "cost_between",
    "nan", "pos_inf", "neg_inf", "huge_exp", "neg_zero",
    "unknown_node", "unknown_output", "type_mismatch", "self_ref", "duplicate_id",
    "id_slash", "id_dot", "arg_range", "unknown_exposed", "wire_param", "missing_outputs_empty",
    "tamper", "wrong_hash", "parent_rev", "bad_created", "note_long", "note_at", "duplicate_rev",
    "huge_nodes", "huge_string", "bundle_graphs_at", "bundle_graphs_over",
    "bundle_items_at", "bundle_items_over", "prompt_at", "prompt_over",
    "unicode_note", "unicode_node", "nul_node", "nfc_nfd",
    "bundle_missing", "bundle_extra", "bundle_reorder", "bundle_hash_swap",
    "finish_ok", "finish_range", "determinism", "hash_sensitivity", "flatten_invariants",
)


class Case:
    __slots__ = ("name", "category", "profile", "expect", "needle", "fn")

    def __init__(self, name, category, profile, expect, needle, fn):
        self.name = name
        self.category = category
        self.profile = profile
        self.expect = expect
        self.needle = needle
        self.fn = fn


class SuiteResult:
    def __init__(self, seed, requested, profile):
        self.seed = seed
        self.requested = requested
        self.profile = profile
        self.counts = {}
        self.pass_n = {"current": 0, "target": 0}
        self.fail_n = {"current": 0, "target": 0}
        self.failures = []
        self.elapsed = 0.0
        self.executed = 0

    @property
    def ok(self):
        return not self.failures

    def summary(self):
        lines = [
            f"seed={self.seed} profile={self.profile} executed={self.executed} "
            f"requested={self.requested} elapsed={self.elapsed:.2f}s",
            "current: {p} passed, {f} failed".format(
                p=self.pass_n["current"], f=self.fail_n["current"]),
            "target: {p} passed, {f} failed".format(
                p=self.pass_n["target"], f=self.fail_n["target"]),
            "counts: " + ", ".join(f"{key}={self.counts[key]}" for key in sorted(self.counts)),
        ]
        if self.failures:
            lines.append(f"failures ({len(self.failures)}):")
            for item in self.failures[:12]:
                lines.append(
                    f"  [{item['profile']}] {item['category']} {item['name']}: "
                    f"expected {item['expect']} got {item['status']} {item['msg'][:180]}"
                )
        return "\n".join(lines)


def load_seeds():
    graphs = []
    for path in sorted((REF / "graphs").glob("*.json")):
        graphs.append(json.loads(path.read_text(encoding="utf-8")))
    return graphs


def seed_registry(mod, graphs):
    return mod.Registry(graphs=[copy.deepcopy(g) for g in graphs])


def stamp(mod, g, number=1, parent=None, note=NOTE, created=CREATED):
    return mod.make_rev(copy.deepcopy(g), number, parent, note, created)


def effect_of(g):
    return g.get("effect_id") or g.get("id")


def has_subeffect(g):
    return any(isinstance(n, dict) and n.get("type") == "subeffect" for n in g.get("nodes", []))


def check_ok(mod, g, reg):
    """full_check plus flatten invariants. Raises GraphError on a bad graph."""
    flat, _lock, _cost, _bake = mod.full_check(g, reg)
    ids = [n["id"] for n in flat["nodes"]]
    if any(n.get("type") == "subeffect" for n in flat["nodes"]):
        raise RuntimeError("flattened graph still has a subeffect")
    if len(ids) != len(set(ids)):
        raise RuntimeError("flattened ids are not unique")
    mod.validate(flat)
    if len(flat["nodes"]) > mod.MAX_NODES:
        raise RuntimeError(f"flat nodes {len(flat['nodes'])} > {mod.MAX_NODES}")
    return flat


def _bcast(types):
    vectors = [t for t in types if t != "f"]
    if not vectors:
        return "f"
    if len(set(vectors)) > 1:
        return None
    return vectors[0]


def _finite_const(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        if isinstance(value, list) and len(value) in (2, 3):
            if any(isinstance(x, bool) or not isinstance(x, (int, float)) for x in value):
                return None
            if any(abs(x) > 1.0e4 or x != x or x in (float("inf"), float("-inf")) for x in value):
                return None
            return "v2" if len(value) == 2 else "v3"
        return None
    if value != value or value in (float("inf"), float("-inf")) or abs(value) > 1.0e4:
        return None
    return "f"


def oracle_issues(g, max_nodes, max_cost):
    """Independent checker for graphs with no SubEffect nodes.

    Returns a list of short reasons. Empty means the graph should flatten.
    """
    issues = []
    if g.get("schema") not in SCHEMAS:
        return ["schema"]
    if not effect_of(g):
        return ["effect_id"]
    for key in ("name", "animated", "nodes", "outputs"):
        if key not in g:
            return ["missing " + key]
    if has_subeffect(g):
        return ["subeffect"]
    if g["animated"]:
        loop = g.get("loop_seconds")
        if isinstance(loop, bool) or not isinstance(loop, (int, float)) or not (0.25 <= loop <= 16):
            return ["loop"]
    nodes = g["nodes"]
    if not isinstance(nodes, list):
        return ["nodes-type"]
    if len(nodes) > max_nodes:
        issues.append("nodes")
    byid = {}
    for node in nodes:
        if not isinstance(node, dict):
            return issues + ["node-type"]
        nid = node.get("id")
        if not isinstance(nid, str) or nid in byid or "/" in nid or "." in nid:
            return issues + ["id"]
        byid[nid] = node
    deps = {}
    for nid, node in byid.items():
        ins = node.get("inputs", [])
        if not isinstance(ins, list):
            return issues + ["inputs"]
        for ref in ins:
            if ref not in byid:
                return issues + ["unknown"]
        deps[nid] = list(ins)
    done, visiting = set(), set()
    order = []

    def visit(nid):
        if nid in done:
            return False
        if nid in visiting:
            return True
        visiting.add(nid)
        for dep in deps[nid]:
            if visit(dep):
                return True
        visiting.discard(nid)
        done.add(nid)
        order.append(nid)
        return False

    for nid in byid:
        if visit(nid):
            return issues + ["cycle"]
    types = {}
    cost = 0
    for nid in order:
        # `done` insertion is postorder, which is a valid topo order.
        node = byid[nid]
        kind = node.get("type")
        params = node.get("params", {})
        ins = node.get("inputs", [])
        incoming = [types[i] for i in ins]
        if kind == "input":
            source = params.get("source")
            if source not in INPUTS:
                return issues + ["source"]
            if source in SAMPLED:
                if len(ins) > 1 or (ins and incoming[0] != "v2"):
                    return issues + ["sampled"]
                if source != "pattern_mask":
                    scale = params.get("scale", 4)
                    seed = params.get("seed", 0)
                    if isinstance(scale, bool) or not isinstance(scale, int) or not 1 <= scale <= 16:
                        return issues + ["scale"]
                    if isinstance(seed, bool) or not isinstance(seed, int) or not 0 <= seed <= 65535:
                        return issues + ["seed"]
            elif ins:
                return issues + ["input-arity"]
            out = INPUTS[source]
            comp = params.get("component")
            if comp is not None:
                if source not in ("uv", "block_world_pos") or not isinstance(comp, str) or comp not in "xyz"[:DIM[out]] or len(comp) != 1:
                    return issues + ["component"]
                out = "f"
            types[nid] = out
        elif kind == "const":
            if ins:
                return issues + ["const-inputs"]
            ctype = _finite_const(params.get("value"))
            if ctype is None:
                return issues + ["const"]
            types[nid] = ctype
        elif kind in OPS:
            arity, op_cost = OPS[kind]
            if len(ins) != arity:
                return issues + ["arity"]
            cost += op_cost
            if kind == "hue_shift":
                if incoming[0] != "v3" or incoming[1] != "f":
                    return issues + ["hue"]
                types[nid] = "v3"
            elif kind == "scroll_uv":
                if incoming != ["v2", "v2", "f"]:
                    return issues + ["scroll"]
                types[nid] = "v2"
            elif kind == "rotate_uv":
                if incoming != ["v2", "f"]:
                    return issues + ["rotate"]
                types[nid] = "v2"
            elif kind == "sin":
                types[nid] = incoming[0]
            elif kind == "lerp":
                mixed = _bcast(incoming[:2])
                if mixed is None or (incoming[2] != "f" and incoming[2] != mixed):
                    return issues + ["lerp"]
                types[nid] = _bcast(incoming)
                if types[nid] is None:
                    return issues + ["lerp"]
            else:
                mixed = _bcast(incoming)
                if mixed is None:
                    return issues + ["bcast"]
                types[nid] = mixed
        else:
            return issues + ["type"]
    if cost > max_cost:
        issues.append("cost")
    outs = g["outputs"]
    if not isinstance(outs, dict) or "base_color" not in outs:
        return issues + ["outputs"]
    for name, nid in outs.items():
        if name not in OUTPUTS:
            return issues + ["output-name"]
        if nid not in types:
            return issues + ["output-node"]
        if OUTPUTS[name] == "f" and types[nid] != "f":
            return issues + ["output-type"]
        if name == "base_color" and types[nid] not in ("f", "v3"):
            return issues + ["output-type"]
    return issues


def const_chain(n, salt):
    nodes = [{"id": "c0", "type": "const", "params": {"value": salt}}]
    prev = "c0"
    for i in range(1, n):
        nid = f"a{i}"
        nodes.append({"id": nid, "type": "add", "inputs": [prev, "c0"]})
        prev = nid
    return nodes, prev


def hue_chain(n_hue, turn):
    nodes = [
        {"id": "col", "type": "input", "params": {"source": "palette_a"}},
        {"id": "turn", "type": "const", "params": {"value": turn}},
    ]
    prev = "col"
    for i in range(n_hue):
        nid = f"h{i}"
        nodes.append({"id": nid, "type": "hue_shift", "inputs": [prev, "turn"]})
        prev = nid
    return nodes, prev


def bare(mod, eid, nodes, out, animated=False, loop=1.0):
    g = {
        "schema": mod.SCHEMA_V2, "effect_id": eid, "name": eid, "animated": animated,
        "nodes": nodes, "outputs": {"base_color": out},
    }
    if animated:
        g["loop_seconds"] = loop
    return g


def sub_ref(sid, eid, rev=1, args=None, wires=None):
    params = {"ref": {"effect_id": eid, "rev": rev}}
    if args:
        params["args"] = args
    if wires:
        params["wires"] = wires
    return {"id": sid, "type": "subeffect", "params": params}


def combine_two(mod, eid, left_id, right_id, left_n, right_n, left_salt, right_salt):
    left_nodes, left_out = const_chain(left_n, left_salt)
    right_nodes, right_out = const_chain(right_n, right_salt)
    left = stamp(mod, bare(mod, left_id, left_nodes, left_out))
    right = stamp(mod, bare(mod, right_id, right_nodes, right_out))
    parent = stamp(mod, bare(mod, eid, [
        sub_ref("s1", left_id),
        sub_ref("s2", right_id),
        {"id": "out", "type": "add", "inputs": ["s1.base_color", "s2.base_color"]},
    ], "out"))
    reg = mod.Registry(graphs=[left, right, parent])
    return parent, reg


def wrap_child(mod, eid, child):
    g = bare(mod, eid, [sub_ref("s", effect_of(child))], "s.base_color",
             animated=child.get("animated", False), loop=child.get("loop_seconds", 1.0))
    return stamp(mod, g)


def cycle_pair(mod, a_id, b_id):
    a = stamp(mod, bare(mod, a_id, [sub_ref("s", b_id)], "s.base_color"))
    b = stamp(mod, bare(mod, b_id, [sub_ref("s", a_id)], "s.base_color"))
    return a, mod.Registry(graphs=[a, b])


def cycle_long(mod):
    graphs = []
    ids = ["fx_cyc_a", "fx_cyc_b", "fx_cyc_c"]
    for i, eid in enumerate(ids):
        nxt = ids[(i + 1) % len(ids)]
        graphs.append(stamp(mod, bare(mod, eid, [sub_ref("s", nxt)], "s.base_color")))
    return graphs[0], mod.Registry(graphs=graphs)


def depth_chain(mod, length):
    """length 1 = a leaf. length 4 = depth 3 (leaf plus 3 wrappers)."""
    graphs = [stamp(mod, bare(mod, "fx_d0", *const_chain(1, 0.2)))]
    for i in range(1, length):
        graphs.append(wrap_child(mod, f"fx_d{i}", graphs[-1]))
    return graphs[-1], mod.Registry(graphs=graphs)


def leaf_graph(mod, eid, salt):
    nodes, out = const_chain(1, salt)
    return stamp(mod, bare(mod, eid, nodes, out))


def prune_dead(g):
    """Drop nodes that cannot affect an output. Cost then matches flatten()."""
    byid = {n["id"]: n for n in g["nodes"]}
    live = set()
    stack = list(g["outputs"].values())
    while stack:
        nid = stack.pop()
        head = nid.split(".", 1)[0]
        if head not in byid or head in live:
            continue
        live.add(head)
        stack.extend(byid[head].get("inputs", []))
    g["nodes"] = [n for n in g["nodes"] if n["id"] in live]
    return g


def wide_bundle_graphs(mod, mids, per_mid):
    """1 root + `mids` children + mids*per_mid leaves. All static, distinct ids."""
    graphs = []
    mid_ids = []
    leaf_index = 0
    for m in range(mids):
        leaf_ids = []
        for _ in range(per_mid):
            eid = f"fx_leaf_{leaf_index}"
            leaf_index += 1
            # Identical leaf bodies collapse when flattened, so a 64-graph
            # closure still fits the combined-effect node cap.
            graphs.append(leaf_graph(mod, eid, 0.25))
            leaf_ids.append(eid)
        mid = f"fx_mid_{m}"
        mid_ids.append(mid)
        nodes = [sub_ref(f"s{i}", eid) for i, eid in enumerate(leaf_ids)]
        prev = "s0.base_color"
        for i in range(1, len(leaf_ids)):
            nid = f"m{i}"
            nodes.append({"id": nid, "type": "add", "inputs": [prev, f"s{i}.base_color"]})
            prev = nid
        out = prev if len(leaf_ids) > 1 else "s0.base_color"
        graphs.append(stamp(mod, bare(mod, mid, nodes, out)))
    root_nodes = [sub_ref(f"s{i}", eid) for i, eid in enumerate(mid_ids)]
    prev = "s0.base_color"
    for i in range(1, len(mid_ids)):
        nid = f"m{i}"
        root_nodes.append({"id": nid, "type": "add", "inputs": [prev, f"s{i}.base_color"]})
        prev = nid
    out = prev if len(mid_ids) > 1 else "s0.base_color"
    root = stamp(mod, bare(mod, "fx_bundle_root", root_nodes, out))
    graphs.append(root)
    return root, graphs


def bundle_hash_payload(manifest):
    payload = {k: manifest[k] for k in ("purpose", "items", "closure")}
    if manifest.get("prompt") is not None:
        payload["prompt"] = manifest["prompt"]
    return payload


def finish_base():
    return {
        "palette_a": "#112233", "palette_b": "#ABCDEF", "pattern_id": 1,
        "glow_mask_id": 255, "strength": 1.0,
        "effect": {"effect_id": "fx_flat_paint", "rev": 1}, "args": {},
    }


def _map_ref(val, old, new):
    if not isinstance(val, str):
        return val
    head, dot, tail = val.partition(".")
    if head == old:
        return new + (("." + tail) if dot else "")
    return val


def rename_id(g, old, new):
    g = copy.deepcopy(g)
    for node in g["nodes"]:
        if node.get("id") == old:
            node["id"] = new
        if node.get("inputs"):
            node["inputs"] = [_map_ref(item, old, new) for item in node["inputs"]]
        wires = node.get("params", {}).get("wires") if node.get("type") == "subeffect" else None
        if isinstance(wires, dict):
            for key, val in list(wires.items()):
                wires[key] = _map_ref(val, old, new)
    for key, val in list(g.get("outputs", {}).items()):
        g["outputs"][key] = _map_ref(val, old, new)
    for exposed in g.get("exposed", []) or []:
        if exposed.get("node") == old:
            exposed["node"] = new
    return g


def tweak_const(g, rng):
    g = copy.deepcopy(g)
    exposed_nodes = {item.get("node") for item in g.get("exposed", []) or [] if item.get("kind") == "param"}
    consts = [n for n in g["nodes"] if n.get("type") == "const" and n.get("id") not in exposed_nodes]
    if not consts:
        g["name"] = str(g.get("name", "")) + " m"
        return g
    node = consts[rng.randrange(len(consts))]
    value = node.get("params", {}).get("value", 0)
    if isinstance(value, list):
        value = list(value)
        value[0] = max(-1000.0, min(1000.0, float(value[0]) + 0.01))
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        value = max(-1000.0, min(1000.0, float(value) + 0.01))
    else:
        g["name"] = str(g.get("name", "")) + " m"
        return g
    node.setdefault("params", {})["value"] = value
    return g


def restamp_if_needed(mod, original, mutated):
    if original.get("schema") != mod.SCHEMA_V2:
        return mutated
    rev = original.get("rev") if isinstance(original.get("rev"), dict) else {}
    return stamp(
        mod, mutated,
        number=rev.get("number", 1),
        parent=rev.get("parent_rev"),
        note=rev.get("note", NOTE),
        created=rev.get("created", CREATED),
    )


def add_case(cases, mod, name, category, profile, expect, fn, needle=None):
    cases.append(Case(name, category, profile, expect, needle, fn))


def add_both(cases, mods, profiles, name, category, expect_for, builder, needle_for=None):
    for profile in profiles:
        mod = mods[profile]
        expect = expect_for(profile)
        needle = None if needle_for is None else needle_for(profile)
        graph_reg_fn = builder(mod)

        def fn(graph_reg_fn=graph_reg_fn, mod=mod, expect=expect):
            g, reg = graph_reg_fn()
            if expect == "accept":
                check_ok(mod, g, reg)
            else:
                mod.full_check(g, reg)

        add_case(cases, mod, name, category, profile, expect, fn, needle)


def structured_cases(mods, profiles, seeds, rng):
    cases = []
    regs = {name: seed_registry(mod, seeds) for name, mod in mods.items()}

    for profile in profiles:
        mod = mods[profile]
        reg = regs[profile]
        for graph in seeds:
            gid = effect_of(graph)

            def fn(graph=graph, mod=mod, reg=reg):
                check_ok(mod, graph, reg)

            add_case(cases, mod, f"seed {gid} r", "seed", profile, "accept", fn)

            if not has_subeffect(graph):
                issues = oracle_issues(graph, 32, mod.MAX_COST)

                def ofn(issues=issues, gid=gid):
                    if issues:
                        raise RuntimeError(f"oracle rejected seed {gid}: {issues}")

                add_case(cases, mod, f"oracle {gid}", "oracle_seed", profile, "accept", ofn)

        for index, graph in enumerate(seeds):
            order = list(range(len(graph["nodes"])))
            rng.shuffle(order)
            mutated = copy.deepcopy(graph)
            mutated["nodes"] = [mutated["nodes"][i] for i in order]
            mutated = restamp_if_needed(mod, graph, mutated)

            def fn(mutated=mutated, mod=mod, reg=reg):
                check_ok(mod, mutated, reg)

            add_case(cases, mod, f"reorder {effect_of(graph)} {index}", "valid_mutation", profile, "accept", fn)

            ids = [n["id"] for n in graph["nodes"] if isinstance(n.get("id"), str) and "/" not in n["id"] and "." not in n["id"]]
            if ids:
                old = ids[0]
                new = "z" + old
                if new not in ids and "/" not in new and "." not in new:
                    renamed = restamp_if_needed(mod, graph, rename_id(graph, old, new))

                    def fn(renamed=renamed, mod=mod, reg=reg):
                        check_ok(mod, renamed, reg)

                    add_case(cases, mod, f"rename {effect_of(graph)}", "valid_mutation", profile, "accept", fn)

            tweaked = restamp_if_needed(mod, graph, tweak_const(graph, rng))

            def fn(tweaked=tweaked, mod=mod, reg=reg):
                check_ok(mod, tweaked, reg)

            add_case(cases, mod, f"tweak {effect_of(graph)}", "valid_mutation", profile, "accept", fn)

    def limits_for(profile):
        mod = mods[profile]
        # Authored cap is 32 on both. Flat caps differ.
        flat_nodes = mod.MAX_NODES
        flat_cost = mod.MAX_COST
        return flat_nodes, flat_cost

    def expect_nodes(profile, count):
        return "accept" if count <= limits_for(profile)[0] else "reject"

    def expect_cost(profile, cost, nodes):
        flat_nodes, flat_cost = limits_for(profile)
        if nodes > flat_nodes or cost > flat_cost:
            return "reject"
        return "accept"

    def needle_nodes(profile, count):
        if count <= 32:
            return None
        if count <= limits_for(profile)[0]:
            return None
        return "nodes >"

    # Authored 32 / 33 on a single graph (no inlining).
    for count, category in ((32, "authored_at"), (33, "authored_over")):
        def builder(mod, count=count):
            nodes, out = const_chain(count, 0.15)
            g = stamp(mod, bare(mod, "fx_authored", nodes, out))
            return lambda g=g: (g, mod.Registry(graphs=[g]))

        add_both(
            cases, mods, profiles, f"authored {count}", category,
            (lambda profile, count=count: "accept" if count <= 32 else "reject"),
            builder,
            (lambda profile, count=count: "before flattening" if count > 32 else None),
        )

    # Flattened node counts via two children + one add.
    # 32 = a single chain (also the authored cap). 33, 64, 65 use inlining.
    singles = [(32, "flat_nodes_at")]
    for count, category in singles:
        def builder(mod, count=count):
            nodes, out = const_chain(count, 0.2)
            g = stamp(mod, bare(mod, "fx_flat_single", nodes, out))
            return lambda g=g: (g, mod.Registry(graphs=[g]))

        add_both(
            cases, mods, profiles, f"flat nodes {count}", category,
            (lambda profile, count=count: expect_nodes(profile, count)),
            builder,
            (lambda profile, count=count: needle_nodes(profile, count)),
        )

    for left, right, category, label in (
        (16, 16, "flat_nodes_between", "flat 33"),
        (32, 31, "flat_nodes_between", "flat 64"),
        (32, 32, "flat_nodes_over", "flat 65"),
    ):
        total = left + right + 1

        def builder(mod, left=left, right=right, total=total):
            g, reg = combine_two(mod, "fx_flat_combo", "fx_flat_l", "fx_flat_r", left, right, 0.11, 0.22)
            return lambda g=g, reg=reg: (g, reg)

        add_both(
            cases, mods, profiles, label, category,
            (lambda profile, total=total: expect_nodes(profile, total)),
            builder,
            (lambda profile, total=total: needle_nodes(profile, total)),
        )

    # Cost 64 (accept both) and 65 (over current, inside target) on one authored graph.
    for n_hue, extra, category, label in ((16, False, "cost_at", "cost 64"), (16, True, "cost_between", "cost 65")):
        def builder(mod, n_hue=n_hue, extra=extra):
            nodes, out = hue_chain(n_hue, 0.125)
            if extra:
                nodes.append({"id": "extra", "type": "add", "inputs": [out, out]})
                out = "extra"
            g = stamp(mod, bare(mod, "fx_cost_small", nodes, out))
            return lambda g=g: (g, mod.Registry(graphs=[g]))

        cost = 4 * n_hue + (1 if extra else 0)
        nodes_n = 2 + n_hue + (1 if extra else 0)
        add_both(
            cases, mods, profiles, label, category,
            (lambda profile, cost=cost, nodes_n=nodes_n: expect_cost(profile, cost, nodes_n)),
            builder,
            (lambda profile, cost=cost, nodes_n=nodes_n: (
                "op cost" if nodes_n <= limits_for(profile)[0] and cost > limits_for(profile)[1] else (
                    "nodes >" if nodes_n > limits_for(profile)[0] else None))),
        )

    def builder_128(mod, extra=0):
        children = []
        for index, turn in enumerate((0.1, 0.2, 0.3)):
            nodes, out = hue_chain(10, turn)
            nodes.append({"id": "p", "type": "add", "inputs": [out, out]})
            nodes.append({"id": "q", "type": "add", "inputs": ["p", out]})
            children.append(stamp(mod, bare(mod, f"fx_hc_{index}", nodes, "q")))
        pnodes = [sub_ref(f"s{i}", effect_of(child)) for i, child in enumerate(children)]
        pnodes.append({"id": "a", "type": "add", "inputs": ["s0.base_color", "s1.base_color"]})
        pnodes.append({"id": "b", "type": "add", "inputs": ["a", "s2.base_color"]})
        out = "b"
        if extra:
            pnodes.append({"id": "c", "type": "add", "inputs": ["b", "b"]})
            out = "c"
        parent = stamp(mod, bare(mod, "fx_cost_top", pnodes, out))
        reg = mod.Registry(graphs=children + [parent])
        return lambda parent=parent, reg=reg: (parent, reg)

    # Measured once on the target evaluator (it can hold the combined graph).
    probe_g, probe_reg = builder_128(mods["target"], 0)()
    flat128, _, cost128, _ = mods["target"].full_check(probe_g, probe_reg)
    nodes128 = len(flat128["nodes"])
    # One more live add on that graph. It is over the target cost cap, so it
    # cannot be full_check'd here; the extra node is unique and stays live.
    cost129 = cost128 + 1
    nodes129 = nodes128 + 1
    if cost128 != 128:
        raise RuntimeError(f"cost builder drifted: {cost128} nodes {nodes128}")

    add_both(
        cases, mods, profiles, "cost 128", "cost_at",
        (lambda profile, nodes128=nodes128, cost128=cost128: expect_cost(profile, cost128, nodes128)),
        lambda mod: builder_128(mod, 0),
        (lambda profile, nodes128=nodes128, cost128=cost128: (
            "op cost" if nodes128 <= limits_for(profile)[0] and cost128 > limits_for(profile)[1] else (
                "nodes >" if nodes128 > limits_for(profile)[0] else None))),
    )
    add_both(
        cases, mods, profiles, "cost 129", "cost_over",
        (lambda profile, nodes129=nodes129, cost129=cost129: expect_cost(profile, cost129, nodes129)),
        lambda mod: builder_128(mod, 1),
        (lambda profile, nodes129=nodes129, cost129=cost129: (
            "op cost" if nodes129 <= limits_for(profile)[0] and cost129 > limits_for(profile)[1] else (
                "nodes >" if nodes129 > limits_for(profile)[0] else None))),
    )

    # Depth and cycles.
    for profile in profiles:
        mod = mods[profile]
        for length, category, expect, needle in (
            (4, "depth_3", "accept", None),
            (5, "depth_4", "reject", "depth"),
            (7, "depth_longer", "reject", "depth"),
        ):
            g, reg = depth_chain(mod, length)

            def fn(g=g, reg=reg, mod=mod, expect=expect):
                if expect == "accept":
                    check_ok(mod, g, reg)
                else:
                    mod.full_check(g, reg)

            add_case(cases, mod, f"depth chain {length}", category, profile, expect, fn, needle)

        self_g = stamp(mod, bare(mod, "fx_self", [sub_ref("s", "fx_self")], "s.base_color"))
        self_reg = mod.Registry(graphs=[self_g])

        def fn(g=self_g, reg=self_reg, mod=mod):
            mod.full_check(g, reg)

        add_case(cases, mod, "cycle self", "cycle_self", profile, "reject", fn, "cycle")

        pair, preg = cycle_pair(mod, "fx_ca", "fx_cb")

        def fn(g=pair, reg=preg, mod=mod):
            mod.full_check(g, reg)

        add_case(cases, mod, "cycle pair", "cycle_pair", profile, "reject", fn, "cycle")

        long_g, lreg = cycle_long(mod)

        def fn(g=long_g, reg=lreg, mod=mod):
            mod.full_check(g, reg)

        add_case(cases, mod, "cycle long", "cycle_long", profile, "reject", fn, "cycle")

    # Non-finite constants and -0.
    for profile in profiles:
        mod = mods[profile]
        reg = mod.Registry(graphs=[])
        for label, value, category, needle in (
            ("nan", float("nan"), "nan", "non-finite"),
            ("+inf", float("inf"), "pos_inf", "non-finite"),
            ("-inf", float("-inf"), "neg_inf", "non-finite"),
            ("1e309", float("1e309"), "huge_exp", "non-finite"),
        ):
            g = bare(mod, "fx_nf", [{"id": "c", "type": "const", "params": {"value": value}}], "c")
            g["rev"] = {"number": 1, "parent_rev": None, "hash": "sha256:" + "0" * 64,
                        "note": "n", "created": CREATED}

            def fn(g=g, mod=mod, reg=reg):
                mod.full_check(g, reg)

            add_case(cases, mod, label, category, profile, "reject", fn, needle)

        zero = stamp(mod, bare(mod, "fx_z", [{"id": "c", "type": "const", "params": {"value": 0.0}}], "c"))
        neg = stamp(mod, bare(mod, "fx_z", [{"id": "c", "type": "const", "params": {"value": -0.0}}], "c"))

        def fn(zero=zero, neg=neg, mod=mod, reg=reg):
            if zero["rev"]["hash"] != neg["rev"]["hash"]:
                raise RuntimeError("-0 and 0 hashed differently")
            check_ok(mod, neg, reg)

        add_case(cases, mod, "neg zero", "neg_zero", profile, "accept", fn)

        # v1 validate path rejects non-finite consts as out of range.
        v1 = {
            "schema": mod.SCHEMA, "id": "fx_v1_nan", "name": "n", "animated": False,
            "nodes": [{"id": "c", "type": "const", "params": {"value": float("nan")}}],
            "outputs": {"base_color": "c"},
        }

        def fn(v1=v1, mod=mod):
            mod.validate(v1)

        add_case(cases, mod, "v1 nan", "nan", profile, "reject", fn, "out of range")

    # Bad wiring on a small v2 graph and on fx_sparkle.
    for profile in profiles:
        mod = mods[profile]
        reg = regs[profile]

        g = stamp(mod, bare(mod, "fx_bad", [
            {"id": "c", "type": "const", "params": {"value": 0.2}},
            {"id": "a", "type": "add", "inputs": ["missing", "c"]},
        ], "a"))

        def fn(g=g, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "unknown node", "unknown_node", profile, "reject", fn, "unknown")

        g = stamp(mod, bare(mod, "fx_bad", [
            {"id": "c", "type": "const", "params": {"value": 0.2}},
            {"id": "a", "type": "add", "inputs": ["a", "c"]},
        ], "a"))

        def fn(g=g, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "self ref", "self_ref", profile, "reject", fn, "cycle")

        g = bare(mod, "fx_bad", [
            {"id": "c", "type": "const", "params": {"value": 0.2}},
            {"id": "c", "type": "const", "params": {"value": 0.3}},
        ], "c")
        g = stamp(mod, g)

        def fn(g=g, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "duplicate id", "duplicate_id", profile, "reject", fn, "duplicate")

        for bad_id, category in (("a/b", "id_slash"), ("a.b", "id_dot")):
            g = stamp(mod, bare(mod, "fx_bad", [
                {"id": bad_id, "type": "const", "params": {"value": 0.2}},
            ], bad_id))

            def fn(g=g, mod=mod):
                mod.full_check(g, mod.Registry(graphs=[g]))

            add_case(cases, mod, bad_id, category, profile, "reject", fn, "bad or duplicate")

        g = stamp(mod, bare(mod, "fx_bad", [
            {"id": "f", "type": "const", "params": {"value": 0.2}},
            {"id": "h", "type": "hue_shift", "inputs": ["f", "f"]},
        ], "h"))

        def fn(g=g, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "f into hue", "type_mismatch", profile, "reject", fn, "hue_shift")

        g = stamp(mod, {
            "schema": mod.SCHEMA_V2, "effect_id": "fx_bad", "name": "b", "animated": False,
            "nodes": [{"id": "c", "type": "const", "params": {"value": 0.2}}],
            "outputs": {},
        })

        def fn(g=g, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "empty outputs", "missing_outputs_empty", profile, "reject", fn, "base_color")

        sparkle = stamp(mod, {
            "schema": mod.SCHEMA_V2, "effect_id": "fx_wrap_spark", "name": "w",
            "animated": True, "loop_seconds": 1.0,
            "nodes": [sub_ref("s", "fx_sparkle", 2, args={"density": 2.0})],
            "outputs": {"base_color": "s.base_color"},
        })

        def fn(g=sparkle, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "arg range", "arg_range", profile, "reject", fn, "outside")

        unknown = stamp(mod, {
            "schema": mod.SCHEMA_V2, "effect_id": "fx_wrap_spark", "name": "w",
            "animated": True, "loop_seconds": 1.0,
            "nodes": [sub_ref("s", "fx_sparkle", 2, args={"nope": 1.0})],
            "outputs": {"base_color": "s.base_color"},
        })

        def fn(g=unknown, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "unknown exposed", "unknown_exposed", profile, "reject", fn, "exposes no")

        wired = stamp(mod, {
            "schema": mod.SCHEMA_V2, "effect_id": "fx_wrap_spark", "name": "w",
            "animated": True, "loop_seconds": 1.0,
            "nodes": [
                {"id": "t", "type": "input", "params": {"source": "time"}},
                sub_ref("s", "fx_sparkle", 2, wires={"density": "t"}),
            ],
            "outputs": {"base_color": "s.base_color"},
        })

        def fn(g=wired, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "wire param", "wire_param", profile, "reject", fn, "param")

        missing_out = stamp(mod, {
            "schema": mod.SCHEMA_V2, "effect_id": "fx_wrap_spark", "name": "w",
            "animated": True, "loop_seconds": 1.0,
            "nodes": [sub_ref("s", "fx_sparkle", 2)],
            "outputs": {"base_color": "s.unknown_output"},
        })

        def fn(g=missing_out, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "unknown output", "unknown_output", profile, "reject", fn, "no output")

    # Revision / hash.
    for profile in profiles:
        mod = mods[profile]
        reg = regs[profile]
        base = next(g for g in seeds if effect_of(g) == "fx_sparkle" and g.get("schema") == mod.SCHEMA_V2)
        tampered = copy.deepcopy(base)
        tampered["name"] = "tampered name"

        def fn(g=tampered, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "tamper", "tamper", profile, "reject", fn, "hash")

        wrong = copy.deepcopy(base)
        wrong["rev"] = dict(wrong["rev"])
        wrong["rev"]["hash"] = "sha256:" + "ab" * 32

        def fn(g=wrong, mod=mod, reg=reg):
            mod.full_check(g, reg)

        add_case(cases, mod, "wrong hash", "wrong_hash", profile, "reject", fn, "hash")

        parent = stamp(mod, bare(mod, "fx_parent", *const_chain(1, 0.3)), number=2, parent=2)

        def fn(g=parent, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "parent_rev", "parent_rev", profile, "reject", fn, "parent_rev")

        created = stamp(mod, bare(mod, "fx_created", *const_chain(1, 0.3)), created="yesterday")

        def fn(g=created, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "bad created", "bad_created", profile, "reject", fn, "created")

        long_note = stamp(mod, bare(mod, "fx_note", *const_chain(1, 0.3)), note="n" * 201)

        def fn(g=long_note, mod=mod):
            mod.full_check(g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "note 201", "note_long", profile, "reject", fn, "note")

        ok_note = stamp(mod, bare(mod, "fx_note", *const_chain(1, 0.3)), note="n" * 200)

        def fn(g=ok_note, mod=mod):
            check_ok(mod, g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "note 200", "note_at", profile, "accept", fn)

        first = leaf_graph(mod, "fx_clash", 0.1)
        second = leaf_graph(mod, "fx_clash", 0.2)

        def fn(a=first, b=second, mod=mod):
            mod.Registry(graphs=[a, b])

        add_case(cases, mod, "duplicate rev", "duplicate_rev", profile, "reject", fn, "claim")

    # Unicode that the schema allows (notes, node ids without / or .).
    for profile in profiles:
        mod = mods[profile]
        note = stamp(mod, bare(mod, "fx_note_u", *const_chain(1, 0.4)), note="sparkle " + "\u2728" + " \u202eok")

        def fn(g=note, mod=mod):
            check_ok(mod, g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "unicode note", "unicode_note", profile, "accept", fn)

        zw = bare(mod, "fx_zw", [{"id": "c\u200b", "type": "const", "params": {"value": 0.2}}], "c\u200b")
        zw = stamp(mod, zw)
        h1 = mod.content_hash(zw)

        def fn(g=zw, h1=h1, mod=mod):
            if mod.content_hash(g) != h1 or mod.content_hash(json.loads(json.dumps(g))) != h1:
                raise RuntimeError("zero-width id hash was not stable")
            check_ok(mod, g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "zwsp node", "unicode_node", profile, "accept", fn)

        nul = stamp(mod, bare(mod, "fx_nul", [{"id": "a\u0000b", "type": "const", "params": {"value": 0.2}}], "a\u0000b"))

        def fn(g=nul, mod=mod):
            again = json.loads(json.dumps(g))
            if mod.content_hash(g) != mod.content_hash(again):
                raise RuntimeError("NUL id hash was not stable")
            check_ok(mod, g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "nul node", "nul_node", profile, "accept", fn)

        nfc = bare(mod, "fx_norm", *const_chain(1, 0.5))
        nfd = copy.deepcopy(nfc)
        nfc["name"] = "caf\u00e9"
        nfd["name"] = "cafe\u0301"

        def fn(nfc=nfc, nfd=nfd, mod=mod):
            if mod.content_hash(nfc) == mod.content_hash(nfd):
                raise RuntimeError("NFC and NFD hashed the same")
            if mod.content_hash(nfc) != mod.content_hash(json.loads(json.dumps(nfc, ensure_ascii=False))):
                raise RuntimeError("NFC hash was not stable")
            if mod.content_hash(nfd) != mod.content_hash(json.loads(json.dumps(nfd, ensure_ascii=False))):
                raise RuntimeError("NFD hash was not stable")

        add_case(cases, mod, "nfc nfd", "nfc_nfd", profile, "accept", fn)

    # Huge inputs that must reject quickly, plus one in-range 1 MB name.
    for profile in profiles:
        mod = mods[profile]
        def fn_real(mod=mod):
            tracemalloc.start()
            try:
                nodes = [{"id": f"n{i}", "type": "const", "params": {"value": 0}} for i in range(10_000)]
                g = {"schema": mod.SCHEMA, "id": "fx_huge", "name": "h", "animated": False,
                     "nodes": nodes, "outputs": {"base_color": "n0"}}
                _current, peak = tracemalloc.get_traced_memory()
                if peak > MEMORY_BUDGET:
                    raise RuntimeError(f"peak {peak} bytes over memory budget")
                mod.validate(g)
            finally:
                tracemalloc.stop()

        add_case(cases, mod, "nodes 10000", "huge_nodes", profile, "reject", fn_real, "nodes >")

        class _LengthOnly(list):
            """len() of a million nodes without allocating the bodies.

            validate() rejects on len() before walking nodes. Materialising a
            million dicts is allocator work and exceeds the 2s case budget
            once the process is already holding registries.
            """

            def __init__(self, n):
                self._n = n

            def __len__(self):
                return self._n

        def fn_million(mod=mod):
            g = {"schema": mod.SCHEMA, "id": "fx_huge", "name": "h", "animated": False,
                 "nodes": _LengthOnly(1_000_000), "outputs": {"base_color": "n0"}}
            mod.validate(g)

        add_case(cases, mod, "nodes 1000000", "huge_nodes", profile, "reject", fn_million, "nodes >")

        def fn(mod=mod):
            g = stamp(mod, bare(mod, "fx_long_name", *const_chain(1, 0.2)))
            g["name"] = "x" * (1024 * 1024)
            # name is inside the hash, so restamp after growing it
            g = stamp(mod, g)
            check_ok(mod, g, mod.Registry(graphs=[g]))

        add_case(cases, mod, "1MB name", "huge_string", profile, "accept", fn)

        def fn(mod=mod):
            mod.make_bundle(regs[profile], ["fx_sparkle@2"], "ai_context", "p" * (1024 * 1024))

        add_case(cases, mod, "1MB prompt", "huge_string", profile, "reject", fn, "prompt")

    # Bundles.
    for profile in profiles:
        mod = mods[profile]
        reg = regs[profile]
        root64, graphs64 = wide_bundle_graphs(mod, 7, 8)
        if len(graphs64) != 64:
            raise RuntimeError(f"bundle builder 64 produced {len(graphs64)}")
        reg64 = mod.Registry(graphs=graphs64)

        def fn(root=root64, reg64=reg64, mod=mod):
            bundle = mod.make_bundle(reg64, [effect_of(root) + "@1"], "mailbox")
            if len(bundle["graphs"]) != 64:
                raise RuntimeError(f"closure {len(bundle['graphs'])} != 64")
            mod.verify_bundle(json.loads(json.dumps(bundle)))

        add_case(cases, mod, "bundle graphs 64", "bundle_graphs_at", profile, "accept", fn)

        root65, graphs65 = wide_bundle_graphs(mod, 8, 7)
        if len(graphs65) != 65:
            raise RuntimeError(f"bundle builder 65 produced {len(graphs65)}")
        reg65 = mod.Registry(graphs=graphs65)

        def fn(root=root65, reg65=reg65, mod=mod):
            mod.make_bundle(reg65, [effect_of(root) + "@1"], "mailbox")

        add_case(cases, mod, "bundle graphs 65", "bundle_graphs_over", profile, "reject", fn, "too large")

        def fn(mod=mod, reg=reg):
            mod.make_bundle(reg, ["fx_sparkle@2"] * 8, "mailbox")

        add_case(cases, mod, "items 8", "bundle_items_at", profile, "accept", fn)

        def fn(mod=mod, reg=reg):
            mod.make_bundle(reg, ["fx_sparkle@2"] * 9, "mailbox")

        add_case(cases, mod, "items 9", "bundle_items_over", profile, "reject", fn, "items")

        def fn(mod=mod, reg=reg):
            mod.make_bundle(reg, ["fx_sparkle@2"], "ai_context", "p" * 4000)

        add_case(cases, mod, "prompt 4000", "prompt_at", profile, "accept", fn)

        def fn(mod=mod, reg=reg):
            mod.make_bundle(reg, ["fx_sparkle@2"], "ai_context", "p" * 4001)

        add_case(cases, mod, "prompt 4001", "prompt_over", profile, "reject", fn, "prompt")

        good = mod.make_bundle(reg, ["fx_frosted_sparkle@1", finish_base()], "mailbox")

        def fn(good=good, mod=mod):
            mod.verify_bundle(json.loads(json.dumps(good)))

        add_case(cases, mod, "finish ok", "finish_ok", profile, "accept", fn)

        for label, mutate, needle in (
            ("strength", lambda f: f.__setitem__("strength", 8.1), "outside"),
            ("pattern", lambda f: f.__setitem__("pattern_id", 16), "pattern_id"),
            ("glow", lambda f: f.__setitem__("glow_mask_id", 4), "glow_mask_id"),
            ("palette", lambda f: f.__setitem__("palette_a", "red"), "#RRGGBB"),
        ):
            bad_finish = finish_base()
            mutate(bad_finish)

            def fn(bad_finish=bad_finish, mod=mod, reg=reg):
                mod.make_bundle(reg, [bad_finish], "mailbox")

            add_case(cases, mod, "finish " + label, "finish_range", profile, "reject", fn, needle)

        def fn(good=good, mod=mod):
            broken = json.loads(json.dumps(good))
            broken["graphs"].pop()
            mod.verify_bundle(broken)

        add_case(cases, mod, "bundle missing", "bundle_missing", profile, "reject", fn, "mismatch")

        extra_g = leaf_graph(mod, "fx_extra_graph", 0.4)

        def fn(good=good, extra_g=extra_g, mod=mod):
            broken = json.loads(json.dumps(good))
            broken["graphs"].append(extra_g)
            mod.verify_bundle(broken)

        add_case(cases, mod, "bundle extra", "bundle_extra", profile, "reject", fn, "differ")

        def fn(good=good, mod=mod):
            broken = json.loads(json.dumps(good))
            broken["manifest"]["closure"].reverse()
            mod.verify_bundle(broken)

        add_case(cases, mod, "bundle reorder", "bundle_reorder", profile, "reject", fn, "bundle_hash")

        def fn(good=good, mod=mod):
            broken = json.loads(json.dumps(good))
            closure = broken["manifest"]["closure"]
            closure[0]["hash"], closure[1]["hash"] = closure[1]["hash"], closure[0]["hash"]
            broken["manifest"]["bundle_hash"] = mod.sha256_tag(
                mod.canonical_json(bundle_hash_payload(broken["manifest"])))
            mod.verify_bundle(broken)

        add_case(cases, mod, "hash swap", "bundle_hash_swap", profile, "reject", fn, "closure hash")

    # Determinism and hash sensitivity.
    for profile in profiles:
        mod = mods[profile]
        reg = regs[profile]
        sample = next(g for g in seeds if effect_of(g) == "fx_frosted_sparkle")

        def fn(sample=sample, mod=mod, reg=reg):
            h1 = mod.content_hash(sample)
            h2 = mod.content_hash(json.loads(json.dumps(sample, sort_keys=True, indent=2)))
            h3 = mod.content_hash(json.loads(json.dumps(sample, indent=4, ensure_ascii=False)))
            if not (h1 == h2 == h3):
                raise RuntimeError("content hash depended on key order or whitespace")
            k1, _ = mod.bake_key(sample, reg)
            k2, _ = mod.bake_key(json.loads(json.dumps(sample)), reg)
            if k1 != k2:
                raise RuntimeError("bake key was not stable")

        add_case(cases, mod, "determinism", "determinism", profile, "accept", fn)

        def fn(sample=sample, mod=mod):
            original = mod.content_hash(sample)
            preview = copy.deepcopy(sample)
            preview.setdefault("preview", {})
            preview["preview"]["pattern_id"] = 7 if preview["preview"].get("pattern_id") != 7 else 1
            if mod.content_hash(preview) != original:
                raise RuntimeError("preview changed the content hash")
            renamed = copy.deepcopy(sample)
            renamed["name"] = str(renamed.get("name", "")) + " changed"
            if mod.content_hash(renamed) == original:
                raise RuntimeError("renaming did not change the content hash")
            rev_only = copy.deepcopy(sample)
            rev_only["rev"] = dict(rev_only["rev"])
            rev_only["rev"]["note"] = "different note"
            if mod.content_hash(rev_only) != original:
                raise RuntimeError("rev.note changed the content hash")

        add_case(cases, mod, "hash sensitivity", "hash_sensitivity", profile, "accept", fn)

        def fn(sample=sample, mod=mod, reg=reg):
            flat, _, _, _ = mod.full_check(sample, reg)
            if any(n.get("type") == "subeffect" for n in flat["nodes"]):
                raise RuntimeError("subeffect remained")
            ids = [n["id"] for n in flat["nodes"]]
            if len(ids) != len(set(ids)):
                raise RuntimeError("duplicate flattened ids")
            mod.validate(flat)
            if len(flat["nodes"]) > mod.MAX_NODES:
                raise RuntimeError("flatten invariant limit")

        add_case(cases, mod, "flatten invariants", "flatten_invariants", profile, "accept", fn)

    return cases


def _pick(rng, pool):
    return pool[rng.randrange(len(pool))]


def random_graph(rng, mod, max_cost, fault):
    nodes = []
    seq = 0

    def nid():
        nonlocal seq
        seq += 1
        return f"n{seq}"

    def add(node, kind):
        nodes.append(node)
        return node["id"]

    cursors = {
        "v3": add({"id": nid(), "type": "input", "params": {"source": "palette_a"}}, "v3"),
        "v2": add({"id": nid(), "type": "input", "params": {"source": "uv"}}, "v2"),
        "f": add({"id": nid(), "type": "const", "params": {"value": round(rng.uniform(-2, 2), 4)}}, "f"),
    }
    cost = 0
    steps = rng.randint(1, 10)
    for _ in range(steps):
        options = []
        for op, (arity, op_cost) in OPS.items():
            if op == "hue_shift" and cursors["v3"] and cursors["f"]:
                options.append(op)
            elif op == "scroll_uv" and cursors["v2"] and cursors["f"]:
                options.append(op)
            elif op == "rotate_uv" and cursors["v2"] and cursors["f"]:
                options.append(op)
            elif op == "sin":
                options.append(op)
            elif op in ("add", "mul", "step", "smoothstep", "clamp", "lerp"):
                options.append(op)
        if not options:
            break
        op = options[rng.randrange(len(options))]
        _arity, op_cost = OPS[op]
        if fault != "cost" and cost + op_cost > max_cost:
            continue
        if op == "hue_shift":
            ins = [cursors["v3"], cursors["f"]]
            kind = "v3"
        elif op == "scroll_uv":
            speed = add({"id": nid(), "type": "const", "params": {"value": [
                round(rng.uniform(-1, 1), 4), round(rng.uniform(-1, 1), 4)]}}, "v2")
            ins = [cursors["v2"], speed, cursors["f"]]
            kind = "v2"
        elif op == "rotate_uv":
            ins = [cursors["v2"], cursors["f"]]
            kind = "v2"
        elif op == "sin":
            kind = rng.choice([k for k, v in cursors.items() if v])
            ins = [cursors[kind]]
        else:
            kind = rng.choice(["f", "v2", "v3"])
            if op == "lerp":
                other = cursors["f"]
                ins = [cursors[kind], cursors[kind], other if rng.random() < 0.5 else cursors[kind]]
            elif op in ("smoothstep", "clamp"):
                ins = [cursors[kind], cursors[kind], cursors[kind]]
            else:
                ins = [cursors[kind], cursors["f"] if rng.random() < 0.4 else cursors[kind]]
        new_id = nid()
        nodes.append({"id": new_id, "type": op, "inputs": ins})
        cursors[kind] = new_id
        cost += op_cost
        if fault == "cost" and cost > max_cost:
            break

    if fault == "cost" and cost <= max_cost:
        # Force the current 64-op cap. Target (128) cannot be exceeded inside
        # 32 authored nodes of hue_shift, so this fault is only requested then.
        prev = cursors["v3"]
        while cost <= max_cost and len(nodes) < 32:
            new_id = nid()
            nodes.append({"id": new_id, "type": "hue_shift", "inputs": [prev, cursors["f"]]})
            prev = new_id
            cursors["v3"] = new_id
            cost += 4

    outputs = {"base_color": cursors["v3"]}
    if rng.random() < 0.5:
        outputs["emissive_strength"] = cursors["f"]
    g = prune_dead({
        "schema": mod.SCHEMA_V2, "effect_id": "fx_rand", "name": "rand", "animated": False,
        "nodes": nodes, "outputs": outputs,
    })
    if fault == "unknown":
        g["nodes"].append({"id": "bad", "type": "add", "inputs": ["missing_node", "missing_node"]})
        g["outputs"]["base_color"] = "bad"
    elif fault == "cycle":
        g["nodes"].append({"id": "bad", "type": "add", "inputs": ["bad", "bad"]})
        g["outputs"]["base_color"] = "bad"
    elif fault == "dup":
        g["nodes"].append(copy.deepcopy(g["nodes"][0]))
    elif fault == "type":
        g["nodes"].append({"id": "sf", "type": "const", "params": {"value": 0.1}})
        g["nodes"].append({"id": "bad", "type": "hue_shift", "inputs": ["sf", "sf"]})
        g["outputs"]["base_color"] = "bad"
    elif fault == "nodes":
        while len(g["nodes"]) <= 32:
            g["nodes"].append({"id": nid(), "type": "const", "params": {"value": len(g["nodes"]) * 0.001}})
    elif fault == "slash":
        g["nodes"][0]["id"] = "bad/id"
    return stamp(mod, g)


def make_random_case(rng, mod, profile):
    max_cost = mod.MAX_COST
    roll = rng.random()
    fault = None
    if roll >= 0.8:
        choices = ["unknown", "cycle", "dup", "type", "nodes", "slash"]
        if profile == "current":
            choices.append("cost")
        fault = choices[rng.randrange(len(choices))]
    graph = random_graph(rng, mod, max_cost, fault)
    # Random graphs are judged by the authored cap (32) plus this profile's cost cap.
    # Flattened node count cannot exceed the authored count when nothing is inlined.
    issues = oracle_issues(graph, 32, max_cost)
    expect = "reject" if issues else "accept"
    category = "random_reject" if issues else "random_accept"

    def fn(graph=graph, mod=mod, expect=expect):
        reg = mod.Registry(graphs=[graph])
        if expect == "accept":
            check_ok(mod, graph, reg)
        else:
            mod.full_check(graph, reg)

    return Case(f"random {fault or 'ok'}", category, profile, expect, None, fn)


def execute(case):
    started = time.perf_counter()
    try:
        case.fn()
        status, message = "accept", ""
    except MODULES[case.profile].GraphError as ex:
        status, message = "reject", str(ex)
    except Exception as ex:
        status, message = "crash", f"{type(ex).__name__}: {ex}"
    elapsed = time.perf_counter() - started
    return status, message, elapsed


def run_suite(seed=1234, cases=2000, profile="both"):
    if profile not in ("current", "target", "both"):
        raise SystemExit("VF_FUZZ_PROFILE must be current, target, or both")
    profiles = ["current", "target"] if profile == "both" else [profile]
    rng = random.Random(seed)
    seeds = load_seeds()
    result = SuiteResult(seed, cases, profile)
    started = time.perf_counter()
    built = structured_cases(MODULES, profiles, seeds, rng)
    remaining = max(0, cases - len(built))
    for index in range(remaining):
        name = profiles[index % len(profiles)]
        built.append(make_random_case(rng, MODULES[name], name))
    for case in built:
        status, message, elapsed = execute(case)
        result.executed += 1
        result.counts[case.category] = result.counts.get(case.category, 0) + 1
        failed = False
        reason = ""
        if elapsed > CASE_BUDGET_S:
            failed = True
            reason = f"budget {elapsed:.3f}s"
        elif status == "crash":
            failed = True
            reason = message
        elif status != case.expect:
            failed = True
            reason = message
        elif case.expect == "reject" and case.needle and case.needle not in message:
            failed = True
            reason = message
        if failed:
            result.fail_n[case.profile] += 1
            result.failures.append({
                "profile": case.profile, "category": case.category, "name": case.name,
                "expect": case.expect, "status": status, "msg": reason or message,
            })
        else:
            result.pass_n[case.profile] += 1
    result.elapsed = time.perf_counter() - started
    if profile == "both":
        missing = [name for name in REQUIRED_CATEGORIES if result.counts.get(name, 0) < 1]
        if missing:
            result.failures.append({
                "profile": "both", "category": "coverage", "name": "required categories",
                "expect": "present", "status": "missing", "msg": ", ".join(missing),
            })
    return result


def run_from_env():
    seed = int(os.environ.get("VF_FUZZ_SEED", "1234"))
    cases = int(os.environ.get("VF_FUZZ_CASES", "2000"))
    profile = os.environ.get("VF_FUZZ_PROFILE", "both")
    return run_suite(seed, cases, profile)


if __name__ == "__main__":
    outcome = run_from_env()
    print(outcome.summary())
    sys.exit(0 if outcome.ok else 1)
