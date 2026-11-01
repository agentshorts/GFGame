#pragma once

// Hand-shaped Beat Hop courses (#278). Layout, song pairing, obstacles and par live in Data/BeatHop/Maps.
// The session on L_Beat spawns them. Free Hop stays the chart-generated course.

#include "CoreMinimal.h"
#include "GFBeatmap.h"
#include "GFBeatHopNinja.h"

class AGFBeatHopCourse;
class UWorld;

/** One authored beat. hop, lane, pad and gap become chart notes. The rest become ninja obstacles. */
struct GFRHYTHM_API FGFBeatHopMapPiece
{
	FString Kind;
	double Beat = 0.0;
	int32 Lane = 0;
	float HeightCm = 0.f;
};

/** A pictogram section. StartBeat is where its sign stands. */
struct GFRHYTHM_API FGFBeatHopMapSection
{
	FString Id;
	FString Teach;
	double StartBeat = 0.0;
};

/** One course file. Every tunable the run reads is on this struct, filled from JSON. */
struct GFRHYTHM_API FGFBeatHopMapDef
{
	FName Id;
	int32 Number = 0;
	FString Title;
	/** Pips, windows and the difficulty tile. */
	EGFBeatDifficulty DisplayDifficulty = EGFBeatDifficulty::Easy;
	/** What Allows() sees. Course 2 is Normal on the sign and Hard here so swivel poles can clear. */
	EGFBeatDifficulty JudgeDifficulty = EGFBeatDifficulty::Easy;
	FName DefaultSong;
	float BpmMin = 80.f;
	float BpmMax = 140.f;
	float ReferenceBpm = 120.f;
	int32 ParScore = 0;
	float BrickWidthScale = 1.f;
	float PerfectWindowMs = 50.f;
	float GoodWindowMs = 110.f;
	int32 CheckpointEveryBeats = 32;
	/** When false, mantle and ledge meshes stay beside the lane and a pad hop is the way across. */
	bool bMantleClimb = false;
	int32 Lanes = 4;
	float RefSpeedCms = 600.f;
	FLinearColor Tint = FLinearColor::White;
	TArray<FGFBeatHopMapSection> Sections;
	TArray<FGFBeatHopMapPiece> Pieces;
};

/** Bricks plus obstacles for one BPM. SafeBrickIndex is filled by the course after the bricks exist. */
struct GFRHYTHM_API FGFBeatHopMapBuilt
{
	FGFBeatmap Beatmap;
	TArray<FGFBeatHopObstacleInstance> Obstacles;
};

namespace GFBeatHopMaps
{
	/** The chart-generated course on the map row. Not a hand-shaped file. */
	GFRHYTHM_API FName FreeHopId();

	GFRHYTHM_API const TArray<FGFBeatHopMapDef>& All();
	GFRHYTHM_API const FGFBeatHopMapDef* Find(FName Id);
	GFRHYTHM_API const FGFBeatHopMapDef* ByNumber(int32 Number);
	GFRHYTHM_API bool IsHandShaped(FName Id);

	/** hop and pad share a variety key. gap is empty (it is not a move). */
	GFRHYTHM_API FString VarietyKey(const FString& Kind);

	/** Pieces except gaps. */
	GFRHYTHM_API int32 ObstacleCount(const FGFBeatHopMapDef& Map);
	GFRHYTHM_API int32 MoveVariety(const FGFBeatHopMapDef& Map);
	GFRHYTHM_API float AverageWindowMs(const FGFBeatHopMapDef& Map);
	/** 1000 / average window. Tighter windows raise this, so course 1 < 2 < 3. */
	GFRHYTHM_API float TimingDemand(const FGFBeatHopMapDef& Map);
	GFRHYTHM_API FString Describe(const FGFBeatHopMapDef& Map);

	/** Inclusive BPM range. OutMessage is the line the lobby shows when this returns false. */
	GFRHYTHM_API bool Fits(const FGFBeatHopMapDef& Map, float Bpm, FString& OutMessage);

	/** Notes and obstacles at Bpm. Spacing is beat time at that BPM times RefSpeedCms. */
	GFRHYTHM_API bool Build(const FGFBeatHopMapDef& Map, float Bpm, FGFBeatHopMapBuilt& Out, FString& OutError);

	/** Best score for one course and one player key. Memory, plus Saved/BeatHop/LocalBests.json. */
	GFRHYTHM_API int32 BestScore(FName MapId, const FString& PlayerKey);
	/** Keeps the higher score. */
	GFRHYTHM_API void NoteBest(FName MapId, const FString& PlayerKey, int32 Score);

	/** GFGame registers this so the rhythm module never includes a game header. */
	using FLayoutHook = void (*)(AGFBeatHopCourse* Course);
	GFRHYTHM_API void SetLayoutHook(FLayoutHook Hook);
	GFRHYTHM_API void NotifyLayout(AGFBeatHopCourse* Course);

	/**
	 * Dev builds: select course 1, 2 or 3 on the Beat Hop session in World.
	 * Shipping builds return "Dev builds only." and do not change the session.
	 */
	GFRHYTHM_API FString LoadCourse(UWorld* World, int32 Number);
}
