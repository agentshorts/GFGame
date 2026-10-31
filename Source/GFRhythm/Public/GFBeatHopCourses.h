#pragma once

// Beat Hop courses (#114). A win is passing the course (the finish), not a grade.
// Fern is the plain chart. Rime is skiddy ice with a chasing revenge dino.
// Cinder is lava with a revenge dino that knocks you off. A hired Ripwing catches a fall.
// Skid and shoves are extra velocity. They do not retune walk speed, gravity or air control.

#include "CoreMinimal.h"

enum class EGFBeatHopCourseHazard : uint8
{
	None,
	Ice,
	Lava
};

enum class EGFBeatHopRevengeRole : uint8
{
	None,
	/** Closes in from behind and shoulders you sideways. Ice. */
	Chase,
	/** Waits on the course and swats you off a brick once. Lava. */
	KnockOff
};

/** One authored course. Layout numbers are applied on top of the song chart. Fern is the identity. */
struct GFRHYTHM_API FGFBeatHopCourseDef
{
	FName Id;
	FString Title;
	/** 0 is the easiest. Later courses are strictly harder. */
	int32 Order = 0;
	EGFBeatHopCourseHazard Hazard = EGFBeatHopCourseHazard::None;
	EGFBeatHopRevengeRole Revenge = EGFBeatHopRevengeRole::None;
	FName RevengeSpecies;
	/** 0 keeps the chart's lane. Otherwise the lane nudges by WeaveLanes, flipping every WeaveEvery bricks. */
	int32 WeaveEvery = 0;
	int32 WeaveLanes = 0;
	/** 1 keeps the chart's X. Above 1 stretches the gaps so the same hop covers less of them. */
	float GapScale = 1.f;
	/** Sideways drift (cm/s) added while this course is running. 0 leaves velocity alone. */
	float SkidCmPerSec = 0.f;
	FLinearColor Tint = FLinearColor::White;

	bool IsSkiddy() const { return SkidCmPerSec > 0.f; }
	/** Higher means a nastier course. Used to check the catalog is actually ordered. */
	int32 DifficultyScore() const;
};

struct GFRHYTHM_API FGFBeatHopCourses
{
	static const TArray<FGFBeatHopCourseDef>& All();
	static const FGFBeatHopCourseDef& Default();
	static const FGFBeatHopCourseDef* Find(FName Id);

	/**
	 * Chart X/Y (cm) to course X/Y. BrickIndex counts bricks, not gaps.
	 * Fern returns the chart point unchanged.
	 */
	static FVector2D Place(const FGFBeatHopCourseDef& Course, int32 BrickIndex, double ChartX, double ChartY, float LaneWidthCm);

	/**
	 * Adds sideways drift along Right, capped at SkidCmPerSec.
	 * The returned vector is the only change. Walk speed, gravity and air control are not inputs.
	 */
	static FVector ApplySkid(const FVector& Velocity, const FVector& Right, float SkidCmPerSec, float DeltaSeconds);
};

/** A hired Ripwing. Cost is GlowDust, burned once per player per course visit. */
struct GFRHYTHM_API FGFBeatHopRescueRules
{
	static FName Species();
	static FName CostItem();
	static constexpr int32 Cost = 8;
	static constexpr int32 Catches = 3;
};

/** Who has a rescue dino, and how many catches it has left. No ledger here. */
struct GFRHYTHM_API FGFBeatHopRescueBook
{
	struct FHire
	{
		FString PlayerKey;
		int32 CatchesLeft = 0;
	};

	const FHire* Find(const FString& PlayerKey) const;
	/** False when the key is empty, Catches is 0, or this player is already hired. */
	bool Hire(const FString& PlayerKey, int32 Catches);
	/** False when this player has no catch left. */
	bool SpendCatch(const FString& PlayerKey);
	void Reset();

	TArray<FHire> Hires;
};

/** One revenge dino on a course. Positions are course-local cm. */
struct GFRHYTHM_API FGFBeatHopRevengeState
{
	FVector Local = FVector::ZeroVector;
	bool bSpawned = false;
	int32 Knocks = 0;

	struct FStep
	{
		bool bChasing = false;
		bool bKnocked = false;
		/** Course-local shove (cm/s) added to the runner. Zero when the dino only moved. */
		FVector Push = FVector::ZeroVector;
	};

	/**
	 * StationX is the lava dino's post (course X). RunnerX is where a chase starts behind.
	 * A course with no revenge role stays despawned.
	 */
	void Reset(const FGFBeatHopCourseDef& Course, float StationX, float RunnerX);

	/** Moves the dino. A knock fires once per player key. */
	FStep Step(const FString& PlayerKey, const FVector& RunnerLocal, float DeltaSeconds, const FGFBeatHopCourseDef& Course);

private:
	TSet<FString> Knocked;
};
