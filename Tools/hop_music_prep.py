"""Get local music ready for Acceleration Hop (issue 343). Never commits or copies anything into git.

The game plays .wav files from a git-ignored LicensedMusic/AccelHop folder, each with a BPM from a sidecar
<name>.json ({"bpm": 128, "offset_ms": 40, "title": "Night Drive"}) or from the file name ("Night Drive 128bpm.wav").

  python3 Tools/hop_music_prep.py check [FOLDER]
      What the game would do with each file in FOLDER (default Content/LicensedMusic/AccelHop).

  python3 Tools/hop_music_prep.py add SONG [SONG ...] [--out FOLDER] [--bpm 128] [--offset-ms 40] [--analyse]
      Converts each song to a 16-bit .wav with ffmpeg (mp3, ogg, flac, m4a... or a .wav the game can't read) into
      FOLDER and writes its sidecar. BPM: --bpm, else the file name, else --analyse (librosa, through
      Tools/beatmap_from_audio.py, which also finds offset_ms). Refuses an output folder git would track.

Only .wav plays in game because Unreal has no runtime mp3/ogg decoder without a plugin.
The name parser below mirrors FGFHopMusicLibrary::ParseBpmFromName (Source/GFRhythm/Private/GFHopMusic.cpp);
Tools/test_hop_music.py keeps the two in step.
"""
import argparse
import json
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_FOLDER = ROOT / "Content" / "LicensedMusic" / "AccelHop"
MIN_BPM, MAX_BPM = 40.0, 300.0
OTHER_AUDIO = {"mp3", "ogg", "flac", "m4a", "aac", "opus", "wma", "aif", "aiff"}
SEPARATORS = " _-=:"


def _try_bpm_number(text):
    if not text or text.startswith(".") or text.endswith(".") or text.count(".") > 1:
        return None
    whole = text.index(".") if "." in text else len(text)
    if whole < 2 or whole > 3:
        return None
    value = float(text)
    return value if MIN_BPM <= value <= MAX_BPM else None


def _tidy_title(title):
    title = title.replace("_", " ")
    for empty in ("[ ]", "( )", "{ }", "[]", "()", "{}"):
        title = title.replace(empty, "")
    while "  " in title:
        title = title.replace("  ", " ")
    return title.strip(" -.:=")


def _is_num(c):
    return c.isdigit() or c == "."


def parse_bpm_from_name(base):
    """(bpm, title) from a file name without its extension, or None. Same rules as the game."""
    lower = base.lower()
    search = 0
    while True:
        at = lower.find("bpm", search)
        if at < 0:
            return None
        search = at + 3
        word_after = at + 3 >= len(lower) or not lower[at + 3].isalpha()
        word_before = at == 0 or not lower[at - 1].isalpha()
        num_end = at
        while num_end > 0 and lower[num_end - 1] in SEPARATORS:
            num_end -= 1
        num_start = num_end
        while num_start > 0 and _is_num(lower[num_start - 1]):
            num_start -= 1
        if word_after and num_end > num_start and (num_start == 0 or not lower[num_start - 1].isalpha()):
            value = _try_bpm_number(lower[num_start:num_end])
            if value is not None:
                return value, _tidy_title(base[:num_start] + base[at + 3:])
        r = at + 3
        while r < len(lower) and lower[r] in SEPARATORS:
            r += 1
        r_end = r
        while r_end < len(lower) and _is_num(lower[r_end]):
            r_end += 1
        if word_before and r_end > r and (r_end >= len(lower) or not lower[r_end].isalnum()):
            value = _try_bpm_number(lower[r:r_end])
            if value is not None:
                return value, _tidy_title(base[:at] + base[r_end:])


