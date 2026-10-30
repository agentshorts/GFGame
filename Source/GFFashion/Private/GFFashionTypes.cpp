#include "GFFashionTypes.h"
#include "GFDyes.h"

namespace GFFashionCatalog
{
	static FGFFashionPiece Piece(const TCHAR* Id, const TCHAR* Name, EGFOutfitSlot Slot, const FLinearColor& Color, std::initializer_list<const TCHAR*> Tags, bool bDyeable = true)
	{
		FGFFashionPiece P;
		P.Id = Id;
		P.DisplayName = Name;
		P.Slot = Slot;
		P.BaseColor = Color;
		for (const TCHAR* T : Tags)
		{
			P.Tags.Add(T);
		}
		P.bDyeable = bDyeable;
		return P;
	}

	static FGFFashionTheme Theme(const TCHAR* Id, const TCHAR* Name, std::initializer_list<const TCHAR*> Wanted, std::initializer_list<const TCHAR*> Banned)
	{
		FGFFashionTheme T;
		T.Id = Id;
		T.DisplayName = Name;
		for (const TCHAR* W : Wanted) { T.WantedTags.Add(W); }
		for (const TCHAR* B : Banned) { T.BannedTags.Add(B); }
		return T;
	}
}

const TArray<FGFFashionPiece>& FGFFashionCatalog::Pieces()
{
	using namespace GFFashionCatalog;
	using S = EGFOutfitSlot;
	static const TArray<FGFFashionPiece> All = {
		// Hair
		Piece(TEXT("FernBraid"), TEXT("Fern Braid"), S::Hair, FLinearColor(0.30f, 0.55f, 0.25f), { TEXT("Nature"), TEXT("Cute") }),
		Piece(TEXT("StarlightBun"), TEXT("Starlight Bun"), S::Hair, FLinearColor(0.85f, 0.80f, 1.00f), { TEXT("Sparkle"), TEXT("Elegant") }),
		Piece(TEXT("MossMop"), TEXT("Moss Mop"), S::Hair, FLinearColor(0.40f, 0.45f, 0.20f), { TEXT("Nature"), TEXT("Cozy") }),
		Piece(TEXT("CometTail"), TEXT("Comet Tail"), S::Hair, FLinearColor(0.95f, 0.60f, 0.20f), { TEXT("Sparkle"), TEXT("Sporty") }),
		Piece(TEXT("DrizzleCurls"), TEXT("Drizzle Curls"), S::Hair, FLinearColor(0.35f, 0.40f, 0.60f), { TEXT("Rain"), TEXT("Cozy") }),
		// Tops
		Piece(TEXT("PetalBlouse"), TEXT("Petal Blouse"), S::Top, FLinearColor(1.00f, 0.65f, 0.75f), { TEXT("Floral"), TEXT("Elegant") }),
		Piece(TEXT("ExplorerVest"), TEXT("Explorer Vest"), S::Top, FLinearColor(0.60f, 0.45f, 0.25f), { TEXT("Adventure"), TEXT("Practical") }),
		Piece(TEXT("GlimmerCape"), TEXT("Glimmer Cape"), S::Top, FLinearColor(0.45f, 0.30f, 0.85f), { TEXT("Sparkle"), TEXT("Elegant") }),
		Piece(TEXT("ChunkyKnit"), TEXT("Chunky Knit"), S::Top, FLinearColor(0.80f, 0.70f, 0.55f), { TEXT("Cozy"), TEXT("Rain") }),
		Piece(TEXT("ScaleTee"), TEXT("Scale Tee"), S::Top, FLinearColor(0.30f, 0.75f, 0.45f), { TEXT("Dino"), TEXT("Sporty") }),
		// Bottoms
		Piece(TEXT("MeadowSkirt"), TEXT("Meadow Skirt"), S::Bottom, FLinearColor(0.95f, 0.85f, 0.45f), { TEXT("Floral"), TEXT("Cute") }),
		Piece(TEXT("CargoShorts"), TEXT("Cargo Shorts"), S::Bottom, FLinearColor(0.55f, 0.50f, 0.35f), { TEXT("Adventure"), TEXT("Practical") }),
		Piece(TEXT("NebulaTrousers"), TEXT("Nebula Trousers"), S::Bottom, FLinearColor(0.20f, 0.15f, 0.45f), { TEXT("Sparkle"), TEXT("Elegant") }),
		Piece(TEXT("PuddleOveralls"), TEXT("Puddle Overalls"), S::Bottom, FLinearColor(0.30f, 0.50f, 0.80f), { TEXT("Rain"), TEXT("Cozy") }),
		Piece(TEXT("SpikeTailSkirt"), TEXT("Spike-Tail Skirt"), S::Bottom, FLinearColor(0.35f, 0.70f, 0.40f), { TEXT("Dino"), TEXT("Cute") }),
		// Shoes
		Piece(TEXT("BloomSandals"), TEXT("Bloom Sandals"), S::Shoes, FLinearColor(1.00f, 0.75f, 0.80f), { TEXT("Floral"), TEXT("Cute") }),
		Piece(TEXT("TrailBoots"), TEXT("Trail Boots"), S::Shoes, FLinearColor(0.45f, 0.30f, 0.20f), { TEXT("Adventure"), TEXT("Practical") }, false),
		Piece(TEXT("MoonSlippers"), TEXT("Moon Slippers"), S::Shoes, FLinearColor(0.90f, 0.90f, 0.95f), { TEXT("Sparkle"), TEXT("Elegant") }),
		Piece(TEXT("SplashBoots"), TEXT("Splash Boots"), S::Shoes, FLinearColor(1.00f, 0.85f, 0.20f), { TEXT("Rain"), TEXT("Cozy") }),
		Piece(TEXT("ClawSneakers"), TEXT("Claw Sneakers"), S::Shoes, FLinearColor(0.25f, 0.65f, 0.40f), { TEXT("Dino"), TEXT("Sporty") }),
		// Accessories
		Piece(TEXT("DaisyCrown"), TEXT("Daisy Crown"), S::Accessory, FLinearColor(1.00f, 1.00f, 0.85f), { TEXT("Floral"), TEXT("Cute"), TEXT("Nature") }),
		Piece(TEXT("FossilCompass"), TEXT("Fossil Compass"), S::Accessory, FLinearColor(0.70f, 0.60f, 0.40f), { TEXT("Adventure"), TEXT("Dino") }, false),
		Piece(TEXT("StardustSash"), TEXT("Stardust Sash"), S::Accessory, FLinearColor(0.75f, 0.55f, 1.00f), { TEXT("Sparkle"), TEXT("Elegant") }),
		Piece(TEXT("LeafBrolly"), TEXT("Leaf Brolly"), S::Accessory, FLinearColor(0.35f, 0.70f, 0.35f), { TEXT("Rain"), TEXT("Nature") }),
		Piece(TEXT("EggshellPack"), TEXT("Eggshell Pack"), S::Accessory, FLinearColor(0.95f, 0.93f, 0.85f), { TEXT("Dino"), TEXT("Cute") }),
	};
	return All;
}

