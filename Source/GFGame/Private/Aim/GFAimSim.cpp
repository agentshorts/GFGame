#include "Aim/GFAimSim.h"

namespace
{
	float TargetFov(const FGFAimState& State, const FGFAimTuning& Tuning)
	{
		if (State.bSnoutZoom)
		{
			return Tuning.WeaponAdsFov;
		}
		if (State.Held == EGFAimHeldKind::Fists)
		{
			return Tuning.FistAdsFov;
		}
		if (State.Weapon == EGFWeaponProfile::Sniper)
		{
			return Tuning.SniperAdsFov;
		}
		return Tuning.WeaponAdsFov;
	}

	void RefreshDerived(FGFAimState& State, const FGFAimTuning& Tuning)
	{
		State.TargetFov = TargetFov(State, Tuning);
		const float ShownAim = (State.Phase == EGFAdsPhase::Hip && !State.bAdsIntent) ? Tuning.HipFov : State.TargetFov;
		State.Fov = FMath::Lerp(Tuning.HipFov, ShownAim, State.PhaseAlpha);
		if (State.bAdsIntent)
		{
			State.LookScale = GFAimSensitivity(Tuning.HipFov, State.TargetFov, State.SensitivityScale);
		}
		else
		{
			State.LookScale = 1.f;
		}
		State.bFistScope = State.bAdsIntent && State.Held == EGFAimHeldKind::Fists && !State.bSnoutZoom;
		State.Pose = State.bFistScope ? GFAim::FistScopeAnim() : NAME_None;
	}

	void CancelRecovery(FGFAimState& State, FGFAimStepOut& Out)
	{
		if (State.bHasRecovery && State.RecoveryLeft > 0.f)
		{
			Out.bCancelled = true;
		}
		State.bHasRecovery = false;
		State.RecoveryLeft = 0.f;
	}

	void BeginEnter(FGFAimState& State)
	{
		if (State.Phase != EGFAdsPhase::Held)
		{
			State.Phase = EGFAdsPhase::Entering;
		}
	}

	void BeginLeave(FGFAimState& State)
	{
		if (State.Phase == EGFAdsPhase::Entering || State.Phase == EGFAdsPhase::Held)
		{
			State.Phase = EGFAdsPhase::Leaving;
		}
	}

	void SnapHip(FGFAimState& State)
	{
		State.bAdsIntent = false;
		State.Phase = EGFAdsPhase::Hip;
		State.PhaseAlpha = 0.f;
	}

	FGFAimStepOut Fire(FGFAimState& State, const FGFAimTuning& Tuning)
	{
		FGFAimStepOut Out;
		const bool bAds = State.bAdsIntent;
		Out.bFired = true;
		Out.bAdsRay = bAds;
		Out.bAdsAccuracy = bAds;
		Out.Spread = bAds ? Tuning.AdsSpread : Tuning.HipSpread;
		if (State.Held == EGFAimHeldKind::Fists)
		{
			Out.Damage = Tuning.HipPunchDamage;
		}
		else if (State.Held == EGFAimHeldKind::Weapon || State.Held == EGFAimHeldKind::Ability || State.bSnoutZoom)
		{
			Out.Damage = Tuning.WeaponDamage;
		}
		State.bFiring = true;
		State.SpreadFireLeft = 0.12f;
		State.bHasRecovery = true;
		State.RecoveryKind = EGFAimEvent::FirePressed;
		State.RecoveryLeft = Tuning.FireRecoverySeconds;
		if (State.bFistScope)
		{
			State.bFistCue = true;
			Out.bFistCue = true;
		}
		RefreshDerived(State, Tuning);
		return Out;
	}

	void AddLine(FGFCrosshairDraw& Draw, const FGFCrosshairSettings& Settings, float X, float Y, float X2, float Y2, float ExtraThickness)
	{
		const FLinearColor Ink(Settings.Color.R, Settings.Color.G, Settings.Color.B, Settings.Opacity);
		if (Settings.bOutline)
		{
			FGFCrosshairMark Outline;
			Outline.Kind = EGFCrosshairMarkKind::Line;
			Outline.X = X;
			Outline.Y = Y;
			Outline.X2 = X2;
			Outline.Y2 = Y2;
			Outline.Thickness = Settings.Thickness + ExtraThickness + Settings.OutlinePx * 2.f;
			Outline.Color = FLinearColor(0.f, 0.f, 0.f, Settings.Opacity);
			Outline.bOutline = true;
			Draw.Marks.Add(Outline);
		}
		FGFCrosshairMark Mark;
		Mark.Kind = EGFCrosshairMarkKind::Line;
		Mark.X = X;
		Mark.Y = Y;
		Mark.X2 = X2;
		Mark.Y2 = Y2;
		Mark.Thickness = Settings.Thickness + ExtraThickness;
		Mark.Color = Ink;
		Draw.Marks.Add(Mark);
	}

