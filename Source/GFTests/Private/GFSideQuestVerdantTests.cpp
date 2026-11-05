// Verdant town line (QL-c): the shipped Rootward Run walked step by step on the pure board (Builtin and CSV),
// its refusals, the escort hire rules, the escort hire in a world (spawn, HireSpawned, stand down), and the
// Verdant climb lip fallback from DefaultGame.ini.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.SideQuest;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFDino.h"
#include "GFElderTalk.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFModeTypes.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestAdapters.h"
#include "GFSideQuestEscort.h"
#include "GFSideQuestLine.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFTownNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestVerdantTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwright(TEXT("Cinderwright"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName LineId(TEXT("VerdantRootward"));
	static const FName RimeLine(TEXT("RimeIceSpan"));
	static const FName CinderLine(TEXT("CinderEmberAscent"));
	static const FName PlankId(TEXT("WoodPlank"));
	static const FName DirtId(TEXT("Dirt"));

	/** The Verdant table from the quest lines plan, in order. */
	struct FWantStep
	{
		const TCHAR* StepId;
		EGFSideQuestEventKind Kind;
		int32 Goal;
		TArray<FName> Items;
	};

	static TArray<FWantStep> Want()
	{
		return {
			{ TEXT("Accept"), EGFSideQuestEventKind::TalkAccepted, 1, {} },
			{ TEXT("GatherWood"), EGFSideQuestEventKind::TreeBroken, 3, {} },
			{ TEXT("RoadHops"), EGFSideQuestEventKind::HopLanded, 5, {} },
			{ TEXT("ScoutClimb"), EGFSideQuestEventKind::ClimbMantled, 1, {} },
			{ TEXT("HandIn"), EGFSideQuestEventKind::TurnIn, 6, { PlankId } },
			{ TEXT("HireEscort"), EGFSideQuestEventKind::HireSpawned, 1, {} },
		};
	}

	static void CheckSpec(FAutomationTestBase& T, const FGFSideQuestLineRule* Rule, const TCHAR* Source)
	{
		if (!T.TestNotNull(*FString::Printf(TEXT("%s has the Verdant line"), Source), Rule))
		{
			return;
		}
		T.TestEqual(*FString::Printf(TEXT("%s town"), Source), Rule->TownId, Verdant);
		T.TestEqual(*FString::Printf(TEXT("%s title"), Source), Rule->Title, FString(TEXT("Rootward Run")));
		T.TestTrue(*FString::Printf(TEXT("%s unlocks at Member"), Source), Rule->UnlockRank == EGFFactionRank::Member);
		T.TestTrue(*FString::Printf(TEXT("%s playable"), Source), Rule->IsPlayable());
		const TArray<FWantStep> Steps = Want();
		if (!T.TestEqual(*FString::Printf(TEXT("%s step count"), Source), Rule->Steps.Num(), Steps.Num()))
		{
			return;
		}
		for (int32 Index = 0; Index < Steps.Num(); ++Index)
		{
			const FGFSideQuestStepRule& Got = Rule->Steps[Index];
			const FString Name = FString::Printf(TEXT("%s %s"), Source, Steps[Index].StepId);
			T.TestEqual(*(Name + TEXT(" id")), Got.StepId, FName(Steps[Index].StepId));
			T.TestTrue(*(Name + TEXT(" kind")), Got.AdvanceKind == Steps[Index].Kind);
			T.TestEqual(*(Name + TEXT(" goal")), Got.Goal, Steps[Index].Goal);
			T.TestTrue(*(Name + TEXT(" items")), Got.Items == Steps[Index].Items);
			T.TestFalse(*(Name + TEXT(" title")), Got.Title.IsEmpty());
			T.TestFalse(*(Name + TEXT(" hint")), Got.Hint.IsEmpty());
		}
	}

	static FGFSideQuestLine Player(FName Faction, EGFFactionRank Rank, const FGFSideQuestCatalog* Rules = &FGFSideQuestCatalog::Builtin())
	{
		FGFSideQuestLine Line;
		Line.Catalog = Rules;
		Line.FactionId = Faction;
		Line.Rank = Rank;
		Line.Refresh();
		return Line;
	}

	static FName StepOf(const FGFSideQuestLine& Line, FName Id)
	{
		const FGFSideQuestProgress* P = Line.Board.Find(Id);
		return P && P->Status == EGFSideQuestStatus::Active ? P->StepId : NAME_None;
	}

	static int32 CountOf(const FGFSideQuestLine& Line, FName Id)
	{
		const FGFSideQuestProgress* P = Line.Board.Find(Id);
		return P ? P->Count : -1;
	}

	static EGFSideQuestStatus StatusOf(const FGFSideQuestLine& Line, FName Id)
	{
		const FGFSideQuestProgress* P = Line.Board.Find(Id);
		return P ? P->Status : EGFSideQuestStatus::Locked;
	}

	static FGFSideQuestEvent Event(EGFSideQuestEventKind Kind, FName TownId = NAME_None, int32 Count = 1)
	{
		return FGFSideQuestEvents::Progress(TEXT("P"), Kind, TownId, Count);
	}

	/** A small world with a game state, the quest subsystem, the adapters and the escorts. */
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFSideQuestAdapters* Adapters = nullptr;
		UGFSideQuestEscorts* Escorts = nullptr;
		TArray<FGFQuestRecord> Records;
		int32 NextId = 700;

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
			Quest = World->GetSubsystem<UGFQuestSubsystem>();
			Adapters = World->GetSubsystem<UGFSideQuestAdapters>();
			Escorts = World->GetSubsystem<UGFSideQuestEscorts>();
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest)
				&& T.TestNotNull(TEXT("Side quest adapters"), Adapters) && T.TestNotNull(TEXT("Side quest escorts"), Escorts);
		}

		AGFPlayerState* Add(const FString& Key)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
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

		void Stand(const FString& Key, FName FactionId, EGFFactionRank Rank)
		{
			FGFQuestRecord& Record = Records.AddDefaulted_GetRef();
			Record.PlayerKey = Key;
			Record.Progress.Step = EGFQuestStep::HuntSkins;
			Record.Progress.FactionId = FactionId;
			Record.Progress.Rank = Rank;
			FGFQuestSave Save;
			Save.Records = Records;
			TArray<uint8> Bytes;
			FGFModeBytes::Write(Save, Bytes);
			Quest->RestoreModeState(Bytes);
		}

		FName StepOf(const FString& Key) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			const FGFSideQuestProgress* P = Board ? Board->Find(LineId) : nullptr;
			return P && P->Status == EGFSideQuestStatus::Active ? P->StepId : NAME_None;
		}

		bool IsDone(const FString& Key) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			const FGFSideQuestProgress* P = Board ? Board->Find(LineId) : nullptr;
			return P && P->Status == EGFSideQuestStatus::Done;
		}

		/** What the Hand in town task button does on the server. */
		FGFSideQuestApplyResult HandIn(AGFPlayerState* PS)
		{
			FGFSideQuestApplyResult None;
			None.Reason = FGFQuestCodes::OutOfOrder;
			const FGFQuestProgress* Errand = Quest->FindProgress(PS->PlayerKey);
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(PS->PlayerKey);
			const FGFSideQuestBoard Empty;
			const FGFElderTalk::FSideOffer Offer = FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
				Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, Verdant);
			FGFSideQuestEvent Out;
			const FGFInventory NoItems;
			if (!FGFElderTalk::SideHandInEvent(Offer, PS->PlayerKey, Verdant, PS->HeldItem, PS->Inventory ? PS->Inventory->GetInventory() : NoItems, Out))
			{
				return None;
			}
			return Quest->ApplySideEvent(Out);
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

	static bool VerdantCenter(FVector& Out)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		if (!Defaults)
		{
			return false;
		}
		for (const FGFTerritoryFaction& Faction : Defaults->FactionLayout)
		{
			if (FGFSideQuestCatalog::SameTown(Faction.Id, Verdant))
			{
				Out = Faction.Center;
				return true;
			}
		}
		return false;
	}

	static int32 DinoCount(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			N += (*It)->IsActorBeingDestroyed() ? 0 : 1;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestVerdantTransitionsTest, "GFGame.SideQuest.Line.Verdant.Transitions", GFSideQuestVerdantTest::Flags)
