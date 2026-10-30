#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GFPlayerController.generated.h"

class UVoxelFinishPainterComponent;

/** Thin controller; input lives on AGFCharacter, UI (Common UI) arrives in M1. */
UCLASS()
class GFGAME_API AGFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGFPlayerController();

	/** Block paint (VoxelFinish). Server checks: 600 cm reach, 10 paints per second, solid blocks only (AGFPaintVolume). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Paint")
	TObjectPtr<UVoxelFinishPainterComponent> Painter;

	UVoxelFinishPainterComponent* GetPainter() const { return Painter; }

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_PlayerState() override;
};
