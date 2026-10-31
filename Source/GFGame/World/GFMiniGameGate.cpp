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
#include "Materials/MaterialInstanceDynamic.h"
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

	/** M_GF_Prop dye: _M R takes Dye_R. A fresh dynamic MI per apply, so a re-applied gate never stacks tints. */
	void DyeSlot0(UStaticMeshComponent* Comp, const FLinearColor& Color)
	{
		if (Comp && Comp->GetMaterial(0))
		{
			if (UMaterialInstanceDynamic* Dyed = UMaterialInstanceDynamic::Create(Comp->GetMaterial(0), Comp))
			{
				Dyed->SetVectorParameterValue(TEXT("Dye_R"), Color);
				Comp->SetMaterial(0, Dyed);
			}
		}
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

	PadFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadFloor"));
	PadFloor->SetupAttachment(Root);
	PadFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadFloor->SetCanEverAffectNavigation(false);
	PadFloor->SetHiddenInGame(true);
	PadFloor->SetVisibility(false);

	ModeSign = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ModeSign"));
	ModeSign->SetupAttachment(Root);
	ModeSign->SetRelativeLocation(FVector(0.f, FGFMiniGameCatalog::SignRightCm, 0.f));
	ModeSign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ModeSign->SetHiddenInGame(true);
	ModeSign->SetVisibility(false);

	Jukebox = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Jukebox"));
	Jukebox->SetupAttachment(Root);
	Jukebox->SetRelativeLocation(FVector(0.f, -FGFMiniGameCatalog::SignRightCm, 0.f));
	Jukebox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Jukebox->SetHiddenInGame(true);
	Jukebox->SetVisibility(false);
}

void AGFMiniGameGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMiniGameGate, EntryId);
	DOREPLIFETIME(AGFMiniGameGate, GateRole);
	DOREPLIFETIME(AGFMiniGameGate, RimeCenter);
}

void AGFMiniGameGate::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Deferred spawn sets EntryId before FinishSpawning, which is what calls this.
	// Headless tests never begin play, so the sign and the mesh have to exist here.
	ApplyVisuals();
}

void AGFMiniGameGate::BeginPlay()
{
	Super::BeginPlay();
	// Clients receive EntryId on the spawn bunch before BeginPlay, and OnRep does not run for that first bunch.
	ApplyVisuals();
}

FText AGFMiniGameGate::GetSignText() const
{
	return SignText;
}

void AGFMiniGameGate::OnRep_Gate()
{
	ApplyVisuals();
}

