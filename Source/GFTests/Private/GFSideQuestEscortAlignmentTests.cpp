// Escort rows hold their hire (player key + line id), so the escort and the dino stay one entry.
// A save or a give-up while the hire is out leaves the line waiting on that hire, with no escort row and no hire left behind.
// OnModePlayerReady asks again, and the next tick with a body finishes the re-request.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GFCharacter.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "GFModeTypes.h"
#include "GFPlayerState.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestEscort.h"
#include "GFSideQuestLine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSideQuestEscortAlign
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Verdant(TEXT("Verdant"));
	static const FName LineId(TEXT("VerdantRootward"));
	static const FName HireStep(TEXT("HireEscort"));

	static int32 DinoCount(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			N += (*It)->IsActorBeingDestroyed() ? 0 : 1;
		}
		return N;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFSideQuestEscorts* Escorts = nullptr;
		int32 NextId = 880;

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
			Escorts = World->GetSubsystem<UGFSideQuestEscorts>();
			if (Quest)
			{
				Quest->EnsureRegistered();
				Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest)
				&& T.TestNotNull(TEXT("Side quest escorts"), Escorts);
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

		void PlantWaiting(const TArray<FString>& Keys)
		{
			FGFQuestSave Save;
			for (const FString& Key : Keys)
			{
				FGFQuestRecord& Record = Save.Records.AddDefaulted_GetRef();
				Record.PlayerKey = Key;
				Record.Progress.Step = EGFQuestStep::HuntSkins;
				Record.Progress.FactionId = Verdant;
				Record.Progress.Rank = EGFFactionRank::Member;
				FGFSideQuestBoardRecord& Side = Save.SideBoards.AddDefaulted_GetRef();
				Side.PlayerKey = Key;
				FGFSideQuestProgress Waiting;
				Waiting.LineId = LineId;
				Waiting.StepId = HireStep;
				Waiting.Status = EGFSideQuestStatus::Active;
				Waiting.Count = 0;
				Side.Board.Lines.Add(Waiting);
				Side.Board.ActiveLineId = LineId;
			}
			TArray<uint8> Bytes;
			FGFModeBytes::Write(Save, Bytes);
			Quest->RestoreModeState(Bytes);
		}

		bool IsDone(const FString& Key) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			const FGFSideQuestProgress* Progress = Board ? Board->Find(LineId) : nullptr;
			return Progress && Progress->Status == EGFSideQuestStatus::Done;
		}

		FName StepOf(const FString& Key) const
		{
			const FGFSideQuestBoard* Board = Quest->FindSideBoard(Key);
			const FGFSideQuestProgress* Progress = Board ? Board->Find(LineId) : nullptr;
			return Progress && Progress->Status == EGFSideQuestStatus::Active ? Progress->StepId : NAME_None;
		}

		/** Ready, then a body, then one tick: the hire is out and the line is Done. */
		bool BringOut(FAutomationTestBase& T, AGFPlayerState* PS, const FVector& At)
		{
			if (!PS)
			{
				return false;
			}
			Quest->OnModePlayerReady(PS);
			AGFCharacter* Pawn = Body(PS, At);
			if (!T.TestNotNull(*FString::Printf(TEXT("%s body"), *PS->PlayerKey), Pawn))
			{
				return false;
			}
			Escorts->TickEscorts(0.1f);
			AGFDino* Hire = Escorts->HireFor(PS->PlayerKey);
			const FGFSideQuestEscort* Row = Escorts->GetEscorts().FindByPredicate([PS](const FGFSideQuestEscort& Escort)
			{
				return Escort.PlayerKey == PS->PlayerKey;
			});
			return T.TestNotNull(*FString::Printf(TEXT("%s hire"), *PS->PlayerKey), Hire)
				&& T.TestNotNull(*FString::Printf(TEXT("%s row"), *PS->PlayerKey), Row)
				&& T.TestTrue(*FString::Printf(TEXT("%s hire is on its row"), *PS->PlayerKey), Row && Row->Hire.Get() == Hire)
				&& T.TestTrue(*FString::Printf(TEXT("%s escorting"), *PS->PlayerKey), Row && Row->Phase == EGFSideQuestEscortPhase::Escorting)
				&& T.TestTrue(*FString::Printf(TEXT("%s line done while the hire walks"), *PS->PlayerKey), IsDone(PS->PlayerKey))
				&& T.TestTrue(TEXT("Rows stay aligned"), Escorts->CheckAlignment());
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

	static const FGFSideQuestProgress* SavedLine(const FGFQuestSave& Save, const FString& Key)
	{
		const FGFSideQuestBoardRecord* Row = Save.SideBoards.FindByPredicate([&Key](const FGFSideQuestBoardRecord& Record)
		{
			return Record.PlayerKey == Key;
		});
		return Row ? Row->Board.Find(LineId) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestEscortAlignmentRulesTest, "GFGame.SideQuest.Hire.EscortAlignment", GFSideQuestEscortAlign::Flags)
bool FGFSideQuestEscortAlignmentRulesTest::RunTest(const FString&)
{
	using namespace GFSideQuestEscortAlign;
	using FRules = FGFSideQuestEscortRules;
	TArray<FGFSideQuestEscort> List;
	TestTrue(TEXT("Empty list is aligned"), FRules::IsAligned(List));
	TestTrue(TEXT("Ask"), FRules::Ask(List, TEXT("A"), LineId, Verdant));
	TestTrue(TEXT("A second player"), FRules::Ask(List, TEXT("B"), LineId, Verdant));
	TestTrue(TEXT("Two pending rows, no hires"), FRules::IsAligned(List));

	FGFSideQuestEscort Duplicate;
	Duplicate.PlayerKey = TEXT("A");
	Duplicate.LineId = LineId;
	Duplicate.Phase = EGFSideQuestEscortPhase::Pending;
	List.Add(Duplicate);
	TestFalse(TEXT("Two open rows for one player are out of line"), FRules::IsAligned(List));
	List.Pop();

	FGFSideQuestEscort Blank;
	Blank.Phase = EGFSideQuestEscortPhase::Pending;
	List.Add(Blank);
	TestFalse(TEXT("An empty player key is out of line"), FRules::IsAligned(List));
	List.Pop();

	FRules::Spawned(List[0]);
	TestFalse(TEXT("Escorting with no hire is out of line"), FRules::IsAligned(List));
	List[0].Phase = EGFSideQuestEscortPhase::Done;
	List[0].Hire.Reset();
	TestTrue(TEXT("A finished row with no hire is aligned"), FRules::IsAligned(List));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestEscortSaveMidTest, "GFGame.SideQuest.Hire.SaveMidEscort", GFSideQuestEscortAlign::Flags)
bool FGFSideQuestEscortSaveMidTest::RunTest(const FString&)
{
	// Mid escort the live line is Done and the hire is out. The blob waits on the hire instead.
	// A new world has no escort row and no dino. Player ready asks, and a tick with a body spawns the hire again.
	using namespace GFSideQuestEscortAlign;
	const FString Key(TEXT("Saver"));
	TArray<uint8> Saved;
	{
		FWorld First;
		if (!First.Init(*this, TEXT("GFSideEscortSaveA")))
		{
			return false;
		}
		AGFPlayerState* PS = First.Add(Key);
		if (!TestNotNull(TEXT("Saver"), PS))
		{
			return false;
		}
		First.PlantWaiting({ Key });
		FName WaitLine;
		FName WaitTown;
		TestTrue(TEXT("Planted on the hire step"), First.Quest->WaitsOnHire(Key, WaitLine, WaitTown) && WaitLine == LineId && WaitTown == Verdant);
		TestEqual(TEXT("Plant does not spawn"), First.Escorts->GetEscorts().Num(), 0);
		if (!First.BringOut(*this, PS, FVector(0.f, 0.f, 200.f)))
		{
			return false;
		}
		TestEqual(TEXT("One hire out"), DinoCount(First.World), 1);
		First.Quest->CaptureModeState(Saved);
		TestTrue(TEXT("Live line stays Done after the save"), First.IsDone(Key));
		TestTrue(TEXT("Live hire stays out after the save"), First.Escorts->HireFor(Key) != nullptr);
		TestEqual(TEXT("Live escort row stays"), First.Escorts->GetEscorts().Num(), 1);
	}
	if (!TestTrue(TEXT("Blob captured"), Saved.Num() > 0))
	{
		return false;
	}
	FGFQuestSave Back;
	if (!TestTrue(TEXT("Blob reads"), FGFModeBytes::Read(Back, Saved)))
	{
		return false;
	}
	const FGFSideQuestProgress* Waiting = SavedLine(Back, Key);
	const FGFSideQuestBoardRecord* Row = Back.SideBoards.FindByPredicate([&Key](const FGFSideQuestBoardRecord& Record)
	{
		return Record.PlayerKey == Key;
	});
	if (!TestNotNull(TEXT("Saved line"), Waiting) || !TestNotNull(TEXT("Saved board"), Row))
	{
		return false;
	}
	TestTrue(TEXT("Saved line waits"), Waiting->Status == EGFSideQuestStatus::Active);
	TestEqual(TEXT("Saved step is the hire"), Waiting->StepId, HireStep);
	TestEqual(TEXT("Saved hire count is zero"), Waiting->Count, 0);
	TestEqual(TEXT("Saved pointer is the line"), Row->Board.ActiveLineId, LineId);

	FWorld Second;
	if (!Second.Init(*this, TEXT("GFSideEscortSaveB")))
	{
		return false;
	}
	AGFPlayerState* PS = Second.Add(Key);
	if (!TestNotNull(TEXT("Saver returns"), PS))
	{
		return false;
	}
	Second.Quest->SetSideCatalogForTests(FGFSideQuestCatalog::Builtin());
	Second.Quest->RestoreModeState(Saved);
	FName WaitLine;
	FName WaitTown;
	TestEqual(TEXT("Reload has no escort row"), Second.Escorts->GetEscorts().Num(), 0);
	TestEqual(TEXT("Reload has no dino"), DinoCount(Second.World), 0);
	TestTrue(TEXT("Reload waits on the hire"), Second.Quest->WaitsOnHire(Key, WaitLine, WaitTown) && WaitLine == LineId && WaitTown == Verdant);
	TestEqual(TEXT("Reload step"), Second.StepOf(Key), HireStep);
	TestFalse(TEXT("Reload line is open"), Second.IsDone(Key));
	TestTrue(TEXT("Empty escort list is aligned"), Second.Escorts->CheckAlignment());
	TestFalse(TEXT("Done list is clear"), PS->DoneSideLineIds.Contains(LineId));

	Second.Quest->OnModePlayerReady(PS);
	TestEqual(TEXT("Rejoin asks once"), Second.Escorts->GetEscorts().Num(), 1);
	TestTrue(TEXT("Asked row is pending"), Second.Escorts->GetEscorts()[0].Phase == EGFSideQuestEscortPhase::Pending);
	TestTrue(TEXT("Asked row has no hire yet"), !Second.Escorts->GetEscorts()[0].Hire.IsValid());
	TestEqual(TEXT("Ask spawns nothing"), DinoCount(Second.World), 0);
	TestTrue(TEXT("Pending row is aligned"), Second.Escorts->CheckAlignment());
	Second.Quest->OnModePlayerReady(PS);
	TestEqual(TEXT("A second ready does not add a row"), Second.Escorts->GetEscorts().Num(), 1);

	AGFCharacter* Pawn = Second.Body(PS, FVector(0.f, 0.f, 200.f));
	if (!TestNotNull(TEXT("Body on the reloaded world"), Pawn))
	{
		return false;
	}
	Second.Escorts->TickEscorts(0.1f);
	AGFDino* Hire = Second.Escorts->HireFor(Key);
	const FGFSideQuestEscort* Escort = Second.Escorts->GetEscorts().Num() == 1 ? &Second.Escorts->GetEscorts()[0] : nullptr;
	TestNotNull(TEXT("Re-request spawns the hire"), Hire);
	TestTrue(TEXT("Spawned hire is on its row"), Escort && Hire && Escort->Hire.Get() == Hire && Escort->Phase == EGFSideQuestEscortPhase::Escorting);
	TestTrue(TEXT("Re-request finishes the line"), Second.IsDone(Key));
	TestTrue(TEXT("Done replicates"), PS->DoneSideLineIds.Contains(LineId));
	TestEqual(TEXT("One dino after the re-request"), DinoCount(Second.World), 1);
	TestTrue(TEXT("Escorting row is aligned"), Second.Escorts->CheckAlignment());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSideQuestEscortGiveUpMidTest, "GFGame.SideQuest.Hire.GiveUpMidEscort", GFSideQuestEscortAlign::Flags)
bool FGFSideQuestEscortGiveUpMidTest::RunTest(const FString&)
{
	// Two hires are out. Giving up the first drops that row and that dino, and puts the line back on the hire step.
	// The other hire stays on its own row. Rejoin asks for the one who gave up, and a tick spawns that hire again.
	using namespace GFSideQuestEscortAlign;
	FWorld W;
	if (!W.Init(*this, TEXT("GFSideEscortGiveUp")))
	{
		return false;
	}
	const FString Leaver(TEXT("Leaver"));
	const FString Keeper(TEXT("Keeper"));
	AGFPlayerState* LeavePS = W.Add(Leaver);
	AGFPlayerState* KeepPS = W.Add(Keeper);
	if (!TestNotNull(TEXT("Leaver"), LeavePS) || !TestNotNull(TEXT("Keeper"), KeepPS))
	{
		return false;
	}
	TestFalse(TEXT("Give-up with no escort"), W.Escorts->GiveUp(Leaver));
	W.PlantWaiting({ Leaver, Keeper });
	if (!W.BringOut(*this, LeavePS, FVector(0.f, 0.f, 200.f)) || !W.BringOut(*this, KeepPS, FVector(4000.f, 0.f, 200.f)))
	{
		return false;
	}
	TestEqual(TEXT("Two rows"), W.Escorts->GetEscorts().Num(), 2);
	TestEqual(TEXT("Two dinos"), DinoCount(W.World), 2);
	TWeakObjectPtr<AGFDino> LeftHire = W.Escorts->HireFor(Leaver);
	TWeakObjectPtr<AGFDino> KeptHire = W.Escorts->HireFor(Keeper);
	if (!TestTrue(TEXT("Both hires"), LeftHire.IsValid() && KeptHire.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("Give up the first escort"), W.Escorts->GiveUp(Leaver));
	TestFalse(TEXT("A second give-up finds nothing"), W.Escorts->GiveUp(Leaver));
	TestFalse(TEXT("Leaver hire is gone"), LeftHire.IsValid());
	TestTrue(TEXT("Keeper hire stays"), KeptHire.IsValid() && W.Escorts->HireFor(Keeper) == KeptHire.Get());
	TestEqual(TEXT("One row left"), W.Escorts->GetEscorts().Num(), 1);
	TestEqual(TEXT("The remaining row is the keeper"), W.Escorts->GetEscorts()[0].PlayerKey, Keeper);
	TestTrue(TEXT("Keeper hire is still on that row"), W.Escorts->GetEscorts()[0].Hire.Get() == KeptHire.Get());
	TestEqual(TEXT("One dino left"), DinoCount(W.World), 1);
	TestTrue(TEXT("After give-up the rows are aligned"), W.Escorts->CheckAlignment());
	FName WaitLine;
	FName WaitTown;
	TestTrue(TEXT("Leaver waits on the hire"), W.Quest->WaitsOnHire(Leaver, WaitLine, WaitTown) && WaitLine == LineId && WaitTown == Verdant);
	TestEqual(TEXT("Leaver step"), W.StepOf(Leaver), HireStep);
	TestFalse(TEXT("Leaver line is open"), W.IsDone(Leaver));
	TestEqual(TEXT("Leaver replicated line"), LeavePS->ActiveSideLineId, LineId);
	TestEqual(TEXT("Leaver replicated step"), LeavePS->ActiveSideStepId, HireStep);
	TestTrue(TEXT("Leaver replicated status"), LeavePS->ActiveSideStatus == EGFSideQuestStatus::Active);
	TestFalse(TEXT("Leaver dropped off the done list"), LeavePS->DoneSideLineIds.Contains(LineId));
	TestTrue(TEXT("Keeper line stays Done"), W.IsDone(Keeper));
	TestTrue(TEXT("Keeper stays on the done list"), KeepPS->DoneSideLineIds.Contains(LineId));

	W.Quest->OnModePlayerReady(LeavePS);
	TestEqual(TEXT("Rejoin asks for the leaver"), W.Escorts->GetEscorts().Num(), 2);
	TestTrue(TEXT("Keeper hire unchanged by the ask"), W.Escorts->HireFor(Keeper) == KeptHire.Get());
	TestEqual(TEXT("Ask adds no dino"), DinoCount(W.World), 1);
	TestTrue(TEXT("Rows aligned after the ask"), W.Escorts->CheckAlignment());
	W.Quest->OnModePlayerReady(KeepPS);
	TestEqual(TEXT("Keeper ready does not add a row"), W.Escorts->GetEscorts().Num(), 2);

	W.Escorts->TickEscorts(0.1f);
	AGFDino* Again = W.Escorts->HireFor(Leaver);
	const FGFSideQuestEscort* LeaveRow = W.Escorts->GetEscorts().FindByPredicate([&Leaver](const FGFSideQuestEscort& Escort)
	{
		return Escort.PlayerKey == Leaver;
	});
	TestNotNull(TEXT("Re-request spawns the leaver hire"), Again);
	TestTrue(TEXT("Leaver hire is on its row"), LeaveRow && Again && LeaveRow->Hire.Get() == Again);
	TestTrue(TEXT("Leaver line done again"), W.IsDone(Leaver));
	TestTrue(TEXT("Keeper hire still the same dino"), W.Escorts->HireFor(Keeper) == KeptHire.Get());
	TestEqual(TEXT("Two dinos again"), DinoCount(W.World), 2);
	TestTrue(TEXT("Both rows aligned"), W.Escorts->CheckAlignment());
	return true;
}

#endif
