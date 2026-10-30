#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFAssetMap.h"
#include "GFSimpleAnimComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AGFDino::AGFDino()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	SetReplicateMovement(true);
	Taming = CreateDefaultSubobject<UGFTamingComponent>(TEXT("Taming"));
	Wander = CreateDefaultSubobject<UGFWanderComponent>(TEXT("Wander"));
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	Art = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Art"));
	Art->SetupAttachment(GetCapsuleComponent());
	Art->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Art->SetCanEverAffectNavigation(false);
	Art->SetVisibility(false);
	Art->SetHiddenInGame(true);
	Art->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// The character mesh stays unused; Art is the one DT_AssetMap fills in.
	if (USkeletalMeshComponent* DefaultMesh = GetMesh())
	{
		DefaultMesh->SetVisibility(false);
		DefaultMesh->SetHiddenInGame(true);
		DefaultMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SimpleAnim = CreateDefaultSubobject<UGFSimpleAnimComponent>(TEXT("SimpleAnim"));
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

FName AGFDino::AssetKey(FName Species)
{
	return FName(*FString::Printf(TEXT("Dino.%s"), *Species.ToString()));
}

void AGFDino::SetSpecies(FName InSpecies)
{
	Taming->Species = InSpecies;
	ApplyPlaceholder();
}

void AGFDino::ApplyPlaceholder()
{
	if (!Taming->Species.IsNone())
	{
		FGFAssetMap::ApplySkeletal(Art, Body, AssetKey(Taming->Species));
	}
}

void AGFDino::BeginPlay()
{
	Super::BeginPlay();
	ApplyPlaceholder();
	Taming->OnTameStateChanged.AddDynamic(this, &AGFDino::HandleTameStateChanged);
}

void AGFDino::HandleTameStateChanged(EGFTameState NewState)
{
	if (Body && !Body->GetStaticMesh())
	{
		ApplyPlaceholder(); // clients learn the species by replication
	}
}

void AGFDino::SetLeader(APawn* NewLeader)
{
	Leader = NewLeader;
}

void AGFDino::SetAttackTarget(AActor* Target)
{
	AttackTarget = Target;
}

EGFDinoState AGFDino::NextState(EGFDinoState Current, bool bTamed, bool bHasTarget, float DistToTarget, float InAttackRange,
	bool bHasLeader, float DistToLeader, float FollowStart, float FollowStop)
{
	if (bHasTarget)
	{
		return EGFDinoState::Attack; // Attack covers closing in and hitting.
	}
	if (bTamed && bHasLeader)
	{
		if (DistToLeader > FollowStart) return EGFDinoState::Follow;
		if (Current == EGFDinoState::Follow && DistToLeader > FollowStop) return EGFDinoState::Follow;
	}
	return Current == EGFDinoState::Rest ? EGFDinoState::Rest : EGFDinoState::Idle;
}

void AGFDino::MoveToward(const FVector& Location)
{
	const FVector Dir = (Location - GetActorLocation()).GetSafeNormal2D();
	AddMovementInput(Dir, 1.f);
}

void AGFDino::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return; // movement replicates
	}
	const UWorld* World = GetWorld();
	const bool bTamed = Taming->GetTameState() == EGFTameState::Tamed;
	if (bTamed)
	{
		AttackTarget = nullptr; // M6 arena / Hallow Night give it targets; never the players
		if (!Leader.IsValid())
		{
			Leader = UGFTamingComponent::FindTamerPawn(World, Taming->GetOwnerKey());
		}
	}
	else
	{
		AttackTarget = Taming->GetTameState() == EGFTameState::Hostile ? UGFTamingComponent::FindTamerPawn(World, Taming->GetTargetKey()) : nullptr;
	}

	AActor* Target = AttackTarget.Get();
	APawn* Lead = Leader.Get();
	const float DistT = Target ? FVector::Dist(Target->GetActorLocation(), GetActorLocation()) : 0.f;
	const float DistL = Lead ? FVector::Dist(Lead->GetActorLocation(), GetActorLocation()) : 0.f;
	DinoState = NextState(DinoState, bTamed, Target != nullptr, DistT, AttackRange, Lead != nullptr, DistL, FollowStartDistance, FollowStopDistance);

	switch (DinoState)
	{
	case EGFDinoState::Follow:
		MoveToward(Lead->GetActorLocation());
		break;
	case EGFDinoState::Attack:
		if (DistT > AttackRange)
		{
			MoveToward(Target->GetActorLocation());
		}
		else if (World->GetTimeSeconds() - LastAttackTime >= AttackCooldown)
		{
			LastAttackTime = World->GetTimeSeconds();
			// TODO(M2 polish): attack montage + knockout at low HP. Greybox: plain damage.
			UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, nullptr);
		}
		break;
	default:
		if (Wander)
		{
			Wander->TickWander(DeltaSeconds);
		}
		break;
	}
}
