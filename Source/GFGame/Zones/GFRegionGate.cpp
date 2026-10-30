#include "GFRegionGate.h"

#include "GFCharacter.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFPlayerState.h"
#include "GFShardTravelSubsystem.h"
#include "GFZoneLayout.h"
#include "GFZoneTypes.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GFAssetMap.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"

namespace
{
	constexpr float GateCooldownSeconds = 3.f;
}

AGFRegionGate::AGFRegionGate()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetupAttachment(RootComponent);
	Box->SetBoxExtent(FVector(200.f, 400.f, 300.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
}

void AGFRegionGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFRegionGate, RegionId);
	DOREPLIFETIME(AGFRegionGate, GateId);
	DOREPLIFETIME(AGFRegionGate, bGateEnabled);
	DOREPLIFETIME(AGFRegionGate, Color);
}

void AGFRegionGate::Setup(const FGFRegionSlot& Slot)
{
	RegionId = Slot.RegionId;
	GateId = Slot.GateId;
	bGateEnabled = Slot.bEnabled;
	Color = Slot.Color;
	ApplyVisual();
}

void AGFRegionGate::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisual();
	if (HasAuthority() && Box)
	{
		Box->OnComponentBeginOverlap.AddDynamic(this, &AGFRegionGate::OnGateOverlap);
	}
}

void AGFRegionGate::OnRep_Color()
{
	ApplyVisual();
}

void AGFRegionGate::ApplyVisual()
{
	if (!Mesh)
	{
		return;
	}
	if (UStaticMesh* Asset = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube))
	{
		Mesh->SetStaticMesh(Asset);
	}
	if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Mesh, Color))
	{
		Mesh->SetMaterial(0, Tint);
	}
	const FVector Scale(2.f, 8.f, 6.f);
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Scale.Z * 50.f));
}

void AGFRegionGate::OnGateOverlap(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	(void)Overlapped;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)Sweep;
	if (!HasAuthority())
	{
		return;
	}
	AGFCharacter* Character = Cast<AGFCharacter>(Other);
	AGFPlayerState* PS = Character ? Character->GetPlayerState<AGFPlayerState>() : nullptr;
	const FGFRegionSlot* Slot = FGFZoneLayout::FindRegion(RegionId);
	UWorld* World = GetWorld();
	if (!Character || !PS || !Slot || !World)
	{
		return;
	}
	const FString Key = PS->PlayerKey.IsEmpty() ? PS->GetPlayerName() : PS->PlayerKey;
	const double Now = World->GetTimeSeconds();
	if (const double* Last = LastUseSeconds.Find(Key); Last && (Now - *Last) < static_cast<double>(GateCooldownSeconds))
	{
		return;
	}

	FName Map = NAME_None;
	if (UClass* Class = FGFZoneLayout::FindGateClass(Slot->GateId))
	{
		if (const UGFMiniGameGateSubsystem* CDO = Cast<UGFMiniGameGateSubsystem>(Class->GetDefaultObject()))
		{
			Map = CDO->GetMapName();
		}
	}
	const bool bPackage = !Map.IsNone() && FPackageName::DoesPackageExist(Map.ToString());
	const FGFRegionTravelPlan Plan = FGFZoneLayout::PlanTravel(*Slot, bPackage);
	if (!Plan.bTravel)
	{
		if (!Plan.Message.IsEmpty())
		{
			LastUseSeconds.Add(Key, Now);
			PS->NotifyLand(Plan.Message);
		}
		return;
	}

	LastUseSeconds.Add(Key, Now);
	UGameInstance* GI = World->GetGameInstance();
	UGFShardTravelSubsystem* Travel = GI ? GI->GetSubsystem<UGFShardTravelSubsystem>() : nullptr;
	if (Travel)
	{
		Travel->SetPending(Plan.PendingStoneId);
	}
	UGFMiniGameGateSubsystem* Gate = FGFZoneLayout::FindLiveGate(GI, Slot->GateId);
	if (!Gate || !Gate->EnterMiniGame())
	{
		if (Travel)
		{
			Travel->ClearPending();
		}
		const FString Why = Gate && !Gate->LastEnterMessage.IsEmpty() ? Gate->LastEnterMessage : FString(GFZoneCopy::UnderConstruction());
		PS->NotifyLand(Why);
	}
}
