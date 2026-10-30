#include "VoxelFinishLookup.h"

#include "Engine/Texture2D.h"

namespace VoxelFinishLookupPrivate
{
	static uint8 Channel(float V)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(V, 0.f, 1.f) * 255.f), 0, 255));
	}

	static FColor FromLinear(const FLinearColor& Color, uint8 Alpha)
	{
		return FColor(Channel(Color.R), Channel(Color.G), Channel(Color.B), Alpha);
	}

	static int32 Index(uint16 FinishId, int32 Row)
	{
		return Row * VoxelFinish::LookupTextureWidth + FinishId;
	}
}

void FVoxelFinishLookup::InitPixels(TArray<FColor>& OutPixels)
{
	OutPixels.SetNumZeroed(VoxelFinish::LookupTextureWidth * VoxelFinish::LookupTextureHeight);
}

void FVoxelFinishLookup::WriteFinish(TArray<FColor>& Pixels, uint16 FinishId, const FFinish& Finish)
{
	if (FinishId == VoxelFinish::UnpaintedId || FinishId >= VoxelFinish::LookupTextureWidth)
	{
		return;
	}
	if (Pixels.Num() != VoxelFinish::LookupTextureWidth * VoxelFinish::LookupTextureHeight)
	{
		InitPixels(Pixels);
	}
	const bool bUsePattern = Finish.PatternId >= 0 && Finish.PatternId < VoxelFinish::PatternAtlasGrid * VoxelFinish::PatternAtlasGrid;
	const int32 Pattern = bUsePattern ? Finish.PatternId : 0;
	const uint8 Intensity = static_cast<uint8>(FMath::Clamp(
		FMath::RoundToInt(FMath::Clamp(Finish.Intensity, 0.f, 8.f) / 8.f * 255.f), 0, 255));
	const bool bSparkle = Finish.HasEffect() && Finish.EffectId == VoxelFinish::SparkleEffectId;

	Pixels[VoxelFinishLookupPrivate::Index(FinishId, RowColorA)] = VoxelFinishLookupPrivate::FromLinear(Finish.ColorA, bUsePattern ? 255 : 0);
	Pixels[VoxelFinishLookupPrivate::Index(FinishId, RowColorB)] = VoxelFinishLookupPrivate::FromLinear(Finish.ColorB, Finish.bGlow ? 255 : 0);
	Pixels[VoxelFinishLookupPrivate::Index(FinishId, RowTint)] = VoxelFinishLookupPrivate::FromLinear(Finish.Tint, Intensity);
	Pixels[VoxelFinishLookupPrivate::Index(FinishId, RowMeta)] = FColor(bSparkle ? 255 : 0, static_cast<uint8>(Pattern), 0, 255);
}

const FName FVoxelFinishLookup::ParameterName(TEXT("FinishLookup"));

void FVoxelFinishLookup::ClearFinish(TArray<FColor>& Pixels, uint16 FinishId)
{
	if (FinishId >= VoxelFinish::LookupTextureWidth || Pixels.Num() != VoxelFinish::LookupTextureWidth * VoxelFinish::LookupTextureHeight)
	{
		return;
	}
	for (int32 Row = 0; Row < VoxelFinish::LookupTextureHeight; ++Row)
	{
		Pixels[VoxelFinishLookupPrivate::Index(FinishId, Row)] = FColor(0, 0, 0, 0);
	}
}

FColor FVoxelFinishLookup::Read(const TArray<FColor>& Pixels, uint16 FinishId, int32 Row)
{
	const int32 Index = VoxelFinishLookupPrivate::Index(FinishId, Row);
	if (!Pixels.IsValidIndex(Index))
	{
		return FColor::Black;
	}
	return Pixels[Index];
}

void FVoxelFinishLookup::Upload(UTexture2D* Texture, const TArray<FColor>& Pixels)
{
	if (!Texture || Pixels.Num() != VoxelFinish::LookupTextureWidth * VoxelFinish::LookupTextureHeight)
	{
		return;
	}
	FTexturePlatformData* Platform = Texture->GetPlatformData();
	if (!Platform || Platform->Mips.Num() == 0)
	{
		return;
	}
	FTexture2DMipMap& Mip = Platform->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!Data)
	{
		Mip.BulkData.Unlock();
		return;
	}
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
}
