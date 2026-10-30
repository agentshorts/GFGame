"""Creates the empty greybox maps the game mode populates at runtime (run once, headless):
UnrealEditor-Cmd.exe GFGame.uproject -ExecutePythonScript="<abs>/Tools/create_maps.py" -unattended -nullrhi
"""
import unreal

MAPS = ["/Game/Maps/L_NetTest", "/Game/Maps/L_Hub"]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for path in MAPS:
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("GFMaps: exists " + path)
        continue
    ok = les.new_level(path)
    ok = ok and les.save_current_level()
    unreal.log("GFMaps: created %s ok=%s" % (path, ok))
unreal.SystemLibrary.quit_editor()
