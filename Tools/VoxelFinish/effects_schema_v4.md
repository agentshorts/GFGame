# VoxelFinish effect graphs — schema v4 (combined-effect limits)

**What changed from v3:** only the limits. Each authored graph is still capped at **32 nodes**
(checked before any inlining). The flattened (combined) graph after SubEffect inlining may now use
**up to 64 nodes and 128 op cost** (v3: 32 nodes / 64 op cost). Depth stays at 3, and a combined
effect may run at most **64 steps** (flattened nodes). Everything else (node types, op costs,
revisions, hashing, bake keys, bundles, AI hook) is unchanged from v3. The v3 schema document is not in
this repo; the full text below is the v3 text with the v4 limits applied.
Reference implementation of these limits: `Tools/VoxelFinish/fuzz/vf_reference/fxgraph_eval_v3.py`
(`MAX_AUTHORED_NODES = 32`, `MAX_FLAT_NODES = 64`, `MAX_FLAT_COST = 128`, `MAX_DEPTH = 3`). The Unreal
plugin constants match (`VoxelFinish::MaxAuthoredNodes` 32, `MaxFlattenedNodes` 64, `MaxFlattenedOps`
128, `MaxSubEffectDepth` 3, `MaxCombinedSteps` 64). Graph files keep schema id
`voxelfinish.effect_graph/2`: no file format change.

| limit | v3 | v4 |
|---|---|---|
| authored graph nodes (before inlining) | 32 | 32 |
| flattened nodes | 32 | 64 |
| flattened op cost | 64 | 128 |
| SubEffect depth | 3 | 3 |
| combined steps | — | 64 |

Graphs that flatten to 33–64 nodes or 65–128 op cost were rejected under v3 and are accepted under v4.
65 flattened nodes, 129 op cost, or an authored graph of 33 nodes are still rejected.

---

## Full text (v3 with the v4 limits applied)

### VoxelFinish effect graphs — revisions, SubEffects, bundles, AI hook

Builds on `effects_schema_v1.md` (node types, ops, op costs, inputs, outputs) and `effects_schema_v2.md`
(bakeable uv+time rules). Everything in those still holds. Reference implementation:
`fxgraph_eval_v2.py` (self-tests: `test_fxgraph_eval_v2.py`, 31 checks) for every rule except the
limits, which follow `fxgraph_eval_v3.py`. The shipping evaluator in
Unreal must match the reference on every rule marked **MUST**.

Graph schema id: `voxelfinish.effect_graph/2`. Files with `gfgame.effect_graph/1` still load: they are
read as rev = their `revision` field (or 1), no parent, and cannot contain SubEffects.

---

## (a) Immutable revisions

### Revision fields

Every v2 graph file is **one immutable revision** of an effect.

```json
{
  "schema": "voxelfinish.effect_graph/2",
  "effect_id": "fx_sparkle",
  "rev": {
    "number": 2,
    "parent_rev": 1,
    "hash": "sha256:3d48e497…",
    "note": "expose base input, density, sparkle_glow",
    "created": "2026-10-30T05:45:00Z"
  },
  "...": "name, description, animated, loop_seconds, exposed, nodes, outputs, preview"
}
```

| field | rule |
|---|---|
| `effect_id` | `[a-z0-9_]+`, stable across revisions. Not an asset path; no "GF" prefix. |
| `rev.number` | int ≥ 1, unique per effect_id. The registry **MUST** refuse two different contents claiming the same (effect_id, number). |
| `rev.parent_rev` | `null` for the first rev, otherwise an existing **lower** rev number. Several revs may share a parent → branches. |
| `rev.hash` | content hash (below). **MUST** be re-computed on load; a mismatch rejects the file. |
| `rev.note` | short free text, ≤ 200 chars. |
| `rev.created` | UTC, `YYYY-MM-DDTHH:MM:SSZ`. Informational only, not hashed. |

Never edit a revision in place. Any change = a new file with a new `rev.number`
(file naming on disk: first file `<effect_id>_v1.json`, later revisions `<effect_id>_rev<N>.json`;
the registry goes by `effect_id` + `rev.number`, not by file name).

### Canonical JSON and content hash

`content_hash = "sha256:" + hex(SHA-256(UTF-8(canonical_json(graph minus {"rev","preview"}))))`

