#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFAnimMap.generated.h"

class UAnimSequenceBase;

/**
 * Row for DT_AnimMap (Data/CSV/DT_AnimMap.csv). Row name = gameplay animation key, e.g. Player.Run,
 * Player.Emote_Cheer. Gameplay code asks for a key, never a clip path, so an emote can reuse a clip
 * (Emote_Cheer and Emote_Wave play the artist's Cheer and Wave clips) and new art only changes this row.
 */
USTRUCT(BlueprintType)
struct GFCORE_API FGFAnimMapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim") TSoftObjectPtr<UAnimSequenceBase> Anim;
	/** Loops until stopped (Idle, Walk, Emote_Sit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim") bool bLoop = false;
	/** One-shot that holds its last frame instead of blending out (Cheer ends in a V pose). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim") bool bHoldLastFrame = false;
	/**
	 * cm, along Z. Where a clip leaves a prop in the world (Plant_Flag drives the claim flag into the ground and lets go
	 * after its impact frame), the gameplay code that detaches the prop adds this to its world Z so it ends up planted.
	 * Plant_Flag: -10 (the pole foot only reaches the ground in the clip). 0 for clips that leave nothing behind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anim") float PlantedPropOffsetZ = 0.f;
};

struct GFCORE_API FGFAnimMap
{
	static const FGFAnimMapRow* Find(FName Key);
	/** Loads the clip for Key, or nullptr if the key is unknown or the clip isn't imported. */
	static UAnimSequenceBase* Load(FName Key);
};
