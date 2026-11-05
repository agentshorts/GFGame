"""Checks for Acceleration Hop local music (issue 343). No Unreal editor needed.

Run from the repo root:

  python3 -m unittest Tools/test_hop_music.py

Guards the never-commit-music rule (git ignores both music folders, tracks no audio there, and the cook skips
them) and keeps Tools/hop_music_prep.py's BPM-from-name parser in step with the C++ one by reading the C++ test
vectors straight out of GFHopMusicTests.cpp. Every WAV here is generated in a temp folder.
"""
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Tools"))
import hop_music_prep as prep  # noqa: E402

CPP_TESTS = ROOT / "Source" / "GFRhythm" / "Private" / "Tests" / "GFHopMusicTests.cpp"
AUDIO_EXT = {"wav", "mp3", "ogg", "flac", "m4a", "aac", "opus", "wma", "aif", "aiff"}


def _wav(channels=1, rate=8000, bits=16, fmt=1, frames=800, extensible=False):
    align = channels * bits // 8
    body = bytes(frames * align)
    fmt_chunk = struct.pack("<HHIIHH", 0xFFFE if extensible else fmt, channels, rate, rate * align, align, bits)
    if extensible:
        fmt_chunk += struct.pack("<HHIH", 22, bits, 0, fmt) + bytes(14)
    chunks = b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk
    chunks += b"LIST" + struct.pack("<I", 5) + b"abcde\x00"
    chunks += b"data" + struct.pack("<I", len(body)) + body
    return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


def _git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)


class NeverCommitMusic(unittest.TestCase):
    def setUp(self):
        if _git("rev-parse", "--git-dir").returncode != 0:
            self.skipTest("not a git checkout")

    def test_both_music_folders_are_ignored(self):
        for rel in ("Content/LicensedMusic/AccelHop/Song 128bpm.wav", "Content/LicensedMusic/AccelHop/Song 128bpm.json",
                    "LicensedMusic/AccelHop/Song.wav", "Content/LicensedMusic/AccelHop/Song.uasset"):
            self.assertEqual(_git("check-ignore", "-q", rel).returncode, 0, rel + " must be git-ignored")

    def test_no_audio_tracked_under_licensed_music(self):
        tracked = _git("ls-files", "--", "Content/LicensedMusic", "LicensedMusic").stdout.split()
        self.assertEqual(tracked, [])

    def test_no_audio_files_tracked_outside_the_known_folders(self):
        # Committed game audio lives as imported .uasset files; raw audio would be a licensing slip.
        raw = [p for p in _git("ls-files").stdout.splitlines()
               if p.rsplit(".", 1)[-1].lower() in AUDIO_EXT and not p.startswith(("Content/Audio/", "Tools/"))]
        self.assertEqual(raw, [])

    def test_cook_never_packages_licensed_music(self):
        ini = (ROOT / "Config" / "DefaultGame.ini").read_text(encoding="utf-8")
        section = ini.split("[/Script/UnrealEd.ProjectPackagingSettings]", 1)[1].split("\n[", 1)[0]
        self.assertIn('+DirectoriesToNeverCook=(Path="/Game/LicensedMusic")', section)

    def test_prep_tool_refuses_a_tracked_folder(self):
        self.assertTrue(prep.git_tracks(ROOT / "Content" / "Audio"))
        self.assertFalse(prep.git_tracks(ROOT / "Content" / "LicensedMusic" / "AccelHop"))
        self.assertFalse(prep.git_tracks(ROOT / "LicensedMusic" / "AccelHop"))


class BpmFromName(unittest.TestCase):
    def cpp_cases(self):
        text = CPP_TESTS.read_text(encoding="utf-8")
        found = re.findall(r'\{ TEXT\("([^"]*)"\), (true|false), ([\d.]+)f, (?:TEXT\("([^"]*)"\)|nullptr) \}', text)
        self.assertGreaterEqual(len(found), 10, "C++ BPM vectors not found")
        return [(name, ok == "true", float(bpm), title) for name, ok, bpm, title in found]

    def test_matches_the_cpp_vectors(self):
        for name, ok, bpm, title in self.cpp_cases():
            got = prep.parse_bpm_from_name(name)
            self.assertEqual(got is not None, ok, name)
            if ok:
                self.assertAlmostEqual(got[0], bpm, places=2, msg=name)
                self.assertEqual(got[1], title, name)

    def test_more_names(self):
        self.assertEqual(prep.parse_bpm_from_name("BPM=95 Rain"), (95.0, "Rain"))
        self.assertEqual(prep.parse_bpm_from_name("Song (174 BPM)"), (174.0, "Song"))
        self.assertIsNone(prep.parse_bpm_from_name("bpm"))
        self.assertIsNone(prep.parse_bpm_from_name("Version 1.2.3 bpm"))


class WavHeaders(unittest.TestCase):
    def test_formats_the_game_plays(self):
        self.assertEqual(prep.read_wav_info(_wav())["data_bytes"], 1600)
        self.assertEqual(prep.read_wav_info(_wav(channels=2, bits=24, rate=44100))["bits"], 24)
        self.assertEqual(prep.read_wav_info(_wav(bits=32, fmt=3, extensible=True))["format"], 3)
        self.assertEqual(prep.read_wav_info(_wav(bits=8))["channels"], 1)

    def test_formats_the_game_refuses(self):
        for bad in (_wav(channels=6), _wav(fmt=2, bits=4), _wav(rate=4000), b"ID3\x04\x00\x00", b""):
            with self.assertRaises(ValueError):
                prep.read_wav_info(bad)


class CheckFolder(unittest.TestCase):
    def test_describe_matches_the_game_rules(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            (d / "B Side 140bpm.wav").write_bytes(_wav(frames=8000 * 3))
            (d / "A Side.wav").write_bytes(_wav())
            (d / "A Side.json").write_text(json.dumps({"bpm": 100, "title": "Alpha"}))
            (d / "C 90bpm.wav").write_bytes(_wav())
            (d / "C 90bpm.json").write_text('{"bpm": 120}')
            (d / "Mystery.wav").write_bytes(_wav())
            (d / "Song 120bpm.mp3").write_bytes(b"ID3")
            (d / "notes.txt").write_text("hello")
            self.assertEqual(prep.describe(d / "B Side 140bpm.wav")[0], "plays")
            self.assertIn("3.0 s", prep.describe(d / "B Side 140bpm.wav")[1])
            self.assertIn("Alpha: 100 BPM (sidecar)", prep.describe(d / "A Side.wav")[1])
            self.assertIn("120 BPM (sidecar)", prep.describe(d / "C 90bpm.wav")[1])
            self.assertEqual(prep.describe(d / "Mystery.wav")[0], "skipped")
            self.assertEqual(prep.describe(d / "Song 120bpm.mp3")[0], "skipped")
            self.assertEqual(prep.describe(d / "notes.txt")[0], "ignored")
            self.assertEqual(prep.main(["check", str(d)]), 0)

    def test_missing_folder_means_the_built_in_song(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(prep.main(["check", str(pathlib.Path(tmp) / "nope")]), 0)


if __name__ == "__main__":
    unittest.main()
