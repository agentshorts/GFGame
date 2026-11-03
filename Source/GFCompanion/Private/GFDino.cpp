#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFTamer.h"
#include "GFAssetMap.h"
#include "GFSimpleAnimComponent.h"
#include "GFProtectedActor.h"
#include "GFInventoryComponent.h"
#include "GFLog.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

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
	// Same defaults as UGFMovementTuning stair fields. BeginPlay copies the asset through the game-module hook.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxStepHeight = 105.f;
		Move->SetWalkableFloorAngle(50.f);
	}
}

namespace GFDinoReadyHook
{
	static FGFDinoReadyHook Installed = nullptr;
}

void GFSetDinoReadyHook(FGFDinoReadyHook Hook)
{
	GFDinoReadyHook::Installed = Hook;
}

FName AGFDino::AssetKey(FName Species)
{
	return FName(*FString::Printf(TEXT("Dino.%s"), *Species.ToString()));
}

void AGFDino::SetSpecies(FName InSpecies)
{
	Taming->Species = InSpecies;
	ApplyPlaceholder();
	RefreshVitals();
}

void AGFDino::SetDropSeed(int32 Seed)
{
	DropSeed = Seed;
	bDropSeedPinned = true;
}

float AGFDino::HitPointsFor(FName Species)
{
	const AGFDino* Defaults = GetDefault<AGFDino>();
	const float Fallback = Defaults && Defaults->DefaultHitPoints > 0.f ? Defaults->DefaultHitPoints : FGFDinoKill::DefaultHitPoints;
	if (Defaults)
	{
		if (const float* Found = Defaults->HitPointsBySpecies.Find(Species))
		{
			return *Found > 0.f ? *Found : Fallback;
		}
	}
	return Fallback;
}

void AGFDino::RefreshVitals()
{
	if (!HasAuthority() || bVitalsLocked)
	{
		return;
	}
	Kill.Reset(HitPointsFor(Taming ? Taming->Species : NAME_None));
	CopyVitals();
}

void AGFDino::CopyVitals()
{
	Life = Kill.Life;
	HitPoints = Kill.HitPoints;
}

FString AGFDino::AttackerKeyFrom(AController* EventInstigator, AActor* DamageCauser)
{
	if (EventInstigator)
	{
		if (const IGFTamer* Tamer = Cast<IGFTamer>(EventInstigator->PlayerState))
		{
			if (!Tamer->GetTamerKey().IsEmpty())
			{
				return Tamer->GetTamerKey();
			}
		}
	}
	if (!DamageCauser)
	{
		return FString();
	}
	if (const IGFTamer* Tamer = Cast<IGFTamer>(DamageCauser))
	{
		return Tamer->GetTamerKey();
	}
	if (const APawn* Pawn = Cast<APawn>(DamageCauser))
	{
		if (const IGFTamer* Tamer = Cast<IGFTamer>(Pawn->GetPlayerState()))
		{
			return Tamer->GetTamerKey();
		}
	}
	if (const AController* By = DamageCauser->GetInstigatorController())
	{
		if (const IGFTamer* Tamer = Cast<IGFTamer>(By->PlayerState))
		{
			return Tamer->GetTamerKey();
		}
	}
	return FString();
}

void AGFDino::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFDino, Life);
	DOREPLIFETIME(AGFDino, HitPoints);
}

void AGFDino::GrantKill(const FGFDinoKillResult& Result)
{
	if (!Result.bKilled || bKillGranted || !HasAuthority())
	{
		return;
	}
	bKillGranted = true;
	if (UWorld* World = GetWorld())
	{
		if (IGFTamer* Tamer = UGFTamingComponent::FindTamer(World, Result.KillerKey))
		{
			AActor* Holder = nullptr;
			if (APawn* Pawn = Tamer->GetTamerPawn())
			{
				Holder = Pawn->GetPlayerState();
			}
			if (UGFInventoryComponent* Inv = Holder ? Holder->FindComponentByClass<UGFInventoryComponent>() : nullptr)
			{
				for (const FGFItemStack& Stack : Result.Stacks)
				{
					Inv->AddItem(Stack.ItemId, Stack.Count);
				}
			}
		}
	}
	const FName Species = Taming ? Taming->Species : NAME_None;
	UE_LOG(LogGF, Log, TEXT("DinoKill: %s down, killer %s, %d stacks"), *Species.ToString(), *Result.KillerKey, Result.Stacks.Num());
	FGFDinoKill::OnDinoKilled().Broadcast(this, Result.KillerKey, Species);
}

float AGFDino::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	(void)DamageEvent;
	if (!HasAuthority() || DamageAmount <= 0.f || !Taming || Kill.Life != EGFDinoLife::Alive)
	{
		return 0.f;
	}
	Taming->ServerNotifyHit();
	bVitalsLocked = true;
	const bool bTamed = Taming->GetTameState() == EGFTameState::Tamed;
	const FGFDinoKillResult Result = Kill.ApplyHit(Taming->Species, DamageAmount, AttackerKeyFrom(EventInstigator, DamageCauser), DropSeed, bTamed, bRental);
	CopyVitals();
	if (Result.bKilled)
	{
		GrantKill(Result);
	}
	return Result.Applied;
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
	if (HasAuthority() && !bDropSeedPinned)
	{
		DropSeed = FMath::RandRange(1, 1000000000);
	}
	RefreshVitals();
	Taming->OnTameStateChanged.AddDynamic(this, &AGFDino::HandleTameStateChanged);
	if (GFDinoReadyHook::Installed)
	{
		GFDinoReadyHook::Installed(this);
	}
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
	// Faction-protected actors (the ferry, a rider in flight) are never a target.
	if (FGFProtectedActor::Is(Target))
	{
		AttackTarget = nullptr;
		return;
	}
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
	if (Kill.Life == EGFDinoLife::Downed)
	{
		const FGFDinoKillResult Step = Kill.Advance(DeltaSeconds);
		CopyVitals();
		if (Step.bNowDead)
		{
			Destroy();
		}
		return;
	}
	if (Kill.Life == EGFDinoLife::Dead)
	{
		Destroy();
		return;
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

	if (FGFProtectedActor::Is(AttackTarget.Get()))
	{
		AttackTarget = nullptr;
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
