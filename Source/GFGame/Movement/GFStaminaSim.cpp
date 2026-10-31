#include "GFStaminaSim.h"

namespace GFStaminaSimDetail
{
	float GFStaminaNonNegative(float Value)
	{
		return Value > 0.f ? Value : 0.f;
	}

	FVector GFStaminaFlatUnit(const FVector& Direction)
	{
		return FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal();
	}
}

void FGFStaminaState::ResetFull(const UGFMovementTuning& Tuning)
{
	Stamina = Tuning.MaxStamina;
	RegenDelayLeft = 0.f;
	DashTimeLeft = 0.f;
	GroundCooldownLeft = 0.f;
	AirDashesUsed = 0;
	bSprintBlocked = false;
	bSprinting = false;
	bDashing = false;
	bWasGrounded = true;
	DashDir = FVector::ForwardVector;
	bGripping = false;
	GripTimeLeft = 0.f;
	GripNormal = FVector::ForwardVector;
	bWallRunning = false;
	bWallRunBlocked = false;
	WallRunTimeLeft = Tuning.WallRunMaxSeconds;
	WallRunSpeed = 0.f;
	WallRunDir = FVector::ForwardVector;
	WallNormal = FVector::ForwardVector;
	bSwiveling = false;
	SwivelAngle = 0.f;
	SwivelRadius = 0.f;
	SwivelSpeed = 0.f;
	SwivelSign = 1.f;
	SwivelOrigin = FVector::ZeroVector;
	bSwinging = false;
	bYanking = false;
	GrappleCooldownLeft = 0.f;
	RopeLength = 0.f;
	GrappleAnchor = FVector::ZeroVector;
	TravelLocation = FVector::ZeroVector;
	SwingVelocity = FVector::ZeroVector;
	bOverrideVelocity = false;
	OverrideVelocity = FVector::ZeroVector;
}

FVector GFStaminaSim::ResolveDashDirection(const FVector& MoveDirection, const FVector& CameraForward)
{
	const FVector MoveFlat = GFStaminaSimDetail::GFStaminaFlatUnit(MoveDirection);
	if (!MoveFlat.IsNearlyZero())
	{
		return MoveFlat;
	}
	const FVector CameraFlat = GFStaminaSimDetail::GFStaminaFlatUnit(CameraForward);
	if (!CameraFlat.IsNearlyZero())
	{
		return CameraFlat;
	}
	return FVector::ForwardVector;
}

float GFStaminaSim::HorizontalSpeed(const FGFStaminaState& State, const UGFMovementTuning& Tuning, float LiveWalkSpeed)
{
	if (State.bDashing)
	{
		return Tuning.DashSpeed;
	}
	if (State.bOverrideVelocity)
	{
		const float OverrideSpeed = static_cast<float>(State.OverrideVelocity.Size());
		return FMath::Max(LiveWalkSpeed, OverrideSpeed);
	}
	if (State.bSprinting)
	{
		return LiveWalkSpeed * Tuning.SprintSpeedMultiplier;
	}
	return LiveWalkSpeed;
}

float GFStaminaSim::GravityScale(const FGFStaminaState& State, const UGFMovementTuning& Tuning)
{
	if (State.bDashing)
	{
		return Tuning.DashGravityScale;
	}
	if (State.bGripping || State.bWallRunning || State.bSwiveling || State.bSwinging || State.bYanking)
	{
		return 0.f;
	}
	return 1.f;
}

bool GFStaminaSim::ShouldShowArc(float Stamina, float MaxStamina, bool bEnabled, float HideEpsilon)
{
	if (!bEnabled || MaxStamina <= 0.f)
	{
		return false;
	}
	return Stamina < MaxStamina - FMath::Max(0.f, HideEpsilon);
}

