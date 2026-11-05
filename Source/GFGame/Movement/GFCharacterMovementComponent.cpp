#include "GFCharacterMovementComponent.h"
#include "GFBeatHopCourse.h"
#include "GFAccelHopTile.h"
#include "GFAccelHopRoad.h"
#include "GFBeatClockSubsystem.h"
#include "GFCharacter.h"
#include "GFKindleRing.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFStairMovement.h"
#include "GFMovementWorldSubsystem.h"
#include "GFStaminaMode.h"
#include "GFLog.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace GFCharacterMovementDetail
{
	FString GFMovementMapLabelForPolicy(const UWorld* World)
	{
		if (!World)
		{
			return FString();
		}
		const FString MapName = World->GetMapName();
		if (GFMovementPolicy::IsMiniGameMapName(MapName))
		{
			return MapName;
		}
		if (const UPackage* Package = World->GetOutermost())
		{
			return Package->GetName();
		}
		return MapName;
	}

	void GFMovementReadOptIn(const UWorld* World, bool& bOpted, bool& bValue)
	{
		bOpted = false;
		bValue = false;
		if (!World)
		{
			return;
		}
		if (const UGFMovementWorldSubsystem* Subsystem = UGFMovementWorldSubsystem::Get(World))
		{
			if (Subsystem->HasStaminaOptIn())
			{
				bOpted = true;
				bValue = Subsystem->WantsStamina();
				return;
			}
		}
		if (const AGameModeBase* Mode = World->GetAuthGameMode())
		{
			if (const IGFStaminaMode* Choice = Cast<IGFStaminaMode>(Mode))
			{
				bOpted = true;
				bValue = Choice->WantsPlayerStamina();
			}
		}
	}

	bool GFMovementCapsuleOverlapsSphere(const ACharacter* Character, const FVector& SphereCenter, float SphereRadius)
	{
		if (!Character)
		{
			return false;
		}
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float CapsuleRadius = Capsule ? Capsule->GetScaledCapsuleRadius() : 0.f;
		const float CapsuleHalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		const double SegmentHalf = static_cast<double>(FMath::Max(0.f, CapsuleHalfHeight - CapsuleRadius));
		const FVector Center = Character->GetActorLocation();
		const FVector SegmentStart = Center - FVector(0.0, 0.0, SegmentHalf);
		const FVector SegmentEnd = Center + FVector(0.0, 0.0, SegmentHalf);
		const FVector Closest = FMath::ClosestPointOnSegment(SphereCenter, SegmentStart, SegmentEnd);
		const double Limit = static_cast<double>(FMath::Max(0.f, SphereRadius)) + static_cast<double>(FMath::Max(0.f, CapsuleRadius));
		return FVector::DistSquared(Closest, SphereCenter) <= Limit * Limit;
	}
}

class FSavedMove_GFCharacter : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	bool bSavedSprint = false;
	bool bSavedDash = false;
	bool bSavedSwing = false;
	bool bSavedYank = false;
	bool bSavedJump = false;
	FGFStaminaState StartSim;
	FGFTraversalContact SavedContact;
	FVector SavedCameraForward = FVector::ForwardVector;

	virtual void Clear() override
	{
		Super::Clear();
		bSavedSprint = false;
		bSavedDash = false;
		bSavedSwing = false;
		bSavedYank = false;
		bSavedJump = false;
		StartSim = FGFStaminaState();
		SavedContact = FGFTraversalContact();
		SavedCameraForward = FVector::ForwardVector;
	}

	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
		UGFCharacterMovementComponent* Move = C ? Cast<UGFCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			return;
		}
		bSavedSprint = Move->WantsSprint();
		bSavedDash = Move->WasDashRecorded();
		bSavedSwing = Move->WantsGrappleSwing();
		bSavedYank = Move->WasYankRecorded();
		bSavedJump = Move->WasJumpUsed();
		StartSim = Move->GetSimAtMoveStart();
		SavedContact = Move->GetContactAtMoveStart();
		SavedCameraForward = Move->GetLastCameraForward();
		Move->ClearDashRecorded();
		Move->ClearYankRecorded();
	}

	virtual void PrepMoveFor(ACharacter* C) override
	{
		Super::PrepMoveFor(C);
		UGFCharacterMovementComponent* Move = C ? Cast<UGFCharacterMovementComponent>(C->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			return;
		}
		Move->SetSprintHeld(bSavedSprint);
		Move->SetDashFromReplay(bSavedDash);
		Move->RestoreSim(StartSim);
		Move->SetReplayCamera(SavedCameraForward);
		Move->SetReplayTraversal(bSavedJump, bSavedYank, bSavedSwing, SavedContact);
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Flags = Super::GetCompressedFlags();
		if (bSavedSprint)
		{
			Flags |= FSavedMove_Character::FLAG_Custom_0;
		}
		if (bSavedDash)
		{
			Flags |= FSavedMove_Character::FLAG_Custom_1;
		}
		if (bSavedSwing)
		{
			Flags |= FSavedMove_Character::FLAG_Custom_2;
		}
		if (bSavedYank)
		{
			Flags |= FSavedMove_Character::FLAG_Custom_3;
		}
		return Flags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSavedMove_GFCharacter* Next = static_cast<const FSavedMove_GFCharacter*>(NewMove.Get());
		if (!Next || bSavedDash || Next->bSavedDash || bSavedSprint != Next->bSavedSprint)
		{
			return false;
		}
		if (bSavedYank || Next->bSavedYank || bSavedSwing != Next->bSavedSwing || bSavedJump != Next->bSavedJump)
		{
			return false;
		}
		if (SavedContact.bDashImpact || Next->SavedContact.bDashImpact || SavedContact.bWallContact != Next->SavedContact.bWallContact)
		{
			return false;
		}
		if (SavedContact.bSwivelContact != Next->SavedContact.bSwivelContact || SavedContact.bGrappleHit != Next->SavedContact.bGrappleHit)
		{
			return false;
		}
		// #355: a climb starting, a lip coming into reach, or a mantle starting or ending is its own move.
		if (SavedContact.bWallAhead != Next->SavedContact.bWallAhead || SavedContact.bLedge != Next->SavedContact.bLedge)
		{
			return false;
		}
		if (StartSim.bWallClimbing != Next->StartSim.bWallClimbing || StartSim.bMantling != Next->StartSim.bMantling)
		{
			return false;
		}
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}
};

class FNetworkPredictionData_Client_GFCharacter : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_GFCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FSavedMove_GFCharacter());
	}
};

UGFCharacterMovementComponent::UGFCharacterMovementComponent()
{
	// Constructor values match UGFMovementTuning. BeginPlay copies the asset, which may override them.
	MaxWalkSpeed = 500.f;
	JumpZVelocity = 520.f;
	// 50 degrees matches UGFMovementTuning::WalkableFloorAngle. BeginPlay copies the asset.
	SetWalkableFloorAngle(50.f);
}

void UGFCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyBaseRunSpeed();
	ApplyJumpTuning();
	ApplyStairTuning();
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		Sim.ResetFull(GetTuning());
		AuthoritativeStamina = Sim.Stamina;
		RefreshStaminaGate();
	}
	else if (!bGotAuthoritativeStamina)
	{
		// No stamina packet yet. Guess the gate from the map until ReplicatedStaminaGate arrives.
		if (ReplicatedStaminaGate == 0)
		{
			bStaminaEnabled = GFMovementPolicy::IsStaminaEnabled(GFCharacterMovementDetail::GFMovementMapLabelForPolicy(GetWorld()), false, false);
		}
		else
		{
			ApplyStaminaGate();
		}
		Sim.ResetFull(GetTuning());
	}
	else
	{
		ApplyStaminaGate();
	}
	SimAtMoveStart = Sim;
	RefreshKindleOverlaps();
}

void UGFCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGFCharacterMovementComponent, AuthoritativeStamina, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFCharacterMovementComponent, ReplicatedStaminaGate, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFCharacterMovementComponent, bInsideKindleRing, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UGFCharacterMovementComponent, ReplicatedFlipTimeLeft, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UGFCharacterMovementComponent, ReplicatedWallClimbPhase, COND_SkipOwner);
}

