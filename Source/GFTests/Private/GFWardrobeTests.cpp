// Wardrobe catalog, earning, craft, rack labels, and save round-trip (#279).

#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "GFFashionItems.h"
#include "GFFashionTypes.h"
#include "GFGameState.h"
#include "GFHUD.h"
#include "GFInventoryComponent.h"
#include "GFItemTypes.h"
#include "GFPlayerState.h"
#include "GFRunwayJudge.h"
#include "GFRunwaySite.h"
#include "GFRunwayStage.h"
#include "GFSaveGame.h"
#include "GFWardrobe.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFWardrobeTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	static FGFMapLedgerAccounts Account(const TCHAR* Key)
	{
		FGFMapLedgerAccounts Accounts;
		Accounts.Accounts.Add(Key, FGFInventory());
		return Accounts;
	}

	static int32 CountPrefix(const TArray<FString>& Lines, const TCHAR* Prefix)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.StartsWith(Prefix) ? 1 : 0;
		}
		return N;
	}

	struct FWorld
	{
		UWorld* World = nullptr;

		explicit FWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeCatalogTest, "GFGame.Fashion.Wardrobe.Catalog", GFWardrobeTests::Flags)
bool FGFWardrobeCatalogTest::RunTest(const FString&)
{
	const TArray<FGFFashionPiece>& Pieces = FGFFashionCatalog::Pieces();
	TestTrue(TEXT("At least 24 pieces"), Pieces.Num() >= 24);
	TestEqual(TEXT("Every CSV row became a piece"), FGFFashionCatalog::RowCount(), Pieces.Num());

	TSet<FName> Sets;
	TSet<EGFOutfitSlot> Slots;
	int32 Starters = 0;
	for (const FGFFashionPiece& P : Pieces)
	{
		TestTrue(*(P.Id.ToString() + TEXT(" has a slot")), P.Slot != EGFOutfitSlot::Count);
		TestFalse(*(P.Id.ToString() + TEXT(" has a set")), P.SetId.IsNone());
		TestTrue(*(P.Id.ToString() + TEXT(" has a source")), P.Sources.Num() >= 1 && P.Sources.Num() <= 2);
		TestFalse(*(P.Id.ToString() + TEXT(" has a source line")), P.SourceLine.IsEmpty());
		TestEqual(*(P.Id.ToString() + TEXT(" dye channels stay unused")), P.DyeChannels, 0);
		Sets.Add(P.SetId);
		Slots.Add(P.Slot);
		Starters += P.bStarter ? 1 : 0;
		if (P.Sources.Contains(FGFWardrobe::ActivityCraft))
		{
			TestTrue(*(P.Id.ToString() + TEXT(" craft cost parsed")), P.CraftCost.Num() > 0);
		}
	}
	TestTrue(TEXT("At least four sets"), Sets.Num() >= 4);
	TestTrue(TEXT("Garden stays"), Sets.Contains(FName(TEXT("Garden"))));
	TestTrue(TEXT("Explorer stays"), Sets.Contains(FName(TEXT("Explorer"))));
	TestEqual(TEXT("Six slots"), Slots.Num(), 6);
	TestEqual(TEXT("One starter per slot"), Starters, 6);

	const TCHAR* StarterIds[] = { TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown"), TEXT("VineTail") };
	const EGFOutfitSlot StarterSlots[] = { EGFOutfitSlot::Hair, EGFOutfitSlot::Top, EGFOutfitSlot::Bottom, EGFOutfitSlot::Shoes, EGFOutfitSlot::Accessory, EGFOutfitSlot::Tail };
	for (int32 i = 0; i < UE_ARRAY_COUNT(StarterIds); ++i)
	{
		const FGFFashionPiece* Starter = FGFFashionCatalog::StarterForSlot(StarterSlots[i]);
		TestTrue(*(FString(StarterIds[i]) + TEXT(" is the starter")), Starter && Starter->Id == StarterIds[i]);
	}

	const FGFFashionPiece* Fern = FGFFashionCatalog::FindPiece(TEXT("FernBraid"));
	const FGFFashionPiece* Boots = FGFFashionCatalog::FindPiece(TEXT("TrailBoots"));
	if (!TestNotNull(TEXT("Fern braid"), Fern) || !TestNotNull(TEXT("Trail boots"), Boots))
	{
		return false;
	}
	TestTrue(TEXT("Fern braid is still Nature"), Fern->Tags.Contains(FName(TEXT("Nature"))));
	TestTrue(TEXT("Fern braid is still Cute"), Fern->Tags.Contains(FName(TEXT("Cute"))));
	TestFalse(TEXT("Trail boots stay undyeable"), Boots->bDyeable);

	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	for (const FGFFashionPiece& P : Pieces)
	{
		const FGFItemRow* Row = Items->FindRow<FGFItemRow>(P.Id, TEXT("Wardrobe"), false);
		if (!TestNotNull(*P.Id.ToString(), Row))
		{
			continue;
		}
		TestEqual(*(P.Id.ToString() + TEXT(" is an outfit")), Row->Category, EGFItemCategory::Outfit);
		TestEqual(*(P.Id.ToString() + TEXT(" stacks to 1")), Row->StackMax, 1);
		TestEqual(*(P.Id.ToString() + TEXT(" tier 0")), Row->Tier, 0);
		TestEqual(*(P.Id.ToString() + TEXT(" value 0")), Row->Value, 0);
		TestFalse(*(P.Id.ToString() + TEXT(" is not wagerable")), Row->bWagerable);
		// Brand outfits stay tradable. The closet can still list one so the runway rack can wear it.
		// Every other catalog piece stays on the account.
		if (P.Id == FName(TEXT("dirtachi_01")))
		{
			TestFalse(*(P.Id.ToString() + TEXT(" stays tradable like the other brand outfits")), Row->bAccountBound);
		}
		else
		{
			TestTrue(*(P.Id.ToString() + TEXT(" stays on the account")), Row->bAccountBound);
		}
		TestEqual(*(P.Id.ToString() + TEXT(" item slot")), Row->OutfitSlot, FGFFashionItems::RowNameForSlot(P.Slot));
		for (const FGFItemStack& Cost : P.CraftCost)
		{
			const FGFItemRow* Mat = Items->FindRow<FGFItemRow>(Cost.ItemId, TEXT("Wardrobe"), false);
			TestTrue(*(Cost.ItemId.ToString() + TEXT(" is a material")), Mat && Mat->Tier > 0 && Mat->MaterialCategory != EGFMaterialCategory::None);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeScoreTest, "GFGame.Fashion.Wardrobe.Score", GFWardrobeTests::Flags)
bool FGFWardrobeScoreTest::RunTest(const FString&)
{
	const FGFFashionTheme* GardenParty = FGFFashionCatalog::FindTheme(TEXT("GardenParty"));
	if (!TestNotNull(TEXT("Garden party"), GardenParty))
	{
		return false;
	}
	FGFOutfit Garden;
	for (const TCHAR* Id : { TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown") })
	{
		TestTrue(TEXT("Garden piece equips"), Garden.Equip(Id));
	}
	TestEqual(TEXT("Garden theme still 10"), FGFRunwayJudge::ScoreTheme(Garden, *GardenParty), 10.f);
	TestEqual(TEXT("Garden completeness still 10"), FGFRunwayJudge::ScoreCompleteness(Garden), 10.f);
	TestEqual(TEXT("Garden is one set"), FGFRunwayJudge::ScoreSet(Garden), 10.f);
	FGFOutfit Partial;
	Partial.Equip(TEXT("PetalBlouse"));
	Partial.Equip(TEXT("MeadowSkirt"));
	TestEqual(TEXT("Two pieces are still partial"), FGFRunwayJudge::ScoreCompleteness(Partial), 4.f);

	FGFOutfit Mixed = Garden;
	TestTrue(TEXT("Swap in an explorer top"), Mixed.Equip(TEXT("ExplorerVest")));
	const float MixedTheme = FGFRunwayJudge::ScoreTheme(Mixed, *GardenParty);
	TestTrue(TEXT("A mix still scores on the theme"), MixedTheme > 0.f && MixedTheme < 10.f);

	FGFOutfit Formal;
	for (const TCHAR* Id : { TEXT("FossilBun"), TEXT("FossilJacket"), TEXT("FossilTrousers"), TEXT("FossilHeels"), TEXT("FossilBrooch"), TEXT("BoneTail") })
	{
		TestTrue(TEXT("Formal piece equips"), Formal.Equip(Id));
	}
	TestEqual(TEXT("A full set scores 10"), FGFRunwayJudge::ScoreSet(Formal), 10.f);
	FGFOutfit Broken = Formal;
	TestTrue(TEXT("A garden piece breaks the set"), Broken.Equip(TEXT("DaisyCrown")));
	const float BrokenSet = FGFRunwayJudge::ScoreSet(Broken);
	TestTrue(TEXT("A mixed set scores lower"), BrokenSet < FGFRunwayJudge::ScoreSet(Formal));
	TestEqual(TEXT("Empty set score"), FGFRunwayJudge::ScoreSet(FGFOutfit()), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeEarnTest, "GFGame.Fashion.Wardrobe.Earn", GFWardrobeTests::Flags)
bool FGFWardrobeEarnTest::RunTest(const FString&)
{
	FGFMapLedgerAccounts Accounts = GFWardrobeTests::Account(TEXT("P"));
	FGFTransferLedger Ledger;
	TArray<FName> Granted;
	FGFWardrobe::GrantStarters(Ledger, Accounts, TEXT("P"), &Granted);
	TestEqual(TEXT("Six starters"), Granted.Num(), 6);
	FGFInventory Inv;
	TestTrue(TEXT("Account"), Accounts.ReadAccount(TEXT("P"), Inv));
	TestEqual(TEXT("Fern braid once"), Inv.GetCount(TEXT("FernBraid")), 1);
	TestEqual(TEXT("Vine tail once"), Inv.GetCount(TEXT("VineTail")), 1);
	TestTrue(TEXT("Starter tx remembered"), Ledger.WasApplied(FGFWardrobe::TxIdFor(TEXT("P"), TEXT("FernBraid"), false)));
	Granted.Reset();
	FGFWardrobe::GrantStarters(Ledger, Accounts, TEXT("P"), &Granted);
	Accounts.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Second starter grant mints nothing"), Granted.Num(), 0);
	TestEqual(TEXT("Fern braid stays one"), Inv.GetCount(TEXT("FernBraid")), 1);

	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("P"), FGFWardrobe::ActivityRunway, NAME_None, &Granted);
	TestTrue(TEXT("A runway win grants Ember Tail"), Granted.Contains(FName(TEXT("EmberTail"))));
	TestFalse(TEXT("A runway win does not grant the craft bun"), Granted.Contains(FName(TEXT("FossilBun"))));
	TestFalse(TEXT("A runway win does not grant a starter again"), Granted.Contains(FName(TEXT("FernBraid"))));
	const int32 RunwayGrants = Granted.Num();
	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("P"), FGFWardrobe::ActivityRunway, NAME_None, &Granted);
	Accounts.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("A second runway win mints nothing"), Granted.Num(), 0);
	TestEqual(TEXT("Ember tail stays one"), Inv.GetCount(TEXT("EmberTail")), 1);
	TestTrue(TEXT("The first win did grant something"), RunwayGrants > 0);

	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("Rime"), FGFWardrobe::ActivityBeatHop, TEXT("Rime"), &Granted);
	TestEqual(TEXT("No account, no piece"), Granted.Num(), 0);
	Accounts.Accounts.Add(TEXT("Hop"), FGFInventory());
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("Hop"), FGFWardrobe::ActivityBeatHop, TEXT("Rime"), &Granted);
	Accounts.ReadAccount(TEXT("Hop"), Inv);
	TestEqual(TEXT("Course 2 does not grant course 1 slippers"), Inv.GetCount(TEXT("MoonSlippers")), 0);
	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("Hop"), FGFWardrobe::ActivityBeatHop, TEXT("Fern"), &Granted);
	Accounts.ReadAccount(TEXT("Hop"), Inv);
	TestEqual(TEXT("Course 1 grants the slippers"), Inv.GetCount(TEXT("MoonSlippers")), 1);
	TestTrue(TEXT("Slippers were in the grant"), Granted.Contains(FName(TEXT("MoonSlippers"))));

	Accounts.Accounts.Add(TEXT("Miner"), FGFInventory());
	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("Miner"), FGFWardrobe::ActivityMine, NAME_None, &Granted);
	Accounts.ReadAccount(TEXT("Miner"), Inv);
	TestEqual(TEXT("A haul grants the scale tee"), Inv.GetCount(TEXT("ScaleTee")), 1);
	TestEqual(TEXT("A haul also grants the eggshell pack"), Inv.GetCount(TEXT("EggshellPack")), 1);
	Granted.Reset();
	FGFWardrobe::GrantForActivity(Ledger, Accounts, TEXT("Miner"), FGFWardrobe::ActivityMine, NAME_None, &Granted);
	Accounts.ReadAccount(TEXT("Miner"), Inv);
	TestEqual(TEXT("A second haul does not mint again"), Granted.Num(), 0);
	TestEqual(TEXT("Scale tee stays one"), Inv.GetCount(TEXT("ScaleTee")), 1);

	FGFMapLedgerAccounts Other = GFWardrobeTests::Account(TEXT("A"));
	Other.Accounts.Add(TEXT("B"), FGFInventory());
	FGFTransferLedger Split;
	FGFWardrobe::GrantStarters(Split, Other, TEXT("A"));
	FGFInventory AInv, BInv;
	Other.ReadAccount(TEXT("A"), AInv);
	Other.ReadAccount(TEXT("B"), BInv);
	TestEqual(TEXT("A owns the braid"), AInv.GetCount(TEXT("FernBraid")), 1);
	TestEqual(TEXT("B does not"), BInv.GetCount(TEXT("FernBraid")), 0);
	const FGFFashionPiece* Braid = FGFFashionCatalog::FindPiece(TEXT("FernBraid"));
	if (!TestNotNull(TEXT("Braid"), Braid))
	{
		return false;
	}
	TestEqual(TEXT("A's rack names it"), FGFWardrobe::RackLabel(*Braid, true), FString(TEXT("Fern Braid")));
	TestNotEqual(TEXT("B's rack keeps the source"), FGFWardrobe::RackLabel(*Braid, false), Braid->DisplayName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeCraftTest, "GFGame.Fashion.Wardrobe.Craft", GFWardrobeTests::Flags)
