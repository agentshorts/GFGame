// Acceleration Hop (issue 334) tests: judging a landing on the beat, the boost, the tile and the road actor.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.AccelHop;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GFAccelHop.h"
#include "GFAccelHopRoad.h"
#include "GFAccelHopTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAccelHopTestsPrivate
{
	static constexpr EAutomationTestFlags AccelHopFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A throwaway game world for the actor tests. */
	struct FAccelHopTestWorld
	{
		UWorld* World = nullptr;

		explicit FAccelHopTestWorld(const TCHAR* Name)
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

		~FAccelHopTestWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static FGFAccelHopHit AccelHopHitAt(double NowBeat, float Bpm = 120.f)
	{
		const FGFAccelHopTuning Tuning;
		return FGFAccelHopRules::JudgeLanding(NowBeat, Bpm, FGFAccelHopRules::WindowsFor(EGFBeatHopMode::Songs, Tuning), Tuning.BeatsPerBar);
	}

	static bool AccelHopNear(double A, double B, double Tol = 1.0e-3)
	{
		return FMath::Abs(A - B) <= Tol;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTuningDefaultsTest, "GFGame.AccelHop.TuningDefaults", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopTuningDefaultsTest::RunTest(const FString&)
{
	const FGFAccelHopTuning T;
	TestTrue(TEXT("Perfect beats Good"), T.PerfectBoost > T.GoodBoost);
	TestTrue(TEXT("Good still boosts"), T.GoodBoost > 0.f);
	TestTrue(TEXT("Cap is above one Perfect Boost tile"), T.MaxMultiplier >= 1.f + T.PerfectBoost + T.BoostTileBonus);
	TestTrue(TEXT("Cap stays sane (under 2x run speed)"), T.MaxMultiplier <= 2.f);
	TestEqual(TEXT("Four beats to the bar"), T.BeatsPerBar, 4);
	TestTrue(TEXT("Fade is inside the boost"), T.FadeBeats <= T.BoostBeats);
	TestTrue(TEXT("Normal difficulty windows"), T.Difficulty == EGFBeatDifficulty::Normal);
	const FGFJudgeWindows Songs = FGFAccelHopRules::WindowsFor(EGFBeatHopMode::Songs, T);
	const FGFJudgeWindows BeatHopSongs = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
	TestEqual(TEXT("Same Perfect window as Beat Hop"), Songs.PerfectMs, BeatHopSongs.PerfectMs);
	TestEqual(TEXT("Same Good window as Beat Hop"), Songs.GoodMs, BeatHopSongs.GoodMs);
	const FGFJudgeWindows Live = FGFAccelHopRules::WindowsFor(EGFBeatHopMode::Live, T);
	TestTrue(TEXT("Live windows are looser, like Beat Hop"), Live.GoodMs > Songs.GoodMs);
	TestEqual(TEXT("Outline stencil"), FGFAccelHopRules::OutlineStencil, 3);
	TestEqual(TEXT("Boost mesh path"), FGFAccelHopRules::MeshPathFor(EGFHopTileKind::Boost), FString(TEXT("/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_Boost.SM_Hop_Tile_Boost")));
	TestEqual(TEXT("Landing mesh path"), FGFAccelHopRules::MeshPathFor(EGFHopTileKind::Landing), FString(TEXT("/Game/Art/AccelerationHop/Tiles/SM_Hop_Tile_Landing.SM_Hop_Tile_Landing")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopJudgeTest, "GFGame.AccelHop.Judge", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopJudgeTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	// 120 BPM: one beat is 0.5 s, so 0.1 beat is 50 ms.
	FGFAccelHopHit Hit = AccelHopHitAt(8.0);
	TestTrue(TEXT("On the beat is Perfect"), Hit.Judgement == EGFJudgement::Perfect);
	TestEqual(TEXT("Nearest beat"), Hit.BeatIndex, 8);
	TestTrue(TEXT("Beat 8 is a bar line"), Hit.bOnBarLine);
	TestTrue(TEXT("No error"), AccelHopNear(Hit.ErrorMs, 0.0));

	Hit = AccelHopHitAt(9.1);
	TestTrue(TEXT("50 ms late is still Perfect (inclusive edge)"), Hit.Judgement == EGFJudgement::Perfect);
	TestFalse(TEXT("Beat 9 is not a bar line"), Hit.bOnBarLine);
	TestTrue(TEXT("Late is positive"), AccelHopNear(Hit.ErrorMs, 50.0, 0.01));

	Hit = AccelHopHitAt(11.84);
	TestTrue(TEXT("80 ms early is Good"), Hit.Judgement == EGFJudgement::Good);
	TestEqual(TEXT("Rounds to the coming beat"), Hit.BeatIndex, 12);
	TestTrue(TEXT("Early is negative"), Hit.ErrorMs < 0.f);
	TestTrue(TEXT("Beat 12 is a bar line"), Hit.bOnBarLine);

	Hit = AccelHopHitAt(5.4);
	TestTrue(TEXT("200 ms off is a Miss"), Hit.Judgement == EGFJudgement::Miss);
	TestEqual(TEXT("0.4 of a beat rounds back to beat 5"), Hit.BeatIndex, 5);

	TestTrue(TEXT("No clock yet (negative beat) is None"), AccelHopHitAt(-1.0).Judgement == EGFJudgement::None);
	TestTrue(TEXT("No tempo is None"), AccelHopHitAt(4.0, 0.f).Judgement == EGFJudgement::None);
	TestEqual(TEXT("None keeps no beat"), AccelHopHitAt(-1.0).BeatIndex, -1);

	// Slower song: 60 BPM, 0.1 beat is 100 ms, which is Good, not Perfect.
	TestTrue(TEXT("Windows are in ms, not beats"), AccelHopHitAt(3.1, 60.f).Judgement == EGFJudgement::Good);

	const FGFJudgeWindows W = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
	TestTrue(TEXT("Bar of 3"), FGFAccelHopRules::JudgeLanding(6.0, 120.f, W, 3).bOnBarLine);
	TestTrue(TEXT("A zero bar length is treated as 1"), FGFAccelHopRules::JudgeLanding(7.0, 120.f, W, 0).bOnBarLine);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopGainTest, "GFGame.AccelHop.Gain", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopGainTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	const FGFAccelHopTuning T;
	const FGFAccelHopHit PerfectBar = AccelHopHitAt(4.0);
	const FGFAccelHopHit PerfectOff = AccelHopHitAt(5.0);
	const FGFAccelHopHit Good = AccelHopHitAt(6.16);
	const FGFAccelHopHit Miss = AccelHopHitAt(6.5);
	TestTrue(TEXT("Standard Perfect"), AccelHopNear(FGFAccelHopRules::GainFor(PerfectOff, EGFHopTileKind::Standard, T), T.PerfectBoost));
	TestTrue(TEXT("Standard Good"), AccelHopNear(FGFAccelHopRules::GainFor(Good, EGFHopTileKind::Standard, T), T.GoodBoost));
	TestTrue(TEXT("Downbeat on the bar line adds the bonus"), AccelHopNear(FGFAccelHopRules::GainFor(PerfectBar, EGFHopTileKind::Downbeat, T), T.PerfectBoost + T.DownbeatBonus));
	TestTrue(TEXT("Downbeat off the bar line is a plain hop"), AccelHopNear(FGFAccelHopRules::GainFor(PerfectOff, EGFHopTileKind::Downbeat, T), T.PerfectBoost));
	TestTrue(TEXT("Boost tile adds its bonus on any beat"), AccelHopNear(FGFAccelHopRules::GainFor(Good, EGFHopTileKind::Boost, T), T.GoodBoost + T.BoostTileBonus));
	TestTrue(TEXT("A Miss gains nothing, even on a Boost tile"), AccelHopNear(FGFAccelHopRules::GainFor(Miss, EGFHopTileKind::Boost, T), 0.0));
	TestTrue(TEXT("None gains nothing"), AccelHopNear(FGFAccelHopRules::GainFor(FGFAccelHopHit(), EGFHopTileKind::Boost, T), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopBoostTest, "GFGame.AccelHop.Boost", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopBoostTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	const FGFAccelHopTuning T;
	const float Beat = 0.5f; // 120 BPM
	FGFAccelHopBoost S;
	TestTrue(TEXT("No boost is 1x"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 0.0), 1.0));

	TestTrue(TEXT("Perfect hop boosts"), FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(5.0), EGFHopTileKind::Standard, 10.0, Beat, T));
	TestTrue(TEXT("1 + Perfect"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 10.0), 1.0 + T.PerfectBoost));
	TestEqual(TEXT("Chain 1"), S.Chain, 1);
	TestTrue(TEXT("Lasts BoostBeats beats"), AccelHopNear(S.ExpiresAt, 10.0 + T.BoostBeats * Beat));

	TestFalse(TEXT("Same beat again is ignored"), FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(5.02), EGFHopTileKind::Standard, 10.01, Beat, T));
	TestEqual(TEXT("Chain unchanged"), S.Chain, 1);

	TestTrue(TEXT("Next beat stacks"), FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(6.0), EGFHopTileKind::Standard, 10.5, Beat, T));
	TestTrue(TEXT("Stacked"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 10.5), 1.0 + 2.0 * T.PerfectBoost));
	TestEqual(TEXT("Chain 2"), S.Chain, 2);

	for (int32 i = 0; i < 10; ++i)
	{
		FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(7.0 + i), EGFHopTileKind::Boost, 11.0 + 0.5 * i, Beat, T);
	}
	TestTrue(TEXT("Capped at MaxMultiplier"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 15.5), T.MaxMultiplier));
	TestEqual(TEXT("Chain keeps counting at the cap"), S.Chain, 12);

	TestFalse(TEXT("A Miss does not boost"), FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(17.5), EGFHopTileKind::Standard, 15.6, Beat, T));
	TestEqual(TEXT("A Miss breaks the chain"), S.Chain, 0);
	TestTrue(TEXT("The running boost plays out after a Miss"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 15.6), T.MaxMultiplier));

	// Expiry and fade: last hop at 15.5 s, expires at 15.5 + 4 beats = 17.5 s, fades over the last beat (17.0 to 17.5).
	TestTrue(TEXT("Full until the fade"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 16.99), T.MaxMultiplier));
	TestTrue(TEXT("Halfway through the fade"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 17.25), 1.0 + (T.MaxMultiplier - 1.0) * 0.5, 1.0e-2));
	TestTrue(TEXT("Back to 1x at expiry"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 17.5), 1.0));
	TestTrue(TEXT("And after"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 30.0), 1.0));

	// A faster song (0.25 s beats) makes a shorter boost: synced to the music, not a fixed timer.
	FGFAccelHopBoost Fast;
	FGFAccelHopRules::ApplyHit(Fast, AccelHopHitAt(5.0, 240.f), EGFHopTileKind::Standard, 0.0, 0.25f, T);
	TestTrue(TEXT("Boost length follows the tempo"), AccelHopNear(Fast.ExpiresAt, T.BoostBeats * 0.25));
	FGFAccelHopBoost Wild;
	FGFAccelHopRules::ApplyHit(Wild, AccelHopHitAt(5.0), EGFHopTileKind::Standard, 0.0, 100.f, T);
	TestTrue(TEXT("A silly beat length is clamped to 2 s"), AccelHopNear(Wild.ExpiresAt, T.BoostBeats * 2.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLandingTileTest, "GFGame.AccelHop.LandingTile", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopLandingTileTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	const FGFAccelHopTuning T;
	FGFAccelHopBoost S;
	FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(1.0), EGFHopTileKind::Boost, 0.0, 0.5f, T);
	FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(2.0), EGFHopTileKind::Boost, 0.5, 0.5f, T);
	const float Before = FGFAccelHopRules::MultiplierAt(S, 1.0);
	TestTrue(TEXT("Built up a boost"), Before > 1.5f);
	TestTrue(TEXT("Landing tile in time refreshes"), FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(3.0), EGFHopTileKind::Landing, 1.0, 0.5f, T));
	TestTrue(TEXT("Landing holds the speed, no stacking"), AccelHopNear(FGFAccelHopRules::MultiplierAt(S, 1.0), Before));
	TestEqual(TEXT("Landing closes the chain"), S.Chain, 0);
	TestTrue(TEXT("Landing restarts the timer"), AccelHopNear(S.ExpiresAt, 1.0 + T.BoostBeats * 0.5));

	FGFAccelHopBoost Fresh;
	FGFAccelHopRules::ApplyHit(Fresh, AccelHopHitAt(1.0), EGFHopTileKind::Landing, 0.0, 0.5f, T);
	TestTrue(TEXT("Landing from a standstill still gives a small boost"), AccelHopNear(FGFAccelHopRules::MultiplierAt(Fresh, 0.0), 1.0 + T.PerfectBoost));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopPulseTest, "GFGame.AccelHop.Pulse", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopPulseTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	TestTrue(TEXT("Full on the beat"), AccelHopNear(FGFAccelHopRules::BeatPulse(4.0), 1.0));
	TestTrue(TEXT("Half a beat later it has mostly faded"), FGFAccelHopRules::BeatPulse(4.5) < 0.2f);
	TestTrue(TEXT("Falls through the beat"), FGFAccelHopRules::BeatPulse(4.1) > FGFAccelHopRules::BeatPulse(4.3));
	TestTrue(TEXT("Dark just before the next beat"), FGFAccelHopRules::BeatPulse(4.99) < 0.01f);
	TestTrue(TEXT("No clock, no pulse"), AccelHopNear(FGFAccelHopRules::BeatPulse(-1.0), 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopServerGateTest, "GFGame.AccelHop.ServerGate", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopServerGateTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	const FGFAccelHopTuning T;
	FGFAccelHopBoost S;
	TestTrue(TEXT("First hop accepted"), FGFAccelHopRules::ServerAccepts(S, 0.0, T));
	FGFAccelHopRules::ApplyHit(S, AccelHopHitAt(1.0), EGFHopTileKind::Standard, 5.0, 0.5f, T);
	TestFalse(TEXT("A second hop 0.1 s later is refused"), FGFAccelHopRules::ServerAccepts(S, 5.1, T));
	TestTrue(TEXT("After the gap it is fine"), FGFAccelHopRules::ServerAccepts(S, 5.0 + T.MinHopGapSeconds + 0.01, T));
	TestTrue(TEXT("Beat length clamps low"), AccelHopNear(FGFAccelHopRules::ClampBeatSeconds(0.01f), 0.2));
	TestTrue(TEXT("Beat length clamps high"), AccelHopNear(FGFAccelHopRules::ClampBeatSeconds(9.f), 2.0));
	TestTrue(TEXT("A real beat passes"), AccelHopNear(FGFAccelHopRules::ClampBeatSeconds(0.5f), 0.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTileActorTest, "GFGame.AccelHop.TileActor", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopTileActorTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	FAccelHopTestWorld W(TEXT("GFAccelHopTileWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFAccelHopTile* Tile = W.World->SpawnActor<AGFAccelHopTile>(FVector(5100.0, 0.0, 6.25), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Tile"), Tile))
	{
		return false;
	}
	UStaticMeshComponent* Mesh = Tile->GetMesh();
	if (!TestNotNull(TEXT("Tile mesh component"), Mesh))
	{
		return false;
	}
	TestFalse(TEXT("Tiles do not replicate"), Tile->GetIsReplicated());
	TestTrue(TEXT("Writes custom depth for the outline"), Mesh->bRenderCustomDepth);
	TestEqual(TEXT("Outline stencil 3"), Mesh->CustomDepthStencilValue, FGFAccelHopRules::OutlineStencil);
	TestTrue(TEXT("Solid floor"), Mesh->GetCollisionProfileName() == UCollisionProfile::BlockAll_ProfileName);
	TestTrue(TEXT("Default kind"), Tile->GetKind() == EGFHopTileKind::Standard);
	Tile->SetKind(EGFHopTileKind::Boost);
	TestTrue(TEXT("Kind set"), Tile->GetKind() == EGFHopTileKind::Boost);
	TestTrue(TEXT("Stencil survives a kind change"), Mesh->bRenderCustomDepth && Mesh->CustomDepthStencilValue == FGFAccelHopRules::OutlineStencil);
	TestTrue(TEXT("Floor component maps back to its tile"), AGFAccelHopTile::FromFloor(Mesh) == Tile);
	TestNull(TEXT("No floor, no tile"), AGFAccelHopTile::FromFloor(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopRoadActorTest, "GFGame.AccelHop.RoadActor", GFAccelHopTestsPrivate::AccelHopFlags)
bool FGFAccelHopRoadActorTest::RunTest(const FString&)
{
	using namespace GFAccelHopTestsPrivate;
	// Box tests (the road runs along +X from the Verdant side, 14800 cm half length in the real plan).
	const FTransform Along(FRotator::ZeroRotator, FVector(19200.0, 0.0, 6.25));
	const FVector Half(14800.0, 575.0, 400.0);
	TestTrue(TEXT("On the road"), AGFAccelHopRoad::BoxContains(Along, Half, FVector(5200.0, 0.0, 100.0)));
	TestTrue(TEXT("Mid hop above the road"), AGFAccelHopRoad::BoxContains(Along, Half, FVector(20000.0, 200.0, 300.0)));
	TestFalse(TEXT("Off the side"), AGFAccelHopRoad::BoxContains(Along, Half, FVector(20000.0, 800.0, 100.0)));
	TestFalse(TEXT("Past the Cinder end"), AGFAccelHopRoad::BoxContains(Along, Half, FVector(34100.0, 0.0, 100.0)));
	const FTransform Turned(FRotator(0.0, 90.0, 0.0), FVector::ZeroVector, FVector(3.0));
	TestTrue(TEXT("A turned box uses its own axes"), AGFAccelHopRoad::BoxContains(Turned, FVector(1000.0, 100.0, 100.0), FVector(0.0, 900.0, 0.0)));
	TestFalse(TEXT("Scale is ignored"), AGFAccelHopRoad::BoxContains(Turned, FVector(1000.0, 100.0, 100.0), FVector(0.0, 2000.0, 0.0)));

	// Mode: Beat Hop's session rule. Co-op always plays the built-in song; live only solo, asked for, and opened.
	TestTrue(TEXT("Default is the song"), AGFAccelHopRoad::PickMode(false, 1, false) == EGFBeatHopMode::Songs);
	TestTrue(TEXT("Solo live"), AGFAccelHopRoad::PickMode(true, 1, true) == EGFBeatHopMode::Live);
	TestTrue(TEXT("Co-op never goes live"), AGFAccelHopRoad::PickMode(true, 2, true) == EGFBeatHopMode::Songs);
	TestTrue(TEXT("Capture failed falls back to the song"), AGFAccelHopRoad::PickMode(true, 1, false) == EGFBeatHopMode::Songs);

	FAccelHopTestWorld W(TEXT("GFAccelHopRoadWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFAccelHopRoad* Road = W.World->SpawnActor<AGFAccelHopRoad>(FVector(19200.0, 0.0, 6.25), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Road"), Road))
	{
		return false;
	}
	TestTrue(TEXT("Road replicates (v2: the shared lit tile, issue 342)"), Road->GetIsReplicated());
	Road->SetRoadExtent(Half);
	TestTrue(TEXT("Extent set"), Road->GetBounds()->GetUnscaledBoxExtent().Equals(Half));
	Road->SetRoadExtent(FVector(1.0));
	TestTrue(TEXT("Extent has a floor"), Road->GetBounds()->GetUnscaledBoxExtent().X >= 50.0);
	TestTrue(TEXT("Bounds never collide"), Road->GetBounds()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	const UPostProcessComponent* Outline = Road->FindComponentByClass<UPostProcessComponent>();
	if (TestNotNull(TEXT("Outline post process"), Outline))
	{
		TestTrue(TEXT("Outline covers the whole view"), Outline->bUnbound);
	}
	TestFalse(TEXT("No player, no music"), Road->IsLocalPlayerOnRoad());
	TestFalse(TEXT("Clock untouched"), Road->OwnsClock());
	return true;
}

#endif
