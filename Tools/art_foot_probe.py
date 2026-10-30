"""Measure foot/ground penetration of handoff FBX clips by evaluating the skinned mesh per frame in Blender.

Run (no render, CPU only):
  blender.exe -b --factory-startup -P Tools/art_foot_probe.py -- <inbox dir> <out json>
For every clip: imports the asset's mesh FBX + the clip FBX, puts the clip action on the mesh armature,
and records min world Z (cm) of the deformed mesh and the foot/toe bone heads for every frame.
"""
import bpy, json, os, re, sys

args = sys.argv[sys.argv.index("--") + 1:]
INBOX, OUT = args[0], args[1]
ASSETS = ["SK_Player_Base", "SK_Dino_Raptor"]

def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def imp(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, use_anim=True)
    return [o for o in bpy.data.objects if o not in before]

def cm_scale():
    # Blender's FBX importer maps the cm file into a metre scene; measure in cm.
    return 100.0 / bpy.context.scene.unit_settings.scale_length if bpy.context.scene.unit_settings.scale_length else 100.0

res = {}
for asset in ASSETS:
    folder = os.path.join(INBOX, asset)
    man = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
    res[asset] = {}
    for af in man["files"]["animations"]:
        clear()
        mesh_objs = imp(os.path.join(folder, man["files"]["mesh"]))
        arm = next(o for o in mesh_objs if o.type == "ARMATURE")
        meshes = [o for o in mesh_objs if o.type == "MESH"]
        for o in mesh_objs:
            if o.animation_data:
                o.animation_data_clear()
        clip_objs = imp(os.path.join(folder, af))
        carm = next(o for o in clip_objs if o.type == "ARMATURE")
        act = carm.animation_data.action
        slot = getattr(carm.animation_data, "action_slot", None)
        arm.animation_data_create()
        arm.animation_data.action = act
        if slot is not None and hasattr(arm.animation_data, "action_slot"):
            try:
                arm.animation_data.action_slot = slot
            except Exception:
                pass
        for o in clip_objs:
            bpy.data.objects.remove(o, do_unlink=True)
        f0, f1 = [int(round(x)) for x in act.frame_range]
        sc = bpy.context.scene
        # rest-pose bounds for unit sanity
        k = 100.0  # metres -> cm (Blender FBX import of a cm file yields metres with object scale applied)
        feet = [b.name for b in arm.pose.bones if re.search(r"foot|toe|ball", b.name, re.I)]
        frames = []
        for f in range(f0, f1 + 1):
            sc.frame_set(f)
            dg = bpy.context.evaluated_depsgraph_get()
            mz = None; mx = None
            for o in meshes:
                oe = o.evaluated_get(dg)
                me = oe.to_mesh()
                mw = oe.matrix_world
                for v in me.vertices:
                    z = (mw @ v.co).z * k
                    mz = z if mz is None or z < mz else mz
                    mx = z if mx is None or z > mx else mx
                oe.to_mesh_clear()
            ae = arm.evaluated_get(dg)
            fz = {b: round((ae.matrix_world @ ae.pose.bones[b].head).z * k, 3) for b in feet}
            frames.append({"f": f, "mesh_min_z": round(mz, 3), "mesh_max_z": round(mx, 3), "feet_z": fz})
        worst = min(frames, key=lambda r: r["mesh_min_z"])
        res[asset][os.path.splitext(af)[0]] = {"frame_range": [f0, f1], "worst_min_z_cm": worst["mesh_min_z"], "worst_frame": worst["f"],
                                              "first_frame_min_z": frames[0]["mesh_min_z"], "per_frame": frames}
        print("GFProbe", asset, af, "range", f0, f1, "worst", worst["mesh_min_z"], "@", worst["f"], "max_z", max(r["mesh_max_z"] for r in frames))
json.dump(res, open(OUT, "w"), indent=1)
print("GFProbe wrote", OUT)
