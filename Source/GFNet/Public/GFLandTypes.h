#pragma once

// Landlord loop (PLAN.md section 2a): her land is split into plots, friends claim one and pay rent in materials,
// letters arrive in mailboxes, and every guest keeps a blueprint backup of their build.
// Everything here is pure data + rules (no world), so it can be unit-tested; AGFGameState runs it on the server.
#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFVoxelTypes.h"
#include "GFLandTypes.generated.h"

class UDataTable;

UENUM(BlueprintType)
enum class EGFPlotKind : uint8
{
	/** Her own plot. Host (and nobody else) builds here. */
	Home,
	/** Can be rented to a friend. */
	Rentable
};

/** Rent Board status chip. Purely cosmetic consequences: nothing is ever deleted because of rent. */
UENUM(BlueprintType)
enum class EGFRentStatus : uint8
{
	/** No tenant. */
	Vacant,
	/** Tenant, no rent (free, or her own plot). */
	Free,
	Paid,
	/** 1 period missed: "Rent due!" sign (grace day). */
	Due,
	/** 2 missed: cosmetic vines; the tenant can't place new blocks until they pay (removing is fine). */
	Overgrown,
	/** 3+ missed (capped at 3 owed): soft desaturated, see-through look, sleepy "zzz" sign. */
	Faded
};

USTRUCT(BlueprintType)
struct GFNET_API FGFLandPlot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FName PlotId;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") EGFPlotKind Kind = EGFPlotKind::Rentable;
	/** Cell bounds: [Min, Min + Size). Plots are full height. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FIntVector Min = FIntVector::ZeroValue;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FIntVector Size = FIntVector(24, 24, 32);
	/** She marked it "For rent" (friends can claim it themselves). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") bool bForRent = false;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FString TenantKey;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FString TenantName;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") int32 TenantColor = -1;

	/** One rentable material and an amount per period (one real-world day). Amount 0 = free. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") FName RentItem;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") int32 RentAmount = 0;
	/** Rent holiday: nothing accrues while paused. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") bool bRentPaused = false;
	/** Tenant setting (on by default): pay automatically on login / at day change if they have the materials. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") bool bAutoPay = true;
	/** Unpaid periods, capped at FGFLandRules::MaxOwedPeriods. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") int32 OwedPeriods = 0;
	/** Host calendar day (FGFLandRules::DayIndex) up to which rent has been accrued. */
	UPROPERTY(SaveGame) int64 LastAccruedDay = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") FDateTime LastPaidUtc;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") int32 LastPaidAmount = 0;
	/** Derived from the fields above (FGFLandRules::RefreshStatus); stored so it replicates to the Rent Board. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land|Rent") EGFRentStatus Status = EGFRentStatus::Vacant;

	FIntVector Max() const { return Min + Size - FIntVector(1, 1, 1); }
	bool Contains(const FIntVector& Cell) const
	{
		return Cell.X >= Min.X && Cell.Y >= Min.Y && Cell.Z >= Min.Z && Cell.X < Min.X + Size.X && Cell.Y < Min.Y + Size.Y && Cell.Z < Min.Z + Size.Z;
	}
	bool HasTenant() const { return !TenantKey.IsEmpty(); }
	bool ChargesRent() const { return Kind == EGFPlotKind::Rentable && HasTenant() && !RentItem.IsNone() && RentAmount > 0; }
};

/** The whole land (replicated to everyone: the Land Board and Rent Board read it). Saved with her world. */
USTRUCT(BlueprintType)
struct GFNET_API FGFLandState
{
	GENERATED_BODY()

