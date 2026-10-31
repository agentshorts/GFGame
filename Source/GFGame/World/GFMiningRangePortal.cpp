#include "GFMiningRangePortal.h"

#include "GFLog.h"
#include "GFMiningRange.h"
#include "GFGameState.h"
#include "GFMiniGameCatalog.h"
#include "GFTerritorySubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

namespace GFMiningRangePortalPrivate
{
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	static const FVector PortalOffset(3200.f, -1600.f, 0.f);
}

void GFTravelToMap(UWorld* World, const FString& Map, const TFunction<void(const FString&)>& Override)
{
	if (Override)
	{
		Override(Map);
		return;
	}
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const ENetMode NetMode = World->GetNetMode();
	if (NetMode == NM_ListenServer)
	{
		World->ServerTravel(Map + TEXT("?listen"));
	}
	else if (NetMode == NM_DedicatedServer)
	{
		World->ServerTravel(Map);
	}
	else if (NetMode != NM_Client)
	{
		UGameplayStatics::OpenLevel(World, FName(*Map));
	}
}

AGFMiningRangePortal::AGFMiningRangePortal()
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
	LabelText->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
	LabelText->SetHorizontalAlignment(EHTA_Center);
	LabelText->SetVerticalAlignment(EVRTA_TextCenter);
	LabelText->SetWorldSize(48.f);
	LabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	Trigger->SetBoxExtent(FVector(120.f, 80.f, 160.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AGFMiningRangePortal::OnTriggerBegin);
}

void AGFMiningRangePortal::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisuals();
}

void AGFMiningRangePortal::ApplyVisuals()
{
	if (bVisuals)
	{
		return;
	}
	bVisuals = true;
	SignText = FText::FromString(TEXT("Mining Range"));
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, GFMiningRangePortalPrivate::CubePath))
	{
		Mesh->SetStaticMesh(Cube);
		Mesh->SetWorldScale3D(FVector(1.2f, 0.4f, 2.2f));
	}
	if (UFont* Font = LoadObject<UFont>(nullptr, GFMiningRangePortalPrivate::FontPath))
	{
		LabelText->SetFont(Font);
	}
	LabelText->SetText(SignText);
	LabelText->SetTextRenderColor(FColor::White);
}

void AGFMiningRangePortal::OnTriggerBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	(void)Overlapped; (void)OtherComp; (void)OtherBodyIndex; (void)bFromSweep; (void)Sweep;
	if (HasAuthority() && Cast<APawn>(Other))
	{
		GFTravelToMap(GetWorld(), AGFMiningRange::RangeMap, TravelOverride);
	}
}

AGFMiningRangeDirector::AGFMiningRangeDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicatingMovement(false);
}

void AGFMiningRangeDirector::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	const FString Package = GetWorld()->GetOutermost() ? GetWorld()->GetOutermost()->GetName() : FString();
	const FString Map = GetWorld()->GetMapName();
	if (GFMiningMaps::IsNamedMap(Package, TEXT("L_Hub")) || GFMiningMaps::IsNamedMap(Map, TEXT("L_Hub"))
		|| GFMiningMaps::IsNamedMap(Package, TEXT("L_MiningRange")) || GFMiningMaps::IsNamedMap(Map, TEXT("L_MiningRange")))
	{
		Place(Package.IsEmpty() ? Map : Package, RimeAnchor(GetWorld()));
	}
}

void AGFMiningRangeDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr)
		{
			GS->UnregisterModeHandler(this);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

FVector AGFMiningRangeDirector::RimeAnchor(const UWorld* World) const
{
	const UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
	const TArray<FGFTerritoryFaction>* Layout = nullptr;
	if (Territory && Territory->FactionLayout.Num() > 0)
	{
		Layout = &Territory->FactionLayout;
	}
	else if (const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>())
	{
		Layout = &Defaults->FactionLayout;
	}
	if (Layout)
	{
		for (const FGFTerritoryFaction& Faction : *Layout)
		{
			if (Faction.Id == FName(TEXT("Rime")))
			{
				return Faction.Center;
			}
		}
	}
	return FVector::ZeroVector;
}

void AGFMiningRangeDirector::Place(const FString& WorldPackage, const FVector& RimeCenter)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	if (GFMiningMaps::IsNamedMap(WorldPackage, TEXT("L_MiningRange")))
	{
		for (TActorIterator<AGFMiningRange> It(World); It; ++It)
		{
			return;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AGFMiningRange>(AGFMiningRange::StaticClass(), FTransform::Identity, Params);
		return;
	}
	if (!GFMiningMaps::IsNamedMap(WorldPackage, TEXT("L_Hub")))
	{
		return;
	}
	if (AGFGameState* GS = World->GetGameState<AGFGameState>())
	{
		GS->RegisterModeHandler(this);
		bRegistered = true;
	}
	const FVector At = RimeCenter + GFMiningRangePortalPrivate::PortalOffset;
	for (TActorIterator<AGFMiningRangePortal> It(World); It; ++It)
	{
		It->SetActorLocation(At);
		It->ApplyVisuals();
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AGFMiningRangePortal* Portal = World->SpawnActor<AGFMiningRangePortal>(AGFMiningRangePortal::StaticClass(), FTransform(At), Params))
	{
		Portal->ApplyVisuals();
	}
}

FGFModeResult AGFMiningRangeDirector::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request)
{
	(void)By;
	const FString Verb = Request.Verb.ToString().ToLower();
	if (Verb == TEXT("go") || Verb.IsEmpty())
	{
		GFTravelToMap(GetWorld(), AGFMiningRange::RangeMap, TFunction<void(const FString&)>());
		return FGFModeResult::Ok();
	}
	return FGFModeResult::Fail(TEXT("NotOnRange"));
}
