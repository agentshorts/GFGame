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
	if (!Row || Row->NaturalSpeedCms <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp(GroundSpeedCms / Row->NaturalSpeedCms, 0.25f, 3.f);
}
