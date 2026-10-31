#include "GFAtelierStation.h"

#include "GFDataTables.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFRecipeTypes.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

const FName AGFAtelierStation::StationId(TEXT("Atelier"));

namespace
{
	const TCHAR* CraftCode(EGFCraftResult Result)
	{
		switch (Result)
		{
		case EGFCraftResult::Crafted: return TEXT("Crafted");
		case EGFCraftResult::UnknownRecipe: return TEXT("UnknownRecipe");
		case EGFCraftResult::Locked: return TEXT("Locked");
		case EGFCraftResult::WrongStation: return TEXT("WrongStation");
		case EGFCraftResult::MissingInputs: return TEXT("MissingInputs");
		case EGFCraftResult::NotAuthority: return TEXT("NotAuthority");
		default: return TEXT("UnknownRecipe");
		}
	}
}

AGFAtelierStation::AGFAtelierStation()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	BenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bench"));
	BenchMesh->SetupAttachment(Root);
	BenchMesh->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
	BenchMesh->SetRelativeScale3D(FVector(1.8f, 0.8f, 0.9f));
	BenchMesh->SetCollisionProfileName(TEXT("BlockAll"));
	BenchMesh->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BenchMesh->SetStaticMesh(Cube.Object);
	}

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetText(FText::FromString(TEXT("Atelier")));
	Sign->SetWorldSize(48.f);
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetVerticalAlignment(EVRTA_TextCenter);
	Sign->SetTextRenderColor(FColor(255, 228, 214));
	// Default text faces +X. Turn it around so a visitor walking up to -X can read it.
	Sign->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 160.f), FRotator(0.f, 180.f, 0.f));
	Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sign->SetCanEverAffectNavigation(false);

	BenchVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	BenchVolume->SetupAttachment(Root);
	BenchVolume->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	BenchVolume->SetBoxExtent(FVector(160.f, 120.f, 100.f));
	BenchVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BenchVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BenchVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BenchVolume->SetGenerateOverlapEvents(true);
	BenchVolume->SetCanEverAffectNavigation(false);
}

void AGFAtelierStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFAtelierStation, LastPrompt);
}

void AGFAtelierStation::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (BenchMesh && !BenchMesh->GetStaticMesh())
	{
		BenchMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	}
	ApplyBenchTint();
}

void AGFAtelierStation::BeginPlay()
{
	Super::BeginPlay();
	ApplyBenchTint();
}

void AGFAtelierStation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PollBench();
}

void AGFAtelierStation::ApplyBenchTint()
{
	if (!BenchMesh)
	{
		return;
	}
	static const FName ColorParam(TEXT("Color"));
	const FLinearColor Cloth(0.72f, 0.45f, 0.55f, 1.f);
	if (UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(BenchMesh->GetMaterial(0)))
	{
		Mid->SetVectorParameterValue(ColorParam, Cloth);
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Base)
	{
		return;
	}
	if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, BenchMesh))
	{
		Mid->SetVectorParameterValue(ColorParam, Cloth);
		BenchMesh->SetMaterial(0, Mid);
	}
}

FString AGFAtelierStation::GetSignText() const
{
	return Sign ? Sign->Text.ToString() : FString();
}

bool AGFAtelierStation::HasCraftAuthority() const
{
	if (bCraftAuthorityOverride)
	{
		return bForcedCraftAuthority;
	}
	return HasAuthority();
}

void AGFAtelierStation::SetCraftAuthorityForTests(bool bAuthority)
{
	bCraftAuthorityOverride = true;
	bForcedCraftAuthority = bAuthority;
}

bool AGFAtelierStation::IsPawnAtBench(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= CraftReachCm;
}

UGFCraftingSubsystem* AGFAtelierStation::ResolveCrafting()
{
	UGFCraftingSubsystem* Crafting = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		Crafting = GI->GetSubsystem<UGFCraftingSubsystem>();
	}
	// A test world, or a map opened without the game mode, has no subsystem yet.
	// The real game mode fills RecipeTable on the game-instance subsystem; that one wins.
	if (!Crafting)
	{
		if (!FallbackCrafting)
		{
			FallbackCrafting = NewObject<UGFCraftingSubsystem>(this);
		}
		Crafting = FallbackCrafting;
	}
	if (Crafting && !Crafting->RecipeTable)
	{
		Crafting->RecipeTable = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	}
	return Crafting;
}

EGFCraftResult AGFAtelierStation::Note(EGFCraftResult Result, const TCHAR* Prompt)
{
	LastResult = Result;
	LastPrompt = Prompt ? FString(Prompt) : FString::Printf(TEXT("Atelier: %s"), CraftCode(Result));
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
	return Result;
}

EGFCraftResult AGFAtelierStation::TryCraft(APawn* Pawn, FName RecipeId)
{
	if (!HasCraftAuthority())
	{
		return Note(EGFCraftResult::NotAuthority);
	}
	AGFPlayerState* PS = Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
	UGFInventoryComponent* Inventory = PS ? PS->Inventory.Get() : nullptr;
	if (!Inventory || RecipeId.IsNone())
	{
		return Note(EGFCraftResult::UnknownRecipe);
	}
	if (!IsPawnAtBench(Pawn))
	{
		return Note(EGFCraftResult::WrongStation, TEXT("Atelier: stand at the bench."));
	}
	UGFCraftingSubsystem* Crafting = ResolveCrafting();
	if (!Crafting)
	{
		return Note(EGFCraftResult::UnknownRecipe);
	}
	// Station is this bench. A Lab Bench or Cookpot row comes back WrongStation and spends nothing.
	return Note(Crafting->Craft(RecipeId, StationId, Inventory));
}

void AGFAtelierStation::PollBench()
{
	if (!HasCraftAuthority() || BenchRecipeId.IsNone())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TSet<TWeakObjectPtr<APawn>> Inside;
	auto Consider = [&](APawn* Pawn)
	{
		if (!Pawn || !Pawn->GetPlayerState<AGFPlayerState>() || !IsPawnAtBench(Pawn))
		{
			return;
		}
		const TWeakObjectPtr<APawn> Key(Pawn);
		Inside.Add(Key);
		if (AtBench.Contains(Key))
		{
			return;
		}
		AtBench.Add(Key);
		TryCraft(Pawn, BenchRecipeId);
	};

	if (BenchVolume)
	{
		BenchVolume->UpdateOverlaps();
		TArray<AActor*> Over;
		BenchVolume->GetOverlappingActors(Over, APawn::StaticClass());
		for (AActor* Actor : Over)
		{
			Consider(Cast<APawn>(Actor));
		}
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Consider(*It);
	}
	for (auto It = AtBench.CreateIterator(); It; ++It)
	{
		if (!Inside.Contains(*It))
		{
			It.RemoveCurrent();
		}
	}
}
