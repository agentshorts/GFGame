// Home-zone leash, revenge-gang map access, and the travel hand-off.
// Map names come from the gate class defaults (MOBA = UGFMobaGateSubsystem, platformer = Beat Hop).
// The roster cap is AGameSession::MaxPlayers. <= 0 means no cap.
// Faction ids here are test-only (F1-F3). Production roaming code does not name factions.

#include "Misc/AutomationTest.h"
#include "../GFRoamingPolicyComponent.h"
#include "../GFRoamingSubsystem.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFDino.h"
#include "GFMobaGateSubsystem.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFTamingGateSubsystem.h"
#include "GFFashionGateSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Test-only faction ids. Opaque to production roaming code. */
namespace GFRoamingTestFactions
{
	inline FName F1() { return TEXT("F1"); }
	inline FName F2() { return TEXT("F2"); }
	inline FName F3() { return TEXT("F3"); }
}

namespace GFRoamingTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FMaps
	{
		FName Valley = NAME_None;
		FName Arena = NAME_None;
		FName Beat = NAME_None;
		FName Runway = NAME_None;

		FMaps()
		{
			Valley = GetDefault<UGFTamingGateSubsystem>()->GetMapName();
			Arena = GetDefault<UGFMobaGateSubsystem>()->GetMapName();
			Beat = GetDefault<UGFBeatHopGateSubsystem>()->GetMapName();
			Runway = GetDefault<UGFFashionGateSubsystem>()->GetMapName();
		}
	};

	/** UGameInstanceSubsystem refuses a transient-package outer on UE 5.8. */
	struct FSubsystemOuter
	{
		TStrongObjectPtr<UGameInstance> Instance;

		FSubsystemOuter()
		{
			Instance.Reset(NewObject<UGameInstance>(GetTransientPackage()));
		}

		UGFRoamingSubsystem* NewTravel() const
		{
			return NewObject<UGFRoamingSubsystem>(Instance.Get());
		}
	};

	static FGFSafeCircles DinoZoneHome()
	{
		FGFSafeRegion Region;
		Region.Id = FName(TEXT("DinoZone"));
		Region.Center = FVector::ZeroVector;
		Region.Members = 1;
		Region.BaseRadius = 2000.f;
		Region.RadiusPerExtraMember = 0.f;
		Region.MaxRadius = 2000.f;
		return FGFSafeCircles({ Region });
	}

	static FGFRoamingPolicy HomeRaptor(FName HomeMap)
	{
		FGFRoamingPolicy Policy;
		Policy.SetHome(DinoZoneHome(), HomeMap);
		Policy.SetSpecies(FName(TEXT("Raptor")));
		Policy.SetMemberId(FName(TEXT("Raptor.Home")));
		return Policy;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingHomeZoneTest, "GFGame.Roaming.HomeZone", GFRoamingTests::Flags)
