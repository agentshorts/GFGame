#include "GFShardstone.h"

#include "GFHubDressing.h"
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
	Configure(EGFPlaceholderShape::Cylinder, Color, GFHubDressing::ShardstoneScale(), 0.f, true);
	SetActorLocation(FVector(Row.Location.X, Row.Location.Y, 0.f));
}
