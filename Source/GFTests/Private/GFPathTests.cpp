// Paths: five classes, open-world filter, one free respec, mixed tier-3 recipes, endgame gate of three.
#include "Misc/AutomationTest.h"
#include "GFPathRules.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "GFItemTypes.h"
#include "GFModeTypes.h"
#include "GFRecipeTypes.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFPathTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace
{
	const UDataTable* PathItems()
	{
		return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	}

	const UDataTable* PathRecipes()
	{
		return FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	}

	EGFPath TagOf(const UDataTable* Items, const TCHAR* Id)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Id);
		return Row ? Row->PathTag : EGFPath::None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathCardsTest, "GFGame.Paths.Cards", GFPathTestFlags)
bool FGFPathCardsTest::RunTest(const FString&)
{
	TestEqual(TEXT("Five classes"), FGFPathRules::ClassCount, 5);
	TestEqual(TEXT("Endgame asks for three"), FGFPathRules::EndgamePaths, 3);
	TestEqual(TEXT("Slot count matches"), FGFPathRules::SlotCount(), 5);

	const TArray<FGFPathCard> Cards = FGFPathRules::Cards();
	TestEqual(TEXT("Five cards"), Cards.Num(), 5);
	int32 Pending = 0;
	for (int32 Slot = 0; Slot < Cards.Num(); ++Slot)
	{
		EGFPath Path = EGFPath::None;
		TestTrue(*FString::Printf(TEXT("Slot %d"), Slot), FGFPathRules::PathFromSlot(Slot, Path));
		TestEqual(*FString::Printf(TEXT("Card %d path"), Slot), static_cast<int32>(Cards[Slot].Path), static_cast<int32>(Path));
		TestEqual(*FString::Printf(TEXT("Card %d slot"), Slot), FGFPathRules::SlotOf(Path), Slot);
		TestFalse(*FString::Printf(TEXT("Card %d has an icon"), Slot), Cards[Slot].IconId.IsEmpty());
		if (Cards[Slot].bNamePending)
		{
			++Pending;
			TestEqual(TEXT("Unnamed card"), Cards[Slot].Name, FString(TEXT("Name pending")));
			TestFalse(TEXT("Unnamed is not a named class"), FGFPathRules::IsNamed(Path));
		}
	}
	TestEqual(TEXT("Two names still pending"), Pending, 2);
	TestEqual(TEXT("Delver"), FGFPathRules::DisplayName(EGFPath::Delver), FString(TEXT("Delver")));
	TestEqual(TEXT("Wrangler"), FGFPathRules::DisplayName(EGFPath::Wrangler), FString(TEXT("Wrangler")));
	TestEqual(TEXT("Scavenger"), FGFPathRules::DisplayName(EGFPath::Scavenger), FString(TEXT("Scavenger")));
	TestTrue(TEXT("Delver, Wrangler and Scavenger are named"), FGFPathRules::IsNamed(EGFPath::Delver) && FGFPathRules::IsNamed(EGFPath::Wrangler) && FGFPathRules::IsNamed(EGFPath::Scavenger));
	TestFalse(TEXT("None is not playable"), FGFPathRules::IsPlayable(EGFPath::None));
	EGFPath Unused = EGFPath::Delver;
	TestFalse(TEXT("No sixth slot"), FGFPathRules::PathFromSlot(5, Unused));
	TestEqual(TEXT("Failed slot leaves the out path"), static_cast<int32>(Unused), static_cast<int32>(EGFPath::Delver));

	TestFalse(TEXT("Delver blurb does not promise a crew slot"), Cards[0].Blurb.Contains(TEXT("crew")));
	TestFalse(TEXT("Wrangler blurb does not promise a cart"), Cards[1].Blurb.Contains(TEXT("cart")));
	TestTrue(TEXT("Scavenger blurb is about glints"), Cards[2].Blurb.Contains(TEXT("glint")));
	TestFalse(TEXT("Scavenger blurb does not mention speed"), Cards[2].Blurb.Contains(TEXT("speed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathPerksTest, "GFGame.Paths.Perks", GFPathTestFlags)
bool FGFPathPerksTest::RunTest(const FString&)
{
	const EGFPath Paths[] = { EGFPath::None, EGFPath::Delver, EGFPath::Wrangler, EGFPath::Scavenger, EGFPath::UnnamedFourth, EGFPath::UnnamedFifth };
	for (EGFPath Path : Paths)
	{
		TestEqual(*FString::Printf(TEXT("%d run speed"), static_cast<int32>(Path)), FGFPathRules::MoveSpeedMultiplier(Path), 1.f);
		TestEqual(*FString::Printf(TEXT("%d crew slots"), static_cast<int32>(Path)), FGFPathRules::CrewSlotBonus(Path), 0);
		TestEqual(*FString::Printf(TEXT("%d cart bonus"), static_cast<int32>(Path)), FGFPathRules::CartBonus(Path), 0.f);
	}
	TestEqual(TEXT("Delver dig speed"), FGFPathRules::DigSpeedMultiplier(EGFPath::Delver), 1.25f);
	TestEqual(TEXT("Delver depth"), FGFPathRules::ExtraDigLayers(EGFPath::Delver), 2);
	TestEqual(TEXT("Delver hazard"), FGFPathRules::HazardDamageMultiplier(EGFPath::Delver), 0.5f);
	TestEqual(TEXT("Other dig speed"), FGFPathRules::DigSpeedMultiplier(EGFPath::Scavenger), 1.f);
	TestEqual(TEXT("Other depth"), FGFPathRules::ExtraDigLayers(EGFPath::Wrangler), 0);
	TestEqual(TEXT("Other hazard"), FGFPathRules::HazardDamageMultiplier(EGFPath::None), 1.f);

	TestEqual(TEXT("Wrangler food"), FGFPathRules::TameFoodMultiplier(EGFPath::Wrangler), 0.7f);
	TestEqual(TEXT("Wrangler time"), FGFPathRules::TameTimeMultiplier(EGFPath::Wrangler), 0.7f);
	TestEqual(TEXT("Wrangler warning"), FGFPathRules::WarningTimeMultiplier(EGFPath::Wrangler), 1.5f);
	TestEqual(TEXT("Wrangler rent"), FGFPathRules::RentMultiplier(EGFPath::Wrangler), 0.8f);
	TestEqual(TEXT("Wrangler rep"), FGFPathRules::RepGainMultiplier(EGFPath::Wrangler), 1.25f);
	TestEqual(TEXT("Other tame food"), FGFPathRules::TameFoodMultiplier(EGFPath::Delver), 1.f);
	TestEqual(TEXT("Other warning"), FGFPathRules::WarningTimeMultiplier(EGFPath::Scavenger), 1.f);

	TestEqual(TEXT("Scavenger glints"), FGFPathRules::GlintRangeMultiplier(EGFPath::Scavenger), 2.f);
	TestEqual(TEXT("Other glints"), FGFPathRules::GlintRangeMultiplier(EGFPath::Wrangler), 1.f);
	TestEqual(TEXT("Off-path rate"), FGFPathRules::OffPathRate, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathItemTagsTest, "GFGame.Paths.ItemTags", GFPathTestFlags)
bool FGFPathItemTagsTest::RunTest(const FString&)
{
	const UDataTable* Items = PathItems();
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	for (const FString& Problem : FGFEconomy::ValidateItemTable(Items))
	{
		AddError(Problem);
	}
	struct FTagged { const TCHAR* Id; EGFPath Path; };
	const FTagged Tagged[] = {
		{ TEXT("Crystal"), EGFPath::Delver }, { TEXT("PrismShard"), EGFPath::Delver }, { TEXT("Amber"), EGFPath::Delver }, { TEXT("Fossil"), EGFPath::Delver },
		{ TEXT("Scales"), EGFPath::Wrangler }, { TEXT("Bone"), EGFPath::Wrangler }, { TEXT("WildEgg"), EGFPath::Wrangler }, { TEXT("Hide"), EGFPath::Wrangler }, { TEXT("EggShell"), EGFPath::Wrangler },
		{ TEXT("ScrapWire"), EGFPath::Scavenger }, { TEXT("BentPipe"), EGFPath::Scavenger }, { TEXT("CrackedLens"), EGFPath::Scavenger },
		{ TEXT("Fabric"), EGFPath::Scavenger }, { TEXT("Ribbon"), EGFPath::Scavenger }, { TEXT("Glitter"), EGFPath::Scavenger }, { TEXT("RareBloom"), EGFPath::Scavenger },
		{ TEXT("RareRoseDye"), EGFPath::Scavenger }, { TEXT("RareSunDye"), EGFPath::Scavenger }, { TEXT("RareFernDye"), EGFPath::Scavenger },
		{ TEXT("RareSkyDye"), EGFPath::Scavenger }, { TEXT("RarePlumDye"), EGFPath::Scavenger }, { TEXT("RareChalkDye"), EGFPath::Scavenger },
		{ TEXT("FourthFind"), EGFPath::UnnamedFourth }, { TEXT("FifthFind"), EGFPath::UnnamedFifth },
	};
	for (const FTagged& Pair : Tagged)
	{
		TestEqual(*FString::Printf(TEXT("%s path"), Pair.Id), static_cast<int32>(TagOf(Items, Pair.Id)), static_cast<int32>(Pair.Path));
		TestTrue(*FString::Printf(TEXT("%s can be held off-path"), Pair.Id), FGFPathRules::CanHold(EGFPath::Delver, Pair.Path));
		TestTrue(*FString::Printf(TEXT("%s can be given away"), Pair.Id), FGFPathRules::CanTransfer(EGFPath::Scavenger, Pair.Path));
	}
	for (const TCHAR* Shared : { TEXT("Wood"), TEXT("Stone"), TEXT("RoseDye"), TEXT("SunDye"), TEXT("FernDye"), TEXT("SkyDye"), TEXT("PlumDye"), TEXT("ChalkDye"),
		TEXT("Moonstone"), TEXT("PolymerGel"), TEXT("AnomalyShard"), TEXT("BigGrumbleTame"), TEXT("CalmingMist"), TEXT("Chair"), TEXT("LabCoat") })
	{
		TestEqual(*FString::Printf(TEXT("%s stays untagged"), Shared), static_cast<int32>(TagOf(Items, Shared)), static_cast<int32>(EGFPath::None));
	}
	const FGFItemRow* Hide = FGFEconomy::FindItem(Items, TEXT("Hide"));
	const FGFItemRow* Fourth = FGFEconomy::FindItem(Items, TEXT("FourthFind"));
	TestTrue(TEXT("New finds use the same empty icon as the rest of the table"), Hide && Fourth && Hide->Icon.IsNull() && Fourth->Icon.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathLootTest, "GFGame.Paths.Loot", GFPathTestFlags)
bool FGFPathLootTest::RunTest(const FString&)
{
	TestEqual(TEXT("On path"), FGFPathRules::KeepChance(EGFPath::Wrangler, EGFPath::Wrangler, EGFPathLootSource::OpenWorld), 1.f);
	TestEqual(TEXT("Untagged"), FGFPathRules::KeepChance(EGFPath::Delver, EGFPath::None, EGFPathLootSource::OpenWorld), 1.f);
	TestEqual(TEXT("Off path"), FGFPathRules::KeepChance(EGFPath::Delver, EGFPath::Wrangler, EGFPathLootSource::OpenWorld), 0.05f);
	TestEqual(TEXT("Unpicked is off path for a tagged find"), FGFPathRules::KeepChance(EGFPath::None, EGFPath::Delver, EGFPathLootSource::OpenWorld), 0.05f);
	TestEqual(TEXT("Pebble Mine ignores the tag"), FGFPathRules::KeepChance(EGFPath::Scavenger, EGFPath::Delver, EGFPathLootSource::PebbleMine), 1.f);
	TestEqual(TEXT("Mode exclusive ignores the tag"), FGFPathRules::KeepChance(EGFPath::Wrangler, EGFPath::Scavenger, EGFPathLootSource::ModeExclusive), 1.f);
	TestEqual(TEXT("Garden dye ignores the tag"), FGFPathRules::KeepChance(EGFPath::Delver, EGFPath::Scavenger, EGFPathLootSource::Garden), 1.f);

	auto Count = [](EGFPath Finder, EGFPath Item, EGFPathLootSource Source, int32 Seed)
	{
		FRandomStream Rng(Seed);
		int32 Kept = 0;
		for (int32 i = 0; i < 10000; ++i)
		{
			Kept += FGFPathRules::KeepsDrop(Finder, Item, Source, Rng) ? 1 : 0;
		}
		return Kept;
	};
	TestEqual(TEXT("On-path open world, 10000"), Count(EGFPath::Wrangler, EGFPath::Wrangler, EGFPathLootSource::OpenWorld, 164), 10000);
	TestEqual(TEXT("Wood, 10000"), Count(EGFPath::Scavenger, EGFPath::None, EGFPathLootSource::OpenWorld, 164), 10000);
	TestEqual(TEXT("Pebble Mine crystal for a scavenger, 10000"), Count(EGFPath::Scavenger, EGFPath::Delver, EGFPathLootSource::PebbleMine, 164), 10000);
	TestEqual(TEXT("Moonstone-style exclusive, 10000"), Count(EGFPath::Delver, EGFPath::Scavenger, EGFPathLootSource::ModeExclusive, 164), 10000);
	TestEqual(TEXT("Garden dye, 10000"), Count(EGFPath::Wrangler, EGFPath::Delver, EGFPathLootSource::Garden, 164), 10000);
	const int32 Off = Count(EGFPath::Delver, EGFPath::Wrangler, EGFPathLootSource::OpenWorld, 164);
	AddInfo(FString::Printf(TEXT("Off-path keeps out of 10000, seed 164: %d"), Off));
	TestTrue(TEXT("Off-path rate lands near 5% (350 to 650)"), Off >= 350 && Off <= 650);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathRespecTest, "GFGame.Paths.Respec", GFPathTestFlags)
bool FGFPathRespecTest::RunTest(const FString&)
{
	const UDataTable* Items = PathItems();
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	FGFPathRecord Record;
	const int64 Now = 1'700'000'000;
	FGFPathChange First = FGFPathRules::Evaluate(Record, EGFPath::Delver, Now, 0);
	TestTrue(TEXT("First pick is free"), First.bOk && First.CostValue == 0 && First.Code == TEXT("Ok"));
	TestFalse(TEXT("None is not a class"), FGFPathRules::Evaluate(Record, EGFPath::None, Now, 0).bOk);
	TestEqual(TEXT("Bad path"), FGFPathRules::Evaluate(Record, EGFPath::None, Now, 0).Code, FName(TEXT("BadPath")));
	FGFPathRules::Commit(Record, EGFPath::Delver, Now);
	TestTrue(TEXT("Chosen"), Record.bChosen && Record.Path == EGFPath::Delver);
	TestEqual(TEXT("First pick is not a respec"), Record.Respecs, 0);
	TestEqual(TEXT("No cooldown stamp yet"), Record.RespecUnix, static_cast<int64>(0));

	TestEqual(TEXT("Same class"), FGFPathRules::Evaluate(Record, EGFPath::Delver, Now, 999).Code, FName(TEXT("SamePath")));
	FGFPathChange Free = FGFPathRules::Evaluate(Record, EGFPath::Wrangler, Now, 0);
	TestTrue(TEXT("The one free respec needs no materials"), Free.bOk && Free.CostValue == 0);
	FGFPathRules::Commit(Record, EGFPath::Wrangler, Now);
	TestEqual(TEXT("Free respec counted"), Record.Respecs, 1);
	TestEqual(TEXT("Cooldown starts"), Record.RespecUnix, Now);

	FGFPathChange Early = FGFPathRules::Evaluate(Record, EGFPath::Scavenger, Now + 1, 999);
	TestFalse(TEXT("Next day has not arrived"), Early.bOk);
	TestEqual(TEXT("Cooldown"), Early.Code, FName(TEXT("Cooldown")));
	const int64 Later = Now + FGFPathRules::RespecCooldownSeconds;
	FGFPathChange Short = FGFPathRules::Evaluate(Record, EGFPath::Scavenger, Later, FGFPathRules::RespecCostValue - 1);
	TestEqual(TEXT("Short of the pile"), Short.Code, FName(TEXT("Cost")));
	FGFPathChange Priced = FGFPathRules::Evaluate(Record, EGFPath::Scavenger, Later, FGFPathRules::RespecCostValue);
	TestTrue(TEXT("After a day, the pile covers it"), Priced.bOk && Priced.CostValue == FGFPathRules::RespecCostValue);

	FGFInventory Bag;
	Bag.Add(TEXT("FerryPass"), 5);
	Bag.Add(TEXT("Chair"), 9);
	Bag.Add(TEXT("LabCoat"), 3);
	Bag.Add(TEXT("Stone"), 1);
	Bag.Add(TEXT("Wood"), 1);
	Bag.Add(TEXT("Fossil"), 1);
	TestEqual(TEXT("Account-bound, furniture and outfits do not pay"), FGFPathRules::TradeableValue(Items, Bag), 1 + 1 + 20);
	TestFalse(TEXT("39 short"), FGFPathRules::PayValue(Items, Bag, 39));
	TestEqual(TEXT("Short pay leaves the bag"), Bag.GetCount(TEXT("Wood")), 1);
	TestTrue(TEXT("Cheapest first, then name"), FGFPathRules::PayValue(Items, Bag, 1));
	TestEqual(TEXT("Stone goes before Wood"), Bag.GetCount(TEXT("Stone")), 0);
	TestEqual(TEXT("Wood remains"), Bag.GetCount(TEXT("Wood")), 1);
	TestEqual(TEXT("Fossil remains"), Bag.GetCount(TEXT("Fossil")), 1);
	TestTrue(TEXT("Zero cost is a no-op"), FGFPathRules::PayValue(Items, Bag, 0));
	TestEqual(TEXT("Still one wood"), Bag.GetCount(TEXT("Wood")), 1);

	FGFInventory Pile;
	Pile.Add(TEXT("Wood"), 50);
	Pile.Add(TEXT("Fossil"), 1);
	TestTrue(TEXT("Forty wood covers the respec"), FGFPathRules::PayValue(Items, Pile, 40));
	TestEqual(TEXT("Ten wood left"), Pile.GetCount(TEXT("Wood")), 10);
	TestEqual(TEXT("Fossil was not the cheap option"), Pile.GetCount(TEXT("Fossil")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathSaveTest, "GFGame.Paths.Save", GFPathTestFlags)
bool FGFPathSaveTest::RunTest(const FString&)
{
	FGFPlayerProgress Progress;
	Progress.PinnedRecipe = TEXT("Chair");
	Progress.bWagersOff = true;
	Progress.ArenaMatches = 4;
	Progress.bHasTamed = true;
	Progress.DepthsReached = 2;
	Progress.UnlockedShardstones = { TEXT("Hub"), TEXT("Flats") };
	Progress.Path = EGFPath::UnnamedFifth;
	Progress.bPathChosen = true;
	Progress.PathRespecs = 1;
	Progress.PathRespecUnix = 1'700'000'000;

	TArray<uint8> Bytes;
	FGFModeBytes::Write(Progress, Bytes);
	TestTrue(TEXT("Blob is not empty"), Bytes.Num() > 0);
	FGFPlayerProgress Loaded;
	TestTrue(TEXT("Blob reads back"), FGFModeBytes::Read(Loaded, Bytes));
	TestEqual(TEXT("Pinned recipe"), Loaded.PinnedRecipe, Progress.PinnedRecipe);
	TestTrue(TEXT("Wagers setting"), Loaded.bWagersOff);
	TestEqual(TEXT("Arena matches"), Loaded.ArenaMatches, 4);
	TestTrue(TEXT("First tame"), Loaded.bHasTamed);
	TestEqual(TEXT("Depths"), Loaded.DepthsReached, 2);
	TestEqual(TEXT("Shardstones"), Loaded.UnlockedShardstones.Num(), 2);
	TestEqual(TEXT("Path"), static_cast<int32>(Loaded.Path), static_cast<int32>(EGFPath::UnnamedFifth));
	TestTrue(TEXT("Chosen flag"), Loaded.bPathChosen);
	TestEqual(TEXT("Respecs"), Loaded.PathRespecs, 1);
	TestEqual(TEXT("Respec clock"), Loaded.PathRespecUnix, Progress.PathRespecUnix);

	FGFPlayerProgress Fresh;
	TArray<uint8> FreshBytes;
	FGFModeBytes::Write(Fresh, FreshBytes);
	FGFPlayerProgress FreshLoaded;
	TestTrue(TEXT("A new character reads back"), FGFModeBytes::Read(FreshLoaded, FreshBytes));
	TestEqual(TEXT("New character has no class"), static_cast<int32>(FreshLoaded.Path), static_cast<int32>(EGFPath::None));
	TestFalse(TEXT("New character has not picked"), FreshLoaded.bPathChosen);
	TestEqual(TEXT("New character has not respecced"), FreshLoaded.PathRespecs, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathRecipeMixTest, "GFGame.Paths.RecipeMix", GFPathTestFlags)
bool FGFPathRecipeMixTest::RunTest(const FString&)
{
	const UDataTable* Items = PathItems();
	const UDataTable* Recipes = PathRecipes();
	if (!TestNotNull(TEXT("DT_Items"), Items) || !TestNotNull(TEXT("DT_Recipes"), Recipes))
	{
		return false;
	}
	int32 Tier3 = 0;
	int32 Mixed = 0;
	Recipes->ForeachRow<FGFRecipeRow>(TEXT("Test"), [&](const FName& Id, const FGFRecipeRow& Recipe)
	{
		int32 Highest = 0;
		TSet<EGFPath> Paths;
		for (const FGFItemStack& Input : Recipe.Inputs)
		{
			const FGFItemRow* Row = FGFEconomy::FindItem(Items, Input.ItemId);
			if (!Row)
			{
				AddError(FString::Printf(TEXT("%s input %s is not in DT_Items"), *Id.ToString(), *Input.ItemId.ToString()));
				return;
			}
			Highest = FMath::Max(Highest, Row->Tier);
			if (Row->PathTag != EGFPath::None)
			{
				Paths.Add(Row->PathTag);
			}
		}
		if (const FGFItemRow* Out = FGFEconomy::FindItem(Items, Recipe.Output.ItemId))
		{
			Highest = FMath::Max(Highest, Out->Tier);
		}
		if (Highest < 3)
		{
			return;
		}
		++Tier3;
		if (Paths.Num() >= 2)
		{
			++Mixed;
		}
	});
	AddInfo(FString::Printf(TEXT("Tier 3+ recipes %d, mixed %d"), Tier3, Mixed));
	TestTrue(TEXT("There are tier 3+ recipes"), Tier3 > 0);
	TestTrue(TEXT("At least half mix two or more paths"), Mixed * 2 >= Tier3);

	auto MixedInputs = [&](const TCHAR* Id)
	{
		const FGFRecipeRow* Recipe = Recipes->FindRow<FGFRecipeRow>(Id, TEXT("Test"), false);
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Id), Recipe))
		{
			return;
		}
		TSet<EGFPath> Paths;
		for (const FGFItemStack& Input : Recipe->Inputs)
		{
			const FGFItemRow* Row = FGFEconomy::FindItem(Items, Input.ItemId);
			if (Row && Row->PathTag != EGFPath::None)
			{
				Paths.Add(Row->PathTag);
			}
		}
		TestTrue(*FString::Printf(TEXT("%s mixes paths"), Id), Paths.Num() >= 2);
	};
	MixedInputs(TEXT("JerkyBites"));
	MixedInputs(TEXT("EmberRoast"));
	MixedInputs(TEXT("FishSkewer"));
	MixedInputs(TEXT("GrubJelly"));
	MixedInputs(TEXT("SmokedHaunch"));
	MixedInputs(TEXT("PetitCake"));
	MixedInputs(TEXT("LabTreat"));

	const FGFRecipeRow* Mist = Recipes->FindRow<FGFRecipeRow>(TEXT("CalmingMist"), TEXT("Test"), false);
	if (TestNotNull(TEXT("Calming Mist recipe"), Mist))
	{
		TestEqual(TEXT("Calming Mist inputs"), Mist->Inputs.Num(), 3);
		TestTrue(TEXT("Calming Mist still starts with bio-culture"), Mist->Inputs.Num() > 0 && Mist->Inputs[0].ItemId == TEXT("BioCulture"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFPathEndgameGateTest, "GFGame.Paths.EndgameGate", GFPathTestFlags)
bool FGFPathEndgameGateTest::RunTest(const FString&)
{
	const UDataTable* Items = PathItems();
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	TArray<FGFInventory> Two;
	Two.AddDefaulted(2);
	Two[0].Add(TEXT("Crystal"), 3);
	Two[1].Add(TEXT("Scales"), 2);
	Two[1].Add(TEXT("Wood"), 9);
	const TArray<FGFInventory> Before = Two;
	TestEqual(TEXT("Two paths"), FGFPathRules::DistinctPaths(Two, Items), 2);
	TestFalse(TEXT("Two is short of the gate"), FGFPathRules::MeetsEndgame(Two, Items));
	TestFalse(TEXT("Short spend fails"), FGFPathRules::SpendEndgame(Two, Items));
	TestEqual(TEXT("Crystal untouched"), Two[0].GetCount(TEXT("Crystal")), Before[0].GetCount(TEXT("Crystal")));
	TestEqual(TEXT("Scales untouched"), Two[1].GetCount(TEXT("Scales")), 2);

	TArray<FGFInventory> Party;
	Party.AddDefaulted(3);
	Party[0].Add(TEXT("Fossil"), 1);
	Party[1].Add(TEXT("Hide"), 1);
	Party[2].Add(TEXT("FourthFind"), 1);
	Party[2].Add(TEXT("Wood"), 4);
	TestTrue(TEXT("Three friends, three paths, no shared bag yet"), FGFPathRules::MeetsEndgame(Party, Items));
	TestTrue(TEXT("Spend pulls one from each bag"), FGFPathRules::SpendEndgame(Party, Items));
	TestEqual(TEXT("Delver item spent"), Party[0].GetCount(TEXT("Fossil")), 0);
	TestEqual(TEXT("Wrangler item spent"), Party[1].GetCount(TEXT("Hide")), 0);
	TestEqual(TEXT("Fourth class item spent"), Party[2].GetCount(TEXT("FourthFind")), 0);
	TestEqual(TEXT("Wood was not a path item"), Party[2].GetCount(TEXT("Wood")), 4);

	TArray<FGFInventory> All;
	All.AddDefaulted(1);
	All[0].Add(TEXT("Crystal"), 4);
	All[0].Add(TEXT("Scales"), 4);
	All[0].Add(TEXT("ScrapWire"), 4);
	All[0].Add(TEXT("FourthFind"), 4);
	All[0].Add(TEXT("FifthFind"), 4);
	TestEqual(TEXT("All five"), FGFPathRules::DistinctPaths(All[0].Items, Items), 5);
	TestTrue(TEXT("Spends the lowest three slots"), FGFPathRules::SpendEndgame(All, Items));
	TestEqual(TEXT("One crystal"), All[0].GetCount(TEXT("Crystal")), 3);
	TestEqual(TEXT("One scales"), All[0].GetCount(TEXT("Scales")), 3);
	TestEqual(TEXT("One scrap"), All[0].GetCount(TEXT("ScrapWire")), 3);
	TestEqual(TEXT("Fourth stays when three earlier paths were here"), All[0].GetCount(TEXT("FourthFind")), 4);
	TestEqual(TEXT("Fifth stays"), All[0].GetCount(TEXT("FifthFind")), 4);

	TArray<FGFInventory> Unnamed;
	Unnamed.AddDefaulted(2);
	Unnamed[0].Add(TEXT("Amber"), 1);
	Unnamed[0].Add(TEXT("FifthFind"), 2);
	Unnamed[1].Add(TEXT("FourthFind"), 2);
	TestTrue(TEXT("Delver plus the two unnamed classes is enough"), FGFPathRules::SpendEndgame(Unnamed, Items));
	TestEqual(TEXT("Amber spent"), Unnamed[0].GetCount(TEXT("Amber")), 0);
	TestEqual(TEXT("One fifth find left"), Unnamed[0].GetCount(TEXT("FifthFind")), 1);
	TestEqual(TEXT("One fourth find left"), Unnamed[1].GetCount(TEXT("FourthFind")), 1);
	return true;
}

#endif
