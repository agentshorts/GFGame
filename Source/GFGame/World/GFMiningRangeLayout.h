#pragma once

// Mining range layout. Rows and the Verdant tree come from Data/MiningRange.
// Dressing comes from MiningRange_Dressing_v2.json when that file is present.
// v1 trim yaws are 90 degrees off, so the spawner does not read v1.
// A missing or unusable dressing file is not fatal: callers keep the row signs and a plain lever.

#include "CoreMinimal.h"

class UDataTable;
class UWorld;

namespace GFMiningMaps
{
	/** Bare name, /Game/Maps/<name>, or a PIE prefix such as UEDPIE_0_<name>. */
	GFGAME_API bool IsNamedMap(const FString& MapOrPackage, const TCHAR* ShortName);
	GFGAME_API bool IsMiningRange(const UWorld* World);
	GFGAME_API bool IsHub(const UWorld* World);
}

struct FGFMiningCam
{
	FString Name;
	FVector LocationCm = FVector::ZeroVector;
	FVector LookAtCm = FVector::ZeroVector;
};

struct FGFMiningRow
{
	FString Id;
	FString Zone;
	FString Kind;
	FName Block = NAME_None;
	FString StandInMesh;
	bool bSpellProof = false;
	bool bProtected = false;
	bool bPlaced = false;
	FVector SignCm = FVector::ZeroVector;
	float SignYaw = 0.f;
	FString Line1;
	FString Line2;
	FString Line3;
	/** Filled once a block table is applied. Stations keep the rows-file lines. */
	FString Sign1;
	FString Sign2;
	FString Sign3;
};

struct FGFMiningCell
{
	FIntVector Range = FIntVector::ZeroValue;
	FName Authored = NAME_None;
	FName Stamp = NAME_None;
	int32 RowIndex = INDEX_NONE;
	bool bProtected = false;
	bool bSpellProof = false;
	bool bBreakable = false;
};

struct FGFDressingSign
{
	FString RowId;
	FString Mesh;
	FVector LocationCm = FVector::ZeroVector;
	float YawDeg = 0.f;
};

struct FGFDressingBadge
{
	FString RowId;
	FString Mesh;
	/** Sign-local cm. Default is 4 forward, viewer's right, 10 up. Viewer's right is local -Y. */
	FVector OffsetCm = FVector(4.f, -44.f, 10.f);
	float YawDeg = 0.f;
};

struct FGFDressingLever
{
	FString BaseMesh;
	FVector LocationCm = FVector::ZeroVector;
	float YawDeg = 0.f;
	FString HandleMesh;
	FVector HandleOffsetCm = FVector(0.f, 0.f, 73.f);
	float RestPitch = -35.f;
	float PulledPitch = 35.f;
	bool bPresent = false;
};

struct FGFDressingProp
{
	FString Mesh;
	FString Material;
	FString Zone;
	FVector LocationCm = FVector::ZeroVector;
	float YawDeg = 0.f;
};

struct FGFMiningDressing
{
	bool bLoaded = false;
	TArray<FGFDressingSign> Signs;
	TArray<FGFDressingBadge> Badges;
	FGFDressingLever Lever;
	TArray<FGFDressingProp> Trims;
	TArray<FGFDressingProp> Tiles;

	const FGFDressingSign* FindSign(const FString& RowId) const;
	const FGFDressingBadge* FindBadge(const FString& RowId) const;
};

struct GFGAME_API FGFMiningRangeLayout
{
	bool bLoaded = false;
	FString Error;

	FVector SpawnCm = FVector(-500.f, 0.f, 0.f);
	float SpawnYaw = 0.f;
	FVector ResetCm = FVector(-500.f, -300.f, 0.f);
	float ResetYaw = 90.f;
	FVector ReturnCm = FVector(-900.f, 0.f, 0.f);
	float ReturnYaw = 0.f;

	TArray<FString> Zones;
	TArray<FGFMiningRow> Rows;
	TArray<FGFMiningCell> Cells;
	TArray<FGFMiningCam> Cams;
	FIntVector Origin = FIntVector::ZeroValue;
	FIntVector GridSize = FIntVector(16, 16, 16);
	int32 TreeLogs = 0;
	int32 TreeLeaves = 0;

	FGFMiningDressing Dressing;

	FIntVector ToGrid(const FIntVector& RangeCell) const { return RangeCell - Origin; }

	static FString RowsPath();
	static FString TreePath();
	static FString DressingPath();

	/** Rows, tree, ground, lip, then dressing v2 if the file parses. */
	bool LoadProject();

	/**
	 * Parses a dressing document. False when the JSON is bad or has no signs.
	 * Accepts the v2 keys and the older names (trims, tiles, reset_lever).
	 * Badge offset prefers sign_local_offset_ue_cm. A forward/right/up record
	 * maps "right" onto local -Y so the badge sits on the viewer's right.
	 */
	static bool ParseDressingString(const FString& Json, FGFMiningDressing& Out, FString& OutError);
	static bool LoadDressingFile(const FString& Path, FGFMiningDressing& Out, FString& OutError);

	/** Writes Stamp and the runtime sign lines. Unknown block ids stamp as Stone. */
	void ApplyBlockTable(const UDataTable* Table);

	int32 BreakableCount(int32 RowIndex) const;
	int32 RowIndexOf(const FString& RowId) const;
};
