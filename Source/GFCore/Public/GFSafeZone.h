#pragma once

// Where players are safe. The safe area is territory, possibly several regions per world (one per group, each growing
// with its member count). Anything that needs to know "is this spot safe?" (the giant's herding, edge-walking and
// "can't enter" rule, the safe-zone sandbox powers) goes through IGFSafeZoneQuery, never through a hard-coded circle.
// Group names stay in data. Lives in GFCore so any module can ask; UGFSafeZoneSubsystem finds the world's current zone.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Interface.h"
#include "GFSafeZone.generated.h"

/** Read-only safe-territory query (horizontal: Z is ignored and kept). */
class GFCORE_API IGFSafeZoneQuery
{
public:
	virtual ~IGFSafeZoneQuery() = default;

	virtual bool IsInsideSafe(const FVector& Location) const = 0;
	/** Nearest point in safe territory (Location itself when already inside). Location when there is no safe area. */
	virtual FVector ClosestSafePoint(const FVector& Location) const = 0;
	/**
	 * Nearest point on a safe border and the border's outward direction there (pointing away from safety). Used to
	 * walk the edge and to push things out. False when there is no safe area.
	 */
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const = 0;
	virtual bool HasAnySafe() const = 0;
};

/** One circular safe region: grows with its member count up to a cap. Id is an opaque key (no names in code). */
USTRUCT(BlueprintType)
struct GFCORE_API FGFSafeRegion
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") int32 Members = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") float BaseRadius = 1200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") float RadiusPerExtraMember = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Safe") float MaxRadius = 3200.f;

	/** min(Base + PerExtra * (Members - 1), Max). */
	float Radius() const;
};

/** Simple implementation: a union of circles. */
struct GFCORE_API FGFSafeCircles : public IGFSafeZoneQuery
{
	TArray<FGFSafeRegion> Regions;

	FGFSafeCircles() = default;
	explicit FGFSafeCircles(const TArray<FGFSafeRegion>& InRegions) : Regions(InRegions) {}

	virtual bool IsInsideSafe(const FVector& Location) const override;
	virtual FVector ClosestSafePoint(const FVector& Location) const override;
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override;
	virtual bool HasAnySafe() const override { return Regions.Num() > 0; }
};

/** Helpers built only on the interface. */
struct GFCORE_API FGFSafeZone
{
	/** Horizontal distance to safety (0 inside). */
	static float DistanceOutside(const IGFSafeZoneQuery& Zone, const FVector& P);
	/** Horizontal unit direction toward safety from P (for a point inside: inward from the nearest border). */
	static FVector DirToSafe(const IGFSafeZoneQuery& Zone, const FVector& P);
	/** P pushed out (horizontally) until it's at least Margin outside every safe region. */
	static FVector KeepOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin);
	/** True if P is at least Margin outside safety (with a small tolerance). */
	static bool IsClear(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin);
	/** One step of StepLen from From toward Goal that stays Margin outside safety (slides along borders). */
	static FVector StepOutside(const IGFSafeZoneQuery& Zone, const FVector& From, const FVector& Goal, float StepLen, float Margin);
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFSafeZoneProvider : public UInterface
{
	GENERATED_BODY()
};

/** Something that owns the world's safe territory (the giant event's regions today, the territory system later). */
class GFCORE_API IGFSafeZoneProvider
{
	GENERATED_BODY()

public:
	/** The provider's current zone (null = it has none right now). */
	virtual const IGFSafeZoneQuery* GetSafeZoneQuery() const = 0;
};

/**
 * Per-world lookup for "the" safe zone: a test/territory override first, else the newest live provider.
 * Works on the server and on clients (providers register wherever they exist).
 */
UCLASS()
class GFCORE_API UGFSafeZoneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGFSafeZoneSubsystem* Get(const UObject* WorldContext);

	void RegisterProvider(UObject* Provider);
	void UnregisterProvider(UObject* Provider);
	/** Replaces every provider (null = back to providers). */
	void SetOverride(TSharedPtr<IGFSafeZoneQuery> Zone) { Override = Zone; }

	/** Current zone or null when nothing provides one. */
	const IGFSafeZoneQuery* GetZone() const;
	/** Inside safe territory? False when there is no safe zone at all. */
	bool IsInsideSafe(const FVector& Location) const;

private:
	TArray<TWeakObjectPtr<UObject>> Providers;
	TSharedPtr<IGFSafeZoneQuery> Override;
};
