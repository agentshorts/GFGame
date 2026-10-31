#include "GFFashionTypes.h"
#include "GFDyes.h"
#include "GFDataTables.h"
#include "GFFashionItems.h"
#include "GFLog.h"

namespace GFFashionCatalog
{
	static int32 GRowCount = 0;

	static void SplitBar(const FString& Text, TArray<FString>& Out)
	{
		Text.ParseIntoArray(Out, TEXT("|"), true);
		for (FString& Part : Out)
		{
			Part.TrimStartAndEndInline();
		}
		Out.RemoveAll([](const FString& Part) { return Part.IsEmpty(); });
	}

	static bool ParseColor(const FString& Text, FLinearColor& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() < 3)
		{
			return false;
		}
		Out = FLinearColor(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), 1.f);
		return true;
	}

	static bool ParseCost(const FString& Text, TArray<FGFItemStack>& Out)
	{
		Out.Reset();
		TArray<FString> Parts;
		SplitBar(Text, Parts);
		for (const FString& Part : Parts)
		{
			FString Id, Count;
			if (!Part.Split(TEXT(":"), &Id, &Count))
			{
				return false;
			}
			Id.TrimStartAndEndInline();
			const int32 N = FCString::Atoi(*Count);
			if (Id.IsEmpty() || N <= 0)
			{
				return false;
			}
			Out.Add(FGFItemStack(FName(*Id), N));
		}
		return true;
	}

	static EGFRarity ParseRarity(FName Name)
	{
		if (Name == TEXT("Uncommon")) { return EGFRarity::Uncommon; }
		if (Name == TEXT("Rare")) { return EGFRarity::Rare; }
		return EGFRarity::Common;
	}

	static TArray<FGFFashionPiece> LoadPieces()
	{
		TArray<FGFFashionPiece> Out;
		GRowCount = 0;
		const UDataTable* Table = FGFDataTables::Get<FGFFashionPieceRow>(TEXT("DT_FashionPieces"));
		if (!Table)
		{
			UE_LOG(LogGF, Error, TEXT("Fashion: DT_FashionPieces failed to load"));
			return Out;
		}
		Table->ForeachRow<FGFFashionPieceRow>(TEXT("FashionCatalog"), [&](const FName& Id, const FGFFashionPieceRow& Row)
		{
			++GRowCount;
			EGFOutfitSlot Slot = EGFOutfitSlot::Count;
			if (Id.IsNone() || !FGFFashionItems::SlotFromRowName(Row.Slot, Slot))
			{
				UE_LOG(LogGF, Error, TEXT("Fashion: %s has no usable slot (%s)"), *Id.ToString(), *Row.Slot.ToString());
				return;
			}
			FGFFashionPiece P;
			P.Id = Id;
			P.DisplayName = Row.DisplayName;
			P.Slot = Slot;
			P.SetId = Row.SetId;
			P.Rarity = ParseRarity(Row.Rarity);
			P.bDyeable = Row.bDyeable;
			P.DyeChannels = Row.DyeChannels;
			P.bStarter = Row.bStarter;
			P.SourceLine = Row.SourceLine;
			P.SourceContext = Row.SourceContext;
			if (!ParseColor(Row.BaseColor, P.BaseColor))
			{
				P.BaseColor = FLinearColor::White;
			}
			TArray<FString> Tags;
			SplitBar(Row.Tags, Tags);
			for (const FString& Tag : Tags)
			{
				P.Tags.Add(FName(*Tag));
			}
			TArray<FString> Sources;
			SplitBar(Row.Sources, Sources);
			for (const FString& Source : Sources)
			{
				P.Sources.Add(FName(*Source));
			}
			if (!Row.CraftCost.IsEmpty() && !ParseCost(Row.CraftCost, P.CraftCost))
			{
				P.CraftCost.Reset();
				UE_LOG(LogGF, Error, TEXT("Fashion: %s has a craft cost that did not parse (%s)"), *Id.ToString(), *Row.CraftCost);
			}
			if (Out.ContainsByPredicate([&Id](const FGFFashionPiece& Have) { return Have.Id == Id; }))
			{
				UE_LOG(LogGF, Error, TEXT("Fashion: duplicate piece %s"), *Id.ToString());
				return;
			}
			Out.Add(MoveTemp(P));
		});
		return Out;
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
	static const TArray<FGFFashionPiece> All = GFFashionCatalog::LoadPieces();
	return All;
}

int32 FGFFashionCatalog::RowCount()
{
	Pieces();
	return GFFashionCatalog::GRowCount;
}

const FGFFashionPiece* FGFFashionCatalog::StarterForSlot(EGFOutfitSlot Slot)
{
	for (const FGFFashionPiece& P : Pieces())
	{
		if (P.bStarter && P.Slot == Slot)
		{
			return &P;
		}
	}
	return nullptr;
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