	void AddDot(FGFCrosshairDraw& Draw, const FGFCrosshairSettings& Settings, float X, float Y, float Radius)
	{
		const FLinearColor Ink(Settings.Color.R, Settings.Color.G, Settings.Color.B, Settings.Opacity);
		if (Settings.bOutline)
		{
			FGFCrosshairMark Outline;
			Outline.Kind = EGFCrosshairMarkKind::Dot;
			Outline.X = X;
			Outline.Y = Y;
			Outline.Radius = Radius + Settings.OutlinePx;
			Outline.Thickness = Outline.Radius * 2.f;
			Outline.Color = FLinearColor(0.f, 0.f, 0.f, Settings.Opacity);
			Outline.bOutline = true;
			Draw.Marks.Add(Outline);
		}
		FGFCrosshairMark Mark;
		Mark.Kind = EGFCrosshairMarkKind::Dot;
		Mark.X = X;
		Mark.Y = Y;
		Mark.Radius = Radius;
		Mark.Thickness = Radius * 2.f;
		Mark.Color = Ink;
		Draw.Marks.Add(Mark);
	}

	bool RaySphere(const FVector& Origin, const FVector& Dir, float MaxDist, const FVector& Center, float Radius, float& OutT)
	{
		const FVector Offset = Origin - Center;
		const double B = FVector::DotProduct(Offset, Dir);
		const double C = Offset.SizeSquared() - static_cast<double>(Radius) * static_cast<double>(Radius);
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return false;
		}
		const double Root = FMath::Sqrt(Disc);
		double T = -B - Root;
		if (T < 0.0)
		{
			T = -B + Root;
		}
		if (T < 0.0 || T > static_cast<double>(MaxDist))
		{
			return false;
		}
		OutT = static_cast<float>(T);
		return true;
	}

	int32 ClosestSphere(const FVector& Origin, const FVector& Dir, float MaxDist, const TArray<FGFAimSphere>& Spheres, float& OutT)
	{
		int32 Best = INDEX_NONE;
		float BestT = MaxDist;
		for (int32 Index = 0; Index < Spheres.Num(); ++Index)
		{
			float T = 0.f;
			if (RaySphere(Origin, Dir, MaxDist, Spheres[Index].Center, Spheres[Index].Radius, T) && T < BestT)
			{
				BestT = T;
				Best = Index;
			}
		}
		OutT = BestT;
		return Best;
	}
}

EGFTriggerUse GFResolvePrimaryUse(EGFAimHeldKind Held, bool bSnoutZoom)
{
	if (bSnoutZoom)
	{
		return EGFTriggerUse::Aim;
	}
	if (Held == EGFAimHeldKind::Block)
	{
		return EGFTriggerUse::PlaceBlock;
	}
	return EGFTriggerUse::Aim;
}

float GFAimSensitivity(float HipFovDeg, float AdsFovDeg, float UserScale)
{
	const float Scale = FMath::Clamp(UserScale, 0.5f, 1.5f);
	const float Hip = FMath::Tan(FMath::DegreesToRadians(FMath::Max(HipFovDeg, 1.f) * 0.5f));
	const float Ads = FMath::Tan(FMath::DegreesToRadians(FMath::Max(AdsFovDeg, 1.f) * 0.5f));
	if (Hip <= KINDA_SMALL_NUMBER)
	{
		return Scale;
	}
	return Scale * Ads / Hip;
}

float GFCameraFadeAlpha(float CameraDistanceCm)
{
	if (CameraDistanceCm >= GFAim::CameraFadeDistance)
	{
		return 1.f;
	}
	if (CameraDistanceCm <= GFAim::CameraFadeHidden)
	{
		return 0.f;
	}
	return (CameraDistanceCm - GFAim::CameraFadeHidden) / (GFAim::CameraFadeDistance - GFAim::CameraFadeHidden);
}

