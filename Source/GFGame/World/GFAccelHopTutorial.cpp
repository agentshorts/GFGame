#include "GFAccelHopTutorial.h"

#include "GFAccelHopRoad.h"
#include "GFAccelHopTile.h"
#include "GFCharacterMovementComponent.h"
#include "GFMiniGameCatalog.h"
#include "GFMovementWorldSubsystem.h"
#include "GFReturnToHub.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

AGFAccelHopTutorial::AGFAccelHopTutorial()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	// Placed with the map on every machine. A runtime spawn by the hub director (no map script yet) still reaches
	// clients this way. Nothing on it replicates: each machine runs its own player's lessons.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AGFAccelHopTutorial* AGFAccelHopTutorial::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFAccelHopTutorial> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FName AGFAccelHopTutorial::FinishGateTag()
{
	static const FName Tag(TEXT("AccelHopTutorial.Return.Finish"));
	return Tag;
}

void AGFAccelHopTutorial::BeginPlay()
{
	Super::BeginPlay();
	LandedHandle = UGFCharacterMovementComponent::OnAccelHopLanded.AddUObject(this, &AGFAccelHopTutorial::HandleLanded);
	RefreshLessons();
	if (HasAuthority())
	{
		if (UGFMovementWorldSubsystem* Movement = UGFMovementWorldSubsystem::Get(this))
		{
			Movement->SetStaminaOptIn(true);
		}
	}
}

void AGFAccelHopTutorial::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UGFCharacterMovementComponent::OnAccelHopLanded.Remove(LandedHandle);
	LandedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

AGFAccelHopRoad* AGFAccelHopTutorial::FindRoad() const
{
	if (Road.IsValid())
	{
		return Road.Get();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFAccelHopRoad> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AGFAccelHopTutorial::RefreshLessons()
{
	Road = FindRoad();
	LessonStartAlong.Init(MAX_flt, FGFAccelHopTutorialRules::LessonCount);
	UWorld* World = GetWorld();
	const AGFAccelHopRoad* OnRoad = Road.Get();
	if (!World || !OnRoad)
	{
		return;
	}
	for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
	{
		const int32 Lesson = FGFAccelHopTutorialRules::LessonFromTags(It->Tags);
		if (LessonStartAlong.IsValidIndex(Lesson))
		{
			LessonStartAlong[Lesson] = FMath::Min(LessonStartAlong[Lesson], OnRoad->AlongOf(It->GetActorLocation()));
		}
	}
}

FGFAccelHopTutorialLanding AGFAccelHopTutorial::DescribeLanding(const AGFAccelHopTile* Tile, EGFJudgement Judgement, bool bLit) const
{
	FGFAccelHopTutorialLanding Landing;
	Landing.Judgement = Judgement;
	Landing.bLit = bLit;
	if (!Tile)
	{
		return Landing;
	}
	Landing.LessonIndex = FGFAccelHopTutorialRules::LessonFromTags(Tile->Tags);
	const AGFAccelHopRoad* OnRoad = Tile->GetRoad() ? Tile->GetRoad() : FindRoad();
	const FTransform Frame = OnRoad ? OnRoad->GetActorTransform() : FTransform::Identity;
	const FVector Local = Frame.InverseTransformPositionNoScale(Tile->GetActorLocation());
	Landing.Side = FGFAccelHopTutorialRules::SideOf(static_cast<float>(Local.Y));
	return Landing;
}

void AGFAccelHopTutorial::NoteChange(bool bChanged)
{
	if (bChanged)
	{
		LastAdvanceTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}
}

bool AGFAccelHopTutorial::ApplyLanding(const FGFAccelHopTutorialLanding& Landing)
{
	const int32 Serial = Progress.Serial;
	FGFAccelHopTutorialRules::OnLanding(Progress, Landing);
	const bool bChanged = Progress.Serial != Serial;
	NoteChange(bChanged);
	return bChanged;
}

bool AGFAccelHopTutorial::ReachAlong(float Along)
{
	if (LessonStartAlong.Num() != FGFAccelHopTutorialRules::LessonCount)
	{
		return false;
	}
	const bool bChanged = FGFAccelHopTutorialRules::OnReachLesson(Progress, FGFAccelHopTutorialRules::LessonAtAlong(LessonStartAlong, Along));
	NoteChange(bChanged);
	return bChanged;
}

void AGFAccelHopTutorial::HandleLanded(const UGFCharacterMovementComponent* Movement, const FGFAccelHopLandingEvent& Event)
{
	if (!Movement || Movement->GetWorld() != GetWorld())
	{
		return;
	}
	const APawn* Pawn = Movement->GetPawnOwner();
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}
	ApplyLanding(DescribeLanding(Event.Tile.Get(), Event.Hit.Judgement, Event.bLit));
}

bool AGFAccelHopTutorial::LocalPawnLocation(FVector& Out) const
{
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return false;
	}
	Out = Pawn->GetActorLocation();
	return true;
}

void AGFAccelHopTutorial::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	NextReachCheck -= DeltaSeconds;
	if (NextReachCheck > 0.f)
	{
		return;
	}
	NextReachCheck = 0.25f;
	if (!Road.IsValid())
	{
		RefreshLessons();
	}
	FVector At;
	if (Road.IsValid() && LocalPawnLocation(At) && Road->ContainsPoint(At))
	{
		ReachAlong(Road->AlongOf(At));
	}
}

void AGFAccelHopTutorial::PlaceFinishGate()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !bHasFinishGate)
	{
		return;
	}
	const FName Id = FGFMiniGameCatalog::AccelHopTutorialId();
	AGFReturnGate* Home = nullptr;
	AGFReturnGate* Finish = nullptr;
	for (TActorIterator<AGFReturnGate> It(World); It; ++It)
	{
		if (It->GetModeId() != Id)
		{
			continue;
		}
		if (It->ActorHasTag(FinishGateTag()))
		{
			Finish = *It;
		}
		else if (!Home)
		{
			Home = *It;
		}
	}
	if (!Home)
	{
		return;
	}
	if (Finish)
	{
		Finish->Configure(Id, Home->GetRimeCenter(), Home->IsMapTravel());
		Finish->SetActorLocation(FinishGateLocation);
		Finish->ApplyVisuals();
		return;
	}
	const FTransform Xform(FRotator::ZeroRotator, FinishGateLocation);
	AGFReturnGate* Gate = World->SpawnActorDeferred<AGFReturnGate>(
		AGFReturnGate::StaticClass(), Xform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Gate)
	{
		return;
	}
	Gate->Configure(Id, Home->GetRimeCenter(), Home->IsMapTravel());
	Gate->Tags.AddUnique(FinishGateTag());
	Gate->FinishSpawning(Xform);
	Gate->ApplyVisuals();
}
