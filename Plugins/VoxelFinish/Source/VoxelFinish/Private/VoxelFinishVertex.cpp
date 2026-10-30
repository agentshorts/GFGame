#include "VoxelFinish.h"

void VoxelFinish::WriteFinishToVertex(uint16 FinishId, const FVector2f& FaceUVs, FVector2f& OutUV2)
{
	const int32 TexelX = FMath::Clamp(FMath::FloorToInt(FaceUVs.X * static_cast<float>(AtlasCellTexels)), 0, AtlasCellTexels - 1);
	const int32 TexelY = FMath::Clamp(FMath::FloorToInt(FaceUVs.Y * static_cast<float>(AtlasCellTexels)), 0, AtlasCellTexels - 1);
	// Finish ids through 65535 are exact float32 integers. UV2.Y is the face texel (x + y * 16).
	OutUV2.X = static_cast<float>(FinishId);
	OutUV2.Y = static_cast<float>(TexelX + TexelY * AtlasCellTexels);
}

void VoxelFinish::DecodeFinishVertex(const FVector2f& UV2, uint16& OutFinishId, FVector2f& OutFaceUV)
{
	const int32 Id = FMath::Clamp(FMath::RoundToInt(UV2.X), 0, 65535);
	OutFinishId = static_cast<uint16>(Id);
	const int32 Packed = FMath::Clamp(FMath::RoundToInt(UV2.Y), 0, AtlasCellTexels * AtlasCellTexels - 1);
	const int32 TexelX = Packed % AtlasCellTexels;
	const int32 TexelY = Packed / AtlasCellTexels;
	const float Denom = static_cast<float>(AtlasCellTexels);
	OutFaceUV.X = (static_cast<float>(TexelX) + 0.5f) / Denom;
	OutFaceUV.Y = (static_cast<float>(TexelY) + 0.5f) / Denom;
}