bool FGFSideQuestVerdantTransitionsTest::RunTest(const FString&)
{
	using namespace GFSideQuestVerdantTest;
	const FGFSideQuestCatalog& Shipped = FGFSideQuestCatalog::Builtin();
	const FGFSideQuestCatalog Loaded = FGFSideQuestCatalog::Load();
	CheckSpec(*this, Shipped.FindLine(LineId), TEXT("Builtin"));
	CheckSpec(*this, Loaded.FindLine(LineId), TEXT("CSV"));

	// Member walks every step to Done.
	FGFSideQuestLine Member = Player(Verdant, EGFFactionRank::Member);
	TestTrue(TEXT("Member: Verdant line available"), StatusOf(Member, LineId) == EGFSideQuestStatus::Available);
	const FGFSideQuestApplyResult Accepted = Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant));
	TestTrue(TEXT("Accept is Ok"), Accepted.Result == EGFQuestResult::Ok && Accepted.bAccepted);
	TestEqual(TEXT("Accept completes the Accept step"), StepOf(Member, LineId), FName(TEXT("GatherWood")));
	TestTrue(TEXT("Active step is the tree step"), Member.ActiveStep() && Member.ActiveStep()->AdvanceKind == EGFSideQuestEventKind::TreeBroken);

	FGFSideQuestBoard Kept = Member.Board;
	TestTrue(TEXT("A hop before the trees is ignored"), Member.Apply(Event(EGFSideQuestEventKind::HopLanded)).Result == EGFQuestResult::Ignored);
	TestEqual(TEXT("A Rime tree is refused"), Member.Apply(Event(EGFSideQuestEventKind::TreeBroken, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("A hire before the hire step is ignored"), Member.Apply(Event(EGFSideQuestEventKind::HireSpawned, Verdant)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Ignored and refused keep the board"), Member.Board == Kept);

	for (int32 Tree = 0; Tree < 3; ++Tree)
	{
		TestTrue(*FString::Printf(TEXT("Tree %d"), Tree + 1), Member.Apply(Event(EGFSideQuestEventKind::TreeBroken, Verdant)).Result == EGFQuestResult::Ok);
	}
	TestEqual(TEXT("Three trees reach the hops"), StepOf(Member, LineId), FName(TEXT("RoadHops")));
	TestEqual(TEXT("Hop count starts at zero"), CountOf(Member, LineId), 0);
	TestTrue(TEXT("A tree on the hop step is ignored"), Member.Apply(Event(EGFSideQuestEventKind::TreeBroken, Verdant)).Result == EGFQuestResult::Ignored);
	for (int32 Hop = 0; Hop < 4; ++Hop)
	{
		TestTrue(*FString::Printf(TEXT("Hop %d"), Hop + 1), Member.Apply(Event(EGFSideQuestEventKind::HopLanded)).Result == EGFQuestResult::Ok);
	}
	TestEqual(TEXT("Four hops stay on the hop step"), CountOf(Member, LineId), 4);
	TestTrue(TEXT("Hop 5"), Member.Apply(Event(EGFSideQuestEventKind::HopLanded)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Five hops reach the climb"), StepOf(Member, LineId), FName(TEXT("ScoutClimb")));

	TestEqual(TEXT("A Rime climb is refused"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Verdant climb"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Verdant)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Climb reaches the hand in"), StepOf(Member, LineId), FName(TEXT("HandIn")));

	const FGFSideQuestApplyResult Short = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(PlankId, 5) }, Verdant));
	TestTrue(TEXT("Five planks are short"), Short.Result == EGFQuestResult::Refused && Short.Reason == FGFQuestCodes::Short);
	const FGFSideQuestApplyResult Wrong = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(DirtId, 6) }, Verdant));
	TestTrue(TEXT("Dirt is the wrong item"), Wrong.Result == EGFQuestResult::Refused && Wrong.Reason == FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Hand in at Rime is refused"), Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(PlankId, 6) }, Rime)).Reason, FGFQuestCodes::WrongTown);
	const FGFSideQuestApplyResult Paid = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(PlankId, 9), FGFItemStack(DirtId, 2) }, Verdant));
	TestTrue(TEXT("Six planks hand in"), Paid.Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Charge is exactly six planks"), Paid.Charge.Num() == 1 && Paid.Charge[0].ItemId == PlankId && Paid.Charge[0].Count == 6);
	TestEqual(TEXT("Hand in reaches the hire"), StepOf(Member, LineId), FName(TEXT("HireEscort")));
	TestTrue(TEXT("Active step waits on the hire"), Member.ActiveStep() && Member.ActiveStep()->AdvanceKind == EGFSideQuestEventKind::HireSpawned);
	TestFalse(TEXT("Not done before the hire spawns"), Paid.bCompleted);

	const FGFSideQuestApplyResult Hired = Member.Apply(Event(EGFSideQuestEventKind::HireSpawned, Verdant));
	TestTrue(TEXT("Hire spawn completes the line"), Hired.Result == EGFQuestResult::Ok && Hired.bCompleted);
	TestTrue(TEXT("Done"), StatusOf(Member, LineId) == EGFSideQuestStatus::Done);
	TestTrue(TEXT("No Active line after Done"), Member.Board.ActiveLineId.IsNone() && Member.ActiveStep() == nullptr);
	TestTrue(TEXT("Done line accept is ignored"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Result == EGFQuestResult::Ignored);

	// Refusals at the accept.
	TestEqual(TEXT("Rime elder: WrongTown"), Player(Verdant, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Rime member: WrongTown"), Player(Rime, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Cinderwrights member: WrongTown"), Player(Cinderwrights, EGFFactionRank::Leader).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("No faction: WrongTown"), Player(NAME_None, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	FGFSideQuestLine Initiate = Player(Verdant, EGFFactionRank::Initiate);
	TestTrue(TEXT("Initiate: line locked"), StatusOf(Initiate, LineId) == EGFSideQuestStatus::Locked);
	TestEqual(TEXT("Initiate: RankLow"), Initiate.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFSideQuestCodes::RankLow);
	TestEqual(TEXT("Outsider: RankLow"), Player(Verdant, EGFFactionRank::Outsider).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFSideQuestCodes::RankLow);
	TestTrue(TEXT("Leader: line available"), StatusOf(Player(Verdant, EGFFactionRank::Leader), LineId) == EGFSideQuestStatus::Available);

	// OtherActive: a second Verdant line next to the shipped one, while the shipped one is Active.
	FGFSideQuestCatalog Two = Shipped;
	FGFSideQuestLineRule Second = *Shipped.FindLine(LineId);
	Second.LineId = TEXT("TestVerdantSecond");
	Second.Sort = 99;
	Two.Lines.Add(Second);
	Two.Normalize();
	FGFSideQuestLine Busy = Player(Verdant, EGFFactionRank::Member, &Two);
	TestTrue(TEXT("Busy: accept the shipped line"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).bAccepted);
	const FGFSideQuestBoard BusyKept = Busy.Board;
	TestEqual(TEXT("Second Verdant line: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Second.LineId, Verdant)).Reason, FGFSideQuestCodes::OtherActive);
	TestEqual(TEXT("Rime line while busy: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), RimeLine, Rime)).Reason, FGFSideQuestCodes::OtherActive);
	TestTrue(TEXT("OtherActive keeps the board"), Busy.Board == BusyKept);

	// Rime and Cinderwright stay Accept-only stubs.
	for (const FName Stub : { RimeLine, CinderLine })
	{
		const FGFSideQuestLineRule* Rule = Shipped.FindLine(Stub);
		if (!TestNotNull(*FString::Printf(TEXT("%s shipped"), *Stub.ToString()), Rule))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s is Accept only"), *Stub.ToString()), Rule->Steps.Num(), 1);
		FGFSideQuestLine Line = Player(Rule->TownId, EGFFactionRank::Leader);
		TestTrue(*FString::Printf(TEXT("%s locked"), *Stub.ToString()), StatusOf(Line, Stub) == EGFSideQuestStatus::Locked);
		TestEqual(*FString::Printf(TEXT("%s not ready"), *Stub.ToString()), Line.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Stub, Rule->TownId)).Reason, FGFSideQuestCodes::NotReady);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestEscortRulesTest, "GFGame.SideQuest.Hire.EscortRules", GFSideQuestVerdantTest::Flags)
bool FGFSideQuestEscortRulesTest::RunTest(const FString&)
{
	using namespace GFSideQuestVerdantTest;
	using FRules = FGFSideQuestEscortRules;
	TestEqual(TEXT("Tusksaw hire"), FRules::Species(), FName(TEXT("Tusksaw")));
	TestEqual(TEXT("About a minute"), FRules::EscortSeconds, 60.f);

	TArray<FGFSideQuestEscort> List;
	TestFalse(TEXT("No key, no escort"), FRules::Ask(List, FString(), LineId, Verdant));
	TestTrue(TEXT("Ask"), FRules::Ask(List, TEXT("A"), LineId, Verdant));
	TestFalse(TEXT("A second ask for the same player is refused"), FRules::Ask(List, TEXT("A"), LineId, Verdant));
	TestTrue(TEXT("Another player may ask"), FRules::Ask(List, TEXT("B"), LineId, Verdant));
	TestEqual(TEXT("Two escorts"), List.Num(), 2);

	FGFSideQuestEscort& A = List[0];
	TestTrue(TEXT("Pending with no body waits"), FRules::Advance(A, 1.f, false, false) == EGFSideQuestEscortAction::None);
	TestTrue(TEXT("Pending with a body spawns"), FRules::Advance(A, 1.f, true, false) == EGFSideQuestEscortAction::Spawn);
	FRules::Spawned(A);
	TestTrue(TEXT("Escorting"), A.Phase == EGFSideQuestEscortPhase::Escorting);
	TestEqual(TEXT("Escort clock"), A.Left, FRules::EscortSeconds);
	TestTrue(TEXT("Mid escort keeps walking"), FRules::Advance(A, 59.f, true, true) == EGFSideQuestEscortAction::None);
	TestTrue(TEXT("Time up stands down"), FRules::Advance(A, 1.f, true, true) == EGFSideQuestEscortAction::StandDown);
	TestTrue(TEXT("Done after stand down"), A.Phase == EGFSideQuestEscortPhase::Done);
	TestTrue(TEXT("Done does nothing more"), FRules::Advance(A, 1.f, true, true) == EGFSideQuestEscortAction::None);
	TestTrue(TEXT("Done frees the player for a new ask"), FRules::Ask(List, TEXT("A"), LineId, Verdant));

	FGFSideQuestEscort Gone;
	FRules::Spawned(Gone);
	TestTrue(TEXT("Player gone stands down"), FRules::Advance(Gone, 0.1f, false, true) == EGFSideQuestEscortAction::StandDown);
	FGFSideQuestEscort Lost;
	FRules::Spawned(Lost);
	TestTrue(TEXT("Hire gone stands down"), FRules::Advance(Lost, 0.1f, true, false) == EGFSideQuestEscortAction::StandDown);

	FGFSideQuestEscort Waiting;
	Waiting.Left = FRules::PendingSeconds;
	TestTrue(TEXT("Long wait with no body gives up"), FRules::Advance(Waiting, FRules::PendingSeconds + 1.f, false, false) == EGFSideQuestEscortAction::GiveUp);

	TestFalse(TEXT("Close: idle"), FRules::ShouldFollow(false, 500.f));
	TestTrue(TEXT("Far: follow"), FRules::ShouldFollow(false, FRules::FollowStartCm + 100.f));
	TestTrue(TEXT("Following keeps going in the band"), FRules::ShouldFollow(true, 500.f));
	TestFalse(TEXT("Following stops when close"), FRules::ShouldFollow(true, FRules::FollowStopCm - 50.f));

	TestTrue(TEXT("Calm about to end renews"), FRules::ShouldRenewCalm(10.f, 11.f));
	TestFalse(TEXT("Fresh calm is kept"), FRules::ShouldRenewCalm(10.f, 20.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestVerdantWorldTest, "GFGame.SideQuest.Hire.Escort", GFSideQuestVerdantTest::Flags)
bool FGFSideQuestVerdantWorldTest::RunTest(const FString&)
{
	// The whole shipped Verdant line through the subsystem and real hooks: town trees, hops, a mantle at the config
	// climb lip, the elder hand in, then the escort: it spawns near the player, HireSpawned finishes the line, it
	// walks after the player, and after about 60 s it stands down and is gone. No theft case and no errand events.
	using namespace GFSideQuestVerdantTest;
	FVector Center;
	if (!TestTrue(TEXT("Verdant in the layout"), VerdantCenter(Center)))
	{
		return false;
	}
	FWorld W;
	if (!W.Init(*this, TEXT("GFSideVerdantEscort")))
	{
		return false;
	}
	const FString Key(TEXT("Walker"));
	AGFPlayerState* PS = W.Add(Key);
	AGFCharacter* Pawn = W.Body(PS, Center + FVector(0.f, -300.f, 100.f));
	UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr) || !TestNotNull(TEXT("Movement"), Move))
	{
		return false;
	}
	W.Stand(Key, Verdant, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());

	int32 ErrandEvents = 0;
	const FDelegateHandle ErrandHandle = FGFQuestEvents::OnRaised().AddLambda([&ErrandEvents, &W](UWorld* At, const FGFQuestEvent&)
	{
		ErrandEvents += At == W.World ? 1 : 0;
	});
	int32 HireEvents = 0;
	const FDelegateHandle SideHandle = FGFSideQuestEvents::OnRaised().AddLambda([&HireEvents, &W](UWorld* At, const FGFSideQuestEvent& Event)
	{
		HireEvents += (At == W.World && Event.Kind == EGFSideQuestEventKind::HireSpawned) ? 1 : 0;
	});
	ON_SCOPE_EXIT
	{
		FGFQuestEvents::OnRaised().Remove(ErrandHandle);
		FGFSideQuestEvents::OnRaised().Remove(SideHandle);
	};

	if (!TestTrue(TEXT("Accepted"), W.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineId, Verdant)).bAccepted))
	{
		return false;
	}
	for (int32 Tree = 0; Tree < 3; ++Tree)
	{
		FGFTownNodeEvents::OnTreeBroken().Broadcast(W.World, *FString::Printf(TEXT("VerdantTree%d"), Tree), Center, { Key });
	}
	TestEqual(TEXT("Town trees reach the hops"), W.StepOf(Key), FName(TEXT("RoadHops")));
	for (int32 Hop = 0; Hop < 5; ++Hop)
	{
		// The hop adapter only raises Good or better landings; that filter has its own test (Adapter.Hop).
		FGFSideQuestEvents::Raise(W.World, FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::HopLanded));
	}
	TestEqual(TEXT("Hops reach the climb"), W.StepOf(Key), FName(TEXT("ScoutClimb")));

	W.Adapters->RefreshClimbLips();
	const FGFQuestClimbLip* Lip = W.Adapters->GetClimbLips().FindByPredicate([](const FGFQuestClimbLip& L) { return L.TownId == Verdant; });
	if (!TestNotNull(TEXT("Config fallback gives Verdant a climb lip"), Lip))
	{
		return false;
	}
	UGFCharacterMovementComponent::OnServerMantleFinished.Broadcast(Move, Lip->Location + FVector(50.f, 0.f, 0.f));
	TestEqual(TEXT("Mantle at the lip reaches the hand in"), W.StepOf(Key), FName(TEXT("HandIn")));

	PS->Inventory->AddItem(PlankId, 4);
	TestEqual(TEXT("Four planks are short"), W.HandIn(PS).Reason, FGFQuestCodes::Short);
	TestEqual(TEXT("Short keeps the planks"), PS->Inventory->GetCount(PlankId), 4);
	PS->Inventory->AddItem(PlankId, 3);
	TestTrue(TEXT("Seven planks hand in"), W.HandIn(PS).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Six planks charged"), PS->Inventory->GetCount(PlankId), 1);
	TestEqual(TEXT("Waiting on the hire"), W.StepOf(Key), FName(TEXT("HireEscort")));
	FName WaitLine;
	FName WaitTown;
	TestTrue(TEXT("Subsystem says the player waits on a hire"), W.Quest->WaitsOnHire(Key, WaitLine, WaitTown) && WaitLine == LineId && WaitTown == Verdant);
	TestEqual(TEXT("The ask queues one escort"), W.Escorts->GetEscorts().Num(), 1);
	TestEqual(TEXT("Nothing spawns inside the hand in"), DinoCount(W.World), 0);
	TestEqual(TEXT("No hire event before the spawn"), HireEvents, 0);

	W.Escorts->TickEscorts(0.1f);
	TWeakObjectPtr<AGFDino> Hire = W.Escorts->HireFor(Key);
	if (!TestTrue(TEXT("Escort spawned"), Hire.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("One dino"), DinoCount(W.World), 1);
	TestEqual(TEXT("HireSpawned raised once"), HireEvents, 1);
	TestTrue(TEXT("Spawned near the player"), FVector::Dist2D(Hire->GetActorLocation(), Pawn->GetActorLocation()) <= FGFSideQuestEscortRules::SpawnMaxCm + 1.f);
	TestEqual(TEXT("Tusksaw"), Hire->Taming ? Hire->Taming->Species : NAME_None, FName(TEXT("Tusksaw")));
	TestTrue(TEXT("A hire, not a kill"), Hire->IsRental());
	TestTrue(TEXT("Calm, so it never turns on the player"), Hire->Taming && Hire->Taming->CalmedUntil > W.World->GetTimeSeconds());
	TestTrue(TEXT("Not tamed by the escort"), Hire->Taming && Hire->Taming->GetTameState() != EGFTameState::Tamed);
	TestTrue(TEXT("Line done when the hire spawns"), W.IsDone(Key));
	TestTrue(TEXT("Done replicates"), PS->DoneSideLineIds.Contains(LineId));
	TestFalse(TEXT("No longer waiting"), W.Quest->WaitsOnHire(Key, WaitLine, WaitTown));

	Pawn->SetActorLocation(Hire->GetActorLocation() + FVector(FGFSideQuestEscortRules::FollowStartCm + 400.f, 0.f, 0.f));
	W.Escorts->TickEscorts(1.f);
	TestTrue(TEXT("Walks after a far player"), W.Escorts->GetEscorts().Num() == 1 && W.Escorts->GetEscorts()[0].bFollowing);
	for (int32 Second = 0; Second < 57; ++Second)
	{
		W.Escorts->TickEscorts(1.f);
	}
	TestTrue(TEXT("Still out before a minute"), W.Escorts->HireFor(Key) != nullptr);
	for (int32 Second = 0; Second < 3; ++Second)
	{
		W.Escorts->TickEscorts(1.f);
	}
	TestTrue(TEXT("Stood down after about a minute"), W.Escorts->GetEscorts().Num() == 0 && W.Escorts->HireFor(Key) == nullptr);
	TestTrue(TEXT("Hire spawn is despawned"), !Hire.IsValid() || Hire->IsActorBeingDestroyed());
	TestEqual(TEXT("No dinos left"), DinoCount(W.World), 0);
	TestEqual(TEXT("HireSpawned still raised once"), HireEvents, 1);
	TestEqual(TEXT("No errand or theft events"), ErrandEvents, 0);
	TestTrue(TEXT("Errand untouched"), W.Quest->FindProgress(Key) && W.Quest->FindProgress(Key)->Step == EGFQuestStep::HuntSkins);

	// A player who comes back still waiting on the hire (saved mid line) is asked for again.
	const FString Back(TEXT("Returner"));
	AGFPlayerState* Returner = W.Add(Back);
	W.Stand(Back, Verdant, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
	{
		FGFQuestSave Save;
		Save.Records = W.Records;
		FGFSideQuestBoardRecord& Saved = Save.SideBoards.AddDefaulted_GetRef();
		Saved.PlayerKey = Back;
		FGFSideQuestProgress Waiting;
		Waiting.LineId = LineId;
		Waiting.StepId = TEXT("HireEscort");
		Waiting.Status = EGFSideQuestStatus::Active;
		Saved.Board.Lines.Add(Waiting);
		Saved.Board.ActiveLineId = LineId;
		TArray<uint8> Bytes;
		FGFModeBytes::Write(Save, Bytes);
		W.Quest->RestoreModeState(Bytes);
	}
	TestEqual(TEXT("Restore alone does not spawn"), W.Escorts->GetEscorts().Num(), 0);
	W.Quest->OnModePlayerReady(Returner);
	TestEqual(TEXT("Player ready asks again"), W.Escorts->GetEscorts().Num(), 1);
	W.Quest->OnModePlayerReady(Returner);
	TestEqual(TEXT("A repeat ask does not add a second escort"), W.Escorts->GetEscorts().Num(), 1);
	W.Escorts->TickEscorts(1.f);
	TestEqual(TEXT("No body yet: nothing spawned"), DinoCount(W.World), 0);
	W.Body(Returner, Center + FVector(300.f, 0.f, 100.f));
	W.Escorts->TickEscorts(0.1f);
	TestTrue(TEXT("Returner gets the escort"), W.Escorts->HireFor(Back) != nullptr);
	TestTrue(TEXT("Returner line done"), W.IsDone(Back));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestVerdantClimbConfigTest, "GFGame.SideQuest.Line.Verdant.ClimbConfig", GFSideQuestVerdantTest::Flags)
bool FGFSideQuestVerdantClimbConfigTest::RunTest(const FString&)
{
	// The shipped fallback stays on Verdant land: lip plus radius inside the 1-member disc.
	using namespace GFSideQuestVerdantTest;
	const UGFSideQuestAdapters* Defaults = GetDefault<UGFSideQuestAdapters>();
	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	if (!TestNotNull(TEXT("Adapter defaults"), Defaults) || !TestNotNull(TEXT("Territory defaults"), Territory))
	{
		return false;
	}
	const FGFQuestClimbLipConfig* Fallback = Defaults->ClimbLipFallbacks.FindByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestVerdantTest::Verdant);
	});
	if (!TestNotNull(TEXT("Verdant fallback in DefaultGame.ini"), Fallback))
	{
		return false;
	}
	TestEqual(TEXT("Radius kept at 400"), Defaults->ClimbLipRadiusCm, 400.f);
	const FGFTerritoryFaction* Faction = Territory->FactionLayout.FindByPredicate([](const FGFTerritoryFaction& F)
	{
		return FGFSideQuestCatalog::SameTown(F.Id, GFSideQuestVerdantTest::Verdant);
	});
	if (!TestNotNull(TEXT("Verdant faction"), Faction) || !TestTrue(TEXT("Verdant has a 1-member radius"), Faction->RadiusByMembers.Num() > 1))
	{
		return false;
	}
	TestTrue(TEXT("Lip and radius inside the 1-member disc"), Fallback->Offset.Size2D() + Defaults->ClimbLipRadiusCm <= Faction->RadiusByMembers[1]);
	TestTrue(TEXT("Lip clear of the elder and the villagers"), Fallback->Offset.Size2D() > 400.f + Defaults->ClimbLipRadiusCm);
	return true;
}

#endif
