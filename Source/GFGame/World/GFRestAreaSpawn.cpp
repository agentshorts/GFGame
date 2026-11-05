#include "GFRestAreaSpawn.h"

const FName FGFRestAreaSpawn::StartTag(TEXT("RestArea"));
const FName FGFRestAreaSpawn::NpcSpotTag(TEXT("GFRestAreaNpcSpot"));
const FName FGFRestAreaSpawn::GatherZoneTag(TEXT("GatherZone_RestArea"));
const FName FGFRestAreaSpawn::CampfireLightTag(TEXT("GFRestAreaCampfireLight"));

int32 FGFRestAreaSpawn::RestAreaIndex(const TArray<FGFMapPlayerStart>& Starts)
{
	return Starts.IndexOfByPredicate([](const FGFMapPlayerStart& Start) { return Start.Tag == StartTag; });
}

FGFRestAreaSpawnChoice FGFRestAreaSpawn::Choose(const TArray<FGFMapPlayerStart>& Starts, bool bHasHome, FName AnchorId)
{
	FGFRestAreaSpawnChoice Choice;
	const int32 Rest = RestAreaIndex(Starts);
	if (Rest != INDEX_NONE)
	{
		Choice.Source = bHasHome ? EGFSpawnSource::Home : EGFSpawnSource::RestArea;
		Choice.Index = bHasHome ? INDEX_NONE : Rest;
		return Choice;
	}
	Choice.Source = EGFSpawnSource::Map;
	Choice.Index = FGFPlayerStartRules::ChooseStartIndex(Starts, AnchorId);
	return Choice;
}
