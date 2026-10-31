// Fashion closet (#134): every brand-list item has an Outfit row in DT_Items.
// Tier 0 and value 0, like LabCoat, so a sewn piece is not a currency.
// OutfitSlot names an EGFOutfitSlot. Rows are written by Tools/gen_fashion_items.py.

#include "Misc/AutomationTest.h"
#include "GFFashionItems.h"
#include "GFItemTypes.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFFashionItemTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFFashionItemTest
{
	struct FPiece
	{
		FName Id;
		FString Type;
		FString Name;
	};

	static TArray<FPiece> LoadPieces(FString& OutError)
	{
		TArray<FPiece> Out;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/fashion/fashion_brands_v1.json"));
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			OutError = TEXT("cannot read ") + Path;
			return Out;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("brand list is not valid JSON");
			return Out;
		}
		const TArray<TSharedPtr<FJsonValue>>* Brands = nullptr;
		if (!Root->TryGetArrayField(TEXT("brands"), Brands) || !Brands)
		{
			OutError = TEXT("brand list has no brands array");
			return Out;
		}
		for (int32 b = 0; b < Brands->Num(); ++b)
		{
			const TSharedPtr<FJsonObject>* Brand = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* BrandItems = nullptr;
			if (!(*Brands)[b].IsValid() || !(*Brands)[b]->TryGetObject(Brand) || !Brand || !Brand->IsValid()
				|| !(*Brand)->TryGetArrayField(TEXT("items"), BrandItems) || !BrandItems)
			{
				OutError = FString::Printf(TEXT("brand %d is not an object with an items array"), b);
				continue;
			}
			for (int32 i = 0; i < BrandItems->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* Item = nullptr;
				FString Id, Type, Name;
				if (!(*BrandItems)[i].IsValid() || !(*BrandItems)[i]->TryGetObject(Item) || !Item || !Item->IsValid()
					|| !(*Item)->TryGetStringField(TEXT("id"), Id) || !(*Item)->TryGetStringField(TEXT("type"), Type)
					|| !(*Item)->TryGetStringField(TEXT("name"), Name) || Id.IsEmpty())
				{
					OutError = FString::Printf(TEXT("brand %d item %d has no id/type/name"), b, i);
					continue;
				}
				FPiece Piece;
				Piece.Id = FName(*Id);
				Piece.Type = Type;
				Piece.Name = Name;
				Out.Add(Piece);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionItemRowsTest, "GFGame.Fashion.Items.Rows", GFFashionItemTestFlags)
bool FGFFashionItemRowsTest::RunTest(const FString&)
{
	using namespace GFFashionItemTest;
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	for (const FString& Problem : FGFEconomy::ValidateItemTable(Items))
	{
		AddError(Problem);
	}

	FString Error;
	const TArray<FPiece> Pieces = LoadPieces(Error);
	TestTrue(*(TEXT("Brand list loads: ") + Error), Error.IsEmpty());
	if (!TestEqual(TEXT("v1 ships 116 fashion items"), Pieces.Num(), 116))
	{
		return false;
	}

	TSet<FName> FashionIds;
	for (const FPiece& Piece : Pieces)
	{
		TestFalse(*FString::Printf(TEXT("%s is listed once"), *Piece.Id.ToString()), FashionIds.Contains(Piece.Id));
		FashionIds.Add(Piece.Id);

		const FGFItemRow* Row = Items->FindRow<FGFItemRow>(Piece.Id, TEXT("FashionItems"), false);
		if (!TestNotNull(*FString::Printf(TEXT("%s has a DT_Items row"), *Piece.Id.ToString()), Row))
		{
			continue;
		}
		const FString What = Piece.Id.ToString();
		TestEqual(*(What + TEXT(" is an outfit")), Row->Category, EGFItemCategory::Outfit);
		TestEqual(*(What + TEXT(" does not stack")), Row->StackMax, 1);
		TestEqual(*(What + TEXT(" tier")), Row->Tier, 0);
		TestEqual(*(What + TEXT(" value")), Row->Value, 0);
		TestEqual(*(What + TEXT(" material category")), Row->MaterialCategory, EGFMaterialCategory::None);
		TestFalse(*(What + TEXT(" wagerable")), Row->bWagerable);
		TestFalse(*(What + TEXT(" rentable")), Row->bRentable);
		TestEqual(*(What + TEXT(" exclusive")), Row->Exclusive, EGFExclusive::None);
		TestFalse(*(What + TEXT(" account-bound")), Row->bAccountBound);
		TestFalse(*(What + TEXT(" display name is empty")), Row->DisplayName.IsEmpty());
		TestFalse(*(What + TEXT(" source tag")), Row->SourceTag.IsValid());

		EGFOutfitSlot FromType = EGFOutfitSlot::Top;
		const bool bTypeKnown = FGFFashionItems::SlotForItemType(Piece.Type, FromType);
		TestTrue(*(What + TEXT(" type ") + Piece.Type + TEXT(" maps to a slot")), bTypeKnown);
		EGFOutfitSlot FromRow = EGFOutfitSlot::Top;
		const bool bRowKnown = FGFFashionItems::SlotFromRowName(Row->OutfitSlot, FromRow);
		TestTrue(*(What + TEXT(" OutfitSlot ") + Row->OutfitSlot.ToString() + TEXT(" is an EGFOutfitSlot")), bRowKnown);
		if (bTypeKnown && bRowKnown)
		{
			TestEqual(*(What + TEXT(" hangs on the slot for ") + Piece.Type), FromRow, FromType);
			TestEqual(*(What + TEXT(" slot token")), Row->OutfitSlot, FGFFashionItems::RowNameForSlot(FromType));
		}
	}

	int32 Other = 0;
	Items->ForeachRow<FGFItemRow>(TEXT("FashionItems"), [&](const FName& Id, const FGFItemRow& Row)
	{
		if (FashionIds.Contains(Id))
		{
			return;
		}
		++Other;
		if (Row.Category == EGFItemCategory::Outfit)
		{
			EGFOutfitSlot Slot = EGFOutfitSlot::Top;
			TestTrue(*FString::Printf(TEXT("%s is an outfit with a real slot"), *Id.ToString()), FGFFashionItems::SlotFromRowName(Row.OutfitSlot, Slot));
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("%s is not a wearable"), *Id.ToString()), Row.OutfitSlot.IsNone());
		}
	});
	TestEqual(TEXT("The rows that were already in DT_Items are still there"), Other, 72);

	const FGFItemRow* Coat = Items->FindRow<FGFItemRow>(TEXT("LabCoat"), TEXT("FashionItems"), false);
	const FGFItemRow* Goggles = Items->FindRow<FGFItemRow>(TEXT("SafetyGoggles"), TEXT("FashionItems"), false);
	const FGFItemRow* Wood = Items->FindRow<FGFItemRow>(TEXT("Wood"), TEXT("FashionItems"), false);
	const FGFItemRow* Pass = Items->FindRow<FGFItemRow>(TEXT("FerryPass"), TEXT("FashionItems"), false);
	if (TestNotNull(TEXT("LabCoat"), Coat))
	{
		TestEqual(TEXT("LabCoat stays tier 0"), Coat->Tier, 0);
		TestEqual(TEXT("LabCoat stays value 0"), Coat->Value, 0);
		TestEqual(TEXT("LabCoat hangs on Top"), Coat->OutfitSlot, FName(TEXT("Top")));
		TestFalse(TEXT("LabCoat stays out of wagers"), Coat->bWagerable);
	}
	if (TestNotNull(TEXT("SafetyGoggles"), Goggles))
	{
		TestEqual(TEXT("Safety goggles hang on Accessory"), Goggles->OutfitSlot, FName(TEXT("Accessory")));
	}
	if (TestNotNull(TEXT("Wood"), Wood))
	{
		TestEqual(TEXT("Wood stays tier 1"), Wood->Tier, 1);
		TestTrue(TEXT("Wood is not a wearable"), Wood->OutfitSlot.IsNone());
	}
	if (TestNotNull(TEXT("FerryPass"), Pass))
	{
		TestTrue(TEXT("FerryPass stays account-bound"), Pass->bAccountBound);
		TestTrue(TEXT("FerryPass is not a wearable"), Pass->OutfitSlot.IsNone());
	}
	return true;
}

#endif
