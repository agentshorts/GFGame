// Acceleration Hop (issue 334) on the movement component: a hop tile landing on the beat raises the run speed.
// v2 (issue 342): landing on the road's lit target tile pays LitBoostScale times the gain, once.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.AccelHop;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.h"
#include "GFAccelHopRoad.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLitLandingTest, "GFGame.AccelHop.Lit.Landing", GFAccelHopMoveTestsPrivate::AccelHopMoveFlags)
bool FGFAccelHopLitLandingTest::RunTest(const FString&)
{
	using namespace GFAccelHopMoveTestsPrivate;
	FAccelHopMoveWorld W(TEXT("GFAccelHopLitMoveWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFAccelHopTile* Near = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(200.0, 0.0, 0.0)), Params);
	AGFAccelHopTile* Target = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(1200.0, 0.0, 0.0)), Params);
	AGFAccelHopTile* Other = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(1400.0, 0.0, 0.0)), Params);
	AGFAccelHopRoad* Road = W.World->SpawnActor<AGFAccelHopRoad>(AGFAccelHopRoad::StaticClass(), FTransform(FVector(1000.0, 0.0, 0.0)), Params);
	if (!TestNotNull(TEXT("Near"), Near) || !TestNotNull(TEXT("Target"), Target) || !TestNotNull(TEXT("Other"), Other) || !TestNotNull(TEXT("Road"), Road))
	{
		return false;
	}
	Target->SetKind(EGFHopTileKind::Boost);
	Other->SetKind(EGFHopTileKind::Boost);
	Road->SetAutoPick(false);
	Road->RefreshTiles();
	Road->ServerPickFrom(Road->AlongOf(Near->GetActorLocation()), 1.f, 0.0);
	AGFAccelHopTile* Lit = Road->GetLitTile();
	if (!TestNotNull(TEXT("A Boost tile ahead is lit"), Lit))
	{
		return false;
	}
	AGFAccelHopTile* Unlit = Lit == Target ? Other : Target;

	// Two runners, same Perfect landing on a Boost tile: one on the lit tile, one beside it.
	UGFCharacterMovementComponent* OnLit = AccelHopBareMove();
	UGFCharacterMovementComponent* OnPlain = AccelHopBareMove();
	const FGFAccelHopTuning& Hop = OnLit->GetTuning().AccelHop;
	const float Base = OnLit->GetMaxSpeed();
	TestTrue(TEXT("The plain Boost tile lands"), OnPlain->HandleAccelHopLanding(Unlit, 9.0, 120.f, EGFBeatHopMode::Songs));
	TestFalse(TEXT("Not a lit landing"), OnPlain->WasLastAccelHopLit());
	TestTrue(TEXT("Missing the lit tile leaves it lit"), Road->GetLitTile() == Lit);
	TestTrue(TEXT("The lit tile lands"), OnLit->HandleAccelHopLanding(Lit, 9.0, 120.f, EGFBeatHopMode::Songs));
	TestTrue(TEXT("A lit landing"), OnLit->WasLastAccelHopLit());

	const float Normal = Base * (1.f + Hop.PerfectBoost + Hop.BoostTileBonus);
	const float Boosted = Base * (1.f + (Hop.PerfectBoost + Hop.BoostTileBonus) * Hop.LitBoostScale);
	TestTrue(*FString::Printf(TEXT("Plain tile: the v1 boost (%.1f, want %.1f)"), OnPlain->GetMaxSpeed(), Normal), FMath::IsNearlyEqual(OnPlain->GetMaxSpeed(), Normal, 0.5f));
	TestTrue(*FString::Printf(TEXT("Lit tile: 1.5x the boost (%.1f, want %.1f)"), OnLit->GetMaxSpeed(), Boosted), FMath::IsNearlyEqual(OnLit->GetMaxSpeed(), Boosted, 0.5f));
	TestTrue(TEXT("Lit beats plain"), OnLit->GetMaxSpeed() > OnPlain->GetMaxSpeed() + 1.f);

	TestNull(TEXT("Paid out: the tile goes dark"), Road->GetLitTile());
	TestFalse(TEXT("And is drawn dark"), Lit->IsLitVisual());
	UGFCharacterMovementComponent* Late = AccelHopBareMove();
	TestTrue(TEXT("A second runner on the same tile still hops"), Late->HandleAccelHopLanding(Lit, 9.0, 120.f, EGFBeatHopMode::Songs));
	TestFalse(TEXT("But not lit: it paid once"), Late->WasLastAccelHopLit());
	TestTrue(TEXT("And gets the normal boost"), FMath::IsNearlyEqual(Late->GetMaxSpeed(), Normal, 0.5f));

	TestTrue(TEXT("A Miss on a lit tile pays nothing"), [&]()
	{
		Road->ServerSetLit(Unlit->GetRoadIndex(), 1.0);
		UGFCharacterMovementComponent* Missed = AccelHopBareMove();
		const bool bChanged = Missed->HandleAccelHopLanding(Unlit, 9.5, 120.f, EGFBeatHopMode::Songs);
		return !bChanged && FMath::IsNearlyEqual(Missed->GetMaxSpeed(), Base, 0.5f) && Road->GetLitTile() == Unlit;
	}());
	return true;
}

#endif
