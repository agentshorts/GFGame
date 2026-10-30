#include "GFMiniGameGate.h"

#include "GFAssetMap.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFFashionGateSubsystem.h"
#include "GFMiniGameCatalog.h"
#include "GFMobaGateSubsystem.h"
#include "GFTamingGateSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

namespace
{
	UGFMiniGameGateSubsystem* SubsystemFor(UGameInstance* GI, FName Id)
	{
		if (!GI)
		{
			return nullptr;
		}
		if (Id == FName(TEXT("Taming")))
		{
			return GI->GetSubsystem<UGFTamingGateSubsystem>();
		}
		if (Id == FName(TEXT("Fashion")))
		{
			return GI->GetSubsystem<UGFFashionGateSubsystem>();
		}
		if (Id == FName(TEXT("Moba")))
		{
			return GI->GetSubsystem<UGFMobaGateSubsystem>();
		}
		if (Id == FName(TEXT("BeatHop")))
		{
			return GI->GetSubsystem<UGFBeatHopGateSubsystem>();
		}
		return nullptr;
	}
}

AGFMiniGameGate::AGFMiniGameGate()
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
	LabelText->SetWorldSize(64.f);
	LabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 180.f));
	Trigger->SetBoxExtent(FVector(160.f, 160.f, 180.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AGFMiniGameGate::OnTriggerBegin);
}

void AGFMiniGameGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMiniGameGate, EntryId);
	DOREPLIFETIME(AGFMiniGameGate, Role);
	DOREPLIFETIME(AGFMiniGameGate, RimeCenter);
}

void AGFMiniGameGate::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisuals();
}

FText AGFMiniGameGate::GetSignText() const
{
	return LabelText ? LabelText->GetText() : FText::GetEmpty();
}

void AGFMiniGameGate::OnRep_Gate()
{
	ApplyVisuals();
}

void AGFMiniGameGate::ApplyVisuals()
{
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(EntryId);
	Label = Entry ? Entry->Label : FString();
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(Label));
		if (!LabelText->GetFont())
		{
			if (UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField")))
			{
				LabelText->SetFont(Font);
			}
		}
	}

	bUsingPlaceholder = true;
	if (Mesh)
	{
		if (Entry && !Entry->ArtObject.IsEmpty())
		{
			if (UStaticMesh* Art = LoadObject<UStaticMesh>(nullptr, *Entry->ArtObject))
			{
				Mesh->SetStaticMesh(Art);
				Mesh->SetRelativeScale3D(FVector::OneVector);
				Mesh->SetRelativeLocation(FVector::ZeroVector);
				Mesh->EmptyOverrideMaterials();
				bUsingPlaceholder = false;
			}
		}
		if (bUsingPlaceholder)
		{
			Mesh->SetStaticMesh(FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube));
			// Engine cube is 100 cm and centred. A gate is 2 x 1 x 3 blocks, sitting on the ground.
			Mesh->SetRelativeScale3D(FVector(2.f, 1.f, 3.f));
			Mesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
			const FLinearColor Color = Entry ? Entry->PlaceholderColor : FLinearColor(1.f, 0.f, 1.f);
			Mesh->SetMaterial(0, FGFAssetMap::MakeTint(Mesh, Color));
		}
	}

	if (Trigger)
	{
		const bool bPortal = Role == EGFMiniGameGateRole::Portal;
		Trigger->SetGenerateOverlapEvents(bPortal);
		Trigger->SetCollisionEnabled(bPortal ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

bool AGFMiniGameGate::EnterFrom(APawn* Pawn)
{
	if (!HasAuthority() || Role != EGFMiniGameGateRole::Portal || !Pawn)
	{
		return false;
	}
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(EntryId);
	if (!Entry)
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() < NextEnterSeconds)
	{
		return false;
	}
	if (Entry->HasMap())
	{
		UGFMiniGameGateSubsystem* Gate = SubsystemFor(GetGameInstance(), Entry->Id);
		if (!Gate)
		{
			return false;
		}
		if (World)
		{
			NextEnterSeconds = World->GetTimeSeconds() + 2.f;
		}
		return Gate->EnterMiniGame();
	}
	if (!Entry->HasSim())
	{
		return false;
	}
	if (World)
	{
		NextEnterSeconds = World->GetTimeSeconds() + 2.f;
	}
	Pawn->SetActorLocation(FGFMiniGameCatalog::SimStand(*Entry, RimeCenter));
	return true;
}

void AGFMiniGameGate::OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	(void)Overlapped;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	(void)Sweep;
	APawn* Pawn = Cast<APawn>(Other);
	if (Pawn && Pawn->IsPlayerControlled())
	{
		EnterFrom(Pawn);
	}
}
