#include "GFFerryStand.h"

#include "GFFerryDirector.h"
#include "GFFerryRiderComponent.h"
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

namespace GFFerryStandPrivate
{
	const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	const FName ColorParam(TEXT("Color"));
	const FName PassPad(TEXT("Pass"));

	FGFFerryResult Fail(FName Code)
	{
		FGFFerryResult Result;
		Result.bOk = false;
		Result.Code = Code;
		return Result;
	}
}

AGFFerryStand::AGFFerryStand()
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

void AGFFerryStand::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFFerryStand, StopId);
	DOREPLIFETIME(AGFFerryStand, AnchorId);
	DOREPLIFETIME(AGFFerryStand, DestinationIds);
	DOREPLIFETIME(AGFFerryStand, DestinationAnchors);
	DOREPLIFETIME(AGFFerryStand, LastPrompt);
}

void AGFFerryStand::BeginPlay()
{
	Super::BeginPlay();
	if (!StopId.IsNone())
	{
		BuildIfNeeded();
	}
}

void AGFFerryStand::OnRep_Stop()
{
	if (!StopId.IsNone())
	{
		BuildIfNeeded();
	}
}

void AGFFerryStand::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		PollPads();
	}
}

void AGFFerryStand::Configure(AGFFerryDirector* InDirector, FName InStopId)
{
	Director = InDirector;
	StopId = InStopId;
	AnchorId = NAME_None;
	DestinationIds.Reset();
	DestinationAnchors.Reset();
	if (InDirector)
	{
		if (const FGFFerryStop* Stop = FGFFerryRules::FindStop(InDirector->Stops, InStopId))
		{
			AnchorId = Stop->AnchorId;
		}
		TArray<FName> Ids;
		FGFFerryRules::CollectDestinations(InDirector->Stops, InDirector->Routes, InStopId, Ids);
		for (const FName Id : Ids)
		{
			DestinationIds.Add(Id);
			const FGFFerryStop* Dest = FGFFerryRules::FindStop(InDirector->Stops, Id);
			DestinationAnchors.Add(Dest && !Dest->AnchorId.IsNone() ? Dest->AnchorId : Id);
		}
	}
	BuildIfNeeded();
}

