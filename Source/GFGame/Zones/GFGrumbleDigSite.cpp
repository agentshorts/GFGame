#include "GFGrumbleDigSite.h"

#include "GFGrumbleDig.h"
#include "GFPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	const FName ColorParam(TEXT("Color"));

	const FName SpotStart(TEXT("Start"));
	const FName SpotFinish(TEXT("Finish"));
	const FName SpotReturn(TEXT("Return"));
	const FName YieldStone(TEXT("Stone"));

	// Local +Y points at the hub. The porch arch sits PorchArchInsetCm further along +Y.
	constexpr float StartY = -500.f;
	constexpr float FinishY = -1700.f;
	constexpr float HubY = 0.f;
	constexpr float LobbyY = 400.f;
	constexpr float DigSeconds = 2.f;
	constexpr float DigPitchDegrees = -30.f;

	bool IsPlayerPawn(const APawn* Pawn)
	{
		return Pawn && (Pawn->IsPlayerControlled() || Pawn->GetPlayerState<AGFPlayerState>() != nullptr);
	}

	/** Local +Y points at the hub, so the trench (local -Y) runs into the porch. */
	FRotator HubwardYaw(const FVector& Origin)
	{
		const FVector Hubward = -Origin.GetSafeNormal2D();
		if (Hubward.IsNearlyZero())
		{
			return FRotator::ZeroRotator;
		}
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Hubward.X, Hubward.Y));
		return FRotator(0.f, Yaw, 0.f);
	}
}

AGFGrumbleDigSite::AGFGrumbleDigSite()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AGFGrumbleDigSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFGrumbleDigSite, bReady);
	DOREPLIFETIME(AGFGrumbleDigSite, LastPrompt);
	DOREPLIFETIME(AGFGrumbleDigSite, Phase);
	DOREPLIFETIME(AGFGrumbleDigSite, bHasResults);
	DOREPLIFETIME(AGFGrumbleDigSite, CellsDug);
	DOREPLIFETIME(AGFGrumbleDigSite, StoneCount);
	DOREPLIFETIME(AGFGrumbleDigSite, StepRiserCm);
	DOREPLIFETIME(AGFGrumbleDigSite, AppliedPitchDegrees);
	DOREPLIFETIME(AGFGrumbleDigSite, YieldItem);
}

void AGFGrumbleDigSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() || bReady)
	{
		BuildIfNeeded();
	}
}

void AGFGrumbleDigSite::OnRep_Ready()
{
	if (bReady)
	{
		BuildIfNeeded();
	}
}

void AGFGrumbleDigSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		PollSpots();
	}
}

AGFGrumbleDigSite* AGFGrumbleDigSite::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFGrumbleDigSite> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AGFGrumbleDigSite* AGFGrumbleDigSite::Ensure(UWorld* World, const FVector& Origin)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return Get(World);
	}
	const FRotator Facing = HubwardYaw(Origin);
	AGFGrumbleDigSite* Site = Get(World);
	if (!Site)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Site = World->SpawnActor<AGFGrumbleDigSite>(AGFGrumbleDigSite::StaticClass(), FTransform(Facing, Origin), Params);
	}
	if (!Site)
	{
		return nullptr;
	}
	Site->SetActorLocationAndRotation(Origin, Facing);
	Site->BuildIfNeeded();
	return Site;
}

bool AGFGrumbleDigSite::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

FVector AGFGrumbleDigSite::GetLobbyStand() const
{
	return GetActorTransform().TransformPosition(FVector(0.f, LobbyY, HubStandZCm));
}

FVector AGFGrumbleDigSite::GetHubReturnLocation() const
{
	return FVector(0.f, 0.f, HubStandZCm);
}

UBoxComponent* AGFGrumbleDigSite::GetSpot(FName Spot) const
{
	for (int32 i = 0; i < Spots.Num() && i < SpotNames.Num(); ++i)
	{
		if (SpotNames[i] == Spot)
		{
			return Spots[i];
		}
	}
	return nullptr;
}

void AGFGrumbleDigSite::BuildIfNeeded()
{
	if (bVisualsBuilt)
	{
		return;
	}
	BuildVisuals();
	bVisualsBuilt = true;
	if (HasAuthority())
	{
		bReady = true;
	}
}