const FGFFashionPiece* FGFFashionCatalog::FindPiece(FName Id)
{
	return Pieces().FindByPredicate([Id](const FGFFashionPiece& P) { return P.Id == Id; });
}

TArray<FName> FGFFashionCatalog::PiecesForSlot(EGFOutfitSlot Slot)
{
	TArray<FName> Out;
	for (const FGFFashionPiece& P : Pieces())
	{
		if (P.Slot == Slot)
		{
			Out.Add(P.Id);
		}
	}
	return Out;
}

const TArray<FGFFashionTheme>& FGFFashionCatalog::Themes()
{
	using namespace GFFashionCatalog;
	static const TArray<FGFFashionTheme> All = {
		Theme(TEXT("GardenParty"), TEXT("Garden Party"), { TEXT("Floral"), TEXT("Cute"), TEXT("Nature") }, { TEXT("Practical") }),
		Theme(TEXT("DinoExplorer"), TEXT("Dino Explorer"), { TEXT("Dino"), TEXT("Adventure"), TEXT("Practical") }, { TEXT("Elegant") }),
		Theme(TEXT("StarlightGala"), TEXT("Starlight Gala"), { TEXT("Sparkle"), TEXT("Elegant") }, { TEXT("Practical"), TEXT("Sporty") }),
		Theme(TEXT("RainyDayCozy"), TEXT("Rainy Day Cozy"), { TEXT("Rain"), TEXT("Cozy") }, { TEXT("Sparkle") }),
	};
	return All;
}

const FGFFashionTheme* FGFFashionCatalog::FindTheme(FName Id)
{
	return Themes().FindByPredicate([Id](const FGFFashionTheme& T) { return T.Id == Id; });
}

// ------------------------------------------------------------------------------------------------

FGFOutfit::FGFOutfit()
{
	Slots.SetNum(static_cast<int32>(EGFOutfitSlot::Count));
}

