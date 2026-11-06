#include "GFMiningFeel.h"
#include "GFMagicRules.h"
#include "GFMagicTables.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

bool FGFTargetedCellFraction::Matches(const FGFTargetedCellFraction& Other) const
{
	return bActive == Other.bActive
		&& bTown == Other.bTown
		&& Cell == Other.Cell
		&& NodeKey == Other.NodeKey
		&& FMath::IsNearlyEqual(Fraction, Other.Fraction, 0.005f);
}

FGFCrackParams FGFCrackParams::FromFraction(float Fraction)
{
	FGFCrackParams Out;
	const float Remaining = FMath::Clamp(Fraction, 0.f, 1.f);
	Out.Stage = FGFMiningFX::CrackStage(Remaining, 1.f);
	Out.Crack75 = Out.Stage >= 1 ? 1.f : 0.f;
	Out.Crack50 = Out.Stage >= 2 ? 1.f : 0.f;
	Out.Crack25 = Out.Stage >= 3 ? 1.f : 0.f;
	return Out;
}

void FGFMiningCrack::Apply(UPrimitiveComponent* Prim, float Fraction)
{
	if (!Prim)
	{
		return;
	}
	const FGFCrackParams Params = FGFCrackParams::FromFraction(Fraction);
	Prim->SetCustomPrimitiveDataFloat(Index75, Params.Crack75);
	Prim->SetCustomPrimitiveDataFloat(Index50, Params.Crack50);
	Prim->SetCustomPrimitiveDataFloat(Index25, Params.Crack25);
	const int32 Slots = Prim->GetNumMaterials();
	for (int32 Index = 0; Index < Slots; ++Index)
	{
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Prim->GetMaterial(Index));
		if (!Mid)
		{
			continue;
		}
		Mid->SetScalarParameterValue(Param75(), Params.Crack75);
		Mid->SetScalarParameterValue(Param50(), Params.Crack50);
		Mid->SetScalarParameterValue(Param25(), Params.Crack25);
	}
}

void FGFMiningCrack::Clear(UPrimitiveComponent* Prim)
{
	Apply(Prim, 1.f);
}

bool FGFMiningCrack::Read(const UPrimitiveComponent* Prim, FGFCrackParams& Out)
{
	if (!Prim)
	{
		return false;
	}
	const TArray<float>& Data = Prim->GetCustomPrimitiveData().Data;
	Out.Crack75 = Data.IsValidIndex(Index75) ? Data[Index75] : 0.f;
	Out.Crack50 = Data.IsValidIndex(Index50) ? Data[Index50] : 0.f;
	Out.Crack25 = Data.IsValidIndex(Index25) ? Data[Index25] : 0.f;
	Out.Stage = Out.Crack25 >= 0.5f ? 3 : Out.Crack50 >= 0.5f ? 2 : Out.Crack75 >= 0.5f ? 1 : 0;
	return true;
}

bool FGFMiningFeelRules::FieldRises(float HeldSeconds, float Threshold)
{
	return Threshold > 0.f && HeldSeconds + KINDA_SMALL_NUMBER >= Threshold;
}

float FGFMiningFeelRules::ClampHitStop(float Seconds)
{
	return FMath::Clamp(Seconds, MinBreakHitStopSeconds, MaxBreakHitStopSeconds);
}

