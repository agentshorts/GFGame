// Rime town line (QL-d): the shipped Ice Span walked step by step on the pure board (Builtin and CSV), its refusals,
// the ferry step (Rime-side departure preferred, no-anchor fallback), crystal credit keyed to the receiving player
// only, the whole line in a world through the real hooks, and the Rime climb lip fallback from DefaultGame.ini.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.SideQuest;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFCharacter.h"
#include "GFElderTalk.h"
#include "GFFerryDirector.h"
#include "GFFerryRiderComponent.h"
#include "GFFerryRules.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFModeTypes.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestAdapters.h"
#include "GFSideQuestLine.h"
#include "GFTerritorySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestRimeTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName LineId(TEXT("RimeIceSpan"));
	static const FName VerdantLine(TEXT("VerdantRootward"));
	static const FName CinderLine(TEXT("CinderEmberAscent"));
	static const FName CrystalId(TEXT("Crystal"));
	static const FName DirtId(TEXT("Dirt"));
	static const FName HideId(TEXT("Hide"));
	static const FName RimeStop(TEXT("RimeLanding"));

	/** Far outside every town. */
	static const FVector Wild(4.0e6, -4.0e6, 0.0);

	/** The Rime table from the quest lines plan, in order. */
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
			{ TEXT("FerryCrossing"), EGFSideQuestEventKind::FerryArrived, 1, {} },
			{ TEXT("CrystalHaul"), EGFSideQuestEventKind::CrystalGathered, 4, { CrystalId } },
			{ TEXT("WatchClimb"), EGFSideQuestEventKind::ClimbMantled, 1, {} },
			{ TEXT("HandIn"), EGFSideQuestEventKind::TurnIn, 4, { CrystalId } },
		};
	}

	static void CheckSpec(FAutomationTestBase& T, const FGFSideQuestLineRule* Rule, const TCHAR* Source)
	{
		if (!T.TestNotNull(*FString::Printf(TEXT("%s has the Rime line"), Source), Rule))
		{
			return;
		}
		T.TestEqual(*FString::Printf(TEXT("%s town"), Source), Rule->TownId, Rime);
		T.TestEqual(*FString::Printf(TEXT("%s title"), Source), Rule->Title, FString(TEXT("Ice Span")));
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
			T.TestEqual(*(Name + TEXT(" order")), Got.Order, Index + 1);
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

	/** What the crystal drop adapter raises: CrystalGathered carrying the Crystal item and the town at the drop. */
	static FGFSideQuestEvent Crystal(FName TownId, int32 Count)
	{
		FGFSideQuestEvent Out = Event(EGFSideQuestEventKind::CrystalGathered, TownId, Count);
		Out.ItemId = CrystalId;
		return Out;
	}

	static bool TownCenter(FName TownId, FVector& Out, const FGFTerritoryFaction** OutFaction = nullptr)
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
				if (OutFaction)
				{
					*OutFaction = &Faction;
				}
				return true;
			}
		}
		return false;
	}

	/** A small world with a game state, the quest subsystem and the adapters. */
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFSideQuestAdapters* Adapters = nullptr;
		TArray<FGFQuestRecord> Records;
		int32 NextId = 900;

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
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest)
				&& T.TestNotNull(TEXT("Side quest adapters"), Adapters);
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

		const FGFSideQuestProgress* Progress(const FString& Key) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			return Board ? Board->Find(LineId) : nullptr;
		}

		FName StepOf(const FString& Key) const
		{
			const FGFSideQuestProgress* P = Progress(Key);
			return P && P->Status == EGFSideQuestStatus::Active ? P->StepId : NAME_None;
		}

		int32 CountOf(const FString& Key) const
		{
			const FGFSideQuestProgress* P = Progress(Key);
			return P ? P->Count : -1;
		}

		bool IsDone(const FString& Key) const
		{
			const FGFSideQuestProgress* P = Progress(Key);
			return P && P->Status == EGFSideQuestStatus::Done;
		}

		/** What the Hand in town task button does on the server, at the Rime elder. */
		FGFSideQuestApplyResult HandIn(AGFPlayerState* PS)
		{
			FGFSideQuestApplyResult None;
			None.Reason = FGFQuestCodes::OutOfOrder;
			const FGFQuestProgress* Errand = Quest->FindProgress(PS->PlayerKey);
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(PS->PlayerKey);
			const FGFSideQuestBoard Empty;
			const FGFElderTalk::FSideOffer Offer = FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
				Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, Rime);
			FGFSideQuestEvent Out;
			const FGFInventory NoItems;
			if (!FGFElderTalk::SideHandInEvent(Offer, PS->PlayerKey, Rime, PS->HeldItem, PS->Inventory ? PS->Inventory->GetInventory() : NoItems, Out))
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestRimeTransitionsTest, "GFGame.SideQuest.Line.Rime.Transitions", GFSideQuestRimeTest::Flags)
bool FGFSideQuestRimeTransitionsTest::RunTest(const FString&)
{
	using namespace GFSideQuestRimeTest;
	const FGFSideQuestCatalog& Shipped = FGFSideQuestCatalog::Builtin();
	const FGFSideQuestCatalog Loaded = FGFSideQuestCatalog::Load();
	CheckSpec(*this, Shipped.FindLine(LineId), TEXT("Builtin"));
	CheckSpec(*this, Loaded.FindLine(LineId), TEXT("CSV"));

	// Member walks every step to Done.
	FGFSideQuestLine Member = Player(Rime, EGFFactionRank::Member);
	TestTrue(TEXT("Member: Rime line available"), StatusOf(Member, LineId) == EGFSideQuestStatus::Available);
	TestTrue(TEXT("Member: Verdant line stays locked for a Rime member"), StatusOf(Member, VerdantLine) == EGFSideQuestStatus::Locked);
	const FGFSideQuestApplyResult Accepted = Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime));
	TestTrue(TEXT("Accept is Ok"), Accepted.Result == EGFQuestResult::Ok && Accepted.bAccepted);
	TestEqual(TEXT("Accept completes the Accept step"), StepOf(Member, LineId), FName(TEXT("FerryCrossing")));
	TestTrue(TEXT("Active step is the ferry step"), Member.ActiveStep() && Member.ActiveStep()->AdvanceKind == EGFSideQuestEventKind::FerryArrived);

	FGFSideQuestBoard Kept = Member.Board;
	TestTrue(TEXT("Crystal before the ferry is ignored"), Member.Apply(Crystal(Rime, 2)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("A climb before the ferry is ignored"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Rime)).Result == EGFQuestResult::Ignored);
	// Ferry: the adapter puts the departure stop's anchor in TownId. A flight that leaves from another town's stop is refused.
	TestEqual(TEXT("A flight from the Verdant stop is refused"), Member.Apply(Event(EGFSideQuestEventKind::FerryArrived, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("A flight from the Cinderwrights stop is refused"), Member.Apply(Event(EGFSideQuestEventKind::FerryArrived, Cinderwrights)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Ignored and refused keep the board"), Member.Board == Kept);

	// Preferred: a finished flight that left from a Rime-side stop (RimeLanding has AnchorId Rime).
	TestTrue(TEXT("Flight from the Rime stop"), Member.Apply(Event(EGFSideQuestEventKind::FerryArrived, Rime)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Ferry reaches the crystal haul"), StepOf(Member, LineId), FName(TEXT("CrystalHaul")));
	TestEqual(TEXT("Crystal count starts at zero"), CountOf(Member, LineId), 0);

	TestTrue(TEXT("A second flight is ignored"), Member.Apply(Event(EGFSideQuestEventKind::FerryArrived, Rime)).Result == EGFQuestResult::Ignored);
	TestEqual(TEXT("A Verdant crystal is refused"), Member.Apply(Crystal(Verdant, 4)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("A gather with no item is refused"), Member.Apply(Event(EGFSideQuestEventKind::CrystalGathered, Rime, 4)).Reason, FGFQuestCodes::WrongItem);
	FGFSideQuestEvent Hide = Crystal(Rime, 4);
	Hide.ItemId = HideId;
	TestEqual(TEXT("A gather of another item is refused"), Member.Apply(Hide).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Refusals leave the count"), CountOf(Member, LineId), 0);
	TestTrue(TEXT("Two Rime crystals"), Member.Apply(Crystal(Rime, 2)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Count is the drop size"), CountOf(Member, LineId), 2);
	TestTrue(TEXT("One wild crystal"), Member.Apply(Crystal(NAME_None, 1)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Three crystals stay on the haul"), StepOf(Member, LineId), FName(TEXT("CrystalHaul")));
	TestTrue(TEXT("A big drop finishes the haul"), Member.Apply(Crystal(Rime, 5)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Haul reaches the climb"), StepOf(Member, LineId), FName(TEXT("WatchClimb")));
	TestEqual(TEXT("Extra crystals do not spill into the climb"), CountOf(Member, LineId), 0);

	TestEqual(TEXT("A Verdant climb is refused"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Crystal on the climb step is ignored"), Member.Apply(Crystal(Rime, 1)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Rime climb"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Rime)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Climb reaches the hand in"), StepOf(Member, LineId), FName(TEXT("HandIn")));

	const FGFSideQuestApplyResult Short = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(CrystalId, 3) }, Rime));
	TestTrue(TEXT("Three crystals are short"), Short.Result == EGFQuestResult::Refused && Short.Reason == FGFQuestCodes::Short);
	const FGFSideQuestApplyResult Wrong = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(DirtId, 4) }, Rime));
	TestTrue(TEXT("Dirt is the wrong item"), Wrong.Result == EGFQuestResult::Refused && Wrong.Reason == FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Hand in at Verdant is refused"), Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(CrystalId, 4) }, Verdant)).Reason, FGFQuestCodes::WrongTown);
	const FGFSideQuestApplyResult Paid = Member.Apply(FGFSideQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(CrystalId, 7), FGFItemStack(DirtId, 2) }, Rime));
	TestTrue(TEXT("Four crystals hand in"), Paid.Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Charge is exactly four crystals"), Paid.Charge.Num() == 1 && Paid.Charge[0].ItemId == CrystalId && Paid.Charge[0].Count == 4);
	TestTrue(TEXT("Hand in completes the line"), Paid.bCompleted);
	TestTrue(TEXT("Done"), StatusOf(Member, LineId) == EGFSideQuestStatus::Done);
	TestTrue(TEXT("No Active line after Done"), Member.Board.ActiveLineId.IsNone() && Member.ActiveStep() == nullptr);
	TestTrue(TEXT("Done line accept is ignored"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Crystal after Done is ignored"), Member.Apply(Crystal(Rime, 1)).Result == EGFQuestResult::Ignored);

	// Ferry fallback: a stop with no anchor (thin metadata) raises the arrival with no town, and any finished trip counts
	// while the line is Active.
	FGFSideQuestLine Fallback = Player(Rime, EGFFactionRank::Member);
	TestTrue(TEXT("Fallback: accept"), Fallback.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).bAccepted);
	TestTrue(TEXT("Fallback: arrival with no departure town counts"), Fallback.Apply(Event(EGFSideQuestEventKind::FerryArrived)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Fallback: reaches the crystal haul"), StepOf(Fallback, LineId), FName(TEXT("CrystalHaul")));

	// Crystal credit only while the line is Active.
	FGFSideQuestLine Idle = Player(Rime, EGFFactionRank::Member);
	const FGFSideQuestBoard IdleKept = Idle.Board;
	TestTrue(TEXT("Not accepted: crystal is ignored"), Idle.Apply(Crystal(Rime, 4)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Not accepted: ferry is ignored"), Idle.Apply(Event(EGFSideQuestEventKind::FerryArrived, Rime)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Not accepted: board unchanged"), Idle.Board == IdleKept);

	// Refusals at the accept.
	TestEqual(TEXT("Verdant elder: WrongTown"), Player(Rime, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Verdant member: WrongTown"), Player(Verdant, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Cinderwrights member: WrongTown"), Player(Cinderwrights, EGFFactionRank::Leader).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("No faction: WrongTown"), Player(NAME_None, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFQuestCodes::WrongTown);
	FGFSideQuestLine Initiate = Player(Rime, EGFFactionRank::Initiate);
	TestTrue(TEXT("Initiate: line locked"), StatusOf(Initiate, LineId) == EGFSideQuestStatus::Locked);
	TestEqual(TEXT("Initiate: RankLow"), Initiate.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFSideQuestCodes::RankLow);
	TestEqual(TEXT("Outsider: RankLow"), Player(Rime, EGFFactionRank::Outsider).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).Reason, FGFSideQuestCodes::RankLow);
	TestTrue(TEXT("Leader: line available"), StatusOf(Player(Rime, EGFFactionRank::Leader), LineId) == EGFSideQuestStatus::Available);

	// OtherActive: a second Rime line next to the shipped one, while the shipped one is Active.
	FGFSideQuestCatalog Two = Shipped;
	FGFSideQuestLineRule Second = *Shipped.FindLine(LineId);
	Second.LineId = TEXT("TestRimeSecond");
	Second.Sort = 99;
	Two.Lines.Add(Second);
	Two.Normalize();
	FGFSideQuestLine Busy = Player(Rime, EGFFactionRank::Member, &Two);
	TestTrue(TEXT("Busy: accept the shipped line"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Rime)).bAccepted);
	const FGFSideQuestBoard BusyKept = Busy.Board;
	TestEqual(TEXT("Second Rime line: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Second.LineId, Rime)).Reason, FGFSideQuestCodes::OtherActive);
	TestEqual(TEXT("Verdant line while busy: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), VerdantLine, Verdant)).Reason, FGFSideQuestCodes::OtherActive);
	TestTrue(TEXT("OtherActive keeps the board"), Busy.Board == BusyKept);

	// Verdant and Cinderwright stay whole (QL-c / QL-e).
	const FGFSideQuestLineRule* VerdantRule = Shipped.FindLine(VerdantLine);
	TestTrue(TEXT("Verdant still playable"), VerdantRule && VerdantRule->IsPlayable() && VerdantRule->Steps.Num() == 6);
	const FGFSideQuestLineRule* CinderRule = Shipped.FindLine(CinderLine);
	TestTrue(TEXT("Cinderwright still playable"), CinderRule && CinderRule->IsPlayable() && CinderRule->Steps.Num() == 5);

	// The preferred ferry path needs Rime-side stop metadata. The shipped Rime stop has it.
	const AGFFerryDirector* Ferry = GetDefault<AGFFerryDirector>();
	const FGFFerryStop* Landing = Ferry ? FGFFerryRules::FindStop(Ferry->Stops, RimeStop) : nullptr;
	if (TestNotNull(TEXT("Shipped RimeLanding stop"), Landing))
	{
		TestTrue(TEXT("RimeLanding is Rime-side"), FGFSideQuestCatalog::SameTown(Landing->AnchorId, Rime));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestRimeWorldTest, "GFGame.SideQuest.Line.Rime.World", GFSideQuestRimeTest::Flags)
bool FGFSideQuestRimeWorldTest::RunTest(const FString&)
{
	// The whole shipped Rime line through the subsystem and the real hooks: a paid flight from RimeLanding, crystal
	// drops through UGFMagicComponent::ReceiveDrop, a mantle at the config climb lip, then the elder hand in.
	// Two Rime members share the world; one player's drops never move the other player's board.
	using namespace GFSideQuestRimeTest;
	FVector Center;
	if (!TestTrue(TEXT("Rime in the layout"), TownCenter(Rime, Center)))
	{
		return false;
	}
	FWorld W;
	if (!W.Init(*this, TEXT("GFSideRimeLine")))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFFerryDirector* Director = W.World->SpawnActor<AGFFerryDirector>(AGFFerryDirector::StaticClass(), FTransform::Identity, Params);
	const FGFFerryStop* Home = Director ? FGFFerryRules::FindStop(Director->Stops, RimeStop) : nullptr;
	const FGFFerryRoute* Route = Director ? Director->Routes.FindByPredicate([](const FGFFerryRoute& R)
	{
		return R.StopA == GFSideQuestRimeTest::RimeStop || R.StopB == GFSideQuestRimeTest::RimeStop;
	}) : nullptr;
	if (!TestNotNull(TEXT("Ferry director"), Director) || !TestNotNull(TEXT("Rime stop"), Home) || !TestNotNull(TEXT("Rime route"), Route))
	{
		return false;
	}
	const FGFFerryStop* Far = FGFFerryRules::FindStop(Director->Stops, Route->StopA == RimeStop ? Route->StopB : Route->StopA);
	if (!TestNotNull(TEXT("Far stop"), Far))
	{
		return false;
	}

	const FString Key(TEXT("Hauler"));
	const FString Other(TEXT("Neighbour"));
	AGFMagicPlayerState* PS = W.Add(Key);
	AGFMagicPlayerState* OtherPS = W.Add(Other);
	AGFCharacter* Pawn = W.Body(PS, Home->Location);
	if (!TestNotNull(TEXT("Hauler"), PS) || !TestNotNull(TEXT("Neighbour"), OtherPS) || !TestNotNull(TEXT("Magic"), PS ? PS->Magic.Get() : nullptr)
		|| !TestNotNull(TEXT("Neighbour magic"), OtherPS ? OtherPS->Magic.Get() : nullptr) || !TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr)
		|| !TestNotNull(TEXT("Rider"), Pawn ? Pawn->FerryRider.Get() : nullptr))
	{
		return false;
	}
	PS->bCharacterReady = true;
	PS->Inventory->AddItem(DirtId, Director->Tuning.FareCount * 2);
	W.Stand(Key, Rime, EGFFactionRank::Member);
	W.Stand(Other, Rime, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());

	if (!TestTrue(TEXT("Accepted"), W.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineId, Rime)).bAccepted))
	{
		return false;
	}
	TestEqual(TEXT("On the ferry step"), W.StepOf(Key), FName(TEXT("FerryCrossing")));
	TestEqual(TEXT("Keyed credit from another town's stop raises an event"), W.Adapters->CreditFerry(Key, Verdant, Home->Id), 1);
	TestEqual(TEXT("A flight from another town's stop does not count"), W.StepOf(Key), FName(TEXT("FerryCrossing")));

	const FGuid Id(0x51DE0101, 0x51DE0102, 0x51DE0103, 0x51DE0104);
	if (!TestTrue(TEXT("Trip from RimeLanding starts"), Director->RequestTripTo(Pawn, Home->Id, Far->Id, Id).bOk))
	{
		return false;
	}
	TestEqual(TEXT("Nothing before landing"), W.StepOf(Key), FName(TEXT("FerryCrossing")));
	for (int32 Step = 0; Step < 4 && Pawn->FerryRider->IsInFlight(); ++Step)
	{
		Director->Tick(Director->Tuning.FlightSeconds);
	}
	TestFalse(TEXT("Flight ended"), Pawn->FerryRider->IsInFlight());
	TestEqual(TEXT("Landing reaches the crystal haul"), W.StepOf(Key), FName(TEXT("CrystalHaul")));

	// Another player's drops are keyed to them: they never move this board, and the other player (not Active) gets nothing.
	OtherPS->Magic->ReceiveDrop(CrystalId, 4, Center);
	TestEqual(TEXT("Neighbour drops leave the hauler at zero"), W.CountOf(Key), 0);
	TestEqual(TEXT("Keyed credit for the neighbour"), W.Adapters->CreditDrop(Other, CrystalId, 4, Center), 1);
	TestEqual(TEXT("Still zero"), W.CountOf(Key), 0);
	TestTrue(TEXT("Neighbour has no Active line"), W.StepOf(Other).IsNone());

	FVector VerdantAt;
	if (TestTrue(TEXT("Verdant in the layout"), TownCenter(Verdant, VerdantAt)))
	{
		PS->Magic->ReceiveDrop(CrystalId, 2, VerdantAt);
		TestEqual(TEXT("Crystals in Verdant do not count"), W.CountOf(Key), 0);
	}
	PS->Magic->ReceiveDrop(HideId, 3, Center);
	TestEqual(TEXT("A hide drop does nothing"), W.CountOf(Key), 0);
	PS->Magic->ReceiveDrop(CrystalId, 2, Center);
	TestEqual(TEXT("Rime crystals count their size"), W.CountOf(Key), 2);
	PS->Magic->ReceiveDrop(CrystalId, 1, Wild);
	TestEqual(TEXT("A wild crystal counts"), W.CountOf(Key), 3);
	PS->Magic->ReceiveDrop(CrystalId, 1, Center);
	TestEqual(TEXT("Four crystals reach the climb"), W.StepOf(Key), FName(TEXT("WatchClimb")));

	W.Adapters->RefreshClimbLips();
	const FGFQuestClimbLip* Lip = W.Adapters->GetClimbLips().FindByPredicate([](const FGFQuestClimbLip& L)
	{
		return FGFSideQuestCatalog::SameTown(L.TownId, GFSideQuestRimeTest::Rime);
	});
	if (!TestNotNull(TEXT("Config fallback gives Rime a climb lip"), Lip))
	{
		return false;
	}
	const FVector LipAt = Lip->Location;
	TestEqual(TEXT("The neighbour's mantle raises for the neighbour only"), W.Adapters->CreditMantle(Other, LipAt), 1);
	TestEqual(TEXT("Hauler still on the climb"), W.StepOf(Key), FName(TEXT("WatchClimb")));
	TestEqual(TEXT("A mantle far from the lip raises nothing"), W.Adapters->CreditMantle(Key, LipAt + FVector(2000.f, 0.f, 0.f)), 0);
	TestEqual(TEXT("Mantle at the Rime lip"), W.Adapters->CreditMantle(Key, LipAt + FVector(50.f, 0.f, 0.f)), 1);
	TestEqual(TEXT("Climb reaches the hand in"), W.StepOf(Key), FName(TEXT("HandIn")));

	const int32 Before = PS->Inventory->GetCount(CrystalId);
	TestTrue(TEXT("Drops landed in the bag"), Before >= 4);
	TestTrue(TEXT("Hand in at the Rime elder"), W.HandIn(PS).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Four crystals charged"), PS->Inventory->GetCount(CrystalId), Before - 4);
	TestTrue(TEXT("Line done"), W.IsDone(Key));
	TestTrue(TEXT("Done replicates"), PS->DoneSideLineIds.Contains(LineId));
	TestTrue(TEXT("Neighbour board untouched"), !W.Progress(Other) || W.Progress(Other)->Status != EGFSideQuestStatus::Done);
	TestTrue(TEXT("Errand untouched"), W.Quest->FindProgress(Key) && W.Quest->FindProgress(Key)->Step == EGFQuestStep::HuntSkins);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestRimeClimbConfigTest, "GFGame.SideQuest.Line.Rime.ClimbConfig", GFSideQuestRimeTest::Flags)
bool FGFSideQuestRimeClimbConfigTest::RunTest(const FString&)
{
	// The shipped fallback stays on Rime land: lip plus radius inside the 1-member disc, clear of the elder, the
	// villagers and the ferry stand. A tagged lip still wins (GFGame.SideQuest.Adapter.ClimbTag).
	using namespace GFSideQuestRimeTest;
	const UGFSideQuestAdapters* Defaults = GetDefault<UGFSideQuestAdapters>();
	const AGFFerryDirector* Ferry = GetDefault<AGFFerryDirector>();
	if (!TestNotNull(TEXT("Adapter defaults"), Defaults) || !TestNotNull(TEXT("Ferry defaults"), Ferry))
	{
		return false;
	}
	const FGFQuestClimbLipConfig* Fallback = Defaults->ClimbLipFallbacks.FindByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestRimeTest::Rime);
	});
	if (!TestNotNull(TEXT("Rime fallback in DefaultGame.ini"), Fallback))
	{
		return false;
	}
	TestTrue(TEXT("Verdant fallback kept"), Defaults->ClimbLipFallbacks.ContainsByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestRimeTest::Verdant);
	}));
	TestEqual(TEXT("Radius kept at 400"), Defaults->ClimbLipRadiusCm, 400.f);
	FVector Center;
	const FGFTerritoryFaction* Faction = nullptr;
	if (!TestTrue(TEXT("Rime faction"), TownCenter(Rime, Center, &Faction)) || !Faction
		|| !TestTrue(TEXT("Rime has a 1-member radius"), Faction->RadiusByMembers.Num() > 1))
	{
		return false;
	}
	TestTrue(TEXT("Lip and radius inside the 1-member disc"), Fallback->Offset.Size2D() + Defaults->ClimbLipRadiusCm <= Faction->RadiusByMembers[1]);
	TestTrue(TEXT("Lip clear of the elder and the villagers"), Fallback->Offset.Size2D() > 400.f + Defaults->ClimbLipRadiusCm);
	const FGFFerryStop* Landing = FGFFerryRules::FindStop(Ferry->Stops, RimeStop);
	if (TestNotNull(TEXT("RimeLanding"), Landing))
	{
		const FVector StopOffset = Landing->Location - Center;
		TestTrue(TEXT("Lip clear of the ferry stand"), FVector::Dist2D(StopOffset, Fallback->Offset) > Ferry->Tuning.BoardRadius + Defaults->ClimbLipRadiusCm);
	}
	return true;
}

#endif