AGFFerryStand* AGFFerryStand::Find(const UWorld* World, FName InStopId)
{
	if (!World || InStopId.IsNone())
	{
		return nullptr;
	}
	for (TActorIterator<AGFFerryStand> It(World); It; ++It)
	{
		if (It->StopId == InStopId)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AGFFerryStand::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

AGFFerryDirector* AGFFerryStand::ResolveDirector()
{
	if (AGFFerryDirector* Live = Director.Get())
	{
		return Live;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFFerryDirector> It(World); It; ++It)
	{
		Director = *It;
		return *It;
	}
	return nullptr;
}

const FGFFerryTuning& AGFFerryStand::Tuning() const
{
	if (const AGFFerryDirector* Live = Director.Get())
	{
		return Live->Tuning;
	}
	if (const AGFFerryDirector* Defaults = GetDefault<AGFFerryDirector>())
	{
		return Defaults->Tuning;
	}
	static const FGFFerryTuning Empty;
	return Empty;
}

void AGFFerryStand::Note(const FString& What, const FGFFerryResult& Result)
{
	LastPrompt = FString::Printf(TEXT("%s: %s"), *What, Result.bOk ? TEXT("Ok") : *Result.Code.ToString());
}

FGFFerryResult AGFFerryStand::TryDepart(APawn* Rider, FName ToStop)
{
	AGFFerryDirector* Live = ResolveDirector();
	if (!Live)
	{
		const FGFFerryResult Result = GFFerryStandPrivate::Fail(FGFFerryCodes::NotAuthority);
		Note(TEXT("Fly"), Result);
		return Result;
	}
	const FGFFerryResult Result = Live->RequestTripTo(Rider, StopId, ToStop, FGuid::NewGuid());
	FString Where = ToStop.ToString();
	const int32 Index = DestinationIds.IndexOfByKey(ToStop);
	if (DestinationAnchors.IsValidIndex(Index))
	{
		Where = DestinationAnchors[Index].ToString();
	}
	Note(FString::Printf(TEXT("Fly %s"), *Where), Result);
	return Result;
}

FGFFerryResult AGFFerryStand::TryBuyPass(APawn* Rider)
{
	AGFFerryDirector* Live = ResolveDirector();
	if (!Live)
	{
		const FGFFerryResult Result = GFFerryStandPrivate::Fail(FGFFerryCodes::NotAuthority);
		Note(TEXT("Pass"), Result);
		return Result;
	}
	const FGFFerryResult Result = Live->BuyPass(Rider, StopId, FGuid::NewGuid());
	Note(TEXT("Pass"), Result);
	return Result;
}

UBoxComponent* AGFFerryStand::GetFlyTrigger(FName ToStop) const
{
	for (int32 Index = 0; Index < Triggers.Num() && Index < TriggerDests.Num(); ++Index)
	{
		if (TriggerDests[Index] == ToStop)
		{
			return Triggers[Index];
		}
	}
	return nullptr;
}

UBoxComponent* AGFFerryStand::GetPassTrigger() const
{
	return GetFlyTrigger(GFFerryStandPrivate::PassPad);
}

void AGFFerryStand::GetPromptLines(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	const FString Who = AnchorId.IsNone() ? StopId.ToString() : AnchorId.ToString();
	OutLines.Add(FString::Printf(TEXT("FERRY  %s"), *Who));
	OutLines.Add(TEXT("The bird stays on its perch. It does not follow."));
	const FGFFerryTuning& Fare = Tuning();
	OutLines.Add(FString::Printf(TEXT("A trip is %d %s. A pass rides free."), Fare.FareCount, *Fare.FareItem.ToString()));
	FString Pads;
	for (const FName& Anchor : DestinationAnchors)
	{
		if (!Pads.IsEmpty())
		{
			Pads += TEXT(", ");
		}
		Pads += Anchor.ToString();
	}
	OutLines.Add(Pads.IsEmpty() ? TEXT("No other safe zone from here.") : FString::Printf(TEXT("Walk a pad: %s."), *Pads));
	OutLines.Add(FString::Printf(TEXT("Lifetime pass: %d %s."), Fare.PassPrice, *Fare.PassMaterialItem.ToString()));
	if (!LastPrompt.IsEmpty())
	{
		OutLines.Add(LastPrompt);
	}
}

void AGFFerryStand::BuildIfNeeded()
{
	if (bVisualsBuilt || StopId.IsNone())
	{
		return;
	}
	TArray<FName> Ids = DestinationIds;
	TArray<FName> Anchors = DestinationAnchors;
	FName Anchor = AnchorId;
	// A client proxy may build before the destination list replicates. The ini has the same stops.
	// Locals only: do not write the replicated arrays from a client.
	if (!HasAuthority() && Ids.Num() == 0)
	{
		if (const AGFFerryDirector* Defaults = GetDefault<AGFFerryDirector>())
		{
			FGFFerryRules::CollectDestinations(Defaults->Stops, Defaults->Routes, StopId, Ids);
			for (const FName Id : Ids)
			{
				const FGFFerryStop* Dest = FGFFerryRules::FindStop(Defaults->Stops, Id);
				Anchors.Add(Dest && !Dest->AnchorId.IsNone() ? Dest->AnchorId : Id);
			}
			if (Anchor.IsNone())
			{
				if (const FGFFerryStop* Stop = FGFFerryRules::FindStop(Defaults->Stops, StopId))
				{
					Anchor = Stop->AnchorId;
				}
			}
		}
	}
	BuildVisuals(Ids, Anchors, Anchor);
	bVisualsBuilt = true;
}

void AGFFerryStand::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
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
			Tint->SetVectorParameterValue(GFFerryStandPrivate::ColorParam, Color);
			Mesh->SetMaterial(0, Tint);
		}
	}
	Mesh->RegisterComponent();
}

void AGFFerryStand::AddLabel(const FString& Text, const FVector& Local)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(36.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor::White);
	if (UFont* SignFont = LoadObject<UFont>(nullptr, GFFerryStandPrivate::FontPath))
	{
		Label->SetFont(SignFont);
	}
	Label->SetRelativeLocationAndRotation(Local, FRotator(0.f, 180.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
}

UBoxComponent* AGFFerryStand::AddTrigger(FName ToStop, const FVector& Local)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(this);
	Box->SetupAttachment(Root);
	Box->SetRelativeLocation(Local);
	Box->SetBoxExtent(FGFFerryStandLayout::Extent());
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	Box->RegisterComponent();
	Triggers.Add(Box);
	TriggerDests.Add(ToStop);
	return Box;
}

