"""VoxelFinish effect graphs: evaluator v3.
Same rules as v2, with the approved combined-effect limits:
  MAX_AUTHORED_NODES = 32   (each graph, before inlining)
  MAX_FLAT_NODES = 64       (flattened / combined effect)
  MAX_FLAT_COST = 128
  MAX_DEPTH = 3
v2 remains the current profile (32 flattened nodes / 64 op cost). Diff against v2:
  - those three constants, and MAX_NODES/MAX_COST aliased to the flat limits
  - flatten() node cap uses MAX_AUTHORED_NODES
  - validate() therefore enforces the flattened limits (it is called on the combined graph)
Adds to v1: revision metadata + canonical content hashing, SubEffect inlining (max depth 3,
cycle checks, flattened node / op limits above), bake keys, history / revert / branch, bundles.
v1 graphs (schema gfgame.effect_graph/1) still load and validate unchanged.

Usage:
  python fxgraph_eval_v3.py validate  FILE...            # flatten + validate (resolves SubEffects via the registry)
  python fxgraph_eval_v3.py flatten   FILE               # print the flattened graph JSON
  python fxgraph_eval_v3.py hash      FILE...            # canonical content hash
  python fxgraph_eval_v3.py bakekey   FILE...            # hash with every 'latest' resolved to a concrete rev
  python fxgraph_eval_v3.py history   EFFECT_ID
  python fxgraph_eval_v3.py revert    EFFECT_ID REV OUT_DIR [NOTE]   # new rev copying REV (parent_rev = REV)
  python fxgraph_eval_v3.py branch    EFFECT_ID REV OUT_DIR [NOTE]   # new rev to edit, parent_rev = REV
  python fxgraph_eval_v3.py preview   OUT_DIR FILE...
  python fxgraph_eval_v3.py bundle    OUT_FILE PURPOSE ITEM... [--prompt TEXT]   # ITEM = effect_id@rev | finish.json
  python fxgraph_eval_v3.py verify-bundle FILE
Registry search path: ./graphs, ../effects_v1/graphs (override with env VOXELFINISH_GRAPH_DIRS, os.pathsep separated).
Note: under Tools/VoxelFinish/fuzz/vf_reference the default ../effects_v1/graphs and ../paint_v1/tiles
paths do not exist (they are layout of the original asset drop). The registry still finds ./graphs;
set VOXELFINISH_GRAPH_DIRS to add other graph folders. The preview command needs the pattern
tiles, so it fails here (the fuzz harness never calls it).
Never overwrites files."""
"""(core copied from fxgraph_eval_v1.py; v1 semantics unchanged)
Usage:
  python fxgraph_eval_v1.py validate graphs/*.json
  python fxgraph_eval_v1.py preview OUT_DIR graphs/*.json   (never overwrites)
Reference semantics only; the shipping evaluator lives in Unreal (C++/HLSL)."""
import json, math, os, sys
import numpy as np

SCHEMA = "gfgame.effect_graph/1"
SCHEMAS = ("gfgame.effect_graph/1", "voxelfinish.effect_graph/2")
TILE = 16
MAX_AUTHORED_NODES = 32
MAX_FLAT_NODES = 64
MAX_FLAT_COST = 128
# validate() runs on the flattened combined effect.
MAX_NODES, MAX_COST = MAX_FLAT_NODES, MAX_FLAT_COST
INPUTS = {"time": "f", "uv": "v2", "block_world_pos": "v3", "noise_value": "f",
          "noise_perlin": "f", "palette_a": "v3", "palette_b": "v3", "pattern_mask": "f"}
SAMPLED = {"noise_value", "noise_perlin", "pattern_mask"}   # may take one optional uv input
# op: (arity, cost)
OPS = {"add": (2, 1), "mul": (2, 1), "lerp": (3, 2), "sin": (1, 1), "step": (2, 1),
       "smoothstep": (3, 3), "clamp": (3, 2), "hue_shift": (2, 4), "scroll_uv": (3, 2),
       "rotate_uv": (2, 4)}
OUTPUTS = {"base_color": "v3", "emissive_strength": "f", "alpha_mask": "f"}
CONST_LIMIT = 1.0e4
DIM = {"f": 1, "v2": 2, "v3": 3}

class GraphError(Exception):
    pass

def _ctype(v):
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        vals = [v]; t = "f"
    elif isinstance(v, list) and len(v) in (2, 3) and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v):
        vals = v; t = "v2" if len(v) == 2 else "v3"
    else:
        raise GraphError(f"bad const value {v!r}")
    for x in vals:
        if not math.isfinite(x) or abs(x) > CONST_LIMIT:
            raise GraphError(f"const out of range {x!r}")
    return t

def _bcast(*ts):
    vs = [t for t in ts if t != "f"]
    if not vs:
        return "f"
    if len(set(vs)) > 1:
        raise GraphError(f"type mismatch {ts}")
    return vs[0]

