#pragma once

#include "CoreMinimal.h"
#include "GFBeatmap.h"

/** The songs that ship inside the game (3 songs x Easy/Normal/Hard). Co-op runs may only use these. */
struct GFRHYTHM_API FGFBuiltInSongs
{
	/** Every built-in chart, parsed once. */
	static const TArray<FGFBeatmap>& All();
	static const FGFBeatmap* Find(FName SongId, EGFBeatDifficulty Difficulty);
	static TArray<FName> SongIds();
	/** How many charts are embedded (All() should parse every one of them). */
	static int32 NumEmbeddedCharts();
};
