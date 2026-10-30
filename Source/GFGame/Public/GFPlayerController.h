#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UVoxelFinishPainterComponent;

/** Thin controller. Movement input lives on AGFCharacter. Enter, Esc and H drive AGFHUD until Common UI. */
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
	virtual void SetupInputComponent() override;

	/** Enter, Esc and H. Kept off AGFCharacter so that file stays untouched. */
	void EnsureHudInput();
	void HandleChatKey();
	void HandleHintKey();
	void HandleCloseChatKey();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> HudMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ChatAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CloseChatAction;

	bool bHudActionsBound = false;
	bool bHudMappingAdded = false;
	bool bChatOpenQueued = false;
};
