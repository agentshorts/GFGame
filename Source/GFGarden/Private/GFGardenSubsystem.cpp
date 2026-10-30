#include "GFGardenSubsystem.h"
#include "GFGardenPlot.h"

void UGFGardenSubsystem::RegisterPlot(AGFGardenPlot* Plot)
{
	Plots.AddUnique(Plot);
}

void UGFGardenSubsystem::UnregisterPlot(AGFGardenPlot* Plot)
{
	Plots.Remove(Plot);
}

void UGFGardenSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float Dt = DeltaTime * GrowthRate;
	for (int32 i = Plots.Num() - 1; i >= 0; --i)
	{
		if (AGFGardenPlot* Plot = Plots[i].Get())
		{
			Plot->AdvanceGrowth(Dt);
		}
		else
		{
			Plots.RemoveAtSwap(i);
		}
	}
}

TStatId UGFGardenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFGardenSubsystem, STATGROUP_Tickables);
}

bool UGFGardenSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
