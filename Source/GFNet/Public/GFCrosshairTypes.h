#pragma once

// Per-player crosshair and aim preferences. They travel inside FGFCharacterData (UGFCharacterSave).

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "GFCrosshairTypes.generated.h"

UENUM(BlueprintType)
enum class EGFCrosshairShape : uint8
{
	Plus UMETA(DisplayName = "Plus"),
	Dot UMETA(DisplayName = "Dot"),
	Circle UMETA(DisplayName = "Circle"),
	PlusDot UMETA(DisplayName = "Plus+Dot"),
	T UMETA(DisplayName = "T"),
	Chevron UMETA(DisplayName = "Chevron")
};

/**
 * What DrawCrosshair paints. Defaults are the previous hard-coded plus: arm 10, gap 4, thickness 1.5, white at 90%.
 * A fresh profile therefore looks the same until someone edits it.
 */
USTRUCT(BlueprintType)
struct GFNET_API FGFCrosshairSettings
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	EGFCrosshairShape Shape = EGFCrosshairShape::Plus;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor Color = FLinearColor::White;

	/** Arm length, pixels. Dot and circle use this as their radius. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float Size = 10.f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float Gap = 4.f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float Thickness = 1.5f;

	/** 0..1. The share code writes this as a percent. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float Opacity = 0.9f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bOutline = false;

	/** 1 or 2 when the outline is on. Ignored while bOutline is false. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	float OutlinePx = 1.f;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bCenterDot = false;

	/** Gap grows while the pawn is moving or a shot is in flight. Off, so the default plus stays put. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bDynamicSpread = false;

	/**
	 * While aiming, keep drawing this crosshair. A sniper scope overlay hides it either way.
	 * Off hides the crosshair for the whole aim blend.
	 */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bSeparateAdsCrosshair = true;

	void Clamp();
	bool Matches(const FGFCrosshairSettings& Other) const;

	/** Full code, every field, so export then import is the same settings. */
	FString ToShareCode() const;

	/**
	 * Accepts the full code and the short form GF-X:plus/ffffff/10/4/1.5/90.
	 * Short form fills outline, dot, spread and the ADS crosshair from the defaults.
	 * On failure Out is left alone.
	 */
	static bool FromShareCode(const FString& Code, FGFCrosshairSettings& Out);

	static FGFCrosshairSettings Defaults() { return FGFCrosshairSettings(); }
};

/** Hold-to-aim, the sensitivity slider, and whether snipers draw a scope. Saved with the crosshair. */
USTRUCT(BlueprintType)
struct GFNET_API FGFAimUserSettings
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Aim")
	bool bHoldToAim = true;

	/** Multiplier on the FOV sensitivity formula. 1 is 100%. Clamped to 0.5..1.5 (50..150%). */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Aim")
	float SensitivityScale = 1.f;

	/** Sniper-type weapons draw the black scope. Fists never do. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category = "Aim")
	bool bScopeOverlay = true;

	void Clamp();
	bool Matches(const FGFAimUserSettings& Other) const;
};

/** Named swatches the settings panel offers. RGB is sRGB, the same bytes the share code stores. */
struct GFNET_API FGFCrosshairPreset
{
	const TCHAR* Name = TEXT("");
	FColor Color = FColor::White;
};

GFNET_API TArrayView<const FGFCrosshairPreset> GFCrosshairPresets();
