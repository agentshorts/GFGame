#pragma once
// Acceleration Hop music from a local, git-ignored folder (issue 343).
//
// The player drops their own .wav files into LicensedMusic/AccelHop (next to the packaged game, or under Content/ in
// an editor checkout). Each track needs a BPM, from a sidecar "<name>.json" ({"bpm":128,"offset_ms":40,"title":"..."})
// or from the file name ("Night Drive 128bpm.wav", "[128 BPM] Night Drive.wav"). The road plays them through the
// player's Beat Hop playlist (Esc, Settings), and the beat clock follows each track's own BPM. With no usable track,
// or every track excluded, the road plays the committed Beat Hop fallback song exactly as before.
//
// Everything here is local to one machine: the folder, the playlist and the resume point are this player's, saved in
// GameUserSettings.ini (UGFRhythmSettings), and never replicated. Each client hops to its own music; the server only
// checks the boost (UGFCharacterMovementComponent), and the client already sends its own beat length with each hop.
//
// No music file is ever committed: /Content/LicensedMusic/ and /LicensedMusic/ are in .gitignore, /Game/LicensedMusic
// is never cooked, and Tools/test_hop_music.py fails if git tracks anything under either folder.
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "GFHopMusic.generated.h"

/** How the playlist moves on when a track ends (and, with Resume off, when the player comes back to the road). */
UENUM(BlueprintType)
enum class EGFHopPlayMode : uint8
{
	InOrder,
	Shuffle,
	RepeatOne
};

/** One row of the saved playlist: a track (by file name) and whether it plays. The array order is the play order. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFHopPlaylistEntry
{
	GENERATED_BODY()

	/** File name inside the music folder, e.g. "Night Drive 128bpm.wav". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	FString Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	bool bIncluded = true;
};

/** This player's Beat Hop playlist, saved locally (UGFRhythmSettings::AccelHopPlaylist). Never replicated. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFHopPlaylistPrefs
{
	GENERATED_BODY()

	/** Every track ever found, in play order. A track whose file is gone keeps its row, so it comes back in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	TArray<FGFHopPlaylistEntry> Entries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	EGFHopPlayMode Mode = EGFHopPlayMode::InOrder;

	/** On: coming back to the road continues the last track where it stopped. Off: the next track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	bool bResume = true;

	/** The track that was playing when the player last left the road (or the last one started). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	FString LastKey;

	/** Where LastKey stopped, seconds into its audio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|AccelHop")
	float ResumeSec = 0.f;
};

/** A usable track in the music folder. */
struct GFRHYTHM_API FGFHopTrack
{
	FString Key;
	FString Path;
	FString Title;
	float Bpm = 0.f;
	/** Beat 0 from the start of the audio, ms (sidecar "offset_ms"; 0 from a file name). */
	float OffsetMs = 0.f;
	/** From the WAV header; 0 when unknown. */
	float DurationSec = 0.f;
	/** "sidecar" or "name". */
	FString BpmFrom;
};

/** What a folder scan found: the usable tracks (sorted by file name) and why the rest were skipped. */
struct GFRHYTHM_API FGFHopLibraryScan
{
	FString Folder;
	TArray<FGFHopTrack> Tracks;
	/** "<file>: <reason>" for every audio file that cannot play (no BPM, not a .wav, unreadable header). */
	TArray<FString> Skipped;

	TArray<FString> Keys() const;
	const FGFHopTrack* Find(const FString& Key) const;
};

/** Format of a WAV file, read from its header. */
struct GFRHYTHM_API FGFWavInfo
{
	int32 Channels = 0;
	int32 SampleRate = 0;
	int32 BitsPerSample = 0;
	/** 1 = integer PCM, 3 = IEEE float (WAVE_FORMAT_EXTENSIBLE is resolved to one of these). */
	int32 Format = 0;
	/** Byte offset of the sample data and its declared size (may run past a truncated buffer). */
	int64 DataOffset = 0;
	int64 DataBytes = 0;

	int32 BytesPerFrame() const { return Channels * (BitsPerSample / 8); }
	double DurationSec() const;
};

/** A WAV decoded to interleaved 16-bit PCM, ready for a USoundWaveProcedural. */
struct GFRHYTHM_API FGFWavPcm
{
	int32 Channels = 0;
	int32 SampleRate = 0;
	TArray<int16> Samples;

	int64 NumFrames() const { return Channels > 0 ? Samples.Num() / Channels : 0; }
	double DurationSec() const { return SampleRate > 0 ? static_cast<double>(NumFrames()) / SampleRate : 0.0; }
};

/** The pure part of the local music folder: names, sidecars, WAV parsing, the scan. Unit tested. */
struct GFRHYTHM_API FGFHopMusicLibrary
{
	/** Folder name under each candidate root. */
	static const TCHAR* const SubFolder;
	/** Tempo range a track may claim (the beat clock clamps to 30..300; hops need a sane beat). */
	static constexpr float MinBpm = 40.f;
	static constexpr float MaxBpm = 300.f;

	/**
	 * Where to look, first match wins: -AccelHopMusicDir=<dir> on the command line, then LicensedMusic/AccelHop next
	 * to the launched exe, under the project folder (a packaged build's GFGame/ folder), and under Content/.
	 */
	static TArray<FString> CandidateFolders();

	/** "Night Drive 128bpm", "[128 BPM] Night Drive", "night_drive_bpm-128", "Track 92.5 BPM". 40..300 only. */
	static bool ParseBpmFromName(const FString& BaseName, float& OutBpm, FString* OutTitle = nullptr);

	/** Reads {"bpm":..,"offset_ms":..,"title":..} into Track. False (Track untouched) without a valid bpm. */
	static bool ApplySidecarJson(const FString& Json, FGFHopTrack& Track);

