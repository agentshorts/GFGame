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
	bWallClimbing = false;
	bWallClimbBlocked = false;
	WallClimbTimeLeft = 0.f;
	WallClimbCoyoteLeft = 0.f;
	WallClimbNormal = FVector::ForwardVector;
	bMantling = false;
	MantleTimeLeft = 0.f;
	MantleTarget = FVector::ZeroVector;
	MantleLipHeight = 0.f;
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
	CurrentMove = EGFTraversalMove::None;
	JumpsUsed = 0;
	bTraversalJump = false;
	bJumpAccepted = false;
	JumpZSpeed = 0.f;
	JumpForward = FVector::ZeroVector;
	FlipTimeLeft = 0.f;
}

namespace GFStaminaSimDetail
{
	FVector GFStaminaJumpFacing(const FGFStaminaCommand& Command)
	{
		FVector Facing = GFStaminaFlatUnit(Command.MoveDirection);
		if (Facing.IsNearlyZero())
		{
			Facing = GFStaminaFlatUnit(Command.Velocity);
		}
		if (Facing.IsNearlyZero())
		{
			Facing = GFStaminaFlatUnit(Command.CameraForward);
		}
		return Facing.IsNearlyZero() ? FVector::ForwardVector : Facing;
	}

	bool GFStaminaResolveJump(FGFStaminaState& Next, const FGFStaminaState& Prev, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning)
	{
		const bool bWallKick = Command.bJumpPressed && Prev.bGripping && !Next.bGripping;
		const bool bSwivelLaunch = Command.bJumpPressed && Prev.bSwiveling && !Next.bSwiveling;
		if (!Command.bJumpPressed || bWallKick || bSwivelLaunch || Next.bTraversalJump)
		{
			return false;
		}
		if (Next.bGripping || Next.bSwiveling || Next.bSwinging || Next.bYanking || Next.bWallClimbing || Next.bMantling)
		{
			return false;
		}
		const int32 JumpCap = Command.JumpsPerAirtimeOverride >= 0 ? Command.JumpsPerAirtimeOverride : Tuning.JumpsPerAirtime;
		if (JumpCap <= 0)
		{
			return false;
		}

		const int32 Index = Command.bGrounded ? 1 : Next.JumpsUsed + 1;
		if (Index > JumpCap)
		{
			return false;
		}

		const bool bExtra = Index >= 2;
		const bool bInfinite = Command.bInsideKindleRing || Command.bCostsFree;
		if (bExtra && !bInfinite && Next.Stamina + KINDA_SMALL_NUMBER < Tuning.ExtraJumpCost)
		{
			return false;
		}

		float HeightScale = 1.f;
		float Boost = 0.f;
		if (Index == 2)
		{
			HeightScale = Tuning.Jump2HeightScale;
		}
		else if (Index >= 3)
		{
			HeightScale = Tuning.Jump3HeightScale;
			Boost = FMath::Max(0.f, Tuning.Jump3ForwardBoost);
		}
		const float SpeedScale = FMath::Sqrt(FMath::Max(0.f, HeightScale));
		Next.bJumpAccepted = true;
		Next.JumpsUsed = Index;
		Next.JumpZSpeed = FMath::Max(0.f, Tuning.JumpZVelocity) * SpeedScale;
		Next.JumpForward = GFStaminaJumpFacing(Command) * static_cast<double>(Boost);
		if (Next.bWallRunning)
		{
			Next.bWallRunBlocked = true;
		}
		Next.bWallRunning = false;
		Next.bOverrideVelocity = false;
		Next.OverrideVelocity = FVector::ZeroVector;
		if (Index >= 3 && Tuning.JumpFlipDuration > 0.f)
		{
			Next.FlipTimeLeft = Tuning.JumpFlipDuration;
		}
		if (bExtra && !bInfinite)
		{
			Next.Stamina = GFStaminaNonNegative(Next.Stamina - Tuning.ExtraJumpCost);
			Next.RegenDelayLeft = Tuning.RegenDelay;
			return true;
		}
		return false;
	}
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
	if (State.bGripping || State.bWallRunning || State.bSwiveling || State.bSwinging || State.bYanking || State.bWallClimbing || State.bMantling)
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
		Idle.JumpsUsed = 0;
		Idle.bTraversalJump = false;
		Idle.bJumpAccepted = false;
		Idle.JumpZSpeed = 0.f;
		Idle.JumpForward = FVector::ZeroVector;
		Idle.FlipTimeLeft = 0.f;
		GFTraversalSim::ClearActivity(Idle, Tuning);
		return Idle;
	}

	FGFStaminaState Next = State;
	Next.bJumpAccepted = false;
	Next.bTraversalJump = false;
	Next.JumpZSpeed = 0.f;
	Next.JumpForward = FVector::ZeroVector;
	if (Next.FlipTimeLeft > 0.f)
	{
		Next.FlipTimeLeft = GFStaminaSimDetail::GFStaminaNonNegative(Next.FlipTimeLeft - Dt);
	}
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
		Next.JumpsUsed = 0;
		Next.FlipTimeLeft = 0.f;
	}
	if (!Command.bSprintHeld)
	{
		Next.bSprintBlocked = false;
	}

	const bool bInfinite = Command.bInsideKindleRing || Command.bCostsFree;
	bool bSpent = GFTraversalSim::Apply(Next, Command, Tuning);
	if (GFStaminaSimDetail::GFStaminaResolveJump(Next, State, Command, Tuning))
	{
		bSpent = true;
	}

	const bool bTraversalBusy = Next.bGripping || Next.bWallRunning || Next.bSwiveling || Next.bSwinging || Next.bYanking
		|| Next.bWallClimbing || Next.bMantling;
	// A kick, launch, or release writes a real exit velocity this tick. That velocity owns the tick.
	// A zero override is only a stop (yank or mantle arrived) and does not.
	const bool bExitOwnsTick = Next.bOverrideVelocity && !Next.OverrideVelocity.IsNearlyZero();
	const bool bAirDash = !Command.bGrounded;
	const float DashOverride = bAirDash ? Command.AirDashDurationOverride : Command.GroundDashDurationOverride;
	const float DashDuration = DashOverride >= 0.f ? DashOverride : Tuning.DashDuration;
	if (Command.bDashPressed && !Next.bDashing && !bTraversalBusy && !bExitOwnsTick && DashDuration > 0.f)
	{
		const bool bAir = bAirDash;
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
			Next.DashTimeLeft = GFStaminaSimDetail::GFStaminaNonNegative(DashDuration - Dt);
			Next.bDashing = true;
			Next.bOverrideVelocity = false;
			Next.OverrideVelocity = FVector::ZeroVector;
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
	GFTraversalSim::PublishCurrentMove(Next);
	return Next;
}
