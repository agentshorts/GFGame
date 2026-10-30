// Wild dinos: ring points stay inside the annulus, the playtest table comes from config,
// a game world gets at least one dino near Rime, and wander targets stay inside the radius.

#include "Misc/AutomationTest.h"
#include "GFWildDinoDirector.h"
#include "GFWanderComponent.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFWildDinoTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FGFWildDinoSpawn* FindSpawn(const AGFWildDinoDirector* Director, FName Anchor, FName Species)
	{
		if (!Director)
		{
			return nullptr;
		}
		for (const FGFWildDinoSpawn& Entry : Director->Spawns)
		{
			if (Entry.AnchorId == Anchor && Entry.Species == Species)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	static const FGFWildDinoSpawn* FindSpecies(const AGFWildDinoDirector* Director, FName Species)
	{
		if (!Director)
		{
			return nullptr;
		}
		for (const FGFWildDinoSpawn& Entry : Director->Spawns)
		{
			if (Entry.Species == Species)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	static const FGFTerritoryFaction* FindFaction(FName Id)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		if (!Defaults)
		{
			return nullptr;
		}
		for (const FGFTerritoryFaction& Faction : Defaults->FactionLayout)
		{
			if (Faction.Id == Id)
			{
				return &Faction;
			}
		}
		return nullptr;
	}

	struct FWildWorld
	{
		UWorld* World = nullptr;

		~FWildWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		bool Open(FAutomationTestBase& T, const TCHAR* Name)
		{
			if (!T.TestNotNull(TEXT("Engine"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!T.TestNotNull(TEXT("World"), World))
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
			return true;
		}
	};

	static bool InRing(float Dist, float MinRadius, float Radius)
	{
		const float Inner = FMath::Min(MinRadius, Radius);
		const float Outer = FMath::Max(MinRadius, Radius);
		return Dist + 0.05f >= Inner && Dist - 0.05f <= Outer;
	}

	static AGFDino* SpawnQuietDino(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Deferred;
		Deferred.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Deferred.bDeferConstruction = true;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), At, FRotator::ZeroRotator, Deferred);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->AutoPossessAI = EAutoPossessAI::Disabled;
		Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, At));
		if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
		{
			Move->DisableMovement();
			Move->GravityScale = 0.f;
		}
		return Dino;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWildDinoRingTest, "GFGame.WildDino.Ring", GFWildDinoTests::Flags)
bool FGFWildDinoRingTest::RunTest(const FString&)
{
	using namespace GFWildDinoTests;

	const FVector Center(10.f, -20.f, 40.f);
	FRandomStream Rng(42);
	for (int32 I = 0; I < 128; ++I)
	{
		const FVector Point = AGFWildDinoDirector::PickRingPoint(Center, 600.f, 1500.f, Rng);
		const float Dist = FVector::Dist2D(Point, Center);
		TestTrue(TEXT("Ring point inside [600, 1500]"), InRing(Dist, 600.f, 1500.f));
		TestEqual(TEXT("Ring keeps centre Z"), Point.Z, Center.Z);
	}

	FRandomStream Swapped(7);
	for (int32 I = 0; I < 32; ++I)
	{
		const FVector Point = AGFWildDinoDirector::PickRingPoint(Center, 1500.f, 600.f, Swapped);
		TestTrue(TEXT("Swapped radii still inside the annulus"), InRing(FVector::Dist2D(Point, Center), 1500.f, 600.f));
	}

	FRandomStream Exact(3);
	for (int32 I = 0; I < 16; ++I)
	{
		const FVector Point = AGFWildDinoDirector::PickRingPoint(Center, 1000.f, 1000.f, Exact);
		TestTrue(TEXT("Equal radii sit on the circle"), FMath::IsNearlyEqual(FVector::Dist2D(Point, Center), 1000.f, 0.05f));
	}

	FRandomStream Zero(1);
	const FVector Origin = AGFWildDinoDirector::PickRingPoint(Center, 0.f, 0.f, Zero);
	TestTrue(TEXT("Zero radius is the centre"), Origin.Equals(Center, 0.05f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWildDinoSpawnTableTest, "GFGame.WildDino.SpawnTable", GFWildDinoTests::Flags)
bool FGFWildDinoSpawnTableTest::RunTest(const FString&)
{
	using namespace GFWildDinoTests;

	const AGFWildDinoDirector* Director = GetDefault<AGFWildDinoDirector>();
	if (!TestNotNull(TEXT("Director config"), Director))
	{
		return false;
	}

	struct FExpect
	{
		const TCHAR* Anchor;
		const TCHAR* Species;
		int32 Count;
	};
	const FExpect Rows[] = {
		{TEXT("Rime"), TEXT("Nipper"), 3},
		{TEXT("Rime"), TEXT("Raptor"), 2},
		{TEXT("Verdant"), TEXT("Tusksaw"), 2},
		{TEXT("Verdant"), TEXT("Lanternmaw"), 2},
		{TEXT("Cinderwrights"), TEXT("Scythepaw"), 2},
		{TEXT("Cinderwrights"), TEXT("Hornback"), 1},
	};
	TestEqual(TEXT("Spawn rows"), Director->Spawns.Num(), static_cast<int32>(UE_ARRAY_COUNT(Rows)));
	int32 Total = 0;
	for (const FExpect& Row : Rows)
	{
		const FGFWildDinoSpawn* Entry = FindSpawn(Director, Row.Anchor, Row.Species);
		const FString What = FString::Printf(TEXT("%s %s"), Row.Anchor, Row.Species);
		if (!TestNotNull(*What, Entry))
		{
			continue;
		}
		TestEqual(*(What + TEXT(" count")), Entry->Count, Row.Count);
		TestEqual(*(What + TEXT(" radius")), Entry->Radius, 1500.f);
		TestEqual(*(What + TEXT(" min radius")), Entry->MinRadius, 600.f);
		Total += Entry->Count;
	}
	TestEqual(TEXT("Table size"), Total, 12);
	TestEqual(TEXT("MaxAlive"), Director->MaxAlive, 24);
	TestEqual(TEXT("RespawnSeconds"), Director->RespawnSeconds, 60.f);
	TestFalse(TEXT("Playtest spawns are not hostile"), Director->bSpawnHostile);

	const UGFWanderComponent* Wander = GetDefault<UGFWanderComponent>();
	if (!TestNotNull(TEXT("Wander config"), Wander))
	{
		return false;
	}
	TestEqual(TEXT("Default walk speed"), Wander->DefaultWalkSpeed, 250.f);
	TestEqual(TEXT("Wander radius"), Wander->WanderRadius, 800.f);
	TestEqual(TEXT("Wander every min"), Wander->WanderEveryMin, 4.f);
	TestEqual(TEXT("Wander every max"), Wander->WanderEveryMax, 9.f);
	TestEqual(TEXT("Stop distance"), Wander->StopDistance, 100.f);
	TestEqual(TEXT("Speed entries"), Wander->WalkSpeedBySpecies.Num(), 6);
	TestEqual(TEXT("Raptor speed"), UGFWanderComponent::SpeedFor(TEXT("Raptor")), 480.f);
	TestEqual(TEXT("Nipper speed"), UGFWanderComponent::SpeedFor(TEXT("Nipper")), 320.f);
	TestEqual(TEXT("Unknown species uses 250"), UGFWanderComponent::SpeedFor(TEXT("NoSuchDino")), 250.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWildDinoNearRimeTest, "GFGame.WildDino.NearRime", GFWildDinoTests::Flags)
bool FGFWildDinoNearRimeTest::RunTest(const FString&)
{
	using namespace GFWildDinoTests;

	const AGFWildDinoDirector* Defaults = GetDefault<AGFWildDinoDirector>();
	const FGFTerritoryFaction* Rime = FindFaction(TEXT("Rime"));
	if (!TestNotNull(TEXT("Director config"), Defaults) || !TestNotNull(TEXT("Rime anchor"), Rime))
	{
		return false;
	}

	FWildWorld Scope;
	if (!Scope.Open(*this, TEXT("GFWildNearRime")))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFWildDinoDirector* Director = Scope.World->SpawnActor<AGFWildDinoDirector>(AGFWildDinoDirector::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Director spawned"), Director))
	{
		return false;
	}

	int32 Dinos = 0;
	int32 NearRime = 0;
	int32 Expected = 0;
	int32 ExpectedRime = 0;
	for (const FGFWildDinoSpawn& Entry : Defaults->Spawns)
	{
		Expected += FMath::Max(0, Entry.Count);
		if (Entry.AnchorId == TEXT("Rime"))
		{
			ExpectedRime += FMath::Max(0, Entry.Count);
		}
	}
	Expected = FMath::Min(Expected, Defaults->MaxAlive);

	AGFDino* Tusksaw = nullptr;
	for (TActorIterator<AGFDino> It(Scope.World); It; ++It)
	{
		AGFDino* Dino = *It;
		if (!Dino || !Dino->Taming)
		{
			continue;
		}
		++Dinos;
		const FGFWildDinoSpawn* Entry = FindSpecies(Defaults, Dino->Taming->Species);
		if (!TestNotNull(TEXT("Spawned species is in the table"), Entry))
		{
			continue;
		}
		const FGFTerritoryFaction* Faction = FindFaction(Entry->AnchorId);
		if (!TestNotNull(TEXT("Anchor"), Faction))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Dino->GetActorLocation(), Faction->Center);
		TestTrue(TEXT("Spawned on the anchor ring"), InRing(Dist, Entry->MinRadius, Entry->Radius));
		TestEqual(TEXT("Spawn stays Wild"), static_cast<int32>(Dino->Taming->GetTameState()), static_cast<int32>(EGFTameState::Wild));
		if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
		{
			TestEqual(TEXT("Species walk speed"), Move->MaxWalkSpeed, UGFWanderComponent::SpeedFor(Dino->Taming->Species));
		}
		if (Entry->AnchorId == TEXT("Rime"))
		{
			++NearRime;
			TestTrue(TEXT("Rime species"), Dino->Taming->Species == TEXT("Nipper") || Dino->Taming->Species == TEXT("Raptor"));
		}
		if (Dino->Taming->Species == TEXT("Tusksaw"))
		{
			Tusksaw = Dino;
		}
	}

	TestTrue(TEXT("At least one dino near Rime"), NearRime >= 1);
	TestEqual(TEXT("Rime count"), NearRime, ExpectedRime);
	TestEqual(TEXT("Alive count"), Dinos, Expected);

	if (TestNotNull(TEXT("Tusksaw spawned"), Tusksaw) && TestNotNull(TEXT("Tusksaw taming"), Tusksaw->Taming.Get()))
	{
		TestNull(TEXT("Tusksaw has no diet row"), Tusksaw->Taming->GetDietRow());
		TestEqual(TEXT("Tusksaw is not tameable"), static_cast<int32>(Tusksaw->Taming->ServerOffer(nullptr, TEXT("Berry"))), static_cast<int32>(EGFOfferResult::NotTameable));
		Tusksaw->Taming->TickComponent(0.1f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Tusksaw stays Wild"), static_cast<int32>(Tusksaw->Taming->GetTameState()), static_cast<int32>(EGFTameState::Wild));
		if (Tusksaw->GetCharacterMovement())
		{
			Tusksaw->GetCharacterMovement()->DisableMovement();
		}
		Tusksaw->Tick(0.1f);
		if (TestNotNull(TEXT("Tusksaw wander"), Tusksaw->Wander.Get()) && TestTrue(TEXT("Tusksaw picked a wander point"), Tusksaw->Wander->HasWanderTarget()))
		{
			const float WanderDist = FVector::Dist2D(Tusksaw->Wander->GetWanderTarget(), Tusksaw->Wander->GetHome());
			TestTrue(TEXT("Tusksaw wander target inside radius"), WanderDist <= Tusksaw->Wander->WanderRadius + 0.05f);
		}
	}

	FActorSpawnParameters Deferred = Params;
	Deferred.bDeferConstruction = true;
	AGFWildDinoDirector* Plain = Scope.World->SpawnActor<AGFWildDinoDirector>(AGFWildDinoDirector::StaticClass(), FTransform::Identity, Deferred);
	if (TestNotNull(TEXT("Plain-point director"), Plain))
	{
		const FVector There(2500.f, -800.f, 0.f);
		FGFWildDinoSpawn One;
		One.AnchorId = TEXT("NotAFaction");
		One.Species = TEXT("Nipper");
		One.Count = 1;
		One.Radius = 0.f;
		One.MinRadius = 0.f;
		One.Location = There;
		Plain->Spawns.Reset();
		Plain->Spawns.Add(One);
		Plain->MaxAlive = 1;
		Plain->FinishSpawning(FTransform::Identity);
		bool bFound = false;
		for (TActorIterator<AGFDino> It(Scope.World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), There) <= 1.f)
			{
				bFound = true;
				break;
			}
		}
		TestTrue(TEXT("Unknown anchor uses Location"), bFound);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWildDinoWanderTest, "GFGame.WildDino.Wander", GFWildDinoTests::Flags)
bool FGFWildDinoWanderTest::RunTest(const FString&)
{
	using namespace GFWildDinoTests;

	const FVector Home(400.f, -150.f, 25.f);
	FRandomStream Rng(11);
	for (int32 I = 0; I < 64; ++I)
	{
		const FVector Point = UGFWanderComponent::PickWanderPoint(Home, 800.f, Rng);
		TestTrue(TEXT("Disc point inside radius"), FVector::Dist2D(Point, Home) <= 800.f + 0.05f);
		TestEqual(TEXT("Disc keeps home Z"), Point.Z, Home.Z);
	}
	FRandomStream Zero(2);
	TestTrue(TEXT("Zero wander radius stays home"), UGFWanderComponent::PickWanderPoint(Home, 0.f, Zero).Equals(Home, 0.05f));

	FWildWorld Scope;
	if (!Scope.Open(*this, TEXT("GFWildWander")))
	{
		return false;
	}
	AGFDino* Dino = SpawnQuietDino(Scope.World, Home);
	if (!TestNotNull(TEXT("Dino"), Dino) || !TestNotNull(TEXT("Wander"), Dino->Wander.Get()))
	{
		return false;
	}
	Dino->SetSpecies(TEXT("Raptor"));
	UGFWanderComponent* Wander = Dino->Wander;
	Wander->SetHome(Home);
	Wander->WanderRadius = 800.f;
	Wander->WanderEveryMin = 0.f;
	Wander->WanderEveryMax = 0.f;

	Dino->SetAttackTarget(Dino);
	Wander->TickWander(0.1f);
	TestFalse(TEXT("No wander target while something is targeted"), Wander->HasWanderTarget());
	Dino->SetAttackTarget(nullptr);

	for (int32 I = 0; I < 24; ++I)
	{
		Wander->TickWander(0.1f);
		if (!TestTrue(TEXT("Wander target set"), Wander->HasWanderTarget()))
		{
			break;
		}
		const float Dist = FVector::Dist2D(Wander->GetWanderTarget(), Home);
		TestTrue(TEXT("Wander target stays inside the radius"), Dist <= Wander->WanderRadius + 0.05f);
		TestEqual(TEXT("Wander target Z"), Wander->GetWanderTarget().Z, Home.Z);
	}
	if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
	{
		TestEqual(TEXT("Raptor MaxWalkSpeed"), Move->MaxWalkSpeed, UGFWanderComponent::SpeedFor(TEXT("Raptor")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFWildDinoMissingDietTest, "GFGame.WildDino.MissingDiet", GFWildDinoTests::Flags)
bool FGFWildDinoMissingDietTest::RunTest(const FString&)
{
	using namespace GFWildDinoTests;

	FWildWorld Scope;
	if (!Scope.Open(*this, TEXT("GFWildMissingDiet")))
	{
		return false;
	}
	AGFDino* Dino = SpawnQuietDino(Scope.World, FVector(0.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("Dino"), Dino) || !TestNotNull(TEXT("Taming"), Dino->Taming.Get()))
	{
		return false;
	}
	Dino->SetSpecies(TEXT("Raptor"));
	TestNotNull(TEXT("Raptor diet row"), Dino->Taming->GetDietRow());
	Dino->SetSpecies(TEXT("Tusksaw"));
	TestNull(TEXT("Tusksaw diet row"), Dino->Taming->GetDietRow());
	TestEqual(TEXT("Offer is not tameable"), static_cast<int32>(Dino->Taming->ServerOffer(nullptr, TEXT("Berry"))), static_cast<int32>(EGFOfferResult::NotTameable));
	Dino->Taming->TickComponent(0.1f, LEVELTICK_All, nullptr);
	Dino->Tick(0.1f);
	TestEqual(TEXT("Still Wild"), static_cast<int32>(Dino->Taming->GetTameState()), static_cast<int32>(EGFTameState::Wild));
	if (TestNotNull(TEXT("Wander"), Dino->Wander.Get()) && TestTrue(TEXT("Still wanders"), Dino->Wander->HasWanderTarget()))
	{
		const float Dist = FVector::Dist2D(Dino->Wander->GetWanderTarget(), Dino->Wander->GetHome());
		TestTrue(TEXT("Missing-diet wander target inside radius"), Dist <= Dino->Wander->WanderRadius + 0.05f);
	}
	Dino->SetSpecies(TEXT("Lanternmaw"));
	TestNull(TEXT("Lanternmaw diet row"), Dino->Taming->GetDietRow());
	Dino->SetSpecies(TEXT("Scythepaw"));
	TestNull(TEXT("Scythepaw diet row"), Dino->Taming->GetDietRow());
	Dino->Taming->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Scythepaw stays Wild"), static_cast<int32>(Dino->Taming->GetTameState()), static_cast<int32>(EGFTameState::Wild));
	return true;
}

#endif
