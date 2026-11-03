#include "GFReturnToHub.h"

#include "GFArenaDirector.h"
#include "GFArenaSite.h"
#include "GFAssetMap.h"
#include "GFBeatHopCourse.h"
#include "GFDepthsDirector.h"
#include "GFFerrySpawn.h"
#include "GFHubGateDirector.h"
#include "GFLabDirector.h"
#include "GFLog.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "GFRunwayShow.h"
#include "GFRunwayStage.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "CoreGlobals.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

namespace
{
	TMap<TWeakObjectPtr<APawn>, double> ReturnCooldown;

	bool Cooling(UWorld* World, APawn* Pawn)
	{
		if (!World || !Pawn)
		{
			return false;
		}
		const double* Until = ReturnCooldown.Find(Pawn);
		return Until && World->GetTimeSeconds() < *Until;
	}

	void Hold(UWorld* World, APawn* Pawn)
	{
		if (World && Pawn)
		{
			ReturnCooldown.Add(Pawn, World->GetTimeSeconds() + 1.0);
		}
	}

	FString KeyOf(const APawn* Pawn)
	{
		const AGFPlayerState* PS = Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
		return PS ? PS->PlayerKey : FString();
	}

	AGFPlayerState* StateOf(APawn* Pawn)
	{
		return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
	}

	struct FLiveRound
	{
		bool bLab = false;
		bool bArena = false;
		bool bDepths = false;
		bool bBeat = false;
		bool bRunway = false;

		bool Any() const { return bLab || bArena || bDepths || bBeat || bRunway; }
	};

	FLiveRound Inspect(UWorld* World, APawn* Pawn)
	{
		FLiveRound Live;
		if (!World || !Pawn)
		{
			return Live;
		}
		const FString Key = KeyOf(Pawn);
		if (const AGFLabDirector* Lab = AGFLabDirector::Get(World, false))
		{
			Live.bLab = !Key.IsEmpty() && Lab->FindRun(Key) != nullptr;
		}
		if (const AGFArenaDirector* Arena = AGFArenaDirector::Get(World, false))
		{
			Live.bArena = Arena->Phase == EGFArenaPhase::Fighting;
		}
		if (const AGFDepthsDirector* Depths = AGFDepthsDirector::Get(World, false))
		{
			Live.bDepths = !Key.IsEmpty() && Depths->FindPlayer(Key) != nullptr;
		}
		for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
		{
			if (It->IsRunning())
			{
				Live.bBeat = true;
				break;
			}
		}
		if (const AGFRunwayStage* Stage = AGFRunwayStage::Find(World))
		{
			const EGFRunwayPhase Phase = Stage->GetPhase();
			Live.bRunway = Phase == EGFRunwayPhase::Dressing || Phase == EGFRunwayPhase::Walking || Phase == EGFRunwayPhase::Judging;
		}
		return Live;
	}

	void ApplyForfeit(UWorld* World, APawn* Pawn, const FLiveRound& Live, FGFReturnResult& Result)
	{
		if (!World || !Pawn || !Live.Any())
		{
			return;
		}
		AGFPlayerState* PS = StateOf(Pawn);
		if (Live.bLab)
		{
			if (AGFLabDirector* Lab = AGFLabDirector::Get(World, false))
			{
				Lab->OnModePlayerLeft(PS);
			}
			Result.Forfeit = TEXT("Lab");
		}
		if (Live.bArena)
		{
			if (AGFArenaDirector* Arena = AGFArenaDirector::Get(World, false))
			{
				Arena->AbortFight();
			}
			if (Result.Forfeit.IsNone())
			{
				Result.Forfeit = TEXT("Arena");
			}
		}
		if (AGFArenaSite* Site = AGFArenaSite::Get(World))
		{
			Site->ReleaseBalconyView(Pawn);
		}
		if (Live.bDepths && PS)
		{
			if (AGFDepthsDirector* Depths = AGFDepthsDirector::Get(World, false))
			{
				Depths->HandleModeRequest(PS, FGFModeRequest::Make(AGFDepthsDirector::ModeName, TEXT("Leave")));
			}
			if (Result.Forfeit.IsNone())
			{
				Result.Forfeit = TEXT("Depths");
			}
		}
		if (Live.bBeat)
		{
			for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
			{
				if (It->IsRunning())
				{
					It->ForfeitReturn();
				}
			}
			if (Result.Forfeit.IsNone())
			{
				Result.Forfeit = TEXT("BeatHop");
			}
		}
		if (Live.bRunway)
		{
			if (AGFRunwayStage* Stage = AGFRunwayStage::Find(World))
			{
				if (Stage->GetState().Contestants.Num() <= 1)
				{
					Stage->AbandonShow();
				}
			}
			if (Result.Forfeit.IsNone())
			{
				Result.Forfeit = TEXT("Runway");
			}
		}
	}

