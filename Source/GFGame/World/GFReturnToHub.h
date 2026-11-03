#pragma once

// One way home. A return gate stands where you arrive in a mode, and the same
// rules serve the fallback key and the pause menu. The server moves one pawn.
// A remote client on a mode map is sent alone, so the host stays put.

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GFReturnToHub.generated.h"

class APawn;
class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
struct FGFMiniGameEntry;

enum class EGFReturnSeat : uint8
{
	Standalone,
	ListenHost,
	RemoteClient
};

enum class EGFReturnRoute : uint8
{
	Teleport,
	ClientTravel,
	ServerTravel
};

struct GFGAME_API FGFReturnResult
{
	bool bMoved = false;
	bool bNeedsConfirm = false;
	FName Forfeit = NAME_None;
	FString Prompt;
	FString TravelMap;
	bool bClientTravel = false;
	bool bServerTravel = false;
	FVector Stand = FVector::ZeroVector;
};

struct GFGAME_API FGFReturnToHub
{
	static constexpr float NearGateCm = 800.f;

	static FVector HubStand(const FVector& RimeCenter);

	/** Open areas you start or walk in (the world map, the towns). Their gate and fallback key travel to the hub map. */
	static const TArray<FName>& AreaMaps();
	static bool IsAreaMap(const FString& WorldPackage);
	static EGFReturnRoute RouteFor(bool bMapTravel, EGFReturnSeat Seat);
	static EGFReturnSeat SeatOf(const APawn* Pawn);

	/** Lab keeps 75 percent. Every other live round is a forfeit. */
	static const TCHAR* LabPrompt();
	static const TCHAR* RoundPrompt();

	static FGFReturnResult Execute(UWorld* World, APawn* Pawn, bool bConfirmed, const class AGFReturnGate* Gate = nullptr, const EGFReturnSeat* SeatOverride = nullptr);
	static FGFReturnResult FromKey(UWorld* World, APawn* Pawn, bool bConfirmed, const class AGFReturnGate* Gate = nullptr, const EGFReturnSeat* SeatOverride = nullptr);
	static FGFReturnResult FromMenu(UWorld* World, APawn* Pawn, bool bConfirmed, const class AGFReturnGate* Gate = nullptr, const EGFReturnSeat* SeatOverride = nullptr);
};

UCLASS()
class GFGAME_API AGFReturnGate : public AActor
{
	GENERATED_BODY()

public:
	AGFReturnGate();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Configure(FName InModeId, const FVector& InRimeCenter, bool bInMapTravel);
	void ApplyVisuals();

	/** Server. A live round asks first, unless bConfirmed. */
	FGFReturnResult TryReturn(APawn* Pawn, bool bConfirmed);

	FName GetModeId() const { return ModeId; }
	FVector GetRimeCenter() const { return RimeCenter; }
	bool IsMapTravel() const { return bMapTravel; }
	FString GetSignText() const { return SignText; }

	static AGFReturnGate* Find(const UWorld* World, FName InModeId);
	static AGFReturnGate* Nearest(const UWorld* World, const FVector& At, float Radius = FGFReturnToHub::NearGateCm);

	/** Beside the mode-map marker the hub director already drops at (-500, 0, 0). */
	static FVector MapLocation();
	static FVector PadLocation(const FGFMiniGameEntry& Entry, const FVector& RimeCenter);

	/** Area maps: AreaBehindCm behind the player start on the ground, so a fresh spawn walks away from it, not into it. */
	static FVector AreaLocation(const FTransform& PlayerStart);
	static FName AreaModeId() { return TEXT("Area"); }

	static constexpr float BesideCm = 400.f;
	static constexpr float AreaBehindCm = 800.f;
	static constexpr float StartHeightCm = 120.f;

private:
	UFUNCTION()
	void OnRep_Gate();

	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> LabelText;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	FName ModeId;

	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	FVector RimeCenter = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	bool bMapTravel = false;

	FString SignText;
};
