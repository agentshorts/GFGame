#pragma once

// Town mesh nodes. A whole-mesh tree uses the Log block (hardness 1, integrity 100, drop WoodPlank).
// A crystal slot uses CrystalVein (hardness 2, integrity 200, drop Crystal). Damage is one synthetic
// cell through FGFMagicRules::Apply, the same formula as voxels. The drop is one FGFRewardEntry
// (chance 1, count 1, the block's DropItem) rolled by FGFRewards and handed to UGFMagicComponent::ReceiveDrop,
// which fills GFInventoryComponent and spawns the magnet ribbon. A depleted node regrows after
// RegrowSeconds (120s, the first stage of Steakroot / Emberpepper / Thornmelon in DT_Plants).
// A broken tree hides and drops collision. A depleted crystal slot keeps the prop and clears material slot 1,
// except on a house or town wall piece (Decision #321): mining its crystal destroys the whole piece, which hides,
// drops collision and regrows like a tree. The break still raises FGFMiningFX Break with the Crystal kit.
// A piece never regrows onto a player: while a pawn overlaps it, the regrow waits and checks again every
// RegrowRetrySeconds, so the gap only closes once it is clear.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GFMagicRules.h"
#include "GFRewards.h"
#include "GFTownNodes.generated.h"

class UGFMagicComponent;
class UGFMineableAssetUserData;
class UPrimitiveComponent;
class UWorld;
struct FMagicGlob;
struct FMagicSwing;
struct FGFMiningSpellRow;

/** Server only. One crystal break, once. Trees do not broadcast. */
DECLARE_MULTICAST_DELEGATE_FourParams(FGFOnTownCrystalTaken, UWorld* /*World*/, const FString& /*NodeKey*/, FVector /*Location*/, const TArray<FString>& /*ContributorKeys*/);

/**
 * The one-shot crystal break. Quest credit listens here and records the player key.
 * Do not read bDepleted for that credit: regrow clears the flag.
 */
struct GFMAGIC_API FGFTownNodeEvents
{
	static FGFOnTownCrystalTaken& OnCrystalTaken();
};

USTRUCT()
struct GFMAGIC_API FGFTownNodeRep
{
	GENERATED_BODY()

	UPROPERTY()
	FString Key;

	UPROPERTY()
	bool bDepleted = false;

	/** Crystal slot: the prop stays, only material slot 1 is spent. */
	UPROPERTY()
	bool bCrystal = false;

	/** Crystal set into a house or wall piece: breaking it destroys the whole piece. */
	UPROPERTY()
	bool bPiece = false;

	/** Set when the actor replicates. Level meshes are found by Key instead. */
	UPROPERTY()
	TObjectPtr<AActor> Actor;
};

/** Replicates which town nodes are spent. The server hides or restores; clients copy that. */
UCLASS()
class GFMAGIC_API AGFTownMineRelay : public AActor
{
	GENERATED_BODY()

public:
	AGFTownMineRelay();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetNodes(const TArray<FGFTownNodeRep>& In);
	const TArray<FGFTownNodeRep>& GetNodes() const { return Nodes; }

private:
	UFUNCTION()
	void OnRep_Nodes();

	void ApplyOne(const FGFTownNodeRep& Node, bool bDepleted) const;

	UPROPERTY(ReplicatedUsing = OnRep_Nodes)
	TArray<FGFTownNodeRep> Nodes;

	TArray<FGFTownNodeRep> Applied;
};

struct GFMAGIC_API FGFTownNodeRules
{
	/** DT_Plants first stage shared by Steakroot, Emberpepper and Thornmelon. */
	static constexpr float RegrowSeconds = 120.f;

	/** A house or wall piece that would regrow onto a pawn waits this long, then checks again. */
	static constexpr float RegrowRetrySeconds = 3.f;

	/** Log for a whole mesh (trees), CrystalVein for a crystal slot, None otherwise. */
	static FName BlockIdFor(const UGFMineableAssetUserData* Data);

	/** One sure drop of the block row's item. Log pays WoodPlank, CrystalVein pays Crystal. */
	static void DropTable(FName BlockId, TArray<FGFRewardEntry>& Out);

	/** Actor name with a PIE prefix stripped, so a placed mesh matches on server and client. */
	static FString KeyFor(const AActor* Actor);
	static FString KeyFor(const UPrimitiveComponent* Component);

	static AActor* FindActor(UWorld* World, const FString& Key);