EGFWallClimbPhase UGFCharacterMovementComponent::GetWallClimbPhase() const
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return ReplicatedWallClimbPhase <= static_cast<uint8>(EGFWallClimbPhase::Mantling)
			? static_cast<EGFWallClimbPhase>(ReplicatedWallClimbPhase)
			: EGFWallClimbPhase::None;
	}
	if (Sim.bMantling)
	{
		return EGFWallClimbPhase::Mantling;
	}
	return Sim.bWallClimbing ? EGFWallClimbPhase::Climbing : EGFWallClimbPhase::None;
}

void UGFCharacterMovementComponent::ApplyBaseRunSpeed()
{
	MaxWalkSpeed = GetTuning().BaseRunSpeed;
}

void UGFCharacterMovementComponent::ApplyJumpTuning()
{
	const UGFMovementTuning& Tuning = GetTuning();
	JumpZVelocity = Tuning.JumpZVelocity;
	if (CharacterOwner)
	{
		CharacterOwner->JumpMaxCount = FMath::Max(Tuning.JumpsPerAirtime, 0);
	}
}

void UGFCharacterMovementComponent::ApplyStairTuning()
{
	const UGFMovementTuning& Tuning = GetTuning();
	float Step = Tuning.MaxStepHeight;
	if (const AGFCharacter* Body = Cast<AGFCharacter>(CharacterOwner))
	{
		Step = Body->PlayerMaxStepHeight;
	}
	GFStairMovement::Apply(this, Step, Tuning.WalkableFloorAngle);
}

const UGFMovementTuning& UGFCharacterMovementComponent::GetTuning() const
{
	if (TuningAsset)
	{
		return *TuningAsset;
	}
	return *GetDefault<UGFMovementTuning>();
}

float UGFCharacterMovementComponent::GetMaxStamina() const
{
	return GetTuning().MaxStamina;
}

void UGFCharacterMovementComponent::SetSprintHeld(bool bHeld)
{
	bWantsToSprint = bHeld;
}

void UGFCharacterMovementComponent::NotifyDashPressed()
{
	bDashThisMove = true;
}

void UGFCharacterMovementComponent::SetReplayCamera(const FVector& Forward)
{
	ReplayCameraForward = Forward;
	bUseReplayCamera = true;
}

void UGFCharacterMovementComponent::SetReplayTraversal(bool bJump, bool bYank, bool bSwing, const FGFTraversalContact& Contact)
{
	bUseReplayTraversal = true;
	bReplayJump = bJump;
	bReplayYank = bYank;
	bReplaySwing = bSwing;
	ReplayContact = Contact;
}

void UGFCharacterMovementComponent::SetGrappleSwingHeld(bool bHeld)
{
	bWantsGrappleSwing = bHeld;
}

void UGFCharacterMovementComponent::NotifyGrappleYank()
{
	bYankThisMove = true;
}

void UGFCharacterMovementComponent::ApplyStaminaGate()
{
	if (ReplicatedStaminaGate == 1)
	{
		bStaminaEnabled = true;
	}
	else if (ReplicatedStaminaGate == 2)
	{
		bStaminaEnabled = false;
	}
}

void UGFCharacterMovementComponent::RefreshStaminaGate()
{
	bool bOpted = false;
	bool bValue = false;
	GFCharacterMovementDetail::GFMovementReadOptIn(GetWorld(), bOpted, bValue);
	const bool bNow = GFMovementPolicy::IsStaminaEnabled(
		GFCharacterMovementDetail::GFMovementMapLabelForPolicy(GetWorld()), bOpted, bValue, bBeatHopRun);
	const uint8 Gate = bNow ? static_cast<uint8>(1) : static_cast<uint8>(2);
	if (bStaminaEnabled != bNow || ReplicatedStaminaGate != Gate)
	{
		bStaminaEnabled = bNow;
		ReplicatedStaminaGate = Gate;
	}
}

void UGFCharacterMovementComponent::RefreshBeatHop()
{
	bBeatHopRun = false;
	BeatHopRefSpeed = 0.f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
	{
		const AGFBeatHopCourse* Course = *It;
		if (!Course || !Course->IsNinjaOptIn())
		{
			continue;
		}
		bBeatHopRun = true;
		BeatHopProfile = Course->GetNinjaProfile();
		BeatHopRefSpeed = Course->GetBeatmap().RefSpeedCms;
		BeatHopDifficulty = Course->GetRunDifficulty();
		break;
	}
}

float UGFCharacterMovementComponent::GetBeatHopCameraPullback() const
{
	if (!bBeatHopRun)
	{
		return 0.f;
	}
	return GFBeatHopNinja::CameraPullbackCm(IsWallRunning(), IsSwinging() || IsYanking(), BeatHopProfile);
}

void UGFCharacterMovementComponent::ApplyBeatHopMoveMask(FGFStaminaCommand& Command) const
{
	if (!bBeatHopRun)
	{
		return;
	}
	// #355: Beat Hop courses are built around the kit they list. Wall climb is not on it, so a run never climbs.
	Command.bWallClimbAllowed = false;
	Command.Contact.bWallAhead = false;
	Command.Contact.bLedge = false;
	if (!GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::WallRun, BeatHopProfile))
	{
		Command.Contact.bWallContact = false;
	}
	if (!GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::Grapple, BeatHopProfile))
	{
		Command.bGrappleSwingHeld = false;
		Command.bGrappleYankPressed = false;
		Command.Contact.bGrappleHit = false;
	}
	if (!GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::Swivel, BeatHopProfile))
	{
		Command.Contact.bSwivelContact = false;
	}
	if (!GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::DashGrip, BeatHopProfile))
	{
		Command.Contact.bDashImpact = false;
	}
	const bool bDash = GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::DashGate, BeatHopProfile)
		|| GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::AirDash, BeatHopProfile);
	if (!bDash)
	{
		Command.bDashPressed = false;
	}
}

void UGFCharacterMovementComponent::RefreshKindleOverlaps()
{
	bLocalInsideKindle = false;
	UWorld* World = GetWorld();
	if (CharacterOwner && World)
	{
		for (TActorIterator<AGFKindleRing> It(World); It; ++It)
		{
			const AGFKindleRing* Ring = *It;
			if (Ring && GFCharacterMovementDetail::GFMovementCapsuleOverlapsSphere(CharacterOwner, Ring->GetActorLocation(), Ring->GetRadius()))
			{
				bLocalInsideKindle = true;
				break;
			}
		}
	}
	if (CharacterOwner && CharacterOwner->HasAuthority() && bInsideKindleRing != bLocalInsideKindle)
	{
		bInsideKindleRing = bLocalInsideKindle;
	}
}

FVector UGFCharacterMovementComponent::LiveCameraForward() const
{
	if (!CharacterOwner)
	{
		return FVector::ForwardVector;
	}
	const double Yaw = CharacterOwner->GetControlRotation().Yaw;
	const FVector Forward = FRotator(0.0, Yaw, 0.0).Vector();
	const FVector Flat = FVector(Forward.X, Forward.Y, 0.0).GetSafeNormal();
	return Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
}

void UGFCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bDashThisMove = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bWantsGrappleSwing = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bYankThisMove = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

FNetworkPredictionData_Client* UGFCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UGFCharacterMovementComponent* MutableThis = const_cast<UGFCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_GFCharacter(*this);
		MutableThis->ClientPredictionData->MaxSmoothNetUpdateDist = NetworkMaxSmoothUpdateDistance;
		MutableThis->ClientPredictionData->NoSmoothNetUpdateDist = NetworkNoSmoothUpdateDistance;
	}
	return ClientPredictionData;
}