float GFAimShoulderSigned(const FGFAimState& State)
{
	return FMath::Lerp(State.ShoulderFrom, State.ShoulderTo, FMath::Clamp(State.ShoulderAlpha, 0.f, 1.f));
}

FVector GFShoulderSocketOffset(const FGFAimState& State, const FGFAimTuning& Tuning)
{
	const float Signed = GFAimShoulderSigned(State);
	return FVector(Tuning.ShoulderOffset.X, Tuning.ShoulderOffset.Y * Signed, Tuning.ShoulderOffset.Z);
}

float GFCrosshairGap(const FGFCrosshairSettings& Settings, bool bMoving, bool bFiring)
{
	float Gap = Settings.Gap;
	if (Settings.bDynamicSpread && (bMoving || bFiring))
	{
		Gap += GFAim::SpreadGapBonus;
	}
	return FMath::Clamp(Gap, 0.f, 40.f);
}

bool GFWantsScopeOverlay(const FGFAimState& State)
{
	return State.bAdsIntent
		&& State.bScopeOverlayEnabled
		&& State.Weapon == EGFWeaponProfile::Sniper
		&& State.Held != EGFAimHeldKind::Fists
		&& !State.bSnoutZoom;
}

bool GFShouldDrawCrosshair(const FGFCrosshairSettings& Settings, const FGFAimState& State)
{
	if (GFWantsScopeOverlay(State))
	{
		return false;
	}
	if (State.bAdsIntent && !Settings.bSeparateAdsCrosshair)
	{
		return false;
	}
	return true;
}

FGFAimStepOut GFAimPush(FGFAimState& State, const FGFAimTuning& Tuning, EGFAimEvent Event)
{
	FGFAimStepOut Out;
	switch (Event)
	{
	case EGFAimEvent::AimPressed:
		CancelRecovery(State, Out);
		if (State.bHoldToAim)
		{
			State.bAdsIntent = true;
			BeginEnter(State);
		}
		else if (State.bAdsIntent)
		{
			State.bAdsIntent = false;
			BeginLeave(State);
		}
		else
		{
			State.bAdsIntent = true;
			BeginEnter(State);
		}
		if (State.bAdsIntent && State.Held == EGFAimHeldKind::Fists && !State.bSnoutZoom)
		{
			State.bFistCue = true;
			Out.bFistCue = true;
		}
		break;
	case EGFAimEvent::AimReleased:
		if (State.bHoldToAim && State.bAdsIntent)
		{
			State.bAdsIntent = false;
			BeginLeave(State);
		}
		break;
	case EGFAimEvent::FirePressed:
		Out = Fire(State, Tuning);
		return Out;
	case EGFAimEvent::SwapWeapon:
	{
		const bool bInterruptedAim = State.bAdsIntent || State.Phase != EGFAdsPhase::Hip;
		CancelRecovery(State, Out);
		if (bInterruptedAim)
		{
			Out.bCancelled = true;
		}
		SnapHip(State);
		State.WeaponId += 1;
		State.bHasRecovery = true;
		State.RecoveryKind = EGFAimEvent::SwapWeapon;
		State.RecoveryLeft = Tuning.WeaponSwitchSeconds;
		break;
	}
	case EGFAimEvent::UseAbility:
	{
		const bool bInterruptedAim = State.bAdsIntent || State.Phase != EGFAdsPhase::Hip;
		CancelRecovery(State, Out);
		if (bInterruptedAim)
		{
			Out.bCancelled = true;
		}
		SnapHip(State);
		State.bAbility = true;
		Out.bAbility = true;
		break;
	}
	case EGFAimEvent::ShoulderSwap:
	{
		const float Now = GFAimShoulderSigned(State);
		const float Sign = FMath::IsNearlyZero(State.ShoulderTo) ? 1.f : FMath::Sign(State.ShoulderTo);
		State.ShoulderFrom = Now;
		State.ShoulderTo = -Sign;
		State.ShoulderAlpha = 0.f;
		break;
	}
	default:
		break;
	}
	RefreshDerived(State, Tuning);
	return Out;
}