	FVector ResolveRime(const UWorld* World, const AGFReturnGate* Gate)
	{
		if (Gate)
		{
			return Gate->GetRimeCenter();
		}
		if (const AGFHubGateDirector* Director = GetDefault<AGFHubGateDirector>())
		{
			return Director->RimeAnchor(World);
		}
		return FVector::ZeroVector;
	}

	bool MapTravelFor(const UWorld* World, const AGFReturnGate* Gate)
	{
		if (Gate)
		{
			return Gate->IsMapTravel();
		}
		if (!World)
		{
			return false;
		}
		const FString Package = World->GetOutermost() ? World->GetOutermost()->GetName() : FString();
		if (FGFMiniGameCatalog::WorldIs(Package, FGFMiniGameCatalog::HubMap()))
		{
			return false;
		}
		if (FGFReturnToHub::IsAreaMap(Package))
		{
			return true;
		}
		for (const FGFMiniGameEntry& Entry : FGFMiniGameCatalog::All())
		{
			if (Entry.HasMap() && FGFMiniGameCatalog::WorldIs(Package, Entry.Map))
			{
				return true;
			}
		}
		return false;
	}

	bool CanOpenMap(const UWorld* World)
	{
		if (!World || !World->IsGameWorld() || GIsAutomationTesting)
		{
			return false;
		}
		const FString Package = World->GetOutermost() ? World->GetOutermost()->GetName() : FString();
		return Package.Contains(TEXT("/Game/Maps/"));
	}

	void TravelMap(UWorld* World, APawn* Pawn, bool bClientOnly, FGFReturnResult& Result)
	{
		const FString Map = FGFMiniGameCatalog::HubMap().ToString();
		Result.TravelMap = Map;
		Result.bMoved = true;
		Result.bClientTravel = bClientOnly;
		Result.bServerTravel = !bClientOnly;
		if (!CanOpenMap(World))
		{
			return;
		}
		if (bClientOnly)
		{
			if (APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
			{
				PC->ClientTravel(Map, TRAVEL_Absolute);
			}
			return;
		}
		const ENetMode Mode = World->GetNetMode();
		if (Mode == NM_ListenServer)
		{
			World->ServerTravel(Map + TEXT("?listen"));
		}
		else if (Mode == NM_DedicatedServer)
		{
			World->ServerTravel(Map);
		}
		else
		{
			UGameplayStatics::OpenLevel(World, FName(*Map));
		}
	}
}

const TCHAR* FGFReturnToHub::LabPrompt()
{
	return TEXT("Leave this shift? You evacuate and keep 75%.");
}

const TCHAR* FGFReturnToHub::RoundPrompt()
{
	return TEXT("Leave this round? You forfeit.");
}

FVector FGFReturnToHub::HubStand(const FVector& RimeCenter)
{
	return RimeCenter + FVector(0.f, 0.f, FGFPlayerStartRules::FallbackZ);
}

EGFReturnRoute FGFReturnToHub::RouteFor(bool bMapTravel, EGFReturnSeat Seat)
{
	if (!bMapTravel)
	{
		return EGFReturnRoute::Teleport;
	}
	if (Seat == EGFReturnSeat::RemoteClient)
	{
		return EGFReturnRoute::ClientTravel;
	}
	return EGFReturnRoute::ServerTravel;
}

EGFReturnSeat FGFReturnToHub::SeatOf(const APawn* Pawn)
{
	const UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return EGFReturnSeat::Standalone;
	}
	const ENetMode Mode = World->GetNetMode();
	if (Mode == NM_DedicatedServer)
	{
		return EGFReturnSeat::RemoteClient;
	}
	if (Mode == NM_ListenServer)
	{
		const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (PC && PC->IsLocalController())
		{
			return EGFReturnSeat::ListenHost;
		}
		return EGFReturnSeat::RemoteClient;
	}
	return EGFReturnSeat::Standalone;
}

