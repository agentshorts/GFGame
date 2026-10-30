// Fashion show (PLAN.md M3) tests: outfit slots, judging, rewards, and the runway show loop.
#include "Misc/AutomationTest.h"
#include "GFFashionTypes.h"
#include "GFRunwayJudge.h"
#include "GFRunwayShow.h"
#include "GFEconomy.h"
#include "GFDataTables.h"
#include "GFItemTypes.h"
#include "GFDyes.h"
#include "GFRunwayStage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFFashionTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFOutfit Make(std::initializer_list<const TCHAR*> Ids)
	{
		FGFOutfit O;
		for (const TCHAR* Id : Ids)
		{
			O.Equip(Id);
		}
		return O;
	}

	static FGFOutfit Garden() { return Make({ TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown") }); }
	static FGFOutfit Explorer() { return Make({ TEXT("MossMop"), TEXT("ExplorerVest"), TEXT("CargoShorts"), TEXT("TrailBoots"), TEXT("FossilCompass") }); }

	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFFashionTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionSlotsTest, "GFGame.Fashion.Slots", GFFashionTests::Flags)
bool FGFFashionSlotsTest::RunTest(const FString&)
{
	TestTrue(TEXT("Wardrobe has 25 pieces"), FGFFashionCatalog::Pieces().Num() >= 25);
	TSet<FName> Ids;
	for (const FGFFashionPiece& P : FGFFashionCatalog::Pieces())
	{
		TestFalse(*(P.Id.ToString() + TEXT(" is unique")), Ids.Contains(P.Id));
		Ids.Add(P.Id);
		TestTrue(*(P.Id.ToString() + TEXT(" has tags")), P.Tags.Num() > 0);
	}
	for (int32 s = 0; s < static_cast<int32>(EGFOutfitSlot::Count); ++s)
	{
		TestTrue(TEXT("Every slot has choices"), FGFFashionCatalog::PiecesForSlot(static_cast<EGFOutfitSlot>(s)).Num() >= 3);
	}
	TestTrue(TEXT("Four themes"), FGFFashionCatalog::Themes().Num() >= 4);

	FGFOutfit O;
	TestEqual(TEXT("Starts empty"), O.NumFilled(), 0);
	TestTrue(TEXT("Equip top"), O.Equip(TEXT("PetalBlouse")));
	TestTrue(TEXT("Top slot"), O.GetPieceId(EGFOutfitSlot::Top) == FName(TEXT("PetalBlouse")));
	TestFalse(TEXT("Unknown piece"), O.Equip(TEXT("NoSuchPiece")));
	TestTrue(TEXT("Dye a dyeable piece"), O.Dye(EGFOutfitSlot::Top, FLinearColor::Blue));
	FLinearColor C;
	TestTrue(TEXT("Dyed colour shows"), O.GetColor(EGFOutfitSlot::Top, C) && C.Equals(FLinearColor::Blue));
	TestTrue(TEXT("Swap top replaces it"), O.Equip(TEXT("ScaleTee")));
	TestEqual(TEXT("Still one piece"), O.NumFilled(), 1);
	TestTrue(TEXT("Swapping clears the dye"), O.GetColor(EGFOutfitSlot::Top, C) && C.Equals(FGFFashionCatalog::FindPiece(TEXT("ScaleTee"))->BaseColor));
	TestFalse(TEXT("Can't dye an empty slot"), O.Dye(EGFOutfitSlot::Shoes, FLinearColor::Red));
	TestTrue(TEXT("Equip boots"), O.Equip(TEXT("TrailBoots")));
	TestFalse(TEXT("Trail boots can't be dyed"), O.Dye(EGFOutfitSlot::Shoes, FLinearColor::Red));
	O.Unequip(EGFOutfitSlot::Top);
	TestEqual(TEXT("Unequip"), O.NumFilled(), 1);
	TestEqual(TEXT("Full outfit"), GFFashionTests::Garden().NumFilled(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionScoreTest, "GFGame.Fashion.Score", GFFashionTests::Flags)
bool FGFFashionScoreTest::RunTest(const FString&)
{
	const FGFFashionTheme* GardenParty = FGFFashionCatalog::FindTheme(TEXT("GardenParty"));
	const FGFFashionTheme* Gala = FGFFashionCatalog::FindTheme(TEXT("StarlightGala"));
	if (!TestNotNull(TEXT("Garden party theme"), GardenParty) || !TestNotNull(TEXT("Gala theme"), Gala))
	{
		return false;
	}
	const FGFOutfit Garden = GFFashionTests::Garden();
	const FGFOutfit Explorer = GFFashionTests::Explorer();
	TestEqual(TEXT("On-theme look maxes theme"), FGFRunwayJudge::ScoreTheme(Garden, *GardenParty), 10.f);
	TestTrue(TEXT("Banned tags tank the theme"), FGFRunwayJudge::ScoreTheme(Explorer, *GardenParty) < 2.f);
	TestTrue(TEXT("Off-theme look scores low on another theme"), FGFRunwayJudge::ScoreTheme(Garden, *Gala) < 5.f);
	TestEqual(TEXT("Complete"), FGFRunwayJudge::ScoreCompleteness(Garden), 10.f);
	TestEqual(TEXT("Partial"), FGFRunwayJudge::ScoreCompleteness(GFFashionTests::Make({ TEXT("PetalBlouse"), TEXT("MeadowSkirt") })), 4.f);

	// Harmony: matching hues beat a clash.
	FGFOutfit Matching = Garden;
	FGFOutfit Clash = Garden;
	const FLinearColor Pinks[] = { FLinearColor(1.f, 0.4f, 0.6f), FLinearColor(1.f, 0.5f, 0.6f), FLinearColor(0.9f, 0.4f, 0.5f), FLinearColor(1.f, 0.45f, 0.65f), FLinearColor(1.f, 0.35f, 0.55f) };
	const FLinearColor Mixed[] = { FLinearColor(1.f, 0.f, 0.f), FLinearColor(0.f, 1.f, 0.2f), FLinearColor(1.f, 0.f, 1.f), FLinearColor(1.f, 0.6f, 0.f), FLinearColor(0.f, 0.7f, 1.f) };
	for (int32 s = 0; s < 5; ++s)
	{
		Matching.Dye(static_cast<EGFOutfitSlot>(s), Pinks[s]);
		Clash.Dye(static_cast<EGFOutfitSlot>(s), Mixed[s]);
	}
	TestTrue(TEXT("Matching hues score high"), FGFRunwayJudge::ScoreHarmony(Matching) >= 9.f);
	TestTrue(TEXT("Matching beats a clash"), FGFRunwayJudge::ScoreHarmony(Matching) > FGFRunwayJudge::ScoreHarmony(Clash) + 2.f);

	// Novelty: repeating a past look is worth nothing; a fresh one is worth full marks.
	TestEqual(TEXT("New look is novel"), FGFRunwayJudge::ScoreNovelty(Garden, {}), 10.f);
	TestEqual(TEXT("Same look again is stale"), FGFRunwayJudge::ScoreNovelty(Garden, { Garden }), 0.f);
	TestEqual(TEXT("Different look is novel"), FGFRunwayJudge::ScoreNovelty(Garden, { Explorer }), 10.f);

	// Deterministic, with a small judge spread.
	const FGFRunwayScore A = FGFRunwayJudge::Judge(Matching, *GardenParty, {}, 42, TEXT("PlayerOne"));
	const FGFRunwayScore B = FGFRunwayJudge::Judge(Matching, *GardenParty, {}, 42, TEXT("PlayerOne"));
	TestEqual(TEXT("Three judges"), A.JudgeCards.Num(), 3);
	TestTrue(TEXT("Same seed, same cards"), A.JudgeCards == B.JudgeCards);
	TestTrue(TEXT("Great look is S tier"), A.Tier == EGFRunwayTier::S);
	const FGFRunwayScore Bad = FGFRunwayJudge::Judge(GFFashionTests::Make({ TEXT("CargoShorts") }), *Gala, { GFFashionTests::Make({ TEXT("CargoShorts") }) }, 42, TEXT("PlayerTwo"));
	TestTrue(TEXT("Lazy off-theme look is C"), Bad.Tier == EGFRunwayTier::C);
	const float Base = (A.Theme + A.Harmony + A.Completeness + A.Novelty) / 4.f;
	for (const float Card : A.JudgeCards)
	{
		TestTrue(TEXT("Cards stay in range"), Card >= 0.f && Card <= 10.f);
		TestTrue(TEXT("Spread is small"), FMath::Abs(Card - Base) <= 2.5f);
	}
	TestTrue(TEXT("Tier thresholds"), FGFRunwayJudge::TierFor(8.5f) == EGFRunwayTier::S && FGFRunwayJudge::TierFor(7.f) == EGFRunwayTier::A
		&& FGFRunwayJudge::TierFor(5.f) == EGFRunwayTier::B && FGFRunwayJudge::TierFor(4.9f) == EGFRunwayTier::C);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionRewardsTest, "GFGame.Fashion.Rewards", GFFashionTests::Flags)
bool FGFFashionRewardsTest::RunTest(const FString&)
{
	const FGFRunwayRewardRules Rules;
	auto Total = [](const TArray<FGFItemStack>& Stacks)
	{
		int32 N = 0;
		for (const FGFItemStack& S : Stacks) { N += S.Count; }
		return N;
	};
	const int32 C = Total(FGFRunwayJudge::RewardsFor(EGFRunwayTier::C, false, Rules));
	const int32 B = Total(FGFRunwayJudge::RewardsFor(EGFRunwayTier::B, false, Rules));
	const int32 A = Total(FGFRunwayJudge::RewardsFor(EGFRunwayTier::A, false, Rules));
	const int32 S = Total(FGFRunwayJudge::RewardsFor(EGFRunwayTier::S, false, Rules));
	TestTrue(TEXT("Better tiers pay more"), C > 0 && C < B && B < A && A < S);
	TestEqual(TEXT("Winner bonus"), Total(FGFRunwayJudge::RewardsFor(EGFRunwayTier::S, true, Rules)), S + 1);

	// Materials are the only currency: every reward id is a tiered material in DT_Items.
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (TestNotNull(TEXT("Item table"), Items))
	{
		for (EGFRunwayTier T : { EGFRunwayTier::C, EGFRunwayTier::B, EGFRunwayTier::A, EGFRunwayTier::S })
		{
			for (const FGFItemStack& St : FGFRunwayJudge::RewardsFor(T, true, Rules))
			{
				const FGFItemRow* Row = FGFEconomy::FindItem(Items, St.ItemId);
				TestTrue(*(St.ItemId.ToString() + TEXT(" is a material")), Row && Row->Tier > 0 && Row->MaterialCategory != EGFMaterialCategory::None);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionShowTest, "GFGame.Fashion.Show", GFFashionTests::Flags)
bool FGFFashionShowTest::RunTest(const FString&)
{
	FGFRunwayShow Show;
	const FGuid ShowId(1, 2, 3, 4);
	TestFalse(TEXT("Unknown theme refused"), Show.Start(ShowId, TEXT("NoSuchTheme"), 7, { TEXT("P1") }));
	TestFalse(TEXT("Nobody joined"), Show.Start(ShowId, TEXT("GardenParty"), 7, {}));
	TestTrue(TEXT("Start"), Show.Start(ShowId, TEXT("GardenParty"), 7, { TEXT("P1"), TEXT("P2"), TEXT("P3") }));
	TestTrue(TEXT("Dressing"), Show.GetPhase() == EGFRunwayPhase::Dressing);
	TestEqual(TEXT("75 s to dress"), Show.GetPhaseTimeLeft(), 75.f);
	TestFalse(TEXT("No second show while one runs"), Show.Start(ShowId, TEXT("GardenParty"), 7, { TEXT("P1") }));

	FGFOutfit Garden = GFFashionTests::Garden();
	TestTrue(TEXT("P1 dresses on theme"), Show.SetOutfit(TEXT("P1"), Garden));
	TestTrue(TEXT("P2 dresses off theme"), Show.SetOutfit(TEXT("P2"), GFFashionTests::Explorer()));
	TestFalse(TEXT("Stranger can't dress"), Show.SetOutfit(TEXT("Stranger"), Garden));
	// P3 never dresses.

	Show.Advance(30.f);
	TestTrue(TEXT("Still dressing"), Show.GetPhase() == EGFRunwayPhase::Dressing);
	TestTrue(TEXT("Ready"), Show.SetReady(TEXT("P1")) && Show.SetReady(TEXT("P2")));
	TestTrue(TEXT("Not everyone is ready yet"), Show.GetPhase() == EGFRunwayPhase::Dressing);
	Show.Advance(45.f);
	TestTrue(TEXT("Timer ends dressing"), Show.GetPhase() == EGFRunwayPhase::Walking);
	TestEqual(TEXT("First walker"), Show.GetWalkerIndex(), 0);
	TestFalse(TEXT("No outfit changes on the runway"), Show.SetOutfit(TEXT("P1"), GFFashionTests::Explorer()));
	Show.Advance(8.f);
	TestEqual(TEXT("Second walker after 8 s"), Show.GetWalkerIndex(), 1);
	Show.Advance(8.f);
	Show.Advance(8.f);
	TestTrue(TEXT("Judging after everyone walked"), Show.GetPhase() == EGFRunwayPhase::Judging);
	Show.Advance(10.f);
	TestTrue(TEXT("Results"), Show.GetPhase() == EGFRunwayPhase::Results);

	const FGFRunwayContestant* P1 = Show.FindContestant(TEXT("P1"));
	const FGFRunwayContestant* P2 = Show.FindContestant(TEXT("P2"));
	const FGFRunwayContestant* P3 = Show.FindContestant(TEXT("P3"));
	if (!TestNotNull(TEXT("P1"), P1) || !TestNotNull(TEXT("P2"), P2) || !TestNotNull(TEXT("P3"), P3))
	{
		return false;
	}
	TestEqual(TEXT("On-theme look wins"), P1->Rank, 1);
	TestTrue(TEXT("Off-theme look scores lower"), P2->Score.Total < P1->Score.Total);
	TestEqual(TEXT("No outfit, no reward"), P3->Rewards.Num(), 0);
	TestTrue(TEXT("Winner is paid"), P1->Rewards.Num() > 0);

	// Rewards go through the ledger once, even if granted twice.
	FGFMapLedgerAccounts Accounts;
	for (const TCHAR* Key : { TEXT("P1"), TEXT("P2"), TEXT("P3") })
	{
		Accounts.WriteAccount(Key, FGFInventory());
	}
	FGFTransferLedger Ledger;
	TestEqual(TEXT("Paid the dressed players"), Show.GrantRewards(Accounts, Ledger), 2);
	TestEqual(TEXT("Second grant is a no-op"), Show.GrantRewards(Accounts, Ledger), 0);
	FGFInventory Inv;
	Accounts.ReadAccount(TEXT("P1"), Inv);
	for (const FGFItemStack& St : P1->Rewards)
	{
		TestEqual(*(TEXT("P1 got ") + St.ItemId.ToString()), Inv.GetCount(St.ItemId), St.Count);
	}
	TestTrue(TEXT("TxIds differ per player"), Show.RewardTxId(TEXT("P1")) != Show.RewardTxId(TEXT("P2")));

	// The next show remembers P1's look: wearing it again is less novel.
	const float FirstNovelty = P1->Score.Novelty;
	TestTrue(TEXT("Next show"), Show.Start(FGuid(5, 6, 7, 8), TEXT("GardenParty"), 7, { TEXT("P1") }));
	Show.SetOutfit(TEXT("P1"), Garden);
	Show.SetReady(TEXT("P1"));
	TestTrue(TEXT("Solo: ready skips the timer"), Show.GetPhase() == EGFRunwayPhase::Walking);
	Show.Advance(100.f);
	TestTrue(TEXT("Solo results"), Show.GetPhase() == EGFRunwayPhase::Results);
	const FGFRunwayContestant* Again = Show.FindContestant(TEXT("P1"));
	TestTrue(TEXT("Repeat look is less novel"), Again && Again->Score.Novelty < FirstNovelty);
	TestTrue(TEXT("Solo has no winner bonus"), Again && Again->Rank == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionValidateTest, "GFGame.Fashion.Validate", GFFashionTests::Flags)
bool FGFFashionValidateTest::RunTest(const FString&)
{
	FString Why;
	TestTrue(TEXT("Catalog outfit is valid"), GFFashionTests::Garden().Validate(&Why));
	TestTrue(TEXT("Empty outfit is valid"), FGFOutfit().Validate());

	FGFOutfit Short = GFFashionTests::Garden();
	Short.Slots.SetNum(2);
	TestFalse(TEXT("Missing slots"), Short.Validate());
	FGFOutfit Long = GFFashionTests::Garden();
	Long.Slots.AddDefaulted();
	TestFalse(TEXT("Extra slots"), Long.Validate());

	FGFOutfit Unknown = GFFashionTests::Garden();
	Unknown.Slots[0].PieceId = TEXT("NotAPiece");
	TestFalse(TEXT("Unknown piece"), Unknown.Validate(&Why));

	FGFOutfit Swapped = GFFashionTests::Garden();
	Swap(Swapped.Slots[0], Swapped.Slots[1]);
	TestFalse(TEXT("Piece in the wrong slot"), Swapped.Validate());

	FGFOutfit EmptyDyed;
	EmptyDyed.Slots[0].bDyed = true;
	TestFalse(TEXT("Dye on an empty slot"), EmptyDyed.Validate());

	// Find an undyeable piece in the catalog and force a dye onto it (as a cheating client could).
	const FGFFashionPiece* Plain = FGFFashionCatalog::Pieces().FindByPredicate([](const FGFFashionPiece& P) { return !P.bDyeable; });
	if (Plain)
	{
		FGFOutfit O;
		O.Equip(Plain->Id);
		TestFalse(TEXT("Dye() refuses undyeable pieces"), O.Dye(Plain->Slot, FLinearColor::Red));
		O.Slots[static_cast<int32>(Plain->Slot)].bDyed = true;
		TestFalse(TEXT("Forced dye on an undyeable piece"), O.Validate());
	}

	FGFOutfit BadColor = GFFashionTests::Garden();
	TestFalse(TEXT("Dye() refuses NaN"), BadColor.Dye(EGFOutfitSlot::Top, FLinearColor(NAN, 0.f, 0.f)));
	BadColor.Slots[static_cast<int32>(EGFOutfitSlot::Top)].bDyed = true;
	BadColor.Slots[static_cast<int32>(EGFOutfitSlot::Top)].DyeColor = FLinearColor(5.f, 0.f, 0.f);
	TestFalse(TEXT("Out-of-range colour"), BadColor.Validate());

	FGFRunwayShow Show;
	TestTrue(TEXT("Start"), Show.Start(FGuid(1, 1, 1, 1), TEXT("GardenParty"), 1, { TEXT("P1") }));
	TestFalse(TEXT("Show refuses an invalid outfit"), Show.SetOutfit(TEXT("P1"), Swapped));
	TestTrue(TEXT("Show takes a valid outfit"), Show.SetOutfit(TEXT("P1"), GFFashionTests::Garden()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionDyesTest, "GFGame.Fashion.Dyes", GFFashionTests::Flags)
bool FGFFashionDyesTest::RunTest(const FString&)
{
	TestEqual(TEXT("Red"), FGFDyes::DyeForColor(FLinearColor(0.9f, 0.1f, 0.1f)), FName(TEXT("RoseDye")));
	TestEqual(TEXT("Pink"), FGFDyes::DyeForColor(FLinearColor(1.f, 0.4f, 0.7f)), FName(TEXT("RoseDye")));
	TestEqual(TEXT("Yellow"), FGFDyes::DyeForColor(FLinearColor(0.9f, 0.8f, 0.1f)), FName(TEXT("SunDye")));
	TestEqual(TEXT("Green"), FGFDyes::DyeForColor(FLinearColor(0.2f, 0.8f, 0.2f)), FName(TEXT("FernDye")));
	TestEqual(TEXT("Blue"), FGFDyes::DyeForColor(FLinearColor(0.1f, 0.3f, 0.9f)), FName(TEXT("SkyDye")));
	TestEqual(TEXT("Purple"), FGFDyes::DyeForColor(FLinearColor(0.6f, 0.2f, 0.9f)), FName(TEXT("PlumDye")));
	TestEqual(TEXT("White"), FGFDyes::DyeForColor(FLinearColor(0.95f, 0.95f, 0.95f)), FName(TEXT("ChalkDye")));
	TestEqual(TEXT("Black"), FGFDyes::DyeForColor(FLinearColor(0.f, 0.f, 0.f)), FName(TEXT("ChalkDye")));
	TestEqual(TEXT("One dye per outfit piece"), FGFDyes::CostForOutfitPiece(FLinearColor::Red)[0].Count, 1);
	TestEqual(TEXT("Blocks: 1 dye"), FGFDyes::CostForBlocks(FLinearColor::Red, 1)[0].Count, 1);
	TestEqual(TEXT("Blocks: 8 per dye"), FGFDyes::CostForBlocks(FLinearColor::Red, 8)[0].Count, 1);
	TestEqual(TEXT("Blocks: rounded up"), FGFDyes::CostForBlocks(FLinearColor::Red, 9)[0].Count, 2);
	TestEqual(TEXT("Blocks: none"), FGFDyes::CostForBlocks(FLinearColor::Red, 0).Num(), 0);

	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (TestNotNull(TEXT("Item table"), Items))
	{
		for (const FName Dye : FGFDyes::AllDyeItems())
		{
			const FGFItemRow* Row = FGFEconomy::FindItem(Items, Dye);
			TestTrue(*(Dye.ToString() + TEXT(" is a tiered material in DT_Items")), Row && Row->Tier > 0 && Row->MaterialCategory != EGFMaterialCategory::None);
		}
		const TArray<FString> Problems = FGFEconomy::ValidateItemTable(Items);
		for (const FString& P : Problems)
		{
			AddError(P);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionStageTest, "GFGame.Fashion.Stage", GFFashionTests::Flags)
bool FGFFashionStageTest::RunTest(const FString&)
{
	GFFashionTests::FTestWorld TW;
	AGFRunwayStage* Stage = TW.World->SpawnActor<AGFRunwayStage>();
	if (!TestNotNull(TEXT("Stage"), Stage))
	{
		return false;
	}
	FGFMapLedgerAccounts Accounts;
	FGFTransferLedger Ledger;
	Stage->SetLedgerOverride(&Ledger, &Accounts);
	Stage->DressingSeconds = 10.f;
	Stage->WalkSeconds = 1.f;
	Stage->JudgingSeconds = 1.f;
	TestTrue(TEXT("Found"), AGFRunwayStage::Find(TW.World) == Stage);

	// P1 has dyes; P2 has none; P3 has no ledger account until after the show.
	FGFInventory P1Inv;
	P1Inv.Add(TEXT("RoseDye"), 1);
	P1Inv.Add(TEXT("SkyDye"), 2);
	Accounts.WriteAccount(TEXT("P1"), P1Inv);
	Accounts.WriteAccount(TEXT("P2"), FGFInventory());

	TestTrue(TEXT("Unknown theme"), Stage->StartShowFor(TEXT("Nope"), { TEXT("P1") }, 1) == EGFRunwayRequestResult::UnknownTheme);
	TestTrue(TEXT("Nobody"), Stage->StartShowFor(TEXT("GardenParty"), {}, 1) == EGFRunwayRequestResult::NoPlayers);
	TestTrue(TEXT("Outfit before a show"), Stage->HandleSetOutfit(TEXT("P1"), GFFashionTests::Garden()) == EGFRunwayRequestResult::WrongPhase);
	TestTrue(TEXT("Start"), Stage->StartShowFor(TEXT("GardenParty"), { TEXT("P1"), TEXT("P2"), TEXT("P3") }, 7) == EGFRunwayRequestResult::Ok);
	TestTrue(TEXT("Replicated state says dressing"), Stage->GetState().Phase == EGFRunwayPhase::Dressing);
	TestEqual(TEXT("Whole seconds left"), Stage->GetState().PhaseSecondsLeft, 10);
	TestTrue(TEXT("No second show"), Stage->StartShowFor(TEXT("GardenParty"), { TEXT("P1") }, 1) == EGFRunwayRequestResult::WrongPhase);

	// P1 dyes the top red and the shoes blue: 1 RoseDye + 1 SkyDye.
	FGFOutfit Dyed = GFFashionTests::Garden();
	TestTrue(TEXT("Dye top"), Dyed.Dye(EGFOutfitSlot::Top, FLinearColor(0.9f, 0.1f, 0.1f)));
	TestTrue(TEXT("Dye shoes"), Dyed.Dye(EGFOutfitSlot::Shoes, FLinearColor(0.1f, 0.3f, 0.9f)));
	TestTrue(TEXT("Stranger"), Stage->HandleSetOutfit(TEXT("Stranger"), Dyed) == EGFRunwayRequestResult::NotInShow);
	FGFOutfit Bad = Dyed;
	Swap(Bad.Slots[0], Bad.Slots[1]);
	TestTrue(TEXT("Invalid outfit refused"), Stage->HandleSetOutfit(TEXT("P1"), Bad) == EGFRunwayRequestResult::InvalidOutfit);
	TestTrue(TEXT("P1 dressed"), Stage->HandleSetOutfit(TEXT("P1"), Dyed) == EGFRunwayRequestResult::Ok);
	FGFInventory Inv;
	Accounts.ReadAccount(TEXT("P1"), Inv);
	TestEqual(TEXT("RoseDye spent"), Inv.GetCount(TEXT("RoseDye")), 0);
	TestEqual(TEXT("SkyDye spent"), Inv.GetCount(TEXT("SkyDye")), 1);
	TestEqual(TEXT("Two burns"), static_cast<int32>(Ledger.GetBurned(TEXT("SkyDye")) + Ledger.GetBurned(TEXT("RoseDye"))), 2);
	TestTrue(TEXT("Dye TxId is show + player + slot + colour"), Ledger.WasApplied(Stage->GetShow().DyeTxId(TEXT("P1"), EGFOutfitSlot::Top, FLinearColor(0.9f, 0.1f, 0.1f))));
	TestFalse(TEXT("Another colour on the same slot is another TxId"), Ledger.WasApplied(Stage->GetShow().DyeTxId(TEXT("P1"), EGFOutfitSlot::Top, FLinearColor(0.1f, 0.2f, 0.95f))));
	// Sending the same outfit again (a resend) costs nothing more.
	TestTrue(TEXT("Resend"), Stage->HandleSetOutfit(TEXT("P1"), Dyed) == EGFRunwayRequestResult::Ok);
	Accounts.ReadAccount(TEXT("P1"), Inv);
	TestEqual(TEXT("Resend is free"), Inv.GetCount(TEXT("SkyDye")), 1);

	// Re-dyeing the top a new colour in the same show charges again; going back to a colour already paid for doesn't.
	Inv.Add(TEXT("SkyDye"), 1);
	Accounts.WriteAccount(TEXT("P1"), Inv);
	FGFOutfit Redyed = Dyed;
	TestTrue(TEXT("Re-dye top blue"), Redyed.Dye(EGFOutfitSlot::Top, FLinearColor(0.1f, 0.2f, 0.95f)));
	const int64 BurnedBefore = Ledger.GetBurned(TEXT("SkyDye"));
	TestTrue(TEXT("Re-dyed"), Stage->HandleSetOutfit(TEXT("P1"), Redyed) == EGFRunwayRequestResult::Ok);
	Accounts.ReadAccount(TEXT("P1"), Inv);
	TestEqual(TEXT("New colour charged again"), Inv.GetCount(TEXT("SkyDye")), 1);
	TestEqual(TEXT("One more SkyDye burned"), static_cast<int32>(Ledger.GetBurned(TEXT("SkyDye")) - BurnedBefore), 1);
	TestTrue(TEXT("Re-dye TxId applied"), Ledger.WasApplied(Stage->GetShow().DyeTxId(TEXT("P1"), EGFOutfitSlot::Top, FLinearColor(0.1f, 0.2f, 0.95f))));
	TestTrue(TEXT("Re-dye resend"), Stage->HandleSetOutfit(TEXT("P1"), Redyed) == EGFRunwayRequestResult::Ok);
	TestTrue(TEXT("Back to red"), Stage->HandleSetOutfit(TEXT("P1"), Dyed) == EGFRunwayRequestResult::Ok);
	Accounts.ReadAccount(TEXT("P1"), Inv);
	TestEqual(TEXT("Resend and back-to-paid colour are free"), Inv.GetCount(TEXT("SkyDye")), 1);
	TestEqual(TEXT("No RoseDye needed for red again"), Inv.GetCount(TEXT("RoseDye")), 0);
	const FGFRunwayContestant* P1 = Stage->FindContestant(TEXT("P1"));
	TestTrue(TEXT("P1's dyed look is in the replicated state"), P1 && P1->Outfit.Slots[static_cast<int32>(EGFOutfitSlot::Top)].bDyed);

	// P2 can't afford a dye: nothing charged, outfit unchanged.
	TestTrue(TEXT("P2 not enough dye"), Stage->HandleSetOutfit(TEXT("P2"), Dyed) == EGFRunwayRequestResult::NotEnoughDye);
	const FGFRunwayContestant* P2 = Stage->FindContestant(TEXT("P2"));
	TestTrue(TEXT("P2 still undressed"), P2 && P2->Outfit.IsEmpty());
	TestTrue(TEXT("P2 undyed outfit"), Stage->HandleSetOutfit(TEXT("P2"), GFFashionTests::Explorer()) == EGFRunwayRequestResult::Ok);
	// P3 has no account: an undyed outfit is fine, a dyed one can't be charged.
	TestTrue(TEXT("P3 dyed without an account"), Stage->HandleSetOutfit(TEXT("P3"), Dyed) == EGFRunwayRequestResult::NoAccount);
	TestTrue(TEXT("P3 plain"), Stage->HandleSetOutfit(TEXT("P3"), GFFashionTests::Garden()) == EGFRunwayRequestResult::Ok);

	for (const TCHAR* K : { TEXT("P1"), TEXT("P2"), TEXT("P3") })
	{
		TestTrue(TEXT("Ready"), Stage->HandleSetReady(K, true) == EGFRunwayRequestResult::Ok);
	}
	TestTrue(TEXT("Everyone ready: walking"), Stage->GetState().Phase == EGFRunwayPhase::Walking);
	TestTrue(TEXT("No outfit changes on the runway"), Stage->HandleSetOutfit(TEXT("P1"), GFFashionTests::Garden()) == EGFRunwayRequestResult::WrongPhase);
	for (int32 i = 0; i < 60 && Stage->GetPhase() != EGFRunwayPhase::Results; ++i)
	{
		Stage->Step(0.1f);
	}
	TestTrue(TEXT("Results"), Stage->GetPhase() == EGFRunwayPhase::Results);

	// P1 and P2 paid through the ledger; P3's reward waits with its fixed TxId.
	const FGFRunwayContestant* R1 = Stage->FindContestant(TEXT("P1"));
	const FGFRunwayContestant* R3 = Stage->FindContestant(TEXT("P3"));
	if (!TestNotNull(TEXT("R1"), R1) || !TestNotNull(TEXT("R3"), R3))
	{
		return false;
	}
	TestTrue(TEXT("P1 reward applied"), Ledger.WasApplied(Stage->GetShow().RewardTxId(TEXT("P1"))));
	Accounts.ReadAccount(TEXT("P1"), Inv);
	for (const FGFItemStack& St : R1->Rewards)
	{
		TestEqual(*(TEXT("P1 got ") + St.ItemId.ToString()), Inv.GetCount(St.ItemId), St.Count);
	}
	TestEqual(TEXT("P3 pending"), Stage->GetPendingRewards().Num(), 1);
	TestTrue(TEXT("Pending keeps the reward TxId"), Stage->GetPendingRewards().Num() == 1 && Stage->GetPendingRewards()[0].TxId == Stage->GetShow().RewardTxId(TEXT("P3")));
	TestEqual(TEXT("Retry without an account pays nothing"), Stage->RetryPendingRewards(), 0);
	Accounts.WriteAccount(TEXT("P3"), FGFInventory());
	TestEqual(TEXT("Retry pays P3"), Stage->RetryPendingRewards(), 1);
	TestEqual(TEXT("Nothing pending"), Stage->GetPendingRewards().Num(), 0);
	Accounts.ReadAccount(TEXT("P3"), Inv);
	for (const FGFItemStack& St : R3->Rewards)
	{
		TestEqual(*(TEXT("P3 got ") + St.ItemId.ToString()), Inv.GetCount(St.ItemId), St.Count);
	}
	// More steps in Results never pay again.
	const int32 Entries = Ledger.AppliedOrder.Num();
	Stage->Step(1.f);
	Stage->Step(1.f);
	TestEqual(TEXT("Paid once"), Ledger.AppliedOrder.Num(), Entries);

	// A new show: new TxIds, so dyeing the top again is charged again.
	TestTrue(TEXT("Next show"), Stage->StartShowFor(TEXT("GardenParty"), { TEXT("P1") }, 3) == EGFRunwayRequestResult::Ok);
	FGFOutfit SkyTop = GFFashionTests::Garden();
	SkyTop.Dye(EGFOutfitSlot::Top, FLinearColor(0.1f, 0.3f, 0.9f));
	TestTrue(TEXT("Dyed in the next show"), Stage->HandleSetOutfit(TEXT("P1"), SkyTop) == EGFRunwayRequestResult::Ok);
	Accounts.ReadAccount(TEXT("P1"), Inv);
	TestEqual(TEXT("Charged again in a new show"), Inv.GetCount(TEXT("SkyDye")), 0);
	TestTrue(TEXT("Out of dye"), Stage->HandleSetOutfit(TEXT("P1"), Dyed) == EGFRunwayRequestResult::NotEnoughDye);
	Stage->Destroy();
	return true;
}

#endif
