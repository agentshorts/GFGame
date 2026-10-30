#include "VoxelFinishTypes.h"

namespace VoxelFinishHash
{
	static uint8 QuantizeChannel(float V)
	{
		const float Clamped = FMath::Clamp(V, 0.f, 1.f);
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Clamped * 255.f), 0, 255));
	}

	static void AppendColor(TArray<uint8>& Bytes, const FLinearColor& Color)
	{
		Bytes.Add(QuantizeChannel(Color.R));
		Bytes.Add(QuantizeChannel(Color.G));
		Bytes.Add(QuantizeChannel(Color.B));
		Bytes.Add(QuantizeChannel(Color.A));
	}

	static uint32 Fnv1a(const TArray<uint8>& Bytes)
	{
		uint32 Hash = 2166136261u;
		for (uint8 Byte : Bytes)
		{
			Hash ^= Byte;
			Hash *= 16777619u;
		}
		return Hash;
	}

	static void AppendKey(TArray<uint8>& Bytes, const FFinish& Finish)
	{
		AppendColor(Bytes, Finish.Tint);
		const int32 Pattern = FMath::Clamp(Finish.PatternId, 0, 255);
		Bytes.Add(static_cast<uint8>(Pattern));
		AppendColor(Bytes, Finish.ColorA);
		AppendColor(Bytes, Finish.ColorB);
		const uint32 EffectBits = static_cast<uint32>(Finish.EffectId);
		Bytes.Add(static_cast<uint8>(EffectBits & 0xFF));
		Bytes.Add(static_cast<uint8>((EffectBits >> 8) & 0xFF));
		Bytes.Add(static_cast<uint8>((EffectBits >> 16) & 0xFF));
		Bytes.Add(static_cast<uint8>((EffectBits >> 24) & 0xFF));
		const int32 Milli = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Finish.Intensity, 0.f, 8.f) * 1000.f), 0, 8000);
		Bytes.Add(static_cast<uint8>(Milli & 0xFF));
		Bytes.Add(static_cast<uint8>((Milli >> 8) & 0xFF));
		Bytes.Add(Finish.bGlow ? 1 : 0);
	}
}

uint32 FFinish::GetCanonicalHash() const
{
	TArray<uint8> Bytes;
	Bytes.Reserve(24);
	VoxelFinishHash::AppendKey(Bytes, *this);
	return VoxelFinishHash::Fnv1a(Bytes);
}

bool FFinish::CanonicallyEquals(const FFinish& Other) const
{
	TArray<uint8> A;
	TArray<uint8> B;
	VoxelFinishHash::AppendKey(A, *this);
	VoxelFinishHash::AppendKey(B, Other);
	return A == B;
}
