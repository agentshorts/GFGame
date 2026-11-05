// Acceleration Hop (issue 334) on the movement component: a hop tile landing on the beat raises the run speed.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.AccelHop;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.h"
#include "GFCharacterMovementComponent.h"
#include "GFMovementTuning.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAccelHopMoveTestsPrivate
{
	static constexpr EAutomationTestFlags AccelHopMoveFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FAccelHopMoveWorld
	{
		UWorld* World = nullptr;

		explicit FAccelHopMoveWorld(const TCHAR* Name)
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

		~FAccelHopMoveWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** A bare movement component walking at the tuned base run (no character: no RPC, world time 0). */
	static UGFCharacterMovementComponent* AccelHopBareMove()
	{
		UGFCharacterMovementComponent* Move = NewObject<UGFCharacterMovementComponent>(GetTransientPackage());
		Move->MovementMode = MOVE_Walking;
		Move->MaxWalkSpeed = GetDefault<UGFMovementTuning>()->BaseRunSpeed;
		return Move;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopTuningAssetTest, "GFGame.AccelHop.TuningAsset", GFAccelHopMoveTestsPrivate::AccelHopMoveFlags)
bool FGFAccelHopTuningAssetTest::RunTest(const FString&)
{
	const UGFMovementTuning* Tuning = GetDefault<UGFMovementTuning>();
	if (!TestNotNull(TEXT("Tuning"), Tuning))
	{
		return false;
	}
	const FGFAccelHopTuning Defaults;
	TestEqual(TEXT("Perfect boost is tunable on the movement asset"), Tuning->AccelHop.PerfectBoost, Defaults.PerfectBoost);
	TestEqual(TEXT("Cap"), Tuning->AccelHop.MaxMultiplier, Defaults.MaxMultiplier);
	TestEqual(TEXT("Boost beats"), Tuning->AccelHop.BoostBeats, Defaults.BoostBeats);
	TestTrue(TEXT("A fully boosted run is no faster than a dash"), Tuning->BaseRunSpeed * Tuning->AccelHop.MaxMultiplier <= Tuning->DashSpeed + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopMaxSpeedTest, "GFGame.AccelHop.MaxSpeed", GFAccelHopMoveTestsPrivate::AccelHopMoveFlags)
bool FGFAccelHopMaxSpeedTest::RunTest(const FString&)
{
	using namespace GFAccelHopMoveTestsPrivate;
	FAccelHopMoveWorld W(TEXT("GFAccelHopMoveWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFAccelHopTile* Boost = W.World->SpawnActor<AGFAccelHopTile>(FVector::ZeroVector, FRotator::ZeroRotator);
	AGFAccelHopTile* Plain = W.World->SpawnActor<AGFAccelHopTile>(FVector(200.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Boost tile"), Boost) || !TestNotNull(TEXT("Plain tile"), Plain))
	{
		return false;
	}
	Boost->SetKind(EGFHopTileKind::Boost);

	UGFCharacterMovementComponent* Move = AccelHopBareMove();
	const FGFAccelHopTuning& Hop = Move->GetTuning().AccelHop;
	const float Base = Move->GetMaxSpeed();
	TestTrue(TEXT("Walking at the base run"), FMath::IsNearlyEqual(Base, GetDefault<UGFMovementTuning>()->BaseRunSpeed, 0.5f));
	TestTrue(TEXT("No boost yet"), FMath::IsNearlyEqual(Move->GetAccelHopMultiplier(), 1.f));

	TestFalse(TEXT("No tile, nothing happens"), Move->HandleAccelHopLanding(nullptr, 8.0, 120.f, EGFBeatHopMode::Songs));
	TestFalse(TEXT("No clock, nothing happens"), Move->HandleAccelHopLanding(Boost, -1.0, 120.f, EGFBeatHopMode::Songs));
	TestFalse(TEXT("Off the beat is a Miss"), Move->HandleAccelHopLanding(Plain, 8.4, 120.f, EGFBeatHopMode::Songs));
	TestTrue(TEXT("The Miss is remembered for the HUD"), Move->GetLastAccelHopHit().Judgement == EGFJudgement::Miss);
	TestTrue(TEXT("Still the base run"), FMath::IsNearlyEqual(Move->GetMaxSpeed(), Base, 0.5f));

	TestTrue(TEXT("Perfect on a Boost tile"), Move->HandleAccelHopLanding(Boost, 9.0, 120.f, EGFBeatHopMode::Songs));
	const float Expected = Base * (1.f + Hop.PerfectBoost + Hop.BoostTileBonus);
	TestTrue(*FString::Printf(TEXT("Run speed is boosted (%.1f, want %.1f)"), Move->GetMaxSpeed(), Expected), FMath::IsNearlyEqual(Move->GetMaxSpeed(), Expected, 0.5f));
	TestEqual(TEXT("Chain 1"), Move->GetAccelHopChain(), 1);
	TestTrue(TEXT("Next beat on a plain tile stacks"), Move->HandleAccelHopLanding(Plain, 10.02, 120.f, EGFBeatHopMode::Songs));
	TestTrue(TEXT("Faster again"), Move->GetMaxSpeed() > Expected);
	TestTrue(TEXT("Never past the cap"), Move->GetMaxSpeed() <= Base * Hop.MaxMultiplier + 0.5f);

	TestFalse(TEXT("The server path needs a character"), Move->ApplyServerAccelHop(Boost, Move->GetLastAccelHopHit(), 0.5f));
	TestFalse(TEXT("And a tile"), Move->ApplyServerAccelHop(nullptr, Move->GetLastAccelHopHit(), 0.5f));
	return true;
}

#endif