void UGFCharacterMovementComponent::PerformMovement(float DeltaTime)
{
	if (!CharacterOwner)
	{
		StepDeltaSeconds = 0.f;
		MoveAirSteer = FGFAirSteer();
		Super::PerformMovement(DeltaTime);
		return;
	}
	const bool bAuthoritative = CharacterOwner->HasAuthority();
	const bool bLocal = CharacterOwner->IsLocallyControlled();
	if (!bAuthoritative && !bLocal)
	{
		StepDeltaSeconds = 0.f;
		MoveAirSteer = FGFAirSteer();
		Super::PerformMovement(DeltaTime);
		return;
	}

	RefreshBeatHop();
	if (bAuthoritative)
	{
		RefreshStaminaGate();
	}
	else if (bBeatHopRun)
	{
		bStaminaEnabled = true;
	}
	else
	{
		ApplyStaminaGate();
	}
	RefreshKindleOverlaps();

	SimAtMoveStart = Sim;
	const bool bDash = bDashThisMove;
	bDashThisMove = false;
	bDashRecorded = bDash;

	bool bJump = false;
	bool bYank = false;
	bool bSwing = bWantsGrappleSwing;
	if (bUseReplayTraversal)
	{
		bJump = bReplayJump;
		bYank = bReplayYank;
		bSwing = bReplaySwing;
		ContactAtMoveStart = ReplayContact;
		bUseReplayTraversal = false;
		bPendingDashHit = false;
	}
	else
	{
		bJump = CharacterOwner->bPressedJump;
		bYank = bYankThisMove;
		GatherLiveContact(ContactAtMoveStart);
		bPendingDashHit = false;
	}
	bYankThisMove = false;
	bYankRecorded = bYank;
	bJumpUsedThisMove = bJump;

	FGFStaminaCommand Command;
	Command.bSprintHeld = bWantsToSprint;
	Command.bDashPressed = bDash;
	Command.bJumpPressed = bJump;
	Command.bGrappleSwingHeld = bSwing;
	Command.bGrappleYankPressed = bYank;
	Command.bGrounded = IsMovingOnGround();
	Command.bStaminaEnabled = bStaminaEnabled;
	Command.HorizontalSpeed = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.0).Size());
	Command.Velocity = Velocity;
	Command.PlayerLocation = CharacterOwner->GetActorLocation();
	Command.Contact = ContactAtMoveStart;
	const bool bReplayMove = bUseReplayCamera;
	// Replay uses the hearth at the rewound location only. The live replicated bit is for the current move.
	Command.bInsideKindleRing = bLocalInsideKindle || (!bReplayMove && !bAuthoritative && bInsideKindleRing);
	Command.DeltaSeconds = DeltaTime;
	Command.MoveDirection = Acceleration;
	if (bUseReplayCamera)
	{
		Command.CameraForward = ReplayCameraForward;
		bUseReplayCamera = false;
	}
	else
	{
		Command.CameraForward = LiveCameraForward();
	}
	LastCameraForward = Command.CameraForward;
	if (bBeatHopRun)
	{
		const UGFMovementTuning& BeatTuning = GetTuning();
		Command.bCostsFree = BeatHopProfile.StaminaCostScale <= 0.f;
		Command.GroundDashDurationOverride = GFBeatHopNinja::DurationForBeats(BeatHopProfile.GroundDashBeats, BeatHopRefSpeed, BeatTuning.DashSpeed);
		Command.AirDashDurationOverride = GFBeatHopNinja::DurationForBeats(BeatHopProfile.AirDashBeats, BeatHopRefSpeed, BeatTuning.DashSpeed);
		Command.WallKickSpeedOverride = GFBeatHopNinja::SpeedForBeats(BeatHopProfile.WallKickBeats, BeatHopRefSpeed, BeatTuning.DashDuration);
		Command.JumpsPerAirtimeOverride = GFBeatHopNinja::JumpsFor(BeatHopDifficulty, BeatHopProfile);
	}
	Command.bWallClimbAllowed = bWallClimbAllowed;
	ApplyBeatHopMoveMask(Command);
	Sim = GFStaminaSim::Step(Sim, Command, GetTuning());
	// Substeps share this frame's start so a rope, yank, or orbit lands on TravelLocation.
	LocationAtStepStart = Command.PlayerLocation;
	StepDeltaSeconds = DeltaTime;
	// #347: aim steering for this move, from the camera this move recorded (a replay restores it), so the owner,
	// the server and a correction replay all turn the flight the same way.
	{
		const bool bSteeredBefore = MoveAirSteer.bOn;
		if (!IsFalling())
		{
			// #347: where this flight takes off from, if this move jumps. The moves in the air keep it.
			bAirSteerTakeoffOnRoad = IsInAirSteerRoadScope(CharacterOwner->GetActorLocation());
			bAirSteerScopeLossLogged = false;
		}
		MoveAirSteer = BuildAirSteer(Command.CameraForward);
		LastAirSteerTurnDeg = 0.f;
		// #347: say so once per flight when the steering (and with it the circle following the camera) drops because
		// the pawn left the road scope mid-flight. Before the takeoff latch and the headroom that froze the circle.
		if (IsFalling() && bSteeredBefore && !MoveAirSteer.bOn && !bAirSteerScopeLossLogged && GetTuning().AccelHop.bAirSteerOnlyOnRoad
			&& !IsInAirSteerRoadScope(CharacterOwner->GetActorLocation()))
		{
			bAirSteerScopeLossLogged = true;
			UE_LOG(LogGF, Log, TEXT("AirSteer: off mid-flight, %s left the hop road scope at %s (took off on the road %d, headroom %.0f cm over the box)"),
				*CharacterOwner->GetName(), *CharacterOwner->GetActorLocation().ToCompactString(), bAirSteerTakeoffOnRoad ? 1 : 0,
				GetTuning().AccelHop.AirSteerRoadHeadroomCm);
		}
	}

	if (bAuthoritative)
	{
		const float Quantized = FMath::RoundToFloat(Sim.Stamina * 2.f) * 0.5f;
		if (!FMath::IsNearlyEqual(AuthoritativeStamina, Quantized, 0.01f))
		{
			AuthoritativeStamina = Quantized;
		}
		if (!FMath::IsNearlyEqual(ReplicatedFlipTimeLeft, Sim.FlipTimeLeft, 0.02f))
		{
			ReplicatedFlipTimeLeft = Sim.FlipTimeLeft;
		}
		const uint8 Phase = static_cast<uint8>(Sim.bMantling ? EGFWallClimbPhase::Mantling
			: (Sim.bWallClimbing ? EGFWallClimbPhase::Climbing : EGFWallClimbPhase::None));
		if (ReplicatedWallClimbPhase != Phase)
		{
			ReplicatedWallClimbPhase = Phase;
		}
	}

	// The step decides whether this press is a real hop. The engine jump runs inside Super.
	if (!bStaminaEnabled)
	{
		if (CharacterOwner)
		{
			CharacterOwner->JumpMaxCount = 1;
			if (!IsMovingOnGround())
			{
				CharacterOwner->bPressedJump = false;
			}
		}
	}
	else if (Sim.bJumpAccepted)
	{
		const int32 JumpCap = bBeatHopRun
			? GFBeatHopNinja::JumpsFor(BeatHopDifficulty, BeatHopProfile)
			: GetTuning().JumpsPerAirtime;
		CharacterOwner->bPressedJump = true;
		CharacterOwner->JumpCurrentCount = 0;
		CharacterOwner->JumpMaxCount = FMath::Max(JumpCap, 1);
		JumpZVelocity = Sim.JumpZSpeed;
		// The engine checks jump input before PerformMovement, so before this step accepted the press,
		// and Super clears the press before the next check. The accepted hop has to run here.
		TGuardValue<bool> StepJump(bJumpFromStep, true);
		CharacterOwner->CheckJumpInput(DeltaTime);
	}
	else if (bJump)
	{
		CharacterOwner->bPressedJump = false;
	}
	ApplyWallClimbMove();

	Super::PerformMovement(DeltaTime);
	JumpZVelocity = GetTuning().JumpZVelocity;
	if (CharacterOwner && bStaminaEnabled)
	{
		const int32 JumpCap = bBeatHopRun
			? GFBeatHopNinja::JumpsFor(BeatHopDifficulty, BeatHopProfile)
			: GetTuning().JumpsPerAirtime;
		CharacterOwner->JumpMaxCount = FMath::Max(JumpCap, 0);
	}
}

float UGFCharacterMovementComponent::GetMaxSpeed() const
{
	// Acceleration Hop scales the live walk speed, so sprint stacks on it; a dash keeps its own fixed speed.
	return GFStaminaSim::HorizontalSpeed(Sim, GetTuning(), Super::GetMaxSpeed() * GetAccelHopMultiplier());
}

double UGFCharacterMovementComponent::AccelHopNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

float UGFCharacterMovementComponent::GetAccelHopMultiplier() const
{
	return FGFAccelHopRules::MultiplierAt(AccelHop, AccelHopNow());
}

