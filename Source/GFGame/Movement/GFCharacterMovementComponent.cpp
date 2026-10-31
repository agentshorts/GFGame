#include "GFCharacterMovementComponent.h"
#include "GFKindleRing.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFMovementWorldSubsystem.h"
#include "GFStaminaMode.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Components/CapsuleComponent.h"
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
}

void UGFCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyBaseRunSpeed();
	ApplyJumpTuning();
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
	const bool bNow = GFMovementPolicy::IsStaminaEnabled(GFCharacterMovementDetail::GFMovementMapLabelForPolicy(GetWorld()), bOpted, bValue);
	const uint8 Gate = bNow ? static_cast<uint8>(1) : static_cast<uint8>(2);
	if (bStaminaEnabled != bNow || ReplicatedStaminaGate != Gate)
	{
		bStaminaEnabled = bNow;
		ReplicatedStaminaGate = Gate;
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
		Super::PerformMovement(DeltaTime);
		return;
	}
	const bool bAuthoritative = CharacterOwner->HasAuthority();
	const bool bLocal = CharacterOwner->IsLocallyControlled();
	if (!bAuthoritative && !bLocal)
	{
		StepDeltaSeconds = 0.f;
		Super::PerformMovement(DeltaTime);
		return;
	}

	if (bAuthoritative)
	{
		RefreshStaminaGate();
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
	Sim = GFStaminaSim::Step(Sim, Command, GetTuning());
	// Substeps share this frame's start so a rope, yank, or orbit lands on TravelLocation.
	LocationAtStepStart = Command.PlayerLocation;
	StepDeltaSeconds = DeltaTime;

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
		CharacterOwner->bPressedJump = true;
		CharacterOwner->JumpCurrentCount = 0;
		CharacterOwner->JumpMaxCount = FMath::Max(GetTuning().JumpsPerAirtime, 2);
		JumpZVelocity = Sim.JumpZSpeed;
	}
	else if (bJump)
	{
		CharacterOwner->bPressedJump = false;
	}

	Super::PerformMovement(DeltaTime);
	JumpZVelocity = GetTuning().JumpZVelocity;
	if (CharacterOwner && bStaminaEnabled)
	{
		CharacterOwner->JumpMaxCount = FMath::Max(GetTuning().JumpsPerAirtime, 0);
	}
}

float UGFCharacterMovementComponent::GetMaxSpeed() const
{
	return GFStaminaSim::HorizontalSpeed(Sim, GetTuning(), Super::GetMaxSpeed());
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
	if (!Sim.bJumpAccepted || !CharacterOwner)
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
}

void UGFCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
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

void UGFCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
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

namespace GFCharacterMovementLanding
{
	bool GFMovementFloorNormal(const FVector& Normal, float MinZ)
	{
		return Normal.Z >= static_cast<double>(MinZ);
	}
}

bool UGFCharacterMovementComponent::GetLandingMarker(FVector& OutPoint) const
{
	if (!CharacterOwner || !GFTraversalSim::ShouldShowLandingMarker(Sim, IsMovingOnGround(), bStaminaEnabled))
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const UGFMovementTuning& Tuning = GetTuning();
	const float Sample = FMath::Max(Tuning.LandingSampleSeconds, 0.02f);
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Tuning.LandingLookahead / Sample), 1, 64);
	const float StepTime = Tuning.LandingLookahead / static_cast<float>(Steps);
	FVector Origin = CharacterOwner->GetActorLocation();
	if (const UCapsuleComponent* Capsule = CharacterOwner->GetCapsuleComponent())
	{
		Origin.Z -= static_cast<double>(Capsule->GetScaledCapsuleHalfHeight());
	}
	const float Gravity = Super::GetGravityZ();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFLanding), false, CharacterOwner);
	FVector Previous = Origin;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const FVector Next = GFTraversalSim::BallisticPoint(Origin, Velocity, Gravity, StepTime * static_cast<float>(Index));
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Previous, Next, ECC_Visibility, Params)
			&& GFCharacterMovementLanding::GFMovementFloorNormal(Hit.ImpactNormal, Tuning.LandingMinFloorNormalZ))
		{
			OutPoint = Hit.ImpactPoint;
			return true;
		}
		Previous = Next;
	}
	return false;
}
