#pragma once

// Side quest world adapters (QL-b). Turn things that already happen in the world into FGFSideQuestEvents:
// town tree breaks, a member taking a town crystal, crystal drops, Accel Hop landings, climb mantles at tagged lips,
// and ferry arrivals. Server only. Each adapter only raises events; the quest subsystem decides what they move.
// Nothing here hires, opens theft cases, or changes heat.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFAccelHop.h"
#include "GFRhythmJudge.h"
#include "GFQuestTypes.h"
#include "GFSideQuestTypes.h"
#include "GFSideQuestAdapters.generated.h"

class AActor;
class APawn;
class UGFCharacterMovementComponent;
class UGFQuestSubsystem;
struct FGFFerryStop;

/** A climb lip from config: TownId plus an offset from that town's territory center. For maps with no tagged lip yet. */
USTRUCT(BlueprintType)
struct GFGAME_API FGFQuestClimbLipConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "GF|Quest")
	FName TownId;

	UPROPERTY(EditAnywhere, Category = "GF|Quest")
	FVector Offset = FVector::ZeroVector;
};

/** One known climb lip in this world. */
struct GFGAME_API FGFQuestClimbLip
{
	FName TownId;
	FVector Location = FVector::ZeroVector;
};

/** Pure rules the adapters use. Tests call these directly. */
struct GFGAME_API FGFSideQuestAdapterRules
{
	/** Actor tag prefix for a climb lip. The rest of the tag is the town id, for example GFQuestClimb_Cinderwright. */
	static const TCHAR* ClimbTagPrefix();
	/** Town id from the first tag with the prefix (None when no tag has it, or the suffix is empty). */
	static FName ClimbTownFromTags(const TArray<FName>& Tags);
	/** Town of the nearest lip within RadiusCm of At (None when no lip is close enough). */
	static FName ClimbTownAt(const TArray<FGFQuestClimbLip>& Lips, const FVector& At, float RadiusCm);

	/** A hop counts when it was applied and judged Good or Perfect. */
	static bool HopCounts(EGFJudgement Judgement, bool bApplied);
	/** Perfect 2, Good 1, else 0. Goes in the event Score. */
	static int32 HopScore(EGFJudgement Judgement);
	/** Item id for a hop event, so a step's Items can ask for one tile kind (Standard, Downbeat, Boost, Landing). */
	static FName HopTileId(EGFHopTileKind Kind);

	/** Member or better of TownId on the errand. */
	static bool IsTownMember(FName FactionId, EGFFactionRank Rank, FName TownId);
	/** The item id a crystal drop carries. */
	static FName CrystalItemId();
};

UCLASS(Config = Game)
class GFGAME_API UGFSideQuestAdapters : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGFSideQuestAdapters* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Lips used when the map has no actor tagged GFQuestClimb_<TownId> for a town (offset from the town anchor). DefaultGame.ini ships Verdant and Rime. */
	UPROPERTY(Config, EditAnywhere, Category = "GF|Quest")
	TArray<FGFQuestClimbLipConfig> ClimbLipFallbacks;

	/** A mantle ending this close to a lip counts for that lip's town. */
	UPROPERTY(Config, EditAnywhere, Category = "GF|Quest")
	float ClimbLipRadiusCm = 400.f;

	// Keyed credit. Each returns how many events it raised. The world handlers below call these.

	/** TreeBroken for every key, when Location is inside a town. */
	int32 CreditTree(const TArray<FString>& Keys, const FVector& Location);
	/** CrystalTaken for every key that is a member of the vein's town. Outsiders get nothing here (the errand handles them). */
	int32 CreditCrystalTaken(const TArray<FString>& Keys, const FVector& Location);
	/** CrystalGathered for a crystal drop. TownId is the town at From (None in the wild, which any line accepts). */
	int32 CreditDrop(const FString& Key, FName ItemId, int32 Count, const FVector& From);
	/** HopLanded for a counted hop. ItemId is the tile kind, Score the grade. */
	int32 CreditHop(const FString& Key, EGFJudgement Judgement, EGFHopTileKind Kind, bool bApplied);
	/** ClimbMantled when Lip is near a known climb lip. */
	int32 CreditMantle(const FString& Key, const FVector& Lip);
	/** FerryArrived. TownId is the departure stop's anchor, ItemId the arrival stop id. */
	int32 CreditFerry(const FString& Key, FName FromAnchorId, FName ToStopId);

	/** Rebuilds the lip list from tagged actors, then fallbacks for towns with no tagged lip. Runs at BeginPlay. */
	void RefreshClimbLips();
	const TArray<FGFQuestClimbLip>& GetClimbLips() const { return ClimbLips; }

private:
	bool IsServerWorld(const UWorld* World) const;
	UGFQuestSubsystem* Quest() const;
	void RaiseSide(const FGFSideQuestEvent& Event) const;

	void HandleTreeBroken(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& Keys);
	void HandleCrystalTaken(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& Keys);
	void HandleDrop(UWorld* World, const FString& Key, FName ItemId, int32 Count, FVector From);
	void HandleHop(const UGFCharacterMovementComponent* Movement, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, bool bApplied);
	void HandleMantle(const UGFCharacterMovementComponent* Movement, FVector Lip);
	void HandleFerry(UWorld* World, APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To);

	static FString KeyOf(const APawn* Pawn);

	TArray<FGFQuestClimbLip> ClimbLips;

	FDelegateHandle TreeHandle;
	FDelegateHandle CrystalHandle;
	FDelegateHandle DropHandle;
	FDelegateHandle HopHandle;
	FDelegateHandle MantleHandle;
	FDelegateHandle FerryHandle;
};
