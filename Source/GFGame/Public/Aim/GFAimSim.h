#pragma once

// Aim, shoulder camera and crosshair drawing. No actors. The character and the HUD call these; tests lock them.
// Movement speeds, jump and the grapple are not in here.

#include "CoreMinimal.h"
#include "GFCrosshairTypes.h"

namespace GFAim
{
	constexpr float HipFov = 90.f;
	constexpr float WeaponAdsFov = 45.f;
	constexpr float SniperAdsFov = 20.f;
	constexpr float FistAdsFov = 80.f;
	constexpr float AdsInSeconds = 0.10f;
	constexpr float AdsOutSeconds = 0.10f;
	constexpr float WeaponSwitchSeconds = 0.15f;
	constexpr float FireBufferSeconds = 0.10f;
	constexpr float FireRecoverySeconds = 0.25f;
	constexpr float ShoulderBlendSeconds = 0.15f;
	constexpr float HipSpread = 2.f;
	constexpr float AdsSpread = 0.25f;
	constexpr float HipPunchDamage = 12.f;
	constexpr float WeaponDamage = 20.f;
	constexpr float FistReach = 250.f;
	constexpr float MaxAimRange = 10000.f;
	constexpr float ArmLength = 300.f;
	constexpr float ArmLengthMin = 150.f;
	constexpr float ArmLengthMax = 800.f;
	constexpr float ArmLengthStep = 40.f;
	constexpr float CameraLagSpeed = 12.f;
	constexpr float CameraFadeDistance = 40.f;
	constexpr float CameraFadeHidden = 10.f;
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float SpreadGapBonus = 8.f;

	inline FVector ShoulderOffset() { return FVector(0.f, 60.f, 20.f); }
	inline FName FistsId() { return FName(TEXT("Fists")); }
	inline FName FistScopeAnim() { return FName(TEXT("Player.FistScope")); }
}

enum class EGFAimHeldKind : uint8
{
	Block,
	Fists,
	Weapon,
	Ability
};

enum class EGFWeaponProfile : uint8
{
	Normal,
	Sniper
};

enum class EGFAdsPhase : uint8
{
	Hip,
	Entering,
	Held,
	Leaving
};

enum class EGFAimEvent : uint8
{
	AimPressed,
	AimReleased,
	FirePressed,
	SwapWeapon,
	UseAbility,
	ShoulderSwap
};

enum class EGFTriggerUse : uint8
{
	PlaceBlock,
	Aim
};

struct GFGAME_API FGFAimTuning
{
	float HipFov = GFAim::HipFov;
	float WeaponAdsFov = GFAim::WeaponAdsFov;
	float SniperAdsFov = GFAim::SniperAdsFov;
	float FistAdsFov = GFAim::FistAdsFov;
	float AdsInSeconds = GFAim::AdsInSeconds;
	float AdsOutSeconds = GFAim::AdsOutSeconds;
	float WeaponSwitchSeconds = GFAim::WeaponSwitchSeconds;
	float FireBufferSeconds = GFAim::FireBufferSeconds;
	float FireRecoverySeconds = GFAim::FireRecoverySeconds;
	float ShoulderBlendSeconds = GFAim::ShoulderBlendSeconds;
	float HipSpread = GFAim::HipSpread;
	float AdsSpread = GFAim::AdsSpread;
	float HipPunchDamage = GFAim::HipPunchDamage;
	float WeaponDamage = GFAim::WeaponDamage;
	float FistReach = GFAim::FistReach;
	float MaxAimRange = GFAim::MaxAimRange;
	FVector ShoulderOffset = GFAim::ShoulderOffset();
};

struct GFGAME_API FGFAimState
{
	EGFAimHeldKind Held = EGFAimHeldKind::Block;
	EGFWeaponProfile Weapon = EGFWeaponProfile::Normal;
	int32 WeaponId = 0;
	bool bHoldToAim = true;
	bool bScopeOverlayEnabled = true;
	float SensitivityScale = 1.f;
	bool bSnoutZoom = false;

	bool bAdsIntent = false;
	EGFAdsPhase Phase = EGFAdsPhase::Hip;
	float PhaseAlpha = 0.f;
	bool bHasRecovery = false;
	EGFAimEvent RecoveryKind = EGFAimEvent::FirePressed;
	float RecoveryLeft = 0.f;

	float ShoulderFrom = 1.f;
	float ShoulderTo = 1.f;
	float ShoulderAlpha = 1.f;

	bool bMoving = false;
	bool bFiring = false;
	/** Seconds left to widen a dynamic crosshair after a shot. */
	float SpreadFireLeft = 0.f;
	bool bAbility = false;
	bool bFistCue = false;

	float Fov = GFAim::HipFov;
	float TargetFov = GFAim::HipFov;
	float LookScale = 1.f;
	bool bFistScope = false;
	FName Pose = NAME_None;
};

