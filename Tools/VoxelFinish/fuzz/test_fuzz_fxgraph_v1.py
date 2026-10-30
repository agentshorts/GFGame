"""Pytest entry for the effect-graph fuzz harness.

Run with `py -3.12 -m pytest Tools/VoxelFinish/fuzz -q`. pytest, numpy, and Pillow are required;
running this file directly (`py -3.12 test_fuzz_fxgraph_v1.py`) still imports pytest and only runs
the seeded fuzz suite.
"""
import json
import sys
from pathlib import Path

import pytest

import fuzz_fxgraph_v1 as fuzz

E = fuzz.eval_v2
FINDINGS = Path(__file__).resolve().parent / "findings"


def _load(name):
    return json.loads((FINDINGS / name).read_text(encoding="utf-8"))


def test_fuzz_suite():
    result = fuzz.run_from_env()
    assert result.ok, result.summary()
    if result.requested == 2000 and result.seed == 1234 and result.profile == "both":
        assert result.elapsed < 60.0, result.elapsed


@pytest.mark.xfail(raises=KeyError, strict=True, reason="F1 flatten raises KeyError when outputs is absent")
def test_missing_outputs_is_graph_error():
    graph = _load("repro_missing_outputs_v1.json")
    with pytest.raises(E.GraphError):
        E.flatten(graph, E.Registry(graphs=[]))


@pytest.mark.xfail(raises=KeyError, strict=True, reason="F1 flatten raises KeyError when nodes is absent")
def test_missing_nodes_is_graph_error():
    graph = _load("repro_missing_nodes_v1.json")
    with pytest.raises(E.GraphError):
        E.flatten(graph, E.Registry(graphs=[]))


@pytest.mark.xfail(raises=TypeError, strict=True, reason="F2 a non-object node raises TypeError")
def test_node_not_object_is_graph_error():
    graph = _load("repro_node_not_object_v1.json")
    with pytest.raises(E.GraphError):
        E.flatten(graph, E.Registry(graphs=[]))


@pytest.mark.xfail(raises=TypeError, strict=True, reason="F3 vector min/max crashes instead of a per-component check")
def test_vector_minmax_is_graph_error():
    graph = _load("repro_vector_minmax_v1.json")
    with pytest.raises(E.GraphError):
        E.flatten(graph, E.Registry(graphs=[]))


@pytest.mark.xfail(raises=RecursionError, strict=True, reason="F4 canonical JSON recurses on deep arrays")
def test_deep_nest_file_is_graph_error():
    graph = _load("repro_deep_nest_v1.json")
    with pytest.raises(E.GraphError):
        E.content_hash(graph)


@pytest.mark.xfail(raises=RecursionError, strict=True, reason="F4 depth 10000 also recurses")
def test_deep_nest_10000_is_graph_error():
    nested = 0
    for _ in range(10_000):
        nested = [nested]
    with pytest.raises(E.GraphError):
        E.canonical_json(nested)


@pytest.mark.xfail(raises=UnicodeEncodeError, strict=True, reason="F5 lone surrogate escapes as UnicodeEncodeError")
def test_lone_surrogate_is_graph_error():
    graph = _load("repro_lone_surrogate_v1.json")
    with pytest.raises(E.GraphError):
        E.content_hash(graph)


@pytest.mark.xfail(strict=True, reason="F6 effect_id charset is not enforced")
def test_effect_id_charset_rejected():
    graph = _load("repro_unicode_effect_id_v1.json")
    with pytest.raises(E.GraphError):
        E.flatten(graph, E.Registry(graphs=[]))


@pytest.mark.xfail(strict=True, reason="F7 JSON booleans are accepted as noise scale and seed")
def test_bool_noise_seed_rejected():
    graph = _load("repro_bool_seed_v1.json")
    with pytest.raises(E.GraphError):
        E.validate(graph)


@pytest.mark.xfail(strict=True, reason="F8 created timestamp with a lowercase z is accepted: strptime matches the literal Z case-insensitively")
def test_created_requires_uppercase_z():
    graph = _load("repro_created_lower_z_v1.json")
    with pytest.raises(E.GraphError):
        E.check_rev_meta(graph)


if __name__ == "__main__":
    outcome = fuzz.run_from_env()
    print(outcome.summary())
    sys.exit(0 if outcome.ok else 1)