FGFReturnResult FGFReturnToHub::FromKey(UWorld* World, APawn* Pawn, bool bConfirmed, const AGFReturnGate* Gate, const EGFReturnSeat* SeatOverride)
{
	return Execute(World, Pawn, bConfirmed, Gate, SeatOverride);
}

FGFReturnResult FGFReturnToHub::FromMenu(UWorld* World, APawn* Pawn, bool bConfirmed, const AGFReturnGate* Gate, const EGFReturnSeat* SeatOverride)
{
	return Execute(World, Pawn, bConfirmed, Gate, SeatOverride);
}

FGFReturnResult FGFReturnToHub::Execute(UWorld* World, APawn* Pawn, bool bConfirmed, const AGFReturnGate* Gate, const EGFReturnSeat* SeatOverride)
{
	FGFReturnResult Result;
	if (!World || !Pawn)
	{
		return Result;
	}
	if (!Gate)
	{
		Gate = AGFReturnGate::Nearest(World, Pawn->GetActorLocation());
	}
	const FLiveRound Live = Inspect(World, Pawn);
	if (Live.Any() && !bConfirmed)
	{
		Result.bNeedsConfirm = true;
		Result.Prompt = Live.bLab ? LabPrompt() : RoundPrompt();
		Result.Forfeit = Live.bLab ? FName(TEXT("Lab")) : (Live.bArena ? FName(TEXT("Arena")) : (Live.bDepths ? FName(TEXT("Depths")) : (Live.bBeat ? FName(TEXT("BeatHop")) : FName(TEXT("Runway")))));
		return Result;
	}
	if (Cooling(World, Pawn))
	{
		return Result;
	}

	const FVector Rime = ResolveRime(World, Gate);
	Result.Stand = HubStand(Rime);
	const bool bMap = MapTravelFor(World, Gate);
	const EGFReturnSeat Seat = SeatOverride ? *SeatOverride : SeatOf(Pawn);
	const EGFReturnRoute Route = RouteFor(bMap, Seat);

	if (Live.Any())
	{
		ApplyForfeit(World, Pawn, Live, Result);
	}
	else if (AGFArenaSite* Site = AGFArenaSite::Get(World))
	{
		Site->ReleaseBalconyView(Pawn);
	}

	if (Route == EGFReturnRoute::Teleport)
	{
		Pawn->SetActorLocation(Result.Stand, false, nullptr, ETeleportType::TeleportPhysics);
		Result.bMoved = true;
		Hold(World, Pawn);
		UE_LOG(LogGF, Log, TEXT("Return: %s is back on the hub stand"), *Pawn->GetName());
		return Result;
	}

	TravelMap(World, Pawn, Route == EGFReturnRoute::ClientTravel, Result);
	if (Result.bMoved)
	{
		Hold(World, Pawn);
		UE_LOG(LogGF, Log, TEXT("Return: %s %s to %s"), *Pawn->GetName(), Result.bClientTravel ? TEXT("client-travels") : TEXT("travels"), *Result.TravelMap);
	}
	return Result;
}

AGFReturnGate::AGFReturnGate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	LabelText->SetupAttachment(Root);
	LabelText->SetRelativeLocation(FVector(0.f, 0.f, 340.f));
	LabelText->SetHorizontalAlignment(EHTA_Center);
	LabelText->SetVerticalAlignment(EVRTA_TextCenter);
	LabelText->SetWorldSize(72.f);
	LabelText->SetTextRenderColor(FColor(20, 40, 70));
	LabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	// A step onto the pad, not a room. The old 320 cm box reached the ferry pass pad.
	Trigger->SetBoxExtent(FVector(70.f, 70.f, 120.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AGFReturnGate::OnTriggerBegin);
}

void AGFReturnGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFReturnGate, ModeId);
	DOREPLIFETIME(AGFReturnGate, RimeCenter);
	DOREPLIFETIME(AGFReturnGate, bMapTravel);
}

void AGFReturnGate::Configure(FName InModeId, const FVector& InRimeCenter, bool bInMapTravel)
{
	ModeId = InModeId;
	RimeCenter = InRimeCenter;
	bMapTravel = bInMapTravel;
}

void AGFReturnGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVisuals();
}

void AGFReturnGate::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisuals();
}

