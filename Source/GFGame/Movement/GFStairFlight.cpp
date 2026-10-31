#include "GFStairFlight.h"

#include "GFAssetMap.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AGFStairFlight::AGFStairFlight()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Tags.Add(TEXT("GFStair"));
}

void AGFStairFlight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFStairFlight, Spec);
}

void AGFStairFlight::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AGFStairFlight::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
}

void AGFStairFlight::OnRep_Spec()
{
	Rebuild();
}

void AGFStairFlight::ClearParts()
{
	for (UPrimitiveComponent* Part : Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Empty();
	Ramp = nullptr;
	bPartsBuilt = false;
}

void AGFStairFlight::RampPlacement(const FGFStairFlightSpec& InSpec, FVector& OutCenter, FRotator& OutRotation, FVector& OutHalfExtent)
{
	const float Rise = InSpec.RiseCm();
	const float Run = InSpec.RunCm();
	const float Pitch = InSpec.PitchDegrees();
	const float Slope = FMath::Sqrt(Rise * Rise + Run * Run);
	OutRotation = FRotator(Pitch, 0.f, 0.f);
	const FVector Mid(Run * 0.5f, 0.f, Rise * 0.5f);
	const FVector Normal = OutRotation.RotateVector(FVector::UpVector);
	OutCenter = Mid - Normal * (RampThicknessCm * 0.5f);
	OutHalfExtent = FVector(Slope * 0.5f, InSpec.WidthCm * 0.5f, RampThicknessCm * 0.5f);
}

void AGFStairFlight::Rebuild()
{
	if (Spec.Steps < 1 || Spec.RiserCm <= KINDA_SMALL_NUMBER || Spec.TreadCm <= KINDA_SMALL_NUMBER || Spec.WidthCm <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	if (bPartsBuilt && BuiltSteps == Spec.Steps
		&& FMath::IsNearlyEqual(BuiltRiser, Spec.RiserCm)
		&& FMath::IsNearlyEqual(BuiltTread, Spec.TreadCm)
		&& FMath::IsNearlyEqual(BuiltWidth, Spec.WidthCm))
	{
		return;
	}
	ClearParts();

	UStaticMesh* Cube = FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Cube);
	const FLinearColor Stone(0.55f, 0.52f, 0.46f, 1.f);

	for (int32 Index = 0; Index < Spec.Steps; ++Index)
	{
		const float Height = static_cast<float>(Index + 1) * Spec.RiserCm;
		UStaticMeshComponent* Step = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("StairStep_%d"), Index));
		Step->SetupAttachment(RootComponent);
		Step->SetStaticMesh(Cube);
		Step->SetRelativeScale3D(FVector(Spec.TreadCm / 100.f, Spec.WidthCm / 100.f, Height / 100.f));
		Step->SetRelativeLocation(FVector((static_cast<float>(Index) + 0.5f) * Spec.TreadCm, 0.f, Height * 0.5f));
		Step->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Step->SetCanEverAffectNavigation(false);
		Step->ComponentTags.Add(TEXT("GFStairStep"));
		if (UMaterialInterface* Tint = FGFAssetMap::MakeTint(Step, Stone))
		{
			Step->SetMaterial(0, Tint);
		}
		Step->RegisterComponent();
		Parts.Add(Step);
	}

	auto AddFloor = [this](const TCHAR* Name, const FVector& Center, const FVector& Half) -> UBoxComponent*
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(this, Name);
		Box->SetupAttachment(RootComponent);
		Box->SetBoxExtent(Half);
		Box->SetRelativeLocation(Center);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCanEverAffectNavigation(false);
		Box->RegisterComponent();
		Parts.Add(Box);
		return Box;
	};

	const float HalfWidth = Spec.WidthCm * 0.5f;
	const float PadHalfZ = 20.f;
	// Lower pad runs under the ramp start so the capsule meets the slope from a floor. Its top stays at Z=0, under the slope.
	AddFloor(TEXT("StairPad"), FVector(-50.f, 0.f, -PadHalfZ), FVector(70.f, HalfWidth, PadHalfZ));
	// Landing top is the flat at Rise. It starts at the end of the run so its front face does not cut the slope.
	AddFloor(TEXT("StairLanding"), FVector(Spec.RunCm() + 50.f, 0.f, Spec.RiseCm() - PadHalfZ), FVector(50.f, HalfWidth, PadHalfZ));

	FVector RampCenter = FVector::ZeroVector;
	FRotator RampRotation = FRotator::ZeroRotator;
	FVector RampHalf = FVector::ZeroVector;
	RampPlacement(Spec, RampCenter, RampRotation, RampHalf);
	Ramp = AddFloor(TEXT("StairRamp"), RampCenter, RampHalf);
	if (Ramp)
	{
		Ramp->SetRelativeRotation(RampRotation);
	}

	BuiltSteps = Spec.Steps;
	BuiltRiser = Spec.RiserCm;
	BuiltTread = Spec.TreadCm;
	BuiltWidth = Spec.WidthCm;
	bPartsBuilt = true;
}

TArray<UStaticMeshComponent*> AGFStairFlight::GetStepMeshes() const
{
	TArray<UStaticMeshComponent*> Out;
	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->ComponentHasTag(TEXT("GFStairStep")))
		{
			Out.Add(Mesh);
		}
	}
	Out.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
	{
		return A.GetRelativeLocation().X < B.GetRelativeLocation().X;
	});
	return Out;
}

bool AGFStairFlight::TreadTopZ(const FVector& WorldPoint, float& OutWorldZ) const
{
	if (Spec.Steps < 1 || Spec.TreadCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	const float HalfW = Spec.WidthCm * 0.5f;
	if (Local.Y < -HalfW || Local.Y > HalfW)
	{
		return false;
	}
	if (Local.X < -40.f || Local.X > Spec.RunCm() + 80.f)
	{
		return false;
	}
	float Top = 0.f;
	if (Local.X < 0.f)
	{
		Top = 0.f;
	}
	else if (Local.X >= Spec.RunCm())
	{
		Top = Spec.RiseCm();
	}
	else
	{
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Local.X / Spec.TreadCm), 0, Spec.Steps - 1);
		Top = static_cast<float>(Index + 1) * Spec.RiserCm;
	}
	OutWorldZ = GetActorTransform().TransformPosition(FVector(Local.X, Local.Y, Top)).Z;
	return true;
}

bool AGFStairFlight::SurfaceZAt(const UWorld* World, const FVector& WorldPoint, float& OutSurfaceZ)
{
	if (!World)
	{
		return false;
	}
	bool bHit = false;
	float Best = 0.f;
	for (TActorIterator<AGFStairFlight> It(World); It; ++It)
	{
		float Z = 0.f;
		if (It->TreadTopZ(WorldPoint, Z) && (!bHit || Z > Best))
		{
			Best = Z;
			bHit = true;
		}
	}
	if (bHit)
	{
		OutSurfaceZ = Best;
	}
	return bHit;
}

FVector AGFStairFlight::ApproachLocation(float CapsuleHalfHeight, float LocalY) const
{
	return GetActorTransform().TransformPosition(FVector(-70.f, LocalY, CapsuleHalfHeight + 2.f));
}

FVector AGFStairFlight::LandingLocation(float CapsuleHalfHeight, float LocalY) const
{
	return GetActorTransform().TransformPosition(FVector(Spec.RunCm() + 40.f, LocalY, Spec.RiseCm() + CapsuleHalfHeight + 2.f));
}
