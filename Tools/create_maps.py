"""Creates the empty greybox maps the game mode populates at runtime (run once, headless):
UnrealEditor-Cmd.exe GFGame.uproject -ExecutePythonScript="<abs>/Tools/create_maps.py" -unattended -nullrhi

L_Hub is the Rime hub (gates are spawned there at play).
L_Valley, L_Runway, L_Moba and L_Beat are the mode maps the hub gates open.
The runway map hosts the runway stage. The Beat Hop map hosts the course.
Depths, the dino arena, the mine and the lab have no map; they sit on pads in the hub.
"""
import unreal

MAPS = [
    "/Game/Maps/L_NetTest",
    "/Game/Maps/L_Hub",
    "/Game/Maps/L_Valley",
    "/Game/Maps/L_Runway",
    "/Game/Maps/L_Moba",
    "/Game/Maps/L_Beat",
]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def readable(path):
    try:
        return unreal.EditorAssetLibrary.load_asset(path) is not None
    except Exception:
        return False


for path in MAPS:
    if unreal.EditorAssetLibrary.does_asset_exist(path) and readable(path):
        unreal.log("GFMaps: exists " + path)
        continue
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("GFMaps: replacing unreadable " + path)
        unreal.EditorAssetLibrary.delete_asset(path)
    ok = les.new_level(path)
    ok = ok and les.save_current_level()
    unreal.log("GFMaps: created %s ok=%s" % (path, ok))
unreal.SystemLibrary.quit_editor()