void UGFCharacterMovementComponent::PollAccelHopLanding()
{
	const bool bFalling = IsFalling();
	const bool bLanded = bAccelHopWasFalling && !bFalling && CurrentFloor.IsWalkableFloor();
	bAccelHopWasFalling = bFalling;
	if (!bLanded || !CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	AGFAccelHopTile* Tile = AGFAccelHopTile::FromFloor(CurrentFloor.HitResult.GetComponent());
	const UGFBeatClockSubsystem* Clock = Tile && GetWorld() ? GetWorld()->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
	if (!Clock || !Clock->IsRunning())
	{
		return; // no music on this machine (off the road box): a hop tile is just floor
	}
	const EGFBeatHopMode Mode = Clock->GetBeatSource() == EGFBeatSource::Live ? EGFBeatHopMode::Live : EGFBeatHopMode::Songs;
	HandleAccelHopLanding(Tile, Clock->GetCurrentBeat(), Clock->GetBPM(), Mode);
}

FGFOnAccelHopLanded UGFCharacterMovementComponent::OnAccelHopLanded;

bool UGFCharacterMovementComponent::HandleAccelHopLanding(AGFAccelHopTile* Tile, double NowBeat, float Bpm, EGFBeatHopMode Mode)
{
	if (!Tile)
	{
		return false;
	}
	const FGFAccelHopTuning& Hop = GetTuning().AccelHop;
	const FGFAccelHopHit Hit = FGFAccelHopRules::JudgeLanding(NowBeat, Bpm, FGFAccelHopRules::WindowsFor(Mode, Hop), Hop.BeatsPerBar);
	if (Hit.Judgement == EGFJudgement::None)
	{
		return false;
	}
	LastAccelHopHit = Hit;
	const float BeatSeconds = FGFAccelHopRules::ClampBeatSeconds(Bpm > 0.f ? 60.f / Bpm : 0.5f);
	const double Now = AccelHopNow();
	// v2 (#342): the lit target pays LitBoostScale. The server (or a solo / host player) checks its own pick and
	// spends it; a remote client predicts from the replicated pick and the server's copy decides for real.
	const bool bAuthority = !CharacterOwner || CharacterOwner->HasAuthority();
	AGFAccelHopRoad* Road = Tile->GetRoad();
	const bool bLit = Road && (bAuthority ? Road->ServerIsLitLanding(Tile, Now) : Road->IsTileLit(Tile));
	const bool bChanged = FGFAccelHopRules::ApplyHit(AccelHop, Hit, Tile->GetKind(), Now, BeatSeconds, Hop, bLit);
	if (bChanged)
	{
		bLastAccelHopLit = bLit;
		if (bLit && bAuthority)
		{
			Road->ServerConsumeLit(Tile, Now);
		}
	}
	if (CharacterOwner && !CharacterOwner->HasAuthority())
	{
		ServerAccelHop(Tile, Hit, BeatSeconds);
	}
	if (OnAccelHopLanded.IsBound())
	{
		FGFAccelHopLandingEvent Event;
		Event.Tile = Tile;
		Event.Hit = Hit;
		Event.bLit = bLit && bChanged;
		Event.bApplied = bChanged;
		OnAccelHopLanded.Broadcast(this, Event);
	}
	return bChanged;
}

bool UGFCharacterMovementComponent::ApplyServerAccelHop(AGFAccelHopTile* Tile, const FGFAccelHopHit& Hit, float BeatSeconds)
{
	if (!Tile || !CharacterOwner)
	{
		return false;
	}
	const FGFAccelHopTuning& Hop = GetTuning().AccelHop;
	if (FVector::Dist2D(Tile->GetActorLocation(), CharacterOwner->GetActorLocation()) > Hop.ServerCheckRadiusCm)
	{
		return false; // the reported tile is not under this pawn
	}
	const double Now = AccelHopNow();
	const bool bInTime = Hit.Judgement == EGFJudgement::Perfect || Hit.Judgement == EGFJudgement::Good;
	if (bInTime && !FGFAccelHopRules::ServerAccepts(AccelHop, Now, Hop))
	{
		return false;
	}
	FGFAccelHopHit Checked = Hit;
	Checked.bOnBarLine = Hit.BeatIndex >= 0 && Hit.BeatIndex % FMath::Max(1, Hop.BeatsPerBar) == 0;
	LastAccelHopHit = Checked;
	// The server's own lit pick, never the client's word (#342).
	AGFAccelHopRoad* Road = Tile->GetRoad();
	const bool bLit = Road && Road->ServerIsLitLanding(Tile, Now);
	const bool bChanged = FGFAccelHopRules::ApplyHit(AccelHop, Checked, Tile->GetKind(), Now, FGFAccelHopRules::ClampBeatSeconds(BeatSeconds), Hop, bLit);
	if (bChanged)
	{
		bLastAccelHopLit = bLit;
		if (bLit)
		{
			Road->ServerConsumeLit(Tile, Now);
		}
	}
	return bChanged;
}

void UGFCharacterMovementComponent::ServerAccelHop_Implementation(AGFAccelHopTile* Tile, const FGFAccelHopHit& Hit, float BeatSeconds)
{
	ApplyServerAccelHop(Tile, Hit, BeatSeconds);
}

float UGFCharacterMovementComponent::GetGravityZ() const
{
	return Super::GetGravityZ() * GFStaminaSim::GravityScale(Sim, GetTuning());
}

bool UGFCharacterMovementComponent::DoJump(bool bReplayingMoves, float DeltaTime)
{
	// Mini-games keep the ordinary hop. The triple jump is part of the stamina move set.
	if (!bStaminaEnabled)
	{
		return Super::DoJump(bReplayingMoves, DeltaTime);
	}
	if (!bJumpFromStep || !Sim.bJumpAccepted || !CharacterOwner)
	{
		return false;
	}
	if (!Super::DoJump(bReplayingMoves, DeltaTime))
	{
		return false;
	}
	Velocity.Z = static_cast<double>(Sim.JumpZSpeed);
	Velocity += Sim.JumpForward;
	return true;
}

void UGFCharacterMovementComponent::ApplyWallClimbMove()
{
	const bool bClimbNow = Sim.bWallClimbing || Sim.bMantling;
	const bool bClimbBefore = SimAtMoveStart.bWallClimbing || SimAtMoveStart.bMantling;
	if (!bClimbNow && !bClimbBefore)
	{
		return;
	}
	if (bClimbNow && IsMovingOnGround())
	{
		// Walking keeps the capsule on the floor. A climb from the ground has to leave it.
		SetMovementMode(MOVE_Falling);
	}
	// PhysFalling hands CalcVelocity a flat velocity and puts the old Z back, so the climb's Z goes in here.
	// That covers the climb, the mantle, the stop at the lip, the kick off the wall and the hand-off to a wall run.
	if (Sim.bOverrideVelocity)
	{
		Velocity = Sim.OverrideVelocity;
	}
	else
	{
		// A climb that just dropped (peel, out of time or stamina) falls from here instead of keeping its climb speed.
		Velocity.Z = FMath::Min(Velocity.Z, 0.0);
	}
}

void UGFCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (Sim.bDashing)
	{
		const double Speed = static_cast<double>(GetTuning().DashSpeed);
		const FVector Flat = FVector(Sim.DashDir.X, Sim.DashDir.Y, 0.0).GetSafeNormal();
		Velocity.X = Flat.X * Speed;
		Velocity.Y = Flat.Y * Speed;
		DashPlanarSpeed = static_cast<float>(Speed);
		return;
	}
	if (Sim.bOverrideVelocity)
	{
		// Tangential OverrideVelocity is what a release boost reads. While the constraint is active,
		// the capsule tracks TravelLocation so movement substeps do not walk off the rope or the pole.
		if (StepDeltaSeconds > 0.f && CharacterOwner && (Sim.bSwinging || Sim.bYanking || Sim.bSwiveling))
		{
			const double Dt = static_cast<double>(StepDeltaSeconds);
			Velocity = (Sim.TravelLocation - LocationAtStepStart) / Dt;
			return;
		}
		Velocity = Sim.OverrideVelocity;
		return;
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	// #347: in the air, turn the flat momentum toward the aim after the stick push, same speed, capped turn rate.
	// PhysFalling hands CalcVelocity the flat velocity (Z zeroed), and GetLandingTarget predicts the same turn.
	if (MoveAirSteer.bOn && IsFalling())
	{
		const FVector Before(Velocity.X, Velocity.Y, 0.0);
		Velocity = GFTraversalSim::SteerTowardAim(Velocity, MoveAirSteer, DeltaTime);
		const FVector After(Velocity.X, Velocity.Y, 0.0);
		if (!Before.IsNearlyZero() && !After.IsNearlyZero())
		{
			LastAirSteerTurnDeg = FMath::RadiansToDegrees(static_cast<float>(FMath::Acos(FMath::Clamp(FVector::DotProduct(Before.GetSafeNormal(), After.GetSafeNormal()), -1.0, 1.0))));
		}
	}
}

FGFAirSteer UGFCharacterMovementComponent::BuildAirSteer(const FVector& Aim) const
{
	FGFAirSteer Steer;
	const FGFAccelHopTuning& Hop = GetTuning().AccelHop;
	if (!Hop.bAirSteerFollowsAim || !CharacterOwner || !CharacterOwner->IsPlayerControlled())
	{
		return Steer;
	}
	if (!CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
	{
		return Steer;
	}
	if (Sim.bSwinging || Sim.bYanking || Sim.bSwiveling || Sim.bDashing || Sim.bOverrideVelocity)
	{
		return Steer;
	}
	// #347: on the road means inside the box (plus headroom on top), or in a flight that took off inside it.
	if (Hop.bAirSteerOnlyOnRoad && !(Hop.bAirSteerKeepsTakeoffRoad && IsFalling() && bAirSteerTakeoffOnRoad)
		&& !IsInAirSteerRoadScope(CharacterOwner->GetActorLocation()))
	{
		return Steer;
	}
	Steer.bOn = true;
	Steer.Aim = Aim;
	Steer.TurnRateDeg = Hop.AirSteerTurnRateDeg;
	Steer.MinSpeed = Hop.AirSteerMinSpeedCm;
	Steer.MaxAngleDeg = Hop.AirSteerMaxAngleDeg;
	return Steer;
}

bool UGFCharacterMovementComponent::IsOnAccelHopRoad(const FVector& Location, double HeadroomUpCm) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<AGFAccelHopRoad> It(World); It; ++It)
	{
		if (It->ContainsPoint(Location, HeadroomUpCm))
		{
			return true;
		}
	}
	return false;
}

