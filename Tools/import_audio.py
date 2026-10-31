"""Imports handoff audio as SoundWaves under /Game/Audio (headless editor Python, like import_art_batch.py).

Run:
  UnrealEditor-Cmd.exe GFGame.uproject -run=pythonscript -script="<abs>/Tools/import_audio.py" -unattended -nullrhi -nosplash

The raw .wav stays out of git (handoff/ is ignored); the imported .uasset is what ships. GFAUDIO_SRC overrides the
source file for the one entry (a copy elsewhere on disk). Writes Saved/ArtImport/audio_report.json.
"""
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())

# name -> (handoff file, destination folder, expected seconds, channels)
AUDIO = {
    "MUS_BeatHop_Fallback_v1": (
        os.path.join("handoff", "inbox", "MUS_BeatHop_Fallback_v1", "MUS_BeatHop_Fallback_v1.wav"),
        "/Game/Audio",
        31.29,
        2,
    ),
}


def main():
    report = {"imported": [], "errors": []}
    tasks = []
    for name, (rel, dest, _, _) in AUDIO.items():
        src = os.environ.get("GFAUDIO_SRC") if len(AUDIO) == 1 and os.environ.get("GFAUDIO_SRC") else os.path.join(PROJECT, rel)
        if not os.path.isfile(src):
            report["errors"].append("%s: source missing (%s)" % (name, src))
            continue
        task = unreal.AssetImportTask()
        task.filename = src
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append((name, dest, task))
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, _, t in tasks])
    for name, dest, task in tasks:
        path = "%s/%s.%s" % (dest, name, name)
        wave = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(wave, unreal.SoundWave):
            report["errors"].append("%s: no SoundWave at %s" % (name, path))
            continue
        want_s, want_ch = AUDIO[name][2], AUDIO[name][3]
        dur = float(wave.get_editor_property("duration"))
        ch = rate = None
        try:
            ch = int(wave.get_editor_property("num_channels"))
        except Exception:
            pass
        try:
            rate = int(wave.get_sample_rate_for_current_platform())
        except Exception:
            pass
        row = {"asset": path, "source": task.filename, "duration_s": round(dur, 3), "channels": ch, "sample_rate": rate}
        if abs(dur - want_s) > 0.1:
            report["errors"].append("%s: duration %.3f s, expected %.2f" % (name, dur, want_s))
        if ch is not None and ch != want_ch:
            report["errors"].append("%s: %d channels, expected %d" % (name, ch, want_ch))
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        report["imported"].append(row)
    out_dir = os.path.join(PROJECT, "Saved", "ArtImport")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "audio_report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    for row in report["imported"]:
        unreal.log("GFAUDIO imported %s" % json.dumps(row))
    for err in report["errors"]:
        unreal.log_error("GFAUDIO error %s" % err)
    unreal.log("GFAUDIO RESULT imported=%d errors=%d" % (len(report["imported"]), len(report["errors"])))


main()
