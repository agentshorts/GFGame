"""Beat map for a Beat Hop song from its audio: tempo, first-beat offset and Easy/Normal/Hard charts.

Run (needs numpy + librosa; no Unreal):
  python Tools/beatmap_from_audio.py <song.wav> --song BeatHopFallback --title "Beat Hop Fallback" [--out Content/Charts]
Writes <out>/<Song>_<Difficulty>.json in the beatmap format (format 1, the one FGFBeatmapIO reads and
GFBuiltInSongs.cpp embeds) and prints the analysis as JSON.

Tempo: librosa's beat tracker seeds it, then a constant grid (BPM and phase) is fitted to the onset envelope.
Confidence = share of the tracker's beats within 25 ms of that grid; also reported: eighth-note onsets on the grid
and the on-beat / quarter-off-beat onset contrast. The charts follow the onset strength of every eighth: the strong
eighths of each bar get the extra Normal taps and the Hard lane switches. Notes start on beat 4 (one bar of lead-in,
like the built-in songs) and stop at the last audible beat.
"""
import argparse, json, os
import numpy as np
import librosa

HOP = 128


def analyse(path):
    y, sr = librosa.load(path, sr=None, mono=True)
    dur = len(y) / sr
    env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=HOP)
    tempo, beats = librosa.beat.beat_track(onset_envelope=env, sr=sr, hop_length=HOP, units="time")
    seed = float(np.atleast_1d(tempo)[0])
    w = max(1, int(0.010 * sr / HOP))

    def peak(t):
        i = int(round(t * sr / HOP))
        return float(env[max(0, i - w):i + w + 1].max()) if 0 <= i < len(env) else 0.0

    def score(bpm, phase):
        return float(np.mean([peak(t) for t in np.arange(phase, dur, 60.0 / bpm)]))

    best = (-1.0, seed, 0.0)
    for bpm in np.arange(seed - 2.0, seed + 2.0, 0.01):
        for ph in np.arange(0.0, 60.0 / bpm, 0.002):
            s = score(bpm, ph)
            if s > best[0]:
                best = (s, bpm, ph)
    _, bpm, phase = best
    # A whole BPM within 0.02 is the producer's tempo (the fit can't tell 130.00 from 130.01 over half a minute).
    if abs(bpm - round(bpm)) <= 0.02:
        bpm = float(round(bpm))
        phase = max((0.0, 0.0), *[(score(bpm, p), p) for p in np.arange(0.0, 60.0 / bpm, 0.001)])[1]
    period = 60.0 / bpm
    grid = np.arange(phase, dur, period)
    near = [float(np.min(np.abs(grid - b))) for b in beats]
    onsets = librosa.onset.onset_detect(onset_envelope=env, sr=sr, hop_length=HOP, units="time")
    eighths = np.arange(phase, dur, period / 2)
    on8 = sum(1 for o in onsets if np.min(np.abs(eighths - o)) <= 0.025)
    rms = librosa.feature.rms(y=y, hop_length=HOP)[0]
    loud = np.where(rms > rms.max() * 0.02)[0]
    audible_end = float(librosa.frames_to_time(loud[-1], sr=sr, hop_length=HOP))
    strength = [peak(phase + i * period / 2) for i in range(int((audible_end - phase) / (period / 2)) + 1)]
    return {
        "file": os.path.basename(path), "sample_rate": sr, "duration_s": round(dur, 3),
        "bpm": round(bpm, 2), "tracker_bpm": round(seed, 2), "offset_ms": int(round(phase * 1000)),
        "confidence": round(sum(1 for d in near if d <= 0.025) / max(1, len(near)), 3),
        "tracker_beats": len(beats), "beat_error_ms": round(1000 * float(np.mean(near)), 1),
        "eighth_onsets_on_grid": round(on8 / max(1, len(onsets)), 3),
        "onbeat_offbeat_contrast": round(score(bpm, phase) / max(1e-6, score(bpm, phase + period / 4)), 1),
        "audible_end_s": round(audible_end, 3), "audible_beats": int((audible_end - phase) / period + 0.5),
        "eighth_strength": strength,
    }


