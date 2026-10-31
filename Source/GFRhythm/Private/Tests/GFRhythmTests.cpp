// Beat Hop (PLAN.md M5) unit tests: beatmap format, built-in charts, judging, lights, rewards, co-op rules, course.
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFBeatmap.h"
#include "GFBuiltInSongs.h"
#include "GFRhythmJudge.h"
#include "GFBeatHopCourse.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFDataTables.h"
#include "GFItemTypes.h"
#include "GFEconomy.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFRhythmTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFBeatmap SmallMap()
	{
		FGFBeatmap M;
		M.SongId = TEXT("TestSong");
		M.Title = TEXT("Test Song");
		M.Difficulty = EGFBeatDifficulty::Normal;
		M.Bpm = 120.f;
		M.OffsetMs = 0.f;
		M.Lanes = 4;
		M.RefSpeedCms = 300.f;
		auto Add = [&M](double Beat, int32 Lane, EGFBeatNoteType Type, double Len = 0.0)
		{
			FGFBeatNote N;
			N.Beat = Beat;
			N.Lane = Lane;
			N.Type = Type;
			N.LenBeats = Len;
			M.Notes.Add(N);
		};
		Add(4, 1, EGFBeatNoteType::Tap);
		Add(5, 2, EGFBeatNoteType::Tap);
		Add(6, 2, EGFBeatNoteType::Hold, 1.0);
		Add(8, 1, EGFBeatNoteType::Gap);
		Add(9, 1, EGFBeatNoteType::Pad);
		Add(10, 3, EGFBeatNoteType::Switch);
		return M;
	}

	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFBeatHopTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmBeatmapTest, "GFGame.Rhythm.Beatmap", GFRhythmTests::Flags)
