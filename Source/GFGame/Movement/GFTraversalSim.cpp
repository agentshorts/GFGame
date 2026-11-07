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

	FVector GFTraversalFlatUnit(const FVector& Direction)
	{
		return FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal();
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

bool GFTraversalSim::IsIntoWall(const FVector& MoveDirection, const FVector& WallNormal, float MinDot)
{
	const FVector Normal = GFTraversalDetail::GFTraversalFlatNormal(WallNormal);
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	const FVector Move = GFTraversalDetail::GFTraversalFlatUnit(MoveDirection);
	if (Move.IsNearlyZero())
	{
		return false;
	}
	const double Into = FVector::DotProduct(Move, Normal * -1.0);
	const double Along = FMath::Abs(FVector::DotProduct(Move, Tangent));
	return Into >= static_cast<double>(FMath::Max(0.f, MinDot)) && Into > Along;
}

bool GFTraversalSim::MantleFinished(const FGFStaminaState& Before, const FGFStaminaState& After)
{
	return Before.bMantling && !After.bMantling;
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
	State.bWallClimbing = false;
	State.bWallClimbBlocked = false;
	State.WallClimbTimeLeft = 0.f;
	State.bMantling = false;
	State.MantleTimeLeft = 0.f;
	State.bSwiveling = false;
	State.bSwinging = false;
	State.bYanking = false;
	State.bOverrideVelocity = false;
	State.OverrideVelocity = FVector::ZeroVector;
	State.CurrentMove = EGFTraversalMove::None;
}

void GFTraversalSim::PublishCurrentMove(FGFStaminaState& State)
{
	// A block always clears. An in-progress run or climb must not keep going once the visit is closed.
	if (State.bWallRunBlocked)
	{
		State.bWallRunning = false;
	}
	if (State.bWallClimbBlocked)
	{
		State.bWallClimbing = false;
	}

	// First match wins: the move that cancels the others when it starts (yank, then swing, swivel, grip),
	// then the wall chain (mantle, climb, wall run), then dash.
	const bool Active[] = {
		State.bYanking,
		State.bSwinging,
		State.bSwiveling,
		State.bGripping,
		State.bMantling,
		State.bWallClimbing,
		State.bWallRunning,
		State.bDashing,
	};
	const EGFTraversalMove Ids[] = {
		EGFTraversalMove::Yank,
		EGFTraversalMove::Swing,
		EGFTraversalMove::Swivel,
		EGFTraversalMove::Grip,
		EGFTraversalMove::Mantle,
		EGFTraversalMove::WallClimb,
		EGFTraversalMove::WallRun,
		EGFTraversalMove::Dash,
	};
	int32 Winner = INDEX_NONE;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Active); ++Index)
	{
		if (Active[Index])
		{
			Winner = Index;
			break;
		}
	}

	if (Winner != 0)
	{
		State.bYanking = false;
	}
	if (Winner != 1)
	{
		State.bSwinging = false;
	}
	if (Winner != 2)
	{
		State.bSwiveling = false;
	}
	if (Winner != 3)
	{
		State.bGripping = false;
	}
	if (Winner != 4)
	{
		State.bMantling = false;
	}
	if (Winner != 5)
	{
		State.bWallClimbing = false;
	}
	if (Winner != 6)
	{
		State.bWallRunning = false;
	}
	if (Winner != 7)
	{
		State.bDashing = false;
		State.DashTimeLeft = 0.f;
	}
	else
	{
		State.bOverrideVelocity = false;
		State.OverrideVelocity = FVector::ZeroVector;
	}
	State.CurrentMove = Winner == INDEX_NONE ? EGFTraversalMove::None : Ids[Winner];
}