void GFAimAdvance(FGFAimState& State, const FGFAimTuning& Tuning, float DeltaSeconds)
{
	const float Dt = FMath::Max(0.f, DeltaSeconds);
	if (State.Phase == EGFAdsPhase::Entering)
	{
		const float Duration = FMath::Max(Tuning.AdsInSeconds, KINDA_SMALL_NUMBER);
		State.PhaseAlpha = FMath::Min(1.f, State.PhaseAlpha + Dt / Duration);
		if (State.PhaseAlpha >= 1.f - KINDA_SMALL_NUMBER)
		{
			State.PhaseAlpha = 1.f;
			State.Phase = EGFAdsPhase::Held;
		}
	}
	else if (State.Phase == EGFAdsPhase::Leaving)
	{
		const float Duration = FMath::Max(Tuning.AdsOutSeconds, KINDA_SMALL_NUMBER);
		State.PhaseAlpha = FMath::Max(0.f, State.PhaseAlpha - Dt / Duration);
		if (State.PhaseAlpha <= KINDA_SMALL_NUMBER)
		{
			State.PhaseAlpha = 0.f;
			State.Phase = EGFAdsPhase::Hip;
		}
	}
	if (State.bHasRecovery)
	{
		State.RecoveryLeft = FMath::Max(0.f, State.RecoveryLeft - Dt);
		if (State.RecoveryLeft <= 0.f)
		{
			State.bHasRecovery = false;
		}
	}
	if (State.ShoulderAlpha < 1.f)
	{
		const float Duration = FMath::Max(Tuning.ShoulderBlendSeconds, KINDA_SMALL_NUMBER);
		State.ShoulderAlpha = FMath::Min(1.f, State.ShoulderAlpha + Dt / Duration);
	}
	if (Dt > 0.f)
	{
		State.bFiring = false;
	}
	State.SpreadFireLeft = FMath::Max(0.f, State.SpreadFireLeft - Dt);
	RefreshDerived(State, Tuning);
}

FGFCrosshairDraw GFBuildCrosshair(const FGFCrosshairSettings& Settings, float CenterX, float CenterY, float ExtraGap)
{
	FGFCrosshairSettings S = Settings;
	S.Clamp();
	const float Gap = FMath::Clamp(S.Gap + ExtraGap, 0.f, 40.f);
	const float Arm = S.Size;
	FGFCrosshairDraw Draw;
	const bool bPlus = S.Shape == EGFCrosshairShape::Plus || S.Shape == EGFCrosshairShape::PlusDot;
	const bool bT = S.Shape == EGFCrosshairShape::T;
	const bool bChevron = S.Shape == EGFCrosshairShape::Chevron;
	const bool bCircle = S.Shape == EGFCrosshairShape::Circle;
	const bool bDotShape = S.Shape == EGFCrosshairShape::Dot;
	if (bPlus || bT)
	{
		AddLine(Draw, S, CenterX - Arm, CenterY, CenterX - Gap, CenterY, 0.f);
		AddLine(Draw, S, CenterX + Gap, CenterY, CenterX + Arm, CenterY, 0.f);
	}
	if (bPlus)
	{
		AddLine(Draw, S, CenterX, CenterY - Arm, CenterX, CenterY - Gap, 0.f);
	}
	if (bPlus || bT)
	{
		AddLine(Draw, S, CenterX, CenterY + Gap, CenterX, CenterY + Arm, 0.f);
	}
	if (bChevron)
	{
		const float Drop = Arm * 0.45f;
		AddLine(Draw, S, CenterX - Gap - Arm, CenterY + Drop, CenterX - Gap, CenterY, 0.f);
		AddLine(Draw, S, CenterX + Gap, CenterY, CenterX + Gap + Arm, CenterY + Drop, 0.f);
	}
	if (bCircle)
	{
		constexpr int32 Steps = 24;
		const float Radius = Arm;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float A0 = 2.f * PI * static_cast<float>(Step) / static_cast<float>(Steps);
			const float A1 = 2.f * PI * static_cast<float>(Step + 1) / static_cast<float>(Steps);
			AddLine(Draw, S,
				CenterX + FMath::Cos(A0) * Radius, CenterY + FMath::Sin(A0) * Radius,
				CenterX + FMath::Cos(A1) * Radius, CenterY + FMath::Sin(A1) * Radius, 0.f);
		}
	}
	const bool bDot = bDotShape || S.Shape == EGFCrosshairShape::PlusDot || S.bCenterDot;
	if (bDot)
	{
		const float Radius = bDotShape ? FMath::Max(2.f, Arm * 0.35f) : FMath::Max(2.f, S.Thickness);
		AddDot(Draw, S, CenterX, CenterY, Radius);
	}
	return Draw;
}

