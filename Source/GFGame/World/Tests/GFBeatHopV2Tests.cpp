// Beat Hop v2 (#201, #213): the StartGate countdown screen plays its cells on the beat, and the hub jukebox stands
// beside the Beat Hop portal where the sign's mirror is.

#include "Misc/AutomationTest.h"
#include "GFBeatHopCourse.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFBeatHopV2Tests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	enum : int32 { Off = 0, ReadyA = 1, Three = 2, Two = 3, One = 4, Go = 5, GoFlash = 6, ReadyB = 7 };

	struct FWorld
	{
		UWorld* World = nullptr;

		FWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFBeatHopV2World"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
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

	static float Scalar(const UMaterialInstanceDynamic* Mid, const TCHAR* Name)
	{
		float V = -1.f;
		if (Mid)
		{
			Mid->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), V);
		}
		return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCountdownTimingTest, "GFGame.BeatHop.CountdownTiming", GFBeatHopV2Tests::Flags)
bool FGFBeatHopCountdownTimingTest::RunTest(const FString&)
{
	using namespace GFBeatHopV2Tests;
	const double P = 0.5, GoT = 4.0; // 120 BPM, first brick at 4 s
	auto F = [&](double RunTime) { return AGFBeatHopCourse::CountdownFrame(RunTime, GoT, P, true, 0.0); };

	// Waiting on the pad (no run): Ready A/B at 2 Hz on the world clock.
	TestEqual(TEXT("Idle 0.1 s is Ready_A"), AGFBeatHopCourse::CountdownFrame(0.0, GoT, P, false, 0.1), (int32)ReadyA);
	TestEqual(TEXT("Idle 0.6 s is Ready_B"), AGFBeatHopCourse::CountdownFrame(0.0, GoT, P, false, 0.6), (int32)ReadyB);
	TestEqual(TEXT("Idle 1.1 s is Ready_A again"), AGFBeatHopCourse::CountdownFrame(0.0, GoT, P, false, 1.1), (int32)ReadyA);

	// Run: Ready on the beat until GO-3 beats.
	TestEqual(TEXT("Beat 0 Ready_A"), F(0.1), (int32)ReadyA);
	TestEqual(TEXT("Beat 1 Ready_B"), F(0.6), (int32)ReadyB);
	TestEqual(TEXT("Just before GO-3 still Ready"), F(GoT - 3.0 * P - 0.01) == ReadyA || F(GoT - 3.0 * P - 0.01) == ReadyB, true);
	TestEqual(TEXT("GO-3 beats shows 3"), F(GoT - 3.0 * P + 0.01), (int32)Three);
	TestEqual(TEXT("GO-2 beats shows 2"), F(GoT - 2.0 * P + 0.01), (int32)Two);
	TestEqual(TEXT("GO-1 beat shows 1"), F(GoT - P + 0.01), (int32)One);
	TestEqual(TEXT("Just before GO still 1"), F(GoT - 0.001), (int32)One);

	// GO: flash/GO every 0.125 s for 0.5 s, then GO held to +1.0 s, then Off.
	TestEqual(TEXT("GO +0.05 flash"), F(GoT + 0.05), (int32)GoFlash);
	TestEqual(TEXT("GO +0.18 GO"), F(GoT + 0.18), (int32)Go);
	TestEqual(TEXT("GO +0.30 flash"), F(GoT + 0.30), (int32)GoFlash);
	TestEqual(TEXT("GO +0.43 GO"), F(GoT + 0.43), (int32)Go);
	TestEqual(TEXT("GO +0.7 held"), F(GoT + 0.7), (int32)Go);
	TestEqual(TEXT("GO +1.1 off"), F(GoT + 1.1), (int32)Off);
	TestEqual(TEXT("Mid-run off"), F(GoT + 30.0), (int32)Off);

	// Short lead-in (first brick on beat 1): the countdown starts already inside 3/2/1, no Ready.
	TestEqual(TEXT("Short lead-in starts at 2"), AGFBeatHopCourse::CountdownFrame(0.0, 0.75, 0.5, true, 0.0), (int32)Two);
	// Silly period never divides by zero.
	TestEqual(TEXT("Zero period clamps"), AGFBeatHopCourse::CountdownFrame(10.0, 4.0, 0.0, true, 0.0), (int32)Off);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopCountdownScreenTest, "GFGame.BeatHop.CountdownScreen", GFBeatHopV2Tests::Flags)
