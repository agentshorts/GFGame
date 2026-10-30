#pragma once

// Where players are safe. The safe area is territory, possibly several regions per world (one per group, each growing
// with its member count). Anything that needs to know "is this spot safe?" (the giant's herding, edge-walking and
// "can't enter" rule, the safe-zone sandbox powers) goes through IGFSafeZoneQuery, never through a hard-coded circle.
// Group names stay in data. Lives in GFCore so any module can ask; UGFSafeZoneSubsystem finds the world's current zone.
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Interface.h"
#include "GFSafeZone.generated.h"

/**
 * Who a per-player safety question is about. Opaque ids only (no names in code).
 * Key is the stable player key (empty for a non-player), GroupId is the player's group (faction) or None,
 * bHost marks the session host.
 */
struct GFCORE_API FGFSafeSubject
{
	FString Key;
	FName GroupId;
	bool bHost = false;

	FGFSafeSubject() = default;
	FGFSafeSubject(const FString& InKey, FName InGroupId, bool bInHost = false) : Key(InKey), GroupId(InGroupId), bHost(bInHost) {}
};

/** Read-only safe-territory query (horizontal: Z is ignored and kept). */
class GFCORE_API IGFSafeZoneQuery
{
public:
	virtual ~IGFSafeZoneQuery() = default;

	virtual bool IsInsideSafe(const FVector& Location) const = 0;
	/**
	 * "In safe land AND protected" for one player. Zones with no per-player rules (plain circles, the giant's
	 * regions) answer IsInsideSafe. Territory overrides this with land ownership plus visitor protection.
	 */
	virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const { return IsInsideSafe(Location); }
	/** The group that owns the land at Location, or None (unowned, or a zone that has no owners). */
	virtual FName OwnerAt(const FVector& Location) const { return NAME_None; }
	/** Nearest point in safe territory (Location itself when already inside). Location when there is no safe area. */
	virtual FVector ClosestSafePoint(const FVector& Location) const = 0;
	/**
	 * Nearest point on a safe border and the border's outward direction there (pointing away from safety). Used to
	 * walk the edge and to push things out. False when there is no safe area.
	 */
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const = 0;
	virtual bool HasAnySafe() const = 0;
	/**
	 * True where nothing should be placed (wild hazards such as lava). Zones that know no hazards answer false.
	 * Used to keep respawn points off hazards.
	 */
	virtual bool IsHazardAt(const FVector& Location) const { return false; }
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
	/**
	 * P pushed out (horizontally) until it's at least Margin outside every safe region. Where pushing out from the
	 * nearest border keeps landing in another region's margin (gaps narrower than 2 * Margin), it searches outward for
	 * the nearest clear spot, which is far more work: call it once, not every step.
	 */
	static FVector KeepOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin);
	/**
	 * KeepOut's cheap part only: pushes P out from the nearest border up to MaxIter times. Returns true (and Out) if
	 * that ends clear; false in gaps narrower than 2 * Margin, where only KeepOut's costly search would find a spot.
	 * Safe to call every step.
	 */
	static bool PushOut(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin, FVector& Out, int32 MaxIter = 12);
	/** True if P is at least Margin outside safety (with a small tolerance). */
	static bool IsClear(const IGFSafeZoneQuery& Zone, const FVector& P, float Margin);
	/**
	 * One step toward Goal that stays Margin outside safety: straight, else sliding along the border, else the clear
	 * direction closest to the wanted one. From a clear spot it never moves more than StepLen (it stays put when no
	 * direction is clear, e.g. at the mouth of a gap too narrow to pass). From a spot that safety grew over, it is
	 * pushed out at once (KeepOut), however far that is.
	 */
	static FVector StepOutside(const IGFSafeZoneQuery& Zone, const FVector& From, const FVector& Goal, float StepLen, float Margin);
};

/**
 * No-entry boxes: places nothing that walks may enter (e.g. a jail cell). World-space boxes; anything inside one is
 * out of reach. Pure helpers, no world.
 */
struct GFCORE_API FGFNoEntry
{
	/** True if P is inside any box (3D). */
	static bool IsInside(const TArray<FBox>& Boxes, const FVector& P);
	/**
	 * P pushed out horizontally until a body of Radius at P is clear of every box whose height range overlaps the body
	 * (P.Z - HalfHeight .. P.Z + HalfHeight). Boxes above or below the body are ignored. Z is kept.
	 */
	static FVector KeepOut(const TArray<FBox>& Boxes, const FVector& P, float Radius, float HalfHeight);
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
	/**
	 * What answers IsHazardAt for this provider, also while it has no safe area (wild hazards exist before anyone
	 * holds land). Defaults to GetSafeZoneQuery.
	 */
	virtual const IGFSafeZoneQuery* GetHazardQuery() const { return GetSafeZoneQuery(); }
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
	/**
	 * IGFSafeZoneQuery::IsSafeFor asked of the source that owns the land at Location (same search as OwnerAt), so a
	 * newer provider without owners can't hide protection. Unowned land: the current zone. False with no safe zone.
	 */
	bool IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const;
	/**
	 * Owner of the land at Location. The override answers alone when set. Otherwise providers are asked newest
	 * first and the first owner wins, so a newer provider without owners (the giant's circles) does not hide land.
	 */
	FName OwnerAt(const FVector& Location) const;
	/** A wild hazard at Location (IGFSafeZoneQuery::IsHazardAt): the override, or any provider but Skip (GetHazardQuery). */
	bool IsHazardAt(const FVector& Location, const UObject* Skip = nullptr) const;

