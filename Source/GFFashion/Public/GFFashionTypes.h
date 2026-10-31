#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFItemTypes.h"
#include "GFFashionTypes.generated.h"

/** Result of a tailor craft. Grants that miss report this instead of changing the bag. */
UENUM()
enum class EGFWardrobeResult : uint8
{
	Granted,
	AlreadyOwned,
	UnknownPiece,
	NotThisSource,
	MissingMaterials,
	NoAccount,
	Refused
};

/**
 * Outfit slots (PLAN.md M3). Dyes are per slot, not a slot of their own.
 * Hair is the head slot and Shoes are the feet slot (those names stay so saved outfits and DT_Items tokens do not move).
 * Tail is the dino tail. An empty tail does not change the score of a five-piece look.
 */
UENUM(BlueprintType)
enum class EGFOutfitSlot : uint8
{
	Hair,
	Top,
	Bottom,
	Shoes,
	Accessory,
	Tail,
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

	/** Themed set (Garden, Explorer, Volcano, NightMarket, FossilFormal). The judge's set score reads this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName SetId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	EGFRarity Rarity = EGFRarity::Common;

	/** Reserved for dyes. Unused this sprint; catalog rows keep it at 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	int32 DyeChannels = 0;

	/** Free piece handed to a new character, one per slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	bool bStarter = false;

	/** One line on the rack for a locked piece ("Win Course 1 in Beat Hop."). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString SourceLine;

	/** One or two of Starter, Runway, BeatHop, Mine, Craft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	TArray<FName> Sources;

	/** Beat Hop course id when a BeatHop source is for one course (Fern is course 1). None matches any. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName SourceContext;

	/** Tailor inputs when Sources contains Craft. Item ids are DT_Items materials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	TArray<FGFItemStack> CraftCost;
};

/** One row of DT_FashionPieces. Strings stay easy to type in CSV; the catalog parses them into FGFFashionPiece. */
USTRUCT(BlueprintType)
struct GFFASHION_API FGFFashionPieceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString DisplayName;

	/** Head or Hair, Top, Bottom, Feet or Shoes, Accessory, Tail. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName Slot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName SetId;

	/** Common, Uncommon or Rare. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName Rarity;

	/** Tags separated by |. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString Tags;

	/** R,G,B in 0..1, separated by commas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString BaseColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	bool bDyeable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	int32 DyeChannels = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	bool bStarter = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString SourceLine;

	/** Sources separated by | (Starter, Runway, BeatHop, Mine, Craft). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString Sources;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FName SourceContext;

	/** ItemId:Count pairs separated by |, for a Craft source. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Fashion")
	FString CraftCost;
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

/** Wardrobe from Data/CSV/DT_FashionPieces.csv, plus the built-in themes. Adding a piece is a new CSV row. */
struct GFFASHION_API FGFFashionCatalog
{
	static const TArray<FGFFashionPiece>& Pieces();
	static const FGFFashionPiece* FindPiece(FName Id);
	static TArray<FName> PiecesForSlot(EGFOutfitSlot Slot);
	static const FGFFashionPiece* StarterForSlot(EGFOutfitSlot Slot);
	static const TArray<FGFFashionTheme>& Themes();
	static const FGFFashionTheme* FindTheme(FName Id);
	/** How many catalog rows the CSV held the last time pieces were loaded. */
	static int32 RowCount();
};
