#pragma once

#include "CoreMinimal.h"
#include "GFBeatmap.h"
#include "GFRhythmJudge.h"

/**
 * The songs that ship inside the game, each Easy/Normal/Hard: three greybox songs that play to a metronome, and the
 * fallback track (real audio, FGFBeatHopMusic). Co-op runs may only use these.
 */
struct GFRHYTHM_API FGFBuiltInSongs
{
	/** Every built-in chart, parsed once. */
	static const TArray<FGFBeatmap>& All();
	static const FGFBeatmap* Find(FName SongId, EGFBeatDifficulty Difficulty);
	static TArray<FName> SongIds();
	/** How many charts are embedded (All() should parse every one of them). */
	static int32 NumEmbeddedCharts();
};

/**
 * Which music a Beat Hop run plays to. The fallback track is an original song with its own SoundWave and beat map
 * (Content/Charts/BeatHopFallback_<Difficulty>.json, embedded in GFBuiltInSongs.cpp): it plays when there is no music
 * to follow (live mode can't open an output device, or hears only silence), and sessions open on it for demos.
 */
struct GFRHYTHM_API FGFBeatHopMusic
{
	static FName FallbackSongId();

	/** SoundWave object path for a built-in song, or empty when the song plays to the metronome only. */
	static FString SoundPathFor(FName SongId);

	/**
	 * The start-flow rule. Songs mode plays the chosen song (the fallback when none is chosen). Live mode stays live
	 * (returns NAME_None) only with a music source that isn't silent; otherwise it plays the fallback song.
	 */
	static FName PickSong(EGFBeatHopMode Mode, FName Requested, bool bHasMusicSource, bool bMusicSilent);

	/** The song a session opens on: the fallback for demos, otherwise the first built-in song. */
	static FName DefaultSong(bool bDemo);
};
