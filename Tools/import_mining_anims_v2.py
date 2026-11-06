"""Reimport mining_anims_v2 clips onto SKEL_GF_Humanoid at 120 Hz sample rate (#380).

Replaces existing A_Player_Base_* mining clips. Root motion off. Drops the _v2 suffix.
"""
import unreal, sys, os
SRC = os.environ.get("GFMINING_FBX", r"C:/DevPersonal/GFGame-builds/mining380/fbx_v2")
DEST = "/Game/Art/Characters/Player/Anims"
CLIPS = [
    "Mine_Punch", "Mine_Dig", "Mine_Field_Start", "Mine_Field_Loop", "Mine_Field_End",
    "Mine_Claw", "Mine_Overcharge", "Cast_Element_Swap", "FistScope",
]
ref = unreal.load_asset(DEST + "/A_Player_Base_Idle")
skel = ref.get_editor_property("skeleton")
unreal.log("ANIMIMPORT skeleton %s" % skel.get_path_name())
tasks = []
for c in CLIPS:
    name = "A_Player_Base_" + c
    o = unreal.FbxImportUI()
    o.set_editor_property("import_mesh", False)
    o.set_editor_property("import_animations", True)
    o.set_editor_property("import_as_skeletal", True)
    o.set_editor_property("import_materials", False)
    o.set_editor_property("import_textures", False)
    o.set_editor_property("skeleton", skel)
    o.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    a = o.get_editor_property("anim_sequence_import_data")
    a.set_editor_property("use_default_sample_rate", False)
    a.set_editor_property("custom_sample_rate", 120)
    a.set_editor_property("import_bone_tracks", True)
    a.set_editor_property("remove_redundant_keys", False)
    try:
        a.set_editor_property("import_custom_attribute", False)
    except Exception:
        pass
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", SRC + "/A_Player_Base_" + c + "_v2.fbx")
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    t.set_editor_property("options", o)
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = 0
for c in CLIPS:
    p = DEST + "/A_Player_Base_" + c
    s = unreal.load_asset(p)
    if not s:
        unreal.log_error("ANIMIMPORT missing %s" % p)
        continue
    try:
        n = unreal.AnimationLibrary.get_num_frames(s)
        ln = unreal.AnimationLibrary.get_sequence_length(s)
    except Exception:
        n, ln = -1, -1.0
    unreal.log("ANIMIMPORT ok %s skel=%s keys=%s len=%.4f" % (p, s.get_editor_property("skeleton").get_name(), n, ln))
    ok += 1
unreal.log("ANIMIMPORT DONE ok=%d" % ok)
if ok != len(CLIPS):
    sys.exit(1)
