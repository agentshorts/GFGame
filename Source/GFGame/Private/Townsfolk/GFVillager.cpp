#include "GFVillager.h"
#include "GFAnimMap.h"
#include "GFAssetMap.h"
#include "GFSimpleAnimComponent.h"
#include "GFWanderComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

namespace GFVillagerPrivate
{
	FLinearColor TownColor(FName TownId)
	{
		if (TownId == TEXT("Rime"))
		{
			return FLinearColor(0.623529f, 0.847059f, 0.960784f);
		}
		if (TownId == TEXT("Verdant"))
		{
			return FLinearColor(0.556863f, 0.831373f, 0.478431f);
		}
		if (TownId == TEXT("Cinderwrights"))
		{
			return FLinearColor(0.850980f, 0.329412f, 0.168627f);
		}
		return FLinearColor(0.55f, 0.55f, 0.55f);
	}

	FName AnimKey(EGFTownsfolkRole Role, const TCHAR* Kind)
	{
		const TCHAR* Who = (Role == EGFTownsfolkRole::Elder) ? TEXT("Elder") : TEXT("Villager");
		return FName(*FString::Printf(TEXT("%s.%s"), Who, Kind));
	}
}

AGFVillager::AGFVillager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, 88.f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RootComponent = Capsule;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Capsule);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);

	Wander = CreateDefaultSubobject<UGFWanderComponent>(TEXT("Wander"));
	SimpleAnim = CreateDefaultSubobject<UGFSimpleAnimComponent>(TEXT("SimpleAnim"));
}

void AGFVillager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFVillager, TownId);
	DOREPLIFETIME(AGFVillager, TownsfolkRole);
	DOREPLIFETIME(AGFVillager, Behavior);
	DOREPLIFETIME(AGFVillager, ReachCm);
	DOREPLIFETIME(AGFVillager, Shout);
}

FOnGFVillagerHire& AGFVillager::OnHireAsked()
{
	static FOnGFVillagerHire Delegate;
	return Delegate;
}

FString AGFVillager::DisplayTitle() const
{
	return FGFTownsfolkRules::DisplayName(TownId, TownsfolkRole);
}

void AGFVillager::InitTownsfolk(const FGFTownsfolkSpawnParams& Params)
{
	TownId = Params.TownId;
	TownsfolkRole = Params.Role;
	ReachCm = Params.Reach > 0.f ? Params.Reach : FGFTownsfolkRules::InteractReachCm;
	TalkSeconds = FMath::Max(0.f, Params.TalkSeconds);
	Mind.Home = GetActorLocation();
	Mind.TownCenter = Params.TownCenter;
	Mind.TownRadius = FMath::Max(0.f, Params.TownRadius);
	Mind.WanderRadius = FMath::Max(0.f, Params.WanderRadius);
	Mind.WalkSpeed = FMath::Max(0.f, Params.WalkSpeed);
	Mind.StopDistance = FMath::Max(0.f, Params.StopDistance);
	Mind.IdleMin = FMath::Max(0.f, Params.IdleMin);
	Mind.IdleMax = FMath::Max(0.f, Params.IdleMax);
	Mind.State = EGFTownsfolkBehavior::Idle;
	const int32 Salt = GetTypeHash(TownId) ^ FMath::RoundToInt(Mind.Home.X) ^ (FMath::RoundToInt(Mind.Home.Y) << 8);
	Rng.Initialize(Salt ^ 0x70E11);
	Mind.TimeLeft = Mind.PickIdleWait(Rng);
	Behavior = Mind.State;
	if (Wander)
	{
		Wander->SetHome(Mind.Home);
		Wander->WanderRadius = Mind.WanderRadius;
	}
	const FVector Clamped = FGFTownsfolkRules::ClampToDisc(Mind.TownCenter, GetActorLocation(), Mind.TownRadius);
	SetActorLocation(Clamped);
	Mind.Home = GetActorLocation();
	ApplyVisuals();
}

FText AGFVillager::GetInteractPrompt() const
{
	return FText::FromString(FString::Printf(TEXT("Talk to %s"), *DisplayTitle()));
}

bool AGFVillager::CanInteract(APawn* Pawn) const
{
	if (!Pawn || Pawn == this)
	{
		return false;
	}
	const float Reach = ReachCm > 0.f ? ReachCm : FGFTownsfolkRules::InteractReachCm;
	return FGFTownsfolkRules::InReach(Pawn->GetActorLocation(), GetActorLocation(), Reach);
}