struct GFGAME_API FGFAimStepOut
{
	bool bFired = false;
	bool bAdsRay = false;
	bool bAdsAccuracy = false;
	float Spread = 0.f;
	float Damage = 0.f;
	bool bCancelled = false;
	bool bInputEaten = false;
	bool bAbility = false;
	bool bFistCue = false;
};

/** Block in hand places. Fists, a weapon, an ability, or a possessed arena dino aims. */
GFGAME_API EGFTriggerUse GFResolvePrimaryUse(EGFAimHeldKind Held, bool bSnoutZoom);

/** ADS sens = userScale * tan(adsFov/2) / tan(hipFov/2). userScale is clamped to 0.5..1.5. Hip aim returns 1. */
GFGAME_API float GFAimSensitivity(float HipFovDeg, float AdsFovDeg, float UserScale);

/** 1 at CameraFadeDistance and beyond, 0 at CameraFadeHidden and closer. */
GFGAME_API float GFCameraFadeAlpha(float CameraDistanceCm);

GFGAME_API float GFAimShoulderSigned(const FGFAimState& State);
GFGAME_API FVector GFShoulderSocketOffset(const FGFAimState& State, const FGFAimTuning& Tuning);

/** Gap after dynamic spread. The flag defaults off, so moving does not change a fresh profile. */
GFGAME_API float GFCrosshairGap(const FGFCrosshairSettings& Settings, bool bMoving, bool bFiring);

GFGAME_API bool GFWantsScopeOverlay(const FGFAimState& State);
GFGAME_API bool GFShouldDrawCrosshair(const FGFCrosshairSettings& Settings, const FGFAimState& State);

/** Apply one input with no time passing. Same-frame aim-then-fire is two calls. */
GFGAME_API FGFAimStepOut GFAimPush(FGFAimState& State, const FGFAimTuning& Tuning, EGFAimEvent Event);

/** Blend FOV, shoulder and recovery. Does not invent an input. */
GFGAME_API void GFAimAdvance(FGFAimState& State, const FGFAimTuning& Tuning, float DeltaSeconds);

enum class EGFCrosshairMarkKind : uint8
{
	Line,
	Dot
};

struct GFGAME_API FGFCrosshairMark
{
	EGFCrosshairMarkKind Kind = EGFCrosshairMarkKind::Line;
	float X = 0.f;
	float Y = 0.f;
	float X2 = 0.f;
	float Y2 = 0.f;
	float Thickness = 1.f;
	float Radius = 0.f;
	FLinearColor Color = FLinearColor::White;
	bool bOutline = false;
};

struct GFGAME_API FGFCrosshairDraw
{
	TArray<FGFCrosshairMark> Marks;
};

/** ExtraGap is added to the saved gap (dynamic spread). Outline marks are listed before the ink they sit under. */
GFGAME_API FGFCrosshairDraw GFBuildCrosshair(const FGFCrosshairSettings& Settings, float CenterX, float CenterY, float ExtraGap = 0.f);

struct GFGAME_API FGFScreenRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
	FLinearColor Color = FLinearColor::Black;
};

/** Finger-shaped edge darkening. The centre of the canvas is not covered. */
GFGAME_API TArray<FGFScreenRect> GFFistVignette(float CanvasW, float CanvasH);

/** Black ring for a sniper scope. The centre stays clear for the reticle the HUD draws on top. */
GFGAME_API TArray<FGFScreenRect> GFScopeVignette(float CanvasW, float CanvasH);

GFGAME_API bool GFScreenRectContains(const FGFScreenRect& Rect, float X, float Y);

struct GFGAME_API FGFAimSphere
{
	FVector Center = FVector::ZeroVector;
	float Radius = 1.f;
	int32 Id = 0;
};

struct GFGAME_API FGFAimShot
{
	FVector CameraOrigin = FVector::ZeroVector;
	FVector CameraDir = FVector::ForwardVector;
	FVector Muzzle = FVector::ZeroVector;
	float MaxRange = GFAim::MaxAimRange;
};

struct GFGAME_API FGFAimShotResult
{
	FVector AimPoint = FVector::ZeroVector;
	bool bCameraHit = false;
	int32 CameraHitId = INDEX_NONE;
	FVector Impact = FVector::ZeroVector;
	int32 ImpactId = INDEX_NONE;
	/** True when the muzzle segment hits something other than the camera's aim target. */
	bool bBlocked = false;
};

/**
 * Camera ray out to MaxRange finds the aim point (screen centre).
 * The shot then runs from the muzzle toward that point and stops on the first sphere in the way.
 */
GFGAME_API FGFAimShotResult GFResolveShot(const FGFAimShot& Shot, const TArray<FGFAimSphere>& Spheres);
