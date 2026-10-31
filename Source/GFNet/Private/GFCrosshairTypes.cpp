#include "GFCrosshairTypes.h"

namespace
{
	FString CompactNumber(float Value)
	{
		FString Text = FString::Printf(TEXT("%.4f"), Value);
		while (Text.Contains(TEXT(".")) && (Text.EndsWith(TEXT("0")) || Text.EndsWith(TEXT("."))))
		{
			if (Text.EndsWith(TEXT(".")))
			{
				Text.LeftChopInline(1);
				break;
			}
			Text.LeftChopInline(1);
		}
		if (Text.IsEmpty() || Text == TEXT("-"))
		{
			return TEXT("0");
		}
		return Text;
	}

	const TCHAR* ShapeToken(EGFCrosshairShape Shape)
	{
		switch (Shape)
		{
		case EGFCrosshairShape::Dot: return TEXT("dot");
		case EGFCrosshairShape::Circle: return TEXT("circle");
		case EGFCrosshairShape::PlusDot: return TEXT("plusdot");
		case EGFCrosshairShape::T: return TEXT("t");
		case EGFCrosshairShape::Chevron: return TEXT("chevron");
		default: return TEXT("plus");
		}
	}

	bool ParseShape(const FString& Token, EGFCrosshairShape& Out)
	{
		if (Token == TEXT("plus")) Out = EGFCrosshairShape::Plus;
		else if (Token == TEXT("dot")) Out = EGFCrosshairShape::Dot;
		else if (Token == TEXT("circle")) Out = EGFCrosshairShape::Circle;
		else if (Token == TEXT("plusdot") || Token == TEXT("plus+dot")) Out = EGFCrosshairShape::PlusDot;
		else if (Token == TEXT("t")) Out = EGFCrosshairShape::T;
		else if (Token == TEXT("chevron")) Out = EGFCrosshairShape::Chevron;
		else return false;
		return true;
	}

	int32 Nibble(TCHAR C)
	{
		if (C >= TEXT('0') && C <= TEXT('9')) return C - TEXT('0');
		if (C >= TEXT('a') && C <= TEXT('f')) return C - TEXT('a') + 10;
		if (C >= TEXT('A') && C <= TEXT('F')) return C - TEXT('A') + 10;
		return -1;
	}

	bool ParseHexByte(TCHAR HiChar, TCHAR LoChar, uint8& Out)
	{
		const int32 Hi = Nibble(HiChar);
		const int32 Lo = Nibble(LoChar);
		if (Hi < 0 || Lo < 0)
		{
			return false;
		}
		Out = static_cast<uint8>((Hi << 4) | Lo);
		return true;
	}

	bool ParseHexColor(FString Token, FLinearColor& Out)
	{
		if (Token.StartsWith(TEXT("#")))
		{
			Token.RightChopInline(1);
		}
		if (Token.Len() != 6)
		{
			return false;
		}
		uint8 R = 0, G = 0, B = 0;
		if (!ParseHexByte(Token[0], Token[1], R) || !ParseHexByte(Token[2], Token[3], G) || !ParseHexByte(Token[4], Token[5], B))
		{
			return false;
		}
		Out = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
		return true;
	}

	bool ParseFlag(const FString& Token, bool& Out)
	{
		if (Token == TEXT("1") || Token == TEXT("on") || Token == TEXT("true"))
		{
			Out = true;
			return true;
		}
		if (Token == TEXT("0") || Token == TEXT("off") || Token == TEXT("false"))
		{
			Out = false;
			return true;
		}
		return false;
	}

	const FGFCrosshairPreset Presets[] = {
		{ TEXT("White"), FColor(255, 255, 255) },
		{ TEXT("Lime"), FColor(50, 215, 75) },
		{ TEXT("Cyan"), FColor(0, 255, 255) },
		{ TEXT("Magenta"), FColor(255, 0, 255) },
		{ TEXT("Yellow"), FColor(255, 255, 0) },
		{ TEXT("Red"), FColor(255, 0, 0) },
	};
}

void FGFCrosshairSettings::Clamp()
{
	Size = FMath::Clamp(Size, 2.f, 40.f);
	Gap = FMath::Clamp(Gap, 0.f, 20.f);
	Thickness = FMath::Clamp(Thickness, 1.f, 6.f);
	Opacity = FMath::Clamp(Opacity, 0.f, 1.f);
	OutlinePx = FMath::Clamp(OutlinePx, 1.f, 2.f);
	Color.R = FMath::Clamp(Color.R, 0.f, 1.f);
	Color.G = FMath::Clamp(Color.G, 0.f, 1.f);
	Color.B = FMath::Clamp(Color.B, 0.f, 1.f);
	Color.A = 1.f;
}

