// Mesh mining whitelist. Captain's rule: only crystal and trees can be mined.
// A static mesh is mineable only when it carries UGFMineableAssetUserData: WholeMesh (trees) allows any face,
// Slots (the town_v1 crystal-slot meshes) allows only faces of the listed material slots (slot 1 Crystal).
// A mesh without the data, or with Mode None, is refused. The data is stamped by Tools/import_town_v1.py from
// Data/Town/town_v1_mineable.json (generated from the handoff's MINEABLE json by Tools/gen_town_mineable.py), and by
// Tools/import_towns_v2.py from Data/Town/towns_v2_mineable.json (Tools/gen_towns_v2_mineable.py).
// Voxel blocks keep their own path (FGFMagicRules / AGFPlayerState::CanMineAt); this covers mesh hits.
// Allowed hits are spent by UGFBlockIntegritySubsystem (damage, break, drop, regrow).
#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CollisionQueryParams.h"
#include "GFMineable.generated.h"

class UPrimitiveComponent;
class UStaticMesh;
struct FHitResult;

UENUM(BlueprintType)
enum class EGFMineableMode : uint8
{
	/** Never mineable (walls, canal, paving, props, fountain, power span...). */
	None,
	/** Every face is mineable (forest trees). */
	WholeMesh,
	/** Only faces of the listed material slots are mineable (crystal slot). */
	Slots,
};

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced)
class GFCORE_API UGFMineableAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	EGFMineableMode Mode = EGFMineableMode::None;

	/** Material slot indices that can be mined when Mode is Slots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	TArray<int32> Slots;

	/** Kit the entry came from (for example town_v1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	FName Kit;

	/** What can be mined, or why not, from the whitelist json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	FString Reason;
};

/** One whitelist row from Data/Town/<kit>_mineable.json. */
struct GFCORE_API FGFMineableEntry
{
	FName Asset;
	FString Path;
	EGFMineableMode Mode = EGFMineableMode::None;
	TArray<int32> Slots;
	TArray<FName> MaterialSlots;
	int32 TriCount = 0;
	FString Reason;
};

struct GFCORE_API FGFMineable
{
	/** Pure rule: may a face that uses material slot SlotIndex be mined under this data? Null data refuses. */
	static bool AllowsSlot(const UGFMineableAssetUserData* Data, int32 SlotIndex);

	/** Mineable data on a static mesh, or null. */
	static const UGFMineableAssetUserData* Find(const UStaticMesh* Mesh);

	/** Static mesh behind a hit component (static mesh and instanced static mesh components), or null. */
	static const UStaticMesh* MeshOf(const UPrimitiveComponent* Component);

	/**
	 * Material slot of the face a complex trace hit, or INDEX_NONE (no face index, not a static mesh, or the
	 * face does not map to a section). Trace with QueryParams() so FaceIndex is filled in.
	 */
	static int32 SlotFromHit(const FHitResult& Hit);

	/** Whitelist gate for a mesh hit. Unknown meshes and unresolved faces on Slots meshes are refused. */
	static bool CanMineHit(const FHitResult& Hit, FString& OutReason);

	/** Query params for mining traces: complex collision and the face index. */
	static FCollisionQueryParams QueryParams(FName Tag = TEXT("GFMine"));

	/** Parses the whitelist json (gf.mineable/v1). */
	static bool ParseWhitelist(const FString& Json, TArray<FGFMineableEntry>& Out, FString& OutError);

	/** Loads <ProjectDir>/Data/Town/town_v1_mineable.json. */
	static bool LoadTownWhitelist(TArray<FGFMineableEntry>& Out, FString& OutError);
	static FString TownWhitelistPath();

	/** Loads <ProjectDir>/Data/Town/towns_v2_mineable.json (Rime, Cinderwright and homestead kit). */
	static bool LoadTownsV2Whitelist(TArray<FGFMineableEntry>& Out, FString& OutError);
	static FString TownsV2WhitelistPath();

	/** Loads and parses one whitelist json file. */
	static bool LoadWhitelistFile(const FString& Path, TArray<FGFMineableEntry>& Out, FString& OutError);

	static EGFMineableMode ModeFromString(const FString& Mode);

	/** True when Mesh's name is a row in Data/Town/*_mineable.json and it has no mineable tag. */
	static bool ListedWithoutTag(const UStaticMesh* Mesh);

	/**
	 * Logs once when a listed kit mesh has no UGFMineableAssetUserData.
	 * Returns the same answer as ListedWithoutTag.
	 */
	static bool WarnIfListedWithoutTag(const UStaticMesh* Mesh);

	/** Watches static mesh loads and warns when a listed kit mesh has no mineable tag. */
	static void StartKitAudit();
	static void StopKitAudit();
};

/** Editor/script hooks (Tools/import_town_v1.py stamps the data through these). */
UCLASS()
class GFCORE_API UGFMineableLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Replaces the mesh's mineable data. Mode None still stamps data, so the refusal is explicit. */
	UFUNCTION(BlueprintCallable, Category = "Mining")
	static bool SetMeshMineable(UStaticMesh* Mesh, EGFMineableMode Mode, const TArray<int32>& Slots, FName Kit, const FString& Reason);

	UFUNCTION(BlueprintCallable, Category = "Mining")
	static EGFMineableMode GetMeshMineableMode(const UStaticMesh* Mesh, TArray<int32>& OutSlots);
};
