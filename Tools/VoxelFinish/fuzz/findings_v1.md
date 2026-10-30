# Effect-graph fuzz findings (v1)

Seed `1234`. Profiles: `current` = flattened 32 nodes / 64 op cost (`fxgraph_eval_v2.py`); `target` = 32 authored nodes, flattened 64 nodes / 128 op cost (`fxgraph_eval_v3.py`). v3 is v2 plus those constants. v2 was not changed. None of the crashes below are fixed in either file.

The default suite (`VF_FUZZ_CASES=2000`, profile `both`) passes. The cases below are `xfail` in `test_fuzz_fxgraph_v1.py` with a repro in `findings/`. An xfail that starts passing means the evaluator changed and the finding should be retired.

## Soak

`VF_FUZZ_CASES=100000`, `VF_FUZZ_SEED=1234`, `VF_FUZZ_PROFILE=both`:

```
py -3.12 Tools/VoxelFinish/fuzz/test_fuzz_fxgraph_v1.py
```

Result: **100,000 cases, 45.06 s, 0 failures**. `current` 50,000 passed, `target` 50,000 passed. Random graphs: 80,665 accept, 19,019 reject (the rest are the structured cases). No new crash, wrong accept, or wrong reject.

Default run on the same seed: **2,000 cases, 0.73 s, 0 failures** (1,000 per profile). Pytest: `1 passed, 10 xfailed` (the xfail tests are F1–F8).

## Spec gap (not a defect in either profile)

v2 rejects a combined effect with 33–64 flattened nodes, or with flattened op cost 65–128. v3 accepts those and still rejects 65 flattened nodes, 129 op cost, and any authored graph over 32 nodes. The schema v3 write-up still says the flattened cap is 32 nodes / 64 op cost. It needs a new revision if the 64-node / 128-cost combined cap is confirmed.

Observed on both profiles for the limit matrix (message category, not exact text):

| case | current | target |
|---|---|---|
| 32 authored nodes | accept | accept |
| 33 authored nodes | reject, before flattening | reject, before flattening |
| 32 flattened nodes | accept | accept |
| 33 flattened nodes | reject, node cap | accept |
| 64 flattened nodes | reject, node cap | accept |
| 65 flattened nodes | reject, node cap | reject, node cap |
| op cost 64 (18 nodes) | accept | accept |
| op cost 65 (19 nodes) | reject, op cost | accept |
| op cost 128 | reject (flattened node cap fires first) | accept |
| op cost 129 | reject (flattened node cap fires first) | reject, op cost |
| depth 3 | accept | accept |
| depth 4 and longer | reject | reject |

## Non-finite numbers

Python's `json` parser accepts `NaN`, `Infinity`, `-Infinity`, and `1e309` (which becomes infinity).

- On a v2 graph, hashing raises `GraphError: non-finite number` before the const range check.
- On a v1 graph, `validate` raises `GraphError: const out of range`.
- `-0.0` is finite, is accepted, and canonicalises to `0`. A const `-0.0` and a const `0.0` produce the same content hash. Locked by the `neg_zero` cases.

## Unicode that is accepted on purpose

The schema forbids `/` and `.` in node ids and restricts `effect_id` to `[a-z0-9_]+`. Node ids are not otherwise restricted.

- A zero-width node id and a node id containing NUL flatten successfully. Reloading the JSON does not change the content hash (`unicode_node`, `nul_node`).
- An emoji / bidi note of at most 200 characters is accepted (`unicode_note`).
- NFC and NFD forms of the same name hash differently, and each form is stable across a JSON round trip (`nfc_nfd`).

`effect_id` values outside `[a-z0-9_]+` are a separate finding (F6), because the schema rejects them and the evaluator does not.

## F1 — missing required keys raise KeyError

`flatten` assumes `nodes` and `outputs` exist. `validate` raises `GraphError` for a missing key. `flatten` / `full_check` / `bake_key` do not.

- Property: no exception other than `GraphError`.
- Profiles: both (same code).
- Repros: `findings/repro_missing_outputs_v1.json` (`KeyError: 'outputs'`), `findings/repro_missing_nodes_v1.json` (`KeyError: 'nodes'`).
- An empty `outputs` object is handled and raises `GraphError` (`base_color` required).

