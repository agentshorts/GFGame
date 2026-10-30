# Effect-graph validator fuzz (v1)

Deterministic property harness for VoxelFinish effect graphs. It does not bake previews and it does not use the GPU. The only libraries it needs beyond the standard library are the ones the reference evaluator already imports: numpy and Pillow. Pytest runs the suite. Hypothesis is not required.

## Run

From the repo root, with Python 3.12 (it has numpy, Pillow, and pytest on the dev machine; the default `py -3` is 3.13 there and has none of them):

```
py -3.12 -m pytest Tools/VoxelFinish/fuzz -q
```

pytest is required: `test_fuzz_fxgraph_v1.py` imports it even when run as a script. Running the file directly only runs the seeded fuzz suite (no xfail findings):

```
py -3.12 Tools/VoxelFinish/fuzz/test_fuzz_fxgraph_v1.py
```

Default: seed `1234`, `2000` cases, profile `both`, under 60 seconds. Any unexpected crash, wrong accept, wrong reject, or case over 2 seconds / 512 MB fails the process.

| variable | meaning |
|---|---|
| `VF_FUZZ_SEED` | integer seed for `random.Random` (default `1234`) |
| `VF_FUZZ_CASES` | total cases (default `2000`). Structured cases always run; the rest are random graphs |
| `VF_FUZZ_PROFILE` | `current`, `target`, or `both` (default `both`) |

Long soak:

```
VF_FUZZ_CASES=100000 py -3.12 -m pytest Tools/VoxelFinish/fuzz/test_fuzz_fxgraph_v1.py -q -k fuzz_suite
```

On Windows PowerShell, set the variables first (`$env:VF_FUZZ_CASES="100000"`). Temp files, if any, stay in the system temp directory. There is no network and no GPU work.

This repo has no CI workflow. The command above is the check to add when a workflow exists. Add it as a new workflow file; do not edit an existing one.

## Profiles

| profile | module | authored nodes | flattened nodes | flattened op cost | depth |
|---|---|---|---|---|---|
| `current` | `vf_reference/fxgraph_eval_v2.py` | 32 | 32 | 64 | 3 |
| `target` | `vf_reference/fxgraph_eval_v3.py` | 32 | 64 | 128 | 3 |

`current` is what v2 enforces today. `target` is the approved combined-effect cap: each authored graph stays at 32 nodes, and the flattened graph may use 64 nodes and 128 op cost. Graphs with 33–64 flattened nodes, or flattened op cost 65–128, are rejected by v2 and accepted by v3. That is a spec gap, recorded in `findings_v1.md`. v2 is not modified.

Entry point for a graph file is `flatten` / `full_check` (authored cap, then the flattened cap). `validate()` is the checker that runs on the flattened graph, so on v3 its node and cost caps are the flattened ones.

## What is generated

- Valid mutations of the vendored starter graphs (reorder, rename, in-range const tweak). These must flatten.
- Random subeffect-free graphs. An independent oracle in `fuzz_fxgraph_v1.py` decides accept or reject; the evaluator must agree.
- Adversarial graphs for cycles, depth, both limit profiles (at the cap and just over it), non-finite numbers, bad wiring, revision/hash tampering, bundle tampering, unicode, and large inputs.

Known crashes and schema mismatches are not "fixed" in v2 or v3. They are xfail tests with a repro under `findings/`. See `findings_v1.md`.

## v3 diff against v2

`vf_reference/fxgraph_eval_v3.py` is a copy of v2 plus:

- `MAX_AUTHORED_NODES = 32`, `MAX_FLAT_NODES = 64`, `MAX_FLAT_COST = 128`
- `MAX_NODES, MAX_COST = MAX_FLAT_NODES, MAX_FLAT_COST`, so `validate()` enforces the flattened cap
- the pre-inline node check in `flatten()` uses `MAX_AUTHORED_NODES`

No other rule changes.

## Schema note

The effect-graph schema v3 document describes a flattened cap of 32 nodes and 64 op cost. The target profile above is 64 flattened nodes and 128 op cost, with authored graphs still capped at 32. The target profile is confirmed and is written up in `Tools/VoxelFinish/effects_schema_v4.md` (v3 is not edited).

## Budgets

- Each case: under 2 seconds and under 512 MB.
- Default suite: seed 1234, 2000 cases, under 60 seconds.
- A real list of 10,000 nodes is rejected inside that budget. A million-node graph is rejected by the `len` check without building a million node dicts; materialising that list is allocator work and can exceed 2 seconds once the process is already busy. Numbers are in `findings_v1.md`.