	/** Registers (or moves) Owner's no-entry box (FGFNoEntry). Works wherever Owner exists (server and clients). */
	void SetNoEntry(const UObject* Owner, const FBox& Box);
	void ClearNoEntry(const UObject* Owner);
	/** Every live no-entry box. */
	TArray<FBox> GetNoEntry() const;
	bool IsInsideNoEntry(const FVector& Location) const { return FGFNoEntry::IsInside(GetNoEntry(), Location); }

	/**
	 * Every live provider's current zone, newest first, leaving out Skip (a provider whose own zone wraps this list).
	 * The override is a whole-world replacement, not a provider, so it is not in this list.
	 */
	TArray<const IGFSafeZoneQuery*> GetProviderZones(const UObject* Skip = nullptr) const;
	/** GetProviderZones into a caller's buffer (appended; no allocation once the buffer has grown). */
	void AppendProviderZones(TArray<const IGFSafeZoneQuery*>& Out, const UObject* Skip = nullptr) const;

private:
	/** The query that owns the land at Location (override alone when set, else providers newest first), or null. */
	const IGFSafeZoneQuery* OwnerZoneAt(const FVector& Location, FName& OutOwner) const;

	TArray<TWeakObjectPtr<UObject>> Providers;
	TSharedPtr<IGFSafeZoneQuery> Override;
	TArray<TPair<TWeakObjectPtr<const UObject>, FBox>> NoEntry;
};

/**
 * Several zones' shapes as one: Own (optional) plus every provider zone in Zones except Skip
 * (UGFSafeZoneSubsystem::GetProviderZones). The list is read again on every query (or once per FPartsScope), so the
 * union follows providers as they appear, grow or go away. Inside is "inside any part". Edges are the parts' borders
 * that no other part covers, plus the points where two parts' borders cross (the notch between two overlapping
 * parts). Owned land answers OwnerAt and IsSafeFor through the part that owns it. Elsewhere any part that says safe
 * wins. Hazards: Own's, plus any provider's (UGFSafeZoneSubsystem::IsHazardAt, also providers without land). A query that re-enters the same union (two unions that see each other) gets
 * an empty zone, so it can't loop.
 * Own is not owned: the caller keeps it alive for as long as the union is queried.
 */
struct GFCORE_API FGFSafeZoneUnion : public IGFSafeZoneQuery
{
	const IGFSafeZoneQuery* Own = nullptr;
	TWeakObjectPtr<const UGFSafeZoneSubsystem> Zones;
	TWeakObjectPtr<const UObject> Skip;
	/** Own counts only while no provider has a safe area (a stand-in until real land exists). */
	bool bOwnOnlyWithoutLand = false;

	virtual bool IsInsideSafe(const FVector& Location) const override;
	virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const override;
	virtual FName OwnerAt(const FVector& Location) const override;
	virtual FVector ClosestSafePoint(const FVector& Location) const override;
	virtual bool ClosestEdge(const FVector& Location, FVector& OutPoint, FVector& OutOutward) const override;
	virtual bool HasAnySafe() const override;
	virtual bool IsHazardAt(const FVector& Location) const override;

	/**
	 * While one is alive, the parts are read once and reused by every query (e.g. for one sim step), instead of
	 * asking the subsystem each time. Providers must not come or go meanwhile. Nests; a null union is a no-op.
	 */
	struct GFCORE_API FPartsScope
	{
		explicit FPartsScope(const FGFSafeZoneUnion* InUnion);
		~FPartsScope();
		FPartsScope(const FPartsScope&) = delete;
		FPartsScope& operator=(const FPartsScope&) = delete;

	private:
		const FGFSafeZoneUnion* Union = nullptr;
	};

private:
	/**
	 * The parts with a safe area: Own first (unless bOwnOnlyWithoutLand and a provider has land), then providers
	 * newest first. Filled into a reused buffer; only valid until the next call (the Depth guard keeps queries from
	 * re-entering this union while one is iterating).
	 */
	const TArray<const IGFSafeZoneQuery*>& Parts() const;
	mutable int32 Depth = 0;
	mutable int32 ScopeDepth = 0;
	mutable bool bPartsCached = false;
	mutable TArray<const IGFSafeZoneQuery*> PartsBuffer;
	mutable TArray<const IGFSafeZoneQuery*> ProviderBuffer;
};