## F2 — a non-object node raises TypeError

`check_exposed` builds `{node["id"]: node ...}` before it knows the node is an object. A node that is a JSON number fails with `TypeError: 'int' object is not subscriptable`.

- Property: no exception other than `GraphError`.
- Repro: `findings/repro_node_not_object_v1.json`.

## F3 — vector min/max crashes

The schema says vector params are range-checked per component. `_check_value` compares each component with `<` against `min` / `max`. When `min` is itself an array, that comparison raises `TypeError: '<' not supported between instances of 'float' and 'list'`. Scalar min/max on a vector param works.

- Property: no exception other than `GraphError`.
- Repro: `findings/repro_vector_minmax_v1.json`.

## F4 — deep arrays recurse out of `canonical_json`

`canonical_json` walks arrays by recursion. Depth 480 still returns. Depth 500 and depth 10,000 raise `RecursionError` in well under 2 seconds (no hang). The same happens for a graph field that is never otherwise validated, because the content hash canonicalises the whole document except `rev` and `preview`.

- Property: no exception other than `GraphError`.
- Repro: `findings/repro_deep_nest_v1.json` (depth 500, small enough for `json.loads`). The depth-10,000 case is built in memory by `test_deep_nest_10000_is_graph_error` because a 10,000-deep JSON text is the same failure mode.

## F5 — lone UTF-16 surrogate is not a GraphError

A JSON string `\ud800` loads in Python as a lone surrogate. `content_hash` then does `encode("utf-8")` and raises `UnicodeEncodeError`. Hashing is therefore not defined for that input, rather than rejected as `GraphError`.

- Property: no exception other than `GraphError`.
- Repro: `findings/repro_lone_surrogate_v1.json`.

## F6 — `effect_id` charset is not enforced

Schema text: `effect_id` matches `[a-z0-9_]+`. `flatten` accepts `fx_café` (and the same path accepts NUL, bidi, and emoji ids). The content hash is still deterministic for those strings.

- Property: expected reject, got accept.
- Repro: `findings/repro_unicode_effect_id_v1.json`.

## F7 — JSON booleans count as noise scale and seed

`isinstance(True, int)` is true, so `"scale": true` and `"seed": true` pass the int range checks (`true` is 1). The schema asks for an int. A boolean const value is rejected; this hole is only the noise scale and seed.

- Property: expected reject, got accept.
- Repro: `findings/repro_bool_seed_v1.json`.

## F8 — `rev.created` lowercase `z`

The schema asks for `YYYY-MM-DDTHH:MM:SSZ`. `datetime.strptime` matches literal characters in the format case-insensitively on every platform, so a trailing lowercase `z` is accepted and `check_rev_meta` accepts it. February 31 and a numeric offset are rejected. The behaviour is deterministic, so the xfail is strict.

- Property: expected reject, got accept on this runtime.
- Repro: `findings/repro_created_lower_z_v1.json`.

## Large inputs that behave

- 10,000 real const nodes: `validate` raises `GraphError` on the length check without walking the nodes, inside the 2 second / 512 MB budget.
- 1,000,000 nodes: the same length check rejects without walking. Building a million real dicts took about 0.63 seconds in an idle process, then `validate` returned in under a millisecond. Repeated under allocator pressure the build alone took 2.8 seconds and 5.8 seconds, so the default suite checks the million-node cap with a length-only sequence instead of allocating the bodies.
- A 1 MB `name` hashes and flattens. A 1 MB bundle prompt is rejected on the 4000-character cap.
- Bundle closure of 64 graphs accepts; 65 rejects. 8 items accept; 9 reject. Prompt length 4000 accepts; 4001 rejects.

## Open questions

1. Confirm the target profile (32 authored, 64 flattened nodes, 128 flattened op cost, depth 3) and publish it as a schema revision. Schema v3 still documents 32 / 64 flattened.
2. Should `effect_id` (and exposed names) actually be checked against `[a-z0-9_]+`, or is the free-string behaviour of the reference evaluator the rule?
3. Should vector `min` / `max` be scalar only, or per-component arrays? The schema describes a per-component check; the code crashes on arrays.
4. Should a lone surrogate and a too-deep array become `GraphError` instead of `UnicodeEncodeError` / `RecursionError`?