bool GFTraversalSim::ShouldShowLandingMarker(const FGFStaminaState& State, bool bGrounded, bool bEnabled)
{
	if (!bEnabled || State.bGripping || State.bWallRunning || State.bSwiveling || State.bWallClimbing || State.bMantling)
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

FVector GFTraversalSim::SteerTowardAim(const FVector& Velocity, const FGFAirSteer& Steer, float Dt)
{
	if (!Steer.bOn || Dt <= 0.f || Steer.TurnRateDeg <= 0.f)
	{
		return Velocity;
	}
	const double Speed = FVector(Velocity.X, Velocity.Y, 0.0).Size();
	if (Speed < static_cast<double>(FMath::Max(1.f, Steer.MinSpeed)))
	{
		return Velocity;
	}
	const FVector Aim = FVector(Steer.Aim.X, Steer.Aim.Y, 0.0).GetSafeNormal();
	if (Aim.IsNearlyZero())
	{
		return Velocity;
	}
	const double Heading = FMath::Atan2(Velocity.Y, Velocity.X);
	const double Want = FMath::Atan2(Aim.Y, Aim.X);
	const double Delta = FMath::FindDeltaAngleRadians(Heading, Want);
	if (FMath::Abs(Delta) > FMath::DegreesToRadians(static_cast<double>(FMath::Clamp(Steer.MaxAngleDeg, 0.f, 180.f))))
	{
		return Velocity;
	}
	const double MaxTurn = FMath::DegreesToRadians(static_cast<double>(Steer.TurnRateDeg)) * static_cast<double>(Dt);
	const double Turned = Heading + FMath::Clamp(Delta, -MaxTurn, MaxTurn);
	return FVector(FMath::Cos(Turned) * Speed, FMath::Sin(Turned) * Speed, Velocity.Z);
}

void GFTraversalSim::StepFlight(FVector& Position, FVector& Velocity, const FVector& LateralAccel, float GravityZ, float MaxLateralSpeed, float Dt)
{
	StepFlight(Position, Velocity, LateralAccel, FGFAirSteer(), GravityZ, MaxLateralSpeed, Dt);
}

void GFTraversalSim::StepFlight(FVector& Position, FVector& Velocity, const FVector& LateralAccel, const FGFAirSteer& Steer, float GravityZ, float MaxLateralSpeed, float Dt)
{
	const double T = static_cast<double>(FMath::Max(0.f, Dt));
	const FVector FlatBefore(Velocity.X, Velocity.Y, 0.0);
	FVector FlatAfter = FlatBefore + FVector(LateralAccel.X, LateralAccel.Y, 0.0) * T;
	const double Cap = FMath::Max(static_cast<double>(FMath::Max(0.f, MaxLateralSpeed)), FlatBefore.Size());
	if (FlatAfter.SizeSquared() > Cap * Cap)
	{
		FlatAfter = FlatAfter.GetSafeNormal() * Cap;
	}
	FlatAfter = SteerTowardAim(FlatAfter, Steer, Dt);
	const double Gravity = static_cast<double>(GravityZ);
	Position.X += 0.5 * (FlatBefore.X + FlatAfter.X) * T;
	Position.Y += 0.5 * (FlatBefore.Y + FlatAfter.Y) * T;
	Position.Z += Velocity.Z * T + 0.5 * Gravity * T * T;
	Velocity = FVector(FlatAfter.X, FlatAfter.Y, Velocity.Z + Gravity * T);
}

FGFLandingPrediction GFTraversalSim::PredictFlightLanding(const FGFFlightProbe& Probe, TFunctionRef<bool(const FVector&, const FVector&, FVector&)> Trace)
{
	FGFLandingPrediction Result;
	const float Lookahead = FMath::Max(0.05f, Probe.Lookahead);
	const float Sample = FMath::Max(Probe.SampleSeconds, 0.02f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Lookahead / Sample), 1, 64);
	const float StepTime = Lookahead / static_cast<float>(Steps);
	FVector Position = Probe.Origin;
	FVector Velocity = Probe.Velocity;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const FVector Previous = Position;
		StepFlight(Position, Velocity, Probe.LateralAccel, Probe.Steer, Probe.GravityZ, Probe.MaxLateralSpeed, StepTime);
		FVector Hit = FVector::ZeroVector;
		if (Trace(Previous, Position, Hit))
		{
			const double Segment = FVector::Dist(Previous, Position);
			const double Along = Segment > 1.0e-6 ? FMath::Clamp(FVector::Dist(Previous, Hit) / Segment, 0.0, 1.0) : 1.0;
			Result.bHit = true;
			Result.Point = Hit;
			Result.Time = StepTime * (static_cast<float>(Index - 1) + static_cast<float>(Along));
			return Result;
		}
	}
	return Result;
}

