#include "GFNoise.h"

FGFOnNoise& FGFNoise::OnNoise()
{
	static FGFOnNoise Delegate;
	return Delegate;
}

void FGFNoise::Raise(UWorld* World, const FGFNoiseEvent& Noise)
{
	if (World)
	{
		OnNoise().Broadcast(World, Noise);
	}
}