bool FGFBeatHopCountdownScreenTest::RunTest(const FString&)
{
	using namespace GFBeatHopV2Tests;
	TestTrue(TEXT("MI_Track_StartGate_Screen imported"), FPackageName::DoesPackageExist(TEXT("/Game/Art/BeatHop/Track/Screen/MI_Track_StartGate_Screen")));
	TestTrue(TEXT("T_BeatHop_Countdown_D imported"), FPackageName::DoesPackageExist(TEXT("/Game/Art/BeatHop/Track/Screen/T_BeatHop_Countdown_D")));
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Beat"), FVector::ZeroVector);
	AGFBeatHopCourse* Course = nullptr;
	for (TActorIterator<AGFBeatHopCourse> It(W.World); It; ++It)
	{
		Course = *It;
	}
	if (!TestNotNull(TEXT("Course on L_Beat"), Course) || !TestNotNull(TEXT("Start gate"), Course->GetStartGate()))
	{
		return false;
	}
	UMaterialInstanceDynamic* Screen = Course->GetScreenMaterial();
	if (!TestNotNull(TEXT("Screen MID from StartGate slot 1"), Screen))
	{
		return false;
	}
	TestTrue(TEXT("Slot 1 shows the MID"), Course->GetStartGate()->GetMaterial(1) == Screen);
	TestTrue(TEXT("MID parent is MI_Track_StartGate_Screen"), Screen->Parent && Screen->Parent->GetName() == TEXT("MI_Track_StartGate_Screen"));
	TestEqual(TEXT("EmissiveStrength stays 1.5 (#204)"), Scalar(Screen, TEXT("EmissiveStrength")), 1.5f, 0.001f);

	Course->UpdateScreen(0.1);
	TestEqual(TEXT("Idle shows Ready_A"), Course->GetShownScreenFrame(), (int32)ReadyA);
	TestEqual(TEXT("Frame scalar follows"), Scalar(Screen, TEXT("Frame")), 1.f, 0.001f);
	Course->UpdateScreen(0.6);
	TestEqual(TEXT("Idle half a second later shows Ready_B"), Course->GetShownScreenFrame(), (int32)ReadyB);
	TestEqual(TEXT("Frame scalar follows again"), Scalar(Screen, TEXT("Frame")), 7.f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFBeatHopHubJukeboxTest, "GFGame.BeatHop.HubJukebox", GFBeatHopV2Tests::Flags)
bool FGFBeatHopHubJukeboxTest::RunTest(const FString&)
{
	using namespace GFBeatHopV2Tests;
	const bool bArt = FPackageName::DoesPackageExist(TEXT("/Game/Art/Furniture/SM_Furn_Jukebox"));
	TestTrue(TEXT("SM_Furn_Jukebox imported"), bArt);
	FWorld W;
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);
	int32 Shown = 0, Portals = 0;
	for (TActorIterator<AGFMiniGameGate> It(W.World); It; ++It)
	{
		AGFMiniGameGate* Gate = *It;
		Gate->ApplyVisuals(); // bare worlds skip BeginPlay on deferred spawns; Place() already did this, so a re-apply must hold
		UStaticMeshComponent* Box = Gate->GetJukebox();
		if (!TestNotNull(TEXT("Jukebox component"), Box))
		{
			continue;
		}
		const bool bBeatPortal = Gate->GateRole == EGFMiniGameGateRole::Portal && Gate->EntryId == FName(TEXT("BeatHop"));
		Portals += Gate->GateRole == EGFMiniGameGateRole::Portal ? 1 : 0;
		if (!bBeatPortal)
		{
			TestTrue(*FString::Printf(TEXT("%s: no jukebox"), *Gate->EntryId.ToString()), Box->bHiddenInGame && Box->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Box->GetStaticMesh());
			continue;
		}
		++Shown;
		if (!bArt)
		{
			continue;
		}
		const FVector At = Box->GetComponentLocation();
		AddInfo(FString::Printf(TEXT("Jukebox at (%.2f, %.2f, %.2f) yaw %.2f"), At.X, At.Y, At.Z, Box->GetComponentRotation().Yaw));
		TestTrue(TEXT("Jukebox mesh"), Box->GetStaticMesh() && Box->GetStaticMesh()->GetPathName() == AGFMiniGameGate::JukeboxPath);
		TestFalse(TEXT("Jukebox shows"), Box->bHiddenInGame);
		TestTrue(TEXT("Jukebox blocks"), Box->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
		TestEqual(TEXT("Jukebox X (#213)"), At.X, -1626.34, 0.5);
		TestEqual(TEXT("Jukebox Y (#213)"), At.Y, 1202.08, 0.5);
		TestEqual(TEXT("Jukebox on the gate's ground"), At.Z, Gate->GetActorLocation().Z, 0.5);
		TestEqual(TEXT("Jukebox yaw -45 (#213)"), FRotator::NormalizeAxis(Box->GetComponentRotation().Yaw), -45.0, 0.1);
		TestTrue(TEXT("Jukebox scale 1"), Box->GetComponentScale().Equals(FVector::OneVector, 0.001));
		if (UStaticMeshComponent* Sign = Gate->GetModeSign())
		{
			TestTrue(TEXT("Jukebox is the sign's mirror (600 cm apart)"), FMath::IsNearlyEqual(FVector::Dist2D(Sign->GetComponentLocation(), At), 2.0 * FGFMiniGameCatalog::SignRightCm, 1.0));
		}
	}
	TestTrue(TEXT("Hub has portals"), Portals > 0);
	TestEqual(TEXT("Exactly one jukebox, on the Beat Hop portal"), Shown, 1);
	return true;
}

#endif
