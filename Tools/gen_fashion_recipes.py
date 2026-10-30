"""Generate the fashion rows of Data/CSV/DT_Recipes.csv from Data/fashion/fashion_brands_v1.json (#88).

Plain Python 3, no Unreal needed. Re-run after the brand list changes:
  python Tools/gen_fashion_recipes.py            # rewrite the fashion rows in place
  python Tools/gen_fashion_recipes.py --check    # exit 1 if the CSV is out of date

Rules (see the Decision issues linked from #88):
  * Row name = recipe id = fashion item id (e.g. dirtachi_01). Output = 1 of that id.
  * Station = Atelier. UnlockTag empty (unlocked from the start).
  * Inputs are existing, evergreen DT_Items materials only (Exclusive=None). Placeholder
    material names are mapped through MATERIAL_MAP below.
  * Each item type has a unit budget (TYPE_UNITS). Tier 3+ inputs cost 1 each (2 for a
    full suit); the remaining units are split over the tier 1-2 inputs.
Non-fashion rows (anything whose Station is not Atelier) are kept untouched and in order.
"""
import csv
import io
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BRANDS = os.path.join(ROOT, "Data", "fashion", "fashion_brands_v1.json")
ITEMS = os.path.join(ROOT, "Data", "CSV", "DT_Items.csv")
RECIPES = os.path.join(ROOT, "Data", "CSV", "DT_Recipes.csv")

STATION = "Atelier"

# Placeholder material (fashion_brands_v1.json) -> existing DT_Items id.
MATERIAL_MAP = {
    "dead skin": "Scales",       # shed skin; no new item row (see the Decision issue)
    "ash silk": "Fabric",
    "crystal": "Crystal",
    "fur": "Fiber",
    "gold dust": "Glitter",
    "rubber sap": "Clay",
    "moss thread": "Ribbon",
    "scales": "Scales",
    "berry dye": "RoseDye",
    "glitter": "Glitter",
    "teeth": "Bone",
    "frost thread": "Ribbon",
    "bone": "Bone",
    "glacier water": "SkyDye",
    "obsidian": "Stone",
    "ember core": "Amber",       # EmberCore is HallowNight-only; Amber keeps it year-round
    "mud clay": "Clay",
    "junk scrap": "ScrapWire",
    "amber": "Amber",
    "feathers": "Flowers",
    "sap": "Amber",
    "ash": "Coalnut",
    "tar": "Coalnut",
}

# Total input units per item type (cheap -> pricey).
TYPE_UNITS = {
    "cosmetic": 3,
    "accessory": 4,
    "hat/mask": 5,
    "shoes": 6,
    "bottoms": 6,
    "top": 7,
    "bag/purse": 7,
    "full suit": 12,
}


def load_items():
    with open(ITEMS, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    head = rows[0]
    out = {}
    for r in rows[1:]:
        d = dict(zip(head, r))
        out[r[0]] = d
    return out


def recipe_for(item, items):
    t = item["type"]
    if t not in TYPE_UNITS:
        raise SystemExit("unknown item type %r on %s" % (t, item["id"]))
    ids = []
    for m in item["materials"]:
        if m not in MATERIAL_MAP:
            raise SystemExit("unmapped placeholder material %r on %s" % (m, item["id"]))
        mid = MATERIAL_MAP[m]
        if mid not in ids:
            ids.append(mid)
    budget = TYPE_UNITS[t]
    pricey_each = 2 if t == "full suit" else 1
    counts = {}
    commons = []
    for mid in ids:
        row = items.get(mid)
        if row is None or row["Exclusive"] != "None":
            raise SystemExit("%s maps to %s, not an evergreen DT_Items row" % (item["id"], mid))
        if int(row["Tier"]) >= 3:
            counts[mid] = pricey_each
        else:
            commons.append(mid)
    left = budget - sum(counts.values())
    if not commons:
        # All-pricey recipe: the tier-3 inputs already make it dear; nothing else to add.
        pass
    else:
        left = max(left, len(commons))
        base, extra = divmod(left, len(commons))
        for i, mid in enumerate(commons):
            counts[mid] = base + (1 if i < extra else 0)
    return [(mid, counts[mid]) for mid in ids]


def fmt_inputs(stacks):
    return "(" + ",".join('(ItemId="%s",Count=%d)' % (i, c) for i, c in stacks) + ")"


def build():
    items = load_items()
    with open(BRANDS, encoding="utf-8") as f:
        data = json.load(f)
    with open(RECIPES, newline="", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    head, body = rows[0], rows[1:]
    kept = [r for r in body if r[3] != STATION]
    taken = {r[0] for r in kept}
    fashion = []
    for b in data["brands"]:
        for it in b["items"]:
            if it["id"] in taken:
                raise SystemExit("recipe id clash: " + it["id"])
            taken.add(it["id"])
            fashion.append([it["id"], fmt_inputs(recipe_for(it, items)), '(ItemId="%s",Count=1)' % it["id"], STATION, ""])
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\r\n", quoting=csv.QUOTE_MINIMAL)
    for r in [head] + kept + fashion:
        w.writerow(r)
    return buf.getvalue(), len(fashion)


def main():
    text, n = build()
    with open(RECIPES, newline="", encoding="utf-8") as f:
        current = f.read()
    if "--check" in sys.argv:
        if current != text:
            print("DT_Recipes.csv fashion rows are out of date; run Tools/gen_fashion_recipes.py")
            sys.exit(1)
        print("DT_Recipes.csv is up to date (%d fashion rows)" % n)
        return
    with open(RECIPES, "w", newline="", encoding="utf-8") as f:
        f.write(text)
    print("wrote %d fashion rows to %s" % (n, RECIPES))


if __name__ == "__main__":
    main()