void AGFFerryStand::BuildVisuals(const TArray<FName>& Ids, const TArray<FName>& Anchors, FName Anchor)
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, GFFerryStandPrivate::CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, GFFerryStandPrivate::MaterialPath);

	const FLinearColor Floor(0.62f, 0.64f, 0.68f);
	const FLinearColor Fly(0.95f, 0.78f, 0.35f);
	const FLinearColor Pass(0.55f, 0.75f, 0.95f);
	const float FarY = Ids.Num() > 0
		? FGFFerryStandLayout::DestLocal(Ids.Num() - 1).Y
		: -FGFFerryStandLayout::Ring;
	const float Span = FGFFerryStandLayout::Ring - FarY;
	AddBlock(FVector(0.f, (FGFFerryStandLayout::Ring + FarY) * 0.5f, -10.f), FVector(420.f, Span + 160.f, 20.f), Floor, true);
	AddBlock(FVector(0.f, 0.f, 80.f), FVector(36.f, 36.f, 160.f), Floor, false);

	const FString Title = Anchor.IsNone() ? StopId.ToString() : Anchor.ToString();
	AddLabel(FString::Printf(TEXT("%s ferry"), *Title), FVector(0.f, 0.f, 200.f));

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		const FVector At = FGFFerryStandLayout::DestLocal(Index);
		const FString Name = Anchors.IsValidIndex(Index) ? Anchors[Index].ToString() : Ids[Index].ToString();
		AddBlock(At + FVector(0.f, 0.f, -70.f), FVector(50.f, 50.f, 20.f), Fly, false);
		AddLabel(Name, At + FVector(0.f, 0.f, 80.f));
		AddTrigger(Ids[Index], At);
	}

	const FVector PassAt = FGFFerryStandLayout::PassLocal();
	AddBlock(PassAt + FVector(0.f, 0.f, -70.f), FVector(50.f, 50.f, 20.f), Pass, false);
	AddLabel(TEXT("Pass"), PassAt + FVector(0.f, 0.f, 80.f));
	AddTrigger(GFFerryStandPrivate::PassPad, PassAt);
}

bool AGFFerryStand::PawnInPad(const APawn* Pawn, const UBoxComponent* Box) const
{
	if (!Pawn || !Box)
	{
		return false;
	}
	const FVector Local = Box->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const FVector Ext = Box->GetScaledBoxExtent();
	return FMath::Abs(Local.X) <= Ext.X && FMath::Abs(Local.Y) <= Ext.Y && FMath::Abs(Local.Z) <= Ext.Z;
}

void AGFFerryStand::PollPads()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SpotInside.SetNum(Triggers.Num());
	for (int32 Index = 0; Index < Triggers.Num(); ++Index)
	{
		UBoxComponent* Box = Triggers[Index];
		if (!Box || !TriggerDests.IsValidIndex(Index))
		{
			continue;
		}
		const FName Dest = TriggerDests[Index];
		TSet<TWeakObjectPtr<APawn>> Inside;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* Pawn = *It;
			const AGFPlayerState* PS = Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
			if (!PS || PS->PlayerKey.IsEmpty() || !PawnInPad(Pawn, Box))
			{
				continue;
			}
			if (const UGFFerryRiderComponent* Ride = Pawn->FindComponentByClass<UGFFerryRiderComponent>())
			{
				if (Ride->IsInFlight())
				{
					continue;
				}
			}
			const TWeakObjectPtr<APawn> Key(Pawn);
			Inside.Add(Key);
			if (SpotInside[Index].Contains(Key))
			{
				continue;
			}
			SpotInside[Index].Add(Key);
			if (Dest == GFFerryStandPrivate::PassPad)
			{
				TryBuyPass(Pawn);
			}
			else
			{
				TryDepart(Pawn, Dest);
			}
		}
		for (auto It = SpotInside[Index].CreateIterator(); It; ++It)
		{
			if (!Inside.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}
	}
}
