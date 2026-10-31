#pragma once

// Client entry for a ferry trip or a lifetime pass. UI prompts come later.
// The server work lives on AGFFerryDirector.
// This header is public so GFTests can call BeginFlightProtection. GFCharacter.h only forward-declares the type.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

class USceneComponent;

#include "GFFerryRiderComponent.generated.h"

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGAME_API UGFFerryRiderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFFerryRiderComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Puts the pawn's feet on the basket floor and matches the basket's rotation.
	 * The actor root is the capsule centre, so the seat point is lifted by the capsule half-height.
	 * Safe to call again: an already-seated pawn is only moved when it has drifted.
	 */
	static void SeatOnBasket(APawn* Rider, USceneComponent* Basket);

	UFUNCTION(Server, Reliable)
	void ServerRequestTrip(FName FromStop, FGuid RequestId);

	UFUNCTION(Server, Reliable)
	void ServerBuyPass(FName AtStop, FGuid RequestId);

	UFUNCTION(BlueprintCallable, Category = "GF|Ferry")
	void RequestTrip(FName FromStop);

	UFUNCTION(BlueprintCallable, Category = "GF|Ferry")
	void BuyPass(FName AtStop);

	UFUNCTION(BlueprintPure, Category = "GF|Ferry")
	bool IsInFlight() const { return bInFlight; }

	UFUNCTION(BlueprintPure, Category = "GF|Ferry")
	FName GetLastCode() const { return LastCode; }

	UFUNCTION(BlueprintPure, Category = "GF|Ferry")
	FName GetSeatAnim() const { return SeatAnim; }

	/** Server: mark this pawn as a rider. Adds GF.FactionProtected and turns damage off. */
	void BeginFlightProtection();

	/** Server: clear the flight flag and restore damage. */
	void EndFlightProtection();

	/**
	 * Server: play Player.Emote_Sit, and turn movement and collision off.
	 * SeatAnim replicates, so clients apply the same lock.
	 */
	void BeginSeat();

	/** Server: drop the sit emote and give movement and collision back. */
	void EndSeat();

	void SetLastCode(FName Code) { LastCode = Code; }

	UFUNCTION()
	void OnRep_SeatAnim();

private:
	void ApplySeatPose();
	void ApplySeatLock();

	bool bRestoreCollision = false;
	bool bRestoreMovement = false;

	UPROPERTY(Replicated)
	bool bInFlight = false;

	UPROPERTY(Replicated)
	FName LastCode;

	UPROPERTY(ReplicatedUsing = OnRep_SeatAnim, VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry", meta = (AllowPrivateAccess = "true"))
	FName SeatAnim;

	bool bFlightProtected = false;
	bool bAddedProtectionTag = false;
	bool bDamageWasEnabled = true;
};
