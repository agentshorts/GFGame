# Local music for Acceleration Hop

Acceleration Hop can play your own music instead of the built-in Beat Hop song (issue 343). The music stays on
your machine. This repo is public, so music is never committed, cooked or shipped.

## Where the files go

The game uses the first of these folders that holds a playable track:

1. The folder given on the command line: `-AccelHopMusicDir="D:/Music/Hop"`
2. `LicensedMusic/AccelHop` in the folder the game was launched from
3. `LicensedMusic/AccelHop` in the project folder (in a packaged build, the `GFGame/` folder next to the exe)
4. `Content/LicensedMusic/AccelHop` in an editor checkout

`/LicensedMusic/` and `/Content/LicensedMusic/` are in `.gitignore`, and `/Game/LicensedMusic` is in
`DirectoriesToNeverCook`. `Tools/test_hop_music.py` fails if git ever tracks a file there.

Tip: the editor may offer to import a .wav dropped under `Content/`. Say no, or use the project-root
`LicensedMusic/AccelHop` folder instead. Either way nothing there is tracked.

## What a track needs

* A `.wav` file: 8, 16, 24 or 32-bit PCM, or 32-bit float, mono or stereo, 8 to 192 kHz. Unreal has no runtime
  mp3 or ogg decoder without a plugin, so other formats are listed as skipped.
* A BPM between 40 and 300, from either:
  * a sidecar JSON with the same name: `Night Drive.json` with `{"bpm": 128, "offset_ms": 40, "title": "Night Drive"}`.
    `offset_ms` is where beat 0 falls in the audio, and `title` is the name shown in the playlist. Both are optional.
  * the file name: `Night Drive 128bpm.wav`, `[128 BPM] Night Drive.wav`, `night_drive_bpm-128.wav` or
    `Track 92.5 BPM.wav`.

  The sidecar wins when both are present. A track with no BPM is skipped, and the log says why.

## The prep tool

```
python3 Tools/hop_music_prep.py check                          # what the game will do with each file
python3 Tools/hop_music_prep.py add song.mp3 --bpm 128         # convert to .wav and write the sidecar
python3 Tools/hop_music_prep.py add song.flac --analyse        # find the BPM and offset (numpy + librosa)
```

`add` needs ffmpeg. It writes into `Content/LicensedMusic/AccelHop` by default, and refuses any folder git would track.

## The playlist (Esc, Settings, "Beat Hop playlist")

* Every track found appears as a row: a tick box to include it, and Up and Down to reorder.
* Play modes: **In Order** (wraps around), **Shuffle** (plays every track once before any repeat, and never the
  same track twice in a row when there are two or more), **Repeat One**.
* **Resume** (on by default): leave the road and come back, and the same track carries on where you left it. Off:
  the next track starts each time you enter a road. A track that was within 2 seconds of its end counts as finished.
* **Rescan folder** picks up new files without a restart. The folder is also rescanned every time you step onto a road.
* No tracks, or every track unticked: the built-in song plays as before.

Settings are saved in this player's `GameUserSettings.ini` (`[/Script/GFRhythm.GFRhythmSettings]`,
`AccelHopPlaylist`). They are never replicated. In co-op each player hears their own playlist, and their hops are
judged on their own track's BPM. The server only checks the boost against the beat length each client sends.

## The lit tile and your tempo

The lit target tile (issue 342) is the same for everyone and does not follow anyone's music. The server picks it a
few hops ahead of the lead player on its own steady grid at the road song's committed tempo (130 BPM for the fallback
song), even on a listen host that is playing a local track. Your own track sets only your beat feel, the tile pulses
and your HUD timing. Land on the lit tile on your own beat and you get the lit boost, however far your beat sits from
the server's grid or anyone else's. `GFGame.HopMusic.LitPickIgnoresClientBpm` pins this down.