bool FGFCrosshairSettings::Matches(const FGFCrosshairSettings& Other) const
{
	return Shape == Other.Shape
		&& Color.Equals(Other.Color, 0.004f)
		&& FMath::IsNearlyEqual(Size, Other.Size, 0.01f)
		&& FMath::IsNearlyEqual(Gap, Other.Gap, 0.01f)
		&& FMath::IsNearlyEqual(Thickness, Other.Thickness, 0.01f)
		&& FMath::IsNearlyEqual(Opacity, Other.Opacity, 0.001f)
		&& bOutline == Other.bOutline
		&& FMath::IsNearlyEqual(OutlinePx, Other.OutlinePx, 0.01f)
		&& bCenterDot == Other.bCenterDot
		&& bDynamicSpread == Other.bDynamicSpread
		&& bSeparateAdsCrosshair == Other.bSeparateAdsCrosshair;
}

FString FGFCrosshairSettings::ToShareCode() const
{
	FGFCrosshairSettings Copy = *this;
	Copy.Clamp();
	const FColor Bytes = Copy.Color.ToFColor(true);
	const int32 Percent = FMath::Clamp(FMath::RoundToInt(Copy.Opacity * 100.f), 0, 100);
	const int32 Outline = Copy.bOutline ? FMath::Clamp(FMath::RoundToInt(Copy.OutlinePx), 1, 2) : 0;
	return FString::Printf(TEXT("GF-X:%s/%02x%02x%02x/%s/%s/%s/%d/%d/%d/%d/%d"),
		ShapeToken(Copy.Shape),
		Bytes.R, Bytes.G, Bytes.B,
		*CompactNumber(Copy.Size),
		*CompactNumber(Copy.Gap),
		*CompactNumber(Copy.Thickness),
		Percent,
		Outline,
		Copy.bCenterDot ? 1 : 0,
		Copy.bDynamicSpread ? 1 : 0,
		Copy.bSeparateAdsCrosshair ? 1 : 0);
}

bool FGFCrosshairSettings::FromShareCode(const FString& Code, FGFCrosshairSettings& Out)
{
	FString Rest = Code;
	Rest.TrimStartAndEndInline();
	if (!Rest.StartsWith(TEXT("GF-X:"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	Rest.RightChopInline(5);
	TArray<FString> Parts;
	Rest.ParseIntoArray(Parts, TEXT("/"), true);
	if (Parts.Num() != 6 && Parts.Num() != 10)
	{
		return false;
	}
	for (FString& Part : Parts)
	{
		Part.TrimStartAndEndInline();
		Part.ToLowerInline();
	}

	FGFCrosshairSettings Next = Defaults();
	if (!ParseShape(Parts[0], Next.Shape) || !ParseHexColor(Parts[1], Next.Color))
	{
		return false;
	}
	Next.Size = FCString::Atof(*Parts[2]);
	Next.Gap = FCString::Atof(*Parts[3]);
	Next.Thickness = FCString::Atof(*Parts[4]);
	Next.Opacity = FCString::Atof(*Parts[5]) / 100.f;
	if (Parts.Num() == 10)
	{
		const float Outline = FCString::Atof(*Parts[6]);
		Next.bOutline = Outline >= 0.5f;
		Next.OutlinePx = Next.bOutline ? Outline : 1.f;
		if (!ParseFlag(Parts[7], Next.bCenterDot) || !ParseFlag(Parts[8], Next.bDynamicSpread) || !ParseFlag(Parts[9], Next.bSeparateAdsCrosshair))
		{
			return false;
		}
	}
	Next.Clamp();
	Out = Next;
	return true;
}

void FGFAimUserSettings::Clamp()
{
	SensitivityScale = FMath::Clamp(SensitivityScale, 0.5f, 1.5f);
}

bool FGFAimUserSettings::Matches(const FGFAimUserSettings& Other) const
{
	return bHoldToAim == Other.bHoldToAim
		&& bScopeOverlay == Other.bScopeOverlay
		&& FMath::IsNearlyEqual(SensitivityScale, Other.SensitivityScale, 0.001f);
}

TArrayView<const FGFCrosshairPreset> GFCrosshairPresets()
{
	return TArrayView<const FGFCrosshairPreset>(Presets, UE_ARRAY_COUNT(Presets));
}