float FGFMiningFeelRules::FractionLoss(FName BlockId, EGFMagicElement Element, float IntegrityPerSecond, float Rate, float Dt)
{
	if (BlockId.IsNone() || Dt <= 0.f || IntegrityPerSecond <= 0.f || Rate <= 0.f)
	{
		return 0.f;
	}
	const float Full = FGFMagicRules::IntegrityOf(FMath::Max(0.05f, FGFMagicCatalog::BlockHardness(BlockId)));
	if (Full <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const float Mult = FGFMagicRules::Multiplier(FGFMagicCatalog::AffinityOrDefault(BlockId), Element);
	if (Mult <= 0.f)
	{
		return 0.f;
	}
	return (IntegrityPerSecond * Mult * Rate * Dt) / Full;
}

FGFMiningFeelResponse FGFMiningFeelRules::ResponseFor(EGFMiningFXKind Kind)
{
	FGFMiningFeelResponse Out;
	if (Kind == EGFMiningFXKind::Hit || Kind == EGFMiningFXKind::Crack)
	{
		Out.bShake = true;
		Out.bCrosshair = true;
	}
	else if (Kind == EGFMiningFXKind::Break)
	{
		Out.bShake = true;
		Out.bCrosshair = true;
		Out.bHitStop = true;
	}
	return Out;
}

void FGFCrackClient::BeginTarget(bool bInTown, const FIntVector& InCell, const FString& InKey)
{
	const bool bChanged = !bHasTarget || bTown != bInTown || (bInTown ? NodeKey != InKey : Cell != InCell);
	bTown = bInTown;
	Cell = InCell;
	NodeKey = InKey;
	bHasTarget = true;
	if (bChanged)
	{
		Predicted = 1.f;
	}
}

void FGFCrackClient::PredictStep(float InLoss)
{
	if (!bHasTarget || InLoss <= 0.f)
	{
		return;
	}
	Predicted = FMath::Max(FGFMiningFeelRules::PredictFloor, Predicted - InLoss);
}

void FGFCrackClient::Reconcile(const FGFTargetedCellFraction& Server, float Tolerance)
{
	bRolledBack = false;
	if (Server.Serial <= 0)
	{
		return;
	}
	const float Tol = FMath::Max(0.f, Tolerance);
	if (!Server.bActive)
	{
		if (Predicted < 0.999f)
		{
			Predicted = 1.f;
			bRolledBack = true;
		}
		bHasTarget = false;
		return;
	}
	const bool bSame = bHasTarget && Server.bTown == bTown && (Server.bTown ? Server.NodeKey == NodeKey : Server.Cell == Cell);
	if (!bSame)
	{
		const float Before = Predicted;
		bTown = Server.bTown;
		Cell = Server.Cell;
		NodeKey = Server.NodeKey;
		bHasTarget = true;
		Predicted = Server.Fraction <= 0.f ? 0.f : Server.Fraction;
		bRolledBack = Before + Tol < Server.Fraction;
		return;
	}
	if (Server.Fraction + KINDA_SMALL_NUMBER < Predicted)
	{
		Predicted = Server.Fraction <= 0.f ? 0.f : Server.Fraction;
		return;
	}
	if (Server.Fraction > Predicted + Tol)
	{
		Predicted = Server.Fraction;
		bRolledBack = true;
	}
}

AGFMiningCrackMarker::AGFMiningCrackMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicateMovement(false);
	Plate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plate"));
	SetRootComponent(Plate);
	Plate->SetMobility(EComponentMobility::Movable);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetGenerateOverlapEvents(false);
	Plate->SetCastShadow(false);
}

UStaticMeshComponent* AGFMiningCrackMarker::GetPlate() const
{
	return Plate.Get();
}

void AGFMiningCrackMarker::Show(const FVector& At, const FVector& Normal, float Fraction)
{
	const FGFCrackParams Params = FGFCrackParams::FromFraction(Fraction);
	if (!Plate || Params.Stage <= 0)
	{
		Hide();
		return;
	}
	if (!Plate->GetStaticMesh())
	{
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			Plate->SetStaticMesh(Cube);
		}
	}
	SetActorHiddenInGame(false);
	const FVector N = Normal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	SetActorLocation(At + N * 3.f);
	SetActorRotation(N.ToOrientationRotator());
	const float Width = 0.16f + 0.04f * static_cast<float>(Params.Stage);
	SetActorScale3D(FVector(Width, Width, 0.012f));
	if (!Cast<UMaterialInstanceDynamic>(Plate->GetMaterial(0)))
	{
		UMaterialInterface* Base = Plate->GetMaterial(0);
		if (!Base)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		if (Base)
		{
			Plate->SetMaterial(0, UMaterialInstanceDynamic::Create(Base, Plate));
		}
	}
	FGFMiningCrack::Apply(Plate, Fraction);
	const float Ink = 0.08f + 0.16f * static_cast<float>(Params.Stage);
	Plate->SetVectorParameterValueOnMaterials(TEXT("Color"), FLinearColor(Ink, Ink * 0.85f, Ink * 0.7f, 1.f));
}

void AGFMiningCrackMarker::Hide()
{
	SetActorHiddenInGame(true);
	if (Plate)
	{
		FGFMiningCrack::Clear(Plate);
	}
}
