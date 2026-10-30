"""Ledger coverage check: every asset the importer imports (ORDER in Tools/import_art_batch.py) has a row in
docs/LicenseLedger.csv, and no row credits a bot or is marked PRIVATE.

Run: python Tools/check_ledger.py   (exit code 1 on any problem; no Unreal needed)
"""
import ast
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def import_order():
    tree = ast.parse(io.open(os.path.join(ROOT, "Tools", "import_art_batch.py"), encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "ORDER" for t in node.targets):
            return [e.value for e in node.value.elts]
    raise RuntimeError("ORDER not found in import_art_batch.py")


def main():
    rows = list(csv.DictReader(io.open(os.path.join(ROOT, "docs", "LicenseLedger.csv"), encoding="utf-8", newline="")))
    problems = []
    for name in import_order():
        pat = re.compile(r"handoff[\\/]inbox[\\/]" + re.escape(name) + r"($|[\\/ ;,(])")
        if not any(pat.search(r["asset_path"]) for r in rows):
            problems.append("no ledger row for imported asset " + name)
    for i, r in enumerate(rows, start=2):
        if r["creator"] not in ("GFGame project (own work)", "project artist (Blender)"):
            problems.append("line {}: creator is not a project credit".format(i))
        if "PRIVATE" in ",".join(v or "" for v in r.values()):
            problems.append("line {}: PRIVATE row".format(i))
    for p in problems:
        print(p)
    print("ledger check: {} rows, {} problems".format(len(rows), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())