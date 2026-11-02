#pragma once

// L_MiningRange. The server stamps the rows, the tree and the protected ground.
// Dressing is built on every machine from MiningRange_Dressing_v2.json.
// When that file is missing, or a prop mesh will not load, the row signs and a cube lever stay up.
// Sign lines for block rows are filled from DT_Blocks. Stations keep the rows-file lines.
// The way home is the shared return gate, standing where the rows file puts it.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFModeTypes.h"
#include "GFMiningRangeLayout.h"
#include "TimerManager.h"
#include "GFMiningRange.generated.h"

class AGFPlayerState;
class AGFVoxelWorld;
class UBoxComponent;
class UStaticMeshComponent;

USTRUCT()
struct GFGAME_API FGFMiningHudRow
{
	GENERATED_BODY()

	UPROPERTY()
	FString RowId;

	UPROPERTY()
	int32 Broken = 0;

	UPROPERTY()
	int32 Total = 0;

	/** Seconds from the last reset until the row was cleared. Negative until then. */
	UPROPERTY()
	float ClearSeconds = -1.f;
};

USTRUCT()
struct GFGAME_API FGFMiningHudGain
{
	GENERATED_BODY()

	UPROPERTY()
	FString Player;

	UPROPERTY()
	FString Item;

	UPROPERTY()
	int32 Count = 0;
};

UCLASS()
class GFGAME_API AGFMiningRange : public AActor, public IGFModeHandler
{
	GENERATED_BODY()

public:
	AGFMiningRange();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static const FName ModeName;
	static const TCHAR* RangeMap;

	virtual FName GetModeName() const override { return ModeName; }
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) override;
	virtual FName EditBlockReason(const AGFPlayerState* By, const FVector& Location) const override;

	/** Host console and the physical lever. Rebuilds every authored cell. */
	void ResetAll();
	/** Rebuilds one row, including its protected lip. */
	bool ResetRow(const FString& RowId);
	void ToggleHud();
	void PollGains();

	const FGFMiningRangeLayout& GetLayout() const { return Layout; }
	AGFVoxelWorld* GetVoxel() const { return Voxel.Get(); }
	bool IsHudOn() const { return bHudOn; }
	bool UsedArtDressing() const { return bArtDressing; }
	int32 SignCount() const { return SignsBuilt; }
	int32 BadgeCount() const { return BadgesBuilt; }
	int32 TrimCount() const { return TrimsBuilt; }
	int32 TileCount() const { return TilesBuilt; }
	bool HasResetTrigger() const { return ResetTrigger != nullptr; }

	int32 GainedCount(const FString& PlayerKey, FName Item) const;
	TArray<FString> HudLines() const;
	static void CaptureShotNames(TArray<FString>& Out);

	/** One overlap per row. Tests read this without walking a pawn into the sign. */
	int32 DemoTriggerCount() const { return DemoTriggers; }
	/** Runs that row's gf.Magic commands on the server. */
	void RunRowDemo(AGFPlayerState* By, const FString& RowId);
	/** Releases the field when the row's demo was holding one. */
	void ReleaseRowDemo(AGFPlayerState* By, const FString& RowId);

	/** Tests skip the art props and use the row-file signs. */
	static bool bTestForceDressingFallback;

	FIntVector ToGrid(const FIntVector& RangeCell) const { return Layout.ToGrid(RangeCell); }

private:
	UFUNCTION()
	void OnResetOverlap(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnDemoBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnDemoEnd(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void BuildWorld();
	void EnsureReturnGate();
	void BuildVisuals();
	void EnsurePlayerStart();
	void RegisterHandler();
	void StartPull();
	void CaptureIfEditor();
	void RunEditorCapture();
	void PublishStats();
	FString PlayerLabel(const AGFPlayerState* PS) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UActorComponent>> SpawnedBits;

	UPROPERTY(Replicated)
	TArray<FGFMiningHudRow> HudRows;

	UPROPERTY(Replicated)
	TArray<FGFMiningHudGain> HudGains;

	UPROPERTY(Replicated)
	bool bHudOn = true;

	UPROPERTY(Replicated)
	float HandlePitch = -35.f;

	UPROPERTY(Replicated)
	float ResetServerTime = 0.f;

	FGFMiningRangeLayout Layout;
	TWeakObjectPtr<AGFVoxelWorld> Voxel;
	TWeakObjectPtr<UStaticMeshComponent> HandleMesh;
	TWeakObjectPtr<UBoxComponent> ResetTrigger;

	struct FGainTracker
	{
		TMap<FName, int32> Last;
		TMap<FName, int32> Gained;
		FString Label;
		bool bSeeded = false;
	};
	TMap<FString, FGainTracker> Trackers;
	TArray<float> ClearAt;
	bool bArtDressing = false;
	int32 SignsBuilt = 0;
	int32 DemoTriggers = 0;
	TMap<TObjectPtr<UBoxComponent>, FString> DemoByTrigger;
	int32 BadgesBuilt = 0;
	int32 TrimsBuilt = 0;
	int32 TilesBuilt = 0;

	enum class ELever : uint8 { Rest, Pulling, Spring };
	ELever Lever = ELever::Rest;
	float LeverFrom = -35.f;
	float LeverTo = -35.f;
	float LeverAlpha = 1.f;
	bool bRegistered = false;
	FTimerHandle CaptureTimer;
	float CaptureArmedAt = -1.f;
};
