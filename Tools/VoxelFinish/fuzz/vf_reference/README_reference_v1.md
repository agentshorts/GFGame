# Vendored effect-graph reference

These files are the VoxelFinish effect-graph reference used by the fuzz harness.

- `fxgraph_eval_v2.py` — unchanged reference evaluator (schema `voxelfinish.effect_graph/2`, with `gfgame.effect_graph/1` still loading).
- `graphs/` — unchanged starter graphs (v1 graphs plus v2 revisions and composites).
- `fxgraph_eval_v3.py` — the same evaluator with the target combined-effect limits. This is the only file in this folder that differs from the reference drop. The diff is listed in `../README_fuzz_v1.md`.

- `__init__.py` — makes this folder the `vf_reference` package. The harness imports `vf_reference.fxgraph_eval_v2` and `vf_reference.fxgraph_eval_v3`, never the bare module names.

Do not edit `fxgraph_eval_v2.py` or the graphs. A later limit change belongs in a new `fxgraph_eval_vN.py` beside v3.
