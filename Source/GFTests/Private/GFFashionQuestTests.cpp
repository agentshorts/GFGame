// Fashion quest rules: which crafts and worn pieces raise events, which show results win,
// and which player may take the faction seat. The runway show itself is FGFRunwayShow.
#include "Misc/AutomationTest.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFDataTables.h"
#include "GFElderBook.h"
#include "GFFactionLead.h"
#include "GFFashionGateSubsystem.h"
#include "GFFashionQuest.h"
#include "GFFashionQuestTravel.h"
#include "GFFashionTypes.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFQuestSubsystem.h"
#include "GFRecipeTypes.h"
#include "GFRunwayShow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFFashionQuestTest
{
	static const FName Verdant(TEXT("Verdant"));
	static const FName Rime(TEXT("Rime"));

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
			return T.TestNotNull(TEXT("Game state"), GS) && T.TestNotNull(TEXT("Quest"), Quest)
				&& T.TestNotNull(TEXT("Elder Book"), UGFElderBookSubsystem::Get(World));
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

	int32 ShowWins(const UWorld* World, const FString& Key)
	{
		const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(World);
		if (!Book)
		{
			return -1;
		}
		int32 Count = 0;
		for (const FGFElderPost& Post : Book->GetBook().Feed(Verdant))
		{
			if (Post.Kind == EGFElderKind::ShowWin && Post.SubjectKey == Key)
			{
				++Count;
			}
		}
		return Count;
	}

	FGFQuestLine EnteredShow()
	{
		FGFQuestLine Line;
		Line.Progress.Step = EGFQuestStep::WinShow;
		Line.Progress.Count = 1;
		Line.Progress.FactionId = Verdant;
		Line.Progress.Rank = EGFFactionRank::Champion;
		return Line;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionQuestCraftEventTest, "GFGame.FashionQuest.Craft.Event", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFFashionQuestCraftEventTest::RunTest(const FString&)
{
	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	if (!TestNotNull(TEXT("DT_Recipes"), Recipes))
	{
		return false;
	}
	auto Item = [](const FGFRecipeRow* Row, bool bCrafted)
	{
		return FGFFashionQuest::CraftEventItem(bCrafted, Row->Station, Row->Inputs, Row->Output.ItemId);
	};
	const FGFRecipeRow* Dirt = Recipes->FindRow<FGFRecipeRow>(TEXT("dirtachi_01"), TEXT("FashionQuest"), false);
	const FGFRecipeRow* Pred = Recipes->FindRow<FGFRecipeRow>(TEXT("predada_01"), TEXT("FashionQuest"), false);
	const FGFRecipeRow* Plain = Recipes->FindRow<FGFRecipeRow>(TEXT("dirtachi_03"), TEXT("FashionQuest"), false);
	const FGFRecipeRow* Cake = Recipes->FindRow<FGFRecipeRow>(TEXT("PetitCake"), TEXT("FashionQuest"), false);
	const FGFRecipeRow* Moss = Recipes->FindRow<FGFRecipeRow>(TEXT("hidemoss_01"), TEXT("FashionQuest"), false);
	if (!TestNotNull(TEXT("dirtachi_01"), Dirt) || !TestNotNull(TEXT("predada_01"), Pred) || !TestNotNull(TEXT("dirtachi_03"), Plain) || !TestNotNull(TEXT("PetitCake"), Cake) || !TestNotNull(TEXT("hidemoss_01"), Moss))
	{
		return false;
	}
	TestEqual(TEXT("Scales piece"), Item(Dirt, true), FName(TEXT("dirtachi_01")));
	TestEqual(TEXT("Scales and fabric"), Item(Pred, true), FName(TEXT("predada_01")));
	TestTrue(TEXT("Atelier without Hide or Scales"), Item(Plain, true).IsNone());
	TestTrue(TEXT("Scales at the cookpot"), Item(Cake, true).IsNone());
	TestTrue(TEXT("A name with hide is not the Hide item"), Item(Moss, true).IsNone());
	TestTrue(TEXT("A failed sew raises nothing"), Item(Dirt, false).IsNone());

	TArray<FGFItemStack> Hide;
	Hide.Add(FGFItemStack(TEXT("Hide"), 2));
	TestEqual(TEXT("Hide piece"), FGFFashionQuest::CraftEventItem(true, FGFFashionQuest::AtelierStation, Hide, TEXT("predada_01")), FName(TEXT("predada_01")));
	TArray<FGFItemStack> Zero;
	Zero.Add(FGFItemStack(TEXT("Scales"), 0));
	TestTrue(TEXT("A zero stack is not material"), FGFFashionQuest::CraftEventItem(true, FGFFashionQuest::AtelierStation, Zero, TEXT("dirtachi_01")).IsNone());

	TestEqual(TEXT("Worn piece"), FGFFashionQuest::WornEventItem(true, TEXT("dirtachi_01")), FName(TEXT("dirtachi_01")));
	TestTrue(TEXT("Refused equip"), FGFFashionQuest::WornEventItem(false, TEXT("dirtachi_01")).IsNone());
	TestTrue(TEXT("Empty piece"), FGFFashionQuest::WornEventItem(true, NAME_None).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionQuestShowWinTierTest, "GFGame.FashionQuest.Show.WinTier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFFashionQuestShowWinTierTest::RunTest(const FString&)
{
	TestEqual(TEXT("Tier C"), FGFFashionQuest::TierValue(EGFRunwayTier::C), 0);
	TestEqual(TEXT("Tier B"), FGFFashionQuest::TierValue(EGFRunwayTier::B), 1);
	TestEqual(TEXT("Tier A"), FGFFashionQuest::TierValue(EGFRunwayTier::A), 2);
	TestEqual(TEXT("Tier S"), FGFFashionQuest::TierValue(EGFRunwayTier::S), 3);
	TestTrue(TEXT("Solo tier A"), FGFFashionQuest::IsShowWin(1, 2, 1));
	TestTrue(TEXT("Solo tier S"), FGFFashionQuest::IsShowWin(1, 3, 1));
	TestTrue(TEXT("A full field tier A"), FGFFashionQuest::IsShowWin(1, 2, 4));
	TestFalse(TEXT("Tier B"), FGFFashionQuest::IsShowWin(1, 1, 1));
	TestFalse(TEXT("Tier C"), FGFFashionQuest::IsShowWin(1, 0, 1));
	TestFalse(TEXT("Second place"), FGFFashionQuest::IsShowWin(2, 3, 2));
	TestFalse(TEXT("Nobody walked"), FGFFashionQuest::IsShowWin(1, 3, 0));

	FGFRunwayShow Show;
	TestTrue(TEXT("One contestant starts"), Show.Start(FGuid(1, 2, 3, 4), TEXT("GardenParty"), 7, { TEXT("Solo") }));
	FGFOutfit Look;
	TestTrue(TEXT("Solo dresses"), Look.Equip(TEXT("FernBraid")) && Look.Equip(TEXT("PetalBlouse")) && Look.Equip(TEXT("MeadowSkirt")) && Look.Equip(TEXT("BloomSandals")) && Look.Equip(TEXT("DaisyCrown")));
	TestTrue(TEXT("Outfit accepted"), Show.SetOutfit(TEXT("Solo"), Look));
	TestTrue(TEXT("Ready"), Show.SetReady(TEXT("Solo")));
	TestTrue(TEXT("Ready walks"), Show.GetPhase() == EGFRunwayPhase::Walking);
	Show.Advance(100.f);
	TestTrue(TEXT("Solo results"), Show.GetPhase() == EGFRunwayPhase::Results);
	const FGFRunwayContestant* Solo = Show.FindContestant(TEXT("Solo"));
	if (!TestNotNull(TEXT("Solo scored"), Solo))
	{
		return false;
	}
	TestEqual(TEXT("Solo rank"), Solo->Rank, 1);
	TestEqual(TEXT("Solo count"), Show.GetContestants().Num(), 1);
	const int32 Tier = FGFFashionQuest::TierValue(Solo->Score.Tier);
	TestEqual(TEXT("Win rule matches the card"), FGFFashionQuest::IsShowWin(Solo->Rank, Tier, 1), Tier >= FGFQuestShowRule::WinTier);

	FGFQuestLine Low = GFFashionQuestTest::EnteredShow();
	const FGFQuestApplyResult Second = Low.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 2, 3));
	TestEqual(TEXT("Line refuses second"), Second.Reason, FGFQuestCodes::LowScore);
	TestFalse(TEXT("Second does not finish"), Second.bBecameLeader);
	TestEqual(TEXT("Line refuses tier B"), Low.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, 1)).Reason, FGFQuestCodes::LowScore);
	const FGFQuestApplyResult Won = Low.Apply(FGFQuestEvents::ShowPlaced(TEXT("P"), 1, FGFQuestShowRule::WinTier));
	TestTrue(TEXT("Tier A finishes the errand"), Won.bBecameLeader && Won.Result == EGFQuestResult::Ok);
	TestTrue(TEXT("Done"), Low.Progress.Step == EGFQuestStep::Done);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionQuestLeaderSwitchTest, "GFGame.FashionQuest.Leader.Switch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFFashionQuestLeaderSwitchTest::RunTest(const FString&)
{
	using namespace GFFashionQuestTest;
	TestEqual(TEXT("Champion may take the seat"), static_cast<int32>(FGFFactionLead::Decide(true, EGFFactionRank::Champion, Verdant, Verdant, TEXT("B"), TEXT("C"))), static_cast<int32>(FGFFactionLead::EResult::Ok));
	TestEqual(TEXT("Same champion is not a new change"), static_cast<int32>(FGFFactionLead::Decide(true, EGFFactionRank::Champion, Verdant, Verdant, TEXT("B"), TEXT("B"))), static_cast<int32>(FGFFactionLead::EResult::Same));

	FGFFashionCarry Carry;
	TArray<uint8> Bytes;
	TArray<FGFFactionLeader> Leaders;
	TMap<FString, FName> Sessions;
	TestFalse(TEXT("Empty carry"), Carry.Take(Bytes, Leaders, Sessions));
	Sessions.Add(TEXT("B"), Verdant);
	Carry.Stash({ 1, 2 }, {}, Sessions);
	TestTrue(TEXT("Holding"), Carry.State == EGFFashionCarryState::Holding);
	Carry.Stash({ 9 }, {}, {});
	TestTrue(TEXT("A second stash replaces"), Carry.Take(Bytes, Leaders, Sessions) && Bytes.Num() == 1 && Bytes[0] == 9 && Sessions.Num() == 0);
	TestTrue(TEXT("Taken"), Carry.State == EGFFashionCarryState::Empty);
	TestFalse(TEXT("Second take"), Carry.Take(Bytes, Leaders, Sessions));

	FWorld World;
	if (!World.Init(*this, TEXT("GFFashionQuestLead")))
	{
		return false;
	}
	AGFPlayerState* Founder = World.Add(TEXT("C"));
	AGFPlayerState* Winner = World.Add(TEXT("B"));
	if (!TestNotNull(TEXT("Founder"), Founder) || !TestNotNull(TEXT("Winner"), Winner))
	{
		return false;
	}
	TestEqual(TEXT("Founder claims"), World.GS->ServerClaimFaction(Founder, Verdant), FName(TEXT("Ok")));
	TestEqual(TEXT("Winner joins"), World.GS->ServerClaimFaction(Winner, Verdant), FName(TEXT("Ok")));
	TestEqual(TEXT("First claimer leads"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	Winner->FactionRank = EGFFactionRank::Champion;

	FGFQuestSave Save;
	FGFQuestRecord Record;
	Record.PlayerKey = TEXT("B");
	Record.Progress.Step = EGFQuestStep::WinShow;
	Record.Progress.Count = 1;
	Record.Progress.FactionId = Verdant;
	Record.Progress.Rank = EGFFactionRank::Champion;
	Save.Records.Add(Record);
	TArray<uint8> QuestBytes;
	FGFModeBytes::Write(Save, QuestBytes);
	World.Quest->RestoreModeState(QuestBytes);

	TestEqual(TEXT("Second place leaves the seat"), World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 2, 3)).Reason, FGFQuestCodes::LowScore);
	TestEqual(TEXT("Seat after second"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestEqual(TEXT("Tier B leaves the seat"), World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 1, 1)).Reason, FGFQuestCodes::LowScore);
	TestEqual(TEXT("Seat after tier B"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestEqual(TEXT("No show post yet"), ShowWins(World.World, TEXT("B")), 0);

	const FGFQuestApplyResult Won = World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 1, FGFQuestShowRule::WinTier));
	TestTrue(TEXT("Show win"), Won.bBecameLeader && Won.Result == EGFQuestResult::Ok);
	TestEqual(TEXT("Winner leads"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	TestTrue(TEXT("Winner rank"), Winner->FactionRank == EGFFactionRank::Leader);
	TestEqual(TEXT("One show post"), ShowWins(World.World, TEXT("B")), 1);
	TestEqual(TEXT("Leader rank cannot take it again"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("NotChampion")));
	TestEqual(TEXT("Seat stays with the winner"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));

	UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
	UGFFashionGateSubsystem* Gate = NewObject<UGFFashionGateSubsystem>(GI);
	UGFFashionQuestTravel::CaptureInto(Gate, World.World);
	TestTrue(TEXT("Carry is holding"), Gate->GetQuestCarryState() == EGFFashionCarryState::Holding);

	FWorld Next;
	if (!Next.Init(*this, TEXT("GFFashionQuestBack")))
	{
		return false;
	}
	TestTrue(TEXT("Map trip restores"), UGFFashionQuestTravel::RestoreFrom(Gate, Next.World));
	TestTrue(TEXT("Carry is spent"), Gate->GetQuestCarryState() == EGFFashionCarryState::Empty);
	TestFalse(TEXT("Second restore"), UGFFashionQuestTravel::RestoreFrom(Gate, Next.World));
	TestEqual(TEXT("Seat survived"), Next.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	const FName* Session = Next.GS->SessionFactions.Find(TEXT("B"));
	TestTrue(TEXT("Session survived"), Session && *Session == Verdant);
	const FGFQuestProgress* Progress = Next.Quest->FindProgress(TEXT("B"));
	TestTrue(TEXT("Errand survived"), Progress && Progress->Step == EGFQuestStep::Done && Progress->Rank == EGFFactionRank::Leader);

	AGFPlayerState* Back = Next.Add(TEXT("B"));
	if (!TestNotNull(TEXT("Winner returns"), Back))
	{
		return false;
	}
	Next.GS->NotifyModesPlayerReady(Back);
	TestEqual(TEXT("Returns to Verdant"), Back->FactionId, Verdant);
	TestEqual(TEXT("Seat still the winner"), Next.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	TestTrue(TEXT("Rank still Leader"), Back->FactionRank == EGFFactionRank::Leader);

	AGFPlayerState* Stranger = Next.Add(TEXT("Z"));
	if (!TestNotNull(TEXT("Stranger"), Stranger))
	{
		return false;
	}
	Next.GS->NotifyModesPlayerReady(Stranger);
	TestEqual(TEXT("Empty seat still goes to the first claimer"), Next.GS->FactionLeaderOf(Rime), FString(TEXT("Z")));
	TestEqual(TEXT("Verdant was not given away"), Next.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFFashionQuestLeaderOnlyChampionTest, "GFGame.FashionQuest.Leader.OnlyChampion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFFashionQuestLeaderOnlyChampionTest::RunTest(const FString&)
{
	using namespace GFFashionQuestTest;
	using E = FGFFactionLead::EResult;
	auto Code = [](E Result) { return static_cast<int32>(Result); };
	TestEqual(TEXT("Member"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Member, Verdant, Verdant, TEXT("B"), TEXT("C"))), Code(E::NotChampion));
	TestEqual(TEXT("Outsider"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Outsider, Verdant, Verdant, TEXT("B"), TEXT("C"))), Code(E::NotChampion));
	TestEqual(TEXT("Already Leader rank"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Leader, Verdant, Verdant, TEXT("B"), TEXT("C"))), Code(E::NotChampion));
	TestEqual(TEXT("Other faction"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Champion, Rime, Verdant, TEXT("B"), TEXT("C"))), Code(E::WrongFaction));
	TestEqual(TEXT("No faction"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Champion, NAME_None, Verdant, TEXT("B"), TEXT("C"))), Code(E::WrongFaction));
	TestEqual(TEXT("Missing player"), Code(FGFFactionLead::Decide(false, EGFFactionRank::Champion, Verdant, Verdant, TEXT("B"), TEXT("C"))), Code(E::Missing));
	TestEqual(TEXT("Empty key"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Champion, Verdant, Verdant, FString(), TEXT("C"))), Code(E::Missing));
	TestEqual(TEXT("No target"), Code(FGFFactionLead::Decide(true, EGFFactionRank::Champion, Verdant, NAME_None, TEXT("B"), TEXT("C"))), Code(E::Missing));

	FWorld World;
	if (!World.Init(*this, TEXT("GFFashionQuestChamp")))
	{
		return false;
	}
	AGFPlayerState* Founder = World.Add(TEXT("C"));
	AGFPlayerState* Member = World.Add(TEXT("B"));
	AGFPlayerState* Other = World.Add(TEXT("D"));
	if (!TestNotNull(TEXT("Founder"), Founder) || !TestNotNull(TEXT("Member"), Member) || !TestNotNull(TEXT("Other"), Other))
	{
		return false;
	}
	TestEqual(TEXT("Founder claims Verdant"), World.GS->ServerClaimFaction(Founder, Verdant), FName(TEXT("Ok")));
	TestEqual(TEXT("Member joins Verdant"), World.GS->ServerClaimFaction(Member, Verdant), FName(TEXT("Ok")));
	TestEqual(TEXT("Other claims Rime"), World.GS->ServerClaimFaction(Other, Rime), FName(TEXT("Ok")));
	Member->FactionRank = EGFFactionRank::Member;
	Other->FactionRank = EGFFactionRank::Champion;

	TestEqual(TEXT("Member refused"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("NotChampion")));
	TestEqual(TEXT("Seat stays"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	Member->FactionRank = EGFFactionRank::Outsider;
	TestEqual(TEXT("Outsider refused"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("NotChampion")));
	Member->FactionRank = EGFFactionRank::Leader;
	TestEqual(TEXT("Leader rank refused"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("NotChampion")));
	TestEqual(TEXT("Other faction refused"), World.GS->ServerSetFactionLeader(Verdant, TEXT("D"), AGFGameState::LeaderReasonShowWin), FName(TEXT("WrongFaction")));
	TestEqual(TEXT("Rime stays with its first claimer"), World.GS->FactionLeaderOf(Rime), FString(TEXT("D")));
	TestEqual(TEXT("Missing key"), World.GS->ServerSetFactionLeader(Verdant, FString(), AGFGameState::LeaderReasonShowWin), FName(TEXT("Missing")));
	TestEqual(TEXT("Still the founder"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestEqual(TEXT("No post for a refusal"), ShowWins(World.World, TEXT("B")), 0);

	FGFQuestSave Save;
	FGFQuestRecord Record;
	Record.PlayerKey = TEXT("B");
	Record.Progress.Step = EGFQuestStep::WinShow;
	Record.Progress.Count = 1;
	Record.Progress.FactionId = Verdant;
	Record.Progress.Rank = EGFFactionRank::Champion;
	Save.Records.Add(Record);
	TArray<uint8> Bytes;
	FGFModeBytes::Write(Save, Bytes);
	World.Quest->RestoreModeState(Bytes);
	Member->FactionRank = EGFFactionRank::Member;
	const FGFQuestApplyResult Won = World.Quest->ApplyEvent(FGFQuestEvents::ShowPlaced(TEXT("B"), 1, FGFQuestShowRule::WinTier));
	TestTrue(TEXT("The errand still records the win"), Won.bBecameLeader && Won.Result == EGFQuestResult::Ok);
	TestEqual(TEXT("A member does not take the seat"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("C")));
	TestEqual(TEXT("Still no post"), ShowWins(World.World, TEXT("B")), 0);

	Member->FactionRank = EGFFactionRank::Champion;
	TestEqual(TEXT("Champion takes it"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("Ok")));
	TestEqual(TEXT("Verdant moved"), World.GS->FactionLeaderOf(Verdant), FString(TEXT("B")));
	TestEqual(TEXT("Rime did not move"), World.GS->FactionLeaderOf(Rime), FString(TEXT("D")));
	TestEqual(TEXT("Show post"), ShowWins(World.World, TEXT("B")), 1);
	TestEqual(TEXT("Same seat again"), World.GS->ServerSetFactionLeader(Verdant, TEXT("B"), AGFGameState::LeaderReasonShowWin), FName(TEXT("Same")));
	TestEqual(TEXT("Still one post"), ShowWins(World.World, TEXT("B")), 1);
	return true;
}

#endif
