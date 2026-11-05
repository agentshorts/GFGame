// Cinderwright town line (QL-e): the shipped Ember Ascent walked step by step on the pure board (Builtin and CSV),
// its refusals, HopDrill Boost/Downbeat filter, HotbarDeliver held Crystal, member vs outsider crystal credit,
// and the Cinderwright climb lip fallback from DefaultGame.ini.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.SideQuest;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFCharacter.h"
#include "GFFerryDirector.h"
#include "GFFerryRules.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFModeTypes.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestAdapters.h"
#include "GFSideQuestLine.h"
#include "GFTerritorySubsystem.h"
#include "GFTheftDirector.h"
#include "GFTownNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestCinderTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwright(TEXT("Cinderwright"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName LineId(TEXT("CinderEmberAscent"));
	static const FName VerdantLine(TEXT("VerdantRootward"));
	static const FName RimeLine(TEXT("RimeIceSpan"));
	static const FName CrystalId(TEXT("Crystal"));
	static const FName PlankId(TEXT("WoodPlank"));
	static const FName BoostId(TEXT("Boost"));
	static const FName DownbeatId(TEXT("Downbeat"));
	static const FName StandardId(TEXT("Standard"));
	static const FName LandingId(TEXT("Landing"));
	static const FName CinderStop(TEXT("CinderLanding"));

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
			{ TEXT("VeinBreak"), EGFSideQuestEventKind::CrystalTaken, 2, {} },
			{ TEXT("HopDrill"), EGFSideQuestEventKind::HopLanded, 3, { BoostId, DownbeatId } },
			{ TEXT("KilnClimb"), EGFSideQuestEventKind::ClimbMantled, 1, {} },
			{ TEXT("HotbarDeliver"), EGFSideQuestEventKind::HeldDelivered, 1, { CrystalId } },
		};
	}

	static void CheckSpec(FAutomationTestBase& T, const FGFSideQuestLineRule* Rule, const TCHAR* Source)
	{
		if (!T.TestNotNull(*FString::Printf(TEXT("%s has the Cinderwright line"), Source), Rule))
		{
			return;
		}
		T.TestEqual(*FString::Printf(TEXT("%s town"), Source), Rule->TownId, Cinderwright);
		T.TestEqual(*FString::Printf(TEXT("%s title"), Source), Rule->Title, FString(TEXT("Ember Ascent")));
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

	static FGFSideQuestEvent Hop(FName TileId)
	{
		FGFSideQuestEvent Out = Event(EGFSideQuestEventKind::HopLanded);
		Out.ItemId = TileId;
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
		int32 NextId = 910;

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

		void Stand(const FString& Key, FName FactionId, EGFFactionRank Rank, EGFQuestStep Step = EGFQuestStep::HuntSkins)
		{
			FGFQuestRecord& Record = Records.AddDefaulted_GetRef();
			Record.PlayerKey = Key;
			Record.Progress.Step = Step;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestCinderTransitionsTest, "GFGame.SideQuest.Line.Cinder.Transitions", GFSideQuestCinderTest::Flags)
bool FGFSideQuestCinderTransitionsTest::RunTest(const FString&)
{
	using namespace GFSideQuestCinderTest;
	const FGFSideQuestCatalog& Shipped = FGFSideQuestCatalog::Builtin();
	const FGFSideQuestCatalog Loaded = FGFSideQuestCatalog::Load();
	CheckSpec(*this, Shipped.FindLine(LineId), TEXT("Builtin"));
	CheckSpec(*this, Loaded.FindLine(LineId), TEXT("CSV"));

	// Member walks every step to Done. Engine faction id Cinderwrights unlocks the Cinderwright town line.
	FGFSideQuestLine Member = Player(Cinderwrights, EGFFactionRank::Member);
	TestTrue(TEXT("Member: Cinderwright line available"), StatusOf(Member, LineId) == EGFSideQuestStatus::Available);
	TestTrue(TEXT("Member: Verdant line stays locked"), StatusOf(Member, VerdantLine) == EGFSideQuestStatus::Locked);
	TestTrue(TEXT("Member: Rime line stays locked"), StatusOf(Member, RimeLine) == EGFSideQuestStatus::Locked);
	const FGFSideQuestApplyResult Accepted = Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright));
	TestTrue(TEXT("Accept is Ok"), Accepted.Result == EGFQuestResult::Ok && Accepted.bAccepted);
	TestEqual(TEXT("Accept completes the Accept step"), StepOf(Member, LineId), FName(TEXT("VeinBreak")));
	TestTrue(TEXT("Active step is CrystalTaken"), Member.ActiveStep() && Member.ActiveStep()->AdvanceKind == EGFSideQuestEventKind::CrystalTaken);

	FGFSideQuestBoard Kept = Member.Board;
	TestTrue(TEXT("A hop before the vein is ignored"), Member.Apply(Hop(BoostId)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("A climb before the vein is ignored"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Cinderwright)).Result == EGFQuestResult::Ignored);
	TestEqual(TEXT("A Verdant crystal take is refused"), Member.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Ignored and refused keep the board"), Member.Board == Kept);

	TestTrue(TEXT("First town crystal"), Member.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwright)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Vein count is one"), CountOf(Member, LineId), 1);
	TestEqual(TEXT("Still on VeinBreak"), StepOf(Member, LineId), FName(TEXT("VeinBreak")));
	// Engine town id also matches via SameTown.
	TestTrue(TEXT("Second crystal with engine town id"), Member.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwrights)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Vein reaches the hop drill"), StepOf(Member, LineId), FName(TEXT("HopDrill")));
	TestEqual(TEXT("Vein count resets"), CountOf(Member, LineId), 0);

	TestTrue(TEXT("Crystal on the hop step is ignored"), Member.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwright)).Result == EGFQuestResult::Ignored);
	TestEqual(TEXT("Standard tile is refused"), Member.Apply(Hop(StandardId)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Landing tile is refused"), Member.Apply(Hop(LandingId)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("A hop with no tile kind is refused"), Member.Apply(Event(EGFSideQuestEventKind::HopLanded)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Refusals leave the hop count"), CountOf(Member, LineId), 0);
	TestTrue(TEXT("Boost hop"), Member.Apply(Hop(BoostId)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Hop count is one"), CountOf(Member, LineId), 1);
	TestTrue(TEXT("Downbeat hop"), Member.Apply(Hop(DownbeatId)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Hop count is two"), CountOf(Member, LineId), 2);
	TestTrue(TEXT("Another Boost finishes the drill"), Member.Apply(Hop(BoostId)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Drill reaches the climb"), StepOf(Member, LineId), FName(TEXT("KilnClimb")));
	TestEqual(TEXT("Hop count resets"), CountOf(Member, LineId), 0);

	TestEqual(TEXT("A Verdant climb is refused"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Cinderwright climb"), Member.Apply(Event(EGFSideQuestEventKind::ClimbMantled, Cinderwright)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Climb reaches the hand in"), StepOf(Member, LineId), FName(TEXT("HotbarDeliver")));

	TestEqual(TEXT("Empty hand is refused"), Member.Apply(FGFSideQuestEvents::HeldDelivered(TEXT("P"), NAME_None, Cinderwright)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("A plank is the wrong held item"), Member.Apply(FGFSideQuestEvents::HeldDelivered(TEXT("P"), PlankId, Cinderwright)).Reason, FGFQuestCodes::WrongItem);
	TestEqual(TEXT("Hand in at Verdant is refused"), Member.Apply(FGFSideQuestEvents::HeldDelivered(TEXT("P"), CrystalId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	const FGFSideQuestApplyResult Paid = Member.Apply(FGFSideQuestEvents::HeldDelivered(TEXT("P"), CrystalId, Cinderwright));
	TestTrue(TEXT("Held Crystal hands in"), Paid.Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Hand in completes the line"), Paid.bCompleted);
	TestTrue(TEXT("Done"), StatusOf(Member, LineId) == EGFSideQuestStatus::Done);
	TestTrue(TEXT("No Active line after Done"), Member.Board.ActiveLineId.IsNone() && Member.ActiveStep() == nullptr);
	TestTrue(TEXT("Done line accept is ignored"), Member.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Crystal after Done is ignored"), Member.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwright)).Result == EGFQuestResult::Ignored);

	// Tag suffix Cinder maps through SameTown to the Cinderwright climb step.
	FGFSideQuestLine Tag = Player(Cinderwright, EGFFactionRank::Member);
	TestTrue(TEXT("Tag: accept"), Tag.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).bAccepted);
	TestTrue(TEXT("Tag: vein"), Tag.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwright, 2)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Tag: three Boost hops"), Tag.Apply(Hop(BoostId)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Tag: second"), Tag.Apply(Hop(BoostId)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Tag: third"), Tag.Apply(Hop(BoostId)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Tag: on the climb"), StepOf(Tag, LineId), FName(TEXT("KilnClimb")));
	TestTrue(TEXT("Tag: climb with Cinder suffix"), Tag.Apply(Event(EGFSideQuestEventKind::ClimbMantled, FName(TEXT("Cinder")))).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Tag: reaches the hand in"), StepOf(Tag, LineId), FName(TEXT("HotbarDeliver")));

	// Crystal credit only while the line is Active (Apply ignore). Adapter member gate is covered below.
	FGFSideQuestLine Idle = Player(Cinderwrights, EGFFactionRank::Member);
	const FGFSideQuestBoard IdleKept = Idle.Board;
	TestTrue(TEXT("Not accepted: crystal is ignored"), Idle.Apply(Event(EGFSideQuestEventKind::CrystalTaken, Cinderwright)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Not accepted: hop is ignored"), Idle.Apply(Hop(BoostId)).Result == EGFQuestResult::Ignored);
	TestTrue(TEXT("Not accepted: board unchanged"), Idle.Board == IdleKept);

	// Refusals at the accept.
	TestEqual(TEXT("Verdant elder: WrongTown"), Player(Cinderwrights, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Verdant)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Verdant member: WrongTown"), Player(Verdant, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("Rime member: WrongTown"), Player(Rime, EGFFactionRank::Leader).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Reason, FGFQuestCodes::WrongTown);
	TestEqual(TEXT("No faction: WrongTown"), Player(NAME_None, EGFFactionRank::Member).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Reason, FGFQuestCodes::WrongTown);
	FGFSideQuestLine Initiate = Player(Cinderwrights, EGFFactionRank::Initiate);
	TestTrue(TEXT("Initiate: line locked"), StatusOf(Initiate, LineId) == EGFSideQuestStatus::Locked);
	TestEqual(TEXT("Initiate: RankLow"), Initiate.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Reason, FGFSideQuestCodes::RankLow);
	TestEqual(TEXT("Outsider: RankLow"), Player(Cinderwrights, EGFFactionRank::Outsider).Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).Reason, FGFSideQuestCodes::RankLow);
	TestTrue(TEXT("Leader: line available"), StatusOf(Player(Cinderwright, EGFFactionRank::Leader), LineId) == EGFSideQuestStatus::Available);

	// OtherActive: a second Cinderwright line next to the shipped one, while the shipped one is Active.
	FGFSideQuestCatalog Two = Shipped;
	FGFSideQuestLineRule Second = *Shipped.FindLine(LineId);
	Second.LineId = TEXT("TestCinderSecond");
	Second.Sort = 99;
	Two.Lines.Add(Second);
	Two.Normalize();
	FGFSideQuestLine Busy = Player(Cinderwrights, EGFFactionRank::Member, &Two);
	TestTrue(TEXT("Busy: accept the shipped line"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), LineId, Cinderwright)).bAccepted);
	const FGFSideQuestBoard BusyKept = Busy.Board;
	TestEqual(TEXT("Second Cinderwright line: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), Second.LineId, Cinderwright)).Reason, FGFSideQuestCodes::OtherActive);
	TestEqual(TEXT("Verdant line while busy: OtherActive"), Busy.Apply(FGFSideQuestEvents::TalkAccepted(TEXT("P"), VerdantLine, Verdant)).Reason, FGFSideQuestCodes::OtherActive);
	TestTrue(TEXT("OtherActive keeps the board"), Busy.Board == BusyKept);

	// Verdant and Rime stay whole.
	const FGFSideQuestLineRule* VerdantRule = Shipped.FindLine(VerdantLine);
	TestTrue(TEXT("Verdant still playable"), VerdantRule && VerdantRule->IsPlayable() && VerdantRule->Steps.Num() == 6);
	const FGFSideQuestLineRule* RimeRule = Shipped.FindLine(RimeLine);
	TestTrue(TEXT("Rime still playable"), RimeRule && RimeRule->IsPlayable() && RimeRule->Steps.Num() == 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestCinderMemberCrystalTest, "GFGame.SideQuest.Line.Cinder.MemberCrystal", GFSideQuestCinderTest::Flags)
bool FGFSideQuestCinderMemberCrystalTest::RunTest(const FString&)
{
	// Two CrystalTaken as a Cinderwrights Member advance VeinBreak. An outsider raise skips side credit while the
	// errand theft path is left alone (adapter CreditCrystalTaken member gate).
	using namespace GFSideQuestCinderTest;
	FVector Center;
	if (!TestTrue(TEXT("Cinderwrights in the layout"), TownCenter(Cinderwright, Center)))
	{
		return false;
	}
	FWorld W;
	if (!W.Init(*this, TEXT("GFSideCinderCrystal")))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!TestNotNull(TEXT("Theft director"), W.World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params)))
	{
		return false;
	}
	if (!TestTrue(TEXT("Center resolves to Cinderwrights"), FGFSideQuestCatalog::SameTown(AGFTheftDirector::TownAt(W.World, Center), Cinderwright)))
	{
		return false;
	}

	const FString Member(TEXT("Member"));
	const FString Outsider(TEXT("Outsider"));
	W.Add(Member);
	W.Add(Outsider);
	W.Stand(Member, Cinderwrights, EGFFactionRank::Member);
	W.Stand(Outsider, NAME_None, EGFFactionRank::Outsider, EGFQuestStep::TakeCrystal);
	W.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());

	if (!TestTrue(TEXT("Member accepted"), W.Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Member, LineId, Cinderwright)).bAccepted))
	{
		return false;
	}
	TestEqual(TEXT("On VeinBreak"), W.StepOf(Member), FName(TEXT("VeinBreak")));
	TestEqual(TEXT("Credit skips the outsider"), W.Adapters->CreditCrystalTaken({ Outsider }, Center), 0);
	TestEqual(TEXT("Outsider left the board alone"), W.CountOf(Member), 0);
	TestTrue(TEXT("Outsider errand still on TakeCrystal before a theft raise"), W.Quest->FindProgress(Outsider)
		&& W.Quest->FindProgress(Outsider)->Step == EGFQuestStep::TakeCrystal);

	TestEqual(TEXT("First member credit"), W.Adapters->CreditCrystalTaken({ Member, Outsider }, Center), 1);
	TestEqual(TEXT("Vein count is one"), W.CountOf(Member), 1);
	TestEqual(TEXT("Still on VeinBreak"), W.StepOf(Member), FName(TEXT("VeinBreak")));
	TestEqual(TEXT("Second member credit"), W.Adapters->CreditCrystalTaken({ Member }, Center), 1);
	TestEqual(TEXT("Vein reaches the hop drill"), W.StepOf(Member), FName(TEXT("HopDrill")));
	TestTrue(TEXT("Member errand untouched"), W.Quest->FindProgress(Member) && W.Quest->FindProgress(Member)->Step == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("Outsider has no Active Cinderwright line"), !W.Progress(Outsider) || W.Progress(Outsider)->Status != EGFSideQuestStatus::Active);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestCinderClimbConfigTest, "GFGame.SideQuest.Line.Cinder.ClimbConfig", GFSideQuestCinderTest::Flags)
bool FGFSideQuestCinderClimbConfigTest::RunTest(const FString&)
{
	// The shipped fallback stays on Cinderwright land: lip plus radius inside the 1-member disc, clear of the elder,
	// the villagers and the ferry stand. A tagged lip still wins (GFGame.SideQuest.Adapter.ClimbTag).
	using namespace GFSideQuestCinderTest;
	const UGFSideQuestAdapters* Defaults = GetDefault<UGFSideQuestAdapters>();
	const AGFFerryDirector* Ferry = GetDefault<AGFFerryDirector>();
	if (!TestNotNull(TEXT("Adapter defaults"), Defaults) || !TestNotNull(TEXT("Ferry defaults"), Ferry))
	{
		return false;
	}
	const FGFQuestClimbLipConfig* Fallback = Defaults->ClimbLipFallbacks.FindByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestCinderTest::Cinderwright);
	});
	if (!TestNotNull(TEXT("Cinderwright fallback in DefaultGame.ini"), Fallback))
	{
		return false;
	}
	TestEqual(TEXT("Fallback TownId is Cinderwright"), Fallback->TownId, Cinderwright);
	TestTrue(TEXT("Verdant fallback kept"), Defaults->ClimbLipFallbacks.ContainsByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestCinderTest::Verdant);
	}));
	TestTrue(TEXT("Rime fallback kept"), Defaults->ClimbLipFallbacks.ContainsByPredicate([](const FGFQuestClimbLipConfig& C)
	{
		return FGFSideQuestCatalog::SameTown(C.TownId, GFSideQuestCinderTest::Rime);
	}));
	TestEqual(TEXT("Radius kept at 400"), Defaults->ClimbLipRadiusCm, 400.f);
	FVector Center;
	const FGFTerritoryFaction* Faction = nullptr;
	if (!TestTrue(TEXT("Cinderwrights faction"), TownCenter(Cinderwright, Center, &Faction)) || !Faction
		|| !TestTrue(TEXT("Cinderwrights has a 1-member radius"), Faction->RadiusByMembers.Num() > 1))
	{
		return false;
	}
	TestTrue(TEXT("Lip and radius inside the 1-member disc"), Fallback->Offset.Size2D() + Defaults->ClimbLipRadiusCm <= Faction->RadiusByMembers[1]);
	TestTrue(TEXT("Lip clear of the elder and the villagers"), Fallback->Offset.Size2D() > 400.f + Defaults->ClimbLipRadiusCm);
	const FGFFerryStop* Landing = FGFFerryRules::FindStop(Ferry->Stops, CinderStop);
	if (TestNotNull(TEXT("CinderLanding"), Landing))
	{
		const FVector StopOffset = Landing->Location - Center;
		TestTrue(TEXT("Lip clear of the ferry stand"), FVector::Dist2D(StopOffset, Fallback->Offset) > Ferry->Tuning.BoardRadius + Defaults->ClimbLipRadiusCm);
	}
	return true;
}

#endif