bool FGFOutfit::Equip(FName PieceId)
{
	const FGFFashionPiece* P = FGFFashionCatalog::FindPiece(PieceId);
	if (!P)
	{
		return false;
	}
	Slots.SetNum(static_cast<int32>(EGFOutfitSlot::Count));
	FGFOutfitSlotState& S = Slots[static_cast<int32>(P->Slot)];
	S = FGFOutfitSlotState();
	S.PieceId = PieceId;
	return true;
}

void FGFOutfit::Unequip(EGFOutfitSlot Slot)
{
	if (Slots.IsValidIndex(static_cast<int32>(Slot)))
	{
		Slots[static_cast<int32>(Slot)] = FGFOutfitSlotState();
	}
}

bool FGFOutfit::Dye(EGFOutfitSlot Slot, const FLinearColor& Color)
{
	const FGFFashionPiece* P = GetPiece(Slot);
	if (!P || !P->bDyeable || !FGFDyes::IsValidDyeColor(Color))
	{
		return false;
	}
	FGFOutfitSlotState& S = Slots[static_cast<int32>(Slot)];
	S.bDyed = true;
	S.DyeColor = Color;
	return true;
}

void FGFOutfit::ClearDye(EGFOutfitSlot Slot)
{
	if (Slots.IsValidIndex(static_cast<int32>(Slot)))
	{
		Slots[static_cast<int32>(Slot)].bDyed = false;
	}
}

bool FGFOutfit::Validate(FString* OutWhy) const
{
	auto Fail = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};
	if (Slots.Num() != static_cast<int32>(EGFOutfitSlot::Count))
	{
		return Fail(FString::Printf(TEXT("expected %d slots, got %d"), static_cast<int32>(EGFOutfitSlot::Count), Slots.Num()));
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FGFOutfitSlotState& S = Slots[i];
		if (S.PieceId.IsNone())
		{
			if (S.bDyed)
			{
				return Fail(FString::Printf(TEXT("slot %d is empty but dyed"), i));
			}
			continue;
		}
		const FGFFashionPiece* P = FGFFashionCatalog::FindPiece(S.PieceId);
		if (!P)
		{
			return Fail(FString::Printf(TEXT("unknown piece %s"), *S.PieceId.ToString()));
		}
		if (static_cast<int32>(P->Slot) != i)
		{
			return Fail(FString::Printf(TEXT("%s is in slot %d, not its own slot"), *S.PieceId.ToString(), i));
		}
		if (S.bDyed && !P->bDyeable)
		{
			return Fail(FString::Printf(TEXT("%s can't be dyed"), *S.PieceId.ToString()));
		}
		if (S.bDyed && !FGFDyes::IsValidDyeColor(S.DyeColor))
		{
			return Fail(FString::Printf(TEXT("bad dye colour on %s"), *S.PieceId.ToString()));
		}
	}
	return true;
}

TArray<FGFItemStack> FGFOutfit::DyeCostForSlot(EGFOutfitSlot Slot) const
{
	const int32 i = static_cast<int32>(Slot);
	if (!Slots.IsValidIndex(i) || !Slots[i].bDyed || Slots[i].PieceId.IsNone())
	{
		return {};
	}
	return FGFDyes::CostForOutfitPiece(Slots[i].DyeColor);
}

FName FGFOutfit::GetPieceId(EGFOutfitSlot Slot) const
{
	return Slots.IsValidIndex(static_cast<int32>(Slot)) ? Slots[static_cast<int32>(Slot)].PieceId : NAME_None;
}

const FGFFashionPiece* FGFOutfit::GetPiece(EGFOutfitSlot Slot) const
{
	const FName Id = GetPieceId(Slot);
	return Id.IsNone() ? nullptr : FGFFashionCatalog::FindPiece(Id);
}

bool FGFOutfit::GetColor(EGFOutfitSlot Slot, FLinearColor& Out) const
{
	const FGFFashionPiece* P = GetPiece(Slot);
	if (!P)
	{
		return false;
	}
	const FGFOutfitSlotState& S = Slots[static_cast<int32>(Slot)];
	Out = S.bDyed ? S.DyeColor : P->BaseColor;
	return true;
}

int32 FGFOutfit::NumFilled() const
{
	int32 N = 0;
	for (int32 i = 0; i < static_cast<int32>(EGFOutfitSlot::Count); ++i)
	{
		N += GetPiece(static_cast<EGFOutfitSlot>(i)) ? 1 : 0;
	}
	return N;
}

TArray<FName> FGFOutfit::GetPieceIds() const
{
	TArray<FName> Out;
	for (const FGFOutfitSlotState& S : Slots)
	{
		if (!S.PieceId.IsNone())
		{
			Out.Add(S.PieceId);
		}
	}
	return Out;
}