bool FGFWardrobeCraftTest::RunTest(const FString&)
{
	const FGFFashionPiece* Bun = FGFFashionCatalog::FindPiece(TEXT("FossilBun"));
	const FGFFashionPiece* Slippers = FGFFashionCatalog::FindPiece(TEXT("MoonSlippers"));
	if (!TestNotNull(TEXT("Fossil bun"), Bun) || !TestNotNull(TEXT("Moon slippers"), Slippers))
	{
		return false;
	}
	TestEqual(TEXT("Craft line"), Bun->SourceLine, FString(TEXT("Craft: 6 Clay + 2 Crystal")));
	TestEqual(TEXT("Locked craft label"), FGFWardrobe::RackLabel(*Bun, false), Bun->SourceLine);
	TestEqual(TEXT("Owned craft label"), FGFWardrobe::RackLabel(*Bun, true), Bun->DisplayName);
	TestEqual(TEXT("Course line"), Slippers->SourceLine, FString(TEXT("Win Course 1 in Beat Hop.")));
	TestFalse(TEXT("Locked colour is the silhouette"), FGFWardrobe::RackColor(*Bun, false).Equals(Bun->BaseColor));
	TestTrue(TEXT("Owned colour is the piece"), FGFWardrobe::RackColor(*Bun, true).Equals(Bun->BaseColor));
	TestTrue(TEXT("Toast names the piece and the rack"), FGFWardrobe::ToastFor(TEXT("FossilBun")).Contains(TEXT("Fossil Bun")) && FGFWardrobe::ToastFor(TEXT("FossilBun")).Contains(TEXT("rack")));

	FGFMapLedgerAccounts Accounts = GFWardrobeTests::Account(TEXT("P"));
	FGFTransferLedger Ledger;
	TestEqual(TEXT("Not a tailor piece"), FGFWardrobe::TryCraft(Ledger, Accounts, TEXT("P"), TEXT("EmberTail")), EGFWardrobeResult::NotThisSource);
	TestEqual(TEXT("Unknown"), FGFWardrobe::TryCraft(Ledger, Accounts, TEXT("P"), TEXT("NoSuch")), EGFWardrobeResult::UnknownPiece);

	FGFInventory Inv;
	Accounts.ReadAccount(TEXT("P"), Inv);
	Inv.Add(TEXT("Clay"), 6);
	Accounts.WriteAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Short a crystal"), FGFWardrobe::TryCraft(Ledger, Accounts, TEXT("P"), TEXT("FossilBun")), EGFWardrobeResult::MissingMaterials);
	Accounts.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Clay was not spent"), Inv.GetCount(TEXT("Clay")), 6);
	TestEqual(TEXT("Nothing minted"), Inv.GetCount(TEXT("FossilBun")), 0);

	Inv.Add(TEXT("Crystal"), 2);
	Accounts.WriteAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Sewn"), FGFWardrobe::TryCraft(Ledger, Accounts, TEXT("P"), TEXT("FossilBun")), EGFWardrobeResult::Granted);
	Accounts.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Bun is owned"), Inv.GetCount(TEXT("FossilBun")), 1);
	TestEqual(TEXT("Clay spent"), Inv.GetCount(TEXT("Clay")), 0);
	TestEqual(TEXT("Crystal spent"), Inv.GetCount(TEXT("Crystal")), 0);
	Inv.Add(TEXT("Clay"), 6);
	Inv.Add(TEXT("Crystal"), 2);
	Accounts.WriteAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Already owned"), FGFWardrobe::TryCraft(Ledger, Accounts, TEXT("P"), TEXT("FossilBun")), EGFWardrobeResult::AlreadyOwned);
	Accounts.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("A second craft does not spend"), Inv.GetCount(TEXT("Clay")), 6);
	TestEqual(TEXT("Still one bun"), Inv.GetCount(TEXT("FossilBun")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeSaveTest, "GFGame.Fashion.Wardrobe.Save", GFWardrobeTests::Flags)
bool FGFWardrobeSaveTest::RunTest(const FString&)
{
	FGFMapLedgerAccounts Accounts = GFWardrobeTests::Account(TEXT("P"));
	FGFTransferLedger Ledger;
	TArray<FName> Granted;
	FGFWardrobe::GrantStarters(Ledger, Accounts, TEXT("P"), &Granted);
	TestEqual(TEXT("Granted"), Granted.Num(), 6);

	UGFSaveGame* Save = NewObject<UGFSaveGame>();
	Save->Ledger = Ledger;
	FGFGuestRecord Guest;
	Guest.PlayerKey = TEXT("P");
	Guest.bHasCharacter = true;
	Guest.LastCharacter.bInitialized = true;
	Guest.LastCharacter.PlayerKey = TEXT("P");
	Accounts.ReadAccount(TEXT("P"), Guest.LastCharacter.Inventory);
	Save->Guests.Add(Guest);

	TArray<uint8> Bytes;
	TestTrue(TEXT("Save"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	UGFSaveGame* Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Load"), Back) || !TestTrue(TEXT("Guest"), Back->Guests.Num() == 1))
	{
		return false;
	}
	TestEqual(TEXT("Braid survived"), Back->Guests[0].LastCharacter.Inventory.GetCount(TEXT("FernBraid")), 1);
	TestEqual(TEXT("Tail survived"), Back->Guests[0].LastCharacter.Inventory.GetCount(TEXT("VineTail")), 1);

	FGFTransferLedger Loaded = Back->Ledger;
	Loaded.RebuildIndex();
	FGFMapLedgerAccounts Restored;
	Restored.Accounts.Add(TEXT("P"), Back->Guests[0].LastCharacter.Inventory);
	Granted.Reset();
	FGFWardrobe::GrantStarters(Loaded, Restored, TEXT("P"), &Granted);
	TestEqual(TEXT("Reload does not mint again"), Granted.Num(), 0);
	FGFInventory Inv;
	Restored.ReadAccount(TEXT("P"), Inv);
	TestEqual(TEXT("Still one braid"), Inv.GetCount(TEXT("FernBraid")), 1);
	TestTrue(TEXT("The mint tx survived"), Loaded.WasApplied(FGFWardrobe::TxIdFor(TEXT("P"), TEXT("FernBraid"), false)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeHintTest, "GFGame.Fashion.Wardrobe.Hint", GFWardrobeTests::Flags)
bool FGFWardrobeHintTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	FGFWardrobe::HintLines(FGFWardrobe::HintStep(false, false, false, false), Lines);
	TestEqual(TEXT("One current step"), GFWardrobeTests::CountPrefix(Lines, TEXT("> ")), 1);
	TestTrue(TEXT("First step is the rack"), Lines.Num() > 0 && Lines[0].StartsWith(TEXT("> Open the wardrobe rack.")));
	FGFWardrobe::HintLines(FGFWardrobe::HintStep(true, false, false, false), Lines);
	TestTrue(TEXT("Next is equip"), Lines.Contains(TEXT("> Equip a piece.")));
	FGFWardrobe::HintLines(FGFWardrobe::HintStep(true, true, false, false), Lines);
	TestTrue(TEXT("Then the theme"), Lines.Contains(TEXT("> Pick a theme.")));
	FGFWardrobe::HintLines(FGFWardrobe::HintStep(true, true, true, false), Lines);
	TestTrue(TEXT("Then the arch"), Lines.Contains(TEXT("> Walk the Start arch.")));
	FGFWardrobe::HintLines(FGFWardrobe::HintStep(true, true, true, true), Lines);
	TestEqual(TEXT("A show in progress highlights nothing"), GFWardrobeTests::CountPrefix(Lines, TEXT("> ")), 0);

	TestEqual(TEXT("Pause entry 0"), FString(AGFHUD::PauseEntryLabel(0)), FString(TEXT("Return to hub")));
	TestEqual(TEXT("Pause entry 2"), FString(AGFHUD::PauseEntryLabel(2)), FString(TEXT("Close")));
	TestEqual(TEXT("How to get items"), FString(AGFHUD::PauseEntryLabel(3)), FString(TEXT("Controls / How to get items")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWardrobeRackTest, "GFGame.Fashion.Wardrobe.Rack", GFWardrobeTests::Flags)
bool FGFWardrobeRackTest::RunTest(const FString&)
{
	using namespace GFWardrobeTests;
	// Qualified: a unity build also sees GFSocialTest::FWorld from `using namespace GFSocialTest`.
	GFWardrobeTests::FWorld W(TEXT("GFWardrobeRackWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = W.World->SpawnActor<AGFGameState>();
	AGFRunwaySite* Site = W.World->SpawnActor<AGFRunwaySite>();
	AGFRunwayStage* Stage = W.World->SpawnActor<AGFRunwayStage>();
	if (!TestNotNull(TEXT("Game"), GS) || !TestNotNull(TEXT("Stage"), Stage) || !TestNotNull(TEXT("Site"), Site))
	{
		return false;
	}
	W.World->SetGameState(GS);
	Site->BuildIfNeeded();
	TestEqual(TEXT("A spot per piece"), Site->NumPieceSpots(), FGFFashionCatalog::Pieces().Num());
	TestEqual(TEXT("Garden and Explorer looks stay"), Site->NumLookSpots(), 2);
	TestTrue(TEXT("Piece verb"), Site->HasVerb(TEXT("Piece")));
	const FGFFashionPiece* Bun = FGFFashionCatalog::FindPiece(TEXT("FossilBun"));
	if (!TestNotNull(TEXT("Bun"), Bun))
	{
		return false;
	}
	TestEqual(TEXT("Rack line matches the catalog"), Site->GetRackLine(TEXT("FossilBun")), Bun->SourceLine);

	TArray<FString> Hint;
	Site->GetItemHintLines(Hint);
	TestEqual(TEXT("Arrival hint is one step"), CountPrefix(Hint, TEXT("> ")), 1);
	TestTrue(TEXT("Arrival hint opens the rack"), Hint.Num() > 0 && Hint[0].Contains(TEXT("Open the wardrobe rack.")));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Site->GetActorLocation()), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Bag"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Walker");
	PS->bCharacterReady = true;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	TestFalse(TEXT("Locked piece stays off"), Site->TryEquipPiece(Pawn, TEXT("FossilBun")));
	TestEqual(TEXT("Refusal is the source line"), Site->LastPrompt, Bun->SourceLine);
	Site->GetItemHintLines(Hint);
	TestTrue(TEXT("The rack was opened"), Hint.Contains(TEXT("> Equip a piece.")));

	TestTrue(TEXT("Braid goes in the bag"), PS->Inventory->AddItem(TEXT("FernBraid"), 1));
	TestTrue(TEXT("Owned piece equips"), Site->TryEquipPiece(Pawn, TEXT("FernBraid")));
	TestTrue(TEXT("Equipped prompt"), Site->LastPrompt.Contains(TEXT("Fern Braid")));
	TestEqual(TEXT("Equipping does not spend it"), PS->Inventory->GetCount(TEXT("FernBraid")), 1);

	TestTrue(TEXT("Theme"), Site->TrySelectTheme(Pawn, TEXT("GardenParty")));
	TestTrue(TEXT("Start from the worn piece"), Site->TryStart(Pawn));
	const FGFRunwayContestant* C = Stage->FindContestant(TEXT("Walker"));
	TestTrue(TEXT("The show wears the braid"), C && C->Outfit.GetPieceId(EGFOutfitSlot::Hair) == FName(TEXT("FernBraid")));
	TestEqual(TEXT("Only the equipped slot"), C ? C->Outfit.NumFilled() : -1, 1);
	return true;
}

#endif