bool UGFCharacterMovementComponent::IsInAirSteerRoadScope(const FVector& Location) const
{
	return IsOnAccelHopRoad(Location, static_cast<double>(FMath::Max(0.f, GetTuning().AccelHop.AirSteerRoadHeadroomCm)));
}

void UGFCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	NoteStairRefusal(Hit);
	if (!Sim.bDashing || !Hit.bBlockingHit)
	{
		return;
	}
	// Consumed on the next step so the saved move replays the same contact.
	bPendingDashHit = true;
	PendingImpactNormal = Hit.ImpactNormal;
	PendingSpeedBefore = DashPlanarSpeed > 0.f ? DashPlanarSpeed : GetTuning().DashSpeed;
	PendingSpeedAfter = static_cast<float>(FVector(Velocity.X, Velocity.Y, 0.0).Size());
}

namespace GFRiserAssistDetail
{
	/** How far past the riser face the tread probe looks. A few cm is enough to land on the tread, not the next riser. */
	constexpr float ProbePastFaceCm = 4.f;
	/** The forward leg clears at least this share of the capsule radius, so the down sweep lands on the tread, not its lip. */
	constexpr float MinForwardRadiusShare = 0.5f;
	/** A face this square to the move is a riser. Grazing a side wall is not. */
	constexpr float MinIntoFaceDot = 0.25f;
	constexpr double LogEverySeconds = 2.0;
	/** A refused face whose walkable top is within this many step heights is named a too-tall stair, not a wall. */
	constexpr float TallRiserShare = 1.5f;
	/** The face-top probe starts this far above the feet, over the tallest town hull (the crane, about 14 m). */
	constexpr float FaceProbeCm = 2500.f;
	/** Past this many components the per-component log clock drops the stale ones. */
	constexpr int32 MaxLogClocks = 128;

	/** The static mesh behind a hit, so a playtest log names the kit piece and not just the placed actor. */
	FString MeshNameOf(const FHitResult& Hit)
	{
		const UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Hit.GetComponent());
		return MeshComponent && MeshComponent->GetStaticMesh() ? MeshComponent->GetStaticMesh()->GetName() : FString(TEXT("none"));
	}
}

bool UGFCharacterMovementComponent::StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult)
{
	PendingRefusalComponent.Reset();
	PendingRefusalWhy = nullptr;
	PendingRefusalRise = -1.f;
	const TCHAR* Why = nullptr;
	float Rise = -1.f;
	{
		TGuardValue<bool> InsideStepUp(bInsideStepUp, true);
		if (Super::StepUp(GravDir, Delta, Hit, OutStepDownResult))
		{
			return true;
		}
		if (TryRiserAssist(GravDir, Delta, Hit, OutStepDownResult, Why, Rise))
		{
			return true;
		}
	}
	// Not logged here: MoveAlongFloor hands this hit to HandleImpact next, which logs every refused walking hit,
	// including the ones that never reach StepUp. Keep what the assist learned for that line.
	PendingRefusalComponent = Hit.GetComponent();
	PendingRefusalWhy = Why;
	PendingRefusalRise = Rise;
	return false;
}

