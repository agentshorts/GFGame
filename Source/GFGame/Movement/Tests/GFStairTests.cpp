// Hub stairs: ramp shape, foot plant, and a walk up and down every flight.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement.Stair;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFDino.h"
#include "GFFootIK.h"
#include "GFMovementTuning.h"
#include "GFStairCatalog.h"
#include "GFStairFlight.h"
#include "GFStairMovement.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFStairTestsDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool Near(float A, float B, float Tol)
	{
		return FMath::IsNearlyEqual(A, B, Tol);
	}

	bool KeptClear(const FVector& Point)
	{
		const float Dist = FVector2D(Point.X, Point.Y).Size();
		if (Dist <= 2000.f)
		{
			return false;
		}
		if (Point.X >= -3100.f && Point.X <= -1500.f && Point.Y >= -1600.f && Point.Y <= 1600.f)
		{
			return false;
		}
		if (Point.X >= 1400.f && Point.X <= 6000.f && FMath::Abs(Point.Y) < 700.f)
		{
			return false;
		}
		if (FVector2D::Distance(FVector2D(Point.X, Point.Y), FVector2D(-400.f, 400.f)) <= 400.f)
		{
			return false;
		}
		return Dist > 500.f;
	}

	struct FWorldScope
	{
		UWorld* World = nullptr;
		explicit FWorldScope(UWorld* InWorld) : World(InWorld) {}
		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FWalkOut
	{
		bool bOk = false;
		float Along = 0.f;
		float FeetZ = 0.f;
		const TCHAR* Why = TEXT("not run");
	};

	float AlongOf(const AGFStairFlight* Flight, const AActor* Actor)
	{
		return Flight->GetActorTransform().InverseTransformPosition(Actor->GetActorLocation()).X;
	}

	float FeetOf(const ACharacter* Pawn)
	{
		const float Half = Pawn->GetCapsuleComponent() ? Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f;
		return Pawn->GetActorLocation().Z - Half;
	}

	FWalkOut Walk(ACharacter* Pawn, const AGFStairFlight* Flight, float GoalAlong, bool bUp)
	{
		FWalkOut Out;
		UCharacterMovementComponent* Move = Pawn ? Pawn->GetCharacterMovement() : nullptr;
		if (!Pawn || !Flight || !Move)
		{
			Out.Why = TEXT("missing pawn");
			return Out;
		}
		Move->bRunPhysicsWithNoController = true;
		Move->StopMovementImmediately();
		const float Dt = 1.f / 60.f;
		for (int32 Index = 0; Index < 30; ++Index)
		{
			Move->TickComponent(Dt, LEVELTICK_All, nullptr);
		}
		Move->StopMovementImmediately();
		const FVector Forward = bUp ? Flight->GetActorForwardVector() : -Flight->GetActorForwardVector();
		float Previous = AlongOf(Flight, Pawn);
		int32 Stall = 0;
		for (int32 Index = 0; Index < 480; ++Index)
		{
			Pawn->AddMovementInput(Forward, 1.f);
			Move->TickComponent(Dt, LEVELTICK_All, nullptr);
			const float Along = AlongOf(Flight, Pawn);
			const float Feet = FeetOf(Pawn);
			Out.Along = Along;
			Out.FeetZ = Feet;
			if (Feet < Flight->GetActorLocation().Z - 40.f)
			{
				Out.Why = TEXT("fell off the flight");
				return Out;
			}
			const bool bReached = bUp ? Along >= GoalAlong : Along <= GoalAlong;
			if (bReached)
			{
				Move->StopMovementImmediately();
				Out.bOk = true;
				Out.Why = TEXT("reached");
				return Out;
			}
			const float Gain = bUp ? Along - Previous : Previous - Along;
			Stall = Gain < 3.f ? Stall + 1 : 0;
			Previous = Along;
			if (Stall >= 24)
			{
				Out.Why = TEXT("stalled");
				return Out;
			}
		}
		Out.Why = TEXT("timed out");
		return Out;
	}

	ACharacter* SpawnWalker(UWorld* World, UClass* Class, const FVector& At, const FRotator& Rot, bool bDino)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = bDino;
		ACharacter* Pawn = World->SpawnActor<ACharacter>(Class, FTransform(Rot, At), Params);
		if (!Pawn)
		{
			return nullptr;
		}
		if (bDino)
		{
			if (AGFDino* Dino = Cast<AGFDino>(Pawn))
			{
				Dino->AutoPossessAI = EAutoPossessAI::Disabled;
			}
			Pawn->FinishSpawning(FTransform(Rot, At));
		}
		Pawn->SetActorTickEnabled(false);
		if (AGFDino* Dino = Cast<AGFDino>(Pawn))
		{
			if (Dino->Wander)
			{
				Dino->Wander->SetComponentTickEnabled(false);
			}
			if (AController* Controller = Dino->GetController())
			{
				Controller->UnPossess();
			}
		}
		if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
		{
			Move->SetComponentTickEnabled(false);
			Move->bRunPhysicsWithNoController = true;
			Move->StopMovementImmediately();
		}
		return Pawn;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairRampTest, "GFGame.Movement.StairRamp", GFStairTestsDetail::Flags)
