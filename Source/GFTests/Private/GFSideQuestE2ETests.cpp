// Side quest end to end (QL-g): each town line walked Accept to Done in one shared test world pattern through the
// side quest adapters and the elder hand in, a save taken midway on the Rime line and resumed in a fresh world, and
// two net checks: one player's progress never moves another player's Active line, and the Active side fields and the
// Done list reach the client player states. Elder's Errand expectations are left alone (errand stays on HuntSkins).
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.SideQuest.EndToEnd+GFGame.Net.SideQuest;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GFAccelHop.h"
#include "GFRhythmJudge.h"
#include "GFCharacter.h"
#include "GFElderTalk.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMagicPlayerState.h"
#include "GFModeTypes.h"
#include "GFNetTestUtils.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestAdapters.h"
#include "GFSideQuestEscort.h"
#include "GFSideQuestLine.h"
#include "GFTerritorySubsystem.h"
#include "GFTheftDirector.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestE2ETest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwright(TEXT("Cinderwright"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName VerdantLine(TEXT("VerdantRootward"));
	static const FName RimeLine(TEXT("RimeIceSpan"));
	static const FName CinderLine(TEXT("CinderEmberAscent"));
	static const FName PlankId(TEXT("WoodPlank"));
	static const FName CrystalId(TEXT("Crystal"));
	static const FName RimeStop(TEXT("RimeLanding"));
	static const FName FarStop(TEXT("VerdantLanding"));

	static bool TownCenter(FName TownId, FVector& Out)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		if (!Defaults)
		{
			return false;
		}
		for (const FGFTerritoryFaction& Faction : Defaults->FactionLayout)
		{
			if (FGFSideQuestCatalog::SameTown(Faction.Id, TownId))
			{
				Out = Faction.Center;
				return true;
			}
		}
		return false;
	}

	static FGFQuestSave StandingSave(const TArray<FGFQuestRecord>& Records)
	{
		FGFQuestSave Save;
		Save.Records = Records;
		return Save;
	}

	static FGFQuestRecord Standing(const FString& Key, FName FactionId, EGFFactionRank Rank)
	{
		FGFQuestRecord Record;
		Record.PlayerKey = Key;
		Record.Progress.Step = EGFQuestStep::HuntSkins;
		Record.Progress.FactionId = FactionId;
		Record.Progress.Rank = Rank;
		return Record;
	}

	/** The shared side quest test world: game state, theft director (town lookup), quest subsystem, adapters, escorts. */
	struct FSideWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFSideQuestAdapters* Adapters = nullptr;
		UGFSideQuestEscorts* Escorts = nullptr;
		TArray<FGFQuestRecord> Records;
		int32 NextId = 1300;

		bool Init(FAutomationTestBase& T, const TCHAR* Name)
		{
			if (!GEngine)
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFTheftDirector* Theft = World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params);
			Quest = World->GetSubsystem<UGFQuestSubsystem>();
			Adapters = World->GetSubsystem<UGFSideQuestAdapters>();
			Escorts = World->GetSubsystem<UGFSideQuestEscorts>();
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			if (Adapters)
			{
				Adapters->RefreshClimbLips();
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Theft director"), Theft)
				&& T.TestNotNull(TEXT("Quest subsystem"), Quest) && T.TestNotNull(TEXT("Side quest adapters"), Adapters)
				&& T.TestNotNull(TEXT("Side quest escorts"), Escorts);
		}

		AGFMagicPlayerState* Add(const FString& Key)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFMagicPlayerState* PS = World->SpawnActor<AGFMagicPlayerState>(AGFMagicPlayerState::StaticClass(), FTransform::Identity, Params);
			if (PS)
			{
				PS->PlayerKey = Key;
				PS->SetPlayerId(++NextId);
				PS->SetPlayerName(Key);
				if (GS->PlayerArray.Find(PS) == INDEX_NONE)
				{
					GS->AddPlayerState(PS);
				}
			}
			return PS;
		}

		AGFCharacter* Body(AGFPlayerState* PS, const FVector& At)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFCharacter* Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), At, FRotator::ZeroRotator, Params);
			if (Pawn && PS)
			{
				Pawn->SetPlayerState(PS);
			}
			return Pawn;
		}

		/** Errand standing for Key (a town member on HuntSkins), restored through the mode blob like a load. */
		void Stand(const FString& Key, FName FactionId, EGFFactionRank Rank)
		{
			Records.Add(Standing(Key, FactionId, Rank));
			TArray<uint8> Bytes;
			FGFModeBytes::Write(StandingSave(Records), Bytes);
			Quest->RestoreModeState(Bytes);
			Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
		}

		const FGFSideQuestProgress* Progress(const FString& Key, FName LineId) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			return Board ? Board->Find(LineId) : nullptr;
		}

		FName StepOf(const FString& Key, FName LineId) const
		{
			const FGFSideQuestProgress* P = Progress(Key, LineId);
			return P && P->Status == EGFSideQuestStatus::Active ? P->StepId : NAME_None;
		}

		int32 CountOf(const FString& Key, FName LineId) const
		{
			const FGFSideQuestProgress* P = Progress(Key, LineId);
			return P ? P->Count : -1;
		}

		bool IsDone(const FString& Key, FName LineId) const
		{
			const FGFSideQuestProgress* P = Progress(Key, LineId);
			return P && P->Status == EGFSideQuestStatus::Done;
		}

		bool ErrandUntouched(const FString& Key) const
		{
			const FGFQuestProgress* Errand = Quest->FindProgress(Key);
			return Errand && Errand->Step == EGFQuestStep::HuntSkins;
		}

		bool LipFor(FName TownId, FVector& Out) const
		{
			const FGFQuestClimbLip* Lip = Adapters->GetClimbLips().FindByPredicate([TownId](const FGFQuestClimbLip& L)
			{
				return FGFSideQuestCatalog::SameTown(L.TownId, TownId);
			});
			if (Lip)
			{
				Out = Lip->Location;
			}
			return Lip != nullptr;
		}

		/** What the Hand in town task button does on the server at ElderTown's elder. */
		FGFSideQuestApplyResult HandIn(AGFPlayerState* PS, FName ElderTown)
		{
			FGFSideQuestApplyResult None;
			None.Reason = FGFQuestCodes::OutOfOrder;
			const FGFQuestProgress* Errand = Quest->FindProgress(PS->PlayerKey);
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(PS->PlayerKey);
			const FGFSideQuestBoard Empty;
			const FGFElderTalk::FSideOffer Offer = FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
				Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, ElderTown);
			FGFSideQuestEvent Out;
			const FGFInventory NoItems;
			if (!FGFElderTalk::SideHandInEvent(Offer, PS->PlayerKey, ElderTown, PS->HeldItem, PS->Inventory ? PS->Inventory->GetInventory() : NoItems, Out))
			{
				return None;
			}
			return Quest->ApplySideEvent(Out);
		}

		void Close()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
			World = nullptr;
		}

		~FSideWorld()
		{
			Close();
		}
	};

	static bool Accept(FAutomationTestBase& T, FSideWorld& W, const FString& Key, FName LineId, FName TownId)
	{
		return T.TestTrue(*FString::Printf(TEXT("%s accepts %s"), *Key, *LineId.ToString()),
			W.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineId, TownId)).bAccepted);
	}

	/** Rime: Rime-side ferry landing and Count crystals dropped in Rime, both through the adapters. */
	static void RimeFerryAndCrystals(FSideWorld& W, const FString& Key, const FVector& RimeAt, int32 Count)
	{
		W.Adapters->CreditFerry(Key, Rime, FarStop);
		if (Count > 0)
		{
			W.Adapters->CreditDrop(Key, CrystalId, Count, RimeAt);
		}
	}

	/** Rime from WatchClimb: mantle at the Rime lip, then the elder hand in with four crystals in the bag. */
	static bool RimeFinish(FAutomationTestBase& T, FSideWorld& W, AGFPlayerState* PS, const FString& Key)
	{
		FVector Lip;
		if (!T.TestTrue(TEXT("Rime climb lip"), W.LipFor(Rime, Lip)))
		{
			return false;
		}
		T.TestEqual(TEXT("Mantle at the Rime lip"), W.Adapters->CreditMantle(Key, Lip + FVector(50.f, 0.f, 0.f)), 1);
		T.TestEqual(TEXT("Climb reaches the hand in"), W.StepOf(Key, RimeLine), FName(TEXT("HandIn")));
		PS->Inventory->AddItem(CrystalId, 4);
		const int32 Before = PS->Inventory->GetCount(CrystalId);
		T.TestTrue(TEXT("Hand in at the Rime elder"), W.HandIn(PS, Rime).Result == EGFQuestResult::Ok);
		T.TestEqual(TEXT("Four crystals charged"), PS->Inventory->GetCount(CrystalId), Before - 4);
		return T.TestTrue(TEXT("Rime line done"), W.IsDone(Key, RimeLine));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestE2EVerdantTest, "GFGame.SideQuest.EndToEnd.Verdant", GFSideQuestE2ETest::Flags)
