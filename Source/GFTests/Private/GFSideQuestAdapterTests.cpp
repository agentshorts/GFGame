// Side quest world adapters and elder Talk side buttons (QL-b). Each world test injects a small test catalog through
// UGFQuestSubsystem::SetSideCatalogForTests (shipped lines stay locked), sets errand standing through the "Quest"
// blob, then drives the real world hook: town tree break, town crystal take, crystal drop, Accel Hop landing,
// mantle at a tagged climb lip, ferry arrival, and the elder hand-in.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.SideQuest;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFDino.h"
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
#include "GFQuestSubsystem.h"
#include "GFSideQuestAdapters.h"
#include "GFSideQuestLine.h"
#include "GFTerritorySubsystem.h"
#include "GFTheftDirector.h"
#include "GFTownNodes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestAdapterTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));
	static const FName Cinderwright(TEXT("Cinderwright"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName CrystalId(TEXT("Crystal"));
	static const FName PlankId(TEXT("WoodPlank"));
	static const FName HideId(TEXT("Hide"));
	static const FName DirtId(TEXT("Dirt"));
	static const FName WorkStep(TEXT("Work"));
	static const FName ReportStep(TEXT("Report"));

	static FGFSideQuestStepRule Step(FName Id, int32 Order, EGFSideQuestEventKind Kind, int32 Goal, TArray<FName> Items = {})
	{
		FGFSideQuestStepRule Rule;
		Rule.StepId = Id;
		Rule.Order = Order;
		Rule.Title = Id.ToString();
		Rule.Hint = TEXT("The elder has a task for you.");
		Rule.Goal = Goal;
		Rule.Items = MoveTemp(Items);
		Rule.AdvanceKind = Kind;
		return Rule;
	}

	/** Accept (done by the accept), one Work step of Kind, then a Report check-in with the elder. */
	static FGFSideQuestLineRule Line(FName LineId, FName TownId, EGFSideQuestEventKind Kind, int32 Goal, TArray<FName> Items = {}, int32 Sort = 0)
	{
		FGFSideQuestLineRule Rule;
		Rule.LineId = LineId;
		Rule.TownId = TownId;
		Rule.Title = TEXT("Town task");
		Rule.UnlockRank = EGFFactionRank::Member;
		Rule.Sort = Sort;
		Rule.Steps.Add(Step(TEXT("Accept"), 0, EGFSideQuestEventKind::TalkAccepted, 1));
		Rule.Steps.Add(Step(WorkStep, 1, Kind, Goal, MoveTemp(Items)));
		Rule.Steps.Add(Step(ReportStep, 2, EGFSideQuestEventKind::TalkAccepted, 1));
		return Rule;
	}

	static FGFSideQuestCatalog Catalog(TArray<FGFSideQuestLineRule> Lines)
	{
		FGFSideQuestCatalog Out;
		Out.Lines = MoveTemp(Lines);
		Out.Normalize();
		return Out;
	}

	/** Territory center of a town from the shipped layout (false when the layout has no such town). */
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

	/** Far outside every town. */
	static const FVector Wild(4.0e6, -4.0e6, 0.0);

	/** Every side event raised on one world, for asserting on adapter output. */
	struct FRaised
	{
		UWorld* World = nullptr;
		TArray<FGFSideQuestEvent> Events;
		FDelegateHandle Handle;

		explicit FRaised(UWorld* InWorld) : World(InWorld)
		{
			Handle = FGFSideQuestEvents::OnRaised().AddLambda([this](UWorld* At, const FGFSideQuestEvent& Event)
			{
				if (At == World)
				{
					Events.Add(Event);
				}
			});
		}

		~FRaised() { FGFSideQuestEvents::OnRaised().Remove(Handle); }

		int32 Count(EGFSideQuestEventKind Kind) const
		{
			int32 N = 0;
			for (const FGFSideQuestEvent& Event : Events)
			{
				N += Event.Kind == Kind ? 1 : 0;
			}
			return N;
		}

		const FGFSideQuestEvent* Last(EGFSideQuestEventKind Kind) const
		{
			for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
			{
				if (Events[Index].Kind == Kind)
				{
					return &Events[Index];
				}
			}
			return nullptr;
		}
	};

	struct FSideWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFSideQuestAdapters* Adapters = nullptr;
		TArray<FGFQuestRecord> Records;
		int32 NextId = 300;

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
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest) && T.TestNotNull(TEXT("Side quest adapters"), Adapters);
		}

		template <typename PlayerStateType = AGFPlayerState>
		PlayerStateType* Add(const FString& Key)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			PlayerStateType* PS = World->SpawnActor<PlayerStateType>(PlayerStateType::StaticClass(), FTransform::Identity, Params);
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

		/** A body for PS at At, so movement and ferry hooks can find the player key. */
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

		/** Errand standing for Key, through the saved "Quest" blob. Call before the catalog and the accept. */
		void Stand(const FString& Key, FName FactionId, EGFFactionRank Rank, EGFQuestStep ErrandStep = EGFQuestStep::HuntSkins)
		{
			FGFQuestRecord& Record = Records.AddDefaulted_GetRef();
			Record.PlayerKey = Key;
			Record.Progress.Step = ErrandStep;
			Record.Progress.FactionId = FactionId;
			Record.Progress.Rank = Rank;
			FGFQuestSave Save;
			Save.Records = Records;
			TArray<uint8> Bytes;
			FGFModeBytes::Write(Save, Bytes);
			Quest->RestoreModeState(Bytes);
		}

		FGFSideQuestApplyResult Accept(const FString& Key, FName LineId, FName ElderTownId)
		{
			return Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(Key, LineId, ElderTownId));
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

		bool IsDone(const FString& Key, FName LineId) const
		{
			const FGFSideQuestProgress* P = Progress(Key, LineId);
			return P && P->Status == EGFSideQuestStatus::Done;
		}

		FGFElderTalk::FSideOffer OfferAt(const FString& Key, FName ElderTownId) const
		{
			const FGFQuestProgress* Errand = Quest->FindProgress(Key);
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			const FGFSideQuestBoard Empty;
			return FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
				Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, ElderTownId);
		}

		/** What the Hand in town task button does on the server. */
		FGFSideQuestApplyResult HandIn(AGFPlayerState* PS, FName ElderTownId)
		{
			FGFSideQuestApplyResult None;
			None.Reason = FGFQuestCodes::OutOfOrder;
			FGFSideQuestEvent Event;
			const FGFInventory Empty;
			if (!PS || !FGFElderTalk::SideHandInEvent(OfferAt(PS->PlayerKey, ElderTownId), PS->PlayerKey, ElderTownId, PS->HeldItem,
				PS->Inventory ? PS->Inventory->GetInventory() : Empty, Event))
			{
				return None;
			}
			return Quest->ApplySideEvent(Event);
		}

		~FSideWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static int32 DinoCount(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterRulesTest, "GFGame.SideQuest.Adapter.Rules", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterRulesTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	TestEqual(TEXT("Climb tag town"), FGFSideQuestAdapterRules::ClimbTownFromTags({ TEXT("Other"), TEXT("GFQuestClimb_Cinderwright") }), Cinderwright);
	TestTrue(TEXT("No climb tag"), FGFSideQuestAdapterRules::ClimbTownFromTags({ TEXT("GFQuestClimb"), TEXT("Climb") }).IsNone());
	TestTrue(TEXT("Empty suffix"), FGFSideQuestAdapterRules::ClimbTownFromTags({ TEXT("GFQuestClimb_") }).IsNone());

	TArray<FGFQuestClimbLip> Lips;
	Lips.Add({ Verdant, FVector(0.f, 0.f, 0.f) });
	Lips.Add({ Rime, FVector(300.f, 0.f, 0.f) });
	TestEqual(TEXT("Nearest lip"), FGFSideQuestAdapterRules::ClimbTownAt(Lips, FVector(200.f, 0.f, 0.f), 400.f), Rime);
	TestTrue(TEXT("Out of radius"), FGFSideQuestAdapterRules::ClimbTownAt(Lips, FVector(2000.f, 0.f, 0.f), 400.f).IsNone());

	TestTrue(TEXT("Perfect counts"), FGFSideQuestAdapterRules::HopCounts(EGFJudgement::Perfect, true));
	TestTrue(TEXT("Good counts"), FGFSideQuestAdapterRules::HopCounts(EGFJudgement::Good, true));
	TestFalse(TEXT("Miss never counts"), FGFSideQuestAdapterRules::HopCounts(EGFJudgement::Miss, true));
	TestFalse(TEXT("Unapplied never counts"), FGFSideQuestAdapterRules::HopCounts(EGFJudgement::Perfect, false));
	TestEqual(TEXT("Perfect score"), FGFSideQuestAdapterRules::HopScore(EGFJudgement::Perfect), 2);
	TestEqual(TEXT("Good score"), FGFSideQuestAdapterRules::HopScore(EGFJudgement::Good), 1);
	TestEqual(TEXT("Boost tile id"), FGFSideQuestAdapterRules::HopTileId(EGFHopTileKind::Boost), FName(TEXT("Boost")));

	TestTrue(TEXT("Member of the engine id"), FGFSideQuestAdapterRules::IsTownMember(Cinderwrights, EGFFactionRank::Member, Cinderwright));
	TestFalse(TEXT("Initiate is not a member"), FGFSideQuestAdapterRules::IsTownMember(Verdant, EGFFactionRank::Initiate, Verdant));
	TestFalse(TEXT("Other town"), FGFSideQuestAdapterRules::IsTownMember(Rime, EGFFactionRank::Leader, Verdant));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterTreeTest, "GFGame.SideQuest.Adapter.Tree", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterTreeTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FVector Center;
	if (!TestTrue(TEXT("Verdant in the layout"), TownCenter(Verdant, Center)))
	{
		return false;
	}
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterTree")))
	{
		return false;
	}
	if (!TestEqual(TEXT("Center resolves to Verdant"), AGFTheftDirector::TownAt(W.World, Center), Verdant))
	{
		return false;
	}
	const FName LineId(TEXT("TestTrees"));
	const FString Key(TEXT("A"));
	W.Add(Key);
	W.Stand(Key, Verdant, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Verdant, EGFSideQuestEventKind::TreeBroken, 2) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, Verdant).bAccepted))
	{
		return false;
	}

	FGFTownNodeEvents::OnTreeBroken().Broadcast(W.World, TEXT("VerdantTree1"), Center, { Key, Key });
	TestEqual(TEXT("A repeated key counts once"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 1);
	FGFTownNodeEvents::OnTreeBroken().Broadcast(W.World, TEXT("WildTree"), Wild, { Key });
	TestEqual(TEXT("A wild tree does not count"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 1);
	FGFTownNodeEvents::OnCrystalTaken().Broadcast(W.World, TEXT("VerdantCrystal"), Center, { Key });
	TestEqual(TEXT("A crystal take is not a tree"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 1);
	FGFTownNodeEvents::OnTreeBroken().Broadcast(W.World, TEXT("VerdantTree2"), Center, { Key });
	TestEqual(TEXT("Two trees reach the report"), W.StepOf(Key, LineId), ReportStep);

	// Report is a check-in: the elder of the line's town hands it in, another elder does not.
	TestTrue(TEXT("Rime elder shows nothing"), W.OfferAt(Key, Rime).Line == nullptr);
	TestTrue(TEXT("Verdant elder offers the hand in"), W.OfferAt(Key, Verdant).Action == FGFElderTalk::ESideAction::HandIn);
	AGFPlayerState* PS = W.GS->FindPlayerByKey(Key);
	TestTrue(TEXT("Check-in is Ok"), W.HandIn(PS, Verdant).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Line done"), W.IsDone(Key, LineId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterCrystalTest, "GFGame.SideQuest.Adapter.Crystal", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterCrystalTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FVector Center;
	if (!TestTrue(TEXT("Verdant in the layout"), TownCenter(Verdant, Center)))
	{
		return false;
	}
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterCrystal")))
	{
		return false;
	}
	if (!TestEqual(TEXT("Center resolves to Verdant"), AGFTheftDirector::TownAt(W.World, Center), Verdant))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!TestNotNull(TEXT("Theft director"), W.World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params)))
	{
		return false;
	}
	const FName LineId(TEXT("TestCrystal"));
	const FString Member(TEXT("Member"));
	const FString Outsider(TEXT("Outsider"));
	W.Add(Member);
	W.Add(Outsider);
	W.Stand(Member, Verdant, EGFFactionRank::Member);
	W.Stand(Outsider, NAME_None, EGFFactionRank::Outsider, EGFQuestStep::TakeCrystal);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Verdant, EGFSideQuestEventKind::CrystalTaken, 1) }));
	if (!TestTrue(TEXT("Member accepted"), W.Accept(Member, LineId, Verdant).bAccepted))
	{
		return false;
	}
	TestTrue(TEXT("Outsider cannot accept"), W.Accept(Outsider, LineId, Verdant).Result == EGFQuestResult::Refused);
	TestEqual(TEXT("Credit skips the outsider"), W.Adapters->CreditCrystalTaken({ Outsider }, Center), 0);

	FRaised Raised(W.World);
	const int32 DinosBefore = DinoCount(W.World);
	FGFTownNodeEvents::OnCrystalTaken().Broadcast(W.World, TEXT("VerdantCrystal"), Center, { Member, Outsider });

	TestEqual(TEXT("One CrystalTaken credit"), Raised.Count(EGFSideQuestEventKind::CrystalTaken), 1);
	const FGFSideQuestEvent* Credit = Raised.Last(EGFSideQuestEventKind::CrystalTaken);
	TestTrue(TEXT("Credit is the member's"), Credit && Credit->PlayerKey == Member && Credit->TownId == Verdant);
	TestEqual(TEXT("Member side line moves on"), W.StepOf(Member, LineId), ReportStep);
	TestTrue(TEXT("Member errand untouched"), W.Quest->FindProgress(Member) && W.Quest->FindProgress(Member)->Step == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("Outsider errand still runs the theft"), W.Quest->FindProgress(Outsider) && W.Quest->FindProgress(Outsider)->Step != EGFQuestStep::TakeCrystal);
	TestTrue(TEXT("Outsider has no side line"), W.Progress(Outsider, LineId) == nullptr || W.Progress(Outsider, LineId)->Status != EGFSideQuestStatus::Active);
	TestEqual(TEXT("The credit path hires nothing"), DinoCount(W.World), DinosBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterCrystalDropTest, "GFGame.SideQuest.Adapter.CrystalDrop", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterCrystalDropTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterDrop")))
	{
		return false;
	}
	const FName LineId(TEXT("TestGather"));
	const FString Key(TEXT("Gatherer"));
	AGFMagicPlayerState* PS = W.Add<AGFMagicPlayerState>(Key);
	if (!TestNotNull(TEXT("Magic player"), PS) || !TestNotNull(TEXT("Magic"), PS ? PS->Magic.Get() : nullptr))
	{
		return false;
	}
	W.Stand(Key, Rime, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Rime, EGFSideQuestEventKind::CrystalGathered, 3) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, Rime).bAccepted))
	{
		return false;
	}
	TestEqual(TEXT("Other items are not crystal"), W.Adapters->CreditDrop(Key, HideId, 4, Wild), 0);
	PS->Magic->ReceiveDrop(HideId, 2, Wild);
	TestEqual(TEXT("A hide drop does nothing"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 0);
	PS->Magic->ReceiveDrop(CrystalId, 2, Wild);
	TestEqual(TEXT("A wild crystal drop counts its size"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 2);
	PS->Magic->ReceiveDrop(CrystalId, 1, Wild);
	TestEqual(TEXT("Three crystals reach the report"), W.StepOf(Key, LineId), ReportStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterHopTest, "GFGame.SideQuest.Adapter.Hop", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterHopTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterHop")))
	{
		return false;
	}
	const FName LineId(TEXT("TestHops"));
	const FString Key(TEXT("Hopper"));
	AGFPlayerState* PS = W.Add(Key);
	AGFCharacter* Pawn = W.Body(PS, FVector(0.f, 0.f, 200.f));
	UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	AGFAccelHopTile* Plain = W.World->SpawnActor<AGFAccelHopTile>(FVector::ZeroVector, FRotator::ZeroRotator);
	AGFAccelHopTile* Boost = W.World->SpawnActor<AGFAccelHopTile>(FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Movement"), Move) || !TestNotNull(TEXT("Plain tile"), Plain) || !TestNotNull(TEXT("Boost tile"), Boost))
	{
		return false;
	}
	Plain->SetKind(EGFHopTileKind::Standard);
	Boost->SetKind(EGFHopTileKind::Boost);
	W.Stand(Key, Rime, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Rime, EGFSideQuestEventKind::HopLanded, 1, { TEXT("Boost") }) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, Rime).bAccepted))
	{
		return false;
	}

	FRaised Raised(W.World);
	Move->HandleAccelHopLanding(Boost, 8.4, 120.f, EGFBeatHopMode::Songs);
	TestEqual(TEXT("A miss raises nothing"), Raised.Count(EGFSideQuestEventKind::HopLanded), 0);
	Move->HandleAccelHopLanding(Plain, 9.0, 120.f, EGFBeatHopMode::Songs);
	const FGFSideQuestEvent* PlainHop = Raised.Last(EGFSideQuestEventKind::HopLanded);
	TestTrue(TEXT("A plain hop is raised as Standard"), PlainHop && PlainHop->ItemId == FName(TEXT("Standard")) && PlainHop->PlayerKey == Key);
	TestEqual(TEXT("A plain hop does not move a Boost step"), W.StepOf(Key, LineId), WorkStep);
	TestTrue(TEXT("On the beat lands"), Move->HandleAccelHopLanding(Boost, 10.0, 120.f, EGFBeatHopMode::Songs));
	const FGFSideQuestEvent* BoostHop = Raised.Last(EGFSideQuestEventKind::HopLanded);
	TestTrue(TEXT("Boost hop is graded"), BoostHop && BoostHop->ItemId == FName(TEXT("Boost")) && BoostHop->Score > 0);
	TestEqual(TEXT("A Boost hop reaches the report"), W.StepOf(Key, LineId), ReportStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterClimbTagTest, "GFGame.SideQuest.Adapter.ClimbTag", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterClimbTagTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterClimb")))
	{
		return false;
	}
	const FVector LipAt(5000.f, 0.f, 800.f);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATargetPoint* Lip = W.World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), FTransform(LipAt), Params);
	ATargetPoint* Plain = W.World->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), FTransform(FVector(0.f, 9000.f, 0.f)), Params);
	if (!TestNotNull(TEXT("Lip"), Lip) || !TestNotNull(TEXT("Plain point"), Plain))
	{
		return false;
	}
	Lip->Tags = { TEXT("GFQuestClimb_Cinderwright") };
	Plain->Tags = { TEXT("Climb") };
	W.Adapters->RefreshClimbLips();
	if (!TestEqual(TEXT("One tagged lip"), W.Adapters->GetClimbLips().Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Lip town"), W.Adapters->GetClimbLips()[0].TownId, Cinderwright);

	const FName LineId(TEXT("TestClimb"));
	const FString Key(TEXT("Climber"));
	AGFPlayerState* PS = W.Add(Key);
	AGFCharacter* Pawn = W.Body(PS, LipAt);
	UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("Movement"), Move))
	{
		return false;
	}
	// The engine faction id is Cinderwrights; new side data says Cinderwright. They are one town.
	W.Stand(Key, Cinderwrights, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Cinderwright, EGFSideQuestEventKind::ClimbMantled, 2) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, Cinderwright).bAccepted))
	{
		return false;
	}
	UGFCharacterMovementComponent::OnServerMantleFinished.Broadcast(Move, LipAt + FVector(5000.f, 0.f, 0.f));
	TestEqual(TEXT("A far mantle does not count"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 0);
	UGFCharacterMovementComponent::OnServerMantleFinished.Broadcast(Move, LipAt + FVector(100.f, 0.f, 0.f));
	TestEqual(TEXT("A mantle at the lip counts"), W.Progress(Key, LineId) ? W.Progress(Key, LineId)->Count : -1, 1);
	TestEqual(TEXT("Keyed credit at the lip"), W.Adapters->CreditMantle(Key, LipAt), 1);
	TestEqual(TEXT("Two mantles reach the report"), W.StepOf(Key, LineId), ReportStep);

	// Config fallback for a town with no tagged lip: an offset from the town's territory center.
	FVector Center;
	if (TownCenter(Verdant, Center))
	{
		FGFQuestClimbLipConfig Fallback;
		Fallback.TownId = Verdant;
		Fallback.Offset = FVector(100.f, 200.f, 300.f);
		FGFQuestClimbLipConfig Shadowed;
		Shadowed.TownId = Cinderwright;
		W.Adapters->ClimbLipFallbacks = { Fallback, Shadowed };
		W.Adapters->RefreshClimbLips();
		TestEqual(TEXT("Tagged lip plus one fallback"), W.Adapters->GetClimbLips().Num(), 2);
		TestEqual(TEXT("Fallback finds Verdant"), FGFSideQuestAdapterRules::ClimbTownAt(W.Adapters->GetClimbLips(), Center + Fallback.Offset, 50.f), Verdant);
		W.Adapters->ClimbLipFallbacks.Reset();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterFerryTest, "GFGame.SideQuest.Adapter.Ferry", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterFerryTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterFerry")))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFFerryDirector* Director = W.World->SpawnActor<AGFFerryDirector>(AGFFerryDirector::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Ferry director"), Director) || !TestTrue(TEXT("Routes"), Director->Routes.Num() > 0))
	{
		return false;
	}
	const FGFFerryStop* Home = FGFFerryRules::FindStop(Director->Stops, Director->Routes[0].StopA);
	const FGFFerryStop* Far = FGFFerryRules::FindStop(Director->Stops, Director->Routes[0].StopB);
	if (!TestNotNull(TEXT("Home stop"), Home) || !TestNotNull(TEXT("Far stop"), Far))
	{
		return false;
	}
	const FName TownId = Home->AnchorId.IsNone() ? Rime : Home->AnchorId;
	const FName LineId(TEXT("TestFerry"));
	const FString Key(TEXT("Rider"));
	AGFPlayerState* PS = W.Add(Key);
	AGFCharacter* Pawn = W.Body(PS, Home->Location);
	if (!TestNotNull(TEXT("Rider"), Pawn) || !TestNotNull(TEXT("Rider component"), Pawn ? Pawn->FerryRider.Get() : nullptr) || !PS->Inventory)
	{
		return false;
	}
	PS->bCharacterReady = true;
	PS->Inventory->AddItem(DirtId, Director->Tuning.FareCount * 2);
	W.Stand(Key, TownId, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, TownId, EGFSideQuestEventKind::FerryArrived, 1, { Far->Id }) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, TownId).bAccepted))
	{
		return false;
	}

	FRaised Raised(W.World);
	const FGuid Id(0x51DE0001, 0x51DE0002, 0x51DE0003, 0x51DE0004);
	if (!TestTrue(TEXT("Trip starts"), Director->RequestTripTo(Pawn, Home->Id, Far->Id, Id).bOk))
	{
		return false;
	}
	TestEqual(TEXT("Nothing before landing"), Raised.Count(EGFSideQuestEventKind::FerryArrived), 0);
	for (int32 Step = 0; Step < 4 && Pawn->FerryRider->IsInFlight(); ++Step)
	{
		Director->Tick(Director->Tuning.FlightSeconds);
	}
	TestFalse(TEXT("Flight ended"), Pawn->FerryRider->IsInFlight());
	const FGFSideQuestEvent* Arrived = Raised.Last(EGFSideQuestEventKind::FerryArrived);
	TestTrue(TEXT("Arrival names the stop"), Arrived && Arrived->ItemId == Far->Id && Arrived->PlayerKey == Key && Arrived->TownId == Home->AnchorId);
	TestEqual(TEXT("Arrival reaches the report"), W.StepOf(Key, LineId), ReportStep);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestAdapterHeldDeliverTest, "GFGame.SideQuest.Adapter.HeldDeliver", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestAdapterHeldDeliverTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideAdapterHeld")))
	{
		return false;
	}
	const FName LineId(TEXT("TestHeld"));
	const FString Key(TEXT("Carrier"));
	AGFPlayerState* PS = W.Add(Key);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	W.Stand(Key, Verdant, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Catalog({ Line(LineId, Verdant, EGFSideQuestEventKind::HeldDelivered, 1, { HideId }) }));
	if (!TestTrue(TEXT("Accepted"), W.Accept(Key, LineId, Verdant).bAccepted))
	{
		return false;
	}
	TestTrue(TEXT("Hand in offered"), W.OfferAt(Key, Verdant).Action == FGFElderTalk::ESideAction::HandIn);

	PS->HeldItem = NAME_None;
	FGFSideQuestApplyResult Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("Empty hand is refused"), Result.Result == EGFQuestResult::Refused && Result.Reason == FGFQuestCodes::WrongItem);
	TestFalse(TEXT("Refusal has a toast"), FGFElderTalk::SideRefusalText(Result.Reason).IsEmpty());

	PS->HeldItem = HideId;
	Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("Held but not owned is refused"), Result.Result == EGFQuestResult::Refused && Result.Reason == FGFQuestCodes::WrongItem);

	PS->Inventory->AddItem(HideId, 1);
	PS->HeldItem = PlankId;
	PS->Inventory->AddItem(PlankId, 1);
	Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("Wrong held item is refused"), Result.Result == EGFQuestResult::Refused);

	PS->HeldItem = HideId;
	Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("Held hide is delivered"), Result.Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Reached the report"), W.StepOf(Key, LineId), ReportStep);
	TestEqual(TEXT("Delivering does not take the item"), PS->Inventory->GetCount(HideId), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestTalkAcceptHandInTest, "GFGame.SideQuest.Talk.AcceptHandIn", GFSideQuestAdapterTest::Flags)
