#include "GFGardenTypes.h"

float FGFPlantRow::GetTotalGrowSeconds() const
{
	float Total = 0.f;
	for (float S : StageSeconds)
	{
		Total += FMath::Max(0.f, S);
	}
	return Total;
}

int32 FGFPlantRow::StageForElapsed(float ElapsedSeconds) const
{
	float Acc = 0.f;
	for (int32 i = 0; i < StageSeconds.Num(); ++i)
	{
		Acc += FMath::Max(0.f, StageSeconds[i]);
		if (ElapsedSeconds < Acc)
		{
			return i;
		}
	}
	return StageSeconds.Num();
}