bool UGFCharacterMovementComponent::TryRiserAssist(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FStepDownResult* OutStepDownResult,
	const TCHAR*& OutWhy, float& OutRise)
{
	using namespace GFRiserAssistDetail;
	OutWhy = nullptr;
	OutRise = -1.f;
	if (!bRiserAssist || !CharacterOwner || !UpdatedComponent || !GetWorld() || GravDir.IsNearlyZero())
	{
		return false;
	}
	if (!IsMovingOnGround() || !CurrentFloor.IsWalkableFloor() || !Hit.IsValidBlockingHit() || Hit.bStartPenetrating || IsWalkable(Hit))
	{
		return false;
	}
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && !HitComponent->CanCharacterStepUp(CharacterOwner))
	{
		return false;
	}
	const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent();
	if (!Capsule)
	{
		return false;
	}

	const FVector Up = -GravDir.GetSafeNormal();
	const FVector Face = FVector::VectorPlaneProject(Hit.ImpactNormal, Up).GetSafeNormal();
	const FVector Planar = FVector::VectorPlaneProject(Delta, Up);
	if (Face.IsNearlyZero() || Planar.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector MoveDir = Planar.GetSafeNormal();
	if (FVector::DotProduct(MoveDir, -Face) < MinIntoFaceDot)
	{
		return false;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const float FloorDist = FMath::Max(0.f, CurrentFloor.GetDistanceToFloor());
	const double FeetUp = FVector::DotProduct(Start, Up) - HalfHeight - FloorDist;

	// Is there a tread just past the face, within one step, that a pawn may stand on?
	const FVector ProbeAt = Hit.ImpactPoint - Face * ProbePastFaceCm;
	const double ProbeUp = FVector::DotProduct(ProbeAt, Up);
	const FVector ProbeTop = ProbeAt + Up * (FeetUp + MaxStepHeight + 1.0 - ProbeUp);
	const FVector ProbeBottom = ProbeAt + Up * (FeetUp + 0.5 - ProbeUp);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFRiserAssist), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	FHitResult Tread;
	if (!GetWorld()->LineTraceSingleByChannel(Tread, ProbeTop, ProbeBottom, UpdatedComponent->GetCollisionObjectType(), Params, Response)
		|| Tread.bStartPenetrating
		|| FVector::DotProduct(Tread.ImpactNormal, Up) < GetWalkableFloorZ())
	{
		// No tread within one step. Diagnostic only: a walkable top a little higher (up to TallRiserShare step heights) is a
		// stair the kit made too tall, which the #341 log names. Anything higher is a wall and stays quiet.
		const FVector TallTop = ProbeAt + Up * (FeetUp + MaxStepHeight * TallRiserShare + 1.0 - ProbeUp);
		const FVector TallBottom = ProbeAt + Up * (FeetUp + MaxStepHeight - ProbeUp);
		FHitResult Tall;
		if (GetWorld()->LineTraceSingleByChannel(Tall, TallTop, TallBottom, UpdatedComponent->GetCollisionObjectType(), Params, Response)
			&& !Tall.bStartPenetrating && FVector::DotProduct(Tall.ImpactNormal, Up) >= GetWalkableFloorZ())
		{
			OutWhy = TEXT("riser taller than MaxStepHeight");
			OutRise = static_cast<float>(FVector::DotProduct(Tall.ImpactPoint, Up) - FeetUp);
		}
		return false;
	}
	const float Rise = static_cast<float>(FVector::DotProduct(Tread.ImpactPoint, Up) - FeetUp);
	OutRise = Rise;
	if (Rise <= 1.f || Rise > MaxStepHeight)
	{
		return false;
	}

	// Same three legs as the engine step (up, forward, down), sized to the tread instead of the full step height.
	FScopedMovementUpdate ScopedAssist(UpdatedComponent, EScopedUpdate::DeferredUpdates);
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();
	const float Lift = Rise + MAX_FLOOR_DIST;
	FHitResult UpHit(1.f);
	MoveUpdatedComponent(Up * Lift, Rotation, true, &UpHit);
	if (UpHit.bStartPenetrating || FVector::DotProduct(UpdatedComponent->GetComponentLocation() - Start, Up) < Rise + 0.5f)
	{
		ScopedAssist.RevertMove();
		OutWhy = TEXT("no headroom over the tread");
		return false;
	}

	const float Forward = FMath::Max(static_cast<float>(Planar.Size()), Radius * MinForwardRadiusShare);
	FHitResult ForwardHit(1.f);
	MoveUpdatedComponent(MoveDir * Forward, Rotation, true, &ForwardHit);
	if (ForwardHit.bStartPenetrating || (ForwardHit.bBlockingHit && ForwardHit.Time <= KINDA_SMALL_NUMBER))
	{
		ScopedAssist.RevertMove();
		OutWhy = TEXT("tread too shallow or the next riser is too close");
		return false;
	}

	FHitResult DownHit(1.f);
	MoveUpdatedComponent(-Up * (Lift + MAX_FLOOR_DIST * 2.f), Rotation, true, &DownHit);
	const float Landed = DownHit.bBlockingHit ? static_cast<float>(FVector::DotProduct(DownHit.ImpactPoint, Up) - FeetUp) : -1.f;
	if (!DownHit.bBlockingHit || DownHit.bStartPenetrating || !IsWalkable(DownHit) || Landed <= 0.5f || Landed > MaxStepHeight)
	{
		ScopedAssist.RevertMove();
		OutWhy = TEXT("no walkable landing past the lip");
		return false;
	}
	if (OutStepDownResult)
	{
		FindFloor(UpdatedComponent->GetComponentLocation(), OutStepDownResult->FloorResult, false, &DownHit);
		if (!OutStepDownResult->FloorResult.IsWalkableFloor())
		{
			ScopedAssist.RevertMove();
			OutWhy = TEXT("landing is not a walkable floor");
			return false;
		}
	}

	// The forward leg may be longer than this frame's delta. Keep the walk velocity instead of deriving it from the jump.
	bJustTeleported = true;
	++RiserAssistCount;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - RiserAssistLoggedAt >= LogEverySeconds)
	{
		RiserAssistLoggedAt = Now;
		UE_LOG(LogGF, Log, TEXT("StairAssist: engine step refused a %.1f cm riser on %s (mesh %s), lifted onto the tread (count %d)"),
			Rise, *GetNameSafe(Hit.GetActor()), *GFRiserAssistDetail::MeshNameOf(Hit), RiserAssistCount);
	}
	return true;
}

bool UGFCharacterMovementComponent::MeasureFaceTop(const FHitResult& Hit, float& OutTopCm, bool& bOutWalkable) const
{
	using namespace GFRiserAssistDetail;
	OutTopCm = -1.f;
	bOutWalkable = false;
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (!HitComponent || !Capsule || !UpdatedComponent)
	{
		return false;
	}
	const FVector Up = -GetGravityDirection().GetSafeNormal();
	FVector Face = FVector::VectorPlaneProject(Hit.ImpactNormal, Up).GetSafeNormal();
	if (Face.IsNearlyZero())
	{
		Face = FVector::VectorPlaneProject(Hit.Normal, Up).GetSafeNormal();
	}
	if (Face.IsNearlyZero())
	{
		return false;
	}
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float FloorDist = CurrentFloor.IsWalkableFloor() ? FMath::Max(0.f, CurrentFloor.GetDistanceToFloor()) : 0.f;
	const double FeetUp = FVector::DotProduct(UpdatedComponent->GetComponentLocation(), Up) - HalfHeight - FloorDist;
	// Straight down onto the component that blocked, just past its face. Simple collision only, since that is what blocked.
	const FVector ProbeAt = Hit.ImpactPoint - Face * ProbePastFaceCm;
	const double ProbeUp = FVector::DotProduct(ProbeAt, Up);
	const FVector Top = ProbeAt + Up * (FeetUp + FaceProbeCm - ProbeUp);
	const FVector Bottom = ProbeAt + Up * (FeetUp - 1.0 - ProbeUp);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFStairRefusalFace), false, CharacterOwner);
	FHitResult TopHit;
	if (!HitComponent->LineTraceComponent(TopHit, Top, Bottom, Params) || TopHit.bStartPenetrating)
	{
		return false;
	}
	OutTopCm = static_cast<float>(FVector::DotProduct(TopHit.ImpactPoint, Up) - FeetUp);
	bOutWalkable = FVector::DotProduct(TopHit.ImpactNormal, Up) >= GetWalkableFloorZ();
	return true;
}

void UGFCharacterMovementComponent::NoteStairRefusal(const FHitResult& Hit)
{
	using namespace GFRiserAssistDetail;
	UWorld* World = GetWorld();
	if (bInsideStepUp || !World || !CharacterOwner || !UpdatedComponent || !Hit.bBlockingHit || !IsMovingOnGround() || IsWalkable(Hit))
	{
		return;
	}
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	const bool bFromStepUp = PendingRefusalComponent.IsValid() && PendingRefusalComponent.Get() == HitComponent;
	const TCHAR* StepWhy = bFromStepUp ? PendingRefusalWhy : nullptr;
	const float Rise = bFromStepUp ? PendingRefusalRise : -1.f;
	PendingRefusalComponent.Reset();
	PendingRefusalWhy = nullptr;
	PendingRefusalRise = -1.f;

	++StairRefusalCount;
	const double Now = World->GetTimeSeconds();
	const TWeakObjectPtr<UPrimitiveComponent> Key(HitComponent);
	if (const double* LoggedAt = StairRefusalLoggedAt.Find(Key))
	{
		if (Now - *LoggedAt < LogEverySeconds)
		{
			return;
		}
	}
	if (StairRefusalLoggedAt.Num() >= MaxLogClocks)
	{
		for (auto It = StairRefusalLoggedAt.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || Now - It.Value() >= LogEverySeconds)
			{
				It.RemoveCurrent();
			}
		}
	}
	StairRefusalLoggedAt.Add(Key, Now);

	float FaceTop = -1.f;
	bool bTopWalkable = false;
	const bool bTop = MeasureFaceTop(Hit, FaceTop, bTopWalkable);
	const TCHAR* Why = nullptr;
	if (Hit.bStartPenetrating)
	{
		Why = TEXT("started inside collision");
	}
	else if (StepWhy)
	{
		Why = StepWhy;
	}
	else if (Cast<APawn>(Hit.GetActor()))
	{
		Why = TEXT("blocked by a pawn");
	}
	else if (HitComponent && !HitComponent->CanCharacterStepUp(CharacterOwner))
	{
		Why = TEXT("component refuses step up (CanCharacterStepUp false)");
	}
	else if (!bTop)
	{
		Why = TEXT("no top found on the blocking face");
	}
	else if (FaceTop > MaxStepHeight)
	{
		Why = TEXT("face taller than MaxStepHeight");
	}
	else if (!bTopWalkable)
	{
		Why = TEXT("face top is not walkable");
	}
	else
	{
		Why = bFromStepUp ? TEXT("engine step refused a face within MaxStepHeight") : TEXT("blocked before a step was tried");
	}

	LastWalkRefusal.Reason = Why;
	LastWalkRefusal.Actor = GetNameSafe(Hit.GetActor());
	LastWalkRefusal.Component = GetNameSafe(HitComponent);
	LastWalkRefusal.Mesh = MeshNameOf(Hit);
	LastWalkRefusal.ImpactPoint = Hit.ImpactPoint;
	LastWalkRefusal.ImpactNormal = Hit.ImpactNormal;
	LastWalkRefusal.FaceTopCm = bTop ? FaceTop : -1.f;
	LastWalkRefusal.bFaceTopWalkable = bTop && bTopWalkable;

	const FString Top = bTop ? FString::Printf(TEXT("%.1f cm (%s)"), FaceTop, bTopWalkable ? TEXT("walkable") : TEXT("not walkable")) : FString(TEXT("not found"));
	const FString Riser = Rise >= 0.f ? FString::Printf(TEXT(", riser %.1f cm"), Rise) : FString();
	const FVector Pawn = UpdatedComponent->GetComponentLocation();
	const float Speed = static_cast<float>(FVector::VectorPlaneProject(Velocity, GetGravityDirection()).Size());
	UE_LOG(LogGF, Log, TEXT("StairRefused: %s; face top %s above feet%s; actor %s, component %s, mesh %s, element %d; hit at %s, normal %s; pawn at %s, speed %.0f (count %d)"),
		Why, *Top, *Riser, *LastWalkRefusal.Actor, *LastWalkRefusal.Component, *LastWalkRefusal.Mesh, static_cast<int32>(Hit.ElementIndex),
		*Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(), *Pawn.ToCompactString(), Speed, StairRefusalCount);
}