bool FGFStairRampTest::RunTest(const FString&)
{
	using namespace GFStairTestsDetail;
	const UGFMovementTuning& Tuning = *GetDefault<UGFMovementTuning>();
	TestTrue(TEXT("Tuning step height is one block plus a margin"), Near(Tuning.MaxStepHeight, 105.f, 0.1f));
	TestTrue(TEXT("Tuning floor angle is 50 degrees"), Near(Tuning.WalkableFloorAngle, 50.f, 0.1f));
	TestTrue(TEXT("Sole offset is the 2 mv foot"), Near(Tuning.FootSoleOffsetCm, 12.5f, 0.1f));

	const AGFDino* Dino = GetDefault<AGFDino>();
	const UCharacterMovementComponent* DinoMove = Dino ? Dino->GetCharacterMovement() : nullptr;
	if (!TestNotNull(TEXT("Dino movement"), DinoMove))
	{
		return false;
	}
	TestTrue(TEXT("Dino step height matches tuning"), Near(DinoMove->MaxStepHeight, Tuning.MaxStepHeight, 0.1f));
	TestTrue(TEXT("Dino floor angle matches tuning"), Near(DinoMove->GetWalkableFloorAngle(), Tuning.WalkableFloorAngle, 0.1f));

	const TArray<FGFStairFlightSpec> Flights = GFStairCatalog::HubFlights();
	TestEqual(TEXT("Three hub flights"), Flights.Num(), 3);
	for (const FGFStairFlightSpec& Spec : Flights)
	{
		TestTrue(*FString::Printf(TEXT("%s riser fits the step height"), *Spec.Id.ToString()), Spec.RiserCm < Tuning.MaxStepHeight);
		TestTrue(*FString::Printf(TEXT("%s pitch stays under the walkable angle"), *Spec.Id.ToString()), Spec.PitchDegrees() < Tuning.WalkableFloorAngle - 1.f);

		FVector Center = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector Half = FVector::ZeroVector;
		AGFStairFlight::RampPlacement(Spec, Center, Rotation, Half);
		const FVector Slope = Rotation.RotateVector(FVector::ForwardVector);
		const FVector Normal = Rotation.RotateVector(FVector::UpVector);
		const FVector TopMid = Center + Normal * (AGFStairFlight::RampThicknessCm * 0.5f);
		const FVector Start = TopMid - Slope * Half.X;
		const FVector End = TopMid + Slope * Half.X;
		TestTrue(*FString::Printf(TEXT("%s ramp starts at the local origin"), *Spec.Id.ToString()), Start.IsNearlyZero(0.5f));
		TestTrue(*FString::Printf(TEXT("%s ramp ends at the top of the run"), *Spec.Id.ToString()), End.Equals(FVector(Spec.RunCm(), 0.f, Spec.RiseCm()), 0.5f));

		const float Run = Spec.RunCm();
		const TArray<FVector> Samples = {
			Spec.Origin,
			Spec.Origin + FVector(-120.f, 0.f, 0.f),
			Spec.Origin + FVector(Run + 100.f, Spec.WidthCm * 0.5f, 0.f),
			Spec.Origin + FVector(Run + 100.f, -Spec.WidthCm * 0.5f, 0.f),
		};
		for (const FVector& Sample : Samples)
		{
			TestTrue(*FString::Printf(TEXT("%s stays clear of the hub dressing at %s"), *Spec.Id.ToString(), *Sample.ToCompactString()), KeptClear(Sample));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairFootIKTest, "GFGame.Movement.StairFootIK", GFStairTestsDetail::Flags)
bool FGFStairFootIKTest::RunTest(const FString&)
{
	using namespace GFStairTestsDetail;
	const FVector Hip(0.f, 0.f, 80.f);
	const FVector Knee(0.f, 30.f, 40.f);
	const FVector Ankle(0.f, 0.f, 0.f);
	const FVector Target(0.f, 0.f, 32.5f);
	FVector OutKnee = FVector::ZeroVector;
	FVector OutAnkle = FVector::ZeroVector;
	const bool bReached = GFFootIK::SolveTwoBone(Hip, Knee, Ankle, Target, OutKnee, OutAnkle);
	TestTrue(TEXT("A 20 cm step plus the sole offset is in reach"), bReached);
	TestTrue(TEXT("Upper bone length stays"), Near(FVector::Distance(Hip, OutKnee), FVector::Distance(Hip, Knee), 0.2f));
	TestTrue(TEXT("Lower bone length stays"), Near(FVector::Distance(OutKnee, OutAnkle), FVector::Distance(Knee, Ankle), 0.2f));
	TestTrue(TEXT("Ankle meets the sole target"), OutAnkle.Equals(Target, 0.5f));
	TestTrue(TEXT("Knee stays bent to the side"), OutKnee.Y > 10.f);

	FVector FarKnee = FVector::ZeroVector;
	FVector FarAnkle = FVector::ZeroVector;
	const bool bFar = GFFootIK::SolveTwoBone(FVector::ZeroVector, FVector(10.f, 0.f, 0.f), FVector(20.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f), FarKnee, FarAnkle);
	TestFalse(TEXT("A target past the leg is not a full plant"), bFar);
	TestTrue(TEXT("The ankle stops at max reach"), Near(FarAnkle.X, 19.5f, 0.2f));
	TestTrue(TEXT("The stretched upper bone keeps its length"), Near(FVector::Distance(FVector::ZeroVector, FarKnee), 10.f, 0.2f));

	const FQuat Aimed = GFFootIK::AlignPrimary(FQuat::Identity, FVector::ForwardVector, FVector::UpVector);
	TestTrue(TEXT("Align swings the bone axis onto the new direction"), Aimed.RotateVector(FVector::ForwardVector).Equals(FVector::UpVector, 0.02f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairClimbTest, "GFGame.Movement.StairClimb", GFStairTestsDetail::Flags)
bool FGFStairClimbTest::RunTest(const FString&)
{
	using namespace GFStairTestsDetail;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	GFInstallDinoStairHook();

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFStairClimb"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}

	TArray<AGFStairFlight*> Flights;
	for (const FGFStairFlightSpec& Spec : GFStairCatalog::HubFlights())
	{
		AGFStairFlight* Flight = GFStairCatalog::SpawnFlight(World, Spec);
		if (!TestNotNull(*FString::Printf(TEXT("Spawn %s"), *Spec.Id.ToString()), Flight))
		{
			return false;
		}
		Flights.Add(Flight);
	}
	TestEqual(TEXT("Hub spawn count"), Flights.Num(), 3);

	for (AGFStairFlight* Flight : Flights)
	{
		const TArray<UStaticMeshComponent*> Steps = Flight->GetStepMeshes();
		TestEqual(*FString::Printf(TEXT("%s step count"), *Flight->Spec.Id.ToString()), Steps.Num(), Flight->Spec.Steps);
		for (const UStaticMeshComponent* Step : Steps)
		{
			TestEqual(*FString::Printf(TEXT("%s step does not block"), *Flight->Spec.Id.ToString()), static_cast<int32>(Step->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
		}
		const UBoxComponent* Ramp = Flight->GetRamp();
		if (!TestNotNull(*FString::Printf(TEXT("%s ramp"), *Flight->Spec.Id.ToString()), Ramp))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s ramp blocks pawns"), *Flight->Spec.Id.ToString()), static_cast<int32>(Ramp->GetCollisionResponseToChannel(ECC_Pawn)), static_cast<int32>(ECR_Block));

		float Surface = -1.f;
		const FVector OverStep = Flight->GetActorTransform().TransformPosition(FVector(Flight->Spec.TreadCm * 0.5f, 0.f, 0.f));
		TestTrue(*FString::Printf(TEXT("%s reports the first tread"), *Flight->Spec.Id.ToString()), Flight->TreadTopZ(OverStep, Surface));
		TestTrue(*FString::Printf(TEXT("%s first tread is one riser up"), *Flight->Spec.Id.ToString()), Near(Surface, Flight->GetActorLocation().Z + Flight->Spec.RiserCm, 0.5f));
	}

	struct FWalker
	{
		UClass* Class = nullptr;
		bool bDino = false;
		const TCHAR* Label = TEXT("");
	};
	const FWalker Walkers[] = {
		{ AGFCharacter::StaticClass(), false, TEXT("player") },
		{ AGFDino::StaticClass(), true, TEXT("dino") },
	};

	for (const FWalker& Walker : Walkers)
	{
		for (AGFStairFlight* Flight : Flights)
		{
			const ACharacter* Defaults = Cast<ACharacter>(Walker.Class->GetDefaultObject());
			const float Half = Defaults && Defaults->GetCapsuleComponent() ? Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 88.f;
			const FRotator Rot(0.f, Flight->Spec.YawDeg, 0.f);
			ACharacter* Pawn = SpawnWalker(World, Walker.Class, Flight->ApproachLocation(Half, 0.f), Rot, Walker.bDino);
			if (!TestNotNull(*FString::Printf(TEXT("%s on %s"), Walker.Label, *Flight->Spec.Id.ToString()), Pawn))
			{
				return false;
			}
			UCharacterMovementComponent* Move = Pawn->GetCharacterMovement();
			TestTrue(*FString::Printf(TEXT("%s step height covers a block"), Walker.Label), Move && Move->MaxStepHeight >= 100.f);
			TestTrue(*FString::Printf(TEXT("%s floor angle is the tuning angle"), Walker.Label), Move && Near(Move->GetWalkableFloorAngle(), 50.f, 0.1f));

			// Run+50 puts a 40 cm radius capsule fully on the landing, which covers the run to run+100.
			const FWalkOut Up = Walk(Pawn, Flight, Flight->Spec.RunCm() + 50.f, true);
			TestTrue(*FString::Printf(TEXT("%s climbs %s (%s, along %.0f, feet %.0f)"), Walker.Label, *Flight->Spec.Id.ToString(), Up.Why, Up.Along, Up.FeetZ), Up.bOk);
			const float TopFeet = Flight->GetActorLocation().Z + Flight->Spec.RiseCm();
			TestTrue(*FString::Printf(TEXT("%s feet reach the landing of %s (%.0f vs %.0f)"), Walker.Label, *Flight->Spec.Id.ToString(), Up.FeetZ, TopFeet), Up.bOk && Near(Up.FeetZ, TopFeet, 30.f));

			const FWalkOut Down = Walk(Pawn, Flight, -40.f, false);
			TestTrue(*FString::Printf(TEXT("%s walks down %s (%s, along %.0f, feet %.0f)"), Walker.Label, *Flight->Spec.Id.ToString(), Down.Why, Down.Along, Down.FeetZ), Down.bOk);
			TestTrue(*FString::Printf(TEXT("%s feet return to the pad of %s"), Walker.Label, *Flight->Spec.Id.ToString()), Down.bOk && Near(Down.FeetZ, Flight->GetActorLocation().Z, 30.f));
			Pawn->Destroy();
		}
	}
	return true;
}

#endif