FGFStaminaState GFStaminaSim::Step(const FGFStaminaState& State, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning)
{
	const float Dt = GFStaminaSimDetail::GFStaminaNonNegative(Command.DeltaSeconds);
	if (!Command.bStaminaEnabled)
	{
		FGFStaminaState Idle = State;
		Idle.Stamina = Tuning.MaxStamina;
		Idle.RegenDelayLeft = 0.f;
		Idle.bSprinting = false;
		Idle.bDashing = false;
		Idle.DashTimeLeft = 0.f;
		Idle.bSprintBlocked = false;
		Idle.bWasGrounded = Command.bGrounded;
		GFTraversalSim::ClearActivity(Idle, Tuning);
		return Idle;
	}

	FGFStaminaState Next = State;
	if (Next.DashTimeLeft > 0.f)
	{
		Next.DashTimeLeft = GFStaminaSimDetail::GFStaminaNonNegative(Next.DashTimeLeft - Dt);
	}
	Next.bDashing = Next.DashTimeLeft > 0.f;
	if (Next.GroundCooldownLeft > 0.f)
	{
		Next.GroundCooldownLeft = GFStaminaSimDetail::GFStaminaNonNegative(Next.GroundCooldownLeft - Dt);
	}

	const bool bLanded = Command.bGrounded && !State.bWasGrounded;
	if (bLanded)
	{
		Next.AirDashesUsed = 0;
	}
	if (!Command.bSprintHeld)
	{
		Next.bSprintBlocked = false;
	}

	const bool bInfinite = Command.bInsideKindleRing;
	bool bSpent = GFTraversalSim::Apply(Next, Command, Tuning);

	const bool bTraversalBusy = Next.bGripping || Next.bWallRunning || Next.bSwiveling || Next.bSwinging || Next.bYanking;
	if (Command.bDashPressed && !Next.bDashing && !bTraversalBusy && Tuning.DashDuration > 0.f)
	{
		const bool bAir = !Command.bGrounded;
		const bool bAirOk = bAir && Next.AirDashesUsed < Tuning.AirDashesPerAirtime;
		const bool bGroundOk = !bAir && Next.GroundCooldownLeft <= 0.f;
		const bool bCanPay = bInfinite || Next.Stamina + KINDA_SMALL_NUMBER >= Tuning.DashCost;
		if ((bAirOk || bGroundOk) && bCanPay)
		{
			if (!bInfinite)
			{
				Next.Stamina = GFStaminaSimDetail::GFStaminaNonNegative(Next.Stamina - Tuning.DashCost);
				Next.RegenDelayLeft = Tuning.RegenDelay;
				bSpent = true;
			}
			Next.DashDir = ResolveDashDirection(Command.MoveDirection, Command.CameraForward);
			Next.DashTimeLeft = GFStaminaSimDetail::GFStaminaNonNegative(Tuning.DashDuration - Dt);
			Next.bDashing = true;
			if (bAir)
			{
				++Next.AirDashesUsed;
			}
			else
			{
				Next.GroundCooldownLeft = GFStaminaSimDetail::GFStaminaNonNegative(Tuning.GroundDashCooldown - Dt);
			}
		}
	}

	Next.bSprinting = false;
	if (Command.bSprintHeld && !Next.bSprintBlocked && !Next.bDashing && !bTraversalBusy && !Next.bOverrideVelocity)
	{
		if (bInfinite)
		{
			Next.bSprinting = true;
		}
		else if (Next.Stamina > 0.f)
		{
			const float Cost = Tuning.SprintCostPerSecond * Dt;
			Next.Stamina = GFStaminaSimDetail::GFStaminaNonNegative(Next.Stamina - Cost);
			Next.RegenDelayLeft = Tuning.RegenDelay;
			bSpent = true;
			Next.bSprinting = true;
			if (Next.Stamina <= 0.f)
			{
				Next.bSprintBlocked = true;
			}
		}
		else
		{
			Next.bSprintBlocked = true;
		}
	}

	if (bInfinite)
	{
		Next.Stamina = Tuning.MaxStamina;
		Next.RegenDelayLeft = 0.f;
	}
	else if (!bSpent)
	{
		float Wait = Next.RegenDelayLeft;
		float RegenTime = Dt;
		if (Wait > 0.f)
		{
			const float Used = FMath::Min(Wait, RegenTime);
			Wait -= Used;
			RegenTime -= Used;
			Next.RegenDelayLeft = GFStaminaSimDetail::GFStaminaNonNegative(Wait);
		}
		if (RegenTime > 0.f)
		{
			Next.Stamina = FMath::Min(Tuning.MaxStamina, Next.Stamina + Tuning.RegenPerSecond * RegenTime);
		}
	}

	Next.bWasGrounded = Command.bGrounded;
	return Next;
}