bool GFTraversalSim::Apply(FGFStaminaState& Next, const FGFStaminaCommand& Command, const UGFMovementTuning& Tuning)
{
	const float Dt = GFTraversalDetail::GFTraversalNonNegative(Command.DeltaSeconds);
	const bool bInfinite = Command.bInsideKindleRing || Command.bCostsFree;
	const float KickSpeed = Command.WallKickSpeedOverride >= 0.f ? Command.WallKickSpeedOverride : Tuning.WallKickSpeed;
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

	// Grip already replaced a dash. Clear it before the kick or the cling so the dash timer cannot revive it.
	if (Next.bGripping)
	{
		Next.bDashing = false;
		Next.DashTimeLeft = 0.f;
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
		Next.OverrideVelocity = Normal * static_cast<double>(KickSpeed) + FVector(0.0, 0.0, static_cast<double>(Tuning.WallKickUp));
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

	// Yank and swing both continue from their own flags. Yank is the one that cancels the other on the press.
	if (Next.bYanking)
	{
		Next.bSwinging = false;
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

	// A live move blocks climb and wall run. A zero override is a stop written when a yank or a mantle arrives,
	// so it is not a move and must not block the next one. A kick, launch, or release still owns the tick.
	const bool bExitOwnsTick = Next.bOverrideVelocity && !Next.OverrideVelocity.IsNearlyZero();
	const bool bBusy = Next.bGripping || Next.bDashing || Next.bSwiveling || Next.bSwinging || Next.bYanking || bExitOwnsTick;

	// #355 wall climb and its mantle. Cling, dash, pole and rope above already won this step if they are busy.
	if (Command.bGrounded)
	{
		Next.WallClimbCoyoteLeft = Tuning.WallClimbCoyoteSeconds;
	}
	else
	{
		GFTraversalDetail::GFTraversalConsume(Next.WallClimbCoyoteLeft, Dt);
	}
	bool bClimbHandoff = false;
	FVector HandoffNormal = Next.WallClimbNormal;
	auto EndMantle = [&Next]()
	{
		Next.bMantling = false;
		Next.MantleTimeLeft = 0.f;
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = FVector::ZeroVector;
	};
	auto StepMantle = [&Next, &Command, &Tuning, Dt, &EndMantle]()
	{
		GFTraversalDetail::GFTraversalConsume(Next.MantleTimeLeft, Dt);
		const FVector To = Next.MantleTarget - Command.PlayerLocation;
		const double Arrive = static_cast<double>(FMath::Max(0.f, Tuning.WallClimbMantleArrive));
		const double StepTime = static_cast<double>(FMath::Max(Dt, KINDA_SMALL_NUMBER));
		if (Next.MantleTimeLeft <= KINDA_SMALL_NUMBER)
		{
			EndMantle();
			return;
		}
		if (To.Z > Arrive)
		{
			// Rise first so the capsule clears the lip instead of sliding into the wall top.
			const double Up = FMath::Min(static_cast<double>(FMath::Max(0.f, Tuning.WallClimbMantleUpSpeed)), To.Z / StepTime);
			Next.bOverrideVelocity = true;
			Next.OverrideVelocity = FVector(0.0, 0.0, Up);
			return;
		}
		const FVector Flat(To.X, To.Y, 0.0);
		const double Dist = Flat.Size();
		if (Dist <= Arrive || Command.bGrounded)
		{
			EndMantle();
			return;
		}
		const double Speed = FMath::Min(static_cast<double>(FMath::Max(0.f, Tuning.WallClimbMantleForwardSpeed)), Dist / StepTime);
		Next.bOverrideVelocity = true;
		Next.OverrideVelocity = Flat / Dist * Speed;
	};

	if (bBusy || !Command.bWallClimbAllowed)
	{
		Next.bWallClimbing = false;
		Next.bMantling = false;
		Next.MantleTimeLeft = 0.f;
	}
	else if (Next.bMantling)
	{
		Next.bWallClimbing = false;
		StepMantle();
	}
	else
	{
		// A closed visit does not keep climbing.
		if (Next.bWallClimbBlocked)
		{
			Next.bWallClimbing = false;
		}
		bool bClimbWall = false;
		FVector ClimbNormal = Next.WallClimbNormal;
		if (Contact.bWallAhead && GFTraversalDetail::GFTraversalIsWallNormal(Contact.WallAheadNormal, Tuning.WallNormalMaxZ))
		{
			bClimbWall = true;
			ClimbNormal = GFTraversalDetail::GFTraversalFlatNormal(Contact.WallAheadNormal);
		}
		else if (Contact.bWallContact && GFTraversalDetail::GFTraversalIsWallNormal(Contact.WallNormal, Tuning.WallNormalMaxZ))
		{
			bClimbWall = true;
			ClimbNormal = GFTraversalDetail::GFTraversalFlatNormal(Contact.WallNormal);
		}
		const bool bInto = bClimbWall && GFTraversalSim::IsIntoWall(Command.MoveDirection, ClimbNormal, Tuning.WallClimbIntoMin);
		if (!bInto && !Next.bWallClimbing)
		{
			Next.bWallClimbBlocked = false;
		}

		const bool bFloorOrCoyote = Command.bGrounded || Next.WallClimbCoyoteLeft > 0.f;
		// A jump press is a hop. Coyote counts as floor for the climb start, so a press in that window
		// used to start a climb and the branch below turned it into a kick. ResolveJump then refused the hop.
		if (!Next.bWallClimbing && !Next.bWallClimbBlocked && bInto && bFloorOrCoyote && !Command.bJumpPressed
			&& Tuning.WallClimbMaxSeconds > 0.f && (bInfinite || Next.Stamina > 0.f))
		{
			Next.bWallClimbing = true;
			Next.WallClimbTimeLeft = Tuning.WallClimbMaxSeconds;
			Next.WallClimbNormal = ClimbNormal;
			Next.bWallRunning = false;
		}

		if (Next.bWallClimbing)
		{
			FVector AlongDir = FVector::ForwardVector;
			const bool bAlong = bClimbWall && GFTraversalDetail::GFTraversalAlongWall(Command.MoveDirection, ClimbNormal, Tuning.WallRunAlongMin, AlongDir);
			const bool bLedgeInReach = Contact.bLedge
				&& Contact.LedgeHeight >= Tuning.WallClimbMantleMinLip
				&& Contact.LedgeHeight <= Tuning.WallClimbMantleReach;
			if (bClimbWall)
			{
				Next.WallClimbNormal = ClimbNormal;
			}
			if (!bClimbWall)
			{
				Next.bWallClimbing = false;
			}
			else if (Command.bJumpPressed && Command.bGrounded)
			{
				// Still on the floor: the press is the ground hop, not a kick. Block a re-grab this visit.
				Next.bWallClimbing = false;
				Next.bWallClimbBlocked = true;
				Next.bOverrideVelocity = false;
				Next.OverrideVelocity = FVector::ZeroVector;
			}
			else if (Command.bJumpPressed)
			{
				// Same kick as a cling: off the wall, with the hop count and the air dash back.
				Next.bWallClimbing = false;
				Next.bWallClimbBlocked = true;
				Next.AirDashesUsed = 0;
				Next.JumpsUsed = 0;
				Next.FlipTimeLeft = 0.f;
				Next.bTraversalJump = true;
				Next.bOverrideVelocity = true;
				Next.OverrideVelocity = ClimbNormal * static_cast<double>(KickSpeed) + FVector(0.0, 0.0, static_cast<double>(Tuning.WallKickUp));
			}
			else if (bAlong)
			{
				Next.bWallClimbing = false;
				bClimbHandoff = true;
				HandoffNormal = ClimbNormal;
			}
			else if (!bInto)
			{
				Next.bWallClimbing = false;
			}
			else if (bLedgeInReach && CanAfford(Tuning.WallClimbMantleCost))
			{
				Pay(Tuning.WallClimbMantleCost);
				Next.bWallClimbing = false;
				Next.bMantling = true;
				Next.MantleTimeLeft = Tuning.WallClimbMantleMaxSeconds;
				Next.MantleTarget = Contact.LedgeTarget;
				Next.MantleLipHeight = Contact.LedgeHeight;
				StepMantle();
			}
			else
			{
				const float Consumed = GFTraversalDetail::GFTraversalConsume(Next.WallClimbTimeLeft, Dt);
				Pay(Tuning.WallClimbDrainPerSecond * Consumed);
				if (Next.WallClimbTimeLeft <= KINDA_SMALL_NUMBER || (!bInfinite && Next.Stamina <= 0.f))
				{
					Next.bWallClimbing = false;
					Next.WallClimbTimeLeft = 0.f;
					Next.bWallClimbBlocked = true;
				}
				else
				{
					Next.bOverrideVelocity = true;
					Next.OverrideVelocity = FVector(0.0, 0.0, static_cast<double>(FMath::Max(0.f, Tuning.WallClimbSpeed)))
						+ ClimbNormal * -static_cast<double>(FMath::Max(0.f, Tuning.WallClimbStickSpeed));
				}
			}
		}
	}

	// A climb that turns along the wall hands off to the wall run below: same budget, same gates, with the climb
	// counted as off the floor and as carrying WallClimbToRunSpeed.
	const bool bRunWall = Contact.bWallContact || bClimbHandoff;
	const FVector RunNormal = bClimbHandoff ? HandoffNormal : Contact.WallNormal;
	if (!bRunWall)
	{
		Next.bWallRunBlocked = false;
		Next.bWallRunning = false;
		Next.WallRunTimeLeft = Tuning.WallRunMaxSeconds;
	}
	else if (bBusy || Next.bWallClimbing || Next.bMantling || ((Next.bOverrideVelocity && !Next.OverrideVelocity.IsNearlyZero()) && !bClimbHandoff))
	{
		Next.bWallRunning = false;
	}
	else
	{
		// A closed visit does not keep running, and it does not spend stamina while the flag was left on.
		if (Next.bWallRunBlocked)
		{
			Next.bWallRunning = false;
		}
		FVector AlongDir = FVector::ForwardVector;
		const bool bAlong = GFTraversalDetail::GFTraversalAlongWall(Command.MoveDirection, RunNormal, Tuning.WallRunAlongMin, AlongDir);
		const float RunSpeed = bClimbHandoff ? FMath::Max(Command.HorizontalSpeed, Tuning.WallClimbToRunSpeed) : Command.HorizontalSpeed;
		const bool bFast = RunSpeed > Tuning.WallRunMinSpeed;
		const bool bAir = !Command.bGrounded || bClimbHandoff;
		if (Next.bWallRunning && (!bAlong || !bFast || !bAir))
		{
			Next.bWallRunning = false;
		}
		if (!Next.bWallRunning && !Next.bWallRunBlocked && bAlong && bFast && bAir && Next.WallRunTimeLeft > 0.f && (bInfinite || Next.Stamina > 0.f))
		{
			Next.bWallRunning = true;
			Next.WallRunDir = AlongDir;
			Next.WallRunSpeed = RunSpeed;
			Next.WallNormal = GFTraversalDetail::GFTraversalFlatNormal(RunNormal);
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
