#pragma once

// Beat Hop beatmaps (PLAN.md M5): one chart per song x difficulty, stored as JSON so agents and people can diff them.
// Pure data + parsing, no world, so charts can be validated in headless tests.
#include "CoreMinimal.h"
#include "GFBeatmap.generated.h"

/** What a chart note asks the player to do. */
UENUM(BlueprintType)
enum class EGFBeatNoteType : uint8
{
	/** Land on the brick on its beat. */
	Tap,
	/** A strip of bricks (LenBeats long): land on its first beat. */
	Hold,
	/** No brick: be airborne across it. Never judged. */
	Gap,
	/** Launch brick for a big hop over the next empty beats. */
	Pad,
	/** Lane change, marked by an arrow glow. Judged like a Tap. */
	Switch
};

UENUM(BlueprintType)
enum class EGFBeatDifficulty : uint8
{
	Easy,
	Normal,
	Hard
};

USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFBeatNote
{
	GENERATED_BODY()

	/** Beat number from the chart's first beat (offset_ms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	double Beat = 0.0;

	/** 0-based lane, left to right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	int32 Lane = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	EGFBeatNoteType Type = EGFBeatNoteType::Tap;

	/** Hold length in beats (Hold only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float LenBeats = 0.f;

	/** True for notes that put a brick on the course (everything but Gap). */
	bool HasBrick() const { return Type != EGFBeatNoteType::Gap; }
	/** True for notes the judge scores. */
	bool IsJudged() const { return Type != EGFBeatNoteType::Gap; }
};

/** One chart: a song at one difficulty. */
USTRUCT(BlueprintType)
struct GFRHYTHM_API FGFBeatmap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	FName SongId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	EGFBeatDifficulty Difficulty = EGFBeatDifficulty::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float Bpm = 120.f;

	/** Time of beat 0 from the start of the song audio, ms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float OffsetMs = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	int32 Lanes = 4;

	/** The course is laid out at this run speed: beat time x speed = distance along the track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	float RefSpeedCms = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Rhythm")
	TArray<FGFBeatNote> Notes;

	double SecondsPerBeat() const { return Bpm > 0.f ? 60.0 / Bpm : 0.0; }
	/** Song time (s) of a beat, including the chart offset. */
	double BeatToSeconds(double Beat) const { return OffsetMs / 1000.0 + Beat * SecondsPerBeat(); }
	double SecondsToBeat(double Seconds) const { return Bpm > 0.f ? (Seconds - OffsetMs / 1000.0) * Bpm / 60.0 : 0.0; }
	/** Distance (cm) along the track of a beat. */
	double BeatToDistanceCm(double Beat) const { return Beat * SecondsPerBeat() * RefSpeedCms; }
	/** Last beat covered by any note (hold ends included). */
	double GetLastBeat() const;
	int32 CountJudgedNotes() const;
};

/** JSON in/out and validation for beatmaps. */
struct GFRHYTHM_API FGFBeatmapIO
{
	/** Current "format" value written by ToJson. Files without a format field are read as version 1. */
	static constexpr int32 FormatVersion = 1;
	/** Default longest jump between two bricks at the chart's reference speed (cm); a Pad allows PadHopScale times this. */
	static constexpr float DefaultMaxHopCm = 900.f;
	static constexpr float PadHopScale = 2.5f;

	static bool FromJson(const FString& Json, FGFBeatmap& Out, FString& OutError);
	static FString ToJson(const FGFBeatmap& Map);
	static bool LoadFile(const FString& Path, FGFBeatmap& Out, FString& OutError);

	/** Every problem found (empty = valid): tempo/lanes in range, notes sorted, lanes in range, Holds have length, jumps possible. */
	static TArray<FString> Validate(const FGFBeatmap& Map, float MaxHopCm = DefaultMaxHopCm);

	static const TCHAR* DifficultyToString(EGFBeatDifficulty D);
	static bool DifficultyFromString(const FString& S, EGFBeatDifficulty& Out);
	static const TCHAR* NoteTypeToString(EGFBeatNoteType T);
	static bool NoteTypeFromString(const FString& S, EGFBeatNoteType& Out);
};
