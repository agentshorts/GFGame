#include "GFAnimMap.h"
#include "GFDataTables.h"
#include "Animation/AnimSequenceBase.h"

const FGFAnimMapRow* FGFAnimMap::Find(FName Key)
{
	UDataTable* Table = FGFDataTables::Get<FGFAnimMapRow>(TEXT("DT_AnimMap"));
	return Table ? Table->FindRow<FGFAnimMapRow>(Key, TEXT("GFAnimMap"), false) : nullptr;
}

UAnimSequenceBase* FGFAnimMap::Load(FName Key)
{
	const FGFAnimMapRow* Row = Find(Key);
	return (Row && !Row->Anim.IsNull()) ? Row->Anim.LoadSynchronous() : nullptr;
}

float FGFAnimMap::PlayRateFor(FName Key, float GroundSpeedCms)
{
	const FGFAnimMapRow* Row = Find(Key);
	return RateFromNaturalSpeed(GroundSpeedCms, Row ? Row->NaturalSpeedCms : 0.f);
}

float FGFAnimMap::NaturalSpeedFor(FName Key, float FallbackCms)
{
	const FGFAnimMapRow* Row = Find(Key);
	return (Row && Row->NaturalSpeedCms > 0.f) ? Row->NaturalSpeedCms : FallbackCms;
}

float FGFAnimMap::RateFromNaturalSpeed(float SpeedCms, float NaturalSpeedCms)
{
	if (NaturalSpeedCms <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(SpeedCms / NaturalSpeedCms, MinPlayRate, MaxPlayRate);
}
