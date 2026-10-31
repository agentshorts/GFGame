#pragma once

// One mini-game gate. Portals stand on the ring around the Rime hub.
// Markers stand on a mode map, or on a sim pad, and show that mode's name.
// Missing gate art falls back to a tinted block so the gate still reads.
// Sim portals share one dyeable mesh and their pads share one dyeable slab, both tinted with the entry colour.
// Map-mode portals on the hub ring get their pictogram sign beside them.
// A sim pad sits outside the voxel floor, so its marker carries a solid slab.
// Visuals are applied in OnConstruction. A bare automation world never begins play,
// so BeginPlay alone would leave the sign blank and the mesh empty.

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "GFMiniGameGate.generated.h"

class UBoxComponent;
class UFont;
class UStaticMeshComponent;
class UTextRenderComponent;
class APawn;

UENUM()
enum class EGFMiniGameGateRole : uint8
{
	Portal,
	Marker
};

UCLASS()
class GFGAME_API AGFMiniGameGate : public AActor
{
	GENERATED_BODY()

public:
	AGFMiniGameGate();

	/** Spawn path. Runs when FinishSpawning builds the actor, before any BeginPlay. */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	FName EntryId;

	/** Not named Role: that would shadow AActor::Role and fail UHT. */
	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	EGFMiniGameGateRole GateRole = EGFMiniGameGateRole::Portal;

	/** Rime anchor the ring and the pads were built from. */
	UPROPERTY(ReplicatedUsing = OnRep_Gate)
	FVector RimeCenter = FVector::ZeroVector;

	UFUNCTION()
	void OnRep_Gate();

	/**
	 * Label, sign, mesh (or the tinted placeholder) and the pad slab.
	 * Place() and the gate tests call this after the entry id is set. BeginPlay and OnRep call it too.
	 */
	void ApplyVisuals();

	/** Server: a pawn walked into a portal. Map modes open their level. Sims move the pawn to the pad. */
	bool EnterFrom(APawn* Pawn);

	const FString& GetLabel() const { return Label; }
	/** Text currently on the sign. Empty until ApplyVisuals has run. */
	FText GetSignText() const;
	bool IsUsingPlaceholder() const { return bUsingPlaceholder; }
	UStaticMeshComponent* GetGateMesh() const { return Mesh; }
	/** Solid floor under a sim-pad marker. Hidden and non-blocking on portals and mode-map signs. */
	UStaticMeshComponent* GetPadFloor() const { return PadFloor; }
	/** Pictogram sign beside a hub portal. Hidden, with no mesh and no collision, when the mode has no sign or this is a marker. */
	UStaticMeshComponent* GetModeSign() const { return ModeSign; }

private:
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

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PadFloor;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ModeSign;

	UPROPERTY(VisibleAnywhere)
	FString Label;

	/** UTextRenderComponent has no GetText in UE 5.8. This is the string last passed to SetText. */
	FText SignText;

	/** UTextRenderComponent has no GetFont in UE 5.8. Loaded once and passed to SetFont. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> SignFont;

	bool bUsingPlaceholder = true;
	float NextEnterSeconds = 0.f;
};
