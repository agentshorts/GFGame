// Acceleration Hop v3 (issue 347): the live landing circle in a real PIE tick. The v2 tests proved the landing math
// with a hand-built probe, and the circle still sat at takeoff in play. This one flies the listen host's own pawn in
// the PIE world, sweeps the camera mid-air the way a mouse does, lets the world tick, and checks that the landing
// target the HUD draws followed the camera frame by frame, held still once the aim held, that the flight turned with
// it, and that the pawn came down where the target said.
// The first version snapped the camera 60 degrees in one frame and then asked for the circle to keep moving. A circle
// that predicts the steering it will do jumps once on the snap and then holds still (it was right the first time), so
// that check failed 0 of 37 frames with the steering working. A sweep moves the circle every frame it turns.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.AccelHop.LiveLandingCircle;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFAccelHop.h"
#include "GFAccelHopRoad.h"
#include "GFCharacterMovementComponent.h"
#include "GFStaminaSim.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetAccelHopSteerDetail
{
	constexpr EAutomationTestFlags NetAccelHopSteerFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** How high above the spawn the test flies, clear of anything the test map has. */
	constexpr double SteerSkyLiftCm = 3000.0;
	/** The camera turn mid-air, degrees left of the takeoff heading (+Y side). */
	constexpr double SteerTurnYawDeg = 60.0;
	/** The camera sweeps from 0 to SteerTurnYawDeg over this many world seconds (a mouse turn, not a snap). */
	constexpr double SteerSweepSeconds = 0.2;
	/** World seconds flown from the start of the sweep before the checks (the rest holds the aim at the end yaw). */
	constexpr double SteerTurnSeconds = 0.3;
	/** With the aim held, the circle may wobble this much frame to frame (sampling), cm. The math port moves under 1. */
	constexpr double SteerHoldWobbleCm = 25.0;
	/** The camera yaw read back from the pawn must match what the test set, degrees. */
	constexpr double SteerYawTolDeg = 0.5;

	struct FSteerRun
	{
		TWeakObjectPtr<ACharacter> Body;
		TWeakObjectPtr<AStaticMeshActor> Plate;
		TWeakObjectPtr<AGFAccelHopRoad> Road;
		FVector Start = FVector::ZeroVector;
		FVector TakeoffTarget = FVector::ZeroVector;
		FVector TakeoffVelocity = FVector::ZeroVector;
		double TurnAt = -1.0;
		int32 Frames = 0;
		/** Frames inside the sweep (the camera turned since the last one), and how many of them moved the circle. */
		int32 SweepFrames = 0;
		int32 FramesMoved = 0;
		/** Frames after the sweep (aim held), and the biggest frame to frame circle move among them. */
		int32 HoldFrames = 0;
		double HoldWobbleCm = 0.0;
		int32 FramesSteerOn = 0;
		/** Falling frames whose move flew with the steering off, and how many of those were outside the road scope. */
		int32 FramesSteerOff = 0;
		int32 FramesOffRoad = 0;
		FVector OffRoadAt = FVector::ZeroVector;
		/** Worst gap between the yaw the test set and the yaw the pawn reports. */
		double MaxYawErrDeg = 0.0;
		float MaxTurnDeg = 0.f;
		FVector LastTarget = FVector::ZeroVector;
		bool bHaveLast = false;
		/** The target as of the end of the turn window (the pawn then flies on with the aim held). */
		FVector HeldTarget = FVector::ZeroVector;
		bool bLandedEarly = false;
	};

	/** The listen host's own pawn in the server world (it moves itself, so its moves run here every tick). */
	ACharacter* SteerHostBody(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return Cast<ACharacter>(PC->GetPawn());
			}
		}
		return nullptr;
	}

	UGFCharacterMovementComponent* SteerMove(const FSteerRun& Run)
	{
		ACharacter* Body = Run.Body.Get();
		return Body ? Cast<UGFCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
	}

	FVector SteerFeet(const ACharacter* Body)
	{
		FVector Feet = Body->GetActorLocation();
		if (const UCapsuleComponent* Capsule = Body->GetCapsuleComponent())
		{
			Feet.Z -= static_cast<double>(Capsule->GetScaledCapsuleHalfHeight());
		}
		return Feet;
	}

	double SteerFlatYawDeg(const FVector& V)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X));
	}

	/** The camera yaw at Elapsed world seconds into the sweep. */
	double SteerSweepYawDeg(double Elapsed)
	{
		return SteerTurnYawDeg * FMath::Clamp(Elapsed / SteerSweepSeconds, 0.0, 1.0);
	}

	/** Sets the camera on the host pawn's controller. False if there is no player controller to set. */
	bool SteerSetYaw(ACharacter* Body, double YawDeg)
	{
		APlayerController* PC = Body ? Cast<APlayerController>(Body->GetController()) : nullptr;
		if (!PC)
		{
			return false;
		}
		PC->SetControlRotation(FRotator(0.0, YawDeg, 0.0));
		return true;
	}

	double SteerFlatSpeed(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0).Size();
	}

	void SteerFail(const GFNetTest::FCtxRef& Ctx, const FString& Why)
	{
		Ctx->Test->AddError(Why);
		Ctx->bFailed = true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetAccelHopLiveLandingCircleTest, "GFGame.Net.AccelHop.LiveLandingCircle", GFNetAccelHopSteerDetail::NetAccelHopSteerFlags)