void AGFReturnGate::OnRep_Gate()
{
	ApplyVisuals();
}

void AGFReturnGate::ApplyVisuals()
{
	SignText = TEXT("HUB");
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(SignText));
		if (UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField")))
		{
			LabelText->SetFont(Font);
		}
	}
	if (Mesh)
	{
		UStaticMesh* Block = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube);
		Mesh->SetStaticMesh(Block);
		Mesh->SetRelativeScale3D(FVector(2.f, 1.f, 3.f));
		Mesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
		Mesh->SetMaterial(0, FGFAssetMap::MakeTint(Mesh, FLinearColor(0.93f, 0.96f, 1.f)));
	}
}

const TArray<FName>& FGFReturnToHub::AreaMaps()
{
	static const TArray<FName> Maps = {
		TEXT("/Game/Maps/L_World_Factions_A"),
		TEXT("/Game/Maps/L_Town_Verdant_A"),
		TEXT("/Game/Maps/L_Town_Cinderwright_A"),
		TEXT("/Game/Maps/L_Town_Rime_A"),
		TEXT("/Game/Maps/L_Homestead_Verdant_A"),
	};
	return Maps;
}

bool FGFReturnToHub::IsAreaMap(const FString& WorldPackage)
{
	for (const FName& Map : AreaMaps())
	{
		if (FGFMiniGameCatalog::WorldIs(WorldPackage, Map))
		{
			return true;
		}
	}
	return false;
}

FVector AGFReturnGate::AreaLocation(const FTransform& PlayerStart)
{
	const FVector Forward = PlayerStart.GetRotation().Rotator().Vector().GetSafeNormal2D();
	FVector At = PlayerStart.GetLocation() - (Forward.IsNearlyZero() ? FVector::ForwardVector : Forward) * AreaBehindCm;
	At.Z = PlayerStart.GetLocation().Z - StartHeightCm;
	return At;
}

FVector AGFReturnGate::MapLocation()
{
	return FVector(-500.f, BesideCm, 0.f);
}

FVector AGFReturnGate::PadLocation(const FGFMiniGameEntry& Entry, const FVector& InRime)
{
	return FGFMiniGameCatalog::PadOrigin(Entry, InRime) + FVector(0.f, BesideCm, 0.f);
}

AGFReturnGate* AGFReturnGate::Find(const UWorld* World, FName InModeId)
{
	if (!World || InModeId.IsNone())
	{
		return nullptr;
	}
	for (TActorIterator<AGFReturnGate> It(World); It; ++It)
	{
		if (It->ModeId == InModeId)
		{
			return *It;
		}
	}
	return nullptr;
}

AGFReturnGate* AGFReturnGate::Nearest(const UWorld* World, const FVector& At, float Radius)
{
	if (!World)
	{
		return nullptr;
	}
	AGFReturnGate* Best = nullptr;
	float BestDist = Radius;
	for (TActorIterator<AGFReturnGate> It(World); It; ++It)
	{
		const float Dist = FVector::Dist2D(It->GetActorLocation(), At);
		if (Dist <= BestDist)
		{
			BestDist = Dist;
			Best = *It;
		}
	}
	return Best;
}

FGFReturnResult AGFReturnGate::TryReturn(APawn* Pawn, bool bConfirmed)
{
	if (!HasAuthority() || !Pawn)
	{
		return FGFReturnResult();
	}
	FGFReturnResult Result = FGFReturnToHub::Execute(GetWorld(), Pawn, bConfirmed, this, nullptr);
	if (Result.bNeedsConfirm)
	{
		if (AGFPlayerController* PC = Cast<AGFPlayerController>(Pawn->GetController()))
		{
			PC->ShowReturnConfirm(Result.Prompt);
		}
	}
	return Result;
}

void AGFReturnGate::OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	(void)Overlapped;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)Sweep;
	APawn* Pawn = Cast<APawn>(Other);
	// Scripted tests teleport pawns onto pads. That is not a walk home.
	if (!Pawn || !Pawn->IsPlayerControlled() || GIsAutomationTesting)
	{
		return;
	}
	if (HasAuthority())
	{
		TryReturn(Pawn, false);
		return;
	}
	if (AGFPlayerController* PC = Cast<AGFPlayerController>(Pawn->GetController()))
	{
		PC->RequestReturnToHub(false);
	}
}
