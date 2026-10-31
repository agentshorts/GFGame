#pragma once

// Zone rows, ambience rows, and Shardstone rows. Data/CSV is the source of truth.
// Distances in this system are centimetres. Z is ignored by every containment test.

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "GFZoneTypes.generated.h"

namespace GFZoneCopy
{
	inline const TCHAR* LeavingKindle() { return TEXT("Leaving the Kindle Ring: Badlands ahead"); }
	inline const TCHAR* WelcomeHome() { return TEXT("Home. Kettle's on."); }
	inline const TCHAR* UnderConstruction() { return TEXT("Under construction, check back soon"); }
}

namespace FGFZoneConstants
{
	constexpr float KindleRadiusCm = 15000.f;
	constexpr float FringeOuterCm = 17000.f;
	constexpr float PorchRingCm = 90000.f;
	constexpr float PorchRadiusCm = 12000.f;
	/** Hub-ward offset from a porch centre to its arch (cm). The Grumble Dig lobby stays clear of this box. */
	constexpr float PorchArchInsetCm = 1000.f;
	constexpr float SafeCoreRadiusCm = 3000.f;
	constexpr float DeepInnerCm = 105000.f;
	constexpr float RimRadiusCm = 120000.f;
	constexpr float StampMarginCm = 2000.f;
	constexpr int32 KindleStampBlocks = 340;
	constexpr float HysteresisCm = 200.f;
	constexpr float HopRadiusCm = 600.f;
	constexpr float UnlockRadiusCm = 1200.f;
	constexpr float CompanionRadiusCm = 2000.f;
	constexpr float ChannelSeconds = 3.f;
	constexpr float PvPBlockSeconds = 8.f;
	constexpr float WalkCmPerSecond = 500.f;
	constexpr float MaxWalkFromStoneCm = 4.f * 60.f * WalkCmPerSecond;
	constexpr float HubToStoneMaxCm = 100000.f;
	constexpr float PoiMinSpacingCm = 12000.f;
	constexpr float SampleGridCm = 5000.f;
	constexpr int32 PoiSeed = 20261030;
	constexpr float ZoneTickSeconds = 0.2f;
	constexpr float ExposureLimit = 0.5f;
	constexpr float DefaultBlendSeconds = 1.5f;
	constexpr float ArrivalSpreadCm = 250.f;
}

UENUM(BlueprintType)
enum class EGFZoneShape : uint8
{
	Circle,
	Ring,
	Box
};

USTRUCT(BlueprintType)
struct GFGAME_API FGFZoneRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FGameplayTag Tag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") EGFZoneShape Shape = EGFZoneShape::Circle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float Radius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float InnerRadius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FVector HalfExtent = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") int32 DangerTier = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") bool bPvPAllowed = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName RegionId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName AmbienceId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName SpawnTableId;
	/** Delver;Wrangler;Scavenger weights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FString ResourceBias;

	bool IsShapeValid() const;
	bool ParseBias(float& Delver, float& Wrangler, float& Scavenger) const;
	bool ContainsXY(const FVector& Location) const;
	/** Centimetres outside the shape. 0 when the point is inside. Ignores Z. */
	float DistanceOutside(const FVector& Location) const;
};

USTRUCT(BlueprintType)
struct GFGAME_API FGFZoneAmbienceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float Saturation = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FLinearColor ContrastTint = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float Vignette = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float ExposureOffset = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") float BlendSeconds = 1.5f;
	/** Soft object path. Empty means no loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FString AmbientLoop;

	float ClampedExposure() const;
	float BlendTime() const;
};

USTRUCT(BlueprintType)
struct GFGAME_API FGFShardstoneRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FName ZoneId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FVector ArrivalOffset = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") bool bStartsUnlocked = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Zone") FString DisplayName;
};

USTRUCT(BlueprintType)
struct GFGAME_API FGFZoneHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") FName ZoneId;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") FGameplayTag Tag;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") int32 DangerTier = 2;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") bool bPvPAllowed = true;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") FName RegionId;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") FName AmbienceId;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Zone") int32 Priority = 0;
};

/** One porch tint for the Shatter Flats slab. Centers come from the zone rows. */
struct GFGAME_API FGFPorchDisc
{
	FName Id;
	FVector2D Center = FVector2D::ZeroVector;
	float RadiusCm = 0.f;
	FLinearColor Color = FLinearColor::White;
};
