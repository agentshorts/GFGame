// Townsfolk spawn, reach, and the interact key.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Townsfolk;Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFDefaultInputKeys.h"
#include "GFGameMode.h"
#include "GFPlayerController.h"
#include "GFTerritoryMap.h"
#include "GFTerritorySubsystem.h"
#include "GFTownsfolkDirector.h"
#include "GFTownsfolkRules.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "InputMappingContext.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTownsfolkTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Cinder(TEXT("Cinderwrights"));

	static int32 CountRole(const TArray<FGFTownsfolkSpot>& Spots, FName Town, EGFTownsfolkRole Role)
	{
		int32 Count = 0;
		for (const FGFTownsfolkSpot& Spot : Spots)
		{
			if (Spot.TownId == Town && Spot.Role == Role)
			{
				++Count;
			}
		}
		return Count;
	}

	static bool HasKey(const TArray<FGFMappedKey>& Keys, FName Action, FKey Key)
	{
		for (const FGFMappedKey& Row : Keys)
		{
			if (Row.Action == Action && Row.Key == Key)
			{
				return true;
			}
		}
		return false;
	}

	struct FInputWorld
	{
		UWorld* World = nullptr;

		~FInputWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		AGFCharacter* Spawn(FAutomationTestBase& T)
		{
			if (!T.TestNotNull(TEXT("Engine"), GEngine))
			{
				return nullptr;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFTownsfolkInput"));
			if (!T.TestNotNull(TEXT("Input world"), World))
			{
				return nullptr;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsfolkSpawnFallbackTest, "GFGame.Townsfolk.Spawn.Fallback", GFTownsfolkTests::Flags)
bool FGFTownsfolkSpawnFallbackTest::RunTest(const FString&)
{
	using namespace GFTownsfolkTests;

	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	const AGFTownsfolkDirector* Director = GetDefault<AGFTownsfolkDirector>();
	const AGFGameMode* GameMode = GetDefault<AGFGameMode>();
	if (!TestNotNull(TEXT("Territory config"), Territory) || !TestNotNull(TEXT("Townsfolk config"), Director) || !TestNotNull(TEXT("Game mode"), GameMode))
	{
		return false;
	}
	TestTrue(TEXT("Townsfolk director is a startup class"), GameMode->StartupDirectorClasses.Contains(FSoftClassPath(TEXT("/Script/GFGame.GFTownsfolkDirector"))));
	TestEqual(TEXT("Four villagers"), Director->VillagersPerTown, FGFTownsfolkRules::VillagersPerTown);
	TestEqual(TEXT("One elder"), Director->EldersPerTown, FGFTownsfolkRules::EldersPerTown);
	TestTrue(TEXT("Reach is 300 cm"), FMath::IsNearlyEqual(Director->InteractReach, FGFTownsfolkRules::InteractReachCm));
	TestTrue(TEXT("World map"), FGFTownsfolkRules::IsFactionsWorld(TEXT("/Game/Maps/L_World_Factions_A")));
	TestTrue(TEXT("PIE world map"), FGFTownsfolkRules::IsFactionsWorld(TEXT("UEDPIE_0_L_World_Factions_A")));
	TestFalse(TEXT("Hub is not the world map"), FGFTownsfolkRules::IsFactionsWorld(TEXT("/Game/Maps/L_Hub")));
	TestFalse(TEXT("Net test map is not the world map"), FGFTownsfolkRules::IsFactionsWorld(TEXT("/Game/Maps/L_NetTest")));

	TArray<FGFTownsfolkSpot> Spots;
	FGFTownsfolkRules::Resolve(Territory->FactionLayout, {}, Director->VillagerOffsets, Director->ElderOffset, Director->VillagersPerTown, Director->EldersPerTown, Spots);

	FGFTerritoryMap Map;
	Map.Curve = Territory->Curve;
	Map.Factions = Territory->FactionLayout;
	TestTrue(TEXT("Rime joins"), Map.SetMembers(Rime, 1));
	TestTrue(TEXT("Verdant joins"), Map.SetMembers(Verdant, 1));
	TestTrue(TEXT("Cinderwrights joins"), Map.SetMembers(Cinder, 1));

	const FName Towns[] = { Verdant, Rime, Cinder };
	for (FName Town : Towns)
	{
		TestEqual(*FString::Printf(TEXT("%s villagers"), *Town.ToString()), CountRole(Spots, Town, EGFTownsfolkRole::Villager), 4);
		TestEqual(*FString::Printf(TEXT("%s elders"), *Town.ToString()), CountRole(Spots, Town, EGFTownsfolkRole::Elder), 1);
		const FGFTerritoryFaction* Faction = Map.FindFaction(Town);
		if (!TestNotNull(*FString::Printf(TEXT("%s anchor"), *Town.ToString()), Faction))
		{
			continue;
		}
		const float Radius = Map.RadiusOf(*Faction);
		TestTrue(*FString::Printf(TEXT("%s camp has land"), *Town.ToString()), Radius > 0.f);
		for (const FGFTownsfolkSpot& Spot : Spots)
		{
			if (Spot.TownId != Town)
			{
				continue;
			}
			const FGFTerritoryZoneHit Zone = Map.GetZoneAt(Spot.Location);
			TestTrue(*FString::Printf(TEXT("%s spot is faction land"), *Town.ToString()), Zone.Zone == EGFTerritoryZone::Faction);
			TestEqual(*FString::Printf(TEXT("%s spot is in that town"), *Town.ToString()), Zone.FactionId, Town);
			TestTrue(*FString::Printf(TEXT("%s spot is inside the camp radius"), *Town.ToString()), FVector::Dist2D(Spot.Location, Faction->Center) <= Radius + 0.1f);
		}
	}
	TestEqual(TEXT("Three towns, five each"), Spots.Num(), 15);

	FGFTownsfolkMind Mind;
	Mind.TownCenter = FVector::ZeroVector;
	Mind.TownRadius = 500.f;
	Mind.Home = FVector(400.f, 0.f, 20.f);
	Mind.WanderRadius = 800.f;
	Mind.WalkSpeed = 300.f;
	Mind.StopDistance = 40.f;
	Mind.IdleMin = 0.05f;
	Mind.IdleMax = 0.05f;
	Mind.State = EGFTownsfolkBehavior::Idle;
	Mind.TimeLeft = 0.05f;
	FRandomStream Rng(11);
	FVector Location(900.f, 0.f, 20.f);
	bool bWandered = false;
	bool bLeftTown = false;
	for (int32 Step = 0; Step < 400; ++Step)
	{
		Mind.Advance(0.2f, Rng, Location);
		bLeftTown = bLeftTown || FVector::Dist2D(Location, Mind.TownCenter) > Mind.TownRadius + 0.05f;
		bWandered = bWandered || Mind.State == EGFTownsfolkBehavior::Wander;
	}
	TestFalse(TEXT("Wander stays inside the town radius"), bLeftTown);
	TestTrue(TEXT("They do wander"), bWandered);
	TestTrue(TEXT("A point outside the disc is pulled in"), FVector::Dist2D(Location, Mind.TownCenter) <= Mind.TownRadius + 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsfolkSpawnTaggedTest, "GFGame.Townsfolk.Spawn.Tagged", GFTownsfolkTests::Flags)
bool FGFTownsfolkSpawnTaggedTest::RunTest(const FString&)
{
	using namespace GFTownsfolkTests;

	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	const AGFTownsfolkDirector* Director = GetDefault<AGFTownsfolkDirector>();
	if (!TestNotNull(TEXT("Territory config"), Territory) || !TestNotNull(TEXT("Townsfolk config"), Director))
	{
		return false;
	}
	const FGFTerritoryFaction* RimeFaction = nullptr;
	for (const FGFTerritoryFaction& Faction : Territory->FactionLayout)
	{
		if (Faction.Id == Rime)
		{
			RimeFaction = &Faction;
			break;
		}
	}
	if (!TestNotNull(TEXT("Rime anchor"), RimeFaction))
	{
		return false;
	}

	const FVector TaggedAt(1234.f, 567.f, 89.f);
	FGFTownsfolkSpot Marker;
	TestTrue(TEXT("Spot tag"), FGFTownsfolkRules::SpotFromTags({ TEXT("Note"), FGFTownsfolkRules::VillagerSpotTag(), TEXT("Town:Rime") }, TaggedAt, Marker));
	TestEqual(TEXT("Tagged town"), Marker.TownId, Rime);
	TestTrue(TEXT("Tagged role"), Marker.Role == EGFTownsfolkRole::Villager);
	TestEqual(TEXT("Tagged location"), Marker.Location, TaggedAt);

	FGFTownsfolkSpot Ignored;
	TestFalse(TEXT("Tag without a town"), FGFTownsfolkRules::SpotFromTags({ FGFTownsfolkRules::VillagerSpotTag() }, TaggedAt, Ignored));
	TestFalse(TEXT("Town without a role"), FGFTownsfolkRules::SpotFromTags({ TEXT("Town:Verdant") }, TaggedAt, Ignored));
	TestFalse(TEXT("Both roles"), FGFTownsfolkRules::SpotFromTags({ FGFTownsfolkRules::VillagerSpotTag(), FGFTownsfolkRules::ElderSpotTag(), TEXT("Town:Rime") }, TaggedAt, Ignored));
	TestFalse(TEXT("Empty town id"), FGFTownsfolkRules::SpotFromTags({ FGFTownsfolkRules::ElderSpotTag(), TEXT("Town:") }, TaggedAt, Ignored));

	FGFTownsfolkSpot ElderMarker;
	TestTrue(TEXT("Elder spot"), FGFTownsfolkRules::SpotFromTags({ TEXT("Town:Verdant"), FGFTownsfolkRules::ElderSpotTag() }, FVector(1.f, 2.f, 3.f), ElderMarker));
	TestEqual(TEXT("Elder town"), ElderMarker.TownId, Verdant);
	TestTrue(TEXT("Elder role"), ElderMarker.Role == EGFTownsfolkRole::Elder);

	TArray<FGFTownsfolkSpot> Tagged;
	Tagged.Add(Marker);
	Tagged.Add(ElderMarker);
	TArray<FGFTownsfolkSpot> Spots;
	FGFTownsfolkRules::Resolve(Territory->FactionLayout, Tagged, Director->VillagerOffsets, Director->ElderOffset, Director->VillagersPerTown, Director->EldersPerTown, Spots);

	TestEqual(TEXT("Rime keeps the tagged villager only"), CountRole(Spots, Rime, EGFTownsfolkRole::Villager), 1);
	TestEqual(TEXT("Rime elder still falls back"), CountRole(Spots, Rime, EGFTownsfolkRole::Elder), 1);
	TestEqual(TEXT("Verdant villagers still fall back"), CountRole(Spots, Verdant, EGFTownsfolkRole::Villager), 4);
	TestEqual(TEXT("Verdant keeps the tagged elder only"), CountRole(Spots, Verdant, EGFTownsfolkRole::Elder), 1);
	TestEqual(TEXT("Cinderwrights villagers fall back"), CountRole(Spots, Cinder, EGFTownsfolkRole::Villager), 4);
	TestEqual(TEXT("Cinderwrights elder falls back"), CountRole(Spots, Cinder, EGFTownsfolkRole::Elder), 1);

	bool bFoundTagged = false;
	bool bFoundFallbackVillager = false;
	const bool bHasOffset = Director->VillagerOffsets.Num() > 0;
	for (const FGFTownsfolkSpot& Spot : Spots)
	{
		if (Spot.TownId == Rime && Spot.Role == EGFTownsfolkRole::Villager)
		{
			TestEqual(TEXT("Tagged point wins"), Spot.Location, TaggedAt);
			bFoundTagged = true;
		}
		if (bHasOffset && Spot.TownId == Rime && Spot.Role == EGFTownsfolkRole::Villager && Spot.Location.Equals(RimeFaction->Center + Director->VillagerOffsets[0], 0.1f))
		{
			bFoundFallbackVillager = true;
		}
	}
	TestTrue(TEXT("The tagged Rime villager is in the list"), bFoundTagged);
	TestFalse(TEXT("Rime fallback villagers are not added"), bFoundFallbackVillager);

	bool bVerdantElder = false;
	for (const FGFTownsfolkSpot& Spot : Spots)
	{
		if (Spot.TownId == Verdant && Spot.Role == EGFTownsfolkRole::Elder)
		{
			TestEqual(TEXT("Tagged elder wins"), Spot.Location, FVector(1.f, 2.f, 3.f));
			bVerdantElder = true;
		}
	}
	TestTrue(TEXT("The tagged Verdant elder is in the list"), bVerdantElder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsfolkInteractReachTest, "GFGame.Townsfolk.Interact.Reach", GFTownsfolkTests::Flags)
bool FGFTownsfolkInteractReachTest::RunTest(const FString&)
{
	const FVector Here(0.f, 0.f, 0.f);
	const FVector Near(299.f, 0.f, 0.f);
	const FVector Edge(300.f, 0.f, 0.f);
	const FVector Far(301.f, 0.f, 0.f);
	TestTrue(TEXT("299 cm is in reach"), FGFTownsfolkRules::InReach(Here, Near, FGFTownsfolkRules::InteractReachCm));
	TestTrue(TEXT("300 cm is in reach"), FGFTownsfolkRules::InReach(Here, Edge, FGFTownsfolkRules::InteractReachCm));
	TestFalse(TEXT("301 cm is out of reach"), FGFTownsfolkRules::InReach(Here, Far, FGFTownsfolkRules::InteractReachCm));
	TestFalse(TEXT("A negative reach never hits"), FGFTownsfolkRules::InReach(Here, Here, -1.f));

	TArray<FVector> Points;
	Points.Add(Far);
	Points.Add(FVector(100.f, 0.f, 0.f));
	Points.Add(FVector(50.f, 0.f, 0.f));
	TestEqual(TEXT("Nearest in reach"), FGFTownsfolkRules::NearestInReach(Here, Points, 300.f), 2);
	Points.Reset();
	Points.Add(Far);
	TestEqual(TEXT("Out of reach is none"), FGFTownsfolkRules::NearestInReach(Here, Points, 300.f), INDEX_NONE);
	TestEqual(TEXT("No points"), FGFTownsfolkRules::NearestInReach(Here, {}, 300.f), INDEX_NONE);

	TestEqual(TEXT("Rime elder title"), FGFTownsfolkRules::DisplayName(TEXT("Rime"), EGFTownsfolkRole::Elder), FString(TEXT("Rime Elder")));
	TestEqual(TEXT("Verdant villager title"), FGFTownsfolkRules::DisplayName(TEXT("Verdant"), EGFTownsfolkRole::Villager), FString(TEXT("Verdant Villager")));
	TestEqual(TEXT("Cinderwrights elder title"), FGFTownsfolkRules::DisplayName(TEXT("Cinderwrights"), EGFTownsfolkRole::Elder), FString(TEXT("Cinderwrights Elder")));

	TestTrue(TEXT("Idle can talk"), FGFTownsfolkMind::CanTransition(EGFTownsfolkBehavior::Idle, EGFTownsfolkBehavior::Talk));
	TestTrue(TEXT("Wander can talk"), FGFTownsfolkMind::CanTransition(EGFTownsfolkBehavior::Wander, EGFTownsfolkBehavior::Talk));
	TestTrue(TEXT("Talk returns to idle"), FGFTownsfolkMind::CanTransition(EGFTownsfolkBehavior::Talk, EGFTownsfolkBehavior::Idle));
	TestFalse(TEXT("Talk does not wander"), FGFTownsfolkMind::CanTransition(EGFTownsfolkBehavior::Talk, EGFTownsfolkBehavior::Wander));
	TestEqual(TEXT("Refused move stays"), FGFTownsfolkMind::Transition(EGFTownsfolkBehavior::Talk, EGFTownsfolkBehavior::Wander), EGFTownsfolkBehavior::Talk);

	FGFTownsfolkMind Mind;
	Mind.State = EGFTownsfolkBehavior::Wander;
	Mind.TownCenter = FVector::ZeroVector;
	Mind.TownRadius = 400.f;
	Mind.Home = FVector::ZeroVector;
	TestTrue(TEXT("Talk starts"), Mind.BeginTalk(1.f));
	TestTrue(TEXT("Talking"), Mind.State == EGFTownsfolkBehavior::Talk);
	TestTrue(TEXT("A second press refreshes talk"), Mind.BeginTalk(2.f));
	TestTrue(TEXT("Still talking"), FMath::IsNearlyEqual(Mind.TimeLeft, 2.f));
	FRandomStream Rng(3);
	FVector Location(10.f, 0.f, 0.f);
	Mind.Advance(2.1f, Rng, Location);
	TestTrue(TEXT("Talk ends in idle"), Mind.State == EGFTownsfolkBehavior::Idle);
	TestTrue(TEXT("Talk did not leave the disc"), FVector::Dist2D(Location, Mind.TownCenter) <= Mind.TownRadius + 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTownsfolkInputNoKeyClashTest, "GFGame.Townsfolk.Input.NoKeyClash", GFTownsfolkTests::Flags)
bool FGFTownsfolkInputNoKeyClashTest::RunTest(const FString&)
{
	using namespace GFTownsfolkTests;

	TArray<FGFMappedKey> Character;
	TArray<FGFMappedKey> Hud;
	FGFDefaultInputKeys::CharacterKeys(Character);
	FGFDefaultInputKeys::HudKeys(Hud, AGFPlayerController::DefaultReturnKey());
	TArray<FGFMappedKey> All = Character;
	All.Append(Hud);

	TestTrue(TEXT("X is only Interact"), FGFDefaultInputKeys::KeyIsOnly(All, EKeys::X, TEXT("Interact")));
	TestTrue(TEXT("Interact key is X"), FGFDefaultInputKeys::InteractKey() == EKeys::X);
	TestTrue(TEXT("Move is still W"), HasKey(Character, TEXT("Move"), EKeys::W));
	TestTrue(TEXT("Move is still A"), HasKey(Character, TEXT("Move"), EKeys::A));
	TestTrue(TEXT("Move is still S"), HasKey(Character, TEXT("Move"), EKeys::S));
	TestTrue(TEXT("Move is still D"), HasKey(Character, TEXT("Move"), EKeys::D));
	TestTrue(TEXT("Jump is still Space"), HasKey(Character, TEXT("Jump"), EKeys::SpaceBar));
	TestTrue(TEXT("Grapple is still Q"), HasKey(Character, TEXT("GrappleSwing"), EKeys::Q));
	TestTrue(TEXT("Yank is still E"), HasKey(Character, TEXT("GrappleYank"), EKeys::E));
	TestTrue(TEXT("Shoulder swap matches the pawn"), AGFCharacter::DefaultShoulderSwapKey() == EKeys::MiddleMouseButton);
	TestTrue(TEXT("Chat is still Enter"), HasKey(Hud, TEXT("Chat"), EKeys::Enter));
	TestTrue(TEXT("Hint is still H"), HasKey(Hud, TEXT("Hint"), EKeys::H));
	TestTrue(TEXT("Shardhop is still F"), HasKey(Hud, TEXT("Shardhop"), EKeys::F));
	TestTrue(TEXT("Villain is still G"), HasKey(Hud, TEXT("Villain"), EKeys::G));
	TestTrue(TEXT("Return is still Home"), HasKey(Hud, TEXT("Return"), EKeys::Home));
	TestFalse(TEXT("HUD does not use X"), HasKey(Hud, TEXT("Return"), EKeys::X));

	FInputWorld Input;
	AGFCharacter* Pawn = Input.Spawn(*this);
	if (!TestNotNull(TEXT("Pawn"), Pawn))
	{
		return false;
	}
	Pawn->BuildDefaultInput();
	if (!TestNotNull(TEXT("Default mapping"), Pawn->MappingContext.Get()))
	{
		return false;
	}
	const TArray<FEnhancedActionKeyMapping>& Mappings = Pawn->MappingContext->GetMappings();
	TestEqual(TEXT("Pawn mapping matches the key table"), Mappings.Num(), Character.Num());
	int32 InteractOnX = 0;
	for (int32 Index = 0; Index < Mappings.Num() && Index < Character.Num(); ++Index)
	{
		const FKey Bound = (Character[Index].Action == TEXT("ShoulderSwap")) ? AGFCharacter::DefaultShoulderSwapKey() : Character[Index].Key;
		TestEqual(*FString::Printf(TEXT("Key %d"), Index), Mappings[Index].Key, Bound);
		if (Mappings[Index].Key == EKeys::X)
		{
			++InteractOnX;
			TestEqual(TEXT("X is the interact action"), Mappings[Index].Action ? Mappings[Index].Action->GetFName() : NAME_None, FName(TEXT("IA_Interact")));
		}
	}
	TestEqual(TEXT("The built mapping binds X once"), InteractOnX, 1);
	return true;
}

#endif
