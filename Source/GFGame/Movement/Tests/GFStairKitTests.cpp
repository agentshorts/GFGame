// #341: the player walks up box-collision stair kits on the world-map movement path (stamina on).
// Town and voxel stairs are UCX boxes (BlockAll), not the hub's ramp flights, so these kits are boxes too:
// risers on the 6.25 cm voxel grid, the Sovereign Court front steps, and one-block (100 cm) Stairwell steps.
// Each kit is walked with the engine step alone (reported, not asserted) and with the riser assist (asserted).
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Movement.StairKit;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFStairCollision.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFStairKitTestsDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	constexpr float Dt = 1.f / 60.f;
	constexpr float KitWidthCm = 300.f;
	constexpr float ApproachCm = 150.f;
	constexpr float KitSpacingCm = 1500.f;
	constexpr const TCHAR* CourtMeshPath = TEXT("/Game/Art/Town/Rime/SM_Town_Rime_SovereignCourt.SM_Town_Rime_SovereignCourt");

	struct FKitStep
	{
		float TopCm = 0.f;
		float DepthCm = 0.f;
	};

	struct FKit
	{
		const TCHAR* Id = TEXT("");
		TArray<FKitStep> Steps;

		float RunCm() const
		{
			float Run = 0.f;
			for (const FKitStep& Step : Steps)
			{
				Run += Step.DepthCm;
			}
			return Run;
		}
		float TopCm() const { return Steps.Num() ? Steps.Last().TopCm : 0.f; }
		float LastDepthCm() const { return Steps.Num() ? Steps.Last().DepthCm : 0.f; }
	};

	/** Box kits. Heights are cm above the ground, each step a solid box from the ground up, like the town UCX boxes. */
	TArray<FKit> Kits()
	{
		TArray<FKit> Out;
		// Sovereign Court UCX: step box 0..50 (150 deep), terrace 0..75, dais 75..125.
		Out.Add({ TEXT("CourtFront"), { { 50.f, 150.f }, { 75.f, 350.f }, { 125.f, 250.f } } });
		// Every riser on the 6.25 cm voxel grid from one tick up to three quarters of a block.
		Out.Add({ TEXT("VoxelGrid"), { { 6.25f, 100.f }, { 18.75f, 100.f }, { 43.75f, 100.f }, { 81.25f, 100.f }, { 131.25f, 100.f }, { 206.25f, 150.f } } });
		// A tight kit flight: 12.5 cm risers on 37.5 cm treads.
		{
			FKit Tight{ TEXT("TightFlight"), {} };
			for (int32 Index = 1; Index <= 8; ++Index)
			{
				Tight.Steps.Add({ 12.5f * Index, Index == 8 ? 150.f : 37.5f });
			}
			Out.Add(Tight);
		}
		// House stoop: 25 cm plinth, then another 25.
		Out.Add({ TEXT("Stoop"), { { 25.f, 60.f }, { 50.f, 150.f } } });
		// Stairwell Call and voxel terrain: whole 100 cm blocks.
		Out.Add({ TEXT("BlockStairs"), { { 100.f, 100.f }, { 200.f, 100.f }, { 300.f, 150.f } } });
		return Out;
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

	UWorld* MakeWorld(const TCHAR* Name)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
		if (!World)
		{
			return nullptr;
		}
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		if (!World->HasBegunPlay() && World->GetWorldSettings())
		{
			World->GetWorldSettings()->NotifyBeginPlay();
		}
		return World;
	}

	/** One BlockAll box from Min to Max, the same collision a UCX box imports as. */
	AActor* SpawnBox(UWorld* World, const FVector& Min, const FVector& Max, const FString& Label)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Block = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		if (!Block)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Block, *Label);
		Box->SetBoxExtent((Max - Min) * 0.5f, false);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Static);
		Block->SetRootComponent(Box);
		Box->SetWorldLocation((Min + Max) * 0.5f);
		Box->RegisterComponent();
		Block->AddInstanceComponent(Box);
		return Block;
	}

	/** Ground slab with its top at Z 0 (the town slabs), then the kit steps going +X from Origin. */
	void SpawnKit(UWorld* World, const FKit& Kit, const FVector& Origin)
	{
		const float Half = KitWidthCm * 0.5f;
		SpawnBox(World, Origin + FVector(-ApproachCm - 300.f, -Half - 300.f, -20.f), Origin + FVector(Kit.RunCm() + 300.f, Half + 300.f, 0.f),
			FString::Printf(TEXT("%s_Ground"), Kit.Id));
		float X = 0.f;
		int32 Index = 0;
		for (const FKitStep& Step : Kit.Steps)
		{
			SpawnBox(World, Origin + FVector(X, -Half, 0.f), Origin + FVector(X + Step.DepthCm, Half, Step.TopCm),
				FString::Printf(TEXT("%s_Step%d"), Kit.Id, Index++));
			X += Step.DepthCm;
		}
	}

	AGFCharacter* SpawnPlayer(UWorld* World, const FVector& Feet, const FRotator& Facing)
	{
		const AGFCharacter* Defaults = GetDefault<AGFCharacter>();
		const float Half = Defaults->GetCapsuleComponent() ? Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 88.f;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFCharacter* Pawn = World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), FTransform(Facing, Feet + FVector(0.f, 0.f, Half + 2.f)), Params);
		if (!Pawn)
		{
			return nullptr;
		}
		Pawn->SetActorTickEnabled(false);
		if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
		{
			// A pawn with no controller never runs Restart, so the mode stays None and nothing walks.
			Move->bRunPhysicsWithNoController = true;
			Move->SetComponentTickEnabled(true);
			if (Move->MovementMode == MOVE_None)
			{
				Move->SetDefaultMovementMode();
			}
			Move->StopMovementImmediately();
		}
		return Pawn;
	}

	float FeetOf(const ACharacter* Pawn)
	{
		const float Half = Pawn->GetCapsuleComponent() ? Pawn->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 0.f;
		return Pawn->GetActorLocation().Z - Half;
	}

	struct FClimb
	{
		bool bReached = false;
		float Along = 0.f;
		float FeetZ = 0.f;
		float MaxFeetZ = 0.f;
		int32 Assists = 0;
		int32 Refusals = 0;
		int32 Mode = 0;
		const TCHAR* Why = TEXT("not run");
	};

	/** Holds forward along Dir until the pawn is GoalAlong past Origin, stalls for 0.4 s, falls, or 8 s pass. */
	FClimb Climb(AGFCharacter* Pawn, const FVector& Origin, const FVector& Dir, float GoalAlong, bool bSprint)
	{
		FClimb Out;
		UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			Out.Why = TEXT("no GF movement");
			return Out;
		}
		for (int32 Index = 0; Index < 30; ++Index)
		{
			Move->TickComponent(Dt, LEVELTICK_All, nullptr); // settle on the ground
		}
		Move->StopMovementImmediately();
		const int32 AssistsBefore = Move->GetRiserAssistCount();
		const int32 RefusalsBefore = Move->GetStairRefusalCount();
		Move->SetSprintHeld(bSprint);
		float Previous = FVector::DotProduct(Pawn->GetActorLocation() - Origin, Dir);
		Out.MaxFeetZ = FeetOf(Pawn);
		int32 Stall = 0;
		for (int32 Index = 0; Index < 480; ++Index)
		{
			Pawn->AddMovementInput(Dir, 1.f);
			Move->TickComponent(Dt, LEVELTICK_All, nullptr);
			Out.Along = FVector::DotProduct(Pawn->GetActorLocation() - Origin, Dir);
			Out.FeetZ = FeetOf(Pawn);
			Out.MaxFeetZ = FMath::Max(Out.MaxFeetZ, Out.FeetZ);
			Out.Mode = static_cast<int32>(Move->MovementMode);
			if (Out.FeetZ < Origin.Z - 60.f)
			{
				Out.Why = TEXT("fell through");
				break;
			}
			if (Out.Along >= GoalAlong)
			{
				Out.bReached = true;
				Out.Why = TEXT("reached");
				break;
			}
			Stall = Out.Along - Previous < 1.f ? Stall + 1 : 0;
			Previous = Out.Along;
			if (Stall >= 24)
			{
				Out.Why = TEXT("stalled at a riser");
				break;
			}
		}
		if (!Out.bReached && FCString::Strcmp(Out.Why, TEXT("not run")) == 0)
		{
			Out.Why = TEXT("timed out");
		}
		Move->SetSprintHeld(false);
		Move->StopMovementImmediately();
		Out.Assists = Move->GetRiserAssistCount() - AssistsBefore;
		Out.Refusals = Move->GetStairRefusalCount() - RefusalsBefore;
		return Out;
	}

	FString Describe(const FClimb& C)
	{
		return FString::Printf(TEXT("%s, along %.0f, feet %.1f (max %.1f), mode %d, assists %d, refused walking hits %d"),
			C.Why, C.Along, C.FeetZ, C.MaxFeetZ, C.Mode, C.Assists, C.Refusals);
	}

	/** Where the court's front step is, in world space, and how the probe found it (for the #341 evidence line). */
	struct FCourtStep
	{
		bool bFound = false;
		FVector Axis = FVector::ZeroVector; // unit world axis from the court out through the step (one of +-X, +-Y)
		FVector Centre = FVector::ZeroVector; // step box centre, world
		float TopCm = 0.f;
		float OuterCm = 0.f; // outer face distance from the court pivot along Axis
		float InnerCm = 0.f; // inner face (terrace side) distance along Axis
		FString How;
	};

	FVector SnapToAxis(const FVector& Planar)
	{
		return FMath::Abs(Planar.X) >= FMath::Abs(Planar.Y) ? FVector(FMath::Sign(Planar.X), 0.f, 0.f) : FVector(0.f, FMath::Sign(Planar.Y), 0.f);
	}

	/**
	 * Reads the imported UCX boxes (BodySetup AggGeom) instead of guessing where Blender -Y landed. Static meshes import
	 * with Force Front X, so the court front comes in on +X, not on +-Y where the first probe looked. The front step is
	 * the ground-standing element with a walkable top (within one step) whose centre is farthest out from the mesh
	 * bounds centre. No AggGeom match falls back to a down-trace band over all four axes.
	 */
	FCourtStep FindCourtStep(UWorld* World, const UStaticMeshComponent* Component, float MaxStepCm)
	{
		FCourtStep Out;
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		if (!World || !Body)
		{
			return Out;
		}
		const FTransform ToWorld = Component->GetComponentTransform();
		const FVector Pivot = ToWorld.GetLocation();
		const float GroundZ = static_cast<float>(Pivot.Z);
		const FVector BoundsCentre = Mesh->GetBoundingBox().TransformBy(ToWorld).GetCenter();

		struct FElem
		{
			FBox Box;
			FString Name;
		};
		TArray<FElem> Elems;
		for (int32 Index = 0; Index < Body->AggGeom.ConvexElems.Num(); ++Index)
		{
			const FKConvexElem& Convex = Body->AggGeom.ConvexElems[Index];
			Elems.Add({ Convex.ElemBox.TransformBy(Convex.GetTransform()).TransformBy(ToWorld), FString::Printf(TEXT("convex %d"), Index) });
		}
		for (int32 Index = 0; Index < Body->AggGeom.BoxElems.Num(); ++Index)
		{
			Elems.Add({ Body->AggGeom.BoxElems[Index].CalcAABB(FTransform::Identity, 1.f).TransformBy(ToWorld), FString::Printf(TEXT("box %d"), Index) });
		}

		float BestOut = -1.f;
		for (const FElem& Elem : Elems)
		{
			const float Top = static_cast<float>(Elem.Box.Max.Z) - GroundZ;
			if (!Elem.Box.IsValid || Elem.Box.Min.Z - GroundZ > 10.f || Top <= 10.f || Top > MaxStepCm)
			{
				continue;
			}
			const FVector Planar(Elem.Box.GetCenter().X - BoundsCentre.X, Elem.Box.GetCenter().Y - BoundsCentre.Y, 0.f);
			const float OutBy = static_cast<float>(Planar.Size());
			if (OutBy <= BestOut || Planar.IsNearlyZero())
			{
				continue;
			}
			BestOut = OutBy;
			Out.bFound = true;
			Out.Axis = SnapToAxis(Planar);
			Out.Centre = Elem.Box.GetCenter();
			Out.TopCm = Top;
			const float A = static_cast<float>(FVector::DotProduct(Elem.Box.Min - Pivot, Out.Axis));
			const float B = static_cast<float>(FVector::DotProduct(Elem.Box.Max - Pivot, Out.Axis));
			Out.OuterCm = FMath::Max(A, B);
			Out.InnerCm = FMath::Min(A, B);
			Out.How = FString::Printf(TEXT("AggGeom %s of %d elements"), *Elem.Name, Elems.Num());
		}

		if (!Out.bFound)
		{
			// Fallback: down-traces 600 to 1000 cm out on each axis, first hit that stands one step proud of the ground.
			const FVector Axes[] = { FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f) };
			for (const FVector& Axis : Axes)
			{
				for (float Dist = 1000.f; Dist >= 600.f && !Out.bFound; Dist -= 12.5f)
				{
					const FVector Probe = Pivot + Axis * Dist + FVector(0.f, 0.f, 400.f);
					FHitResult Hit;
					if (World->LineTraceSingleByChannel(Hit, Probe, Probe - FVector(0.f, 0.f, 500.f), ECC_Pawn) && Hit.GetComponent() == Component)
					{
						const float Top = static_cast<float>(Hit.ImpactPoint.Z) - GroundZ;
						if (Top > 10.f && Top <= MaxStepCm)
						{
							Out.bFound = true;
							Out.Axis = Axis;
							Out.Centre = Pivot + Axis * (Dist - 75.f);
							Out.TopCm = Top;
							Out.OuterCm = Dist;
							Out.InnerCm = Dist - 150.f;
							Out.How = FString::Printf(TEXT("fallback down-trace band (AggGeom had %d elements, none matched)"), Elems.Num());
						}
					}
				}
				if (Out.bFound)
				{
					break;
				}
			}
		}

		if (Out.bFound)
		{
			// Sanity check: the collision really is there where the probe says.
			FHitResult Hit;
			const FVector Probe(Out.Centre.X, Out.Centre.Y, GroundZ + 400.f);
			if (!World->LineTraceSingleByChannel(Hit, Probe, Probe - FVector(0.f, 0.f, 500.f), ECC_Pawn)
				|| !FMath::IsNearlyEqual(static_cast<float>(Hit.ImpactPoint.Z) - GroundZ, Out.TopCm, 3.f))
			{
				Out.How += TEXT(", but the down-trace at its centre disagrees");
				Out.bFound = false;
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairKitWalkTest, "GFGame.Movement.StairKit.Walk", GFStairKitTestsDetail::Flags)
bool FGFStairKitWalkTest::RunTest(const FString&)
{
	using namespace GFStairKitTestsDetail;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UWorld* World = MakeWorld(TEXT("GFStairKitWalk"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	const TArray<FKit> AllKits = Kits();
	for (int32 KitIndex = 0; KitIndex < AllKits.Num(); ++KitIndex)
	{
		const FKit& Kit = AllKits[KitIndex];
		const FVector Origin(0.f, KitIndex * KitSpacingCm, 0.f);
		SpawnKit(World, Kit, Origin);
		// Fully on the last tread: half its depth in, so the 40 cm radius capsule is clear of the lip.
		const float Goal = Kit.RunCm() - Kit.LastDepthCm() * 0.5f;

		for (const bool bSprint : { false, true })
		{
			for (const bool bAssist : { false, true })
			{
				AGFCharacter* Pawn = SpawnPlayer(World, Origin + FVector(-ApproachCm, 0.f, 0.f), FRotator::ZeroRotator);
				UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
				if (!TestNotNull(*FString::Printf(TEXT("%s player"), Kit.Id), Move))
				{
					return false;
				}
				TestTrue(*FString::Printf(TEXT("%s runs the world-map movement (stamina on)"), Kit.Id), Move->IsStaminaEnabled());
				TestTrue(*FString::Printf(TEXT("%s step height covers a block"), Kit.Id), Move->MaxStepHeight >= 100.f);
				Move->bRiserAssist = bAssist;

				const FClimb Result = Climb(Pawn, Origin, FVector::ForwardVector, Goal, bSprint);
				const FString Label = FString::Printf(TEXT("%s %s, %s"), Kit.Id, bSprint ? TEXT("sprint") : TEXT("walk"), bAssist ? TEXT("riser assist") : TEXT("engine step only"));
				if (bAssist)
				{
					TestTrue(*FString::Printf(TEXT("%s climbs (%s)"), *Label, *Describe(Result)), Result.bReached);
					TestTrue(*FString::Printf(TEXT("%s feet on the top tread (%.1f vs %.1f)"), *Label, Result.FeetZ, Kit.TopCm()),
						Result.bReached && FMath::IsNearlyEqual(Result.FeetZ, Kit.TopCm(), 15.f));
				}
				else
				{
					// The engine-only run is the #341 evidence: which kits the stock StepUp refuses on its own.
					AddInfo(FString::Printf(TEXT("#341 evidence: %s: %s"), *Label, *Describe(Result)));
					TestEqual(*FString::Printf(TEXT("%s with the assist off never assists"), *Label), Result.Assists, 0);
				}
				Pawn->Destroy();
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairKitWallTest, "GFGame.Movement.StairKit.WallStaysAWall", GFStairKitTestsDetail::Flags)
bool FGFStairKitWallTest::RunTest(const FString&)
{
	using namespace GFStairKitTestsDetail;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UWorld* World = MakeWorld(TEXT("GFStairKitWall"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	// 112.5 cm is past the 105 cm step: a wall, assist or not. A 2 m wall is plainly a wall.
	const FKit Ledge{ TEXT("Ledge"), { { 112.5f, 300.f } } };
	const FKit Wall{ TEXT("Wall"), { { 200.f, 300.f } } };
	const FKit Walls[] = { Ledge, Wall };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FKit& Kit = Walls[Index];
		const FVector Origin(0.f, Index * KitSpacingCm, 0.f);
		SpawnKit(World, Kit, Origin);
		AGFCharacter* Pawn = SpawnPlayer(World, Origin + FVector(-ApproachCm, 0.f, 0.f), FRotator::ZeroRotator);
		UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s player"), Kit.Id), Move))
		{
			return false;
		}
		Move->bRiserAssist = true;
		const FClimb Result = Climb(Pawn, Origin, FVector::ForwardVector, Kit.RunCm() * 0.5f, false);
		TestFalse(*FString::Printf(TEXT("%s is not walked up (%s)"), Kit.Id, *Describe(Result)), Result.bReached);
		TestTrue(*FString::Printf(TEXT("%s feet stay on the ground"), Kit.Id), Result.MaxFeetZ < 10.f);
		TestEqual(*FString::Printf(TEXT("%s gets no assist"), Kit.Id), Result.Assists, 0);
		// The #341 map diagnostic: every refused blocking hit while walking is logged, the 2 m wall too, so a playtest
		// names whatever stopped the player. The line measures the face it hit.
		TestTrue(*FString::Printf(TEXT("%s logs a StairRefused line (%s)"), Kit.Id, *Describe(Result)), Result.Refusals > 0);
		const FGFWalkRefusal& Line = Move->GetLastWalkRefusal();
		AddInfo(FString::Printf(TEXT("#341 evidence: %s StairRefused: %s, face top %.1f cm, component %s, normal %s"),
			Kit.Id, *Line.Reason, Line.FaceTopCm, *Line.Component, *Line.ImpactNormal.ToCompactString()));
		TestTrue(*FString::Printf(TEXT("%s line names the step box (%s)"), Kit.Id, *Line.Component), Line.Component.Contains(TEXT("_Step0")));
		TestTrue(*FString::Printf(TEXT("%s line measures the face top (%.1f vs %.1f)"), Kit.Id, Line.FaceTopCm, Kit.TopCm()),
			FMath::IsNearlyEqual(Line.FaceTopCm, Kit.TopCm(), 3.f));
		TestTrue(*FString::Printf(TEXT("%s face top is walkable"), Kit.Id), Line.bFaceTopWalkable);
		TestTrue(*FString::Printf(TEXT("%s line has a reason"), Kit.Id), !Line.Reason.IsEmpty());
		Pawn->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairKitCourtTest, "GFGame.Movement.StairKit.SovereignCourt", GFStairKitTestsDetail::Flags)
bool FGFStairKitCourtTest::RunTest(const FString&)
{
	using namespace GFStairKitTestsDetail;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, CourtMeshPath, nullptr, LOAD_NoWarn);
	if (!Mesh)
	{
		AddWarning(FString::Printf(TEXT("%s did not load, so the imported stair kit was not walked"), CourtMeshPath));
		return true;
	}
	UWorld* World = MakeWorld(TEXT("GFStairKitCourt"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	SpawnBox(World, FVector(-2000.f, -2000.f, -20.f), FVector(2000.f, 2000.f, 0.f), TEXT("CourtGround"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Court = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Court actor"), Court))
	{
		return false;
	}
	Court->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Court->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	Court->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	TestTrue(TEXT("Court mesh has simple collision (the UCX boxes)"), Mesh->GetBodySetup() && Mesh->GetBodySetup()->AggGeom.GetElementCount() > 0);

	const float MaxStep = GetDefault<AGFCharacter>()->PlayerMaxStepHeight;
	const FCourtStep Step = FindCourtStep(World, Court->GetStaticMeshComponent(), MaxStep);
	const FString Probe = FString::Printf(TEXT("%s, axis %s%s, step centre %s, top %.1f cm, faces %.0f to %.0f cm out"),
		*Step.How, Step.Axis.X != 0.f ? TEXT("X") : TEXT("Y"), (Step.Axis.X + Step.Axis.Y) > 0.f ? TEXT("+") : TEXT("-"),
		*Step.Centre.ToCompactString(), Step.TopCm, Step.InnerCm, Step.OuterCm);
	AddInfo(FString::Printf(TEXT("#341 evidence: SovereignCourt probe: %s"), *Probe));
	if (!TestTrue(*FString::Printf(TEXT("Found the court front step (%s)"), *Probe), Step.bFound))
	{
		return false;
	}
	// Authored as a 50 cm step box then the 75 cm terrace. Scale both by the step the probe found, so a modest import
	// scale still checks the same climb.
	const float Scale = Step.TopCm / 50.f;
	TestTrue(*FString::Printf(TEXT("Front step is about 50 cm at scale 1 (%.1f cm)"), Step.TopCm), Scale > 0.75f && Scale < 1.5f);
	const float Depth = Step.OuterCm - Step.InnerCm;
	const FVector Pivot = Court->GetActorLocation();
	const FVector Lateral = FVector(Step.Centre.X - Pivot.X, Step.Centre.Y - Pivot.Y, 0.f) - Step.Axis * FVector::DotProduct(Step.Centre - Pivot, Step.Axis);
	const FVector Dir = -Step.Axis;
	const FVector Start = FVector(Pivot.X, Pivot.Y, 0.f) + Lateral + Step.Axis * (Step.OuterCm + ApproachCm);
	// 1.2 step depths past the inner face is on the terrace and short of the dais (180 cm at scale 1: 520 cm out).
	const float GoalOut = Step.InnerCm - Depth * 1.2f;
	const float GoalAlong = Step.OuterCm + ApproachCm - GoalOut;
	FHitResult TerraceHit;
	const FVector GoalAt = Start + Dir * GoalAlong;
	const float TerraceTop = World->LineTraceSingleByChannel(TerraceHit, GoalAt + FVector(0.f, 0.f, 400.f), GoalAt - FVector(0.f, 0.f, 100.f), ECC_Pawn)
		? static_cast<float>(TerraceHit.ImpactPoint.Z) : -1.f;
	TestTrue(*FString::Printf(TEXT("Goal is on the 75 cm terrace (%.1f vs %.1f)"), TerraceTop, 75.f * Scale), FMath::IsNearlyEqual(TerraceTop, 75.f * Scale, 10.f));

	for (const bool bAssist : { false, true })
	{
		AGFCharacter* Pawn = SpawnPlayer(World, Start, Dir.Rotation());
		UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
		if (!TestNotNull(TEXT("Court player"), Move))
		{
			return false;
		}
		Move->bRiserAssist = bAssist;
		const FClimb Result = Climb(Pawn, Start, Dir, GoalAlong, false);
		if (bAssist)
		{
			TestTrue(*FString::Printf(TEXT("Court front steps climbed (%s)"), *Describe(Result)), Result.bReached);
			TestTrue(*FString::Printf(TEXT("Feet on the terrace (%.1f vs %.1f)"), Result.FeetZ, TerraceTop),
				Result.bReached && FMath::IsNearlyEqual(Result.FeetZ, TerraceTop, 15.f));
		}
		else
		{
			AddInfo(FString::Printf(TEXT("#341 evidence: SovereignCourt, engine step only, walked %.0f cm along %s: %s"),
				GoalAlong, *Dir.ToCompactString(), *Describe(Result)));
		}
		Pawn->Destroy();
	}
	return true;
}

namespace GFStairKitTestsDetail
{
	constexpr const TCHAR* ControlCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr float TownSpacingCm = 4000.f;
	/** The Board Hall side plinth and door threshold are 25 cm treads in the source FBX (Data/Town/stair_collision_341_v1.json). */
	constexpr float BoardHallPlinthCm = 25.f;

	struct FTownFix
	{
		FString Name;
		FString Path;
	};

	/** The #341 fix list, shared with Tools/fix_stair_collision_341.py. */
	bool LoadTownFixes(TArray<FTownFix>& Out, FString& OutError)
	{
		const FString File = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Town/stair_collision_341_v1.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			OutError = FString::Printf(TEXT("could not read %s"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not json"), *File);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Fixes = nullptr;
		if (!Root->TryGetArrayField(TEXT("fix"), Fixes) || !Fixes)
		{
			OutError = FString::Printf(TEXT("%s has no fix list"), *File);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Fixes)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (Value.IsValid() && Value->TryGetObject(Entry) && Entry && Entry->IsValid())
			{
				FTownFix Fix;
				(*Entry)->TryGetStringField(TEXT("name"), Fix.Name);
				(*Entry)->TryGetStringField(TEXT("path"), Fix.Path);
				if (!Fix.Name.IsEmpty() && !Fix.Path.IsEmpty())
				{
					Out.Add(Fix);
				}
			}
		}
		return Out.Num() > 0;
	}

	AStaticMeshActor* SpawnMesh(UWorld* World, UStaticMesh* Mesh, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(At), Params);
		if (!Actor)
		{
			return nullptr;
		}
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		return Actor;
	}

	FString HeightsOf(const TArray<float>& Heights)
	{
		TArray<float> Unique;
		for (const float Height : Heights)
		{
			Unique.AddUnique(FMath::RoundToFloat(Height * 10.f) / 10.f);
		}
		Unique.Sort();
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Unique.Num() && Index < 8; ++Index)
		{
			Parts.Add(FString::Printf(TEXT("%.1f"), Unique[Index]));
		}
		return FString::Join(Parts, TEXT(", "));
	}
}

/**
 * #341: the town meshes on the fix list must not hide a tread inside their collision. Fails until
 * Tools/fix_stair_collision_341.py (GFSTAIR_MODE=apply) has switched them to complex as simple; the failure lists the
 * buried tread heights, which is also the proof that the probe sees the old hull. An engine cube is the clean control.
 * The gate is: complex as simple on and FindBuriedTreads == 0 (it returns 0 for complex as simple, -1 when it cannot
 * measure, and -1 always fails here). Then walks the Board Hall from its +X side onto the 25 cm side plinth: on the
 * old hull a stall must write a StairRefused line; on complex as simple the pawn must get onto the plinth, and where it
 * stops after that (the hall wall) is reported, not asserted.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStairKitTownCollisionTest, "GFGame.Movement.StairKit.TownCollision", GFStairKitTestsDetail::Flags)
bool FGFStairKitTownCollisionTest::RunTest(const FString&)
{
	using namespace GFStairKitTestsDetail;
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}
	TArray<FTownFix> Fixes;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("Read the #341 fix list (%s)"), *Error), LoadTownFixes(Fixes, Error)))
	{
		return false;
	}
	UWorld* World = MakeWorld(TEXT("GFStairKitTown"));
	FWorldScope Scope(World);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}
	const float MaxStep = GetDefault<AGFCharacter>()->PlayerMaxStepHeight;
	const float Reach = (Fixes.Num() + 1) * TownSpacingCm;
	SpawnBox(World, FVector(-TownSpacingCm, -TownSpacingCm, -20.f), FVector(Reach, TownSpacingCm, 0.f), TEXT("TownGround"));

	// Control: a cube's box collision is its render mesh, so its top face is not buried. The cube keeps its default
	// trace flag, so this also proves the overlap-vs-air test (not the complex as simple shortcut) reads 0 on a clean mesh.
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, ControlCubePath, nullptr, LOAD_NoWarn))
	{
		AStaticMeshActor* Control = SpawnMesh(World, Cube, FVector(0.f, 0.f, 50.f));
		TArray<FVector> Points;
		TArray<float> Heights;
		const int32 Buried = Control ? UGFStairCollisionLibrary::FindBuriedTreads(Control->GetStaticMeshComponent(), MaxStep, Points, Heights) : -1;
		const FString Blocker = Control ? UGFStairCollisionLibrary::DescribeTreadAuditBlocker(Control->GetStaticMeshComponent()) : FString(TEXT("no actor"));
		TestFalse(TEXT("Control cube is on its default trace flag"), UGFStairCollisionLibrary::UsesComplexAsSimple(Cube));
		TestEqual(*FString::Printf(TEXT("Control cube has no buried tread (-1 means it cannot measure: %s)"), *Blocker), Buried, 0);
	}
	else
	{
		AddWarning(FString::Printf(TEXT("%s did not load, so the clean control was not run"), ControlCubePath));
	}

	AStaticMeshActor* BoardHall = nullptr;
	for (int32 Index = 0; Index < Fixes.Num(); ++Index)
	{
		const FTownFix& Fix = Fixes[Index];
		const FString ObjectPath = Fix.Path + TEXT(".") + Fix.Name;
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
		if (!Mesh)
		{
			AddWarning(FString::Printf(TEXT("%s did not load, so its collision was not checked"), *ObjectPath));
			continue;
		}
		AStaticMeshActor* Actor = SpawnMesh(World, Mesh, FVector((Index + 1) * TownSpacingCm, 0.f, 0.f));
		if (!TestNotNull(*FString::Printf(TEXT("%s actor"), *Fix.Name), Actor))
		{
			continue;
		}
		TArray<FVector> Points;
		TArray<float> Heights;
		const int32 Buried = UGFStairCollisionLibrary::FindBuriedTreads(Actor->GetStaticMeshComponent(), MaxStep, Points, Heights);
		const FString Blocker = UGFStairCollisionLibrary::DescribeTreadAuditBlocker(Actor->GetStaticMeshComponent());
		AddInfo(FString::Printf(TEXT("#341 evidence: %s: %d buried tread samples at %s cm (triangles from %s%s%s); %s"),
			*Fix.Name, Buried, *HeightsOf(Heights), *UGFStairCollisionLibrary::DescribeTreadSource(Actor->GetStaticMeshComponent()),
			Blocker.IsEmpty() ? TEXT("") : TEXT(", cannot measure: "), *Blocker, *UGFStairCollisionLibrary::DescribeSimpleCollision(Mesh)));
		TestTrue(*FString::Printf(TEXT("%s uses its render mesh as collision (run Tools/fix_stair_collision_341.py with GFSTAIR_MODE=apply)"), *Fix.Name),
			UGFStairCollisionLibrary::UsesComplexAsSimple(Mesh));
		TestEqual(*FString::Printf(TEXT("%s hides no tread inside its collision (heights %s cm)"), *Fix.Name, *HeightsOf(Heights)), Buried, 0);
		if (Fix.Name.Contains(TEXT("BoardHall")))
		{
			BoardHall = Actor;
		}
	}

	// Walk at the Board Hall +X side, across the 25 cm side plinth toward the hall's middle, so the pawn always ends on
	// the hall wall. On the old hull that wall stands at the plinth lip and a stall must write StairRefused. On complex
	// as simple the plinth is a real step: the pawn must get onto it, and the stall at the wall behind it is expected
	// and silent (a wall is not a refused stair), so it is reported, not asserted.
	if (BoardHall)
	{
		const FBox Bounds = BoardHall->GetStaticMeshComponent()->Bounds.GetBox();
		const FVector Start(Bounds.Max.X + ApproachCm, Bounds.GetCenter().Y, 0.f);
		const FVector Dir = -FVector::ForwardVector;
		AGFCharacter* Pawn = SpawnPlayer(World, Start, Dir.Rotation());
		UGFCharacterMovementComponent* Move = Pawn ? Cast<UGFCharacterMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
		if (TestNotNull(TEXT("Board Hall player"), Move))
		{
			const FClimb Result = Climb(Pawn, Start, Dir, ApproachCm + Bounds.GetSize().X * 0.5f, false);
			const FGFWalkRefusal& Line = Move->GetLastWalkRefusal();
			AddInfo(FString::Printf(TEXT("#341 evidence: Board Hall front walk: %s; last StairRefused: %s, face top %.1f cm, component %s, at %s"),
				*Describe(Result), *Line.Reason, Line.FaceTopCm, *Line.Component, *Line.ImpactPoint.ToCompactString()));
			const bool bComplex = UGFStairCollisionLibrary::UsesComplexAsSimple(BoardHall->GetStaticMeshComponent()->GetStaticMesh());
			if (bComplex)
			{
				TestTrue(*FString::Printf(TEXT("Board Hall on complex as simple: the pawn steps onto the side plinth (%s)"), *Describe(Result)),
					Result.MaxFeetZ >= BoardHallPlinthCm * 0.5f);
				if (!Result.bReached)
				{
					AddInfo(FString::Printf(TEXT("#341 evidence: Board Hall walk stopped at the hall wall after the plinth (along %.0f, feet %.1f, max %.1f, refused %d)"),
						Result.Along, Result.FeetZ, Result.MaxFeetZ, Result.Refusals));
				}
			}
			else if (!Result.bReached)
			{
				TestTrue(TEXT("A stalled walk at the Board Hall wrote a StairRefused line"), Result.Refusals > 0 && !Line.Reason.IsEmpty());
			}
			Pawn->Destroy();
		}
	}
	return true;
}

#endif
