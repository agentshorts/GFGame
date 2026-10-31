"""Create/refresh /Game/Data/DT_* DataTables from Data/CSV/*.csv (PLAN.md section 5).

The CSV files are the source of truth. DT_Items.uasset is not edited by hand; this script
rebuilds /Game/Data/DT_Items from Data/CSV/DT_Items.csv, including the OutfitSlot column on
fashion rows. FGFDataTables reads that CSV whenever the ItemTable soft reference is empty.

Run headless:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs path>/Tools/import_datatables.py" -unattended -nullrhi
"""
import os
import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CSV_DIR = os.path.join(PROJECT_DIR, "Data", "CSV")
DEST = "/Game/Data"
TABLES = {
    "DT_Blocks": "/Script/GFVoxel.GFBlockRow",
    "DT_Items": "/Script/GFCore.GFItemRow",
    "DT_Recipes": "/Script/GFCore.GFRecipeRow",
    "DT_Plants": "/Script/GFGarden.GFPlantRow",
    "DT_DinoDiet": "/Script/GFCompanion.DinoDietRow",
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
failed = []
for name, struct_path in TABLES.items():
    csv_path = os.path.join(CSV_DIR, name + ".csv")
    struct = unreal.load_object(None, struct_path)
    if struct is None:
        failed.append(name + " (struct missing: " + struct_path + ")")
        continue
    asset_path = DEST + "/" + name
    table = unreal.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
    if table is None:
        factory = unreal.DataTableFactory()
        factory.struct = struct
        table = asset_tools.create_asset(name, DEST, unreal.DataTable, factory)
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_file(table, csv_path, struct)
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    unreal.log("GFImport {}: ok={} rows={}".format(name, ok, len(rows)))
    if not ok or len(rows) == 0:
        failed.append(name)
    unreal.EditorAssetLibrary.save_loaded_asset(table)

if failed:
    unreal.log_error("GFImport FAILED: " + ", ".join(failed))
else:
    unreal.log("GFImport ALL OK")