void UGFCharacterMovementComponent::OnRep_StaminaGate()
{
	ApplyStaminaGate();
}

void UGFCharacterMovementComponent::OnRep_FlipTime()
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		ProxyFlipTimeLeft = ReplicatedFlipTimeLeft;
	}
}

void UGFCharacterMovementComponent::PrepareControllerlessWalk()
{
	if (!CharacterOwner || CharacterOwner->GetController() || !bRunPhysicsWithNoController)
	{
		return;
	}
	// Possess calls Restart, which is what switches MOVE_None over to walking. A test pawn is never possessed,
	// so StartNewPhysics never runs and the capsule stays on the approach.
	if (MovementMode == MOVE_None)
	{
		SetDefaultMovementMode();
	}
	if (MaxWalkSpeed <= KINDA_SMALL_NUMBER)
	{
		const float Hub = GetTuning().BaseRunSpeed;
		MaxWalkSpeed = Hub > KINDA_SMALL_NUMBER ? Hub : 500.f;
	}
	if (MaxAcceleration <= KINDA_SMALL_NUMBER)
	{
		MaxAcceleration = 2048.f;
	}
}

void UGFCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	PrepareControllerlessWalk();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	PollAccelHopLanding();
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		ProxyFlipTimeLeft = FMath::Max(0.f, ProxyFlipTimeLeft - DeltaTime);
		ApplyFlipPose(ProxyFlipTimeLeft);
	}
	else
	{
		ApplyFlipPose(Sim.FlipTimeLeft);
	}
}

void UGFCharacterMovementComponent::ApplyFlipPose(float TimeLeft)
{
	USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	if (TimeLeft > 0.f)
	{
		if (!bMeshRestCaptured)
		{
			MeshRestRotation = Mesh->GetRelativeRotation();
			bMeshRestCaptured = true;
		}
		const float Duration = FMath::Max(GetTuning().JumpFlipDuration, 0.05f);
		const float Alpha = FMath::Clamp(1.f - (TimeLeft / Duration), 0.f, 1.f);
		const float Degrees = Alpha * FMath::Max(0.f, GetTuning().JumpFlipDegrees);
		const FQuat Spin(FVector::RightVector, FMath::DegreesToRadians(static_cast<double>(Degrees)));
		Mesh->SetRelativeRotation(Spin * MeshRestRotation.Quaternion());
	}
	else if (bMeshRestCaptured)
	{
		Mesh->SetRelativeRotation(MeshRestRotation);
		bMeshRestCaptured = false;
	}
}

void UGFCharacterMovementComponent::OnRep_AuthoritativeStamina()
{
	bGotAuthoritativeStamina = true;
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		return;
	}
	const bool bOwningClient = CharacterOwner && CharacterOwner->IsLocallyControlled();
	if (!bOwningClient)
	{
		const float Cap = GetMaxStamina();
		Sim.Stamina = FMath::Clamp(AuthoritativeStamina, 0.f, Cap);
		SimAtMoveStart.Stamina = Sim.Stamina;
		return;
	}
	const float Delta = AuthoritativeStamina - Sim.Stamina;
	if (FMath::Abs(Delta) < 1.f)
	{
		return;
	}
	const float Cap = GetMaxStamina();
	Sim.Stamina = FMath::Clamp(Sim.Stamina + Delta, 0.f, Cap);
	SimAtMoveStart.Stamina = FMath::Clamp(SimAtMoveStart.Stamina + Delta, 0.f, Cap);
	if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		for (const FSavedMovePtr& Move : ClientData->SavedMoves)
		{
			if (FSavedMove_GFCharacter* Saved = static_cast<FSavedMove_GFCharacter*>(Move.Get()))
			{
				Saved->StartSim.Stamina = FMath::Clamp(Saved->StartSim.Stamina + Delta, 0.f, Cap);
			}
		}
	}
}

void UGFCharacterMovementComponent::GatherLiveContact(FGFTraversalContact& Contact)
{
	Contact = FGFTraversalContact();
	if (bPendingDashHit)
	{
		Contact.bDashImpact = true;
		Contact.ImpactNormal = PendingImpactNormal;
		Contact.SpeedBefore = PendingSpeedBefore;
		Contact.SpeedAfter = PendingSpeedAfter;
	}
	UWorld* World = GetWorld();
	if (!World || !CharacterOwner)
	{
		return;
	}

	const UGFMovementTuning& Tuning = GetTuning();
	const FVector Center = CharacterOwner->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFTraversal), false, CharacterOwner);
	const float CapsuleRadius = CharacterOwner->GetCapsuleComponent() ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
	const float Reach = CapsuleRadius + Tuning.WallProbeDistance;
	FVector Planar = FVector(Velocity.X, Velocity.Y, 0.0);
	if (Planar.IsNearlyZero())
	{
		Planar = FVector(Acceleration.X, Acceleration.Y, 0.0);
	}
	const FVector Forward = Planar.GetSafeNormal();
	if (!Forward.IsNearlyZero())
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Sides[2] = { Right, Right * -1.0 };
		double Closest = 0.0;
		for (const FVector& Side : Sides)
		{
			if (Side.IsNearlyZero())
			{
				continue;
			}
			FHitResult Hit;
			const FVector End = Center + Side.GetSafeNormal() * static_cast<double>(Reach);
			if (World->SweepSingleByChannel(Hit, Center, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.f), Params)
				&& FMath::Abs(Hit.ImpactNormal.Z) <= static_cast<double>(Tuning.WallNormalMaxZ)
				&& (!Contact.bWallContact || Hit.Distance < Closest))
			{
				Contact.bWallContact = true;
				Contact.WallNormal = Hit.ImpactNormal;
				Closest = Hit.Distance;
			}
		}
	}

	GatherClimbContact(Contact);

	const float HalfHeight = CharacterOwner->GetCapsuleComponent() ? CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeCapsule(CapsuleRadius + 16.f, HalfHeight), Params);
	double BestSwivel = 0.0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Actor = Overlap.GetActor();
		if (!Actor || Actor == CharacterOwner)
		{
			continue;
		}
		const bool bTagged = Actor->ActorHasTag(GFTraversalSim::SwivelTag());
		const FVector BoundsSize = Actor->GetComponentsBoundingBox().GetSize();
		if (!GFTraversalSim::IsSwivelTarget(bTagged, BoundsSize, Tuning))
		{
			continue;
		}
		FVector Origin = Actor->GetComponentsBoundingBox().GetCenter();
		Origin.Z = Center.Z;
		const FVector Flat = Origin - Center;
		const double Dist = Flat.X * Flat.X + Flat.Y * Flat.Y;
		if (!Contact.bSwivelContact || Dist < BestSwivel)
		{
			Contact.bSwivelContact = true;
			Contact.SwivelOrigin = Origin;
			BestSwivel = Dist;
		}
	}

	if ((bWantsGrappleSwing || bYankThisMove) && !Sim.bSwinging && !Sim.bYanking)
	{
		const FVector Aim = CharacterOwner->GetControlRotation().Vector();
		FHitResult Hit;
		const FVector Start = CharacterOwner->GetPawnViewLocation();
		const FVector End = Start + Aim * static_cast<double>(Tuning.GrappleRange);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Contact.bGrappleHit = true;
			Contact.GrapplePoint = Hit.ImpactPoint;
		}
	}
}

