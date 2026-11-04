// The Elder's Errand: the pure line, turn-ins, the quest CSV, and a save/load round trip.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "GameFramework/WorldSettings.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFDataTables.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFSaveSubsystem.h"
#include "GFSaveGame.h"
#include "GFModeTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFQuestTest
{
	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Cinder(TEXT("Cinderwrights"));
	static const FName Hide(TEXT("Hide"));
	static const FName Scales(TEXT("Scales"));
	static const FName Nipper(TEXT("Skin_Nipper"));
	static const FName Raptor(TEXT("Skin_Raptor"));
	static const FName Tusksaw(TEXT("Skin_Tusksaw"));
	static const FName Lantern(TEXT("Skin_Lanternmaw"));
	static const FName Loafer(TEXT("dirtachi_01"));
	static const FName Pack(TEXT("predada_01"));

	template <typename EnumType>
	TArray<EnumType> Values()
	{
		TArray<EnumType> Out;
		const UEnum* Enum = StaticEnum<EnumType>();
		for (int32 Index = 0; Enum && Index < Enum->NumEnums(); ++Index)
		{
			const FString Name = Enum->GetNameStringByIndex(Index);
			if (Name.Contains(TEXT("_MAX")))
			{
				continue;
			}
			const int64 Value = Enum->GetValueByIndex(Index);
			if (Value < 0)
			{
				continue;
			}
			Out.Add(static_cast<EnumType>(Value));
		}
		return Out;
	}

	template <typename EnumType>
	FString Label(EnumType Value)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		FString Name = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString();
		int32 Colon = INDEX_NONE;
		if (Name.FindLastChar(TEXT(':'), Colon))
		{
			Name.RightChopInline(Colon + 1);
		}
		return Name;
	}

	EGFFactionRank SetupRank(EGFQuestStep Step)
	{
		switch (Step)
		{
		case EGFQuestStep::HuntHide: return EGFFactionRank::Initiate;
		case EGFQuestStep::HuntSkins: return EGFFactionRank::Member;
		case EGFQuestStep::CraftLook:
		case EGFQuestStep::WinShow: return EGFFactionRank::Champion;
		case EGFQuestStep::Done: return EGFFactionRank::Leader;
		default: return EGFFactionRank::Outsider;
		}
	}

	FName SetupFaction(EGFQuestStep Step)
	{
		switch (Step)
		{
		case EGFQuestStep::HuntHide:
		case EGFQuestStep::HuntSkins:
		case EGFQuestStep::CraftLook:
		case EGFQuestStep::WinShow:
		case EGFQuestStep::Done:
			return Rime;
		default:
			return NAME_None;
		}
	}

	struct FExpect
	{
		bool bOk = false;
		EGFQuestResult Result = EGFQuestResult::Ignored;
		FName Reason;
		EGFQuestStep Step = EGFQuestStep::TakeCrystal;
		int32 Count = 0;
		EGFFactionRank Rank = EGFFactionRank::Outsider;
		FName Faction;
		bool bClaim = false;
		bool bLeader = false;

		FExpect() { Reason = FGFQuestCodes::OutOfOrder; }
	};

	FExpect Expected(EGFQuestStep Step, int32 Sub, EGFQuestEventKind Kind)
	{
		FExpect E;
		E.Step = Step;
		E.Count = Sub;
		E.Rank = SetupRank(Step);
		E.Faction = SetupFaction(Step);
		if (Kind == EGFQuestEventKind::ElderAccepted && Sub == 0
			&& (Step == EGFQuestStep::TakeCrystal || Step == EGFQuestStep::Escape || Step == EGFQuestStep::TalkToElder))
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::HuntHide;
			E.Count = 0;
			E.Rank = EGFFactionRank::Initiate;
			E.Faction = Verdant;
			E.bClaim = true;
			return E;
		}
		if (Step == EGFQuestStep::TakeCrystal && Sub == 0 && Kind == EGFQuestEventKind::CrystalTaken)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::Escape;
			return E;
		}
		if (Step == EGFQuestStep::Escape && Sub == 0 && Kind == EGFQuestEventKind::HeatCleared)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::TalkToElder;
			E.Count = 0;
			return E;
		}
		if (Step == EGFQuestStep::HuntHide && Sub == 0 && Kind == EGFQuestEventKind::TurnIn)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::HuntSkins;
			E.Count = 0;
			E.Rank = EGFFactionRank::Member;
			return E;
		}
		if (Step == EGFQuestStep::HuntSkins && Sub == 0 && Kind == EGFQuestEventKind::TurnIn)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::CraftLook;
			E.Count = 0;
			E.Rank = EGFFactionRank::Champion;
			return E;
		}
		// A newer craft replaces the one to wear (#330), so crafting again after the first is fine.
		if (Step == EGFQuestStep::CraftLook && (Sub == 0 || Sub == 1) && Kind == EGFQuestEventKind::OutfitCrafted)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Count = 1;
			return E;
		}
		if (Step == EGFQuestStep::CraftLook && Sub == 1 && Kind == EGFQuestEventKind::OutfitWorn)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::WinShow;
			E.Count = 0;
			return E;
		}
		if (Step == EGFQuestStep::WinShow && Sub == 0 && Kind == EGFQuestEventKind::ShowEntered)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Count = 1;
			return E;
		}
		if (Step == EGFQuestStep::WinShow && Sub == 1 && Kind == EGFQuestEventKind::ShowPlaced)
		{
			E.bOk = true;
			E.Result = EGFQuestResult::Ok;
			E.Reason = FGFQuestCodes::Ok;
			E.Step = EGFQuestStep::Done;
			E.Count = 0;
			E.Rank = EGFFactionRank::Leader;
			E.bLeader = true;
			return E;
		}
		if (Step == EGFQuestStep::WinShow && Sub == 1 && Kind == EGFQuestEventKind::ShowEntered)
		{
			E.Reason = FGFQuestCodes::Already;
		}
		return E;
	}

	void Setup(FGFQuestLine& Line, EGFQuestStep Step, int32 Sub)
	{
		Line.Progress = FGFQuestProgress();
		Line.Progress.Step = Step;
		Line.Progress.Count = Sub;
		Line.Progress.Rank = SetupRank(Step);
		Line.Progress.FactionId = SetupFaction(Step);
		if (Step == EGFQuestStep::CraftLook && Sub == 1)
		{
			Line.Progress.CraftedItem = Loafer;
		}
	}

	FGFQuestEvent MakeEvent(EGFQuestEventKind Kind, EGFQuestStep Step)
	{
		switch (Kind)
		{
		case EGFQuestEventKind::CrystalTaken: return FGFQuestEvents::CrystalTaken(TEXT("P"), Rime);
		case EGFQuestEventKind::HeatCleared: return FGFQuestEvents::HeatCleared(TEXT("P"));
		case EGFQuestEventKind::ElderAccepted: return FGFQuestEvents::ElderAccepted(TEXT("P"), Verdant);
		case EGFQuestEventKind::TurnIn:
			if (Step == EGFQuestStep::HuntSkins)
			{
				return FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 1), FGFItemStack(Raptor, 1) });
			}
			return FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 6) });
		case EGFQuestEventKind::OutfitCrafted: return FGFQuestEvents::OutfitCrafted(TEXT("P"), Loafer);
		case EGFQuestEventKind::OutfitWorn: return FGFQuestEvents::OutfitWorn(TEXT("P"), Loafer);
		case EGFQuestEventKind::ShowEntered: return FGFQuestEvents::ShowEntered(TEXT("P"));
		case EGFQuestEventKind::ShowPlaced: return FGFQuestEvents::ShowPlaced(TEXT("P"), FGFQuestShowRule::WinPlace, FGFQuestShowRule::WinTier);
		default: return FGFQuestEvents::ShowEntered(TEXT("P"));
		}
	}

	void CheckPair(FAutomationTestBase& T, EGFQuestStep Step, int32 Sub, EGFQuestEventKind Kind)
	{
		FGFQuestLine Line;
		Setup(Line, Step, Sub);
		const FGFQuestProgress Before = Line.Progress;
		const FExpect Want = Expected(Step, Sub, Kind);
		const FGFQuestApplyResult Got = Line.Apply(MakeEvent(Kind, Step));
		const FString What = FString::Printf(TEXT("%s #%d + %s"), *Label(Step), Sub, *Label(Kind));
		T.TestTrue(*FString::Printf(TEXT("%s result %s"), *What, *Label(Want.Result)), Got.Result == Want.Result);
		T.TestEqual(*FString::Printf(TEXT("%s reason"), *What), Got.Reason, Want.Reason);
		T.TestEqual(*FString::Printf(TEXT("%s claim"), *What), Got.bClaimFaction, Want.bClaim);
		T.TestEqual(*FString::Printf(TEXT("%s leader"), *What), Got.bBecameLeader, Want.bLeader);
		if (Want.bOk)
		{
			T.TestTrue(*FString::Printf(TEXT("%s step %s"), *What, *Label(Want.Step)), Line.Progress.Step == Want.Step);
			T.TestEqual(*FString::Printf(TEXT("%s count"), *What), Line.Progress.Count, Want.Count);
			T.TestTrue(*FString::Printf(TEXT("%s rank %s"), *What, *Label(Want.Rank)), Line.Progress.Rank == Want.Rank);
			T.TestEqual(*FString::Printf(TEXT("%s faction"), *What), Line.Progress.FactionId, Want.Faction);
			if (Kind == EGFQuestEventKind::CrystalTaken && Step == EGFQuestStep::TakeCrystal)
			{
				T.TestTrue(*FString::Printf(TEXT("%s heat"), *What), Line.Progress.HeatFactions.Num() == 1 && Line.Progress.HeatFactions.Contains(Rime));
			}
			else
			{
				T.TestTrue(*FString::Printf(TEXT("%s heat stays"), *What), Line.Progress.HeatFactions == Before.HeatFactions);
			}
			if (Kind == EGFQuestEventKind::TurnIn)
			{
				int32 Sum = 0;
				for (const FGFItemStack& Stack : Got.Charge)
				{
					Sum += Stack.Count;
				}
				T.TestEqual(*FString::Printf(TEXT("%s charged"), *What), Sum, Step == EGFQuestStep::HuntHide ? 6 : 2);
			}
			else
			{
				T.TestEqual(*FString::Printf(TEXT("%s no charge"), *What), Got.Charge.Num(), 0);
			}
		}
		else
		{
			T.TestTrue(*FString::Printf(TEXT("%s unchanged"), *What), Line.Progress == Before);
			T.TestEqual(*FString::Printf(TEXT("%s no charge"), *What), Got.Charge.Num(), 0);
		}
	}

	bool ChargeIs(FAutomationTestBase& T, const TCHAR* What, const TArray<FGFItemStack>& Got, const TArray<FGFItemStack>& Want)
	{
		if (!T.TestEqual(*FString::Printf(TEXT("%s stacks"), What), Got.Num(), Want.Num()))
		{
			return false;
		}
		for (int32 Index = 0; Index < Want.Num(); ++Index)
		{
			T.TestEqual(*FString::Printf(TEXT("%s %d"), What, Index), Got[Index].ItemId, Want[Index].ItemId);
			T.TestEqual(*FString::Printf(TEXT("%s %d count"), What, Index), Got[Index].Count, Want[Index].Count);
		}
		return true;
	}

	FGFQuestLine At(EGFQuestStep Step, FName Faction, EGFFactionRank Rank)
	{
		FGFQuestLine Line;
		Line.Progress.Step = Step;
		Line.Progress.FactionId = Faction;
		Line.Progress.Rank = Rank;
		return Line;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		int32 NextId = 100;

		bool Init(FAutomationTestBase& T, const TCHAR* Name)
		{
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
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest subsystem"), Quest);
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

		~FWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestLineTransitionsTest, "GFGame.Quest.Line.Transitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestLineTransitionsTest::RunTest(const FString&)
{
	using namespace GFQuestTest;
	const TArray<EGFQuestStep> Steps = Values<EGFQuestStep>();
	const TArray<EGFQuestEventKind> Kinds = Values<EGFQuestEventKind>();
	TestEqual(TEXT("Eight steps"), Steps.Num(), 8);
	TestEqual(TEXT("Eight events"), Kinds.Num(), 8);
	for (EGFQuestStep Step : Steps)
	{
		for (EGFQuestEventKind Kind : Kinds)
		{
			CheckPair(*this, Step, 0, Kind);
		}
	}
	for (EGFQuestEventKind Kind : Kinds)
	{
		CheckPair(*this, EGFQuestStep::CraftLook, 1, Kind);
		CheckPair(*this, EGFQuestStep::WinShow, 1, Kind);
	}

	// In order, events only.
	{
		FGFQuestLine Line;
		const FGFQuestApplyResult Crystal = Line.Apply(FGFQuestEvents::CrystalTaken(TEXT("P"), Rime));
		TestTrue(TEXT("Crystal taken"), Line.Progress.Step == EGFQuestStep::Escape);
		TestTrue(TEXT("Heat on Rime"), Crystal.Result == EGFQuestResult::Ok && Line.Progress.HeatFactions.Contains(Rime));
		TestTrue(TEXT("Still an outsider"), Line.Progress.Rank == EGFFactionRank::Outsider);
		TestTrue(TEXT("Heat cleared"), Line.Apply(FGFQuestEvents::HeatCleared(TEXT("P"), Rime)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Ready to talk"), Line.Progress.Step == EGFQuestStep::TalkToElder);
		TestEqual(TEXT("Heat is gone"), Line.Progress.HeatFactions.Num(), 0);
		const FGFQuestApplyResult Accept = Line.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), Rime));
		TestTrue(TEXT("Accept claims"), Accept.bClaimFaction && Accept.Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Hunting hide"), Line.Progress.Step == EGFQuestStep::HuntHide);
		TestTrue(TEXT("Initiate"), Line.Progress.Rank == EGFFactionRank::Initiate);
		TestEqual(TEXT("Joined Rime"), Line.Progress.FactionId, Rime);
		const FGFQuestApplyResult HideTurn = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 4), FGFItemStack(Scales, 2) }));
		TestTrue(TEXT("Hunting skins"), Line.Progress.Step == EGFQuestStep::HuntSkins);
		TestTrue(TEXT("Member"), Line.Progress.Rank == EGFFactionRank::Member);
		ChargeIs(*this, TEXT("Hide mix"), HideTurn.Charge, { FGFItemStack(Hide, 4), FGFItemStack(Scales, 2) });
		TestTrue(TEXT("Town skins"), Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 1), FGFItemStack(Raptor, 1) })).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Crafting"), Line.Progress.Step == EGFQuestStep::CraftLook);
		TestTrue(TEXT("Champion"), Line.Progress.Rank == EGFFactionRank::Champion);
		TestTrue(TEXT("Crafted"), Line.Apply(FGFQuestEvents::OutfitCrafted(TEXT("P"), Loafer)).Result == EGFQuestResult::Ok);
		TestEqual(TEXT("Waiting to wear it"), Line.Progress.Count, 1);
		TestTrue(TEXT("Worn"), Line.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Loafer)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("At the show"), Line.Progress.Step == EGFQuestStep::WinShow);
		TestTrue(TEXT("Entered"), Line.Apply(FGFQuestEvents::ShowEntered(TEXT("P"))).Result == EGFQuestResult::Ok);
		const FGFQuestApplyResult Win = Line.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, FGFQuestShowRule::WinTier));
		TestTrue(TEXT("Leader recorded"), Win.bBecameLeader && Win.Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Done"), Line.Progress.Step == EGFQuestStep::Done);
		TestTrue(TEXT("Leader rank"), Line.Progress.Rank == EGFFactionRank::Leader);
		TestEqual(TEXT("Nothing after Done"), Line.Apply(FGFQuestEvents::CrystalTaken(TEXT("P"), Rime)).Reason, FGFQuestCodes::OutOfOrder);
		TestTrue(TEXT("Stays done"), Line.Progress.Step == EGFQuestStep::Done);
	}

	// The only skip: an elder accepts before the theft, or during the escape, and not the town that is still hot.
	{
		FGFQuestLine Early;
		const FGFQuestApplyResult Accept = Early.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), Cinder));
		TestTrue(TEXT("Skip from the start"), Accept.bClaimFaction);
		TestTrue(TEXT("Straight to the hunt"), Early.Progress.Step == EGFQuestStep::HuntHide);
		TestEqual(TEXT("Cinderwrights"), Early.Progress.FactionId, Cinder);
		TestTrue(TEXT("Initiate from the skip"), Early.Progress.Rank == EGFFactionRank::Initiate);

		FGFQuestLine Hot;
		Hot.Apply(FGFQuestEvents::CrystalTaken(TEXT("P"), Rime));
		const FGFQuestApplyResult Refused = Hot.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), Rime));
		TestEqual(TEXT("Hot town refuses"), Refused.Reason, FGFQuestCodes::Heat);
		TestTrue(TEXT("Still escaping"), Hot.Progress.Step == EGFQuestStep::Escape);
		const FGFQuestApplyResult Other = Hot.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), Verdant));
		TestTrue(TEXT("Another town accepts"), Other.bClaimFaction && Other.Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Skipped to the hunt"), Hot.Progress.Step == EGFQuestStep::HuntHide);
		TestEqual(TEXT("Joined Verdant"), Hot.Progress.FactionId, Verdant);
		TestTrue(TEXT("Rime heat remains"), Hot.Progress.HeatFactions.Contains(Rime));

		FGFQuestLine Clear;
		Clear.Progress.Step = EGFQuestStep::Escape;
		Clear.Progress.HeatFactions = { Rime, Verdant };
		TestTrue(TEXT("One town still hot"), Clear.Apply(FGFQuestEvents::HeatCleared(TEXT("P"), Rime)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Not clear yet"), Clear.Progress.Step == EGFQuestStep::Escape);
		TestFalse(TEXT("Rime heat dropped"), Clear.Progress.HeatFactions.Contains(Rime));
		TestEqual(TEXT("Wrong town clear"), Clear.Apply(FGFQuestEvents::HeatCleared(TEXT("P"), Cinder)).Reason, FGFQuestCodes::WrongTown);
		TestTrue(TEXT("Last heat"), Clear.Apply(FGFQuestEvents::HeatCleared(TEXT("P"), Verdant)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Now they can talk"), Clear.Progress.Step == EGFQuestStep::TalkToElder);

		FGFQuestLine None;
		None.Progress.HeatFactions.Add(Rime);
		TestEqual(TEXT("Heat at the first step"), None.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), Rime)).Reason, FGFQuestCodes::Heat);
		TestEqual(TEXT("No faction yet"), None.Apply(FGFQuestEvents::ElderAccepted(TEXT("P"), NAME_None)).Reason, FGFQuestCodes::NoFaction);
		TestTrue(TEXT("Still on the crystal"), None.Progress.Step == EGFQuestStep::TakeCrystal);
	}

	// Show score, craft order, and a bad event value.
	{
		FGFQuestLine Show;
		Show.Progress.Step = EGFQuestStep::WinShow;
		Show.Progress.Count = 1;
		Show.Progress.Rank = EGFFactionRank::Champion;
		Show.Progress.FactionId = Rime;
		TestEqual(TEXT("Second place"), Show.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 2, 3)).Reason, FGFQuestCodes::LowScore);
		TestEqual(TEXT("A lost show drops the entry"), Show.Progress.Count, 0);
		TestEqual(TEXT("A later place without a new entry does not count"), Show.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, 3)).Reason, FGFQuestCodes::OutOfOrder);
		TestTrue(TEXT("Entering the next show works"), Show.Apply(FGFQuestEvents::ShowEntered(TEXT("P"))).Result == EGFQuestResult::Ok);
		TestEqual(TEXT("Tier B"), Show.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, 1)).Reason, FGFQuestCodes::LowScore);
		TestEqual(TEXT("Dropped again"), Show.Progress.Count, 0);
		TestTrue(TEXT("Still on the show step"), Show.Progress.Step == EGFQuestStep::WinShow);
		// An abandoned show reports place 0, which is a loss like any other.
		Show.Apply(FGFQuestEvents::ShowEntered(TEXT("P")));
		TestEqual(TEXT("Abandoned show"), Show.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 0, 0)).Reason, FGFQuestCodes::LowScore);
		TestEqual(TEXT("Abandoned show drops the entry"), Show.Progress.Count, 0);
		Show.Apply(FGFQuestEvents::ShowEntered(TEXT("P")));
		const FGFQuestApplyResult TierS = Show.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, 3));
		TestTrue(TEXT("Tier S wins"), TierS.bBecameLeader);
		TestTrue(TEXT("Leader"), Show.Progress.Rank == EGFFactionRank::Leader);

		FGFQuestLine Craft;
		Craft.Progress.Step = EGFQuestStep::CraftLook;
		Craft.Progress.Rank = EGFFactionRank::Champion;
		TestEqual(TEXT("Wear before craft"), Craft.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Loafer)).Reason, FGFQuestCodes::OutOfOrder);
		Craft.Apply(FGFQuestEvents::OutfitCrafted(TEXT("P"), Loafer));
		TestEqual(TEXT("A different piece"), Craft.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Pack)).Reason, FGFQuestCodes::WrongItem);
		TestEqual(TEXT("Still crafted"), Craft.Progress.Count, 1);
		TestTrue(TEXT("The crafted piece"), Craft.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Loafer)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Show next"), Craft.Progress.Step == EGFQuestStep::WinShow);

		// #330: the first piece was traded away. Crafting another one (any Hide or Scales piece) replaces it.
		FGFQuestLine Lost;
		Lost.Progress.Step = EGFQuestStep::CraftLook;
		Lost.Progress.Rank = EGFFactionRank::Champion;
		TestTrue(TEXT("First craft"), Lost.Apply(FGFQuestEvents::OutfitCrafted(TEXT("P"), Loafer)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("A second craft is taken"), Lost.Apply(FGFQuestEvents::OutfitCrafted(TEXT("P"), Pack)).Result == EGFQuestResult::Ok);
		TestEqual(TEXT("And replaces the piece to wear"), Lost.Progress.CraftedItem, Pack);
		TestEqual(TEXT("Still one craft"), Lost.Progress.Count, 1);
		TestEqual(TEXT("The old piece no longer counts"), Lost.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Loafer)).Reason, FGFQuestCodes::WrongItem);
		TestTrue(TEXT("The new piece does"), Lost.Apply(FGFQuestEvents::OutfitWorn(TEXT("P"), Pack)).Result == EGFQuestResult::Ok);
		TestTrue(TEXT("Show next after a recraft"), Lost.Progress.Step == EGFQuestStep::WinShow);

		FGFQuestLine Bad;
		FGFQuestEvent Unknown;
		Unknown.Kind = static_cast<EGFQuestEventKind>(255);
		Unknown.PlayerKey = TEXT("P");
		const FGFQuestProgress Before = Bad.Progress;
		const FGFQuestApplyResult Junk = Bad.Apply(Unknown);
		TestEqual(TEXT("Unknown event"), Junk.Reason, FGFQuestCodes::Unknown);
		TestTrue(TEXT("Unknown changes nothing"), Bad.Progress == Before);
	}

	// Two lines do not share a step.
	{
		FGFQuestLine A;
		FGFQuestLine B;
		A.Apply(FGFQuestEvents::CrystalTaken(TEXT("A"), Rime));
		B.Apply(FGFQuestEvents::ElderAccepted(TEXT("B"), Verdant));
		TestTrue(TEXT("A is escaping"), A.Progress.Step == EGFQuestStep::Escape);
		TestTrue(TEXT("B is hunting"), B.Progress.Step == EGFQuestStep::HuntHide);
		TestEqual(TEXT("A has no faction"), A.Progress.FactionId, FName());
		TestEqual(TEXT("B joined"), B.Progress.FactionId, Verdant);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestLineTurnInTest, "GFGame.Quest.Line.TurnIn", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestLineTurnInTest::RunTest(const FString&)
{
	using namespace GFQuestTest;
	auto HideLine = []() { return At(EGFQuestStep::HuntHide, Rime, EGFFactionRank::Initiate); };
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 4), FGFItemStack(Scales, 2) }));
		TestTrue(TEXT("4 and 2"), Got.Result == EGFQuestResult::Ok);
		ChargeIs(*this, TEXT("4 and 2 charge"), Got.Charge, { FGFItemStack(Hide, 4), FGFItemStack(Scales, 2) });
		TestTrue(TEXT("Member"), Line.Progress.Rank == EGFFactionRank::Member);
		TestTrue(TEXT("Skins next"), Line.Progress.Step == EGFQuestStep::HuntSkins);
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 4), FGFItemStack(Scales, 4) }));
		ChargeIs(*this, TEXT("Stops at 6"), Got.Charge, { FGFItemStack(Hide, 4), FGFItemStack(Scales, 2) });
		TestTrue(TEXT("Advanced"), Line.Progress.Step == EGFQuestStep::HuntSkins);
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Scales, 1), FGFItemStack(Hide, 10) }));
		ChargeIs(*this, TEXT("Scales then Hide"), Got.Charge, { FGFItemStack(Scales, 1), FGFItemStack(Hide, 5) });
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 6), FGFItemStack(Scales, 4) }));
		ChargeIs(*this, TEXT("First stack covers it"), Got.Charge, { FGFItemStack(Hide, 6) });
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), Scales, 10));
		ChargeIs(*this, TEXT("Scales shorthand"), Got.Charge, { FGFItemStack(Scales, 6) });
		TestTrue(TEXT("Advanced on shorthand"), Line.Progress.Step == EGFQuestStep::HuntSkins);
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestProgress Before = Line.Progress;
		const FGFQuestApplyResult Short = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Hide, 3), FGFItemStack(Scales, 2) }));
		TestEqual(TEXT("5 is short"), Short.Reason, FGFQuestCodes::Short);
		TestEqual(TEXT("Short takes nothing"), Short.Charge.Num(), 0);
		TestTrue(TEXT("Short changes nothing"), Line.Progress == Before);
		const FGFQuestApplyResult Empty = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), {}));
		TestEqual(TEXT("Empty is short"), Empty.Reason, FGFQuestCodes::Short);
		TestTrue(TEXT("Empty changes nothing"), Line.Progress == Before);
		const FGFQuestApplyResult Bone = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(TEXT("Bone"), 6) }));
		TestEqual(TEXT("Bone is the wrong item"), Bone.Reason, FGFQuestCodes::WrongItem);
		TestTrue(TEXT("Bone changes nothing"), Line.Progress == Before);
		const FGFQuestApplyResult Mixed = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(TEXT("Bone"), 6), FGFItemStack(Hide, 6) }));
		ChargeIs(*this, TEXT("Bone is not charged"), Mixed.Charge, { FGFItemStack(Hide, 6) });
		TestTrue(TEXT("Hide still counts"), Line.Progress.Step == EGFQuestStep::HuntSkins);
	}
	{
		FGFQuestLine Line = HideLine();
		const FGFQuestProgress Before = Line.Progress;
		const FGFQuestApplyResult Skins = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 2) }));
		TestEqual(TEXT("Skins are not hide"), Skins.Reason, FGFQuestCodes::WrongItem);
		TestTrue(TEXT("Skins do not finish the hide step"), Line.Progress == Before);
	}

	auto SkinLine = [](FName Faction) { return At(EGFQuestStep::HuntSkins, Faction, EGFFactionRank::Member); };
	{
		FGFQuestLine Line = SkinLine(Rime);
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 1), FGFItemStack(Raptor, 1) }));
		ChargeIs(*this, TEXT("One of each"), Got.Charge, { FGFItemStack(Nipper, 1), FGFItemStack(Raptor, 1) });
		TestTrue(TEXT("Champion"), Line.Progress.Rank == EGFFactionRank::Champion);
		TestTrue(TEXT("Craft next"), Line.Progress.Step == EGFQuestStep::CraftLook);
	}
	{
		FGFQuestLine Line = SkinLine(Rime);
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 3) }));
		ChargeIs(*this, TEXT("Two of one species"), Got.Charge, { FGFItemStack(Nipper, 2) });
	}
	{
		FGFQuestLine Line = SkinLine(Rime);
		const FGFQuestProgress Before = Line.Progress;
		const FGFQuestApplyResult Short = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 1), FGFItemStack(Tusksaw, 5) }));
		TestEqual(TEXT("One town skin is short"), Short.Reason, FGFQuestCodes::Short);
		TestTrue(TEXT("Short skin changes nothing"), Line.Progress == Before);
		const FGFQuestApplyResult Wrong = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Tusksaw, 1), FGFItemStack(Lantern, 1) }));
		TestEqual(TEXT("Verdant skins at Rime"), Wrong.Reason, FGFQuestCodes::WrongItem);
		TestTrue(TEXT("Wrong town changes nothing"), Line.Progress == Before);
	}
	{
		FGFQuestLine Line = SkinLine(Verdant);
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Tusksaw, 2), FGFItemStack(Lantern, 1) }));
		ChargeIs(*this, TEXT("Verdant pair"), Got.Charge, { FGFItemStack(Tusksaw, 2) });
	}
	{
		FGFQuestLine Line = SkinLine(Cinder);
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(TEXT("Skin_Scythepaw"), 1), FGFItemStack(TEXT("Skin_Hornback"), 1) }));
		TestTrue(TEXT("Cinderwrights skins"), Got.Result == EGFQuestResult::Ok);
		ChargeIs(*this, TEXT("Cinderwrights charge"), Got.Charge, { FGFItemStack(TEXT("Skin_Scythepaw"), 1), FGFItemStack(TEXT("Skin_Hornback"), 1) });
	}
	{
		FGFQuestLine Line = SkinLine(NAME_None);
		const FGFQuestProgress Before = Line.Progress;
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 2) }));
		TestEqual(TEXT("No town yet"), Got.Reason, FGFQuestCodes::NoFaction);
		TestTrue(TEXT("No town changes nothing"), Line.Progress == Before);
		Line.Progress.FactionId = TEXT("Nope");
		const FGFQuestProgress UnknownTown = Line.Progress;
		TestEqual(TEXT("Unknown town"), Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), { FGFItemStack(Nipper, 2) })).Reason, FGFQuestCodes::WrongItem);
		TestTrue(TEXT("Unknown town changes nothing"), Line.Progress == UnknownTown);
	}
	{
		FGFQuestLine Line;
		Line.Progress.Step = EGFQuestStep::TalkToElder;
		const FGFQuestProgress Before = Line.Progress;
		const FGFQuestApplyResult Got = Line.Apply(FGFQuestEvents::TurnIn(TEXT("P"), Hide, 6));
		TestEqual(TEXT("Turn-in before the hunt"), Got.Reason, FGFQuestCodes::OutOfOrder);
		TestTrue(TEXT("Early turn-in changes nothing"), Line.Progress == Before);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestDataRowsTest, "GFGame.Quest.Data.Rows", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestDataRowsTest::RunTest(const FString&)
{
	using namespace GFQuestTest;
	const UDataTable* Table = FGFDataTables::Get<FGFQuestRow>(TEXT("DT_Quests"));
	if (!TestNotNull(TEXT("DT_Quests"), Table))
	{
		return false;
	}
	const FGFQuestCatalog Builtin = FGFQuestCatalog::Builtin();
	const FGFQuestCatalog Loaded = FGFQuestCatalog::Load();
	const TArray<EGFQuestStep> Steps = Values<EGFQuestStep>();
	TestEqual(TEXT("Builtin has every step"), Builtin.Steps.Num(), Steps.Num());

	struct FWant
	{
		EGFQuestStep Step;
		int32 Goal;
		EGFFactionRank Rank;
		TArray<FName> Items;
	};
	const FWant Wants[] = {
		{ EGFQuestStep::TakeCrystal, 1, EGFFactionRank::Outsider, {} },
		{ EGFQuestStep::Escape, 1, EGFFactionRank::Outsider, {} },
		{ EGFQuestStep::TalkToElder, 1, EGFFactionRank::Initiate, {} },
		{ EGFQuestStep::HuntHide, 6, EGFFactionRank::Member, { Hide, Scales } },
		{ EGFQuestStep::HuntSkins, 2, EGFFactionRank::Champion, {} },
		{ EGFQuestStep::CraftLook, 1, EGFFactionRank::Champion, {} },
		{ EGFQuestStep::WinShow, 1, EGFFactionRank::Leader, {} },
		{ EGFQuestStep::Done, 0, EGFFactionRank::Leader, {} },
	};
	for (const FWant& Want : Wants)
	{
		const FName Id = FGFQuestCatalog::StepId(Want.Step);
		const FGFQuestRow* Row = Table->FindRow<FGFQuestRow>(Id, TEXT("Quest"), false);
		const FString Name = Label(Want.Step);
		if (!TestNotNull(*FString::Printf(TEXT("Row %s"), *Name), Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s title"), *Name), Row->Title, FString(TEXT("The Elder's Errand")));
		TestFalse(*FString::Printf(TEXT("%s hint"), *Name), Row->Hint.IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s goal"), *Name), Row->Goal, Want.Goal);
		TestTrue(*FString::Printf(TEXT("%s rank"), *Name), Row->RankOnComplete == Want.Rank);
		TestTrue(*FString::Printf(TEXT("%s items"), *Name), Row->Items == Want.Items);
		TestTrue(*FString::Printf(TEXT("%s is a step"), *Name), Row->TownId.IsNone());
		TestEqual(*FString::Printf(TEXT("Builtin %s goal"), *Name), Builtin.GoalFor(Want.Step), Want.Goal);
		TestTrue(*FString::Printf(TEXT("Builtin %s rank"), *Name), Builtin.RankAfter(Want.Step) == Want.Rank);
		TestEqual(*FString::Printf(TEXT("Loaded %s goal"), *Name), Loaded.GoalFor(Want.Step), Want.Goal);
		TestTrue(*FString::Printf(TEXT("Loaded %s items"), *Name), Loaded.ItemsFor(Want.Step) == Want.Items);
		EGFQuestStep Parsed;
		TestTrue(*FString::Printf(TEXT("Parse %s"), *Name), FGFQuestCatalog::TryStep(Id, Parsed) && Parsed == Want.Step);
	}

	struct FTown
	{
		FName Id;
		const TCHAR* Title;
		FName A;
		FName B;
	};
	const FTown Towns[] = {
		{ Rime, TEXT("Rime Elder"), Nipper, Raptor },
		{ Verdant, TEXT("Verdant Elder"), Tusksaw, Lantern },
		{ Cinder, TEXT("Cinderwrights Elder"), FName(TEXT("Skin_Scythepaw")), FName(TEXT("Skin_Hornback")) },
	};
	for (const FTown& Town : Towns)
	{
		const FGFQuestRow* Row = Table->FindRow<FGFQuestRow>(Town.Id, TEXT("Quest"), false);
		if (!TestNotNull(*FString::Printf(TEXT("Town %s"), *Town.Id.ToString()), Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s town id"), *Town.Id.ToString()), Row->TownId, Town.Id);
		TestEqual(*FString::Printf(TEXT("%s elder"), *Town.Id.ToString()), Row->Title, FString(Town.Title));
		TestEqual(*FString::Printf(TEXT("%s goal"), *Town.Id.ToString()), Row->Goal, 2);
		TestTrue(*FString::Printf(TEXT("%s skins"), *Town.Id.ToString()), Row->Items == TArray<FName>({ Town.A, Town.B }));
		TestTrue(*FString::Printf(TEXT("Loaded %s"), *Town.Id.ToString()), Loaded.SkinsFor(Town.Id) == TArray<FName>({ Town.A, Town.B }));
		EGFQuestStep Ignored;
		TestFalse(*FString::Printf(TEXT("%s is not a step"), *Town.Id.ToString()), FGFQuestCatalog::TryStep(Town.Id, Ignored));
	}
	TestEqual(TEXT("Row count"), Table->GetRowMap().Num(), Steps.Num() + 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestSaveRoundTripTest, "GFGame.Quest.Save.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestSaveRoundTripTest::RunTest(const FString&)
{
	using namespace GFQuestTest;
	{
		FGFQuestSave Save;
		FGFQuestRecord A;
		A.PlayerKey = TEXT("A");
		A.Progress.Step = EGFQuestStep::HuntSkins;
		A.Progress.FactionId = Verdant;
		A.Progress.Rank = EGFFactionRank::Member;
		A.Progress.HeatFactions.Add(Rime);
		FGFQuestRecord B;
		B.PlayerKey = TEXT("B");
		B.Progress.Step = EGFQuestStep::Done;
		B.Progress.FactionId = Verdant;
		B.Progress.Rank = EGFFactionRank::Leader;
		Save.Records = { A, B };
		TArray<uint8> Bytes;
		FGFModeBytes::Write(Save, Bytes);
		FGFQuestSave Back;
		TestTrue(TEXT("Bytes read"), FGFModeBytes::Read(Back, Bytes));
		TestEqual(TEXT("Two records"), Back.Records.Num(), 2);
		TestEqual(TEXT("A key"), Back.Records[0].PlayerKey, FString(TEXT("A")));
		TestTrue(TEXT("A progress"), Back.Records[0].Progress == A.Progress);
		TestTrue(TEXT("B progress"), Back.Records[1].Progress == B.Progress);
	}

	FWorld World;
	if (!World.Init(*this, TEXT("GFQuestSave")))
	{
		return false;
	}
	AGFPlayerState* A = World.Add(TEXT("A"));
	AGFPlayerState* B = World.Add(TEXT("B"));
	AGFPlayerState* C = World.Add(TEXT("C"));
	if (!TestNotNull(TEXT("A"), A) || !TestNotNull(TEXT("B"), B) || !TestNotNull(TEXT("C"), C))
	{
		return false;
	}
	TestEqual(TEXT("C founds Verdant"), World.GS->ServerClaimFaction(C, Verdant), FName(TEXT("Ok")));
	TestEqual(TEXT("C leads"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));

	const FGFQuestApplyResult Empty = World.Quest->ApplyEvent(FGFQuestEvent());
	TestEqual(TEXT("Empty key"), Empty.Reason, FGFQuestCodes::Unknown);

	FGFQuestEvents::Raise(World.World, FGFQuestEvents::ElderAccepted(TEXT("A"), Verdant));
	const FGFQuestProgress* AProgress = World.Quest->FindProgress(TEXT("A"));
	if (!TestNotNull(TEXT("A line"), AProgress))
	{
		return false;
	}
	TestTrue(TEXT("A hunt"), AProgress->Step == EGFQuestStep::HuntHide);
	TestTrue(TEXT("A initiate"), AProgress->Rank == EGFFactionRank::Initiate);
	TestEqual(TEXT("A joined"), A->FactionId, Verdant);
	TestTrue(TEXT("A step replicated"), A->QuestStep == EGFQuestStep::HuntHide);
	TestTrue(TEXT("A rank replicated"), A->FactionRank == EGFFactionRank::Initiate);
	TestEqual(TEXT("C still leads after A joins"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestTrue(TEXT("B has no line yet"), World.Quest->FindProgress(TEXT("B")) == nullptr);

	const FGFQuestApplyResult Lie = World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("A"), Hide, 6));
	TestEqual(TEXT("Offer without the items"), Lie.Reason, FGFQuestCodes::Short);
	TestTrue(TEXT("Lie stays on hide"), World.Quest->FindProgress(TEXT("A"))->Step == EGFQuestStep::HuntHide);
	TestEqual(TEXT("Lie took nothing"), A->Inventory->GetCount(Hide), 0);

	TestTrue(TEXT("Give 3 Hide"), A->Inventory->AddItem(Hide, 3));
	const FGFQuestApplyResult Short = World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Hide, 3) }));
	TestEqual(TEXT("3 is short"), Short.Reason, FGFQuestCodes::Short);
	TestEqual(TEXT("3 Hide remain"), A->Inventory->GetCount(Hide), 3);
	TestTrue(TEXT("Still hide"), World.Quest->FindProgress(TEXT("A"))->Step == EGFQuestStep::HuntHide);

	TestTrue(TEXT("Give the rest"), A->Inventory->AddItem(Hide, 1) && A->Inventory->AddItem(Scales, 4));
	const FGFQuestApplyResult Paid = World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Hide, 4), FGFItemStack(Scales, 4) }));
	TestTrue(TEXT("Mix paid"), Paid.Result == EGFQuestResult::Ok);
	TestTrue(TEXT("A on skins"), World.Quest->FindProgress(TEXT("A"))->Step == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("A member"), A->FactionRank == EGFFactionRank::Member);
	TestEqual(TEXT("Hide cleared"), A->Inventory->GetCount(Hide), 0);
	TestEqual(TEXT("2 Scales left"), A->Inventory->GetCount(Scales), 2);

	TestTrue(TEXT("B stole from Rime"), World.Quest->ApplyEvent(FGFQuestEvents::CrystalTaken(TEXT("B"), Rime)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Rime elder refuses B"), World.Quest->ApplyEvent(FGFQuestEvents::ElderAccepted(TEXT("B"), Rime)).Reason, FGFQuestCodes::Heat);
	TestTrue(TEXT("B joins Verdant"), World.Quest->ApplyEvent(FGFQuestEvents::ElderAccepted(TEXT("B"), Verdant)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Leader did not move"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestTrue(TEXT("B still has Rime heat"), World.Quest->FindProgress(TEXT("B"))->HeatFactions.Contains(Rime));
	TestTrue(TEXT("Give B hide"), B->Inventory->AddItem(Hide, 6));
	TestTrue(TEXT("B skins step"), World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("B"), Hide, 6)).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Give B three skins"), B->Inventory->AddItem(Tusksaw, 3));
	TestTrue(TEXT("B crafts next"), World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("B"), { FGFItemStack(Tusksaw, 3) })).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("One skin left"), B->Inventory->GetCount(Tusksaw), 1);
	TestTrue(TEXT("B crafted"), World.Quest->ApplyEvent(FGFQuestEvents::OutfitCrafted(TEXT("B"), Loafer)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("B count"), B->QuestCount, 1);
	TestEqual(TEXT("Wrong piece"), World.Quest->ApplyEvent(FGFQuestEvents::OutfitWorn(TEXT("B"), Pack)).Reason, FGFQuestCodes::WrongItem);
	TestTrue(TEXT("B wore it"), World.Quest->ApplyEvent(FGFQuestEvents::OutfitWorn(TEXT("B"), Loafer)).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Place before entry"), World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 1, 3)).Reason, FGFQuestCodes::OutOfOrder);
	TestTrue(TEXT("B entered"), World.Quest->ApplyEvent(FGFQuestEvents::ShowEntered(TEXT("B"))).Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Low score"), World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 2, 3)).Reason, FGFQuestCodes::LowScore);
	TestEqual(TEXT("The lost entry reaches the player"), B->QuestCount, 0);
	TestTrue(TEXT("B enters the next show"), World.Quest->ApplyEvent(FGFQuestEvents::ShowEntered(TEXT("B"))).Result == EGFQuestResult::Ok);

	FString LeaderKey;
	FName LeaderFaction;
	int32 LeaderHits = 0;
	World.Quest->OnQuestLeader.AddLambda([&](const FString& Key, FName Faction)
	{
		++LeaderHits;
		LeaderKey = Key;
		LeaderFaction = Faction;
	});
	const FGFQuestApplyResult Won = World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 1, FGFQuestShowRule::WinTier));
	TestTrue(TEXT("B won"), Won.bBecameLeader && Won.Result == EGFQuestResult::Ok);
	TestEqual(TEXT("One leader note"), LeaderHits, 1);
	TestEqual(TEXT("Note names B"), LeaderKey, FString(TEXT("B")));
	TestEqual(TEXT("Note names Verdant"), LeaderFaction, Verdant);
	TestTrue(TEXT("B rank Leader"), B->FactionRank == EGFFactionRank::Leader);
	// B is a Champion of Verdant and placed first at tier A, so the seat moves off the first claimer.
	TestEqual(TEXT("B takes the Verdant seat"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	TestTrue(TEXT("A still on skins"), A->QuestStep == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("A still a member"), A->FactionRank == EGFFactionRank::Member);
	TestTrue(TEXT("C quest untouched"), C->QuestStep == EGFQuestStep::TakeCrystal);
	TestTrue(TEXT("C still outsider"), C->FactionRank == EGFFactionRank::Outsider);

	UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(World.World);
	Save->AddToRoot();
	const FGFModeBlob* Blob = Save->ModeStates.Find(UGFQuestSubsystem::ModeName);
	FGFQuestSave Decoded;
	const bool bDecoded = TestNotNull(TEXT("Quest blob"), Blob) && Blob && FGFModeBytes::Read(Decoded, Blob->Bytes);
	TestTrue(TEXT("Blob decodes"), bDecoded);
	if (bDecoded)
	{
		const FGFQuestRecord* SavedA = Decoded.Records.FindByPredicate([](const FGFQuestRecord& Record) { return Record.PlayerKey == TEXT("A"); });
		const FGFQuestRecord* SavedB = Decoded.Records.FindByPredicate([](const FGFQuestRecord& Record) { return Record.PlayerKey == TEXT("B"); });
		TestTrue(TEXT("Saved A"), SavedA && SavedA->Progress == *World.Quest->FindProgress(TEXT("A")));
		TestTrue(TEXT("Saved B"), SavedB && SavedB->Progress == *World.Quest->FindProgress(TEXT("B")));
	}

	TestTrue(TEXT("Give A a skin"), A->Inventory->AddItem(Tusksaw, 2));
	TestTrue(TEXT("A moves on"), World.Quest->ApplyEvent(FGFQuestEvents::TurnIn(TEXT("A"), { FGFItemStack(Tusksaw, 2) })).Result == EGFQuestResult::Ok);
	TestTrue(TEXT("A crafting"), A->QuestStep == EGFQuestStep::CraftLook);
	TestTrue(TEXT("Apply the world save"), UGFSaveSubsystem::ApplyToWorld(World.World, Save));
	TestTrue(TEXT("A restored step"), World.Quest->FindProgress(TEXT("A"))->Step == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("A restored rank"), A->FactionRank == EGFFactionRank::Member);
	TestTrue(TEXT("A restored view"), A->QuestStep == EGFQuestStep::HuntSkins);
	TestTrue(TEXT("B restored"), B->QuestStep == EGFQuestStep::Done);
	TestTrue(TEXT("B still leader rank"), B->FactionRank == EGFFactionRank::Leader);
	TestTrue(TEXT("B heat restored"), World.Quest->FindProgress(TEXT("B"))->HeatFactions.Contains(Rime));

	A->PlayerKey = TEXT("A-gone");
	AGFPlayerState* Again = World.Add(TEXT("A"));
	if (TestNotNull(TEXT("A reconnects"), Again))
	{
		World.GS->NotifyModesPlayerReady(Again);
		TestTrue(TEXT("Rejoin step"), Again->QuestStep == EGFQuestStep::HuntSkins);
		TestEqual(TEXT("Rejoin count"), Again->QuestCount, 0);
		TestTrue(TEXT("Rejoin rank"), Again->FactionRank == EGFFactionRank::Member);
		TestEqual(TEXT("Rejoin faction"), Again->FactionId, Verdant);
		TestTrue(TEXT("B untouched by the rejoin"), B->QuestStep == EGFQuestStep::Done);
	}
	Save->RemoveFromRoot();
	return true;
}

#endif