bool FGFNetAccelHopLiveLandingCircleTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetAccelHopSteerDetail;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FSteerRun> Run = MakeShared<FSteerRun>();

	WaitUntil(Ctx, TEXT("the host pawn is in the server world"), 20.f, []()
	{
		return SteerHostBody(ServerWorld()) != nullptr;
	});

	// A big flat plate in clear sky, a hop road around it (aim steering is road-only by default), the pawn on top.
	Do(Ctx, [Ctx, Run]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = SteerHostBody(World);
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Body || !Cube)
		{
			SteerFail(Ctx, TEXT("No host pawn or no engine cube for the floor plate"));
			return;
		}
		Run->Body = Body;
		const FVector Floor = Body->GetActorLocation() + FVector(0.0, 0.0, SteerSkyLiftCm);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// The engine cube is 100 cm. 80 x 80 x 0.2 is an 80 m plate, 20 cm thick, its top at Floor.
		AStaticMeshActor* Plate = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, Floor - FVector(0.0, 0.0, 10.0), FVector(80.0, 80.0, 0.2)), Params);
		AGFAccelHopRoad* Road = World->SpawnActor<AGFAccelHopRoad>(AGFAccelHopRoad::StaticClass(), FTransform(Floor), Params);
		if (!Plate || !Road)
		{
			SteerFail(Ctx, TEXT("Could not spawn the plate or the road"));
			return;
		}
		UStaticMeshComponent* Mesh = Plate->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Road->SetAutoPick(false);
		Road->SetRoadExtent(FVector(4000.0, 4000.0, 1500.0));
		Run->Plate = Plate;
		Run->Road = Road;
		const float Half = Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		Body->SetActorLocation(Floor + FVector(0.0, 0.0, static_cast<double>(Half) + 5.0), false, nullptr, ETeleportType::TeleportPhysics);
		Body->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		if (APlayerController* PC = Cast<APlayerController>(Body->GetController()))
		{
			PC->SetControlRotation(FRotator::ZeroRotator);
		}
	});

	WaitUntil(Ctx, TEXT("the pawn stands on the plate"), 5.f, [Run]()
	{
		const UGFCharacterMovementComponent* Move = SteerMove(*Run);
		return Move && Move->IsMovingOnGround();
	});

	// Hop straight down +X with the camera looking the same way.
	Do(Ctx, [Ctx, Run]()
	{
		ACharacter* Body = Run->Body.Get();
		UGFCharacterMovementComponent* Move = SteerMove(*Run);
		if (!Body || !Move)
		{
			SteerFail(Ctx, TEXT("Lost the host pawn"));
			return;
		}
		if (!Move->IsStaminaEnabled())
		{
			SteerFail(Ctx, TEXT("Stamina movement is off on the net test map, so there is no landing marker to test"));
			return;
		}
		if (!Move->GetTuning().AccelHop.bAirSteerFollowsAim)
		{
			SteerFail(Ctx, TEXT("bAirSteerFollowsAim is off in the movement tuning asset"));
			return;
		}
		Run->Start = SteerFeet(Body);
		Body->LaunchCharacter(FVector(700.0, 0.0, 900.0), true, true);
	});

	WaitUntil(Ctx, TEXT("the pawn is in the air with a landing target"), 3.f, [Run]()
	{
		const UGFCharacterMovementComponent* Move = SteerMove(*Run);
		return Move && Move->IsFalling() && Move->GetLandingTarget().bValid;
	});

	// Takeoff: the circle is ahead on the line, and asking again a couple of frames later (aim unchanged) agrees.
	Do(Ctx, [Ctx, Run]()
	{
		UGFCharacterMovementComponent* Move = SteerMove(*Run);
		if (!Move)
		{
			SteerFail(Ctx, TEXT("Lost the host movement"));
			return;
		}
		const FGFLandingTarget Target = Move->GetLandingTarget();
		if (!Target.bValid)
		{
			SteerFail(Ctx, TEXT("The landing marker is not valid in the air (GetLandingTarget returned nothing)"));
			return;
		}
		Run->TakeoffTarget = Target.Point;
		Run->TakeoffVelocity = Move->Velocity;
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Takeoff target is ahead (dX %.0f)"), Target.Point.X - Run->Start.X), Target.Point.X > Run->Start.X + 300.0);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Takeoff target is on the line (dY %.1f)"), Target.Point.Y - Run->Start.Y), FMath::Abs(Target.Point.Y - Run->Start.Y) < 30.0);
		Ctx->Test->TestTrue(TEXT("The move flies with aim steering on (on the road, player pawn)"), Move->GetMoveAirSteer().bOn);
	});
	Wait(Ctx, 0.05f);
	Do(Ctx, [Ctx, Run]()
	{
		UGFCharacterMovementComponent* Move = SteerMove(*Run);
		if (!Move || !Move->IsFalling())
		{
			SteerFail(Ctx, TEXT("The pawn came down before the steady check"));
			return;
		}
		const FGFLandingTarget Target = Move->GetLandingTarget();
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Aim held: the target holds still (%.1f cm)"), FVector::Dist2D(Target.Point, Run->TakeoffTarget)), Target.bValid && FVector::Dist2D(Target.Point, Run->TakeoffTarget) < 25.0);
		// From here the camera sweeps 60 degrees left over SteerSweepSeconds, then holds. Nothing else changes: no
		// stick input, no dash.
		if (!SteerSetYaw(Run->Body.Get(), 0.0))
		{
			SteerFail(Ctx, TEXT("The host pawn has no player controller, so the test cannot turn the camera"));
			return;
		}
		Run->TurnAt = ServerWorld()->GetTimeSeconds();
		Run->TakeoffTarget = Target.Point;
	});

	// Tick the world. Every frame: set this frame's camera yaw (set every frame, so nothing can undo it between frames),
	// read it back, read the target the HUD would draw, and what the move did.
	WaitUntil(Ctx, TEXT("0.3 s of world time flown while the camera sweeps then holds"), 5.f, [Run]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = Run->Body.Get();
		UGFCharacterMovementComponent* Move = SteerMove(*Run);
		if (!World || !Body || !Move)
		{
			return true;
		}
		if (!Move->IsFalling())
		{
			Run->bLandedEarly = true;
			return true;
		}
		const double Elapsed = World->GetTimeSeconds() - Run->TurnAt;
		const bool bSweeping = Elapsed < SteerSweepSeconds;
		const double Yaw = SteerSweepYawDeg(Elapsed);
		SteerSetYaw(Body, Yaw);
		Run->MaxYawErrDeg = FMath::Max(Run->MaxYawErrDeg, FMath::Abs(FMath::FindDeltaAngleDegrees(Body->GetControlRotation().Yaw, Yaw)));

		const FGFLandingTarget Target = Move->GetLandingTarget();
		if (Target.bValid)
		{
			++Run->Frames;
			if (Run->bHaveLast)
			{
				const double Step = FVector::Dist2D(Target.Point, Run->LastTarget);
				if (bSweeping)
				{
					++Run->SweepFrames;
					if (Step > 1.0)
					{
						++Run->FramesMoved;
					}
				}
				else if (Run->HoldFrames++ > 0)
				{
					// The first held frame still carries the sweep's last turn; the wobble counts from the second.
					Run->HoldWobbleCm = FMath::Max(Run->HoldWobbleCm, Step);
				}
			}
			Run->LastTarget = Target.Point;
			Run->bHaveLast = true;
			Run->HeldTarget = Target.Point;
		}
		if (Move->GetMoveAirSteer().bOn)
		{
			++Run->FramesSteerOn;
		}
		else
		{
			++Run->FramesSteerOff;
			if (!Move->IsInAirSteerRoadScope(Body->GetActorLocation()))
			{
				++Run->FramesOffRoad;
				Run->OffRoadAt = Body->GetActorLocation();
			}
		}
		Run->MaxTurnDeg = FMath::Max(Run->MaxTurnDeg, Move->GetLastAirSteerTurnDeg());
		return Elapsed >= SteerTurnSeconds;
	});

	Do(Ctx, [Ctx, Run]()
	{
		UGFCharacterMovementComponent* Move = SteerMove(*Run);
		if (!Move)
		{
			SteerFail(Ctx, TEXT("Lost the host movement"));
			return;
		}
		Ctx->Test->TestFalse(TEXT("Still in the air after the turn window (the hop is long enough to test)"), Run->bLandedEarly);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The camera yaw the test set stuck on the pawn (worst %.2f deg off)"), Run->MaxYawErrDeg), Run->MaxYawErrDeg <= SteerYawTolDeg);
		Ctx->Test->TestTrue(TEXT("The flight is known to have taken off on the road (it keeps steering if it drifts out of the box)"), Move->DidAirSteerTakeOffOnRoad());
		if (Run->FramesOffRoad > 0)
		{
			SteerFail(Ctx, FString::Printf(TEXT("The pawn left the hop road scope mid-flight on %d frames (last at %s), so aim steering switched off"),
				Run->FramesOffRoad, *Run->OffRoadAt.ToCompactString()));
		}
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Ticked a few frames with a target (%d)"), Run->Frames), Run->Frames >= 3);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Ticked a few frames of the sweep (%d)"), Run->SweepFrames), Run->SweepFrames >= 3);
		// Every sweep frame turns the camera, so (nearly) every one should move the circle. Half leaves room for a
		// frame where the world did not tick between two reads.
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The circle followed the camera frame to frame (%d of %d sweep frames)"), Run->FramesMoved, Run->SweepFrames),
			Run->FramesMoved >= 2 && Run->FramesMoved * 2 >= Run->SweepFrames);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Aim held after the sweep: the circle holds still (worst %.1f cm a frame over %d frames)"), Run->HoldWobbleCm, Run->HoldFrames),
			Run->HoldWobbleCm < SteerHoldWobbleCm);
		const double Shift = Run->HeldTarget.Y - Run->TakeoffTarget.Y;
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The circle moved toward the new aim (+Y %.0f cm)"), Shift), Shift > 60.0);
		const double Yaw = SteerFlatYawDeg(Move->Velocity);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The flight turned toward the aim (heading %.1f deg)"), Yaw), Yaw > 5.0 && Yaw <= SteerTurnYawDeg + 0.5);
		const double Before = SteerFlatSpeed(Run->TakeoffVelocity);
		const double Now = SteerFlatSpeed(Move->Velocity);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("Speed kept through the turn (%.0f -> %.0f cm/s)"), Before, Now), Before > 1.0 && FMath::Abs(Now - Before) / Before < 0.05);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The move flew with aim steering on (%d on, %d off)"), Run->FramesSteerOn, Run->FramesSteerOff), Run->FramesSteerOn > 0 && Run->FramesSteerOff == 0);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The move turned the velocity (max %.2f deg in a step)"), Run->MaxTurnDeg), Run->MaxTurnDeg > 0.f);
	});

	WaitUntil(Ctx, TEXT("the pawn lands on the plate"), 5.f, [Run]()
	{
		const UGFCharacterMovementComponent* Move = SteerMove(*Run);
		return !Move || Move->IsMovingOnGround();
	});

	// Aim held since the window, so the pawn should come down where the circle said it would.
	Do(Ctx, [Ctx, Run]()
	{
		ACharacter* Body = Run->Body.Get();
		if (Body && !Run->bLandedEarly)
		{
			const FVector Feet = SteerFeet(Body);
			const double Off = FVector::Dist2D(Feet, Run->HeldTarget);
			Ctx->Test->TestTrue(*FString::Printf(TEXT("Landed where the circle was (%.0f cm off)"), Off), Off < 75.0);
			Ctx->Test->TestTrue(*FString::Printf(TEXT("Landed left of the straight line (+Y %.0f cm)"), Feet.Y - Run->Start.Y), Feet.Y > Run->Start.Y + 100.0);
		}
		if (AStaticMeshActor* Plate = Run->Plate.Get())
		{
			Plate->Destroy();
		}
		if (AGFAccelHopRoad* Road = Run->Road.Get())
		{
			Road->Destroy();
		}
	});
	Stop(Ctx);
	return true;
}

#endif
