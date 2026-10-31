#pragma once

// Playable Pebble Mine on the hub sim pad (#216). The hub gate already drops a player on this pad.
// This actor builds a few rooms and the verb spots (Capture, Assign, Haul, Recall, Buy, Status)
// out of engine cubes, and a return arch back to the Rime stand. Dig rates stay in the room table.

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GFModeTypes.h"
#include "GFMineSite.generated.h"

class UFont;
class UMaterialInterface;
class UStaticMesh;
class AGFPlayerState;
class APawn;

UCLASS()
class GFMINE_API AGFMineSite : public AActor
{
	GENERATED_BODY()

public:
	AGFMineSite();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Rooms, spots and labels. Safe to call again. Authority and clients both build their own meshes. */
	void BuildIfNeeded();

	static AGFMineSite* Get(UWorld* World);

	/** HUD radius (cm). The pad is 40 m across; the Rime stand is much farther than this. */
	bool IsPlayerNear(const APawn* Pawn) const;

	/** Where the return arch puts a player: the Rime anchor, plus the no-PlayerStart stand height. */
	FVector GetHubReturnLocation() const;

	int32 NumRooms() const { return RoomIds.Num(); }
	bool HasVerb(FName Verb) const;
	UBoxComponent* GetVerbTrigger(FName Verb) const;
	UBoxComponent* GetRoomTrigger(FName Room) const;

	/** Server. Each one calls AGFPlayerState::RequestMode with Mode "Mine". */
	FGFModeResult TryCapture(APawn* Pawn);
	FGFModeResult TryAssign(APawn* Pawn, FName Room);
	FGFModeResult TryHaul(APawn* Pawn);
	FGFModeResult TryRecall(APawn* Pawn);
	FGFModeResult TryBuy(APawn* Pawn);
	FGFModeResult TryStatus(APawn* Pawn);
	/** Server. Teleports a player pawn back to the hub stand. Not a mode verb. */
	bool TryReturn(APawn* Pawn);

	/**
	 * Lines for the HUD. Always names Capture, Assign, Haul, Recall, Buy and Status.
	 * Crew text comes from the player state (replicated to its owner). LastPrompt is the last verb result.
	 */
	void GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const;

	/** Last verb result, replicated so a client HUD can show it. */
	UPROPERTY(Replicated)
	FString LastPrompt;

	static constexpr float NearRadiusCm = 4500.f;
	/** Matches the no-PlayerStart hub stand (FGFPlayerStartRules::FallbackZ). */
	static constexpr float HubStandZCm = 120.f;

protected:
	UFUNCTION()
	void OnRep_Ready();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(ReplicatedUsing = OnRep_Ready)
	bool bReady = false;

	bool bVisualsBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UFont> SignFont;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> Triggers;

	TArray<FName> TriggerVerbs;
	TArray<FName> TriggerRooms;
	TArray<FName> RoomIds;

	/** Player keys who already got the one-time buy shortfall. Not a drop. */
	TSet<FString> ForemanAdvance;

	struct FStay
	{
		bool bSentFast = false;
		bool bDone = false;
		double NextSlow = 0.0;
	};
	TMap<TWeakObjectPtr<APawn>, FStay> CaptureStays;
	/** Pawns already handled for this stay, one set per trigger. */
	TArray<TSet<TWeakObjectPtr<APawn>>> SpotInside;

	void BuildVisuals();
	void AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock);
	void AddLabel(const FString& Text, const FVector& Local);
	UBoxComponent* AddTrigger(FName Verb, FName Room, const FVector& Local, const FVector& Extent);
	void AddRoom(FName RoomId, const FString& Title, const FVector& Center, const FLinearColor& Color);
	void PollSpots();
	AGFPlayerState* PlayerOf(APawn* Pawn) const;
	/** Name None and an invalid id are left off the request. Capture sends Speed. */
	FGFModeResult Send(AGFPlayerState* PS, FName Verb, FName Name, const FGuid& Id, float Speed);
	FName NextBuyId(const AGFPlayerState* PS) const;
	bool GrantShortfall(AGFPlayerState* PS, FName Upgrade);
};
