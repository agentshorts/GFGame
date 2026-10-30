#include "GFJailCell.h"
#include "Components/BoxComponent.h"

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
