#include "GFShardstone.h"

#include "GFZoneTypes.h"
#include "Net/UnrealNetwork.h"

AGFShardstone::AGFShardstone()
{
	bAlwaysRelevant = true;
}

void AGFShardstone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFShardstone, StoneId);
}

void AGFShardstone::Setup(const FGFShardstoneRow& Row, const FLinearColor& Color)
{
	StoneId = Row.Id;
	Configure(EGFPlaceholderShape::Cylinder, Color, FVector(3.f, 3.f, 4.f), 0.f, true);
	SetActorLocation(FVector(Row.Location.X, Row.Location.Y, 0.f));
}
