#pragma once

// Client entry for a ferry trip or a lifetime pass. UI prompts come later.
// The server work lives on AGFFerryDirector.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFFerryRiderComponent.generated.h"

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGAME_API UGFFerryRiderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFFerryRiderComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	/** Server: remember Player.Emote_Sit and play it when the pawn has a mesh. */
	void BeginSeat();

	void EndSeat();

	void SetLastCode(FName Code) { LastCode = Code; }

private:
	UPROPERTY(Replicated)
	bool bInFlight = false;

	UPROPERTY(Replicated)
	FName LastCode;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Ferry", meta = (AllowPrivateAccess = "true"))
	FName SeatAnim;

	bool bFlightProtected = false;
	bool bAddedProtectionTag = false;
	bool bDamageWasEnabled = true;
};