bool FGFSideQuestE2EVerdantTest::RunTest(const FString&)
{
	// Rootward Run: town trees, Good hops, a mantle at the climb lip, the plank hand in, then the escort spawn.
	using namespace GFSideQuestE2ETest;
	FVector Center;
	if (!TestTrue(TEXT("Verdant in the layout"), TownCenter(Verdant, Center)))
	{
		return false;
	}
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideE2EVerdant")))
	{
		return false;
	}
	const FString Key(TEXT("VerdantWalker"));
	AGFMagicPlayerState* PS = W.Add(Key);
	AGFCharacter* Pawn = W.Body(PS, Center + FVector(0.f, -300.f, 100.f));
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr) || !TestNotNull(TEXT("Body"), Pawn))
	{
		return false;
	}
	W.Stand(Key, Verdant, EGFFactionRank::Member);
	if (!Accept(*this, W, Key, VerdantLine, Verdant))
	{
		return false;
	}
	TestEqual(TEXT("On the tree step"), W.StepOf(Key, VerdantLine), FName(TEXT("GatherWood")));
	for (int32 Tree = 0; Tree < 3; ++Tree)
	{
		TestEqual(*FString::Printf(TEXT("Tree %d credited"), Tree + 1), W.Adapters->CreditTree({ Key }, Center), 1);
	}
	TestEqual(TEXT("Trees reach the hops"), W.StepOf(Key, VerdantLine), FName(TEXT("RoadHops")));
	TestEqual(TEXT("A Miss hop raises nothing"), W.Adapters->CreditHop(Key, EGFJudgement::Miss, EGFHopTileKind::Standard, true), 0);
	for (int32 Hop = 0; Hop < 5; ++Hop)
	{
		W.Adapters->CreditHop(Key, Hop % 2 ? EGFJudgement::Perfect : EGFJudgement::Good, EGFHopTileKind::Standard, true);
	}
	TestEqual(TEXT("Hops reach the climb"), W.StepOf(Key, VerdantLine), FName(TEXT("ScoutClimb")));
	FVector Lip;
	if (!TestTrue(TEXT("Verdant climb lip"), W.LipFor(Verdant, Lip)))
	{
		return false;
	}
	TestEqual(TEXT("Mantle at the Verdant lip"), W.Adapters->CreditMantle(Key, Lip + FVector(50.f, 0.f, 0.f)), 1);
	TestEqual(TEXT("Climb reaches the hand in"), W.StepOf(Key, VerdantLine), FName(TEXT("HandIn")));
	PS->Inventory->AddItem(PlankId, 6);
	TestTrue(TEXT("Six planks hand in"), W.HandIn(PS, Verdant).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Planks charged"), PS->Inventory->GetCount(PlankId), 0);
	TestEqual(TEXT("Waiting on the hire"), W.StepOf(Key, VerdantLine), FName(TEXT("HireEscort")));
	W.Escorts->TickEscorts(0.1f);
	TestTrue(TEXT("Escort spawned"), W.Escorts->HireFor(Key) != nullptr);
	TestTrue(TEXT("Verdant line done"), W.IsDone(Key, VerdantLine));
	TestTrue(TEXT("Done on the player state"), PS->DoneSideLineIds.Contains(VerdantLine));
	TestTrue(TEXT("No Active side line left"), PS->ActiveSideLineId.IsNone());
	TestTrue(TEXT("Errand untouched"), W.ErrandUntouched(Key));
	for (int32 Second = 0; Second < 70 && W.Escorts->GetEscorts().Num() > 0; ++Second)
	{
		W.Escorts->TickEscorts(1.f);
	}
	TestEqual(TEXT("Escort stood down"), W.Escorts->GetEscorts().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestE2ERimeTest, "GFGame.SideQuest.EndToEnd.Rime", GFSideQuestE2ETest::Flags)
bool FGFSideQuestE2ERimeTest::RunTest(const FString&)
{
	// Ice Span: Rime-side ferry landing, four crystals in Rime, a mantle at the climb lip, the crystal hand in.
	using namespace GFSideQuestE2ETest;
	FVector Center;
	if (!TestTrue(TEXT("Rime in the layout"), TownCenter(Rime, Center)))
	{
		return false;
	}
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideE2ERime")))
	{
		return false;
	}
	const FString Key(TEXT("RimeHauler"));
	AGFMagicPlayerState* PS = W.Add(Key);
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	W.Stand(Key, Rime, EGFFactionRank::Member);
	if (!Accept(*this, W, Key, RimeLine, Rime))
	{
		return false;
	}
	TestEqual(TEXT("On the ferry step"), W.StepOf(Key, RimeLine), FName(TEXT("FerryCrossing")));
	RimeFerryAndCrystals(W, Key, Center, 0);
	TestEqual(TEXT("Landing reaches the crystal haul"), W.StepOf(Key, RimeLine), FName(TEXT("CrystalHaul")));
	TestEqual(TEXT("A hide drop raises nothing"), W.Adapters->CreditDrop(Key, TEXT("Hide"), 3, Center), 0);
	W.Adapters->CreditDrop(Key, CrystalId, 3, Center);
	TestEqual(TEXT("Three crystals"), W.CountOf(Key, RimeLine), 3);
	W.Adapters->CreditDrop(Key, CrystalId, 1, Center);
	TestEqual(TEXT("Four crystals reach the climb"), W.StepOf(Key, RimeLine), FName(TEXT("WatchClimb")));
	if (!RimeFinish(*this, W, PS, Key))
	{
		return false;
	}
	TestTrue(TEXT("Done on the player state"), PS->DoneSideLineIds.Contains(RimeLine));
	TestTrue(TEXT("Errand untouched"), W.ErrandUntouched(Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestE2ECinderTest, "GFGame.SideQuest.EndToEnd.Cinder", GFSideQuestE2ETest::Flags)
bool FGFSideQuestE2ECinderTest::RunTest(const FString&)
{
	// Ember Ascent: two member vein breaks, Boost and Downbeat hops, a mantle at the climb lip, a held crystal hand in.
	using namespace GFSideQuestE2ETest;
	FVector Center;
	if (!TestTrue(TEXT("Cinderwright in the layout"), TownCenter(Cinderwright, Center)))
	{
		return false;
	}
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideE2ECinder")))
	{
		return false;
	}
	const FString Key(TEXT("CinderClimber"));
	AGFMagicPlayerState* PS = W.Add(Key);
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	W.Stand(Key, Cinderwrights, EGFFactionRank::Member);
	if (!Accept(*this, W, Key, CinderLine, Cinderwright))
	{
		return false;
	}
	TestEqual(TEXT("On the vein step"), W.StepOf(Key, CinderLine), FName(TEXT("VeinBreak")));
	TestEqual(TEXT("First vein"), W.Adapters->CreditCrystalTaken({ Key }, Center), 1);
	TestEqual(TEXT("Second vein"), W.Adapters->CreditCrystalTaken({ Key }, Center), 1);
	TestEqual(TEXT("Veins reach the hop drill"), W.StepOf(Key, CinderLine), FName(TEXT("HopDrill")));
	W.Adapters->CreditHop(Key, EGFJudgement::Good, EGFHopTileKind::Standard, true);
	TestEqual(TEXT("A Standard tile does not count"), W.CountOf(Key, CinderLine), 0);
	W.Adapters->CreditHop(Key, EGFJudgement::Good, EGFHopTileKind::Boost, true);
	W.Adapters->CreditHop(Key, EGFJudgement::Perfect, EGFHopTileKind::Downbeat, true);
	W.Adapters->CreditHop(Key, EGFJudgement::Good, EGFHopTileKind::Boost, true);
	TestEqual(TEXT("Drill reaches the climb"), W.StepOf(Key, CinderLine), FName(TEXT("KilnClimb")));
	FVector Lip;
	if (!TestTrue(TEXT("Cinderwright climb lip"), W.LipFor(Cinderwright, Lip)))
	{
		return false;
	}
	TestEqual(TEXT("Mantle at the Cinderwright lip"), W.Adapters->CreditMantle(Key, Lip + FVector(50.f, 0.f, 0.f)), 1);
	TestEqual(TEXT("Climb reaches the hand in"), W.StepOf(Key, CinderLine), FName(TEXT("HotbarDeliver")));
	TestTrue(TEXT("Empty hand is refused"), W.HandIn(PS, Cinderwright).Result != EGFQuestResult::Ok);
	// SideHandInEvent only counts HeldItem when inventory still owns one (same rule as ServerSetHeldItem).
	PS->Inventory->AddItem(CrystalId, 1);
	PS->HeldItem = CrystalId;
	TestTrue(TEXT("Held crystal hands in"), W.HandIn(PS, Cinderwright).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Cinderwright line done"), W.IsDone(Key, CinderLine));
	TestTrue(TEXT("Done on the player state"), PS->DoneSideLineIds.Contains(CinderLine));
	TestTrue(TEXT("Errand untouched"), W.ErrandUntouched(Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestE2ESaveMidwayTest, "GFGame.SideQuest.EndToEnd.SaveMidway", GFSideQuestE2ETest::Flags)
bool FGFSideQuestE2ESaveMidwayTest::RunTest(const FString&)
{
	// Rime: accept, land, two of four crystals, capture the quest blob, close the world, restore in a fresh world,
	// and finish from the saved count.
	using namespace GFSideQuestE2ETest;
	FVector Center;
	if (!TestTrue(TEXT("Rime in the layout"), TownCenter(Rime, Center)))
	{
		return false;
	}
	const FString Key(TEXT("RimeSaver"));
	TArray<uint8> Saved;
	{
		FSideWorld First;
		if (!First.Init(*this, TEXT("GFSideE2ESaveA")))
		{
			return false;
		}
		First.Add(Key);
		First.Stand(Key, Rime, EGFFactionRank::Member);
		if (!Accept(*this, First, Key, RimeLine, Rime))
		{
			return false;
		}
		RimeFerryAndCrystals(First, Key, Center, 2);
		if (!TestEqual(TEXT("Midway on the crystal haul"), First.StepOf(Key, RimeLine), FName(TEXT("CrystalHaul")))
			|| !TestEqual(TEXT("Two crystals before the save"), First.CountOf(Key, RimeLine), 2))
		{
			return false;
		}
		First.Quest->CaptureModeState(Saved);
		First.Adapters->CreditDrop(Key, CrystalId, 2, Center);
		TestEqual(TEXT("The live world moved on after the save"), First.StepOf(Key, RimeLine), FName(TEXT("WatchClimb")));
	}
	if (!TestTrue(TEXT("Blob captured"), Saved.Num() > 0))
	{
		return false;
	}

	FSideWorld Second;
	if (!Second.Init(*this, TEXT("GFSideE2ESaveB")))
	{
		return false;
	}
	AGFMagicPlayerState* PS = Second.Add(Key);
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	Second.Quest->RestoreModeState(Saved);
	Second.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
	Second.Quest->OnModePlayerReady(PS);
	TestEqual(TEXT("Reload is still on the crystal haul"), Second.StepOf(Key, RimeLine), FName(TEXT("CrystalHaul")));
	TestEqual(TEXT("Reload kept the count"), Second.CountOf(Key, RimeLine), 2);
	TestEqual(TEXT("Player state shows the Active line"), PS->ActiveSideLineId, RimeLine);
	TestEqual(TEXT("Player state shows the count"), PS->ActiveSideCount, 2);
	TestTrue(TEXT("Reload kept the errand"), Second.ErrandUntouched(Key));
	Second.Adapters->CreditDrop(Key, CrystalId, 2, Center);
	TestEqual(TEXT("Two more crystals reach the climb"), Second.StepOf(Key, RimeLine), FName(TEXT("WatchClimb")));
	return RimeFinish(*this, Second, PS, Key);
}

namespace GFSideQuestE2ETest
{
	/** Server side: both keys become Rime members on HuntSkins with the shipped side catalog. */
	static UGFQuestSubsystem* NetStand(const TArray<FString>& Keys, FName FactionId)
	{
		UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(GFNetTest::ServerWorld());
		if (!Quest)
		{
			return nullptr;
		}
		Quest->EnsureRegistered();
		TArray<FGFQuestRecord> Records;
		for (const FString& Key : Keys)
		{
			Records.Add(Standing(Key, FactionId, EGFFactionRank::Member));
		}
		TArray<uint8> Bytes;
		FGFModeBytes::Write(StandingSave(Records), Bytes);
		Quest->RestoreModeState(Bytes);
		Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
		return Quest;
	}

	static bool Shows(const AGFPlayerState* PS, FName LineId, FName StepId, int32 Count)
	{
		return PS && PS->ActiveSideLineId == LineId && PS->ActiveSideStepId == StepId && PS->ActiveSideCount == Count
			&& PS->ActiveSideStatus == EGFSideQuestStatus::Active;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetSideQuestE2ETwoPlayersTest, "GFGame.Net.SideQuest.EndToEnd.TwoPlayers", GFSideQuestE2ETest::Flags)
bool FGFNetSideQuestE2ETwoPlayersTest::RunTest(const FString&)
{
	// Both players run the Rime line. The host lands and hauls four crystals; the guest's board does not move.
	// Then the guest lands and hauls two; the host's board does not move. The client sees both apart.
	using namespace GFNetTest;
	using namespace GFSideQuestE2ETest;
	FCtxRef Ctx = Start(this, 2, 45.f);
	TSharedRef<FString> HostKey = MakeShared<FString>();
	TSharedRef<FString> GuestKey = MakeShared<FString>();

	Do(Ctx, [this, Ctx, HostKey, GuestKey]()
	{
		UWorld* Server = ServerWorld();
		const AGFPlayerState* Host = Server ? LocalPlayerState(Server) : nullptr;
		const AGFPlayerState* Guest = ClientWorlds().Num() > 0 ? LocalPlayerState(ClientWorlds()[0]) : nullptr;
		UGFSideQuestAdapters* Adapters = Server ? Server->GetSubsystem<UGFSideQuestAdapters>() : nullptr;
		FVector RimeAt;
		if (!TestNotNull(TEXT("Host"), Host) || !TestNotNull(TEXT("Guest"), Guest) || !TestNotNull(TEXT("Adapters"), Adapters)
			|| !TestTrue(TEXT("Rime in the layout"), TownCenter(Rime, RimeAt)))
		{
			Ctx->bFailed = true;
			return;
		}
		*HostKey = Host->PlayerKey;
		*GuestKey = Guest->PlayerKey;
		UGFQuestSubsystem* Quest = NetStand({ *HostKey, *GuestKey }, Rime);
		if (!TestNotNull(TEXT("Server quest"), Quest)
			|| !TestTrue(TEXT("Host accepts"), Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(*HostKey, RimeLine, Rime)).bAccepted)
			|| !TestTrue(TEXT("Guest accepts"), Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(*GuestKey, RimeLine, Rime)).bAccepted))
		{
			Ctx->bFailed = true;
			return;
		}
		Adapters->CreditFerry(*HostKey, Rime, FarStop);
		Adapters->CreditDrop(*HostKey, CrystalId, 4, RimeAt);
		const FGFSideQuestBoard* GuestBoard = Quest->FindSideBoard(*GuestKey);
		const FGFSideQuestProgress* G = GuestBoard ? GuestBoard->Find(RimeLine) : nullptr;
		TestTrue(TEXT("Host progress leaves the guest on the ferry at zero"), G && G->StepId == FName(TEXT("FerryCrossing")) && G->Count == 0);
		Adapters->CreditFerry(*GuestKey, Rime, FarStop);
		Adapters->CreditDrop(*GuestKey, CrystalId, 2, RimeAt);
		const FGFSideQuestBoard* HostBoard = Quest->FindSideBoard(*HostKey);
		const FGFSideQuestProgress* H = HostBoard ? HostBoard->Find(RimeLine) : nullptr;
		TestTrue(TEXT("Guest progress leaves the host on the climb"), H && H->StepId == FName(TEXT("WatchClimb")) && H->Count == 0);
	});
	WaitUntil(Ctx, TEXT("client sees each player's own Rime step"), 15.f, [HostKey, GuestKey]()
	{
		if (ClientWorlds().Num() == 0)
		{
			return false;
		}
		UWorld* Client = ClientWorlds()[0];
		return Shows(LocalPlayerState(Client), RimeLine, TEXT("CrystalHaul"), 2)
			&& Shows(PlayerStateByKey(Client, *HostKey), RimeLine, TEXT("WatchClimb"), 0)
			&& Shows(PlayerStateByKey(ServerWorld(), *GuestKey), RimeLine, TEXT("CrystalHaul"), 2)
			&& Shows(LocalPlayerState(ServerWorld()), RimeLine, TEXT("WatchClimb"), 0);
	});
	Stop(Ctx);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetSideQuestReplicatesTest, "GFGame.Net.SideQuest.Replicates", GFSideQuestE2ETest::Flags)
bool FGFNetSideQuestReplicatesTest::RunTest(const FString&)
{
	// Applied on the server, observed on the client player states: the guest's Active Verdant step and count, and the
	// host's finished Cinderwright line in the Done list with no Active line left.
	using namespace GFNetTest;
	using namespace GFSideQuestE2ETest;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> HostKey = MakeShared<FString>();
	TSharedRef<FString> GuestKey = MakeShared<FString>();

	Do(Ctx, [this, Ctx, HostKey, GuestKey]()
	{
		const AGFPlayerState* Host = LocalPlayerState(ServerWorld());
		const AGFPlayerState* Guest = ClientWorlds().Num() > 0 ? LocalPlayerState(ClientWorlds()[0]) : nullptr;
		if (!TestNotNull(TEXT("Host"), Host) || !TestNotNull(TEXT("Guest"), Guest))
		{
			Ctx->bFailed = true;
			return;
		}
		*HostKey = Host->PlayerKey;
		*GuestKey = Guest->PlayerKey;
		UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(ServerWorld());
		if (!TestNotNull(TEXT("Server quest"), Quest))
		{
			Ctx->bFailed = true;
			return;
		}
		Quest->EnsureRegistered();
		TArray<uint8> Bytes;
		FGFModeBytes::Write(StandingSave({ Standing(*HostKey, Cinderwrights, EGFFactionRank::Member), Standing(*GuestKey, Verdant, EGFFactionRank::Member) }), Bytes);
		Quest->RestoreModeState(Bytes);
		Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());

		Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(*GuestKey, VerdantLine, Verdant));
		Quest->ApplySideEvent(FGFSideQuestEvents::Progress(*GuestKey, EGFSideQuestEventKind::TreeBroken, Verdant, 2));

		Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(*HostKey, CinderLine, Cinderwright));
		Quest->ApplySideEvent(FGFSideQuestEvents::Progress(*HostKey, EGFSideQuestEventKind::CrystalTaken, Cinderwright, 2));
		for (const TCHAR* Tile : { TEXT("Boost"), TEXT("Downbeat"), TEXT("Boost") })
		{
			FGFSideQuestEvent Hop = FGFSideQuestEvents::Progress(*HostKey, EGFSideQuestEventKind::HopLanded);
			Hop.ItemId = Tile;
			Quest->ApplySideEvent(Hop);
		}
		Quest->ApplySideEvent(FGFSideQuestEvents::Progress(*HostKey, EGFSideQuestEventKind::ClimbMantled, Cinderwright));
		TestTrue(TEXT("Host finishes on the server"), Quest->ApplySideEvent(FGFSideQuestEvents::HeldDelivered(*HostKey, CrystalId, Cinderwright)).bCompleted);
	});
	WaitUntil(Ctx, TEXT("client sees the guest's Active line and the host's Done line"), 15.f, [HostKey]()
	{
		if (ClientWorlds().Num() == 0)
		{
			return false;
		}
		UWorld* Client = ClientWorlds()[0];
		const AGFPlayerState* HostOnClient = PlayerStateByKey(Client, *HostKey);
		return Shows(LocalPlayerState(Client), VerdantLine, TEXT("GatherWood"), 2)
			&& HostOnClient && HostOnClient->DoneSideLineIds.Contains(CinderLine)
			&& HostOnClient->ActiveSideLineId.IsNone() && HostOnClient->ActiveSideStatus == EGFSideQuestStatus::Locked;
	});
	Do(Ctx, [this]()
	{
		const AGFPlayerState* Owner = ClientWorlds().Num() > 0 ? LocalPlayerState(ClientWorlds()[0]) : nullptr;
		if (TestNotNull(TEXT("Owner"), Owner))
		{
			TestFalse(TEXT("Owner has not finished Verdant"), Owner->DoneSideLineIds.Contains(VerdantLine));
			TestTrue(TEXT("Owner errand standing kept"), Owner->QuestStep == EGFQuestStep::HuntSkins);
		}
	});
	Stop(Ctx);
	return true;
}

#endif