def read_wav_info(data):
    """{channels, rate, bits, format, data_bytes} from a WAV header, or raises ValueError. Same limits as the game."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos, fmt = 12, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt ":
            tag, channels, rate = struct.unpack_from("<HHI", data, body)
            bits = struct.unpack_from("<H", data, body + 14)[0]
            if tag == 0xFFFE:
                tag = struct.unpack_from("<H", data, body + 24)[0]
            fmt = {"format": tag, "channels": channels, "rate": rate, "bits": bits}
        elif cid == b"data":
            if fmt is None:
                raise ValueError("data chunk before fmt")
            ok = (fmt["format"] == 1 and fmt["bits"] in (8, 16, 24, 32)) or (fmt["format"] == 3 and fmt["bits"] == 32)
            if not ok:
                raise ValueError("unsupported WAV encoding (format %d, %d bit)" % (fmt["format"], fmt["bits"]))
            if not 1 <= fmt["channels"] <= 2 or not 8000 <= fmt["rate"] <= 192000:
                raise ValueError("unsupported WAV layout (%d channels, %d Hz)" % (fmt["channels"], fmt["rate"]))
            fmt["data_bytes"] = size
            fmt["data_offset"] = body
            return fmt
        pos = body + size + (size & 1)
    raise ValueError("no data chunk" if fmt else "no fmt chunk")


def describe(path):
    """What the game does with one file: (status, detail). status is 'plays', 'skipped' or 'ignored'."""
    path = pathlib.Path(path)
    ext = path.suffix.lower().lstrip(".")
    if ext != "wav":
        if ext in OTHER_AUDIO:
            return "skipped", "only .wav plays in game (run: hop_music_prep.py add)"
        return "ignored", ""
    bpm, source, title = None, None, path.stem
    named = parse_bpm_from_name(path.stem)
    if named:
        bpm, title, source = named[0], named[1] or path.stem, "name"
    sidecar = path.with_suffix(".json")
    if sidecar.exists():
        try:
            meta = json.loads(sidecar.read_text(encoding="utf-8"))
            if MIN_BPM <= float(meta.get("bpm", 0)) <= MAX_BPM:
                bpm, source, title = float(meta["bpm"]), "sidecar", meta.get("title") or title
        except (ValueError, TypeError, AttributeError):
            pass
    if bpm is None:
        return "skipped", "no BPM (add %s.json with {\"bpm\": 120}, or put 120bpm in the file name)" % path.stem
    head = path.read_bytes()[:512 * 1024]
    try:
        info = read_wav_info(head)
    except (ValueError, struct.error) as err:
        return "skipped", str(err)
    frame = info["channels"] * info["bits"] // 8
    data_bytes = min(info["data_bytes"], path.stat().st_size - info["data_offset"])
    seconds = data_bytes // frame / info["rate"] if frame else 0
    return "plays", "%s: %.4g BPM (%s), %.1f s, %d Hz %d ch" % (title, bpm, source, seconds, info["rate"], info["channels"])


def git_tracks(folder):
    """True when git would track a new file in folder (so music must not go there)."""
    probe = pathlib.Path(folder).resolve() / "probe.wav"
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "-q", str(probe)], capture_output=True)
    except FileNotFoundError:
        return False
    if out.returncode == 128:  # not inside this repo: nothing to protect
        return False
    return out.returncode != 0


def cmd_check(args):
    folder = pathlib.Path(args.folder or DEFAULT_FOLDER)
    if not folder.is_dir():
        print("%s: missing; the game plays its built-in song" % folder)
        return 0
    playing = 0
    for path in sorted(folder.iterdir()):
        status, detail = describe(path)
        if status == "ignored":
            continue
        playing += status == "plays"
        print("%-7s %s  %s" % (status, path.name, detail))
    print("%d track(s) will play%s" % (playing, "" if playing else "; the game plays its built-in song"))
    return 0


def cmd_add(args):
    out = pathlib.Path(args.out or DEFAULT_FOLDER)
    if git_tracks(out):
        print("refusing: git would track %s; use an ignored LicensedMusic/AccelHop folder" % out, file=sys.stderr)
        return 2
    if not shutil.which("ffmpeg"):
        print("ffmpeg is needed (https://ffmpeg.org)", file=sys.stderr)
        return 2
    if args.bpm is not None and not MIN_BPM <= args.bpm <= MAX_BPM:
        print("--bpm must be %g..%g" % (MIN_BPM, MAX_BPM), file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)
    for song in args.songs:
        src = pathlib.Path(song)
        named = parse_bpm_from_name(src.stem)
        meta = {"title": named[1] if named and named[1] else src.stem}
        if args.bpm:
            meta["bpm"] = args.bpm
        elif named:
            meta["bpm"] = named[0]
        if args.offset_ms is not None:
            meta["offset_ms"] = args.offset_ms
        dst = out / (src.stem + ".wav")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(src), "-vn", "-ac", "2", "-ar", "48000",
                        "-c:a", "pcm_s16le", str(dst)], check=True)
        if "bpm" not in meta or (args.analyse and args.offset_ms is None):
            if not args.analyse:
                print("%s: no BPM; pass --bpm or --analyse" % src.name, file=sys.stderr)
                dst.unlink()
                continue
            sys.path.insert(0, str(ROOT / "Tools"))
            import beatmap_from_audio  # needs numpy + librosa
            found = beatmap_from_audio.analyse(str(dst))
            meta.setdefault("bpm", found["bpm"])
            meta.setdefault("offset_ms", found["offset_ms"])
            print("%s: analysed %.2f BPM, offset %d ms, confidence %.2f" % (src.name, found["bpm"], found["offset_ms"], found["confidence"]))
        dst.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
        print("added  %s  %s" % (dst.name, describe(dst)[1]))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    check = sub.add_parser("check")
    check.add_argument("folder", nargs="?")
    add = sub.add_parser("add")
    add.add_argument("songs", nargs="+")
    add.add_argument("--out")
    add.add_argument("--bpm", type=float)
    add.add_argument("--offset-ms", type=float)
    add.add_argument("--analyse", action="store_true")
    args = parser.parse_args(argv)
    return cmd_check(args) if args.cmd == "check" else cmd_add(args)


if __name__ == "__main__":
    sys.exit(main())