	/** Her player key (the host). Rent letters go here. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FString LandlordKey;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") TArray<FGFLandPlot> Plots;
	/** Friends she gave Builder rights on her common areas. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") TArray<FString> Builders;
	/** Bumped on every change (UI refresh, tests). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") int32 Revision = 0;

	FGFLandPlot* Find(FName PlotId) { return Plots.FindByPredicate([PlotId](const FGFLandPlot& P) { return P.PlotId == PlotId; }); }
	const FGFLandPlot* Find(FName PlotId) const { return Plots.FindByPredicate([PlotId](const FGFLandPlot& P) { return P.PlotId == PlotId; }); }
	/** The plot containing Cell, or null for the Commons. */
	const FGFLandPlot* PlotAt(const FIntVector& Cell) const { return Plots.FindByPredicate([&Cell](const FGFLandPlot& P) { return P.Contains(Cell); }); }
	int32 CountPlotsOf(const FString& Key) const;
	bool IsBuilder(const FString& Key) const { return Builders.Contains(Key); }
};

/**
 * A guest-owned backup of one plot's build (PLAN.md section 2a): the chunk data relative to the plot corner.
 * Kept in the guest's own character under HostWorldId + PlotId, so they can't lose their work.
 * TODO(M1 furniture): add the furniture list when furniture actors exist (none in the greybox yet).
 */
USTRUCT(BlueprintType)
struct GFNET_API FGFPlotBlueprint
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FString HostWorldId;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FName PlotId;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FIntVector Size = FIntVector::ZeroValue;
	UPROPERTY(SaveGame) FGFVoxelSaveData Voxels;
	/** FGFVoxelGrid::GetRegionHash of the plot when it was taken (relative, so equal on any plot). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") int64 Hash = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") int32 BlockCount = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Land") FDateTime CreatedUtc;

	bool SameSlot(const FString& WorldId, FName Plot) const { return HostWorldId == WorldId && PlotId == Plot; }
};

UENUM(BlueprintType)
enum class EGFLetterKind : uint8
{
	Rent,
	Gift,
	Note,
	WagerReceipt
};

/** One letter. Items in a letter sit in a ledger escrow (Kind "Mail", id = letter id) until claimed. */
USTRUCT(BlueprintType)
struct GFNET_API FGFMailLetter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FGuid Id;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") EGFLetterKind Kind = EGFLetterKind::Note;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FString FromKey;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FString FromName;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FString ToKey;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FString Text;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FName PlotId;
	/** What's inside (display copy; the escrow holds the real items). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") TArray<FGFItemStack> Stacks;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") bool bClaimed = false;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Mail") FDateTime SentUtc;

	bool HasItems() const { return Stacks.Num() > 0; }
};

/** Everything a player can ask the landlord system to do (one RPC carries them all). */
UENUM(BlueprintType)
enum class EGFLandOp : uint8
{
	// Guests (and her)
	ClaimPlot,
	PayRent,
	SetAutoPay,
	SendGift,
	SendNote,
	ClaimLetter,
	ClaimAllLetters,
	/** Restore one of your blueprints (SourceWorldId + SourcePlotId) onto PlotId. */
	RestoreBlueprint,
	// Host only
	SetPlotRent,
	AssignPlot,
	ForgiveRent,
	PauseRent,
	GrantBuilder,
	PackUpPlot,
	ApproveRestore
};

UENUM(BlueprintType)
enum class EGFLandResult : uint8
{
	Ok,
	/** Accepted, waiting on someone (her OK for a free restore, the guest's ack for a pack-up). */
	Pending,
	NotAllowed,
	NoSuchPlot,
	NoSuchPlayer,
	PlotTaken,
	AlreadyHasPlot,
	NotForRent,
	NotRentable,
	RentTooHigh,
	NothingOwed,
	CantAfford,
	InvalidStacks,
	NoSuchLetter,
	AlreadyClaimed,
	NoBlueprint,
	PlotNotEmpty,
	NotReady
};