def validate(g):
    """Returns (topo_order, types, cost, bakeable). Raises GraphError."""
    if g.get("schema") not in SCHEMAS:
        raise GraphError("schema must be one of " + ", ".join(SCHEMAS))
    if not (g.get("id") or g.get("effect_id")):
        raise GraphError("missing effect_id")
    for k in ("name", "animated", "nodes", "outputs"):
        if k not in g:
            raise GraphError("missing " + k)
    if g["animated"]:
        ls = g.get("loop_seconds")
        if not isinstance(ls, (int, float)) or not (0.25 <= ls <= 16):
            raise GraphError("animated graphs need loop_seconds in [0.25,16]")
    nodes = g["nodes"]
    if len(nodes) > MAX_NODES:
        raise GraphError(f"{len(nodes)} nodes > {MAX_NODES}")
    byid = {}
    for n in nodes:
        nid = n.get("id")
        if not isinstance(nid, str) or nid in byid:
            raise GraphError(f"bad/duplicate id {nid!r}")
        byid[nid] = n
    # topo sort (Kahn) => rejects cycles
    deps = {k: list(v.get("inputs", [])) for k, v in byid.items()}
    for k, ins in deps.items():
        for i in ins:
            if i not in byid:
                raise GraphError(f"{k}: unknown input {i}")
    order, done, visiting = [], set(), set()
    def visit(k):
        if k in done: return
        if k in visiting: raise GraphError("cycle at " + k)
        visiting.add(k)
        for i in deps[k]: visit(i)
        visiting.discard(k); done.add(k); order.append(k)
    for k in byid: visit(k)
    types, cost, bakeable = {}, 0, True
    for k in order:
        n = byid[k]; t = n.get("type"); p = n.get("params", {}); ins = n.get("inputs", [])
        it = [types[i] for i in ins]
        if t == "input":
            src = p.get("source")
            if src not in INPUTS: raise GraphError(f"{k}: unknown input source {src}")
            if src == "block_world_pos": bakeable = False
            if src in SAMPLED:
                if len(ins) > 1 or (ins and it[0] != "v2"): raise GraphError(f"{k}: sampled input takes 0 or 1 uv")
                if src != "pattern_mask":
                    sc = p.get("scale", 4)
                    if not isinstance(sc, int) or not 1 <= sc <= 16: raise GraphError(f"{k}: scale must be int 1..16")
                    seed = p.get("seed", 0)
                    if not isinstance(seed, int) or not 0 <= seed <= 65535: raise GraphError(f"{k}: seed must be int 0..65535")
            elif ins:
                raise GraphError(f"{k}: input {src} takes no inputs")
            out = INPUTS[src]
            comp = p.get("component")
            if comp is not None:
                if src not in ("uv", "block_world_pos") or comp not in "xyz"[:DIM[out]] or len(comp) != 1:
                    raise GraphError(f"{k}: bad component {comp}")
                out = "f"
            types[k] = out
        elif t == "const":
            if ins: raise GraphError(f"{k}: const takes no inputs")
            types[k] = _ctype(p.get("value"))
        elif t in OPS:
            ar, c = OPS[t]
            if len(ins) != ar: raise GraphError(f"{k}: {t} needs {ar} inputs")
            cost += c
            if t == "hue_shift":
                if it[0] != "v3" or it[1] != "f": raise GraphError(f"{k}: hue_shift(v3, f)")
                types[k] = "v3"
            elif t == "scroll_uv":
                if it[0] != "v2" or it[1] != "v2" or it[2] != "f": raise GraphError(f"{k}: scroll_uv(v2 uv, v2 speed, f time)")
                types[k] = "v2"
            elif t == "rotate_uv":
                if it[0] != "v2" or it[1] != "f": raise GraphError(f"{k}: rotate_uv(v2, f)")
                types[k] = "v2"
            elif t == "sin":
                types[k] = it[0]
            elif t == "lerp":
                if it[2] != "f" and it[2] != _bcast(it[0], it[1]): raise GraphError(f"{k}: lerp t")
                types[k] = _bcast(*it)
            else:
                types[k] = _bcast(*it)
        else:
            raise GraphError(f"{k}: unknown node type {t}")
    if cost > MAX_COST:
        raise GraphError(f"op cost {cost} > {MAX_COST}")
    outs = g["outputs"]
    if "base_color" not in outs: raise GraphError("base_color output required")
    for o, nid in outs.items():
        if o not in OUTPUTS: raise GraphError("unknown output " + o)
        if nid not in types: raise GraphError(f"output {o} -> unknown node {nid}")
        if OUTPUTS[o] == "f" and types[nid] != "f": raise GraphError(f"output {o} must be scalar")
        if o == "base_color" and types[nid] not in ("f", "v3"): raise GraphError("base_color must be v3 or f")
    return order, types, cost, bakeable

# ---------------- reference evaluation (per texel centre, 16x16)
def _hash(ix, iy, seed):
    h = (ix.astype(np.uint64) * np.uint64(1597334677)) ^ (iy.astype(np.uint64) * np.uint64(3812015801)) ^ np.uint64(seed * 2654435761 & 0xFFFFFFFF)
    h &= np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(16); h = (h * np.uint64(2246822519)) & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(13); h = (h * np.uint64(3266489917)) & np.uint64(0xFFFFFFFF)
    h ^= h >> np.uint64(16)
    return h.astype(np.float64) / 4294967296.0

def noise_value(uv, scale, seed):
    u = np.mod(uv[..., 0], 1.0); v = np.mod(uv[..., 1], 1.0)
    ix = np.floor(u * scale).astype(np.int64) % scale; iy = np.floor(v * scale).astype(np.int64) % scale
    return _hash(ix, iy, seed)