bool FGFRhythmBeatmapTest::RunTest(const FString&)
{
	const FGFBeatmap M = GFRhythmTests::SmallMap();
	TestEqual(TEXT("Beat to seconds"), M.BeatToSeconds(4.0), 2.0, 1e-9);
	TestEqual(TEXT("Seconds to beat"), M.SecondsToBeat(2.5), 5.0, 1e-9);
	TestEqual(TEXT("Beat to distance"), M.BeatToDistanceCm(2.0), 300.0, 1e-6);
	TestEqual(TEXT("Last beat includes hold"), M.GetLastBeat(), 10.0, 1e-9);
	TestEqual(TEXT("Gaps are not judged"), M.CountJudgedNotes(), 5);

	const FString Json = FGFBeatmapIO::ToJson(M);
	TestTrue(TEXT("JSON uses the documented keys"), Json.Contains(TEXT("\"offset_ms\"")) && Json.Contains(TEXT("\"len_beats\"")) && Json.Contains(TEXT("\"ref_speed_cms\"")));
	FGFBeatmap Back;
	FString Error;
	TestTrue(TEXT("Round trip parses"), FGFBeatmapIO::FromJson(Json, Back, Error));
	TestEqual(TEXT("Round trip notes"), Back.Notes.Num(), M.Notes.Num());
	TestEqual(TEXT("Round trip bpm"), Back.Bpm, M.Bpm);
	TestEqual(TEXT("Round trip lanes"), Back.Lanes, M.Lanes);
	TestTrue(TEXT("Round trip hold"), Back.Notes[2].Type == EGFBeatNoteType::Hold && FMath::IsNearlyEqual(Back.Notes[2].LenBeats, 1.0));
	TestTrue(TEXT("Round trip pad"), Back.Notes[4].Type == EGFBeatNoteType::Pad);

	const FString Spec = TEXT("{\"song\":\"Tiny\",\"difficulty\":\"Easy\",\"bpm\":100,\"offset_ms\":20,\"lanes\":4,\"ref_speed_cms\":500,")
		TEXT("\"notes\":[{\"beat\":0,\"lane\":0,\"type\":\"Tap\"},{\"beat\":1,\"lane\":1,\"type\":\"Hold\",\"len_beats\":2}]}");
	FGFBeatmap Tiny;
	TestTrue(TEXT("Spec example parses"), FGFBeatmapIO::FromJson(Spec, Tiny, Error));
	TestTrue(TEXT("Spec difficulty"), Tiny.Difficulty == EGFBeatDifficulty::Easy);
	TestEqual(TEXT("Spec offset"), Tiny.OffsetMs, 20.f);

	FGFBeatmap Bad;
	TestFalse(TEXT("Garbage is rejected"), FGFBeatmapIO::FromJson(TEXT("{not json"), Bad, Error));
	TestFalse(TEXT("Unknown note type is rejected"), FGFBeatmapIO::FromJson(
		TEXT("{\"song\":\"X\",\"difficulty\":\"Easy\",\"bpm\":100,\"offset_ms\":0,\"lanes\":4,\"ref_speed_cms\":500,\"notes\":[{\"beat\":0,\"lane\":0,\"type\":\"Wiggle\"}]}"), Bad, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmChartValidateTest, "GFGame.Rhythm.ChartValidate", GFRhythmTests::Flags)
bool FGFRhythmChartValidateTest::RunTest(const FString&)
{
	TestTrue(TEXT("Small map is valid"), FGFBeatmapIO::Validate(GFRhythmTests::SmallMap()).Num() == 0);

	FGFBeatmap M = GFRhythmTests::SmallMap();
	M.Notes[1].Lane = 7;
	TestTrue(TEXT("Lane out of range"), FGFBeatmapIO::Validate(M).Num() > 0);

	M = GFRhythmTests::SmallMap();
	Swap(M.Notes[0], M.Notes[1]);
	TestTrue(TEXT("Unsorted notes"), FGFBeatmapIO::Validate(M).Num() > 0);

	M = GFRhythmTests::SmallMap();
	M.Notes[1].Beat = 20; // a 16-beat jump at 300 cm/beat is impossible
	M.Notes.Sort([](const FGFBeatNote& A, const FGFBeatNote& B) { return A.Beat < B.Beat; });
	TestTrue(TEXT("Impossible jump"), FGFBeatmapIO::Validate(M).Num() > 0);

	M = GFRhythmTests::SmallMap();
	M.Bpm = 10.f;
	TestTrue(TEXT("Silly BPM"), FGFBeatmapIO::Validate(M).Num() > 0);

	// Every built-in chart: present for each difficulty and passes validation (jumps reachable, lanes in range).
	const TArray<FName> Songs = FGFBuiltInSongs::SongIds();
	TestTrue(TEXT("At least three built-in songs"), Songs.Num() >= 3);
	TestEqual(TEXT("Every embedded chart parsed"), FGFBuiltInSongs::All().Num(), FGFBuiltInSongs::NumEmbeddedCharts());
	for (const FName& Song : Songs)
	{
		for (EGFBeatDifficulty D : { EGFBeatDifficulty::Easy, EGFBeatDifficulty::Normal, EGFBeatDifficulty::Hard })
		{
			const FGFBeatmap* Chart = FGFBuiltInSongs::Find(Song, D);
			const FString What = FString::Printf(TEXT("%s %s"), *Song.ToString(), FGFBeatmapIO::DifficultyToString(D));
			if (!TestNotNull(*(What + TEXT(" exists")), Chart))
			{
				continue;
			}
			const TArray<FString> Problems = FGFBeatmapIO::Validate(*Chart);
			for (const FString& P : Problems)
			{
				AddError(What + TEXT(": ") + P);
			}
			TestTrue(*(What + TEXT(" has notes")), Chart->CountJudgedNotes() >= 16);
			TestEqual(*(What + TEXT(" lanes")), Chart->Lanes, D == EGFBeatDifficulty::Hard ? 5 : 4);
		}
		const FGFBeatmap* Easy = FGFBuiltInSongs::Find(Song, EGFBeatDifficulty::Easy);
		const FGFBeatmap* Hard = FGFBuiltInSongs::Find(Song, EGFBeatDifficulty::Hard);
		if (Easy && Hard)
		{
			TestTrue(*(Song.ToString() + TEXT(": hard is denser than easy")), Hard->CountJudgedNotes() > Easy->CountJudgedNotes());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmJudgeTest, "GFGame.Rhythm.Judge", GFRhythmTests::Flags)
bool FGFRhythmJudgeTest::RunTest(const FString&)
{
	const FGFJudgeWindows Songs = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
	const FGFJudgeWindows SongsEasy = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Easy);
	const FGFJudgeWindows Live = FGFJudgeWindows::For(EGFBeatHopMode::Live, EGFBeatDifficulty::Normal);
	const FGFJudgeWindows LiveEasy = FGFJudgeWindows::For(EGFBeatHopMode::Live, EGFBeatDifficulty::Easy);
	TestEqual(TEXT("Songs perfect"), Songs.PerfectMs, 50.f);
	TestEqual(TEXT("Songs good"), Songs.GoodMs, 110.f);
	TestEqual(TEXT("Songs easy"), SongsEasy.GoodMs, 150.f);
	TestEqual(TEXT("Live perfect"), Live.PerfectMs, 80.f);
	TestEqual(TEXT("Live good"), Live.GoodMs, 160.f);
	TestEqual(TEXT("Live easy"), LiveEasy.GoodMs, 200.f);

	TestTrue(TEXT("Dead on"), FGFRhythmJudge::Classify(0.0, Songs) == EGFJudgement::Perfect);
	TestTrue(TEXT("Perfect edge (inclusive)"), FGFRhythmJudge::Classify(-0.050, Songs) == EGFJudgement::Perfect);
	TestTrue(TEXT("Just past perfect"), FGFRhythmJudge::Classify(0.051, Songs) == EGFJudgement::Good);
	TestTrue(TEXT("Good edge"), FGFRhythmJudge::Classify(0.110, Songs) == EGFJudgement::Good);
	TestTrue(TEXT("Late miss"), FGFRhythmJudge::Classify(0.111, Songs) == EGFJudgement::Miss);
	TestTrue(TEXT("Early miss"), FGFRhythmJudge::Classify(-0.2, Songs) == EGFJudgement::Miss);
	TestTrue(TEXT("Live is looser"), FGFRhythmJudge::Classify(0.075, Live) == EGFJudgement::Perfect);

	TestEqual(TEXT("x1 at start"), FGFRhythmJudge::MultiplierForCombo(0), 1);
	TestEqual(TEXT("x2 at 10"), FGFRhythmJudge::MultiplierForCombo(10), 2);
	TestEqual(TEXT("x4 cap"), FGFRhythmJudge::MultiplierForCombo(500), 4);

	FGFRhythmJudge J(Songs);
	for (int32 i = 0; i < 10; ++i)
	{
		J.JudgeLanding(1.0, 0, 1.0, 0);
	}
	TestEqual(TEXT("Ten perfects at x1"), J.GetScore().Score, 3000);
	TestEqual(TEXT("Combo 10"), J.GetScore().Combo, 10);
	J.JudgeLanding(1.08, 2, 1.0, 2); // good at x2
	TestEqual(TEXT("Good at x2"), J.GetScore().Score, 3200);
	TestTrue(TEXT("Wrong lane is a miss"), J.JudgeLanding(1.0, 1, 1.0, 2) == EGFJudgement::Miss);
	TestEqual(TEXT("Miss breaks combo"), J.GetScore().Combo, 0);
	TestEqual(TEXT("Best combo kept"), J.GetScore().BestCombo, 11);
	J.AddMiss();
	TestEqual(TEXT("Misses counted"), J.GetScore().Misses, 2);

	FGFRhythmScore S;
	S.Perfects = 20;
	TestTrue(TEXT("All perfect is S"), FGFRhythmJudge::GradeFor(S) == EGFBeatHopGrade::S);
	S.Perfects = 0;
	S.Goods = 20;
	TestTrue(TEXT("All good is B"), FGFRhythmJudge::GradeFor(S) == EGFBeatHopGrade::B);
	S.Goods = 0;
	S.Misses = 20;
	TestTrue(TEXT("All miss is C"), FGFRhythmJudge::GradeFor(S) == EGFBeatHopGrade::C);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmLightsTest, "GFGame.Rhythm.Lights", GFRhythmTests::Flags)
bool FGFRhythmLightsTest::RunTest(const FString&)
{
	const double W = 0.2;
	TestTrue(TEXT("Far ahead is off"), FGFBrickLighting::LightAt(0.0, 10.0, 0.0, W, false, false) == EGFBrickLight::Off);
	TestTrue(TEXT("Two beats out is dim"), FGFBrickLighting::LightAt(8.5, 10.0, 0.0, W, false, false) == EGFBrickLight::Dim);
	TestTrue(TEXT("On the beat is lit"), FGFBrickLighting::LightAt(10.0, 10.0, 0.0, W, false, false) == EGFBrickLight::Lit);
	TestTrue(TEXT("Inside window is lit"), FGFBrickLighting::LightAt(10.15, 10.0, 0.0, W, false, false) == EGFBrickLight::Lit);
	TestTrue(TEXT("Hold stays lit"), FGFBrickLighting::LightAt(11.5, 10.0, 2.0, W, false, false) == EGFBrickLight::Lit);
	TestTrue(TEXT("Hit shows hit"), FGFBrickLighting::LightAt(10.0, 10.0, 0.0, W, true, false) == EGFBrickLight::Hit);
	TestTrue(TEXT("Missed shows missed"), FGFBrickLighting::LightAt(11.0, 10.0, 0.0, W, false, true) == EGFBrickLight::Missed);
	TestTrue(TEXT("Landable on the beat"), FGFBrickLighting::IsLandable(10.1, 10.0, 0.0, W));
	TestFalse(TEXT("Not landable well before"), FGFBrickLighting::IsLandable(9.0, 10.0, 0.0, W));
	const float Before = FGFBrickLighting::GlowAt(8.5, 10.0, 0.0, W, false, false);
	const float Near = FGFBrickLighting::GlowAt(9.5, 10.0, 0.0, W, false, false);
	const float On = FGFBrickLighting::GlowAt(10.0, 10.0, 0.0, W, false, false);
	TestTrue(TEXT("Glow ramps up toward the beat"), Before < Near && Near < On);
	TestEqual(TEXT("Full glow on the beat"), On, 1.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmRewardsTest, "GFGame.Rhythm.Rewards", GFRhythmTests::Flags)
bool FGFRhythmRewardsTest::RunTest(const FString&)
{
	const FGFBeatHopRewardRules Rules;
	const TArray<FGFItemStack> Perfect = FGFBeatHopRewards::ForLanding(EGFJudgement::Perfect, Rules, false);
	TestTrue(TEXT("Perfect landing pays 2 GlowDust"), Perfect.Num() == 1 && Perfect[0].ItemId == FName(TEXT("GlowDust")) && Perfect[0].Count == 2);
	const TArray<FGFItemStack> Good = FGFBeatHopRewards::ForLanding(EGFJudgement::Good, Rules, false);
	TestTrue(TEXT("Good landing pays 1"), Good.Num() == 1 && Good[0].Count == 1);
	TestEqual(TEXT("Miss pays nothing"), FGFBeatHopRewards::ForLanding(EGFJudgement::Miss, Rules, false).Num(), 0);
	const TArray<FGFItemStack> Cozy = FGFBeatHopRewards::ForLanding(EGFJudgement::Good, Rules, true);
	TestTrue(TEXT("Cozy keeps a floor of 1"), Cozy.Num() == 1 && Cozy[0].Count == 1);

	FGFRhythmScore Full;
	Full.Perfects = 30;
	Full.BestCombo = 30;
	Full.Combo = 30;
	const TArray<FGFItemStack> End = FGFBeatHopRewards::ForSongEnd(Full, 30, Rules, false);
	auto CountOf = [](const TArray<FGFItemStack>& Stacks, const TCHAR* Id)
	{
		int32 N = 0;
		for (const FGFItemStack& S : Stacks) { if (S.ItemId == FName(Id)) { N += S.Count; } }
		return N;
	};
	TestEqual(TEXT("S grade pays 6 PrismShard"), CountOf(End, TEXT("PrismShard")), 6);
	TestEqual(TEXT("Combo bonus 3 GlowDust"), CountOf(End, TEXT("GlowDust")), 3);
	TestEqual(TEXT("Full combo pays the rare"), CountOf(End, TEXT("GoldNote")), 1);
	const TArray<FGFItemStack> CozyEnd = FGFBeatHopRewards::ForSongEnd(Full, 30, Rules, true);
	TestEqual(TEXT("No rare in Cozy"), CountOf(CozyEnd, TEXT("GoldNote")), 0);
	TestEqual(TEXT("Cozy halves the grade reward"), CountOf(CozyEnd, TEXT("PrismShard")), 3);
	TestEqual(TEXT("Nothing played, nothing paid"), FGFBeatHopRewards::ForSongEnd(FGFRhythmScore(), 30, Rules, false).Num(), 0);

	// Materials are the only currency: every id the rules can pay is a tiered material in DT_Items.
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (TestNotNull(TEXT("Item table"), Items))
	{
		TArray<FGFItemStack> All = End;
		All.Append(Perfect);
		FString BadId;
		TestTrue(TEXT("Rewards are materials only"), FGFBeatHopRewards::AreMaterialsOnly(All, Items, &BadId));
		TestFalse(TEXT("A non-material is caught"), FGFBeatHopRewards::AreMaterialsOnly({ FGFItemStack(TEXT("NotARealItem"), 1) }, Items, &BadId));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmCoopRulesTest, "GFGame.Rhythm.CoopRules", GFRhythmTests::Flags)
bool FGFRhythmCoopRulesTest::RunTest(const FString&)
{
	FString Why;
	TestTrue(TEXT("Solo songs"), FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Songs, 1, &Why));
	TestTrue(TEXT("Solo live"), FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Live, 1, &Why));
	TestTrue(TEXT("Co-op songs"), FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Songs, 4, &Why));
	TestFalse(TEXT("Co-op live refused"), FGFBeatHopSessionRules::IsModeAllowed(EGFBeatHopMode::Live, 2, &Why));
	TestFalse(TEXT("Refusal explains itself"), Why.IsEmpty());
	const FName Song = FGFBuiltInSongs::SongIds()[0];
	TestTrue(TEXT("Built-in song starts"), FGFBeatHopSessionRules::CanStart(EGFBeatHopMode::Songs, 3, Song, EGFBeatDifficulty::Hard));
	TestFalse(TEXT("Unknown song refused"), FGFBeatHopSessionRules::CanStart(EGFBeatHopMode::Songs, 1, TEXT("NoSuchSong"), EGFBeatDifficulty::Easy));

	const UGFBeatHopGateSubsystem* Gate = GetDefault<UGFBeatHopGateSubsystem>();
	TestTrue(TEXT("Gate id"), Gate->GetGateId() == FName(TEXT("BeatHop")));
	TestEqual(TEXT("Gate songs"), Gate->GetBuiltInSongIds().Num(), FGFBuiltInSongs::SongIds().Num());
	TestFalse(TEXT("Gate refuses co-op live"), Gate->CanStartRun(EGFBeatHopMode::Live, 2, NAME_None, EGFBeatDifficulty::Normal, Why));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmCourseTest, "GFGame.Rhythm.Course", GFRhythmTests::Flags)
bool FGFRhythmCourseTest::RunTest(const FString&)
{
	GFRhythmTests::FTestWorld TW;
	if (!TestNotNull(TEXT("World"), TW.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = TW.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Course spawned"), Course))
	{
		return false;
	}
	// Rewards go through a real ledger. B has no readable account yet, so B's rewards must wait (keyed to B).
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("A"));
	Accounts.Accounts.Add(TEXT("Solo"));
	Course->SetLedgerOverride(&Ledger, &Accounts);
	auto Held = [&Accounts](const TCHAR* Key, const TCHAR* Item)
	{
		const FGFInventory* Inv = Accounts.Accounts.Find(Key);
		return Inv ? Inv->GetCount(Item) : -1;
	};

	FString Why;
	TestFalse(TEXT("Co-op live refused by the course"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 2, Why));
	TestFalse(TEXT("Not running after refusal"), Course->IsRunning());

	const FName Song = TEXT("FernHop");
	const FGFBeatmap* Chart = FGFBuiltInSongs::Find(Song, EGFBeatDifficulty::Normal);
	if (!TestNotNull(TEXT("FernHop Normal"), Chart))
	{
		return false;
	}
	Course->SetDebugRunTime(0.0);
	TestTrue(TEXT("Co-op song run starts"), Course->StartRunForPlayers(Song, EGFBeatDifficulty::Normal, EGFBeatHopMode::Songs, false, 2, Why));
	int32 BrickNotes = 0;
	for (const FGFBeatNote& N : Chart->Notes) { BrickNotes += N.HasBrick() ? 1 : 0; }
	TestEqual(TEXT("One brick per non-gap note"), Course->NumBricks(), BrickNotes);
	if (Course->NumBricks() < 3)
	{
		return false;
	}
	TestTrue(TEXT("Bricks run forward"), Course->GetBrickTop(1).X > Course->GetBrickTop(0).X);

	const FGFBeatHopBrick& B0 = Course->GetBrick(0);
	Course->SetDebugRunTime(B0.Time - 5.0);
	Course->UpdateRun();
	TestTrue(TEXT("Far brick is off"), Course->GetBrickLight(0) == EGFBrickLight::Off);
	Course->SetDebugRunTime(B0.Time);
	Course->UpdateRun();
	TestTrue(TEXT("Brick lit on its beat"), Course->GetBrickLight(0) == EGFBrickLight::Lit);

	// Player A lands dead on; player B lands late (good); a stray early hop on brick 2 isn't judged.
	TestTrue(TEXT("A perfect"), Course->HandleLanding(TEXT("A"), 0, B0.Time) == EGFJudgement::Perfect);
	TestTrue(TEXT("Same brick isn't paid twice"), Course->HandleLanding(TEXT("A"), 0, B0.Time + 0.01) == EGFJudgement::None);
	TestTrue(TEXT("B good"), Course->HandleLanding(TEXT("B"), 0, B0.Time + 0.09) == EGFJudgement::Good);
	TestTrue(TEXT("Stray early hop ignored"), Course->HandleLanding(TEXT("A"), 2, Course->GetBrick(2).Time - 10.0) == EGFJudgement::None);
	TestTrue(TEXT("Brick shows hit"), Course->GetBrickLight(0) == EGFBrickLight::Hit);
	TestEqual(TEXT("A earned 2 GlowDust"), Course->GetEarnedRewards(TEXT("A"))->GetCount(TEXT("GlowDust")), 2);
	TestEqual(TEXT("A's 2 GlowDust are in A's ledger account"), Held(TEXT("A"), TEXT("GlowDust")), 2);
	TestEqual(TEXT("B earned 1 GlowDust"), Course->GetEarnedRewards(TEXT("B"))->GetCount(TEXT("GlowDust")), 1);
	TestEqual(TEXT("B's reward waits (no account yet)"), Course->GetPendingGrants().Num(), 1);
	if (Course->GetPendingGrants().Num() == 1)
	{
		const FGFBeatHopPendingGrant& P = Course->GetPendingGrants()[0];
		TestEqual(TEXT("Pending grant is keyed to B"), P.PlayerKey, FString(TEXT("B")));
		TestEqual(TEXT("Pending grant has the fixed TxId"), P.TxId, AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("B"), TEXT("Landing.0")));
	}
	TestTrue(TEXT("A's landing used the fixed TxId"), Ledger.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("A"), TEXT("Landing.0"))));
	TestEqual(TEXT("Nothing pays while B's account is missing"), Course->RetryPendingGrants(), 0);
	Accounts.Accounts.Add(TEXT("B")); // B's character is in
	TestEqual(TEXT("B's reward goes in on retry"), Course->RetryPendingGrants(), 1);
	TestEqual(TEXT("B holds 1 GlowDust"), Held(TEXT("B"), TEXT("GlowDust")), 1);
	TestEqual(TEXT("Retrying again pays nothing"), Course->RetryPendingGrants(), 0);
	TestTrue(TEXT("Replaying A's landing TxId is a no-op"), Ledger.Mint(Accounts, AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("A"), TEXT("Landing.0")),
		TEXT("A"), { FGFItemStack(TEXT("GlowDust"), 2) }, TEXT("Replay")) == EGFTxResult::AlreadyApplied);
	TestEqual(TEXT("A still holds 2 GlowDust"), Held(TEXT("A"), TEXT("GlowDust")), 2);
	TestEqual(TEXT("Ledger minted 3 GlowDust in total"), Ledger.GetMinted(TEXT("GlowDust")), int64(3));

	// A lands on brick 1 too late: a miss, nothing paid.
	const FGFBeatHopBrick& B1 = Course->GetBrick(1);
	TestTrue(TEXT("Late landing misses"), Course->HandleLanding(TEXT("A"), 1, B1.Time + B1.LenSeconds + 0.3) == EGFJudgement::Miss);
	TestEqual(TEXT("Miss pays nothing"), Held(TEXT("A"), TEXT("GlowDust")), 2);

	// Time passes brick 1 entirely: B never landed there, so it's a miss for B.
	Course->SetDebugRunTime(B1.Time + B1.LenSeconds + 0.115); // just past its good window
	Course->UpdateRun();
	TestEqual(TEXT("Skipped brick is a miss for B"), Course->GetScore(TEXT("B"))->Misses, 1);
	TestEqual(TEXT("A's late landing counted once"), Course->GetScore(TEXT("A"))->Misses, 1);

	// Song end: everything left is missed and the grade reward is paid.
	Course->SetDebugRunTime(1.0e6);
	Course->UpdateRun();
	TestFalse(TEXT("Run ends after the last note"), Course->IsRunning());
	TestEqual(TEXT("All bricks judged for A"), Course->GetScore(TEXT("A"))->Judged(), Course->NumBricks());
	TestTrue(TEXT("Low accuracy is grade C"), FGFRhythmJudge::GradeFor(*Course->GetScore(TEXT("A"))) == EGFBeatHopGrade::C);
	TestEqual(TEXT("C grade pays 1 PrismShard"), Held(TEXT("A"), TEXT("PrismShard")), 1);
	TestEqual(TEXT("No rare without a full combo"), Held(TEXT("A"), TEXT("GoldNote")), 0);
	TestTrue(TEXT("Song end used the fixed TxId"), Ledger.WasApplied(AGFBeatHopCourse::MakeGrantTxId(Course->GetRunId(), TEXT("A"), TEXT("SongEnd"))));
	TestEqual(TEXT("Nothing left pending"), Course->GetPendingGrants().Num(), 0);
	const FGuid FirstRun = Course->GetRunId();

	// A clean run on a fresh start: perfect every brick, S grade and the full-combo rare.
	Course->SetDebugRunTime(0.0);
	TestTrue(TEXT("Solo restart"), Course->StartRunForPlayers(Song, EGFBeatDifficulty::Normal, EGFBeatHopMode::Songs, false, 1, Why));
	for (int32 i = 0; i < Course->NumBricks(); ++i)
	{
		const double T = Course->GetBrick(i).Time;
		Course->SetDebugRunTime(T);
		Course->UpdateRun();
		TestTrue(TEXT("Perfect"), Course->HandleLanding(TEXT("Solo"), i, T) == EGFJudgement::Perfect);
	}
	Course->SetDebugRunTime(1.0e6);
	Course->UpdateRun();
	const FGFRhythmScore* Clean = Course->GetScore(TEXT("Solo"));
	TestTrue(TEXT("Clean run is S"), Clean && FGFRhythmJudge::GradeFor(*Clean) == EGFBeatHopGrade::S);
	TestTrue(TEXT("A new run gets a new run id"), Course->GetRunId() != FirstRun);
	TestEqual(TEXT("Full combo rare"), Held(TEXT("Solo"), TEXT("GoldNote")), 1);
	TestEqual(TEXT("Landing dust + combo bonus"), Held(TEXT("Solo"), TEXT("GlowDust")), Course->NumBricks() * 2 + Course->NumBricks() / 10);
	TestEqual(TEXT("Earned matches what the ledger paid"), Course->GetEarnedRewards(TEXT("Solo"))->GetCount(TEXT("GlowDust")), Held(TEXT("Solo"), TEXT("GlowDust")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmCourseArtTest, "GFGame.Rhythm.CourseArt", GFRhythmTests::Flags)
bool FGFRhythmCourseArtTest::RunTest(const FString&)
{
	// Pure helpers first: art paths, beat pulse, judgement sheet rects.
	TestEqual(TEXT("Tap lane 0 is L1"), AGFBeatHopCourse::BrickMeshPath(EGFBeatNoteType::Tap, 0), FString(TEXT("/Game/Art/BeatHop/Bricks/SM_Block_Beat_L1.SM_Block_Beat_L1")));
	TestEqual(TEXT("Tap lane 6 wraps to L2"), AGFBeatHopCourse::BrickMeshPath(EGFBeatNoteType::Tap, 6), FString(TEXT("/Game/Art/BeatHop/Bricks/SM_Block_Beat_L2.SM_Block_Beat_L2")));
	TestTrue(TEXT("Gap has no mesh"), AGFBeatHopCourse::BrickMeshPath(EGFBeatNoteType::Gap, 0).IsEmpty());
	TestEqual(TEXT("Pulse peaks at 4 on the beat"), AGFBeatHopCourse::BeatPulseStrength(3.0), 4.f, 1e-4f);
	TestTrue(TEXT("Pulse eases toward 1"), AGFBeatHopCourse::BeatPulseStrength(3.9) < 1.05f && AGFBeatHopCourse::BeatPulseStrength(3.9) >= 1.f);
	FIntRect R;
	TestTrue(TEXT("PERFECT rect"), FGFBeatHopVfx::WordRect(EGFJudgement::Perfect, R) && R == FIntRect(1, 2, 42, 9));
	TestTrue(TEXT("MISS rect"), FGFBeatHopVfx::WordRect(EGFJudgement::Miss, R) && R == FIntRect(34, 12, 57, 19));
	TestFalse(TEXT("No word for None"), FGFBeatHopVfx::WordRect(EGFJudgement::None, R));
	TestTrue(TEXT("digit 7"), FGFBeatHopVfx::DigitRect(7) == FIntRect(25, 49, 30, 56));
	TestTrue(TEXT("digit 0"), FGFBeatHopVfx::DigitRect(0) == FIntRect(1, 34, 6, 41));
	TestEqual(TEXT("Sparkle frame 2 at 0.2 s"), FGFBeatHopVfx::SparkleFrame(0.2), 2);
	TestEqual(TEXT("Sparkle done after 4 frames"), FGFBeatHopVfx::SparkleFrame(0.34), (int32)INDEX_NONE);

	GFRhythmTests::FTestWorld TW;
	if (!TestNotNull(TEXT("World"), TW.World))
	{
		return false;
	}
	AGFBeatHopCourse* Course = TW.World->SpawnActor<AGFBeatHopCourse>();
	if (!TestNotNull(TEXT("Course spawned"), Course))
	{
		return false;
	}
	FGFTransferLedger Ledger;
	FGFMapLedgerAccounts Accounts;
	Accounts.Accounts.Add(TEXT("Solo"));
	Course->SetLedgerOverride(&Ledger, &Accounts);
	Course->SetDebugRunTime(0.0);
	FString Why;
	const FName Song = TEXT("FernHop");
	if (!TestTrue(TEXT("Song run starts"), Course->StartRunForPlayers(Song, EGFBeatDifficulty::Hard, EGFBeatHopMode::Songs, false, 1, Why)) || Course->NumBricks() < 4)
	{
		return false;
	}

	// Bricks: art meshes by type and lane, top at the course's Z, Hold stretched over 300 cm, Switch mirrored to go down.
	int32 Holds = 0, SwitchUp = 0, SwitchDown = 0, Pads = 0;
	int32 PrevLane = INDEX_NONE;
	for (int32 i = 0; i < Course->NumBricks(); ++i)
	{
		const FGFBeatHopBrick& B = Course->GetBrick(i);
		if (!TestNotNull(TEXT("brick mesh"), B.Mesh.Get()))
		{
			return false;
		}
		const UStaticMesh* SM = B.Mesh->GetStaticMesh();
		TestTrue(FString::Printf(TEXT("brick %d uses %s"), i, *AGFBeatHopCourse::BrickMeshPath(B.Type, B.Lane)), SM && SM->GetPathName() == AGFBeatHopCourse::BrickMeshPath(B.Type, B.Lane));
		TestEqual(FString::Printf(TEXT("brick %d top at Z 0"), i), Course->GetBrickTop(i).Z, 0.0, 0.01);
		TestTrue(FString::Printf(TEXT("brick %d MID is on its own MI"), i), B.Material && B.Material->Parent && B.Material->Parent->GetName().StartsWith(TEXT("MI_Block_Beat_")));
		const FVector S = B.Mesh->GetRelativeScale3D();
		TestEqual(FString::Printf(TEXT("brick %d height"), i), S.Z, Course->BrickSizeCm.Z / 31.25, 1e-4);
		if (B.Type == EGFBeatNoteType::Hold)
		{
			++Holds;
			const double Len = Course->BrickSizeCm.X + B.LenSeconds * 600.0;
			TestEqual(TEXT("Hold scale X = length / 300"), S.X, Len / 300.0, 1e-4);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("brick %d scale X"), i), S.X, Course->BrickSizeCm.X / 100.0, 1e-4);
		}
		if (B.Type == EGFBeatNoteType::Pad)
		{
			++Pads;
			TestEqual(TEXT("Pad keeps its 1.3 width"), S.Y, Course->BrickSizeCm.Y * 1.3 / 100.0, 1e-4);
		}
		if (B.Type == EGFBeatNoteType::Switch && PrevLane != INDEX_NONE)
		{
			const bool bDown = B.Lane < PrevLane;
			(bDown ? SwitchDown : SwitchUp)++;
			TestTrue(FString::Printf(TEXT("switch %d arrow points to lane %d"), i, B.Lane), bDown ? S.Y < 0.0 : S.Y > 0.0);
		}
		PrevLane = B.Lane;
	}
	TestTrue(TEXT("FernHop Hard has holds and both switch directions"), Holds > 0 && SwitchUp > 0 && SwitchDown > 0);

	// Track kit around the song.
	const FVector FirstTop = Course->GetBrickTop(0);
	TestNotNull(TEXT("Start gate"), Course->GetStartGate());
	TestNotNull(TEXT("Finish gate"), Course->GetFinishGate());
	if (Course->GetStartGate() && Course->GetFinishGate())
	{
		TestTrue(TEXT("Start gate before the first brick"), Course->GetStartGate()->GetComponentLocation().X < FirstTop.X);
		TestTrue(TEXT("Finish gate after the last brick"), Course->GetFinishGate()->GetComponentLocation().X > Course->GetBrickTop(Course->NumBricks() - 1).X);
	}
	TestTrue(TEXT("Checkpoints every 32 beats"), Course->NumCheckpoints() >= 1);
	UInstancedStaticMeshComponent* Tiles = Course->GetCatchTiles();
	if (TestNotNull(TEXT("Catch floor"), Tiles))
	{
		TestTrue(TEXT("Catch floor has tiles"), Tiles->GetInstanceCount() > 4);
		TestTrue(TEXT("Catch floor blocks"), Tiles->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	}
	if (UBoxComponent* Box = Course->GetCheckpointTrigger(0))
	{
		TestTrue(TEXT("Checkpoint trigger overlaps"), Box->GetGenerateOverlapEvents() && Box->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	}
	TestTrue(TEXT("Respawn without a checkpoint is behind the start gate"), Course->GetRespawnPoint(nullptr).X < FirstTop.X);

	// A Perfect landing pops PERFECT and bursts; a brick everyone missed crumbles into chunks.
	const int32 Hit = 1;
	const double HitTime = Course->GetBrick(Hit).Time;
	Course->SetDebugRunTime(HitTime);
	TestTrue(TEXT("Perfect landing"), Course->HandleLanding(TEXT("Solo"), Hit, HitTime) == EGFJudgement::Perfect);
	TestTrue(TEXT("Pop recorded"), Course->GetPops().Num() > 0 && Course->GetPops().Last().Judgement == EGFJudgement::Perfect && Course->GetPops().Last().PlayerKey == TEXT("Solo"));
	TestTrue(TEXT("Perfect burst"), Course->NumDebris() >= 6);
	const FGFBeatHopBrick& B0 = Course->GetBrick(0);
	Course->SetDebugRunTime(FMath::Max(HitTime, B0.Time + B0.LenSeconds + 1.0));
	const int32 Before = Course->NumDebris();
	Course->Tick(0.016f);
	TestTrue(TEXT("Missed brick hidden"), !Course->GetBrick(0).Mesh->IsVisible());
	TestTrue(TEXT("Missed brick has no collision"), Course->GetBrick(0).Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Crumble chunks fall"), Course->NumDebris() >= Before + 3);
	TestTrue(TEXT("Hit brick stays"), Course->GetBrick(Hit).Mesh->IsVisible());
	for (int32 i = 0; i < 200; ++i)
	{
		Course->Tick(0.016f);
	}
	TestEqual(TEXT("Debris cleans up"), Course->NumDebris(), 0);
	return true;
}

#endif
