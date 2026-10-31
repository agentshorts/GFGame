#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UVoxelFinishPainterComponent;

/** Thin controller. Movement input lives on AGFCharacter. Enter, Esc and H drive AGFHUD until Common UI. F starts a Shardhop. */
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

	/** Local call. On the server it starts the channel directly. F uses PickDestination. */
	void RequestShardhop(FName DestId);

	UFUNCTION(Server, Reliable)
	void ServerRequestShardhop(FName DestId);

	UFUNCTION(Server, Reliable)
	void ServerToggleVillain();

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupInputComponent() override;

	void EnsureHudInput();
	void HandleChatKey();
	void HandleHintKey();
	void HandleCloseChatKey();
	void HandleShardhopKey();
	void HandleVillainKey();
	void HandlePath1();
	void HandlePath2();
	void HandlePath3();
	void HandlePath4();
	void HandlePath5();
	void SendPathSlot(int32 Slot);
	void TickShardhop();
	void FinishShardhop(FName DestId);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> HudMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ChatAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CloseChatAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ShardhopAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> VillainAction;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> PathActions;

	FTimerHandle ShardhopTimer;
	bool bShardhopActive = false;
	FName ShardhopDest;
	double ShardhopStartedAt = 0.0;
	int32 ShardhopSerial = 0;

	bool bHudActionsBound = false;
	bool bHudMappingAdded = false;
	bool bChatOpenQueued = false;
};