void UGFCharacterMovementComponent::GatherClimbContact(FGFTraversalContact& Contact) const
{
	UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule || !UpdatedComponent || bBeatHopRun)
	{
		return;
	}
	const UGFMovementTuning& Tuning = GetTuning();
	FVector Into = Sim.bWallClimbing
		? FVector(-Sim.WallClimbNormal.X, -Sim.WallClimbNormal.Y, 0.0).GetSafeNormal()
		: FVector(Acceleration.X, Acceleration.Y, 0.0).GetSafeNormal();
	if (Into.IsNearlyZero())
	{
		return;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
	const FVector Center = UpdatedComponent->GetComponentLocation();
	const double Feet = Center.Z - static_cast<double>(HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFWallClimb), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	const ECollisionChannel Channel = UpdatedComponent->GetCollisionObjectType();

	// Look ahead above step height, so a stair riser or a one-block step is a step, never a climb.
	constexpr float ProbeSphere = 12.f;
	const double ProbeUp = FMath::Clamp(static_cast<double>(MaxStepHeight + Tuning.WallClimbProbeAboveStep) - static_cast<double>(HalfHeight),
		0.0, static_cast<double>(FMath::Max(0.f, HalfHeight - ProbeSphere)));
	const FVector ProbeFrom = Center + FVector(0.0, 0.0, ProbeUp);
	const FVector ProbeTo = ProbeFrom + Into * static_cast<double>(Radius + Tuning.WallClimbProbeDistance);
	FHitResult Wall;
	if (!World->SweepSingleByChannel(Wall, ProbeFrom, ProbeTo, FQuat::Identity, Channel, FCollisionShape::MakeSphere(ProbeSphere), Params, Response)
		|| Wall.bStartPenetrating
		|| FMath::Abs(Wall.ImpactNormal.Z) > static_cast<double>(Tuning.WallNormalMaxZ)
		|| FVector::DotProduct(FVector(Wall.ImpactNormal.X, Wall.ImpactNormal.Y, 0.0).GetSafeNormal(), Into) > -0.3)
	{
		return;
	}
	Contact.bWallAhead = true;
	Contact.WallAheadNormal = Wall.ImpactNormal;

	// A walkable lip within reach, past the wall face by a capsule, with room to stand and a clear path straight up.
	const FVector FlatNormal = FVector(Wall.ImpactNormal.X, Wall.ImpactNormal.Y, 0.0).GetSafeNormal();
	const FVector LipIn = FlatNormal.IsNearlyZero() ? Into : FlatNormal * -1.0;
	const FVector Over = FVector(Wall.ImpactPoint.X, Wall.ImpactPoint.Y, Center.Z) + LipIn * static_cast<double>(Radius + Tuning.WallClimbMantleInset);
	const FVector LipTop(Over.X, Over.Y, Feet + static_cast<double>(Tuning.WallClimbMantleReach) + 1.0);
	const FVector LipBottom(Over.X, Over.Y, Feet + static_cast<double>(Tuning.WallClimbMantleMinLip));
	FHitResult Lip;
	if (!World->LineTraceSingleByChannel(Lip, LipTop, LipBottom, Channel, Params, Response) || Lip.bStartPenetrating || !IsWalkable(Lip))
	{
		return;
	}
	const FVector Target(Over.X, Over.Y, Lip.ImpactPoint.Z + static_cast<double>(HalfHeight) + 2.0);
	const FCollisionShape Body = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 2.f), FMath::Max(1.f, HalfHeight - 2.f));
	if (World->OverlapBlockingTestByChannel(Target, FQuat::Identity, Channel, Body, Params, Response))
	{
		return;
	}
	const FVector Rise(Center.X, Center.Y, Target.Z);
	if (World->SweepTestByChannel(Center, Rise, FQuat::Identity, Channel, Body, Params, Response))
	{
		return;
	}
	Contact.bLedge = true;
	Contact.LedgeTarget = Target;
	Contact.LedgeHeight = static_cast<float>(Lip.ImpactPoint.Z - Feet);
}

namespace GFCharacterMovementLanding
{
	bool GFMovementFloorNormal(const FVector& Normal, float MinZ)
	{
		return Normal.Z >= static_cast<double>(MinZ);
	}
}

bool UGFCharacterMovementComponent::GetLandingMarker(FVector& OutPoint) const
{
	const FGFLandingTarget Target = GetLandingTarget();
	if (!Target.bValid)
	{
		return false;
	}
	OutPoint = Target.Point;
	return true;
}

FGFLandingTarget UGFCharacterMovementComponent::GetLandingTarget() const
{
	FGFLandingTarget Target;
	if (bBeatHopRun && !GFBeatHopNinja::Allows(BeatHopDifficulty, EGFBeatHopKitMove::LandingMarker, BeatHopProfile))
	{
		return Target;
	}
	if (!CharacterOwner || !GFTraversalSim::ShouldShowLandingMarker(Sim, IsMovingOnGround(), bStaminaEnabled))
	{
		return Target;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return Target;
	}
	const UGFMovementTuning& Tuning = GetTuning();
	// v2 (#342): built from this frame's flight, every frame, steering included. The v1 marker was a pure parabola off
	// the velocity, so air steering never reached it until it had already bent the path, and the ring sat where the
	// jump had aimed it.
	// v3 (#347): v2 was still pinned in play. The only steering it knew was stick input x AirControl, and AirControl is
	// the engine default 0.05 everywhere outside a Beat Hop course (about 100 cm/s^2 of push, half a metre over a whole
	// hop), and the mouse never steered at all. The ring was right: the flight really did not turn. Aim steering
	// (BuildAirSteer) is what turns it now, and the probe below predicts that same turn.
	FGFFlightProbe Probe;
	Probe.Origin = CharacterOwner->GetActorLocation();
	if (const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent())
	{
		Probe.Origin.Z -= static_cast<double>(Capsule->GetScaledCapsuleHalfHeight());
	}
	Probe.Velocity = Velocity;
	// Engine gravity, not GetGravityZ: a swing or a dash zeroes the scale only while it lasts, and the marker shows
	// where you come down after it.
	Probe.GravityZ = Super::GetGravityZ();
	if (!Sim.bSwinging && !Sim.bOverrideVelocity)
	{
		// The falling step adds input acceleration times air control; predict the same push.
		Probe.LateralAccel = FVector(Acceleration.X, Acceleration.Y, 0.0) * static_cast<double>(FMath::Max(0.f, AirControl));
	}
	Probe.MaxLateralSpeed = GetMaxSpeed();
	// v3 (#347): the aim steering the flight is doing, with the aim held where the camera points now. The owner reads
	// its live camera (what its next move will record); anyone else the camera of the last move.
	Probe.Steer = BuildAirSteer(CharacterOwner->IsLocallyControlled() ? LiveCameraForward() : LastCameraForward);
	Probe.Lookahead = Tuning.LandingLookahead;
	Probe.SampleSeconds = Tuning.LandingSampleSeconds;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFLanding), false, CharacterOwner);
	AActor* HitActor = nullptr;
	const float MinNormalZ = Tuning.LandingMinFloorNormalZ;
	const FGFLandingPrediction Landing = GFTraversalSim::PredictFlightLanding(Probe, [World, &Params, &HitActor, MinNormalZ](const FVector& From, const FVector& To, FVector& OutHit)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params)
			&& GFCharacterMovementLanding::GFMovementFloorNormal(Hit.ImpactNormal, MinNormalZ))
		{
			OutHit = Hit.ImpactPoint;
			HitActor = Hit.GetActor();
			return true;
		}
		return false;
	});
	if (!Landing.bHit)
	{
		return Target;
	}
	Target.bValid = true;
	Target.Point = Landing.Point;
	Target.Time = Landing.Time;
	if (AGFAccelHopTile* Tile = Cast<AGFAccelHopTile>(HitActor))
	{
		Target.Tile = Tile;
		const AGFAccelHopRoad* Road = Tile->GetRoad();
		Target.bLit = Road && Road->IsTileLit(Tile);
	}
	return Target;
}