def charts(a, song, title):
    bars = a["audible_beats"] // 4
    st = np.array(a["eighth_strength"] + [0.0] * 8)
    # Strong eighths per bar position, averaged over every full bar (bar 0 is the lead-in).
    prof = np.mean([st[b * 8:b * 8 + 8] for b in range(1, bars)], axis=0)
    prof2 = [np.mean([st[b * 8:b * 8 + 8] for b in range(1, bars) if b % 2 == k], axis=0) for k in (0, 1)]
    top = prof.max()
    off = [p for p in (1, 3, 5, 7) if prof[p] >= 0.55 * top]          # syncopated eighths that hit hard
    bar_end = lambda b: [p for p in (6,) if prof2[b % 2][p] >= 0.55 * top]  # beat 4, only in the bars that play it
    head = {"format": 1, "song": song, "title": title, "bpm": int(a["bpm"]) if float(a["bpm"]).is_integer() else a["bpm"],
            "offset_ms": a["offset_ms"], "ref_speed_cms": 600}
    walk4 = [2, 2, 1, 1, 2, 3, 3, 2, 1, 0, 1, 2, 3, 2, 1, 2]  # lane per bar (bar 1 starts on lane 2, like the built-ins)
    out = {}

    def note(beat, lane, kind, length=0.0):
        n = {"beat": float(beat), "lane": lane, "type": kind}
        if kind == "Hold":
            n["len_beats"] = float(length)
        return n

    # Easy: every beat, one lane per bar.
    notes = [note(b * 4 + k, walk4[b % len(walk4)], "Tap") for b in range(1, bars) for k in range(4)]
    out["Easy"] = dict(head, difficulty="Easy", lanes=4, notes=notes)

    # Normal: the beats plus the strong off-beats; Pad + Gap on the strong off-beat every fourth bar.
    notes = []
    for b in range(1, bars):
        lane, base = walk4[b % len(walk4)], b * 4
        if b % 4 == 2:
            notes += [note(base, lane, "Tap"), note(base + 1, lane, "Tap")]
            if 3 in off:
                notes.append(note(base + 1.5, lane, "Tap"))
            notes += [note(base + 2.5, lane, "Pad"), note(base + 3.5, lane, "Gap")]
            continue
        pos = sorted(set([0, 2, 4] + off + bar_end(b)))
        notes += [note(base + p / 2, lane, "Tap") for p in pos if p != 4 or 5 not in off]
    out["Normal"] = dict(head, difficulty="Normal", lanes=4, notes=notes)

    # Hard: five lanes, a lane switch on the first strong off-beat of each bar, holds into the phrase starts.
    notes, lane, step = [], 2, 1
    for b in range(1, bars):
        base = b * 4
        if b % 4 == 1:
            notes.append(note(base, lane, "Hold", 1.0))
        else:
            notes += [note(base, lane, "Tap"), note(base + 1, lane, "Tap")]
        if not 0 <= lane + step <= 4:
            step = -step
        lane += step
        notes.append(note(base + 1.5, lane, "Switch"))
        if b % 8 == 6:
            notes += [note(base + 2.5, lane, "Pad"), note(base + 3.5, lane, "Gap")]
            continue
        notes += [note(base + p / 2, lane, "Tap") for p in sorted(set([4] + off + bar_end(b))) if p > 3]
    out["Hard"] = dict(head, difficulty="Hard", lanes=5, notes=notes)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--song", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Content", "Charts"))
    args = ap.parse_args()
    a = analyse(args.wav)
    os.makedirs(args.out, exist_ok=True)
    for diff, chart in charts(a, args.song, args.title).items():
        order = ["format", "song", "title", "difficulty", "bpm", "offset_ms", "lanes", "ref_speed_cms", "notes"]
        text = json.dumps({k: chart[k] for k in order}, separators=(",", ":"))
        with open(os.path.join(args.out, "{}_{}.json".format(args.song, diff)), "w", encoding="utf-8", newline="\n") as f:
            f.write(text + "\n")
    a.pop("eighth_strength")
    print(json.dumps(a, indent=1))


if __name__ == "__main__":
    main()