USTRUCT(BlueprintType)
struct GFNET_API FGFLandAction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Land") int32 RequestId = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Land") EGFLandOp Op = EGFLandOp::ClaimPlot;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FName PlotId;
	/** Another player (gift / note recipient, assign target, builder). */
	UPROPERTY(BlueprintReadWrite, Category = "Land") FString OtherKey;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FName ItemId;
	UPROPERTY(BlueprintReadWrite, Category = "Land") int32 Amount = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Land") bool bFlag = false;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FString Text;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FGuid LetterId;
	UPROPERTY(BlueprintReadWrite, Category = "Land") TArray<FGFItemStack> Stacks;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FString SourceWorldId;
	UPROPERTY(BlueprintReadWrite, Category = "Land") FName SourcePlotId;

	static FGFLandAction Make(EGFLandOp InOp, FName InPlot = NAME_None)
	{
		FGFLandAction A;
		A.Op = InOp;
		A.PlotId = InPlot;
		return A;
	}
};

/** Pure landlord rules. */
struct GFNET_API FGFLandRules
{
	static constexpr int32 MaxOwedPeriods = 3;
	/** Rent cap: value per period (PLAN.md section 2 value tiers). */
	static constexpr int32 MaxRentValuePerPeriod = 30;
	static constexpr int32 SuggestedRentValue = 10;
	static constexpr int32 PlotEdge = 24;
	static constexpr int32 MaxClaimedLettersKept = 20;

	static FName HomePlotId() { return FName(TEXT("Home")); }
	static FName PlotAId() { return FName(TEXT("PlotA")); }
	static FName PlotBId() { return FName(TEXT("PlotB")); }

	/** Her home plot + 2 rentable plots (24 x 24, full height) in a 2 x 2 layout; the 4th quarter is the Commons. */
	static FGFLandState MakeDefaultLayout(const FIntVector& WorldSize);

	static EGFRentStatus StatusFor(const FGFLandPlot& Plot);
	/** Returns true if the status changed. */
	static bool RefreshStatus(FGFLandPlot& Plot);

	/** Host calendar day number (local time). */
	static int64 DayIndex(const FDateTime& LocalNow);
	/** Accrues whole days since LastAccruedDay (none while paused or free). Returns periods added (after the cap). */
	static int32 Accrue(FGFLandPlot& Plot, int64 Today);
	/** Whole periods the payer can cover with Have of the rent item (<= OwedPeriods). */
	static int32 AffordablePeriods(const FGFLandPlot& Plot, int32 Have);

	/**
	 * Server permission check for one cell edit (PLAN.md section 2a):
	 * host anywhere; a tenant only in their own plot (no placing while Overgrown / Faded); Builders in the Commons.
	 */
	static bool CanEdit(const FGFLandState& Land, const FString& Key, bool bIsHost, const FIntVector& Cell, bool bPlacing, FString& OutReason);

	/** Ok / NotRentable / RentTooHigh. Amount 0 (free) is always Ok. */
	static EGFLandResult ValidateRent(const UDataTable* Items, FName ItemId, int32 Amount);
	/** Amount for about SuggestedRentValue, at least 1, never over the cap. 0 if the item isn't rentable. */
	static int32 SuggestedAmount(const UDataTable* Items, FName ItemId);

	static FGFPlotBlueprint MakeBlueprint(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot, const FString& HostWorldId, const FDateTime& Now);
	/** Hash of the plot as it stands now (comparable with FGFPlotBlueprint::Hash). */
	static int64 PlotHash(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot);
	/** True if anything stands above the ground layer (Z >= Min.Z + 1). */
	static bool IsBuiltOn(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot);
	/** Materials to rebuild a blueprint somewhere else: one item per block above the ground layer. */
	static TArray<FGFItemStack> RebuildCost(const FGFPlotBlueprint& Blueprint, TFunctionRef<FName(FName)> ItemForBlock);
	/** Adds or replaces (same HostWorldId + PlotId). Returns true if anything changed. */
	static bool StoreBlueprint(TArray<FGFPlotBlueprint>& Book, const FGFPlotBlueprint& Blueprint);
	static const FGFPlotBlueprint* FindBlueprint(const TArray<FGFPlotBlueprint>& Book, const FString& WorldId, FName PlotId);
};
