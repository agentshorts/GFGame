#pragma once

// Pure transition table for The Elder's Errand. Apply is the only way the step changes.
// Legal pairs:
//   TakeCrystal: CrystalTaken -> Escape (that town is heat), ElderAccepted -> HuntHide when that town is not heat
//   Escape:      HeatCleared -> TalkToElder once no heat is left, ElderAccepted -> HuntHide when that town is not heat
//   TalkToElder: ElderAccepted -> HuntHide
//   HuntHide:    TurnIn of 6 Hide or Scales, any mix
//   HuntSkins:   TurnIn of 2 skins from the joined town
//   CraftLook:   OutfitCrafted, then OutfitWorn
//   WinShow:     ShowEntered, then ShowPlaced at 1st and tier A or better -> Done, rank Leader
//   Done:        nothing
// ElderAccepted from TakeCrystal or Escape skips TalkToElder. That is the only skip.
#include "CoreMinimal.h"
#include "GFQuestTypes.h"

class UWorld;
class UDataTable;

DECLARE_MULTICAST_DELEGATE_TwoParams(FGFOnQuestRaised, UWorld*, const FGFQuestEvent&);

/** Place 1 and tier A. Tier numbers match EGFRunwayTier: C = 0, B = 1, A = 2, S = 3. */
struct GFCORE_API FGFQuestShowRule
{
	static constexpr int32 WinPlace = 1;
	static constexpr int32 WinTier = 2;
};

struct GFCORE_API FGFQuestStepRule
{
	EGFQuestStep Step = EGFQuestStep::TakeCrystal;
	int32 Goal = 0;
	EGFFactionRank Rank = EGFFactionRank::Outsider;
	TArray<FName> Items;
};

struct GFCORE_API FGFQuestTownSkins
{
	FName TownId;
	int32 Goal = 2;
	TArray<FName> Items;
};

/** Goals, accepted items, ranks, and each town's skins. Builtin matches DT_Quests.csv. Load() prefers the CSV. */
struct GFCORE_API FGFQuestCatalog
{
	TArray<FGFQuestStepRule> Steps;
	TArray<FGFQuestTownSkins> Towns;

	static const FGFQuestCatalog& Builtin();
	static FGFQuestCatalog Load();
	static FGFQuestCatalog FromTable(const UDataTable* Table);
	static bool TryStep(FName Id, EGFQuestStep& Out);
	static FName StepId(EGFQuestStep Step);

	int32 GoalFor(EGFQuestStep Step) const;
	TArray<FName> ItemsFor(EGFQuestStep Step) const;
	EGFFactionRank RankAfter(EGFQuestStep Step) const;
	TArray<FName> SkinsFor(FName TownId) const;
	const FGFQuestStepRule* FindStep(EGFQuestStep Step) const;
	const FGFQuestTownSkins* FindTown(FName TownId) const;
};

/**
 * One player's line. Catalog null uses FGFQuestCatalog::Builtin().
 * Apply never crashes: unknown or out of order events come back Ignored and leave Progress as it was.
 */
struct GFCORE_API FGFQuestLine
{
	FGFQuestProgress Progress;
	const FGFQuestCatalog* Catalog = nullptr;

	FGFQuestApplyResult Apply(const FGFQuestEvent& Event);

private:
	const FGFQuestCatalog& ActiveRules() const;
	FGFQuestApplyResult ApplyCrystal(const FGFQuestEvent& Event);
	FGFQuestApplyResult ApplyHeatCleared(const FGFQuestEvent& Event);
	FGFQuestApplyResult ApplyAccept(const FGFQuestEvent& Event);
	FGFQuestApplyResult ApplyTurnIn(const FGFQuestEvent& Event);
	FGFQuestApplyResult ApplyCraft(const FGFQuestEvent& Event);
	FGFQuestApplyResult ApplyShow(const FGFQuestEvent& Event);
};

/** Static multicast. Raise from anywhere. The quest subsystem on that world is the listener. */
struct GFCORE_API FGFQuestEvents
{
	static FGFOnQuestRaised& OnRaised();
	static void Raise(UWorld* World, const FGFQuestEvent& Event);

	static FGFQuestEvent CrystalTaken(const FString& PlayerKey, FName TownId);
	static FGFQuestEvent HeatCleared(const FString& PlayerKey, FName TownId = NAME_None);
	static FGFQuestEvent ElderAccepted(const FString& PlayerKey, FName TownId);
	static FGFQuestEvent TurnIn(const FString& PlayerKey, const TArray<FGFItemStack>& Offer);
	static FGFQuestEvent TurnIn(const FString& PlayerKey, FName ItemId, int32 Count);
	static FGFQuestEvent OutfitCrafted(const FString& PlayerKey, FName ItemId);
	static FGFQuestEvent OutfitWorn(const FString& PlayerKey, FName ItemId);
	static FGFQuestEvent ShowEntered(const FString& PlayerKey);
	static FGFQuestEvent ShowPlaced(const FString& PlayerKey, int32 Place, int32 Tier);
};
