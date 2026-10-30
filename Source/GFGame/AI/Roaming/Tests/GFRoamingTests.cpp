// Home-zone leash, revenge-gang map access, and the travel hand-off.
// Maps come from the gate subsystems (MOBA = UGFMobaGateSubsystem, platformer = Beat Hop).
// Session size is faction count times AGFGameState::MaxPerFaction.

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
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

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
			Valley = NewObject<UGFTamingGateSubsystem>(GetTransientPackage())->GetMapName();
			Arena = NewObject<UGFMobaGateSubsystem>(GetTransientPackage())->GetMapName();
			Beat = NewObject<UGFBeatHopGateSubsystem>(GetTransientPackage())->GetMapName();
			Runway = NewObject<UGFFashionGateSubsystem>(GetTransientPackage())->GetMapName();
		}
	};

	static FGFDinoHomeZone DinoZoneHome()
	{
		FGFDinoHomeZone Zone;
		Zone.ZoneId = FName(TEXT("DinoZone"));
		Zone.Center = FVector::ZeroVector;
		Zone.RadiusCm = 2000.f;
		return Zone;
	}

	static FGFRoamingPolicy HomeRaptor(FName HomeMap)
	{
		FGFRoamingPolicy Policy;
		Policy.SetHome(DinoZoneHome(), HomeMap);
		Policy.SetSpecies(FName(TEXT("Raptor")));
		Policy.SetMemberId(FName(TEXT("Raptor.Home")));
		return Policy;
	}

	static FGFGangTarget HostTarget()
	{
		FGFGangTarget Target;
		Target.PlayerKey = TEXT("P1");
		Target.FactionId = GFRoamingFactions::Cinderwrights();
		Target.bIsHost = true;
		return Target;
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
	const FGFGangTarget Target = HostTarget();
	const int32 Seats = GFRoamingLimits::SeatsPerFaction(nullptr);

	TSharedPtr<FGFRevengeGangBoard> Live = MakeShared<FGFRevengeGangBoard>();
	Live->SetMap(Maps.Valley);
	Live->SetTarget(Target);
	FName Code = NAME_None;
	TestTrue(TEXT("Raptor joins"), Live->TryAdd(FName(TEXT("Raptor.1")), FName(TEXT("Raptor")), Seats, Code));
	TestTrue(TEXT("Rex joins"), Live->TryAdd(FName(TEXT("Rex.1")), FName(TEXT("Rex")), Seats, Code));

	FGFRoamingHandoff Record = Live->ExportHandoff(Maps.Beat);
	Live.Reset();

	TestTrue(TEXT("Record is waiting for travel"), Record.bPending);
	TestEqual(TEXT("Left the valley"), Record.FromMap, Maps.Valley);
	TestEqual(TEXT("Going to Beat Hop"), Record.ToMap, Maps.Beat);
	TestTrue(TEXT("Target copied"), Record.Target.SameAs(Target));
	TestEqual(TEXT("Both members copied"), Record.Members.Num(), 2);

	FGFRevengeGangBoard Arrived;
	TestTrue(TEXT("Destination consumes the record"), Arrived.ConsumeHandoff(Record));
	TestFalse(TEXT("Record is spent"), Record.bPending);
	TestTrue(TEXT("Target survived the map change"), Arrived.GetTarget().SameAs(Target));
	TestEqual(TEXT("Board is on Beat Hop"), Arrived.GetMap(), Maps.Beat);
	TestTrue(TEXT("Raptor still a member"), Arrived.Contains(FName(TEXT("Raptor.1"))));
	TestTrue(TEXT("Rex still a member"), Arrived.Contains(FName(TEXT("Rex.1"))));
	TestFalse(TEXT("Second consume does nothing"), Arrived.ConsumeHandoff(Record));
	TestTrue(TEXT("Target still the host"), Arrived.GetTarget().SameAs(Target));
	TestTrue(TEXT("Host flag kept"), Arrived.GetTarget().bIsHost);
	TestEqual(TEXT("Faction kept"), Arrived.GetTarget().FactionId, GFRoamingFactions::Cinderwrights());

	UGFRoamingSubsystem* Travel = NewObject<UGFRoamingSubsystem>(GetTransientPackage());
	TSharedPtr<FGFRevengeGangBoard> Board = Travel->EnsureBoard();
	Board->SetMap(Maps.Valley);
	Board->SetTarget(Target);
	TestTrue(TEXT("Subsystem roster"), Board->TryAdd(FName(TEXT("Raptor.1")), FName(TEXT("Raptor")), Seats, Code));
	TestTrue(TEXT("Begin travel"), Travel->BeginTravel(Maps.Beat));
	Board.Reset();
	TestFalse(TEXT("Live board dropped"), Travel->GetBoard().IsValid());
	TestTrue(TEXT("Pending target"), Travel->GetPending().bPending && Travel->GetPending().Target.SameAs(Target));
	TestTrue(TEXT("Consume travel"), Travel->ConsumeTravel());
	TestFalse(TEXT("Pending cleared"), Travel->GetPending().bPending);
	TestTrue(TEXT("New board has the target"), Travel->GetBoard().IsValid() && Travel->GetBoard()->GetTarget().SameAs(Target));
	TestEqual(TEXT("New board map"), Travel->GetBoard()->GetMap(), Maps.Beat);
	TestFalse(TEXT("Consume again is a no-op"), Travel->ConsumeTravel());
	TestTrue(TEXT("Target remains after the extra consume"), Travel->GetBoard()->GetTarget().SameAs(Target));

	UGFRoamingPolicyComponent* Gang = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Gang->SetMemberId(FName(TEXT("Raptor.1")));
	Gang->SetSpecies(FName(TEXT("Raptor")));
	Gang->SetHome(DinoZoneHome(), Maps.Valley);
	TestTrue(TEXT("Member adopts the new board"), Gang->AdoptBoard(Travel->GetBoard()));
	TestTrue(TEXT("Adopted member is gang"), Gang->IsRevengeGang());
	TestTrue(TEXT("Adopted member enters Beat Hop"), Gang->CanEnter(FGFRoamMap(Maps.Beat)));
	TestTrue(TEXT("Adopted member enters the MOBA"), Gang->CanEnterMap(Maps.Arena));
	TestTrue(TEXT("Component still names the host"), Gang->GetGangTarget().SameAs(Target));

	UGFRoamingPolicyComponent* Nipper = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Nipper->SetMemberId(FName(TEXT("Nipper.1")));
	Nipper->SetSpecies(FName(TEXT("Nipper")));
	Nipper->SetHome(DinoZoneHome(), Maps.Valley);
	TestFalse(TEXT("A dino that was not in the gang stays out"), Nipper->AdoptBoard(Travel->GetBoard()));
	TestFalse(TEXT("Nipper is not gang"), Nipper->IsRevengeGang());
	TestFalse(TEXT("Nipper still refused on Beat Hop"), Nipper->CanEnter(FGFRoamMap(Maps.Beat)));

	UGFRoamingSubsystem* Idle = NewObject<UGFRoamingSubsystem>(GetTransientPackage());
	Idle->EnsureBoard();
	TestFalse(TEXT("No target, no hand-off"), Idle->BeginTravel(Maps.Beat));
	TestTrue(TEXT("Board kept when travel has nothing to carry"), Idle->GetBoard().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRoamingSessionCapTest, "GFGame.Roaming.SessionCap", GFRoamingTests::Flags)
bool FGFRoamingSessionCapTest::RunTest(const FString&)
{
	const int32 Seats = GFRoamingLimits::SeatsPerFaction(nullptr);
	const AGFGameState* Defaults = GetDefault<AGFGameState>();
	if (!TestNotNull(TEXT("Game state defaults"), Defaults))
	{
		return false;
	}
	TestEqual(TEXT("Seats are MaxPerFaction"), Seats, Defaults->MaxPerFaction);
	TestTrue(TEXT("Seats read from the class default"), Seats > 0);
	const int32 Cap = GFRoamingLimits::MaxSessionPlayers(Seats);
	TestEqual(TEXT("Cap is factions times seats"), Cap, GFRoamingFactions::Count() * Seats);
	TestEqual(TEXT("Cap follows the seat count"), GFRoamingLimits::MaxSessionPlayers(Seats + 1), GFRoamingFactions::Count() * (Seats + 1));
	TestTrue(TEXT("Cinderwrights"), GFRoamingFactions::All().Contains(GFRoamingFactions::Cinderwrights()));
	TestTrue(TEXT("Verdant"), GFRoamingFactions::All().Contains(GFRoamingFactions::Verdant()));
	TestTrue(TEXT("Rime"), GFRoamingFactions::All().Contains(GFRoamingFactions::Rime()));

	FGFRevengeGangBoard Board;
	FName Code = NAME_None;
	int32 Added = 0;
	for (int32 Index = 0; Index < Cap; ++Index)
	{
		const FName Id(*FString::Printf(TEXT("Raptor.%d"), Index));
		if (Board.TryAdd(Id, FName(TEXT("Raptor")), Seats, Code))
		{
			++Added;
		}
	}
	TestEqual(TEXT("Roster fills the session"), Added, Cap);
	TestEqual(TEXT("Last add ok"), Code, GFRoamingCodes::Ok());
	TestFalse(TEXT("One past the cap is refused"), Board.TryAdd(FName(TEXT("Rex.Extra")), FName(TEXT("Rex")), Seats, Code));
	TestEqual(TEXT("Full"), Code, GFRoamingCodes::Full());
	TestEqual(TEXT("Full roster unchanged"), Board.GetMembers().Num(), Cap);
	if (Cap > 0)
	{
		TestTrue(TEXT("Same id is not a second seat"), Board.TryAdd(FName(TEXT("Raptor.0")), FName(TEXT("Raptor")), Seats, Code));
		TestEqual(TEXT("Already"), Code, GFRoamingCodes::Already());
		TestEqual(TEXT("Still at the cap"), Board.GetMembers().Num(), Cap);
	}
	TestFalse(TEXT("Missing id"), Board.TryAdd(NAME_None, FName(TEXT("Raptor")), Seats, Code));
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
	AGFPlayerState* Host = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Host"), Host))
	{
		return false;
	}
	Host->PlayerKey = TEXT("P1");
	Host->bIsHost = true;
	Host->SetPlayerId(101);
	Host->SetPlayerName(TEXT("P1"));
	if (GS->PlayerArray.Find(Host) == INDEX_NONE)
	{
		GS->AddPlayerState(Host);
	}
	TestEqual(TEXT("Claims Cinderwrights"), GS->ServerClaimFaction(Host, GFRoamingFactions::Cinderwrights()), FName(TEXT("Ok")));

	const FVector SpawnAt(0.f, 0.f, 100.f);
	FActorSpawnParameters Deferred = Params;
	Deferred.bDeferConstruction = true;
	AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), SpawnAt, FRotator::ZeroRotator, Deferred);
	if (!TestNotNull(TEXT("Raptor"), Dino))
	{
		return false;
	}
	Dino->AutoPossessAI = EAutoPossessAI::Disabled;
	Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, SpawnAt));
	Dino->SetSpecies(FName(TEXT("Raptor")));
	if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
	{
		Move->DisableMovement();
		Move->GravityScale = 0.f;
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

	UGFRoamingSubsystem* Travel = NewObject<UGFRoamingSubsystem>(GetTransientPackage());
	FName Code = NAME_None;
	const int32 Seats = GFRoamingLimits::SeatsPerFaction(GS);
	TestTrue(TEXT("Joins the gang"), Comp->JoinGang(Travel->EnsureBoard(), Seats, Code));
	TestTrue(TEXT("Target is the host player"), Comp->SetGangTargetFromPlayer(Host));
	TestTrue(TEXT("Target matches the player state"), Comp->GetGangTarget().SameAs(FGFGangTarget::FromPlayer(*Host)));
	TestTrue(TEXT("Find before travel"), GFRoamingBind::FindTarget(*GS, Comp->GetGangTarget()) == Host);

	TestTrue(TEXT("Travel to Beat Hop"), Travel->BeginTravel(Maps.Beat));
	TestTrue(TEXT("Arrive"), Travel->ConsumeTravel());
	if (!TestNotNull(TEXT("Board after travel"), Travel->GetBoard().Get()))
	{
		return false;
	}
	TestTrue(TEXT("Host still the target"), GFRoamingBind::FindTarget(*GS, Travel->GetBoard()->GetTarget()) == Host);

	UGFRoamingPolicyComponent* Arrived = NewObject<UGFRoamingPolicyComponent>(GetTransientPackage());
	Arrived->SetMemberId(MemberId);
	Arrived->SetSpecies(FName(TEXT("Raptor")));
	Arrived->SetHome(DinoZoneHome(), Maps.Valley);
	TestTrue(TEXT("Respawned member adopts"), Arrived->AdoptBoard(Travel->GetBoard()));
	TestTrue(TEXT("Follows onto the platformer"), Arrived->CanEnter(FGFRoamMap(Maps.Beat)));
	TestTrue(TEXT("And the MOBA"), Arrived->CanEnter(FGFRoamMap(Maps.Arena)));

	Host->FactionId = GFRoamingFactions::Verdant();
	TestTrue(TEXT("Faction drift drops the bind"), GFRoamingBind::FindTarget(*GS, Arrived->GetGangTarget()) == nullptr);
	Host->FactionId = GFRoamingFactions::Cinderwrights();
	TestTrue(TEXT("Faction restored"), GFRoamingBind::FindTarget(*GS, Arrived->GetGangTarget()) == Host);
	return true;
}

#endif