	/**
	 * True for a house or town wall piece (asset name token House, StarterHouse or Wall). Its crystal is mineable
	 * and breaking it destroys the piece. Civic buildings, stalls, lamps and pylons keep the slot swap.
	 */
	static bool IsStructurePiece(const FString& MeshName);

	/** Hide a tree or a structure piece, or spend material slot 1 on a crystal prop. bDepleted false puts it back. */
	static void ApplyVisual(AActor* Actor, bool bDepleted, bool bCrystal, bool bPiece = false);

	/** True while a pawn with collision overlaps the piece's bounds, so turning its collision back on would trap them. */
	static bool IsRegrowBlocked(const AActor* Piece);

	/** Same integrity step as a voxel cell. The wound is the node's hit points. */
	static FMagicHitOutput ApplyHit(FMagicWound& Wound, const FMagicHitRequest& Request, FName BlockId);

	/** Roll the drop table and pass each stack through ReceiveDrop. */
	static void Grant(UGFMagicComponent* Who, const FMagicBreak& Break, const FVector& From, double Now);
};

/** Server-side town nodes. The integrity subsystem owns one of these. */
class GFMAGIC_API FGFTownNodeField
{
public:
	void Reset();
	void SetRegrowSeconds(float Seconds);
	float GetRegrowSeconds() const { return RegrowSeconds; }

	/** Full block integrity before the first hit, the wound after that, 0 when spent, -1 if the mesh cannot be mined. */
	float Remaining(const UPrimitiveComponent* Component) const;
	bool IsDepleted(const UPrimitiveComponent* Component) const;

	/** True once a hit opened a node on a house or wall piece. */
	bool IsPiece(const UPrimitiveComponent* Component) const;

	/** Cone and Frostbind traces. Spray and Rootmaul are globs and swings. */
	void CastShapes(UGFMagicComponent* Caster, int32& Budget, double Now);

	void HitSwing(UGFMagicComponent* Caster, const FMagicSwing& Swing, int32& Budget, double Now);

	/**
	 * Line from the glob's last position to where this step ends.
	 * True when a town mesh or a solid non-voxel blocker ate the glob.
	 * A voxel world hit returns false so the voxel puddle still runs.
	 */
	bool ConsumeGlob(UWorld* World, const FMagicGlob& Glob, const FVector& From, const FVector& To, const FGFMiningSpellRow& Spell, double Now, UGFMagicComponent* Owner);

	void TickOngoing(UWorld* World, double Now, int32& Budget, TFunctionRef<UGFMagicComponent*(const FString&)> FindCaster);

private:
	struct FFreeze
	{
		float Progress = 0.f;
		bool bLocked = false;
		double ShatterAt = 0.0;
		int32 Bonus = 0;
		TArray<FString> Contributors;
	};

	struct FNode
	{
		FString Key;
		TWeakObjectPtr<UPrimitiveComponent> Component;
		TWeakObjectPtr<AActor> Actor;
		FName BlockId;
		bool bCrystal = false;
		bool bPiece = false;
		bool bDepleted = false;
		bool bRegrowHeld = false;
		double RegrowAt = 0.0;
		FVector LastImpact = FVector::ZeroVector;
		FVector LastNormal = FVector::UpVector;
		FMagicWound Wound;
		FFreeze Freeze;
	};

	struct FSmear
	{
		FString Key;
		double Until = 0.0;
		float PerSecond = 40.f;
		float Rate = 1.f;
		TArray<FString> Owners;
	};

	FNode& FindOrAdd(const UPrimitiveComponent* Component, FName BlockId, bool bCrystal, bool bPiece, const FVector& Impact, const FVector& Normal);
	const FNode* Find(const UPrimitiveComponent* Component) const;
	void CommitBreak(FNode& Node, const FMagicBreak& Break, const FVector& At, double Now, UGFMagicComponent* Fallback, TFunctionRef<UGFMagicComponent*(const FString&)> FindCaster);
	void EmitContact(UWorld* World, const FNode& Node, EGFMagicElement Element, const FVector& Normal, float BeforeRemaining, bool bBroke);
	void Publish(UWorld* World);

	TMap<FString, FNode> Nodes;
	TArray<FSmear> Smears;
	float RegrowSeconds = FGFTownNodeRules::RegrowSeconds;
	TWeakObjectPtr<AGFTownMineRelay> Relay;
	bool bDirty = false;
};