bool FGFSideQuestTalkAcceptHandInTest::RunTest(const FString&)
{
	using namespace GFSideQuestAdapterTest;
	using FTalk = FGFElderTalk;
	const FName First(TEXT("TestPlanks"));
	const FName Second(TEXT("TestTreesLater"));
	const FName RimeLine(TEXT("TestRimeLine"));
	const FGFSideQuestCatalog Rules = Catalog({
		Line(First, Verdant, EGFSideQuestEventKind::TurnIn, 2, { PlankId }, 0),
		Line(Second, Verdant, EGFSideQuestEventKind::TreeBroken, 1, {}, 1),
		Line(RimeLine, Rime, EGFSideQuestEventKind::TreeBroken, 1, {}, 0) });

	// Pure offers, from what a client sees on the player state.
	{
		const TArray<FName> None;
		TestTrue(TEXT("Outsider sees no task"), FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, None, NAME_None, EGFFactionRank::Outsider, Verdant).Line == nullptr);
		TestTrue(TEXT("Initiate sees no task"), FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, None, Verdant, EGFFactionRank::Initiate, Verdant).Line == nullptr);
		const FTalk::FSideOffer Offer = FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, None, Verdant, EGFFactionRank::Member, Verdant);
		TestTrue(TEXT("Member gets the first line"), Offer.Action == FTalk::ESideAction::Accept && Offer.LineId == First);
		TestTrue(TEXT("Another town's elder offers nothing"), FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, None, Verdant, EGFFactionRank::Member, Rime).Line == nullptr);
		TestTrue(TEXT("Done lines are skipped"), FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, { First }, Verdant, EGFFactionRank::Member, Verdant).LineId == Second);
		TestTrue(TEXT("Nothing left"), FTalk::SideOfferFor(Rules, NAME_None, NAME_None, 0, { First, Second }, Verdant, EGFFactionRank::Member, Verdant).Line == nullptr);
		const FTalk::FSideOffer Turn = FTalk::SideOfferFor(Rules, First, WorkStep, 1, None, Verdant, EGFFactionRank::Member, Verdant);
		TestTrue(TEXT("Active turn in is a hand in"), Turn.Action == FTalk::ESideAction::HandIn && Turn.Count == 1);
		TestTrue(TEXT("Active line hides at another elder"), FTalk::SideOfferFor(Rules, First, WorkStep, 0, None, Verdant, EGFFactionRank::Member, Rime).Line == nullptr);
		const FTalk::FSideOffer Trees = FTalk::SideOfferFor(Rules, Second, WorkStep, 0, None, Verdant, EGFFactionRank::Member, Verdant);
		TestTrue(TEXT("A world step shows progress with no button"), Trees.Line && Trees.Step && Trees.Action == FTalk::ESideAction::None);
		TestTrue(TEXT("Report is a hand in"), FTalk::SideOfferFor(Rules, Second, ReportStep, 0, None, Verdant, EGFFactionRank::Member, Verdant).Action == FTalk::ESideAction::HandIn);
	}

	// Server path the buttons take.
	FSideWorld W;
	if (!W.Init(*this, TEXT("GFSideTalk")))
	{
		return false;
	}
	const FString Key(TEXT("Talker"));
	AGFPlayerState* PS = W.Add(Key);
	if (!TestNotNull(TEXT("Inventory"), PS ? PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	W.Stand(Key, Verdant, EGFFactionRank::Member);
	W.Quest->SetSideCatalogForTests(Rules);
	const FTalk::FSideOffer Offer = W.OfferAt(Key, Verdant);
	if (!TestTrue(TEXT("Accept offered"), Offer.Action == FTalk::ESideAction::Accept && Offer.LineId == First))
	{
		return false;
	}
	TestEqual(TEXT("Accept at the Rime elder is refused"), W.Accept(Key, First, Rime).Reason, FGFQuestCodes::WrongTown);
	TestTrue(TEXT("Accept town task"), W.Accept(Key, Offer.LineId, Verdant).bAccepted);
	TestEqual(TEXT("Second accept is refused"), W.Accept(Key, Second, Verdant).Reason, FGFSideQuestCodes::OtherActive);
	TestEqual(TEXT("Active line replicates"), PS->ActiveSideLineId, First);

	PS->Inventory->AddItem(PlankId, 1);
	FGFSideQuestApplyResult Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("One plank is short"), Result.Result == EGFQuestResult::Refused && Result.Reason == FGFQuestCodes::Short);
	TestEqual(TEXT("Short keeps the plank"), PS->Inventory->GetCount(PlankId), 1);
	PS->Inventory->AddItem(PlankId, 2);
	Result = W.HandIn(PS, Verdant);
	TestTrue(TEXT("Two planks hand in"), Result.Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Two planks charged"), PS->Inventory->GetCount(PlankId), 1);
	TestEqual(TEXT("At the report"), W.StepOf(Key, First), ReportStep);
	TestTrue(TEXT("Report check-in"), W.HandIn(PS, Verdant).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Line done"), W.IsDone(Key, First));
	TestTrue(TEXT("Done replicates"), PS->DoneSideLineIds.Contains(First));
	TestTrue(TEXT("Done line accept is ignored"), W.Accept(Key, First, Verdant).Result == EGFQuestResult::Ignored);

	// The joined faction lives on the player state (the game state sets it when the errand joins a town).
	PS->FactionId = Verdant;
	const FTalk::FSideOffer Next = FTalk::SideOfferFor(W.Quest->GetSideCatalog(), PS->ActiveSideStatus == EGFSideQuestStatus::Active ? PS->ActiveSideLineId : NAME_None,
		NAME_None, 0, PS->DoneSideLineIds, PS->FactionId, PS->FactionRank, Verdant);
	TestTrue(TEXT("Client view offers the next line"), Next.Action == FTalk::ESideAction::Accept && Next.LineId == Second);
	TestTrue(TEXT("Errand untouched"), W.Quest->FindProgress(Key) && W.Quest->FindProgress(Key)->Step == EGFQuestStep::HuntSkins);
	return true;
}

#endif
