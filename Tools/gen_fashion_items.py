"""Write the fashion rows of Data/CSV/DT_Items.csv from Data/fashion/fashion_brands_v1.json.

Plain Python 3, no Unreal needed. Edit this script, not the CSV, then re-run it:

  python Tools/gen_fashion_items.py            # rewrite the fashion rows in place
  python Tools/gen_fashion_items.py --check    # exit 1 if the CSV is out of date

Each fashion item becomes one Outfit row, like LabCoat: StackMax 1, Tier 0, Value 0,
MaterialCategory None, not wagerable, not rentable, not exclusive, not account-bound.
OutfitSlot is an EGFOutfitSlot name. Keep TYPE_SLOT in sync with
FGFFashionItems::SlotForItemType in Source/GFFashion/Private/GFFashionItems.cpp.

  hat/mask  -> Hair       (the head slot: visors, beanies, hoods, tiaras)
  top       -> Top
  full suit -> Top        (one item, one slot; there is no suit slot)
  bottoms   -> Bottom
  shoes     -> Shoes
  bag/purse, accessory, cosmetic -> Accessory

Rows that are not fashion items stay in place. LabCoat is Top and SafetyGoggles is
Accessory. Any other Outfit row without a slot stops the script.

There is no hand-edited DT_Items.uasset in the repo. The CSV is what the game reads
when the ItemTable soft reference is empty. To refresh the asset on a machine with
the editor:

  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs path>/Tools/import_datatables.py" -unattended -nullrhi

That rebuilds /Game/Data/DT_Items from this CSV (see Tools/import_datatables.py).
"""
import csv
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRANDS = os.path.join(ROOT, "Data", "fashion", "fashion_brands_v1.json")
ITEMS = os.path.join(ROOT, "Data", "CSV", "DT_Items.csv")

# Brand-list item type -> DT_Items.OutfitSlot. Must match FGFFashionItems::SlotForItemType.
TYPE_SLOT = {
    "hat/mask": "Hair",
    "top": "Top",
    "full suit": "Top",
    "bottoms": "Bottom",
    "shoes": "Shoes",
    "bag/purse": "Accessory",
    "accessory": "Accessory",
    "cosmetic": "Accessory",
}

# Outfit rows that are not fashion items, and the slot they already wear.
KEPT_OUTFIT_SLOTS = {
    "LabCoat": "Top",
    "SafetyGoggles": "Accessory",
}

EXPECTED_HEADER = [
    "---", "DisplayName", "Icon", "Category", "StackMax", "Rarity", "SourceTag",
    "Tier", "Value", "MaterialCategory", "bWagerable", "bRentable", "Exclusive",
    "bAccountBound", "OutfitSlot",
]