	/** Parses RIFF/WAVE chunks up to the data chunk. Works on a header-only prefix of the file. Mono or stereo only. */
	static bool ReadWavInfo(const TArray<uint8>& Bytes, FGFWavInfo& Out, FString& OutError);

	/** Decodes 8/16/24/32-bit PCM or 32-bit float WAV to 16-bit PCM. */
	static bool DecodeWav(const TArray<uint8>& Bytes, FGFWavPcm& Out, FString& OutError);

	/** One folder: usable .wav tracks sorted by file name, the rest listed in Skipped. Missing folder = empty scan. */
	static FGFHopLibraryScan ScanFolder(const FString& Folder);

	/** The first candidate folder with at least one usable track (an empty scan of the first folder otherwise). */
	static FGFHopLibraryScan ScanFirstUsable(const TArray<FString>& Folders);

	/** A tiny click-track WAV (16-bit mono), so tests and tools never need a real music file. */
	static TArray<uint8> MakeTestWav(int32 SampleRate, float Seconds, float Bpm);
};

/** Shuffle memory for one session: tracks not yet played this round. */
struct GFRHYTHM_API FGFHopShuffleBag
{
	TArray<FString> Remaining;
};

/** What the road should play next. Key empty = no local track: play the committed fallback song. */
struct GFRHYTHM_API FGFHopChoice
{
	FString Key;
	float StartSec = 0.f;
	bool bResumed = false;

	bool IsLocal() const { return !Key.IsEmpty(); }
};

/** The pure playlist rules. Unit tested; FGFHopMusicDirector wires them to the folder and the saved settings. */
struct GFRHYTHM_API FGFHopPlaylistRules
{
	/** A resume point this close to the end of the track counts as finished: the next track starts instead. */
	static constexpr float ResumeTailSec = 2.f;

	/** Adds a row (included) for every found track that has none, at the end, in file name order. True if it changed. */
	static bool Sync(FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found);

	/** Included rows whose file is present, in playlist order. */
	static TArray<FString> ActiveKeys(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found);

	/**
	 * The track after Current. In Order: the next one, wrapping. Repeat One: Current again (the first if Current is
	 * gone). Shuffle: a random one from the bag, never Current while there are two or more, and every track once before
	 * any repeats. Empty Active gives "".
	 */
	static FString PickNext(const TArray<FString>& Active, EGFHopPlayMode Mode, const FString& Current, FGFHopShuffleBag& Bag, FRandomStream& Rng);

	/**
	 * Entering the road. No active track: "" (fallback). Resume on and the last track still active with time left:
	 * that track at ResumeSec. Otherwise the next track after LastKey (the first, or a random one in Shuffle, when
	 * nothing has played yet). LastDurationSec <= 0 means unknown, so any resume point is trusted.
	 */
	static FGFHopChoice ChooseOnEnter(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Active, float LastDurationSec, FGFHopShuffleBag& Bag, FRandomStream& Rng);

	/** A track ended on the road: the next one from the start. */
	static FGFHopChoice ChooseAfterEnd(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Active, const FString& Finished, FGFHopShuffleBag& Bag, FRandomStream& Rng);

	/** Remember where the player left a track (Resume reads it). */
	static void NoteStopped(FGFHopPlaylistPrefs& Prefs, const FString& Key, float PositionSec);

	static void SetIncluded(FGFHopPlaylistPrefs& Prefs, const FString& Key, bool bIncluded);

	/** Moves Key one place up (-1) or down (+1) among the found tracks; rows for missing files stay where they are. */
	static bool MoveAmongFound(FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found, const FString& Key, int32 Delta);

	/** Found tracks in playlist order (included or not): what the settings list shows. */
	static TArray<FString> OrderedFound(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found);
};

/**
 * One machine's hop music state: the last scan, the shuffle bag and the saved prefs. Owned by UGFHopMusicSubsystem
 * (one per game instance); a road without a game instance (tests) keeps its own.
 */
class GFRHYTHM_API FGFHopMusicDirector
{
public:
	FGFHopMusicDirector();

	/** Look only here (tests, tools). Empty = the normal candidate folders. Clears the last scan. */
	void SetFolderOverride(const FString& Folder);

	/** Use this prefs object instead of the user's saved settings, and never write the ini (tests). Null = settings. */
	void SetPrefsOverride(FGFHopPlaylistPrefs* InPrefs);

	void SetSeed(int32 Seed) { Rng.Initialize(Seed); }

	/** Scans the folder again and adds rows for new tracks. */
	const FGFHopLibraryScan& Rescan();
	const FGFHopLibraryScan& GetScan() const { return Scan; }
	bool HasScanned() const { return bScanned; }

	FGFHopPlaylistPrefs& Prefs();
	/** Writes GameUserSettings.ini (not with a prefs override). */
	void SavePrefs();

	TArray<FString> ActiveKeys();
	const FGFHopTrack* FindTrack(const FString& Key) const { return Scan.Find(Key); }

	/** Rescans, then picks what to play on entering the road. */
	FGFHopChoice ChooseOnEnter();
	FGFHopChoice ChooseAfterEnd(const FString& Finished);

	/** The road started Key at StartSec / left the road with Key at PositionSec. Both save the resume point. */
	void NoteStarted(const FString& Key, float StartSec);
	void NoteStopped(const FString& Key, float PositionSec);

private:
	FString FolderOverride;
	FGFHopPlaylistPrefs* PrefsOverride = nullptr;
	FGFHopLibraryScan Scan;
	bool bScanned = false;
	FGFHopShuffleBag Bag;
	FRandomStream Rng;
};