def noise_perlin(uv, scale, seed):
    """Tileable gradient noise, period = 1 uv, remapped to 0..1."""
    x = np.mod(uv[..., 0], 1.0) * scale; y = np.mod(uv[..., 1], 1.0) * scale
    x0 = np.floor(x).astype(np.int64); y0 = np.floor(y).astype(np.int64)
    fx, fy = x - x0, y - y0
    def grad(ix, iy, dx, dy):
        a = _hash(ix % scale, iy % scale, seed) * 2 * math.pi
        return np.cos(a) * dx + np.sin(a) * dy
    fade = lambda t: t * t * t * (t * (t * 6 - 15) + 10)
    n00 = grad(x0, y0, fx, fy); n10 = grad(x0 + 1, y0, fx - 1, fy)
    n01 = grad(x0, y0 + 1, fx, fy - 1); n11 = grad(x0 + 1, y0 + 1, fx - 1, fy - 1)
    u, v = fade(fx), fade(fy)
    n = (n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v
    return np.clip(n * 0.7071 + 0.5, 0, 1)

def hue_shift(c, turns):
    # rotate hue in YIQ space by turns*2pi (cheap, shader friendly)
    a = np.asarray(turns) * 2 * math.pi
    a = a[..., None] if np.ndim(a) else a
    k = np.array([0.57735, 0.57735, 0.57735])
    cosA, sinA = np.cos(a), np.sin(a)
    cross = np.cross(np.broadcast_to(k, c.shape), c)
    dot = (c * k).sum(-1, keepdims=True)
    return c * cosA + cross * sinA + k * dot * (1 - cosA)

def evaluate(g, t, ctx):
    """ctx: palette_a, palette_b (rgb 0..1), pattern (16x16 0/1 array), block_pos (3,)"""
    order, types, _, _ = validate(g)
    byid = {n["id"]: n for n in g["nodes"]}
    idx = (np.arange(TILE) + 0.5) / TILE
    U, V = np.meshgrid(idx, idx)  # V row = y down
    uv0 = np.stack([U, V], -1)
    val = {}
    def S(x):  # broadcast scalar/vector to texel grid
        return x
    for k in order:
        n = byid[k]; ty = n["type"]; p = n.get("params", {}); ins = [val[i] for i in n.get("inputs", [])]
        if ty == "input":
            src = p["source"]
            if src == "time": r = np.full((TILE, TILE), float(t))
            elif src == "uv": r = uv0
            elif src == "block_world_pos": r = np.broadcast_to(np.array(ctx.get("block_pos", (0, 0, 0)), float), (TILE, TILE, 3))
            elif src == "palette_a": r = np.broadcast_to(np.array(ctx["palette_a"], float), (TILE, TILE, 3))
            elif src == "palette_b": r = np.broadcast_to(np.array(ctx["palette_b"], float), (TILE, TILE, 3))
            else:
                uv = ins[0] if ins else uv0
                if src == "pattern_mask":
                    pi = np.floor(np.mod(uv, 1.0) * TILE).astype(int) % TILE
                    r = ctx["pattern"][pi[..., 1], pi[..., 0]].astype(float)
                elif src == "noise_value": r = noise_value(uv, p.get("scale", 4), p.get("seed", 0))
                else: r = noise_perlin(uv, p.get("scale", 4), p.get("seed", 0))
            c = p.get("component")
            if c: r = r[..., "xyz".index(c)]
        elif ty == "const":
            v = p["value"]
            r = np.full((TILE, TILE), float(v)) if not isinstance(v, list) else np.broadcast_to(np.array(v, float), (TILE, TILE, len(v)))
        else:
            A = [x if x.ndim == 3 else x[..., None] for x in ins] if ty not in ("hue_shift", "scroll_uv", "rotate_uv") else ins
            if ty == "add": r = A[0] + A[1]
            elif ty == "mul": r = A[0] * A[1]
            elif ty == "lerp": r = A[0] + (A[1] - A[0]) * A[2]
            elif ty == "sin": r = np.sin(A[0])
            elif ty == "step": r = (A[1] >= A[0]).astype(float)
            elif ty == "smoothstep":
                e0, e1, x = A
                tt = np.clip((x - e0) / np.where(e1 - e0 == 0, 1e-6, e1 - e0), 0, 1); r = tt * tt * (3 - 2 * tt)
            elif ty == "clamp": r = np.minimum(np.maximum(A[0], A[1]), A[2])
            elif ty == "hue_shift": r = hue_shift(ins[0], ins[1])
            elif ty == "scroll_uv": r = np.mod(ins[0] + ins[1] * ins[2][..., None], 1.0)
            elif ty == "rotate_uv":
                a = ins[1]; c, s = np.cos(a), np.sin(a)
                x, y = ins[0][..., 0] - 0.5, ins[0][..., 1] - 0.5
                r = np.mod(np.stack([x * c - y * s + 0.5, x * s + y * c + 0.5], -1), 1.0)
            if r.ndim == 3 and r.shape[-1] == 1: r = r[..., 0]
        val[k] = r
    o = g["outputs"]
    bc = val[o["base_color"]]
    if bc.ndim == 2: bc = np.repeat(bc[..., None], 3, -1)
    em = val[o["emissive_strength"]] if "emissive_strength" in o else np.zeros((TILE, TILE))
    al = val[o["alpha_mask"]] if "alpha_mask" in o else np.ones((TILE, TILE))
    return np.clip(bc, 0, 1), np.clip(em, 0, 8), np.clip(al, 0, 1)

def render_preview(g, ctx, path, frames=16, up=8):
    from PIL import Image
    if os.path.exists(path):
        raise SystemExit("refusing to overwrite " + path)
    ls = g.get("loop_seconds", 1.0) if g["animated"] else 0.0
    rows = np.zeros((3 * TILE, frames * TILE, 3))
    for f in range(frames):
        t = ls * f / frames
        bc, em, al = evaluate(g, t, ctx)
        lit = np.clip(bc * (1 + em[..., None] * 0.25), 0, 1)  # cheap "glow" look
        rows[0:16, f*16:(f+1)*16] = lit
        rows[16:32, f*16:(f+1)*16] = (em / 4.0)[..., None]
        rows[32:48, f*16:(f+1)*16] = al[..., None]
    img = Image.fromarray((rows * 255 + 0.5).astype(np.uint8), "RGB").resize((frames * TILE * up, 3 * TILE * up), Image.NEAREST)
    img.save(path)

def hexrgb(h):
    h = h.lstrip("#"); return [int(h[i:i+2], 16) / 255 for i in (0, 2, 4)]


# =====================================================================================
# v2 additions
# =====================================================================================
import copy, glob, hashlib, datetime
from decimal import Decimal

SCHEMA_V2 = "voxelfinish.effect_graph/2"
BUNDLE_SCHEMA = "voxelfinish.bundle/1"
MAX_DEPTH = 3
MAX_BUNDLE_ITEMS = 8
MAX_BUNDLE_GRAPHS = 64
MAX_PROMPT_CHARS = 4000
HASH_EXCLUDE = ("rev", "preview")

def effect_id(g):
    return g.get("effect_id") or g.get("id")

def rev_number(g):
    r = g.get("rev")
    if isinstance(r, dict):
        return int(r["number"])
    if isinstance(g.get("revision"), int):
        return g["revision"]
    return 1

# ---------- canonical JSON (RFC 8785 / JCS style) + SHA-256
def _canon_num(x):
    if isinstance(x, bool):
        raise GraphError("bool is not a number")
    if isinstance(x, int):
        return str(x)
    if not math.isfinite(x):
        raise GraphError("non-finite number")
    if x == 0:
        return "0"
    sign = "-" if x < 0 else ""
    d = Decimal(repr(abs(x))).normalize()
    _, digits, exp = d.as_tuple()
    ds = "".join(map(str, digits)); k = len(ds); n = k + exp
    if k <= n <= 21:
        s = ds + "0" * (n - k)
    elif 0 < n <= 21:
        s = ds[:n] + "." + ds[n:]
    elif -6 < n <= 0:
        s = "0." + "0" * (-n) + ds
    else:
        e = n - 1
        s = (ds if k == 1 else ds[0] + "." + ds[1:]) + "e" + ("+" if e > 0 else "-") + str(abs(e))
    return sign + s

def canonical_json(o):
    if o is None: return "null"
    if o is True: return "true"
    if o is False: return "false"
    if isinstance(o, (int, float)): return _canon_num(o)
    if isinstance(o, str): return json.dumps(o, ensure_ascii=False)
    if isinstance(o, list): return "[" + ",".join(canonical_json(v) for v in o) + "]"
    if isinstance(o, dict):
        keys = sorted(o.keys(), key=lambda s: s.encode("utf-16-be"))
        return "{" + ",".join(json.dumps(k, ensure_ascii=False) + ":" + canonical_json(o[k]) for k in keys) + "}"
    raise GraphError(f"cannot canonicalise {type(o)}")

def sha256_tag(s):
    return "sha256:" + hashlib.sha256(s.encode("utf-8")).hexdigest()

def content_hash(g):
    return sha256_tag(canonical_json({k: v for k, v in g.items() if k not in HASH_EXCLUDE}))

def check_rev_meta(g):
    """v2 graphs must carry rev metadata whose hash matches the content."""
    if g.get("schema") != SCHEMA_V2:
        return
    r = g.get("rev")
    if not isinstance(r, dict):
        raise GraphError("v2 graph needs a rev object")
    for k in ("number", "parent_rev", "hash", "note", "created"):
        if k not in r:
            raise GraphError("rev missing " + k)
    if not isinstance(r["number"], int) or r["number"] < 1:
        raise GraphError("rev.number must be int >= 1")
    if r["parent_rev"] is not None and (not isinstance(r["parent_rev"], int) or not 1 <= r["parent_rev"] < r["number"]):
        raise GraphError("rev.parent_rev must be null or an earlier rev number")
    if not isinstance(r["note"], str) or len(r["note"]) > 200:
        raise GraphError("rev.note must be a string of at most 200 chars")
    if r["hash"] != content_hash(g):
        raise GraphError(f"rev.hash mismatch (stored {r['hash'][:19]}…, computed {content_hash(g)[:19]}…)")
    try:
        datetime.datetime.strptime(r["created"], "%Y-%m-%dT%H:%M:%SZ")
    except Exception:
        raise GraphError("rev.created must be UTC 'YYYY-MM-DDTHH:MM:SSZ'")

def make_rev(g, number, parent_rev, note, created=None):
    g = copy.deepcopy(g); g.pop("rev", None)
    created = created or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    g["rev"] = {"number": number, "parent_rev": parent_rev, "hash": content_hash(g), "note": note, "created": created}
    return g

# ---------- registry (immutable revisions on disk)
class Registry:
    def __init__(self, dirs=None, graphs=None):
        self.by = {}
        if graphs is not None:
            for g in graphs: self._add(g, "<bundle>")
            return
        for d in dirs:
            for f in sorted(glob.glob(os.path.join(d, "*.json"))):
                try:
                    g = json.load(open(f, encoding="utf-8"))
                except Exception:
                    continue
                if isinstance(g, dict) and g.get("schema") in SCHEMAS:
                    self._add(g, f)
    def _add(self, g, path):
        e, r = effect_id(g), rev_number(g)
        revs = self.by.setdefault(e, {})
        if r in revs and content_hash(revs[r][0]) != content_hash(g):
            raise GraphError(f"two different files claim {e} rev {r}: {revs[r][1]} and {path}")
        revs.setdefault(r, (g, path))
    def latest(self, e):
        if e not in self.by: raise GraphError("unknown effect " + e)
        return max(self.by[e])
    def get(self, e, rev="latest"):
        if e not in self.by: raise GraphError("unknown effect " + e)
        r = self.latest(e) if rev in (None, "latest") else rev
        if r not in self.by[e]: raise GraphError(f"{e} has no rev {r}")
        return self.by[e][r][0], r
    def history(self, e):
        out = []
        for r in sorted(self.by.get(e, {})):
            g, p = self.by[e][r]; m = g.get("rev") if isinstance(g.get("rev"), dict) else {}
            out.append({"rev": r, "parent_rev": m.get("parent_rev", None if r == 1 else r - 1), "hash": content_hash(g),
                        "note": m.get("note", "(legacy v1 file)"), "created": m.get("created", ""), "file": os.path.basename(p)})
        return out

def default_registry(extra=()):
    here = os.path.dirname(os.path.abspath(__file__))
    env = os.environ.get("VOXELFINISH_GRAPH_DIRS")
    # ../effects_v1/graphs does not exist under fuzz/vf_reference; only ./graphs is found there by default.
    dirs = env.split(os.pathsep) if env else [os.path.join(here, "graphs"), os.path.join(here, "..", "effects_v1", "graphs")]
    return Registry(list(extra) + dirs)

# ---------- SubEffect flattening
def _split_ref(s):
    if "." in s:
        a, b = s.split(".", 1); return a, b
    return s, None

def _check_value(v, ty, lo=None, hi=None, what=""):
    t = _ctype(v)
    if t != ty: raise GraphError(f"{what}: expected {ty}, got {t}")
    vals = v if isinstance(v, list) else [v]
    for x in vals:
        if lo is not None and x < lo or hi is not None and x > hi:
            raise GraphError(f"{what}: {x} outside [{lo}, {hi}]")

def check_exposed(g):
    byid = {n["id"]: n for n in g["nodes"]}
    names = set()
    for x in g.get("exposed", []):
        for k in ("name", "kind", "type", "node"):
            if k not in x: raise GraphError(f"exposed entry missing {k}")
        if x["name"] in names: raise GraphError("duplicate exposed name " + x["name"])
        names.add(x["name"])
        if x["type"] not in DIM: raise GraphError("exposed type must be f, v2 or v3")
        if x["node"] not in byid: raise GraphError(f"exposed {x['name']} -> unknown node {x['node']}")
        if x["kind"] == "param":
            n = byid[x["node"]]
            if n["type"] != "const": raise GraphError(f"exposed param {x['name']} must point at a const node")
            for k in ("default", "min", "max"):
                if k not in x: raise GraphError(f"exposed param {x['name']} missing {k}")
            if canonical_json(x["default"]) != canonical_json(n["params"]["value"]):
                raise GraphError(f"exposed param {x['name']}: default must equal the const value")
            _check_value(x["default"], x["type"], x["min"], x["max"], "exposed " + x["name"])
        elif x["kind"] == "input":
            pass  # default = the child's own node
        else:
            raise GraphError("exposed kind must be param or input")

def flatten(g, reg, depth=0, stack=(), lock=None, _wire_checks=None):
    """Returns (flat_graph, lock) where lock lists every resolved SubEffect (effect_id, rev, hash, depth)."""
    top = lock is None
    lock = [] if lock is None else lock
    wire_checks = [] if _wire_checks is None else _wire_checks
    e = effect_id(g)
    if e in stack: raise GraphError("SubEffect cycle: " + " -> ".join(stack + (e,)))
    stack = stack + (e,)
    check_rev_meta(g)
    check_exposed(g)
    nodes = g["nodes"]
    if len(nodes) > MAX_AUTHORED_NODES: raise GraphError(f"{e}: {len(nodes)} nodes > {MAX_AUTHORED_NODES} before flattening")
    byid = {}
    for n in nodes:
        if n.get("id") in byid or not isinstance(n.get("id"), str) or "/" in n["id"] or "." in n["id"]:
            raise GraphError(f"{e}: bad or duplicate node id {n.get('id')!r} ('/' and '.' are reserved)")
        byid[n["id"]] = n
    def deps(n):
        if n["type"] == "subeffect":
            return [_split_ref(v)[0] for v in n.get("params", {}).get("wires", {}).values()]
        return [_split_ref(i)[0] for i in n.get("inputs", [])]
    order, done, vis = [], set(), set()
    def visit(k):
        if k in done: return
        if k not in byid: raise GraphError(f"{e}: unknown node {k}")
        if k in vis: raise GraphError(f"{e}: cycle at {k}")
        vis.add(k)
        for d in deps(byid[k]): visit(d)
        vis.discard(k); done.add(k); order.append(k)
    for k in byid: visit(k)
    out_nodes, alias = [], {}
    def res(ref):
        a, port = _split_ref(ref)
        if port is None:
            if byid[a]["type"] == "subeffect": raise GraphError(f"{e}: use '{a}.<output>' to read a SubEffect")
            return alias[a]
        if byid[a]["type"] != "subeffect": raise GraphError(f"{e}: '.{port}' only valid on SubEffect nodes")
        key = f"{a}.{port}"
        if key not in alias: raise GraphError(f"{e}: SubEffect {a} has no output {port}")
        return alias[key]
    anim_children = []
    for k in order:
        n = byid[k]
        if n["type"] != "subeffect":
            m = copy.deepcopy(n); m["inputs"] = [res(i) for i in n.get("inputs", [])]
            if not m["inputs"]: m.pop("inputs")
            out_nodes.append(m); alias[k] = k
            continue
        if depth + 1 > MAX_DEPTH: raise GraphError(f"{e}: SubEffect depth > {MAX_DEPTH}")
        p = n.get("params", {}); ref = p.get("ref", {})
        cid, crev = ref.get("effect_id"), ref.get("rev", "latest")
        if not (crev == "latest" or isinstance(crev, int)): raise GraphError(f"{e}/{k}: rev must be int or 'latest'")
        child, rnum = reg.get(cid, crev)
        lock.append({"effect_id": cid, "rev": rnum, "hash": content_hash(child), "depth": depth + 1, "requested": crev})
        cflat, _ = flatten(child, reg, depth + 1, stack, lock, wire_checks)
        exposed = {x["name"]: x for x in child.get("exposed", [])}
        args, wires = p.get("args", {}), p.get("wires", {})
        for nm in list(args) + list(wires):
            if nm not in exposed: raise GraphError(f"{e}/{k}: {cid} exposes no '{nm}'")
        cnodes = {m["id"]: copy.deepcopy(m) for m in cflat["nodes"]}
        for nm, v in args.items():
            x = exposed[nm]
            if x["kind"] != "param": raise GraphError(f"{e}/{k}: '{nm}' is an input; wire it instead")
            _check_value(v, x["type"], x["min"], x["max"], f"{e}/{k}.{nm}")
            cnodes[x["node"]]["params"]["value"] = v
        replace = {}
        for nm, src in wires.items():
            x = exposed[nm]
            if x["kind"] != "input": raise GraphError(f"{e}/{k}: '{nm}' is a param; set it in args")
            replace[x["node"]] = res(src)
            wire_checks.append((res(src), x["type"], f"{e}/{k}.{nm}"))
        pre = f"{k}/"
        def cmap(i):
            return replace[i] if i in replace else pre + i
        for cid_, m in cnodes.items():
            if cid_ in replace: continue
            m["id"] = pre + cid_
            if m.get("inputs"): m["inputs"] = [cmap(i) for i in m["inputs"]]
            out_nodes.append(m)
        for o, nid in cflat["outputs"].items():
            alias[f"{k}.{o}"] = cmap(nid)
        if cflat["animated"]: anim_children.append((cid, cflat["loop_seconds"]))
    outs = {o: res(r) for o, r in g["outputs"].items()}
    animated = bool(g["animated"])
    if anim_children and not animated:
        raise GraphError(f"{e}: has animated SubEffects, so it must be animated")
    for cid, cl in anim_children:
        ratio = g["loop_seconds"] / cl
        if abs(ratio - round(ratio)) > 1e-6 or round(ratio) < 1:
            raise GraphError(f"{e}: loop_seconds {g['loop_seconds']} is not a whole multiple of {cid}'s {cl}")
    flat = {"schema": SCHEMA_V2, "effect_id": e, "name": g.get("name", e), "animated": animated, "nodes": out_nodes, "outputs": outs}
    if animated: flat["loop_seconds"] = g["loop_seconds"]
    if top:
        flat = optimise(flat)
        validate(flat)  # flattened limits: <= MAX_FLAT_NODES, <= MAX_FLAT_COST, types, DAG
        _, types, _, _ = validate(flat)
        for nid, ty, what in wire_checks:
            nid2 = flat["_merged"].get(nid, nid)
            if nid2 in types and types[nid2] != ty:
                raise GraphError(f"{what}: wired a {types[nid2]} into a {ty} input")
        flat.pop("_merged")
    return flat, lock

COMMUTATIVE = {"add", "mul"}
def optimise(flat):
    """Dead-node elimination + merging identical nodes (same type, params and inputs). Deterministic."""
    nodes = {n["id"]: n for n in flat["nodes"]}
    order = [n["id"] for n in flat["nodes"]]
    # topo order (inputs before users)
    seen, topo = set(), []
    def visit(k):
        if k in seen: return
        seen.add(k)
        for i in nodes[k].get("inputs", []): visit(i)
        topo.append(k)
    for k in order: visit(k)
    merged, keymap = {}, {}
    for k in topo:
        n = nodes[k]
        ins = [merged.get(i, i) for i in n.get("inputs", [])]
        if n["type"] in COMMUTATIVE: ins_key = sorted(ins)
        else: ins_key = ins
        key = canonical_json([n["type"], n.get("params", {}), ins_key])
        if key in keymap:
            merged[k] = keymap[key]
        else:
            keymap[key] = k
            if ins: n["inputs"] = ins
    outs = {o: merged.get(v, v) for o, v in flat["outputs"].items()}
    live, stackk = set(), list(outs.values())
    while stackk:
        k = stackk.pop()
        if k in live: continue
        live.add(k); stackk.extend(nodes[k].get("inputs", []))
    flat["nodes"] = [nodes[k] for k in topo if k in live and k not in merged]
    flat["outputs"] = outs
    flat["_merged"] = merged
    return flat

def bake_key(g, reg):
    """Hash that pins every 'latest' to the concrete rev used at bake time."""
    _, lock = flatten(g, reg)
    deps = sorted({(l["effect_id"], l["rev"], l["hash"]) for l in lock})
    return sha256_tag(canonical_json({"effect": effect_id(g), "hash": content_hash(g),
                                      "deps": [{"effect_id": a, "rev": b, "hash": c} for a, b, c in deps]})), lock

def full_check(g, reg):
    flat, lock = flatten(g, reg)
    _, _, cost, bakeable = validate(flat)
    return flat, lock, cost, bakeable

# ---------- bundles
FINISH_KEYS = {"palette_a", "palette_b", "pattern_id", "glow_mask_id", "strength", "effect", "args"}
def check_finish(f):
    if set(f) - FINISH_KEYS: raise GraphError("unknown finish keys " + str(set(f) - FINISH_KEYS))
    for k in ("palette_a", "palette_b"):
        s = f.get(k, "")
        if not (isinstance(s, str) and len(s) == 7 and s[0] == "#" and all(c in "0123456789abcdefABCDEF" for c in s[1:])):
            raise GraphError(f"finish.{k} must be #RRGGBB")
    if not (isinstance(f.get("pattern_id"), int) and (0 <= f["pattern_id"] <= 15 or f["pattern_id"] == 255)): raise GraphError("finish.pattern_id")
    if not (isinstance(f.get("glow_mask_id", 255), int) and (0 <= f.get("glow_mask_id", 255) <= 3 or f.get("glow_mask_id", 255) == 255)): raise GraphError("finish.glow_mask_id")
    _check_value(f.get("strength", 1.0), "f", 0, 8, "finish.strength")
    ef = f.get("effect", {})
    if not isinstance(ef.get("rev"), int): raise GraphError("finish.effect.rev must be a concrete int inside a bundle")

def _finish_as_graph(f, reg):
    """Wrap a finish's effect + args as a one-node SubEffect graph so the normal checks run on it."""
    g = {"schema": SCHEMA_V2, "effect_id": "_finish_check", "name": "finish", "animated": False,
         "nodes": [{"id": "s", "type": "subeffect", "params": {"ref": f["effect"], "args": f.get("args", {})}}],
         "outputs": {}}
    child, _ = reg.get(f["effect"]["effect_id"], f["effect"]["rev"])
    if child["animated"]:
        g["animated"] = True; g["loop_seconds"] = child["loop_seconds"]
    g["outputs"] = {o: f"s.{o}" for o in child["outputs"]}
    return make_rev(g, 1, None, "finish check", "1970-01-01T00:00:00Z")

def make_bundle(reg, items, purpose, prompt=None):
    if purpose not in ("ai_context", "mailbox"): raise GraphError("purpose must be ai_context or mailbox")
    if purpose == "ai_context" and not prompt: raise GraphError("ai_context bundles need prompt text")
    if prompt and len(prompt) > MAX_PROMPT_CHARS: raise GraphError("prompt too long")
    if not 1 <= len(items) <= MAX_BUNDLE_ITEMS: raise GraphError(f"bundle holds 1..{MAX_BUNDLE_ITEMS} items")
    m_items, closure = [], {}
    def add_closure(eid_, rev):
        g, r = reg.get(eid_, rev)
        closure[(eid_, r)] = g
        _, lock = flatten(g, reg)
        for l in lock: closure[(l["effect_id"], l["rev"])] = reg.get(l["effect_id"], l["rev"])[0]
        return g, r
    for it in items:
        if isinstance(it, str):
            eid_, _, rv = it.partition("@")
            g, r = add_closure(eid_, int(rv) if rv and rv != "latest" else "latest")
            m_items.append({"kind": "revision", "effect_id": eid_, "rev": r, "hash": content_hash(g)})
        else:
            f = copy.deepcopy(it)
            if f.get("effect", {}).get("rev", "latest") == "latest":
                f["effect"]["rev"] = reg.latest(f["effect"]["effect_id"])
            check_finish(f)
            add_closure(f["effect"]["effect_id"], f["effect"]["rev"])
            full_check(_finish_as_graph(f, reg), reg)
            m_items.append({"kind": "finish", "finish": f, "hash": sha256_tag(canonical_json(f))})
    if len(closure) > MAX_BUNDLE_GRAPHS: raise GraphError("dependency closure too large")
    cl = [{"effect_id": a, "rev": b, "hash": content_hash(closure[(a, b)])} for a, b in sorted(closure)]
    manifest = {"bundle_schema": BUNDLE_SCHEMA, "purpose": purpose, "items": m_items, "closure": cl,
                "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    if prompt: manifest["prompt"] = prompt
    manifest["bundle_hash"] = sha256_tag(canonical_json({k: manifest[k] for k in ("purpose", "items", "closure")} | ({"prompt": prompt} if prompt else {})))
    return {"manifest": manifest, "graphs": [closure[k] for k in sorted(closure)]}

def verify_bundle(b):
    """Receiver side: trust nothing. Re-hash, re-validate, re-flatten every item from the bundle's own graphs."""
    m = b["manifest"]
    if m.get("bundle_schema") != BUNDLE_SCHEMA: raise GraphError("bad bundle_schema")
    if m["purpose"] not in ("ai_context", "mailbox"): raise GraphError("bad purpose")
    if not 1 <= len(m["items"]) <= MAX_BUNDLE_ITEMS: raise GraphError("item count")
    if len(b["graphs"]) > MAX_BUNDLE_GRAPHS: raise GraphError("too many graphs")
    expect = sha256_tag(canonical_json({k: m[k] for k in ("purpose", "items", "closure")} | ({"prompt": m["prompt"]} if m.get("prompt") else {})))
    if expect != m["bundle_hash"]: raise GraphError("bundle_hash mismatch")
    reg = Registry(graphs=b["graphs"])
    have = {(effect_id(g), rev_number(g)): content_hash(g) for g in b["graphs"]}
    for c in m["closure"]:
        if have.get((c["effect_id"], c["rev"])) != c["hash"]: raise GraphError(f"closure hash mismatch for {c['effect_id']}@{c['rev']}")
    if set(have) != {(c["effect_id"], c["rev"]) for c in m["closure"]}: raise GraphError("graphs and closure differ")
    for g in b["graphs"]: check_rev_meta(g)
    for it in m["items"]:
        if it["kind"] == "revision":
            g, _ = reg.get(it["effect_id"], it["rev"])
            if content_hash(g) != it["hash"]: raise GraphError("item hash mismatch")
            full_check(g, reg)
        elif it["kind"] == "finish":
            if sha256_tag(canonical_json(it["finish"])) != it["hash"]: raise GraphError("finish hash mismatch")
            check_finish(it["finish"]); full_check(_finish_as_graph(it["finish"], reg), reg)
        else:
            raise GraphError("unknown item kind")
    return True

# ---------- CLI
def _write_new(path, obj):
    if os.path.exists(path): raise SystemExit("refusing to overwrite " + path)
    with open(path, "x", encoding="utf-8") as f: json.dump(obj, f, indent=1, ensure_ascii=False)

def _preview(flat, src, path):
    from PIL import Image
    here = os.path.dirname(os.path.abspath(__file__))
    pv = src.get("preview", {}); pid = pv.get("pattern_id", 0)
    # ../paint_v1/tiles does not exist under fuzz/vf_reference (asset-drop layout only), so preview fails there.
    pdir = os.path.join(here, "..", "paint_v1", "tiles")
    pf = [t for t in sorted(os.listdir(pdir)) if t.startswith(f"T_Paint_Pattern_{pid:02d}_")][0]
    pat = (np.array(Image.open(os.path.join(pdir, pf)).convert("L")) > 127).astype(float)
    ctx = {"palette_a": hexrgb(pv.get("palette_a", "#FF7F6B")), "palette_b": hexrgb(pv.get("palette_b", "#4FB3FF")), "pattern": pat}
    render_preview(flat, ctx, path)

if __name__ == "__main__":
    a = sys.argv[1:]
    cmd = a[0] if a else "help"
    reg = default_registry()
    if cmd == "validate":
        bad = 0
        for f in a[1:]:
            g = json.load(open(f, encoding="utf-8"))
            try:
                flat, lock, cost, bk = full_check(g, reg)
                subs = ", ".join(f"{l['effect_id']}@{l['rev']}" + (" (latest)" if l["requested"] == "latest" else "") for l in lock) or "-"
                print(f"OK   {os.path.basename(f)}  rev={rev_number(g)} src_nodes={len(g['nodes'])} flat_nodes={len(flat['nodes'])} cost={cost} "
                      f"animated={flat['animated']} bakeable={bk} subeffects=[{subs}]")
            except GraphError as ex:
                bad += 1; print(f"FAIL {os.path.basename(f)}: {ex}")
        sys.exit(1 if bad else 0)
    elif cmd == "flatten":
        print(json.dumps(flatten(json.load(open(a[1], encoding="utf-8")), reg)[0], indent=1))
    elif cmd == "hash":
        for f in a[1:]: print(content_hash(json.load(open(f, encoding="utf-8"))), os.path.basename(f))
    elif cmd == "bakekey":
        for f in a[1:]:
            k, lock = bake_key(json.load(open(f, encoding="utf-8")), reg)
            print(k, os.path.basename(f), [f"{l['effect_id']}@{l['rev']}" for l in lock])
    elif cmd == "history":
        for h in reg.history(a[1]): print(json.dumps(h))
    elif cmd in ("revert", "branch"):
        e, r, out = a[1], int(a[2]), a[3]
        old, _ = reg.get(e, r); new_no = reg.latest(e) + 1
        note = a[4] if len(a) > 4 else (f"revert to r{r}" if cmd == "revert" else f"branch from r{r}")
        g = copy.deepcopy(old)
        if g.get("schema") != SCHEMA_V2:  # upgrade legacy file into a v2 revision
            g["schema"] = SCHEMA_V2; g["effect_id"] = effect_id(g); g.pop("id", None); g.pop("revision", None)
        g = make_rev(g, new_no, r, note)
        p = os.path.join(out, f"{e}_rev{new_no}.json"); _write_new(p, g); print("wrote", p)
    elif cmd == "preview":
        out = a[1]
        for f in a[2:]:
            g = json.load(open(f, encoding="utf-8")); flat, _ = flatten(g, reg)
            p = os.path.join(out, f"PREVIEW_{effect_id(g)}_r{rev_number(g)}_16f_v1.png")
            if os.path.exists(p): raise SystemExit("refusing to overwrite " + p)
            _preview(flat, g, p); print("wrote", p)
    elif cmd == "bundle":
        out, purpose = a[1], a[2]; rest = a[3:]; prompt = None
        if "--prompt" in rest:
            i = rest.index("--prompt"); prompt = rest[i + 1]; rest = rest[:i] + rest[i + 2:]
        items = [json.load(open(x, encoding="utf-8")) if x.endswith(".json") else x for x in rest]
        b = make_bundle(reg, items, purpose, prompt); _write_new(out, b)
        print("wrote", out, b["manifest"]["bundle_hash"], f"items={len(b['manifest']['items'])} closure={len(b['graphs'])}")
    elif cmd == "verify-bundle":
        for f in a[1:]:
            try:
                verify_bundle(json.load(open(f, encoding="utf-8"))); print("OK  ", os.path.basename(f))
            except (GraphError, KeyError) as ex:
                print("FAIL", os.path.basename(f), ex); sys.exit(1)
    else:
        print(__doc__)