bool FGFRoamingHomeZoneTest::RunTest(const FString&)
{
	using namespace GFRoamingTests;
	const FMaps Maps;
	TestEqual(TEXT("Valley map is the taming gate"), Maps.Valley, FName(TEXT("/Game/Maps/L_Valley")));
	TestEqual(TEXT("MOBA map is the moba gate"), Maps.Arena, FName(TEXT("/Game/Maps/L_Arena")));
	TestEqual(TEXT("Platformer map is Beat Hop"), Maps.Beat, FName(TEXT("/Game/Maps/L_Beat")));

	const FGFRoamingPolicy Policy = HomeRaptor(Maps.Valley);
	const FGFRoamZone HomeZone(FName(TEXT("DinoZone")));
	const FGFRoamZone OpenWorld(FName(TEXT("OpenWorld")));
	const FGFRoamZone MobaZone(FName(TEXT("Moba")));
	const FGFRoamZone RhythmZone(FName(TEXT("Rhythm")));
	const FVector Inside(100.f, 0.f, 20.f);
	const FVector Outside(6000.f, 8000.f, 40.f);

	TestFalse(TEXT("Not the gang"), Policy.IsRevengeGang());
	TestTrue(TEXT("Home zone"), Policy.CanEnter(HomeZone));
	TestFalse(TEXT("Other zone refused"), Policy.CanEnter(OpenWorld));
	TestFalse(TEXT("MOBA zone refused"), Policy.CanEnter(MobaZone));
	TestFalse(TEXT("Rhythm zone refused"), Policy.CanEnter(RhythmZone));
	TestTrue(TEXT("Home map"), Policy.CanEnter(FGFRoamMap(Maps.Valley)));
	TestFalse(TEXT("MOBA map refused"), Policy.CanEnter(FGFRoamMap(Maps.Arena)));
	TestFalse(TEXT("Beat Hop map refused"), Policy.CanEnter(FGFRoamMap(Maps.Beat)));
	TestFalse(TEXT("Runway map refused"), Policy.CanEnter(FGFRoamMap(Maps.Runway)));
	TestTrue(TEXT("Point inside the circle"), Policy.CanPathTo(Inside));
	TestFalse(TEXT("Point outside the circle"), Policy.CanPathTo(Outside));

	const FGFFollowDecision InZone = Policy.DecideFollow(Inside, HomeZone, FGFRoamMap(Maps.Valley));
	TestTrue(TEXT("Follow inside the home"), InZone.bMove);
	TestTrue(TEXT("Inside goal unchanged"), InZone.Goal.Equals(Inside));

	const FGFFollowDecision Edge = Policy.DecideFollow(Outside, HomeZone, FGFRoamMap(Maps.Valley));
	TestTrue(TEXT("Same zone stops on the circle"), Edge.bMove);
	TestTrue(TEXT("Clamped to the radius"), FMath::IsNearlyEqual(FVector::Dist2D(Edge.Goal, FVector::ZeroVector), 2000.f, 0.05f));
	TestTrue(TEXT("Height kept"), FMath::IsNearlyEqual(Edge.Goal.Z, Outside.Z, 0.01f));
	TestFalse(TEXT("Clamp is not the outside point"), Edge.Goal.Equals(Outside));

	const FGFFollowDecision OtherZone = Policy.DecideFollow(Inside, OpenWorld, FGFRoamMap(Maps.Valley));
	TestFalse(TEXT("No follow into another zone"), OtherZone.bMove);
	const FGFFollowDecision OtherMap = Policy.DecideFollow(Inside, HomeZone, FGFRoamMap(Maps.Arena));
	TestFalse(TEXT("No follow onto the MOBA map"), OtherMap.bMove);
	const FGFFollowDecision BeatMap = Policy.DecideFollow(Inside, RhythmZone, FGFRoamMap(Maps.Beat));
	TestFalse(TEXT("No follow onto Beat Hop"), BeatMap.bMove);

	UGFRoamingPolicyComponent* Comp = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Comp->SetHome(DinoZoneHome(), Maps.Valley);
	Comp->SetSpecies(FName(TEXT("Raptor")));
	TestFalse(TEXT("Component not gang"), Comp->CanEnter(FGFRoamMap(Maps.Beat)));
	TestFalse(TEXT("Component zone refused"), Comp->CanEnterZone(FName(TEXT("OpenWorld"))));
	TestTrue(TEXT("Component home zone"), Comp->CanEnterZone(FName(TEXT("DinoZone"))));
	TestFalse(TEXT("Component MOBA map"), Comp->CanEnterMap(Maps.Arena));
	TestTrue(TEXT("Component home map"), Comp->CanEnterMap(Maps.Valley));
	TestFalse(TEXT("Component outside point"), Comp->CanPathTo(Outside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingRevengeGangTest, "GFGame.Roaming.RevengeGang", GFRoamingTests::Flags)
bool FGFRoamingRevengeGangTest::RunTest(const FString&)
{
	using namespace GFRoamingTests;
	const FMaps Maps;
	FGFRoamingPolicy Policy = HomeRaptor(Maps.Valley);
	Policy.SetRevengeGang(true);
	const FVector Far(6000.f, 8000.f, 40.f);

	TestTrue(TEXT("Gang zone DinoZone"), Policy.CanEnter(FGFRoamZone(FName(TEXT("DinoZone")))));
	TestTrue(TEXT("Gang zone OpenWorld"), Policy.CanEnter(FGFRoamZone(FName(TEXT("OpenWorld")))));
	TestTrue(TEXT("Gang zone Moba"), Policy.CanEnter(FGFRoamZone(FName(TEXT("Moba")))));
	TestTrue(TEXT("Gang zone Rhythm"), Policy.CanEnter(FGFRoamZone(FName(TEXT("Rhythm")))));
	TestTrue(TEXT("Gang valley map"), Policy.CanEnter(FGFRoamMap(Maps.Valley)));
	TestTrue(TEXT("Gang MOBA map"), Policy.CanEnter(FGFRoamMap(Maps.Arena)));
	TestTrue(TEXT("Gang Beat Hop map"), Policy.CanEnter(FGFRoamMap(Maps.Beat)));
	TestTrue(TEXT("Gang runway map"), Policy.CanEnter(FGFRoamMap(Maps.Runway)));
	TestTrue(TEXT("Gang paths outside its old circle"), Policy.CanPathTo(Far));
	TestTrue(TEXT("Gang does not pull the goal back"), Policy.ClampFollowGoal(Far).Equals(Far));

	const FGFFollowDecision Chase = Policy.DecideFollow(Far, FGFRoamZone(FName(TEXT("Rhythm"))), FGFRoamMap(Maps.Beat));
	TestTrue(TEXT("Gang follows onto Beat Hop"), Chase.bMove);
	TestTrue(TEXT("Gang goal is the raw point"), Chase.Goal.Equals(Far));
	TestTrue(TEXT("That point is outside the home circle"), FVector::Dist2D(Chase.Goal, FVector::ZeroVector) > 2000.f);

	const FGFFollowDecision Moba = Policy.DecideFollow(Far, FGFRoamZone(FName(TEXT("Moba"))), FGFRoamMap(Maps.Arena));
	TestTrue(TEXT("Gang follows onto the MOBA"), Moba.bMove);
	TestTrue(TEXT("MOBA goal unchanged"), Moba.Goal.Equals(Far));

	FGFRoamingPolicy StillHome = HomeRaptor(Maps.Valley);
	TestFalse(TEXT("Same species, not in the gang"), StillHome.IsRevengeGang());
	TestFalse(TEXT("Still refused on Beat Hop"), StillHome.CanEnter(FGFRoamMap(Maps.Beat)));

	UGFRoamingPolicyComponent* Comp = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Comp->SetHome(DinoZoneHome(), Maps.Valley);
	Comp->SetRevengeGang(true);
	TestTrue(TEXT("Component gang map"), Comp->CanEnter(FGFRoamMap(Maps.Arena)));
	TestTrue(TEXT("Component gang zone"), Comp->CanEnter(FGFRoamZone(FName(TEXT("Rhythm")))));
	TestTrue(TEXT("Component gang path"), Comp->CanPathTo(Far));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingHandoffTest, "GFGame.Roaming.Handoff", GFRoamingTests::Flags)
bool FGFRoamingHandoffTest::RunTest(const FString&)
{
	using namespace GFRoamingTests;
	const FMaps Maps;
	const FString Target = TEXT("P1");
	// Explicit 0 (no cap): this test is about handoff, not the roster limit.
	const int32 Cap = 0;

	TSharedPtr<FGFRevengeGangBoard> Live = MakeShared<FGFRevengeGangBoard>();
	Live->SetMap(Maps.Valley);
	Live->SetTargetKey(Target);
	FName Code = NAME_None;
	TestTrue(TEXT("Raptor joins"), Live->TryAdd(FName(TEXT("Raptor.1")), FName(TEXT("Raptor")), Cap, Code));
	TestTrue(TEXT("Rex joins"), Live->TryAdd(FName(TEXT("Rex.1")), FName(TEXT("Rex")), Cap, Code));

	FGFRoamingHandoff Record = Live->ExportHandoff(Maps.Beat);
	Live.Reset();

	TestTrue(TEXT("Record is waiting for travel"), Record.bPending);
	TestEqual(TEXT("Left the valley"), Record.FromMap, Maps.Valley);
	TestEqual(TEXT("Going to Beat Hop"), Record.ToMap, Maps.Beat);
	TestEqual(TEXT("Target copied"), Record.TargetKey, Target);
	TestEqual(TEXT("Both members copied"), Record.Members.Num(), 2);

	FGFRevengeGangBoard Arrived;
	TestTrue(TEXT("Destination consumes the record"), Arrived.ConsumeHandoff(Record));
	TestFalse(TEXT("Record is spent"), Record.bPending);
	TestEqual(TEXT("Target survived the map change"), Arrived.GetTargetKey(), Target);
	TestEqual(TEXT("Board is on Beat Hop"), Arrived.GetMap(), Maps.Beat);
	TestTrue(TEXT("Raptor still a member"), Arrived.Contains(FName(TEXT("Raptor.1"))));
	TestTrue(TEXT("Rex still a member"), Arrived.Contains(FName(TEXT("Rex.1"))));
	TestFalse(TEXT("Second consume does nothing"), Arrived.ConsumeHandoff(Record));
	TestEqual(TEXT("Target still set"), Arrived.GetTargetKey(), Target);

	const FSubsystemOuter Outer;
	UGFRoamingSubsystem* Travel = Outer.NewTravel();
	TSharedPtr<FGFRevengeGangBoard> Board = Travel->EnsureBoard();
	Board->SetMap(Maps.Valley);
	Board->SetTargetKey(Target);
	TestTrue(TEXT("Subsystem roster"), Board->TryAdd(FName(TEXT("Raptor.1")), FName(TEXT("Raptor")), Cap, Code));
	TestFalse(TEXT("Someone else travelling leaves the gang"), Travel->BeginTravel(Maps.Beat, TEXT("P2")));
	TestTrue(TEXT("Board kept for another traveller"), Travel->GetBoard().IsValid());
	TestFalse(TEXT("Pending stays clear"), Travel->GetPending().bPending);
	TestTrue(TEXT("Begin travel when the target goes"), Travel->BeginTravel(Maps.Beat, Target));
	Board.Reset();
	TestFalse(TEXT("Live board dropped"), Travel->GetBoard().IsValid());
	TestTrue(TEXT("Pending target"), Travel->GetPending().bPending && Travel->GetPending().TargetKey == Target);
	TestTrue(TEXT("Consume travel"), Travel->ConsumeTravel());
	TestFalse(TEXT("Pending cleared"), Travel->GetPending().bPending);
	TestTrue(TEXT("New board has the target"), Travel->GetBoard().IsValid() && Travel->GetBoard()->GetTargetKey() == Target);
	TestEqual(TEXT("New board map"), Travel->GetBoard()->GetMap(), Maps.Beat);
	TestFalse(TEXT("Consume again is a no-op"), Travel->ConsumeTravel());
	TestEqual(TEXT("Target remains after the extra consume"), Travel->GetBoard()->GetTargetKey(), Target);

	UGFRoamingPolicyComponent* Gang = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Gang->SetMemberId(FName(TEXT("Raptor.1")));
	Gang->SetSpecies(FName(TEXT("Raptor")));
	Gang->SetHome(DinoZoneHome(), Maps.Valley);
	TestTrue(TEXT("Member adopts the new board"), Gang->AdoptBoard(Travel->GetBoard()));
	TestTrue(TEXT("Adopted member is gang"), Gang->IsRevengeGang());
	TestTrue(TEXT("Adopted member enters Beat Hop"), Gang->CanEnter(FGFRoamMap(Maps.Beat)));
	TestTrue(TEXT("Adopted member enters the MOBA"), Gang->CanEnterMap(Maps.Arena));
	TestEqual(TEXT("Component still names the target"), Gang->GetGangTargetKey(), Target);

	UGFRoamingPolicyComponent* Nipper = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Nipper->SetMemberId(FName(TEXT("Nipper.1")));
	Nipper->SetSpecies(FName(TEXT("Nipper")));
	Nipper->SetHome(DinoZoneHome(), Maps.Valley);
	TestFalse(TEXT("A dino that was not in the gang stays out"), Nipper->AdoptBoard(Travel->GetBoard()));
	TestFalse(TEXT("Nipper is not gang"), Nipper->IsRevengeGang());
	TestFalse(TEXT("Nipper still refused on Beat Hop"), Nipper->CanEnter(FGFRoamMap(Maps.Beat)));

	const FSubsystemOuter IdleOuter;
	UGFRoamingSubsystem* Idle = IdleOuter.NewTravel();
	Idle->EnsureBoard();
	TestFalse(TEXT("No target, no hand-off"), Idle->BeginTravel(Maps.Beat, Target));
	TestTrue(TEXT("Board kept when travel has nothing to carry"), Idle->GetBoard().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingSessionCapTest, "GFGame.Roaming.SessionCap", GFRoamingTests::Flags)
bool FGFRoamingSessionCapTest::RunTest(const FString&)
{
	const AGameSession* Session = GetDefault<AGameSession>();
	if (!TestNotNull(TEXT("Game session defaults"), Session))
	{
		return false;
	}
	const int32 Cap = GFRoamingLimits::MaxPlayers(nullptr);
	TestEqual(TEXT("Cap is MaxPlayers"), Cap, Session->MaxPlayers);

	FName Code = NAME_None;
	FGFRevengeGangBoard Open;
	const int32 Uncapped = FMath::Max(Cap, 0) + 3;
	int32 OpenAdded = 0;
	for (int32 Index = 0; Index < Uncapped; ++Index)
	{
		const FName Id(*FString::Printf(TEXT("Raptor.Open.%d"), Index));
		if (Open.TryAdd(Id, FName(TEXT("Raptor")), 0, Code))
		{
			++OpenAdded;
		}
	}
	TestEqual(TEXT("Zero is no cap"), OpenAdded, Uncapped);
	TestEqual(TEXT("Zero stays ok"), Code, GFRoamingCodes::Ok());
	TestTrue(TEXT("Negative is no cap"), Open.TryAdd(FName(TEXT("Rex.Open")), FName(TEXT("Rex")), -1, Code));
	TestEqual(TEXT("Negative stays ok"), Code, GFRoamingCodes::Ok());

	if (Cap > 0)
	{
		FGFRevengeGangBoard Board;
		int32 Added = 0;
		for (int32 Index = 0; Index < Cap; ++Index)
		{
			const FName Id(*FString::Printf(TEXT("Raptor.%d"), Index));
			if (Board.TryAdd(Id, FName(TEXT("Raptor")), Cap, Code))
			{
				++Added;
			}
		}
		TestEqual(TEXT("Roster fills MaxPlayers"), Added, Cap);
		TestEqual(TEXT("Last add ok"), Code, GFRoamingCodes::Ok());
		TestFalse(TEXT("One past the cap is refused"), Board.TryAdd(FName(TEXT("Rex.Extra")), FName(TEXT("Rex")), Cap, Code));
		TestEqual(TEXT("Full"), Code, GFRoamingCodes::Full());
		TestEqual(TEXT("Full roster unchanged"), Board.GetMembers().Num(), Cap);
		TestTrue(TEXT("Same id is not a second seat"), Board.TryAdd(FName(TEXT("Raptor.0")), FName(TEXT("Raptor")), Cap, Code));
		TestEqual(TEXT("Already"), Code, GFRoamingCodes::Already());
		TestEqual(TEXT("Still at the cap"), Board.GetMembers().Num(), Cap);
	}
	else
	{
		FGFRevengeGangBoard Board;
		TestTrue(TEXT("Uncapped default adds"), Board.TryAdd(FName(TEXT("Raptor.0")), FName(TEXT("Raptor")), Cap, Code));
		TestTrue(TEXT("Uncapped default keeps adding"), Board.TryAdd(FName(TEXT("Rex.Extra")), FName(TEXT("Rex")), Cap, Code));
		TestEqual(TEXT("Uncapped default is not full"), Code, GFRoamingCodes::Ok());
		TestEqual(TEXT("Both members kept"), Board.GetMembers().Num(), 2);
	}

	FGFRevengeGangBoard Missing;
	TestFalse(TEXT("Missing id"), Missing.TryAdd(NAME_None, FName(TEXT("Raptor")), Cap, Code));
	TestEqual(TEXT("No dino"), Code, GFRoamingCodes::NoDino());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingBindTest, "GFGame.Roaming.Bind", GFRoamingTests::Flags)
bool FGFRoamingBindTest::RunTest(const FString&)
{
	using namespace GFRoamingTests;
	struct FWorldScope
	{
		UWorld* World = nullptr;
		explicit FWorldScope(UWorld* InWorld) : World(InWorld) {}
		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFRoamingBind"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	AGFGameState* GS = World->SpawnActor<AGFGameState>();
	if (GS)
	{
		World->SetGameState(GS);
	}
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto SpawnPlayer = [&](const TCHAR* Key, int32 PlayerId, FName Faction) -> AGFPlayerState*
	{
		AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		if (!PS)
		{
			return nullptr;
		}
		PS->PlayerKey = Key;
		PS->bIsHost = PlayerId == 101;
		PS->SetPlayerId(PlayerId);
		PS->SetPlayerName(Key);
		if (GS->PlayerArray.Find(PS) == INDEX_NONE)
		{
			GS->AddPlayerState(PS);
		}
		TestEqual(TEXT("Claims a test faction"), GS->ServerClaimFaction(PS, Faction), FName(TEXT("Ok")));
		return PS;
	};
	AGFPlayerState* Host = SpawnPlayer(TEXT("P1"), 101, GFRoamingTestFactions::F1());
	AGFPlayerState* Guest = SpawnPlayer(TEXT("P2"), 102, GFRoamingTestFactions::F2());
	AGFPlayerState* Third = SpawnPlayer(TEXT("P3"), 103, GFRoamingTestFactions::F3());
	if (!TestNotNull(TEXT("Host"), Host) || !TestNotNull(TEXT("Guest"), Guest) || !TestNotNull(TEXT("Third"), Third))
	{
		return false;
	}
	TestEqual(TEXT("F1"), Host->FactionId, GFRoamingTestFactions::F1());
	TestEqual(TEXT("F2"), Guest->FactionId, GFRoamingTestFactions::F2());
	TestEqual(TEXT("F3"), Third->FactionId, GFRoamingTestFactions::F3());

	const FVector SpawnAt(0.f, 0.f, 100.f);
	FActorSpawnParameters Deferred = Params;
	Deferred.bDeferConstruction = true;
	auto SpawnDino = [&](const FVector& At) -> AGFDino*
	{
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), At, FRotator::ZeroRotator, Deferred);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->AutoPossessAI = EAutoPossessAI::Disabled;
		Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, At));
		Dino->SetSpecies(FName(TEXT("Raptor")));
		if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
		{
			Move->DisableMovement();
			Move->GravityScale = 0.f;
		}
		return Dino;
	};

	AGFDino* Dino = SpawnDino(SpawnAt);
	if (!TestNotNull(TEXT("Raptor"), Dino) || !TestNotNull(TEXT("Taming"), Dino->Taming))
	{
		return false;
	}

	const FMaps Maps;
	const FName MemberId(TEXT("Raptor.1"));
	UGFRoamingPolicyComponent* Comp = UGFRoamingPolicyComponent::AttachToDino(Dino, DinoZoneHome(), Maps.Valley, MemberId);
	if (!TestNotNull(TEXT("Roaming component"), Comp))
	{
		return false;
	}
	TestEqual(TEXT("Species from the dino"), Comp->GetSpecies(), FName(TEXT("Raptor")));
	TestEqual(TEXT("Stable member id"), Comp->GetMemberId(), MemberId);

	AGFDino* Twin = SpawnDino(SpawnAt + FVector(200.f, 0.f, 0.f));
	if (!TestNotNull(TEXT("Second raptor"), Twin))
	{
		return false;
	}
	UGFRoamingPolicyComponent* NoId = UGFRoamingPolicyComponent::AttachToDino(Twin, DinoZoneHome(), Maps.Valley);
	if (!TestNotNull(TEXT("Second roaming component"), NoId))
	{
		return false;
	}
	TestEqual(TEXT("Species copied"), NoId->GetSpecies(), FName(TEXT("Raptor")));
	TestTrue(TEXT("Species is not a member id"), NoId->GetMemberId().IsNone());

	const FSubsystemOuter Outer;
	UGFRoamingSubsystem* Travel = Outer.NewTravel();
	FName Code = NAME_None;
	// Explicit 0 (no cap): this test is about binding, not the roster limit.
	const int32 Cap = 0;
	TestTrue(TEXT("Joins the gang"), Comp->JoinGang(Travel->EnsureBoard(), Cap, Code));
	TestFalse(TEXT("No taming target yet"), Comp->SetGangTargetFromTaming(Dino->Taming));
	Dino->Taming->ServerForceHostile(Host->PlayerKey);
	TestEqual(TEXT("Taming target is the host"), Dino->Taming->GetTargetKey(), Host->PlayerKey);
	TestTrue(TEXT("Gang takes the taming target"), Comp->SetGangTargetFromTaming(Dino->Taming));
	TestEqual(TEXT("Target key"), Comp->GetGangTargetKey(), Host->PlayerKey);
	TestTrue(TEXT("Find before travel"), GFRoamingBind::FindTarget(*GS, Comp->GetGangTargetKey()) == Host);
	TestTrue(TEXT("Guest is not the target"), GFRoamingBind::FindTarget(*GS, Guest->PlayerKey) == Guest);
	TestTrue(TEXT("Gang is not chasing the guest"), Comp->GetGangTargetKey() != Guest->PlayerKey);

	TestFalse(TEXT("Guest travelling does not take the gang"), Travel->BeginTravel(Maps.Beat, Guest->PlayerKey));
	TestTrue(TEXT("Board kept"), Travel->GetBoard().IsValid());
	TestTrue(TEXT("Travel to Beat Hop with the target"), Travel->BeginTravel(Maps.Beat, Host->PlayerKey));
	TestTrue(TEXT("Arrive"), Travel->ConsumeTravel());
	if (!TestNotNull(TEXT("Board after travel"), Travel->GetBoard().Get()))
	{
		return false;
	}
	TestEqual(TEXT("Target key survived"), Travel->GetBoard()->GetTargetKey(), Host->PlayerKey);
	TestTrue(TEXT("Host still the target"), GFRoamingBind::FindTarget(*GS, Travel->GetBoard()->GetTargetKey()) == Host);

	UGFRoamingPolicyComponent* Arrived = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Arrived->SetMemberId(MemberId);
	Arrived->SetSpecies(FName(TEXT("Raptor")));
	Arrived->SetHome(DinoZoneHome(), Maps.Valley);
	TestTrue(TEXT("Respawned member adopts"), Arrived->AdoptBoard(Travel->GetBoard()));
	TestTrue(TEXT("Follows onto the platformer"), Arrived->CanEnter(FGFRoamMap(Maps.Beat)));
	TestTrue(TEXT("And the MOBA"), Arrived->CanEnter(FGFRoamMap(Maps.Arena)));
	TestEqual(TEXT("Adopted component keeps the key"), Arrived->GetGangTargetKey(), Host->PlayerKey);

	Host->FactionId = GFRoamingTestFactions::F3();
	TestTrue(TEXT("Faction change keeps the bind"), GFRoamingBind::FindTarget(*GS, Arrived->GetGangTargetKey()) == Host);
	Host->bLoggedOut = true;
	TestTrue(TEXT("Logged-out player is not a target"), GFRoamingBind::FindTarget(*GS, Arrived->GetGangTargetKey()) == nullptr);
	Host->bLoggedOut = false;
	TestTrue(TEXT("Login restores the bind"), GFRoamingBind::FindTarget(*GS, Arrived->GetGangTargetKey()) == Host);
	return true;
}

#endif