Canonical JSON = RFC 8785 (JCS):
- objects: keys sorted by UTF-16 code units, no whitespace anywhere (`,` and `:` only);
- strings: JSON escapes only for `"`, `\`, and control chars (`\b \f \n \r \t`, others `\u00XX` lowercase hex); non-ASCII emitted raw as UTF-8;
- numbers: ECMAScript `Number.prototype.toString` form — `1.0`→`1`, `0.5`→`0.5`, `1e-7`→`1e-7`, `1e21`→`1e+21`, shortest round-trip digits; NaN/Infinity are invalid;
- arrays keep their order (node order is part of the content).

`rev` is excluded so the hash describes content only (a **revert** therefore has the same hash as its
target); `preview` is excluded because it is editor-only.

### History, revert, branch, latest vs pinned

- **History** of an effect = all its revisions sorted by number, each with parent_rev, hash, note, created
  (`fxgraph_eval_v2.py history fx_sparkle`). The UI draws parent links as a small tree.
- **Revert to N** = create a new revision `latest+1` whose content is a copy of rev N and `parent_rev = N`
  (`revert EFFECT N OUT_DIR "note"`). Nothing is deleted; for a v2 target the hash equals rev N's hash.
  Reverting a legacy v1 file upgrades it to schema v2, so its hash differs from the legacy one.
- **Branch from N** = new revision `latest+1`, content copied from N, `parent_rev = N`, then edited
  (`branch EFFECT N OUT_DIR "note"`). Numbers stay global per effect (no branch names needed).
- **Latest** = highest rev number. References default to `"latest"` unless **pinned** to a number.
  Finishes saved on blocks always store a concrete rev (see bake key), so a new revision never silently
  changes already-painted blocks; the editor offers "update to latest".

---

## (b) SubEffect node

```json
{"id": "spark", "type": "subeffect",
 "params": {
   "ref":   {"effect_id": "fx_sparkle", "rev": "latest"},
   "args":  {"density": 0.85, "sparkle_glow": 3.0},
   "wires": {"base": "frosted"}
 }}
```

- No `inputs` list. Outputs are read as `<node_id>.<output>`, e.g. `spark.base_color`,
  `spark.emissive_strength`, `spark.alpha_mask` (only outputs the child defines).
- Node ids **MUST NOT** contain `/` or `.` (reserved for flattening and output refs).
- `rev`: int (pinned) or `"latest"`.

### Exposed inputs / params (declared by the child)

```json
"exposed": [
  {"name": "base", "kind": "input", "type": "v3", "node": "base", "description": "colour underneath"},
  {"name": "density", "kind": "param", "type": "f", "node": "thr",
   "default": 0.875, "min": 0.5, "max": 0.99, "description": "higher = fewer sparkles"}
]
```

| key | rule |
|---|---|
| `name` | `[a-z0-9_]+`, unique in the list. |
| `kind` | `param` = constant the parent may override via `args`; `input` = node the parent may replace via `wires`. |
| `type` | `f`, `v2` or `v3`. |
| `node` | node id inside the child. For `param` it **MUST** be a `const` whose value equals `default`. |
| `default/min/max` | params only; `min ≤ default ≤ max`; vectors are checked per component. |

Rules: `args` keys must be exposed params, values in [min, max]; `wires` keys must be exposed inputs,
the wired node's type must match the declared type (a scalar may broadcast only where v1 broadcasting
allows). Anything not exposed is private.

### Compile-time inlining (flattening) — MUST

1. Resolve `ref` through the registry (`"latest"` → highest rev). Check hash.
2. Copy child nodes with ids prefixed `<subnode_id>/` (e.g. `spark/thr`); apply `args` to the const
   values; replace each wired input node by the parent's node (all references rewired).
3. Recursively inline nested SubEffects. **Max depth 3** (parent → child → grandchild → great-grandchild).
4. **No cycles**: an effect_id already on the current inlining stack is an error (A→B→A, or A→A).
5. Timing: an animated child needs an animated parent whose `loop_seconds` is a whole multiple of the
   child's `loop_seconds` (keeps the 16-frame bake seamless). Static children are fine anywhere.
6. Optimise: drop nodes not reachable from the outputs; merge identical nodes (same type, params and
   inputs; `add`/`mul` inputs compared unordered), e.g. shared `time`, `uv`, palette inputs.
7. Validate the flattened graph with all v1/v2 rules: **≤ 64 nodes, ≤ 128 op cost** (v4; each authored
   graph ≤ 32 nodes before inlining), types, outputs,
   bakeability (`block_world_pos` anywhere ⇒ not bakeable).

### Bake key — what the stored hash pins

`bake_key = sha256(canonical {"effect": effect_id, "hash": content_hash, "deps": sorted [{effect_id, rev, hash}]})`
where `deps` lists every SubEffect revision actually used, **with `"latest"` resolved to the concrete rev
at bake time**. The baked atlas slot is keyed by bake_key, so publishing `fx_sparkle` r3 changes the
bake key of graphs that reference `fx_sparkle@latest` (they re-bake), while graphs pinned to r2 keep
their key and their bake. `fxgraph_eval_v2.py bakekey FILE…` prints it with the resolved deps.

### Starter composites in this drop

| effect | subeffects | flattened nodes / cost | loop |
|---|---|---|---|
| `fx_shimmer_over_pulse` r1 | `fx_pulse_glow@2` (pinned) → its colour wired into `fx_shimmer@latest` (=2) | 32 / 21 | 2 s |
| `fx_lava_embers` r1 | `fx_lava_flow@2` (pinned) + a 3 Hz ember-style flicker on its glow | 29 / 21 | 4 s |
| `fx_frosted_sparkle` r1 | parent frost tint wired into `fx_sparkle@latest` (=2) | 28 / 19 | 1 s |

Limits note (written for the v3 cap): the full `fx_ember_flicker` (26 nodes) + `fx_lava_flow` (21)
would flatten to about 45 nodes, and `fx_frost_edge` (28) + `fx_sparkle` (22) to about 45, both over
the v3 cap of 32 and inside the v4 cap of 64. The starter composites were authored under v3, so lava_embers reuses
only the flicker idea (sin at 3 Hz, a whole number of cycles per 4 s loop), and frosted_sparkle builds
its own 7-node frost tint. The child revisions `fx_pulse_glow`, `fx_shimmer`, `fx_lava_flow`,
`fx_sparkle` rev 2 only add `exposed` blocks (same visuals as rev 1).

---

## (c) Bundles — `voxelfinish.bundle/1`

A bundle is one JSON file with `manifest` + `graphs`.

```json
{
 "manifest": {
  "bundle_schema": "voxelfinish.bundle/1",
  "purpose": "ai_context",
  "prompt": "Make a new effect for a stone block: …",
  "items": [
   {"kind": "revision", "effect_id": "fx_frosted_sparkle", "rev": 1, "hash": "sha256:…"},
   {"kind": "finish", "finish": {"palette_a": "#A98BFF", "palette_b": "#FFD23F", "pattern_id": 3,
       "glow_mask_id": 1, "strength": 1.0, "effect": {"effect_id": "fx_shimmer_over_pulse", "rev": 1},
       "args": {}}, "hash": "sha256:…"}
  ],
  "closure": [{"effect_id": "fx_frosted_sparkle", "rev": 1, "hash": "sha256:…"},
              {"effect_id": "fx_sparkle", "rev": 2, "hash": "sha256:…"}],
  "created": "2026-10-30T05:43:59Z",
  "bundle_hash": "sha256:…"
 },
 "graphs": [ /* one full graph file per closure entry, same order */ ]
}
```

- **items**: 1–8. `revision` items name a concrete rev (the sender's `"latest"` is resolved when the
  bundle is made). `finish` items carry a full finish: `palette_a`, `palette_b` (`#RRGGBB`),
  `pattern_id` (0–15 or 255 = none), `glow_mask_id` (0–3 or 255), `strength` (0–8),
  `effect {effect_id, rev:int}`, `args` (exposed params of that effect). Finish hash = sha256 of its canonical JSON.
