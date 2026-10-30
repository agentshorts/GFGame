#pragma once

// Hub gates and the pads for mini-games that have no map of their own.
// Map gates travel with UGFMiniGameGateSubsystem::EnterMiniGame.
// Sim pads are offsets from the Rime anchor so those modes are not stacked on the hub.
// Numbers are the layout locked in by the gate tests. See issue 158 for why.

#include "CoreMinimal.h"

struct GFCORE_API FGFMiniGameEntry
{
	FName Id;
	/** Shown on the hub gate and on that mode's map or pad. */
	FString Label;
	/** Long package name, or None when the mode is a sim on a pad. */
	FName Map;
	/** Director mode name, or None when the mode has a map. */
	FName SimMode;
	/** Object path (/Game/.../SM_X.SM_X). Empty when no mesh was authored. */
	FString ArtObject;
	FLinearColor PlaceholderColor = FLinearColor::White;
	/** True when the mesh is already in Content/Art/Gates and the gate should use it. */
	bool bExpectArt = false;
	int32 RingIndex = 0;
	bool bHasPad = false;
	/** Added to the Rime anchor. Ignored when bHasPad is false. */
	FVector PadOffset = FVector::ZeroVector;
	FVector2D PadHalfExtent = FVector2D::ZeroVector;
	/** /Script/Module.Class for a sim director. Empty for map modes. */
	FString DirectorClass;
	/** /Script/Module.Class spawned on the mode map. Empty when the map has no stage actor yet. */
	FString PlacedClass;

	bool HasMap() const { return !Map.IsNone(); }
	bool HasSim() const { return !SimMode.IsNone(); }
};

struct GFCORE_API FGFMiniGameCatalog
{
	/** Distance from the Rime anchor to each hub gate (cm). Inside the two-member Rime disc. */
	static constexpr float GateRingRadiusCm = 2000.f;
	/** Pads must stay outside this disc around the Rime anchor (cm). Max Rime land is 3400. */
	static constexpr float HubClearanceRadiusCm = 3600.f;
	static constexpr float PadHalfCm = 2000.f;
	/** Clear space between neighbouring pad boxes (cm). */
	static constexpr float PadGapCm = 2000.f;
	/** First sim pad, north of the anchor, on +Y (cm). */
	static constexpr float FirstPadYCm = 9000.f;
	/** Player stand in front of a pad marker (cm). */
	static constexpr float SimStandForwardCm = 400.f;
	static constexpr float SimStandUpCm = 120.f;

	static const TArray<FGFMiniGameEntry>& All();
	static const FGFMiniGameEntry* Find(FName Id);
	static FName HubMap();

	/** PIE packages look like /Game/Maps/UEDPIE_0_L_Hub. Match the short map name. */
	static bool WorldIs(const FString& WorldPackage, FName Map);

	static FVector GateLocation(const FVector& RimeCenter, int32 RingIndex, int32 RingCount);
	static FRotator GateFacing(const FVector& GateLocation, const FVector& RimeCenter);
	static FVector PadOrigin(const FGFMiniGameEntry& Entry, const FVector& RimeCenter);
	static FVector SimStand(const FGFMiniGameEntry& Entry, const FVector& RimeCenter);

	/**
	 * Where a sim director spawns when the caller does not know the Rime anchor.
	 * The shipped anchor is the origin, so this is the pad itself. None if this id is not a sim.
	 */
	static FVector DefaultSpawn(FName SimMode);

	/** Package path of ArtObject, without the object name. Empty if there is no art path. */
	static FString ArtPackage(const FGFMiniGameEntry& Entry);
};
