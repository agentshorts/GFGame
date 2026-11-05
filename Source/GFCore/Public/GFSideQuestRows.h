#pragma once

// CSV rows for the side quest board. New files only: DT_Quests.csv stays the errand's table.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFQuestTypes.h"
#include "GFSideQuestTypes.h"
#include "GFSideQuestRows.generated.h"

/** DT_QuestLines.csv. Row name is the LineId. One row per side line. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSideQuestLineRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FName TownId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FString Title;
	/** Default Member. ASK CANON: whether Champion should be required instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") EGFFactionRank UnlockRank = EGFFactionRank::Member;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") int32 Sort = 0;
};

/** DT_QuestSteps.csv. Row name is LineId_StepId (any unique name works). One row per step. */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSideQuestStepRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FName LineId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FName StepId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") int32 Order = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FString Title;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") FString Hint;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") int32 Goal = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") TArray<FName> Items;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Quest") EGFSideQuestEventKind AdvanceKind = EGFSideQuestEventKind::TalkAccepted;
};