void AGFMiniGameGate::ApplyVisuals()
{
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(EntryId);
	Label = Entry ? Entry->Label : FString();
	SignText = FText::FromString(Label);
	if (LabelText)
	{
		LabelText->SetText(SignText);
		if (!SignFont)
		{
			SignFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
		}
		if (SignFont)
		{
			LabelText->SetFont(SignFont);
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
				if (Entry->HasSim())
				{
					// The four sims share SM_Gate_Sim; the entry colour tells them apart.
					DyeSlot0(Mesh, Entry->PlaceholderColor);
				}
				bUsingPlaceholder = false;
			}
		}
		if (bUsingPlaceholder)
		{
			// Authored art is optional. Beat Hop, Depths, the mine, the lab and the dino arena
			// have no gate mesh yet, and a failed load takes the same tinted block.
			UStaticMesh* Block = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube);
			Mesh->SetStaticMesh(Block);
			// Engine cube is 100 cm and centred. A gate is 2 x 1 x 3 blocks, sitting on the ground.
			Mesh->SetRelativeScale3D(FVector(2.f, 1.f, 3.f));
			Mesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
			const FLinearColor Color = Entry ? Entry->PlaceholderColor : FLinearColor(1.f, 0.f, 1.f);
			Mesh->SetMaterial(0, FGFAssetMap::MakeTint(Mesh, Color));
		}
	}

	if (Trigger)
	{
		const bool bPortal = GateRole == EGFMiniGameGateRole::Portal;
		Trigger->SetGenerateOverlapEvents(bPortal);
		Trigger->SetCollisionEnabled(bPortal ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}

	if (PadFloor)
	{
		const bool bSolidPad = GateRole == EGFMiniGameGateRole::Marker && Entry && Entry->bHasPad;
		PadFloor->SetHiddenInGame(!bSolidPad);
		PadFloor->SetVisibility(bSolidPad);
		if (!bSolidPad)
		{
			PadFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else if (UStaticMesh* PadArt = LoadObject<UStaticMesh>(nullptr, FGFMiniGameCatalog::PadObject, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			// SM_GatePad origin is the walk surface, so it drops in at scale 1 and Z 0 (slab Z -100..0).
			PadFloor->SetStaticMesh(PadArt);
			PadFloor->SetRelativeScale3D(FVector::OneVector);
			PadFloor->SetRelativeLocation(FVector::ZeroVector);
			PadFloor->SetCollisionProfileName(TEXT("BlockAll"));
			PadFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			PadFloor->SetCanEverAffectNavigation(false);
			PadFloor->EmptyOverrideMaterials();
			DyeSlot0(PadFloor, Entry->PlaceholderColor);
		}
		else
		{
			// No pad art: engine cube is 100 cm and centred. Top of the slab sits on the marker origin.
			const float CubeCm = 100.f;
			const float Thickness = FGFMiniGameCatalog::PadFloorThicknessCm;
			PadFloor->SetStaticMesh(FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube));
			PadFloor->SetRelativeScale3D(FVector(
				(Entry->PadHalfExtent.X * 2.f) / CubeCm,
				(Entry->PadHalfExtent.Y * 2.f) / CubeCm,
				Thickness / CubeCm));
			PadFloor->SetRelativeLocation(FVector(0.f, 0.f, -Thickness * 0.5f));
			PadFloor->SetCollisionProfileName(TEXT("BlockAll"));
			PadFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			PadFloor->SetCanEverAffectNavigation(false);
			PadFloor->SetMaterial(0, FGFAssetMap::MakeTint(PadFloor, Entry->PlaceholderColor));
		}
	}

	if (ModeSign)
	{
		UStaticMesh* SignArt = nullptr;
		if (GateRole == EGFMiniGameGateRole::Portal && Entry && !Entry->SignObject.IsEmpty())
		{
			SignArt = LoadObject<UStaticMesh>(nullptr, *Entry->SignObject, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		ModeSign->SetStaticMesh(SignArt);
		ModeSign->SetRelativeLocation(FVector(0.f, FGFMiniGameCatalog::SignRightCm, 0.f));
		ModeSign->SetRelativeRotation(FRotator::ZeroRotator);
		ModeSign->SetHiddenInGame(SignArt == nullptr);
		ModeSign->SetVisibility(SignArt != nullptr);
		if (SignArt)
		{
			ModeSign->SetCollisionProfileName(TEXT("BlockAll"));
			ModeSign->SetCanEverAffectNavigation(false);
		}
		else
		{
			ModeSign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// #213: the jukebox stands on the Beat Hop hub portal's other side, mirroring the sign (gate-local (0, -300, 0),
	// yaw 0 relative to the gate, so it faces Rime with it). With Rime at the origin that's world (-1626.34, 1202.08), yaw -45.
	if (Jukebox)
	{
		UStaticMesh* JukeboxArt = nullptr;
		if (GateRole == EGFMiniGameGateRole::Portal && EntryId == FName(TEXT("BeatHop")))
		{
			JukeboxArt = LoadObject<UStaticMesh>(nullptr, JukeboxPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		Jukebox->SetStaticMesh(JukeboxArt);
		Jukebox->SetRelativeLocation(FVector(0.f, -FGFMiniGameCatalog::SignRightCm, 0.f));
		Jukebox->SetRelativeRotation(FRotator::ZeroRotator);
		Jukebox->SetRelativeScale3D(FVector::OneVector);
		Jukebox->SetHiddenInGame(JukeboxArt == nullptr);
		Jukebox->SetVisibility(JukeboxArt != nullptr);
		if (JukeboxArt)
		{
			Jukebox->SetCollisionProfileName(TEXT("BlockAll"));
			Jukebox->SetCanEverAffectNavigation(false);
		}
		else
		{
			Jukebox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

bool AGFMiniGameGate::EnterFrom(APawn* Pawn)
{
	if (!HasAuthority() || GateRole != EGFMiniGameGateRole::Portal || !Pawn)
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
