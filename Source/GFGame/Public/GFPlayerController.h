#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "GFElder.h"
#include "GFElderTalk.h"
#include "GFTheftRules.h"
#include "GFPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UVoxelFinishPainterComponent;

/** Thin controller. Movement input lives on AGFCharacter. Enter, Esc, H and Home drive AGFHUD until Common UI. F starts a Shardhop. Home returns to the hub. */
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

	/**
	 * Fallback home. The key and the pause menu both call this.
	 * A live round asks for a confirm first. The server moves only this pawn.
	 */
	void RequestReturnToHub(bool bConfirmed);
	void RequestReturnFromMenu(bool bConfirmed);
	void ShowReturnConfirm(const FString& Prompt);
	void CancelReturnConfirm();
	/** Clears the prompt text. The modal itself changes only through the HUD setter. */
	void ClearReturnPrompt();
	bool IsReturnConfirmOpen() const;
	const FString& GetReturnConfirmPrompt() const { return ReturnPrompt; }

	/**
	 * Server. Copies this player's theft heat for the spotted banner.
	 * The director remains the clock. A remote client receives the snapshot.
	 */
	void NotifyTheftHeat(EGFTheftHeat State, float HuntLeft);
	EGFTheftHeat GetShownHeat() const { return ShownHeat; }
	float GetShownHuntLeft() const { return ShownHuntLeft; }

	/** Server. A validated elder interact. Opens the menu on the owning client. */
	void OpenElderMenu(AGFElder* Elder);
	void RequestCloseElderMenu();
	void RequestElderAccept();
	void RequestElderHandIn();
	/** Side line buttons (QL-b). Separate from the errand Accept and Hand in. */
	void RequestElderSideAccept();
	void RequestElderSideHandIn();
	void RequestElderTrade(FName RowId);
	bool IsElderMenuSessionOpen() const { return ElderPhase == EGFElderSession::Open; }
	void ShowElderToast(const FString& Text);

	/** Home, unless someone rebound it. H stays the controls hint. */
	static FKey DefaultReturnKey() { return EKeys::Home; }
	FKey GetReturnKey() const { return ReturnKey.IsValid() ? ReturnKey : DefaultReturnKey(); }
	void SetReturnKey(FKey Key);

	UFUNCTION(Server, Reliable)
	void ServerRequestShardhop(FName DestId);

	UFUNCTION(Server, Reliable)
	void ServerRequestReturnToHub(bool bConfirmed, bool bFromMenu);

	UFUNCTION(Client, Reliable)
	void ClientPromptReturnConfirm(const FString& Prompt);

	UFUNCTION(Client, Reliable)
	void ClientSetTheftHeat(uint8 State, float HuntLeft);

	UFUNCTION(Client, Reliable)
	void ClientReturnFinished();

	UFUNCTION(Client, Reliable)
	void ClientOpenElderMenu(AGFElder* Elder, FName TownId);

	UFUNCTION(Client, Reliable)
	void ClientElderToast(const FString& Text);

	UFUNCTION(Server, Reliable)
	void ServerCloseElderMenu();

	UFUNCTION(Server, Reliable)
	void ServerElderAccept();

	UFUNCTION(Server, Reliable)
	void ServerElderHandIn();

	UFUNCTION(Server, Reliable)
	void ServerElderSideAccept();

	UFUNCTION(Server, Reliable)
	void ServerElderSideHandIn();

	UFUNCTION(Server, Reliable)
	void ServerElderTrade(FName RowId, int32 RequestId);

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
	void HandleReturnKey();
	void HandleInventoryKey();
	void HandleSlot1();
	void HandleSlot2();
	void HandleSlot3();
	void HandleSlot4();
	void HandleSlot5();
	void HandleSlot6();
	void HandleSlot7();
	void HandleSlot8();
	void HandleSlot9();
	/** Number key KeyIndex (0 is the 1 key): a class pick while the choice is open, else a hotbar slot. */
	void SendSlotKey(int32 KeyIndex);
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
	TObjectPtr<UInputAction> ReturnAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InventoryAction;

	FKey ReturnKey;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> SlotActions;

	FTimerHandle ShardhopTimer;
	bool bShardhopActive = false;
	FName ShardhopDest;
	double ShardhopStartedAt = 0.0;
	int32 ShardhopSerial = 0;

	bool bHudActionsBound = false;
	bool bHudMappingAdded = false;
	bool bChatOpenQueued = false;
	FString ReturnPrompt;

	EGFElderSession ElderPhase = EGFElderSession::Closed;
	TWeakObjectPtr<AGFElder> ElderSession;
	/** Server. New for every menu open, so trade ids from an earlier session or controller never collide. */
	FGuid ElderSessionId;
	int32 ElderRequestSerial = 0;

	void SubmitReturn(bool bConfirmed, bool bFromMenu);
	bool ElderMenuAllows(FString& OutWhy) const;
	void ApplyShownHeat(EGFTheftHeat State, float HuntLeft);

	/** Display copy of FGFTheftHeat. Not a second clock. */
	EGFTheftHeat ShownHeat = EGFTheftHeat::Clear;
	float ShownHuntLeft = 0.f;
};
