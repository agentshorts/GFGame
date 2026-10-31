#include "GFStaminaSim.h"

namespace GFTraversalDetail
{
	float GFTraversalNonNegative(float Value)
	{
		return Value > 0.f ? Value : 0.f;
	}

	float GFTraversalConsume(float& TimeLeft, float Dt)
	{
		const float Available = GFTraversalNonNegative(TimeLeft);
		const float Consumed = FMath::Min(Available, GFTraversalNonNegative(Dt));
		TimeLeft = GFTraversalNonNegative(Available - Consumed);
		return Consumed;
	}

	FVector GFTraversalFlatNormal(const FVector& Normal)
	{
		const FVector Flat = FVector(Normal.X, Normal.Y, 0.0).GetSafeNormal();
		return Flat.IsNearlyZero() ? FVector(1.0, 0.0, 0.0) : Flat;
	}

	bool GFTraversalIsWallNormal(const FVector& Normal, float MaxZ)
	{
		return FMath::Abs(Normal.Z) <= static_cast<double>(FMath::Max(0.f, MaxZ));
	}

	bool GFTraversalLostSpeed(float Before, float After, float LossFraction)
	{
		if (Before <= KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const float Keep = 1.f - FMath::Clamp(LossFraction, 0.f, 1.f);
		return After < Before * Keep;
	}

	bool GFTraversalAlongWall(const FVector& MoveDirection, const FVector& WallNormal, float MinDot, FVector& OutDir)
	{
		const FVector Normal = GFTraversalFlatNormal(WallNormal);
		FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
		const FVector Move = FVector(MoveDirection.X, MoveDirection.Y, 0.0).GetSafeNormal();
		if (Move.IsNearlyZero() || Tangent.IsNearlyZero())
		{
			return false;
		}
		const double Along = FVector::DotProduct(Move, Tangent);
		const double Into = FVector::DotProduct(Move, Normal * -1.0);
		if (FMath::Abs(Along) < static_cast<double>(FMath::Max(0.f, MinDot)))
		{
			return false;
		}
		if (FMath::Abs(Along) <= Into)
		{
			return false;
		}
		OutDir = Tangent * (Along >= 0.0 ? 1.0 : -1.0);
		return true;
	}

	FVector GFTraversalTangent(float Angle, float Sign)
	{
		const double Radians = static_cast<double>(Angle);
		const FVector Radial(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
		FVector Tangent(-Radial.Y, Radial.X, 0.0);
		if (Sign < 0.f)
		{
			Tangent *= -1.0;
		}
		return Tangent;
	}
}

FName GFTraversalSim::SwivelTag()
{
	return FName(TEXT("Swivel"));
}

bool GFTraversalSim::IsSwivelTarget(bool bTagged, const FVector& BoundsSize, const UGFMovementTuning& Tuning)
{
	if (bTagged)
	{
		return true;
	}
	const double Horizontal = FMath::Max(BoundsSize.X, BoundsSize.Y);
	const double Thickness = static_cast<double>(FMath::Max(0.f, Tuning.SwivelMaxThickness));
	const double MinHeight = static_cast<double>(FMath::Max(0.f, Tuning.SwivelMinHeight));
	const double Aspect = static_cast<double>(FMath::Max(0.f, Tuning.SwivelMinAspect));
	if (Horizontal > Thickness || BoundsSize.Z < MinHeight)
	{
		return false;
	}
	if (Horizontal <= 1.0)
	{
		return true;
	}
	return BoundsSize.Z >= Horizontal * Aspect;
}

void GFTraversalSim::ClearActivity(FGFStaminaState& State, const UGFMovementTuning& Tuning)
{
	State.bGripping = false;
	State.GripTimeLeft = 0.f;
	State.bWallRunning = false;
	State.bWallRunBlocked = false;
	State.WallRunTimeLeft = Tuning.WallRunMaxSeconds;
	State.bSwiveling = false;
	State.bSwinging = false;
	State.bYanking = false;
	State.bOverrideVelocity = false;
	State.OverrideVelocity = FVector::ZeroVector;
}

bool GFTraversalSim::ShouldShowLandingMarker(const FGFStaminaState& State, bool bGrounded, bool bEnabled)
{
	if (!bEnabled || State.bGripping || State.bWallRunning || State.bSwiveling)
	{
		return false;
	}
	if (State.bSwinging)
	{
		return true;
	}
	return !bGrounded;
}

FVector GFTraversalSim::BallisticPoint(const FVector& Origin, const FVector& Velocity, float GravityZ, float Time)
{
	const double T = static_cast<double>(Time);
	const double Gravity = static_cast<double>(GravityZ);
	return Origin + Velocity * T + FVector(0.0, 0.0, 0.5 * Gravity * T * T);
}

FGFLandingPrediction GFTraversalSim::PredictLandingOnPlane(const FVector& Origin, const FVector& Velocity, float GravityZ, float FloorZ, float MaxTime)
{
	FGFLandingPrediction Result;
	const double A = 0.5 * static_cast<double>(GravityZ);
	const double B = Velocity.Z;
	const double C = Origin.Z - static_cast<double>(FloorZ);
	const double Limit = static_cast<double>(FMath::Max(0.f, MaxTime));
	double Soonest = -1.0;

	if (FMath::Abs(A) < 1.0e-8)
	{
		if (FMath::Abs(B) > 1.0e-8)
		{
			const double Linear = -C / B;
			if (Linear > 0.0)
			{
				Soonest = Linear;
			}
		}
	}
	else
	{
		const double Disc = B * B - 4.0 * A * C;
		if (Disc >= 0.0)
		{
			const double Root = FMath::Sqrt(Disc);
			const double T1 = (-B - Root) / (2.0 * A);
			const double T2 = (-B + Root) / (2.0 * A);
			if (T1 > 0.0)
			{
				Soonest = T1;
			}
			if (T2 > 0.0 && (Soonest < 0.0 || T2 < Soonest))
			{
				Soonest = T2;
			}
		}
	}

	if (Soonest < 0.0 || Soonest > Limit)
	{
		return Result;
	}
	Result.bHit = true;
	Result.Time = static_cast<float>(Soonest);
	Result.Point = BallisticPoint(Origin, Velocity, GravityZ, Result.Time);
	return Result;
}

bool GFTraversalSim::Apply(FGFStaminaState& Next, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning)
{
	const float Dt = GFTraversalDetail::GFTraversalNonNegative(Command.DeltaSeconds);
	const bool bInfinite = Command.bInsideKindleRing;
	bool bSpent = false;
	Next.bOverrideVelocity = false;

	if (Next.GrappleCooldownLeft > 0.f)
	{
		Next.GrappleCooldownLeft = GFTraversalDetail::GFTraversalNonNegative(Next.GrappleCooldownLeft - Dt);
	}

	auto Pay = [&Next, &Tuning, &bSpent, bInfinite](float Cost)
	{
		if (bInfinite || Cost <= 0.f)
		{
			return;
		}
		Next.Stamina = GFTraversalDetail::GFTraversalNonNegative(Next.Stamina - Cost);
		Next.RegenDelayLeft = Tuning.RegenDelay;
		bSpent = true;
	};
	auto CanAfford = [&Next, bInfinite](float Cost)
	{
		return bInfinite || Next.Stamina + KINDA_SMALL_NUMBER >= Cost;
	};

	const FGFTraversalContact& Contact = Command.Contact;
	if (Next.bDashing && Contact.bSwivelContact)
	{
		Next.bDashing = false;
		Next.DashTimeLeft = 0.f;
	}
	else if (Next.bDashing && Contact.bDashImpact && !Next.bGripping)
	{
		const bool bWall = GFTraversalDetail::GFTraversalIsWallNormal(Contact.ImpactNormal, Tuning.WallNormalMaxZ);
		const bool bLoss = GFTraversalDetail::GFTraversalLostSpeed(Contact.SpeedBefore, Contact.SpeedAfter, Tuning.DashGripSpeedLoss);
		if (bWall && bLoss && Tuning.DashGripMaxSeconds > 0.f && (bInfinite || Next.Stamina > 0.f))
		{
			Next.bDashing = false;
			Next.DashTimeLeft = 0.f;
			Next.bGripping = true;
			Next.bWallRunning = false;
			Next.GripTimeLeft = Tuning.DashGripMaxSeconds;
			Next.GripNormal = GFTraversalDetail::GFTraversalFlatNormal(Contact.ImpactNormal);
		}
	}

	if (Next.bGripping && Command.bJumpPressed)
	{
		const FVector Normal = GFTraversalDetail::GFTraversalFlatNormal(Next.GripNormal);
		Next.bGripping = false;
		Next.GripTimeLeft = 0.f;
		Next.AirDashesUsed = 0;
		Next.JumpsUsed = 0;
		Next.FlipTimeLeft = 0.f;
		Next.bTraversalJump = true;
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = Normal * static_cast<double>(Tuning.WallKickSpeed) + FVector(0.0, 0.0, static_cast<double>(Tuning.WallKickUp));
	}
	else if (Next.bGripping)
	{
		const float Consumed = GFTraversalDetail::GFTraversalConsume(Next.GripTimeLeft, Dt);
		Pay(Tuning.DashGripDrainPerSecond * Consumed);
		if (Next.GripTimeLeft <= KINDA_SMALL_NUMBER || (!bInfinite && Next.Stamina <= 0.f))
		{
			Next.bGripping = false;
			Next.GripTimeLeft = 0.f;
		}
		else
		{
			Next.bOverrideVelocity = true;
			Next.OverrideVelocity = Next.GripNormal * -static_cast<double>(FMath::Max(0.f, Tuning.GripStickSpeed));
		}
	}

	const bool bLaunchSwivel = Next.bSwiveling && Command.bJumpPressed;
	if (bLaunchSwivel)
	{
		const FVector Tangent = GFTraversalDetail::GFTraversalTangent(Next.SwivelAngle, Next.SwivelSign);
		Next.bSwiveling = false;
		Next.bTraversalJump = true;
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = Tangent * static_cast<double>(Next.SwivelSpeed) * static_cast<double>(Tuning.SwivelLaunchBoost)
			+ FVector(0.0, 0.0, static_cast<double>(Tuning.SwivelLaunchUp));
	}
	else if (Contact.bSwivelContact && !Next.bGripping && !Next.bDashing && !Next.bSwinging && !Next.bYanking && !Next.bOverrideVelocity)
	{
		if (!Next.bSwiveling)
		{
			FVector Offset = Command.PlayerLocation - Contact.SwivelOrigin;
			Offset.Z = 0.0;
			double Radius = Offset.Size();
			const double MinRadius = static_cast<double>(FMath::Max(1.f, Tuning.SwivelMinRadius));
			if (Radius < MinRadius)
			{
				Offset = FVector(MinRadius, 0.0, 0.0);
				Radius = MinRadius;
			}
			Next.bSwiveling = true;
			Next.bWallRunning = false;
			Next.SwivelOrigin = Contact.SwivelOrigin;
			Next.SwivelRadius = static_cast<float>(Radius);
			Next.SwivelAngle = static_cast<float>(FMath::Atan2(Offset.Y, Offset.X));
			Next.SwivelSpeed = Command.HorizontalSpeed < Tuning.SwivelMinSpeed ? Tuning.SwivelMinSpeed : Command.HorizontalSpeed;
			const FVector Tangent = FVector(-Offset.Y, Offset.X, 0.0).GetSafeNormal();
			FVector Move = FVector(Command.MoveDirection.X, Command.MoveDirection.Y, 0.0);
			if (Move.IsNearlyZero())
			{
				Move = FVector(Command.Velocity.X, Command.Velocity.Y, 0.0);
			}
			Next.SwivelSign = (!Move.IsNearlyZero() && FVector::DotProduct(Tangent, Move) < 0.0) ? -1.f : 1.f;
		}
		if (Next.bSwiveling)
		{
			const double Radius = static_cast<double>(FMath::Max(Next.SwivelRadius, 1.f));
			const double Spin = static_cast<double>(Next.SwivelSign < 0.f ? -1.f : 1.f) * static_cast<double>(Next.SwivelSpeed) / Radius;
			Next.SwivelAngle += static_cast<float>(Spin * static_cast<double>(Dt));
			const FVector Tangent = GFTraversalDetail::GFTraversalTangent(Next.SwivelAngle, Next.SwivelSign);
			const double Radians = static_cast<double>(Next.SwivelAngle);
			const FVector Radial(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
			Next.TravelLocation = Next.SwivelOrigin + Radial * Radius;
			Next.TravelLocation.Z = Command.PlayerLocation.Z;
			Next.bOverrideVelocity = true;
			Next.OverrideVelocity = Tangent * static_cast<double>(Next.SwivelSpeed);
		}
	}
	else if (!bLaunchSwivel)
	{
		Next.bSwiveling = false;
	}

	const double GrappleDistance = FVector::Dist(Command.PlayerLocation, Contact.GrapplePoint);
	const bool bInRange = Contact.bGrappleHit && GrappleDistance <= static_cast<double>(FMath::Max(0.f, Tuning.GrappleRange));
	const bool bAttach = Next.GrappleCooldownLeft <= 0.f && bInRange && CanAfford(Tuning.GrappleCost);
	if (Command.bGrappleYankPressed && !Next.bYanking && bAttach)
	{
		Pay(Tuning.GrappleCost);
		Next.GrappleCooldownLeft = Tuning.GrappleCooldown;
		Next.bYanking = true;
		Next.bSwinging = false;
		Next.bGripping = false;
		Next.bWallRunning = false;
		Next.bSwiveling = false;
		Next.bDashing = false;
		Next.DashTimeLeft = 0.f;
		Next.GrappleAnchor = Contact.GrapplePoint;
		Next.TravelLocation = Command.PlayerLocation;
	}
	else if (Command.bGrappleSwingHeld && !Next.bSwinging && !Next.bYanking && !Next.bOverrideVelocity && bAttach)
	{
		Pay(Tuning.GrappleCost);
		Next.GrappleCooldownLeft = Tuning.GrappleCooldown;
		Next.bSwinging = true;
		Next.bGripping = false;
		Next.bWallRunning = false;
		Next.bSwiveling = false;
		Next.bDashing = false;
		Next.DashTimeLeft = 0.f;
		Next.GrappleAnchor = Contact.GrapplePoint;
		Next.RopeLength = static_cast<float>(GrappleDistance);
		Next.TravelLocation = Command.PlayerLocation;
		Next.SwingVelocity = Command.Velocity;
	}

	if (Next.bSwinging && !Command.bGrappleSwingHeld)
	{
		Next.bSwinging = false;
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = Next.SwingVelocity * static_cast<double>(Tuning.GrappleReleaseBoost);
	}
	else if (Next.bSwinging)
	{
		FVector Vel = Next.SwingVelocity;
		Vel.Z += static_cast<double>(Tuning.GrappleGravity) * static_cast<double>(Dt);
		const FVector ToAnchor = Next.GrappleAnchor - Next.TravelLocation;
		const double Span = ToAnchor.Size();
		const FVector Toward = Span > 1.0 ? ToAnchor / Span : FVector(1.0, 0.0, 0.0);
		Vel = Vel - Toward * FVector::DotProduct(Vel, Toward);
		Next.TravelLocation += Vel * static_cast<double>(Dt);
		FVector FromAnchor = Next.TravelLocation - Next.GrappleAnchor;
		const double Length = FromAnchor.Size();
		const double Rope = static_cast<double>(FMath::Max(Next.RopeLength, 1.f));
		if (Length > 1.0)
		{
			FromAnchor *= Rope / Length;
		}
		Next.TravelLocation = Next.GrappleAnchor + FromAnchor;
		const FVector Outward = FromAnchor.GetSafeNormal();
		Vel = Vel - Outward * FVector::DotProduct(Vel, Outward);
		Next.SwingVelocity = Vel;
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = Vel;
	}

	if (Next.bYanking)
	{
		const FVector ToAnchor = Next.GrappleAnchor - Next.TravelLocation;
		const double Span = ToAnchor.Size();
		const double Arrive = static_cast<double>(FMath::Max(0.f, Tuning.GrappleYankArrive));
		if (Span <= Arrive)
		{
			Next.bYanking = false;
			Next.TravelLocation = Next.GrappleAnchor;
			Next.bOverrideVelocity = true;
			Next.OverrideVelocity = FVector::ZeroVector;
		}
		else
		{
			const FVector Dir = ToAnchor / Span;
			const double StepDist = static_cast<double>(FMath::Max(0.f, Tuning.GrappleYankSpeed)) * static_cast<double>(Dt);
			if (StepDist >= Span - Arrive)
			{
				Next.bYanking = false;
				Next.TravelLocation = Next.GrappleAnchor;
				Next.bOverrideVelocity = true;
				Next.OverrideVelocity = Dir * Span / static_cast<double>(FMath::Max(Dt, 0.016f));
			}
			else
			{
				Next.TravelLocation += Dir * StepDist;
				Next.bOverrideVelocity = true;
				Next.OverrideVelocity = Dir * static_cast<double>(Tuning.GrappleYankSpeed);
			}
		}
	}

	const bool bBusy = Next.bGripping || Next.bDashing || Next.bSwiveling || Next.bSwinging || Next.bYanking || Next.bOverrideVelocity;
	if (!Contact.bWallContact)
	{
		Next.bWallRunBlocked = false;
		Next.bWallRunning = false;
		Next.WallRunTimeLeft = Tuning.WallRunMaxSeconds;
	}
	else if (bBusy)
	{
		Next.bWallRunning = false;
	}
	else
	{
		FVector AlongDir = FVector::ForwardVector;
		const bool bAlong = GFTraversalDetail::GFTraversalAlongWall(Command.MoveDirection, Contact.WallNormal, Tuning.WallRunAlongMin, AlongDir);
		const bool bFast = Command.HorizontalSpeed > Tuning.WallRunMinSpeed;
		const bool bAir = !Command.bGrounded;
		if (Next.bWallRunning && (!bAlong || !bFast || !bAir))
		{
			Next.bWallRunning = false;
		}
		if (!Next.bWallRunning && !Next.bWallRunBlocked && bAlong && bFast && bAir && Next.WallRunTimeLeft > 0.f && (bInfinite || Next.Stamina > 0.f))
		{
			Next.bWallRunning = true;
			Next.WallRunDir = AlongDir;
			Next.WallRunSpeed = Command.HorizontalSpeed;
			Next.WallNormal = GFTraversalDetail::GFTraversalFlatNormal(Contact.WallNormal);
		}
		if (Next.bWallRunning)
		{
			if (bAlong)
			{
				Next.WallRunDir = AlongDir;
			}
			const float Consumed = GFTraversalDetail::GFTraversalConsume(Next.WallRunTimeLeft, Dt);
			Pay(Tuning.WallRunDrainPerSecond * Consumed);
			if (Next.WallRunTimeLeft <= KINDA_SMALL_NUMBER || (!bInfinite && Next.Stamina <= 0.f))
			{
				Next.bWallRunning = false;
				Next.WallRunTimeLeft = 0.f;
				Next.bWallRunBlocked = true;
			}
			else
			{
				Next.bOverrideVelocity = true;
				Next.OverrideVelocity = Next.WallRunDir * static_cast<double>(Next.WallRunSpeed)
					+ Next.WallNormal * -static_cast<double>(FMath::Max(0.f, Tuning.WallStickSpeed));
			}
		}
	}

	return bSpent;
}