void AGFGrumbleDigSite::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
{
	if (!CubeMesh)
	{
		return;
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetupAttachment(Root);
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetRelativeLocation(LocalCenter);
	Mesh->SetRelativeScale3D(SizeCm / 100.f);
	Mesh->SetCanEverAffectNavigation(false);
	if (bBlock)
	{
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (BaseMaterial)
	{
		if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, Mesh))
		{
			Tint->SetVectorParameterValue(ColorParam, Color);
			Mesh->SetMaterial(0, Tint);
		}
	}
	Mesh->RegisterComponent();
}

void AGFGrumbleDigSite::AddLabel(const FString& Text, const FVector& Local)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(42.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor::White);
	if (!SignFont)
	{
		SignFont = LoadObject<UFont>(nullptr, FontPath);
	}
	if (SignFont)
	{
		Label->SetFont(SignFont);
	}
	// Default text faces local +X. Players walk in from local +Y, so face the words that way.
	Label->SetRelativeLocationAndRotation(Local, FRotator(0.f, 90.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
}

UBoxComponent* AGFGrumbleDigSite::AddSpot(FName Spot, const FVector& Local)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(this);
	Box->SetupAttachment(Root);
	Box->SetRelativeLocation(Local);
	Box->SetBoxExtent(FVector(140.f, 140.f, 120.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	Box->RegisterComponent();
	Spots.Add(Box);
	SpotNames.Add(Spot);
	return Box;
}

void AGFGrumbleDigSite::AddArch(const FString& Title, const FVector& Local, const FLinearColor& Color)
{
	// Posts sit on X so the opening faces the trench (local Y, toward the hub and away from it).
	AddBlock(Local + FVector(-140.f, 0.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(Local + FVector(140.f, 0.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(Local + FVector(0.f, 0.f, 260.f), FVector(320.f, 40.f, 30.f), Color, true);
	AddLabel(Title, Local + FVector(0.f, 0.f, 320.f));
}

void AGFGrumbleDigSite::BuildVisuals()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	const FLinearColor Earth(0.62f, 0.42f, 0.18f);
	const FLinearColor Trench(0.40f, 0.28f, 0.16f);
	AddBlock(FVector(0.f, -700.f, -20.f), FVector(900.f, 2600.f, 40.f), Earth, true);
	AddBlock(FVector(0.f, (StartY + FinishY) * 0.5f, 2.f), FVector(220.f, 1000.f, 4.f), Trench, false);

	AddArch(TEXT("Start"), FVector(0.f, StartY, 0.f), FLinearColor(0.45f, 0.75f, 0.35f));
	AddSpot(SpotStart, FVector(0.f, StartY, 120.f));

	AddArch(TEXT("Finish"), FVector(0.f, FinishY, 0.f), FLinearColor(0.85f, 0.45f, 0.22f));
	AddSpot(SpotFinish, FVector(0.f, FinishY, 120.f));

	AddArch(TEXT("Hub"), FVector(0.f, HubY, 0.f), FLinearColor(0.90f, 0.90f, 0.85f));
	AddSpot(SpotReturn, FVector(0.f, HubY, 120.f));

	AddLabel(TEXT("Big Grumble"), FVector(0.f, LobbyY, 280.f));
}

bool AGFGrumbleDigSite::Accepts(const APawn* Pawn) const
{
	return HasAuthority() && IsPlayerPawn(Pawn);
}

bool AGFGrumbleDigSite::ReceiveFromGate(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn)
	{
		return false;
	}
	Pawn->SetActorLocation(GetLobbyStand(), false, nullptr, ETeleportType::TeleportPhysics);
	LastPrompt = TEXT("On the trench. Walk through Start.");
	return true;
}

void AGFGrumbleDigSite::ClearBoard()
{
	bHasResults = false;
	CellsDug = 0;
	StoneCount = 0;
	StepRiserCm = 0.f;
	AppliedPitchDegrees = 0.f;
	YieldItem = NAME_None;
}

bool AGFGrumbleDigSite::TryStart(APawn* Pawn)
{
	if (!Accepts(Pawn) || Phase == EGFGrumbleDigPhase::Running)
	{
		return false;
	}
	ClearBoard();
	Phase = EGFGrumbleDigPhase::Running;
	LastPrompt = TEXT("Start: the trench is open.");
	return true;
}

bool AGFGrumbleDigSite::TryFinish(APawn* Pawn)
{
	if (!Accepts(Pawn) || Phase != EGFGrumbleDigPhase::Running)
	{
		return false;
	}

	// The greybox floor is the solid trench. One stair sample, no voxel write, no bag mint.
	FGFGrumbleDigContext Context;
	Context.TameState = EGFTameState::Tamed;
	Context.Species = FGFGrumbleDig::SpeciesId;
	Context.DisplayName = TEXT("Big Grumble");

	FGFGrumbleDigTuning Tuning;
	Tuning.FallbackDrop = YieldStone;

	const FVector LocalTravel(0.f, FinishY - StartY, 0.f);
	FGFGrumbleDigMove Move;
	Move.Position = GetActorTransform().TransformPosition(FVector(0.f, StartY, 0.f));
	Move.Velocity = GetActorTransform().TransformVector(LocalTravel) / DigSeconds;
	Move.PitchDegrees = DigPitchDegrees;
	Move.DeltaSeconds = DigSeconds;

	const FGFGrumbleDigResult Dug = FGFGrumbleDig::Plan(Move, Context, Tuning);
	CellsDug = Dug.Cells.Num();
	StoneCount = 0;
	YieldItem = NAME_None;
	for (const FGFItemStack& Stack : Dug.Yields)
	{
		if (Stack.ItemId == YieldStone)
		{
			StoneCount += Stack.Count;
			YieldItem = YieldStone;
		}
	}
	StepRiserCm = Dug.StepRiserCm;
	AppliedPitchDegrees = Dug.AppliedPitchDegrees;
	bHasResults = true;
	Phase = EGFGrumbleDigPhase::Results;
	LastPrompt = FString::Printf(TEXT("Results: %d cells, %d Stone, riser %.0f cm"), CellsDug, StoneCount, StepRiserCm);
	return true;
}

bool AGFGrumbleDigSite::TryReturn(APawn* Pawn)
{
	if (!Accepts(Pawn))
	{
		return false;
	}
	Pawn->SetActorLocation(GetHubReturnLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	Phase = EGFGrumbleDigPhase::Idle;
	LastPrompt = TEXT("Back on the hub stand.");
	return true;
}

void AGFGrumbleDigSite::GetPromptLines(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	if (Phase == EGFGrumbleDigPhase::Running)
	{
		OutLines.Add(TEXT("BIG GRUMBLE"));
		OutLines.Add(TEXT("Digging. Walk through Finish."));
		return;
	}
	if (bHasResults)
	{
		OutLines.Add(TEXT("RESULTS"));
		OutLines.Add(FString::Printf(TEXT("Cells %d"), CellsDug));
		OutLines.Add(FString::Printf(TEXT("Stone %d"), StoneCount));
		OutLines.Add(FString::Printf(TEXT("Riser %.0f cm"), StepRiserCm));
		OutLines.Add(TEXT("Hub arch returns to the stand."));
		return;
	}
	OutLines.Add(TEXT("BIG GRUMBLE"));
	OutLines.Add(TEXT("Walk through Start."));
	OutLines.Add(TEXT("Finish reads the trench. The Hub arch returns to the stand."));
}

void AGFGrumbleDigSite::PollSpots()
{
	SpotInside.SetNum(Spots.Num());
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		UBoxComponent* Box = Spots[i];
		if (!Box || !SpotNames.IsValidIndex(i))
		{
			continue;
		}
		const FName Spot = SpotNames[i];
		TSet<TWeakObjectPtr<APawn>> Inside;
		TArray<AActor*> Over;
		Box->GetOverlappingActors(Over, APawn::StaticClass());
		for (AActor* Actor : Over)
		{
			APawn* Pawn = Cast<APawn>(Actor);
			if (!IsPlayerPawn(Pawn))
			{
				continue;
			}
			const TWeakObjectPtr<APawn> Key(Pawn);
			Inside.Add(Key);
			if (SpotInside[i].Contains(Key))
			{
				continue;
			}
			SpotInside[i].Add(Key);
			if (Spot == SpotStart)
			{
				TryStart(Pawn);
			}
			else if (Spot == SpotFinish)
			{
				TryFinish(Pawn);
			}
			else if (Spot == SpotReturn)
			{
				TryReturn(Pawn);
			}
		}
		for (auto It = SpotInside[i].CreateIterator(); It; ++It)
		{
			if (!Inside.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}
	}
}