void AGFVillager::Interact(APawn* Pawn)
{
	if (!HasAuthority() || !CanInteract(Pawn))
	{
		return;
	}
	if (!Mind.BeginTalk(TalkSeconds))
	{
		return;
	}
	Behavior = Mind.State;
	if (Pawn)
	{
		const FVector Dir = (Pawn->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			SetActorRotation(Dir.Rotation());
		}
	}
	PlayTalkClip();
	OnInteract.Broadcast(this, Pawn);
}

bool AGFVillager::BeginWarn(const FVector& Thief, const FVector& Elder)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!Mind.BeginWarn(Thief, Elder))
	{
		return false;
	}
	Behavior = Mind.State;
	Shout = FGFTownsfolkRules::WarnShout();
	const FVector Dir = (Thief - GetActorLocation()).GetSafeNormal2D();
	if (!Dir.IsNearlyZero())
	{
		SetActorRotation(Dir.Rotation());
	}
	return true;
}

void AGFVillager::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisuals();
}

void AGFVillager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || TownId.IsNone())
	{
		return;
	}
	FVector Location = GetActorLocation();
	const EGFTownsfolkPulse Pulse = Mind.Advance(DeltaSeconds, Rng, Location);
	if (!Location.Equals(GetActorLocation(), 0.1f))
	{
		SetActorLocation(Location);
	}
	if (Mind.State == EGFTownsfolkBehavior::Warn)
	{
		const FVector Dir = (Mind.FacePoint - GetActorLocation()).GetSafeNormal2D();
		if (!Dir.IsNearlyZero())
		{
			SetActorRotation(Dir.Rotation());
		}
		if (Shout.IsEmpty())
		{
			Shout = FGFTownsfolkRules::WarnShout();
		}
	}
	else if (!Shout.IsEmpty())
	{
		Shout.Reset();
	}
	if (Behavior != Mind.State)
	{
		Behavior = Mind.State;
	}
	if (Pulse == EGFTownsfolkPulse::AskHire && TownsfolkRole == EGFTownsfolkRole::Villager)
	{
		OnHireAsked().Broadcast(this);
	}
}

void AGFVillager::OnRep_Behavior()
{
	if (Behavior == EGFTownsfolkBehavior::Talk)
	{
		PlayTalkClip();
	}
}

void AGFVillager::ApplyVisuals()
{
	if (Body)
	{
		if (UStaticMesh* Mesh = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube))
		{
			Body->SetStaticMesh(Mesh);
		}
		const bool bElder = TownsfolkRole == EGFTownsfolkRole::Elder;
		const float Width = bElder ? 0.62f : 0.5f;
		const float Height = bElder ? 1.7f : 1.35f;
		Body->SetRelativeScale3D(FVector(Width, Width, Height));
		Body->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
		Body->SetMaterial(0, FGFAssetMap::MakeTint(Body, GFVillagerPrivate::TownColor(TownId)));
	}
	if (SimpleAnim)
	{
		const FName IdleKey = GFVillagerPrivate::AnimKey(TownsfolkRole, TEXT("Idle"));
		const FName MoveKey = GFVillagerPrivate::AnimKey(TownsfolkRole, TEXT("Move"));
		if (FGFAnimMap::Find(IdleKey))
		{
			SimpleAnim->SetAnims(IdleKey, FGFAnimMap::Find(MoveKey) ? MoveKey : NAME_None);
		}
	}
}

void AGFVillager::PlayTalkClip()
{
	if (!SimpleAnim)
	{
		return;
	}
	const FName Key = GFVillagerPrivate::AnimKey(TownsfolkRole, TEXT("Talk"));
	if (!FGFAnimMap::Find(Key))
	{
		return;
	}
	UAnimSequenceBase* Clip = FGFAnimMap::Load(Key);
	USkeletalMeshComponent* Mesh = SimpleAnim->FindArtMesh();
	if (!Clip || !Mesh)
	{
		return;
	}
	// The talk clip claims the emote slot for the talk, or for the clip when there is no talk time.
	SimpleAnim->ClaimClip(EGFAnimOwner::Emote, Key, false, TalkSeconds > 0.f ? TalkSeconds : Clip->GetPlayLength());
}
