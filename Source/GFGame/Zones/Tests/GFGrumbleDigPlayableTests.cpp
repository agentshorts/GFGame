// Grumble Dig, playable from the porch arch: one greybox site, Start, Finish, results, back to the hub stand.
// L_GrumbleDig stays missing, so the ring and the porch travel plan are unchanged.

#include "Misc/AutomationTest.h"
#include "GFGrumbleDigGateSubsystem.h"
#include "GFGrumbleDigSite.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerState.h"
#include "GFRegionGate.h"
#include "GFVoxelTypes.h"
#include "GFWorldLayoutDirector.h"
#include "GFZoneLayout.h"
#include "GFZoneTypes.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Misc/PackageName.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGrumbleDigPlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FDigWorld
	{
		UWorld* World = nullptr;

		explicit FDigWorld(const TCHAR* Name)
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

		~FDigWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static int32 CountSites(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFGrumbleDigSite> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static AGFWorldLayoutDirector* SpawnDirector(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGFWorldLayoutDirector>(AGFWorldLayoutDirector::StaticClass(), FTransform::Identity, Params);
	}

	static ACharacter* SpawnPawn(UWorld* World, const FVector& At, AGFPlayerState** OutState)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		ACharacter* Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(At), Params);
		if (PS)
		{
			PS->PlayerKey = TEXT("Digger");
			PS->SetPlayerId(7);
		}
		if (Pawn && PS)
		{
			Pawn->SetPlayerState(PS);
		}
		if (OutState)
		{
			*OutState = PS;
		}
		return Pawn;
	}

	static bool SpotOk(FAutomationTestBase& Test, const AGFGrumbleDigSite* Site, const TCHAR* Name)
	{
		UBoxComponent* Box = Site ? Site->GetSpot(FName(Name)) : nullptr;
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s spot"), Name), Box))
		{
			return false;
		}
		Test.TestTrue(*FString::Printf(TEXT("%s overlaps"), Name), Box->GetGenerateOverlapEvents());
		Test.TestTrue(*FString::Printf(TEXT("%s hits pawns"), Name), Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigSiteOnGateTest, "GFGame.GrumbleDig.Playable.SiteOnGate", GFGrumbleDigPlayableTests::Flags)
bool FGFGrumbleDigSiteOnGateTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigPlayableTests;
	FDigWorld W(TEXT("GFGrumbleSiteWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}

	const FGFRegionSlot* Slot = FGFZoneLayout::FindByGate(TEXT("GrumbleDig"));
	if (!TestNotNull(TEXT("Grumble Dig porch slot"), Slot))
	{
		return false;
	}
	const FGFRegionTravelPlan Missing = FGFZoneLayout::PlanTravel(*Slot, false);
	TestFalse(TEXT("Missing package still does not travel"), Missing.bTravel);
	TestTrue(TEXT("Missing package still sets no stone"), Missing.PendingStoneId.IsNone());
	TestEqual(TEXT("Under construction plan is unchanged"), Missing.Message, FString(GFZoneCopy::UnderConstruction()));

	const UGFGrumbleDigGateSubsystem* GateDefaults = GetDefault<UGFGrumbleDigGateSubsystem>();
	if (!TestNotNull(TEXT("Gate class"), GateDefaults))
	{
		return false;
	}
	TestEqual(TEXT("Map name stays L_GrumbleDig"), GateDefaults->GetMapName().ToString(), FString(TEXT("/Game/Maps/L_GrumbleDig")));
	TestFalse(TEXT("No L_GrumbleDig package"), FPackageName::DoesPackageExist(GateDefaults->GetMapName().ToString()));
	TestNull(TEXT("Not a hub-ring row"), FGFMiniGameCatalog::Find(TEXT("GrumbleDig")));

	AGFWorldLayoutDirector* Director = SpawnDirector(W.World);
	if (!TestNotNull(TEXT("Layout director"), Director))
	{
		return false;
	}
	Director->EnsureGrumbleDigSite();
	AGFGrumbleDigSite* Site = AGFGrumbleDigSite::Get(W.World);
	if (!TestNotNull(TEXT("Director places a site"), Site))
	{
		return false;
	}
	TestEqual(TEXT("One site"), CountSites(W.World), 1);
	const FVector Porch = FGFZoneLayout::BearingPoint(Slot->BearingDeg, FGFZoneConstants::PorchRingCm);
	TestTrue(TEXT("Site sits on the porch center"), Site->GetActorLocation().Equals(Porch, 1.f));
	TestEqual(TEXT("Three spots"), Site->NumSpots(), 3);
	SpotOk(*this, Site, TEXT("Start"));
	SpotOk(*this, Site, TEXT("Finish"));
	SpotOk(*this, Site, TEXT("Return"));

	TArray<FString> Lines;
	Site->GetPromptLines(Lines);
	FString Joined;
	for (const FString& Line : Lines)
	{
		Joined += Line;
		Joined += TEXT("\n");
	}
	TestTrue(TEXT("Prompt names Start"), Joined.Contains(TEXT("Start")));
	TestTrue(TEXT("Prompt names Finish"), Joined.Contains(TEXT("Finish")));
	TestTrue(TEXT("Prompt names Hub"), Joined.Contains(TEXT("Hub")));

	const FVector TowardHub = -Porch.GetSafeNormal2D();
	const FVector Arch = Porch + FVector(TowardHub.X, TowardHub.Y, 0.f) * FGFZoneConstants::PorchArchInsetCm;
	const float LobbyToArch = FVector::Dist2D(Site->GetLobbyStand(), Arch);
	TestTrue(TEXT("Lobby is clear of the porch arch"), LobbyToArch > 500.f);
	TestTrue(TEXT("Lobby is hub-ward of the porch center"), LobbyToArch < FVector::Dist2D(Porch, Arch));
	TestTrue(TEXT("Lobby sits between the arch and the porch center"), FVector::Dist2D(Site->GetLobbyStand(), Porch) < FGFZoneConstants::PorchArchInsetCm);
	if (const UBoxComponent* StartBox = Site->GetSpot(TEXT("Start")))
	{
		if (const UBoxComponent* FinishBox = Site->GetSpot(TEXT("Finish")))
		{
			const float StartToArch = FVector::Dist2D(StartBox->GetComponentLocation(), Arch);
			const float FinishToArch = FVector::Dist2D(FinishBox->GetComponentLocation(), Arch);
			TestTrue(TEXT("Start is past the lobby, away from the arch"), StartToArch > LobbyToArch);
			TestTrue(TEXT("Finish is past Start"), FinishToArch > StartToArch);
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFRegionGate* Other = W.World->SpawnActor<AGFRegionGate>(AGFRegionGate::StaticClass(), FTransform(Arch), Params);
	const FGFRegionSlot* Mine = FGFZoneLayout::FindByGate(TEXT("Mine"));
	if (TestNotNull(TEXT("Other porch"), Other) && TestNotNull(TEXT("Mine slot"), Mine))
	{
		Other->Setup(*Mine);
		ACharacter* Walker = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(0.f, 0.f, 200.f)), Params);
		if (TestNotNull(TEXT("Walker"), Walker))
		{
			TestFalse(TEXT("Another porch does not enter the trench"), Other->EnterGrumbleDig(Walker));
			TestTrue(TEXT("Walker stayed put"), Walker->GetActorLocation().Equals(FVector(0.f, 0.f, 200.f), 1.f));
		}
	}

	AGFRegionGate* Gate = W.World->SpawnActor<AGFRegionGate>(AGFRegionGate::StaticClass(), FTransform(Arch), Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(40.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Grumble arch"), Gate) || !TestNotNull(TEXT("Pawn"), Pawn))
	{
		return false;
	}
	Gate->Setup(*Slot);
	TestFalse(TEXT("Hub stand is not on the trench"), Site->IsPlayerNear(Pawn));
	TestTrue(TEXT("Walking the arch enters the trench"), Gate->EnterGrumbleDig(Pawn));
	TestTrue(TEXT("Stands in the lobby"), Pawn->GetActorLocation().Equals(Site->GetLobbyStand(), 5.f));
	TestTrue(TEXT("Lobby is on the site"), Site->IsPlayerNear(Pawn));
	TestFalse(TEXT("Entry does not start the run"), Site->IsRunning());

	Director->EnsureGrumbleDigSite();
	TestEqual(TEXT("Ensuring again keeps a single site"), CountSites(W.World), 1);
	TestEqual(TEXT("Spots not doubled"), Site->NumSpots(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGrumbleDigPlayableLoopTest, "GFGame.GrumbleDig.Playable.Loop", GFGrumbleDigPlayableTests::Flags)
bool FGFGrumbleDigPlayableLoopTest::RunTest(const FString&)
{
	using namespace GFGrumbleDigPlayableTests;
	FDigWorld W(TEXT("GFGrumbleLoopWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFWorldLayoutDirector* Director = SpawnDirector(W.World);
	if (!TestNotNull(TEXT("Layout director"), Director))
	{
		return false;
	}
	Director->EnsureGrumbleDigSite();
	AGFGrumbleDigSite* Site = AGFGrumbleDigSite::Get(W.World);
	const FGFRegionSlot* Slot = FGFZoneLayout::FindByGate(TEXT("GrumbleDig"));
	if (!TestNotNull(TEXT("Site"), Site) || !TestNotNull(TEXT("Slot"), Slot))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Porch = Site->GetActorLocation();
	const FVector TowardHub = -Porch.GetSafeNormal2D();
	const FVector Arch = Porch + FVector(TowardHub.X, TowardHub.Y, 0.f) * FGFZoneConstants::PorchArchInsetCm;
	AGFRegionGate* Gate = W.World->SpawnActor<AGFRegionGate>(AGFRegionGate::StaticClass(), FTransform(Arch), Params);
	AGFPlayerState* PS = nullptr;
	ACharacter* Pawn = SpawnPawn(W.World, FVector(0.f, 0.f, 200.f), &PS);
	if (!TestNotNull(TEXT("Arch"), Gate) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Player"), PS))
	{
		return false;
	}
	Gate->Setup(*Slot);

	TestFalse(TEXT("Finish before start does nothing"), Site->TryFinish(Pawn));
	TestFalse(TEXT("No board yet"), Site->HasResults());
	TestTrue(TEXT("Arch drops you in the lobby"), Gate->EnterGrumbleDig(Pawn));
	TestTrue(TEXT("Lobby"), Pawn->GetActorLocation().Equals(Site->GetLobbyStand(), 5.f));
	TestFalse(TEXT("Still idle"), Site->IsRunning());

	if (!TestTrue(TEXT("Start"), Site->TryStart(Pawn)))
	{
		return false;
	}
	TestTrue(TEXT("Running"), Site->IsRunning());
	TestFalse(TEXT("Start cleared the board"), Site->HasResults());
	TestFalse(TEXT("Start does not restart a run"), Site->TryStart(Pawn));
	TestTrue(TEXT("Still the same run"), Site->IsRunning());

	if (!TestTrue(TEXT("Finish"), Site->TryFinish(Pawn)))
	{
		return false;
	}
	TestFalse(TEXT("Run ended"), Site->IsRunning());
	TestFalse(TEXT("Finish twice stays on the board"), Site->TryFinish(Pawn));
	TestTrue(TEXT("Board is up"), Site->HasResults());
	TestTrue(TEXT("The trench carved cells"), Site->GetCellsDug() > 0);
	TestEqual(TEXT("Stone matches the carve"), Site->GetStoneCount(), Site->GetCellsDug());
	TestEqual(TEXT("Yield is Stone"), Site->GetYieldItem(), FName(TEXT("Stone")));
	TestEqual(TEXT("Stair pitch"), Site->GetAppliedPitchDegrees(), -30.f, 0.01f);
	TestEqual(TEXT("Riser is one block"), Site->GetStepRiserCm(), GFBlockSize, 0.01f);

	TArray<FString> Lines;
	Site->GetPromptLines(Lines);
	FString Joined;
	for (const FString& Line : Lines)
	{
		Joined += Line;
		Joined += TEXT("\n");
	}
	TestTrue(TEXT("Results title"), Joined.Contains(TEXT("RESULTS")));
	TestTrue(TEXT("Results name Stone"), Joined.Contains(TEXT("Stone")));
	TestTrue(TEXT("Results name the cell count"), Joined.Contains(FString::Printf(TEXT("Cells %d"), Site->GetCellsDug())));
	TestTrue(TEXT("Prompt still offers the hub"), Joined.Contains(TEXT("Hub")));

	TestTrue(TEXT("Hub arch"), Site->TryReturn(Pawn));
	TestTrue(TEXT("Back on the hub stand"), Pawn->GetActorLocation().Equals(Site->GetHubReturnLocation(), 5.f));
	TestTrue(TEXT("Hub stand is the origin plus 120"), Site->GetHubReturnLocation().Equals(FVector(0.f, 0.f, AGFGrumbleDigSite::HubStandZCm), 1.f));
	TestFalse(TEXT("The hub stand is outside the trench HUD"), Site->IsPlayerNear(Pawn));
	TestTrue(TEXT("The board survives the walk home"), Site->HasResults());
	TestFalse(TEXT("Not still running at the hub"), Site->IsRunning());

	TestTrue(TEXT("The arch will take you back"), Gate->EnterGrumbleDig(Pawn));
	TestTrue(TEXT("Lobby again"), Pawn->GetActorLocation().Equals(Site->GetLobbyStand(), 5.f));
	TestTrue(TEXT("Start again"), Site->TryStart(Pawn));
	TestFalse(TEXT("A new run clears the board"), Site->HasResults());
	TestTrue(TEXT("Running again"), Site->IsRunning());
	return true;
}

#endif
