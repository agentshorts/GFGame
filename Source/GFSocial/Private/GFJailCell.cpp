#include "GFJailCell.h"
#include "Components/BoxComponent.h"
#include "GFSafeZone.h"

AGFJailCell::AGFJailCell()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Floor = CreateDefaultSubobject<UBoxComponent>(TEXT("Floor"));
	Floor->SetupAttachment(RootComponent);
	Floor->SetBoxExtent(FVector(HalfX + Wall, HalfX + Wall, Wall));
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetRelativeLocation(FVector(0.f, 0.f, -Wall)); // floor top at the actor's Z

	auto AddBox = [this](const TCHAR* Name, const FVector& Extent, const FVector& At)
	{
		UBoxComponent* B = CreateDefaultSubobject<UBoxComponent>(Name);
		B->SetupAttachment(RootComponent);
		B->SetBoxExtent(Extent);
		B->SetRelativeLocation(At);
		B->SetCollisionProfileName(TEXT("BlockAll"));
		Walls.Add(B);
	};
	const float H = HalfZ * 2.f;
	AddBox(TEXT("WallN"), FVector(HalfX + Wall, Wall, HalfZ), FVector(0.f, HalfX + Wall, HalfZ));
	AddBox(TEXT("WallS"), FVector(HalfX + Wall, Wall, HalfZ), FVector(0.f, -HalfX - Wall, HalfZ));
	AddBox(TEXT("WallE"), FVector(Wall, HalfX + Wall, HalfZ), FVector(HalfX + Wall, 0.f, HalfZ));
	AddBox(TEXT("WallW"), FVector(Wall, HalfX + Wall, HalfZ), FVector(-HalfX - Wall, 0.f, HalfZ));
	AddBox(TEXT("Ceiling"), FVector(HalfX + Wall, HalfX + Wall, Wall), FVector(0.f, 0.f, H + Wall));
}

FBox AGFJailCell::GetCellBox() const
{
	const FVector L = GetActorLocation();
	const float XY = HalfX + 2.f * Wall;
	return FBox(L + FVector(-XY, -XY, -2.f * Wall), L + FVector(XY, XY, HalfZ * 2.f + 2.f * Wall));
}

void AGFJailCell::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Registered at spawn (not BeginPlay) so the box exists as soon as the cell does; a destroyed cell drops out on
	// its own (weak owner) even if EndPlay never runs.
	if (UGFSafeZoneSubsystem* Sub = UGFSafeZoneSubsystem::Get(this))
	{
		Sub->SetNoEntry(this, GetCellBox());
	}
}

void AGFJailCell::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UGFSafeZoneSubsystem* Sub = UGFSafeZoneSubsystem::Get(this))
	{
		Sub->ClearNoEntry(this);
	}
	Super::EndPlay(Reason);
}

