#pragma once

#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFFashionTypes.generated.h"

/** Outfit slots (PLAN.md M3). Dyes are per slot, not a slot of their own. */
UENUM(BlueprintType)
enum class EGFOutfitSlot : uint8
{
	Hair,
	Top,
	Bottom,
	Shoes,
	Accessory,
	Count UMETA(Hidden)
};

/** One wearable piece in the wardrobe catalog. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFFashionPiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	EGFOutfitSlot Slot = EGFOutfitSlot::Top;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FLinearColor BaseColor = FLinearColor::White;

	/** Style tags the themes look for (Floral, Dino, Sparkle, Cozy...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	TArray<FName> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	bool bDyeable = true;
};

/** What is worn in one slot. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFOutfitSlotState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "GF|Fashion")
	FName PieceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "GF|Fashion")
	bool bDyed = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "GF|Fashion")
	FLinearColor DyeColor = FLinearColor::White;
};

/** A full outfit: one entry per slot. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFOutfit
{
	GENERATED_BODY()

	FGFOutfit();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "GF|Fashion")
	TArray<FGFOutfitSlotState> Slots;

	/** Wears a catalog piece in its own slot (replacing what was there, dye cleared). False for unknown pieces. */
	bool Equip(FName PieceId);
	void Unequip(EGFOutfitSlot Slot);
	/** Dyes the piece in a slot. False if the slot is empty, the piece can't be dyed or the colour is invalid. */
	bool Dye(EGFOutfitSlot Slot, const FLinearColor& Color);
	void ClearDye(EGFOutfitSlot Slot);

	/**
	 * Checks an outfit from an untrusted source (a client RPC): one entry per slot, every piece known and in its own
	 * slot, dye only on dyeable pieces and only with finite 0..1 colours. OutWhy says what failed.
	 */
	bool Validate(FString* OutWhy = nullptr) const;
	/** Dye items this outfit costs, one entry per dyed slot (FGFDyes::CostForOutfitPiece); empty slots for undyed. */
	TArray<FGFItemStack> DyeCostForSlot(EGFOutfitSlot Slot) const;

	FName GetPieceId(EGFOutfitSlot Slot) const;
	const FGFFashionPiece* GetPiece(EGFOutfitSlot Slot) const;
	/** Dye if dyed, else the piece's own colour. False if empty. */
	bool GetColor(EGFOutfitSlot Slot, FLinearColor& Out) const;
	int32 NumFilled() const;
	bool IsEmpty() const { return NumFilled() == 0; }
	TArray<FName> GetPieceIds() const;
};

/** A runway theme: what the judges want to see, and what they really don't. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFFashionTheme
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	TArray<FName> WantedTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	TArray<FName> BannedTags;
};

/** Built-in wardrobe and themes (all original). Data tables can replace these later. */
struct GFFASHION_API FGFFashionCatalog
{
	static const TArray<FGFFashionPiece>& Pieces();
	static const FGFFashionPiece* FindPiece(FName Id);
	static TArray<FName> PiecesForSlot(EGFOutfitSlot Slot);
	static const TArray<FGFFashionTheme>& Themes();
	static const FGFFashionTheme* FindTheme(FName Id);
};