def nsloc(key, text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return 'NSLOCTEXT("GFItems", "%s", "%s")' % (key, escaped)


def load_brands():
    with open(BRANDS, encoding="utf-8") as f:
        data = json.load(f)
    items = []
    seen = set()
    for brand in data["brands"]:
        for item in brand["items"]:
            if item["id"] in seen:
                raise SystemExit("duplicate fashion item id " + item["id"])
            seen.add(item["id"])
            if item["type"] not in TYPE_SLOT:
                raise SystemExit("unknown item type %r on %s" % (item["type"], item["id"]))
            items.append(item)
    if len(items) != 116:
        raise SystemExit("expected 116 fashion items, found %d" % len(items))
    return items


def load_table():
    with open(ITEMS, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    if not rows:
        raise SystemExit("DT_Items.csv is empty")
    head = rows[0]
    base = [c for c in head if c != "OutfitSlot"]
    expected_base = [c for c in EXPECTED_HEADER if c != "OutfitSlot"]
    if base != expected_base:
        raise SystemExit("DT_Items.csv header changed; update the generator: %r" % head)
    if "OutfitSlot" in head and head[-1] != "OutfitSlot":
        raise SystemExit("OutfitSlot must stay the last column")
    body = []
    for n, row in enumerate(rows[1:], start=2):
        if not any(c.strip() for c in row):
            continue
        if len(row) < len(base):
            raise SystemExit("DT_Items.csv line %d has %d columns, expected at least %d" % (n, len(row), len(base)))
        body.append(row[:len(head)] if "OutfitSlot" in head else row[:len(base)])
    return head, body


def fashion_row(item):
    return [
        item["id"],
        nsloc(item["id"], item["name"]),
        "None",
        "Outfit",
        "1",
        "Common",
        "",
        "0",
        "0",
        "None",
        "False",
        "False",
        "None",
        "False",
        TYPE_SLOT[item["type"]],
    ]


def build():
    items = load_brands()
    fashion_ids = {item["id"] for item in items}
    head, body = load_table()
    base_len = len(EXPECTED_HEADER) - 1
    kept = []
    for row in body:
        item_id = row[0]
        if item_id in fashion_ids:
            continue
        fields = row[:base_len]
        category = fields[3]
        if category == "Outfit":
            slot = KEPT_OUTFIT_SLOTS.get(item_id)
            if not slot:
                raise SystemExit("outfit %s has no EGFOutfitSlot; add it to KEPT_OUTFIT_SLOTS" % item_id)
        else:
            slot = "None"
        kept.append(fields + [slot])
    taken = {row[0] for row in kept}
    fashion = []
    for item in items:
        if item["id"] in taken:
            raise SystemExit("item id clash: " + item["id"])
        taken.add(item["id"])
        fashion.append(fashion_row(item))
    buf = io.StringIO()
    writer = csv.writer(buf, lineterminator="\r\n", quoting=csv.QUOTE_MINIMAL)
    writer.writerow(EXPECTED_HEADER)
    for row in kept + fashion:
        if len(row) != len(EXPECTED_HEADER):
            raise SystemExit("row %s has %d columns" % (row[0], len(row)))
        writer.writerow(row)
    return buf.getvalue(), len(fashion), len(kept)


def validate(text):
    rows = list(csv.reader(io.StringIO(text)))
    if rows[0] != EXPECTED_HEADER:
        raise SystemExit("header mismatch")
    items = load_brands()
    by_id = {row[0]: row for row in rows[1:]}
    if len(by_id) != len(rows) - 1:
        raise SystemExit("duplicate row names")
    problems = []
    for item in items:
        row = by_id.get(item["id"])
        if row is None:
            problems.append("missing " + item["id"])
            continue
        slot = TYPE_SLOT[item["type"]]
        expect = fashion_row(item)
        if row != expect:
            problems.append("%s row mismatch: %r" % (item["id"], row))
        if slot not in ("Hair", "Top", "Bottom", "Shoes", "Accessory"):
            problems.append("bad slot " + slot)
    for item_id, slot in KEPT_OUTFIT_SLOTS.items():
        row = by_id.get(item_id)
        if not row or row[3] != "Outfit" or row[7] != "0" or row[8] != "0" or row[-1] != slot:
            problems.append("kept outfit %s drifted" % item_id)
    wood = by_id.get("Wood")
    if not wood or wood[-1] != "None" or wood[7] != "1":
        problems.append("Wood drifted")
    ferry = by_id.get("FerryPass")
    if not ferry or ferry[-2] != "True" or ferry[-1] != "None":
        problems.append("FerryPass account-bound flag drifted")
    parka = by_id.get("offrime_01")
    if not parka or "PARKA" not in parka[1] or parka[-1] != "Top":
        problems.append("quoted display name failed to round-trip")
    if problems:
        raise SystemExit("DT_Items.csv fashion rows failed validation:\n" + "\n".join(problems))


def main():
    text, fashion_count, kept_count = build()
    validate(text)
    with open(ITEMS, newline="", encoding="utf-8") as f:
        current = f.read()
    if "--check" in sys.argv:
        if current != text:
            print("DT_Items.csv fashion rows are out of date; run Tools/gen_fashion_items.py")
            sys.exit(1)
        print("DT_Items.csv is up to date (%d fashion rows, %d other rows)" % (fashion_count, kept_count))
        return
    with open(ITEMS, "w", newline="", encoding="utf-8") as f:
        f.write(text)
    print("wrote %d fashion rows (%d other rows kept) to %s" % (fashion_count, kept_count, ITEMS))


if __name__ == "__main__":
    main()