- **closure**: automatically every graph needed — each item's effect plus all SubEffect revisions,
  recursively, deduplicated, sorted by (effect_id, rev). Max 64 graphs.
- **purpose**: `ai_context` (needs `prompt`, ≤ 4000 chars; the bundle is context for the AI hook) or
  `mailbox` (send to another player/workshop; no prompt).
- **bundle_hash** = sha256 of canonical `{purpose, items, closure, prompt?}`.
- Contains only effect data: no player names, ids, chat, or file paths. Graph `preview` blocks are
  editor hints only.

**Receivers MUST** (`verify-bundle`): recompute every graph hash and match the closure; recompute
finish hashes and bundle_hash; build a registry from the bundle's own graphs only (never the local
library, so `"latest"` cannot drift), re-validate and re-flatten every item within the limits, then
**re-bake locally**. Nothing pre-baked is ever accepted. Importing stores the revisions as-is (same
effect_id + rev + hash already present → skip; same effect_id + rev with a different hash → import under
the next free rev number with `parent_rev` = the nearest shared rev and a note "imported").

---

## (d) AI text-to-effect hook (note)

- Optional editor feature: text (+ optional `ai_context` bundle) → candidate graph JSON.
- Endpoint is configurable, never hard-coded. Settings live in a **local, git-ignored** file, e.g.
  `Saved/VoxelFinish/voxelfinish.local.json` (template: `voxelfinish.local.example.json`):
  `{"ai_endpoint": "https://…", "ai_api_key": "", "ai_model": "", "timeout_s": 30}`.
  Add `gitignore_snippet_v1.txt` lines to the repo's `.gitignore`. Keys are never committed, logged,
  put in bundles, or shown in screenshots. With no config, the feature is hidden.
- Request = prompt + the bundle's graphs + this schema's rules (node/op list, limits).
- Response is **untrusted data**: it goes through exactly the same path as a hand-made graph —
  parse → schema check → hash/rev assigned locally (`make_rev`, new rev, note "ai draft") → flatten →
  validate (32 authored nodes, 64 flattened nodes / 128 ops, types, depth, cycles, bakeable) → preview. Rejected output shows the
  validator message; nothing is baked or saved until the player accepts it.
- Unknown ops, extra keys, or references to effects not in the local library or the bundle are errors.