TArray<FGFScreenRect> GFFistVignette(float CanvasW, float CanvasH)
{
	TArray<FGFScreenRect> Rects;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Rects;
	}
	const FLinearColor Ink(0.05f, 0.03f, 0.02f, 0.72f);
	auto Add = [&Rects, Ink](float X, float Y, float W, float H)
	{
		FGFScreenRect Rect;
		Rect.X = X;
		Rect.Y = Y;
		Rect.W = W;
		Rect.H = H;
		Rect.Color = Ink;
		Rects.Add(Rect);
	};
	const float Top = CanvasH * 0.08f;
	const float Bottom = CanvasH * 0.12f;
	const float Side = CanvasW * 0.07f;
	Add(0.f, 0.f, CanvasW, Top);
	Add(0.f, CanvasH - Bottom, CanvasW, Bottom);
	Add(0.f, 0.f, Side, CanvasH);
	Add(CanvasW - Side, 0.f, Side, CanvasH);
	// Two curled fingers at the top corners, leaving the middle of the picture alone.
	const float FingerW = CanvasW * 0.18f;
	const float FingerH = CanvasH * 0.22f;
	Add(0.f, 0.f, FingerW, FingerH);
	Add(CanvasW - FingerW, 0.f, FingerW, FingerH);
	return Rects;
}

TArray<FGFScreenRect> GFScopeVignette(float CanvasW, float CanvasH)
{
	TArray<FGFScreenRect> Rects;
	if (CanvasW <= 1.f || CanvasH <= 1.f)
	{
		return Rects;
	}
	const FLinearColor Ink(0.f, 0.f, 0.f, 0.92f);
	const float BandX = CanvasW * 0.18f;
	const float BandY = CanvasH * 0.16f;
	auto Add = [&Rects, Ink](float X, float Y, float W, float H)
	{
		FGFScreenRect Rect;
		Rect.X = X;
		Rect.Y = Y;
		Rect.W = W;
		Rect.H = H;
		Rect.Color = Ink;
		Rects.Add(Rect);
	};
	Add(0.f, 0.f, CanvasW, BandY);
	Add(0.f, CanvasH - BandY, CanvasW, BandY);
	Add(0.f, 0.f, BandX, CanvasH);
	Add(CanvasW - BandX, 0.f, BandX, CanvasH);
	return Rects;
}

bool GFScreenRectContains(const FGFScreenRect& Rect, float X, float Y)
{
	return X >= Rect.X && Y >= Rect.Y && X <= Rect.X + Rect.W && Y <= Rect.Y + Rect.H;
}

FGFAimShotResult GFResolveShot(const FGFAimShot& Shot, const TArray<FGFAimSphere>& Spheres)
{
	FGFAimShotResult Result;
	const FVector CameraDir = Shot.CameraDir.GetSafeNormal();
	const float MaxRange = FMath::Max(0.f, Shot.MaxRange);
	float CameraT = MaxRange;
	const int32 CameraIndex = ClosestSphere(Shot.CameraOrigin, CameraDir, MaxRange, Spheres, CameraT);
	Result.bCameraHit = CameraIndex != INDEX_NONE;
	Result.CameraHitId = Result.bCameraHit ? Spheres[CameraIndex].Id : INDEX_NONE;
	Result.AimPoint = Shot.CameraOrigin + CameraDir * CameraT;

	const FVector ToAim = Result.AimPoint - Shot.Muzzle;
	const float ShotLen = ToAim.Size();
	if (ShotLen <= KINDA_SMALL_NUMBER)
	{
		Result.Impact = Result.AimPoint;
		Result.ImpactId = Result.CameraHitId;
		return Result;
	}
	const FVector ShotDir = ToAim / ShotLen;
	float ShotT = ShotLen;
	const int32 ShotIndex = ClosestSphere(Shot.Muzzle, ShotDir, ShotLen, Spheres, ShotT);
	if (ShotIndex == INDEX_NONE)
	{
		Result.Impact = Result.AimPoint;
		Result.ImpactId = Result.CameraHitId;
		return Result;
	}
	Result.Impact = Shot.Muzzle + ShotDir * ShotT;
	Result.ImpactId = Spheres[ShotIndex].Id;
	Result.bBlocked = Result.ImpactId != Result.CameraHitId;
	return Result;
}
