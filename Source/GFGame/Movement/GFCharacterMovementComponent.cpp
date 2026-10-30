#include "GFCharacterMovementComponent.h"
#include "GFKindleRing.h"
#include "GFMovementPolicy.h"
#include "GFMovementTuning.h"
#include "GFMovementWorldSubsystem.h"
#include "GFStaminaMode.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Components/CapsuleComponent.h"
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
	bool bSavedSprint = false;
	bool bSavedDash = false;
	FGFStaminaState StartSim;
	FVector SavedCameraForward = FVector::ForwardVector;

	virtual void Clear() override
	{
		Super::Clear();
		bSavedSprint = false;
		bSavedDash = false;
		StartSim = FGFStaminaState();
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
		StartSim = Move->GetSimAtMoveStart();
		SavedCameraForward = Move->GetLastCameraForward();
		Move->ClearDashRecorded();
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
		return Flags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		const FSavedMove_GFCharacter* Next = static_cast<const FSavedMove_GFCharacter*>(NewMove.Get());
		if (!Next || bSavedDash || Next->bSavedDash || bSavedSprint != Next->bSavedSprint)
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
	// Constructor value matches UGFMovementTuning::BaseRunSpeed. BeginPlay copies the asset, which may override it.
	MaxWalkSpeed = 500.f;
}

void UGFCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	ApplyBaseRunSpeed();
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
}

void UGFCharacterMovementComponent::ApplyBaseRunSpeed()
{
	MaxWalkSpeed = GetTuning().BaseRunSpeed;
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
		Super::PerformMovement(DeltaTime);
		return;
	}
	const bool bAuthoritative = CharacterOwner->HasAuthority();
	const bool bLocal = CharacterOwner->IsLocallyControlled();
	if (!bAuthoritative && !bLocal)
	{
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

	FGFStaminaCommand Command;
	Command.bSprintHeld = bWantsToSprint;
	Command.bDashPressed = bDash;
	Command.bGrounded = IsMovingOnGround();
	Command.bStaminaEnabled = bStaminaEnabled;
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

	if (bAuthoritative)
	{
		const float Quantized = FMath::RoundToFloat(Sim.Stamina * 2.f) * 0.5f;
		if (!FMath::IsNearlyEqual(AuthoritativeStamina, Quantized, 0.01f))
		{
			AuthoritativeStamina = Quantized;
		}
	}

	Super::PerformMovement(DeltaTime);
}

float UGFCharacterMovementComponent::GetMaxSpeed() const
{
	return GFStaminaSim::HorizontalSpeed(Sim, GetTuning(), Super::GetMaxSpeed());
}

float UGFCharacterMovementComponent::GetGravityZ() const
{
	return Super::GetGravityZ() * GFStaminaSim::GravityScale(Sim, GetTuning());
}

void UGFCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (Sim.bDashing)
	{
		const double Speed = static_cast<double>(GetTuning().DashSpeed);
		const FVector Flat = FVector(Sim.DashDir.X, Sim.DashDir.Y, 0.0).GetSafeNormal();
		Velocity.X = Flat.X * Speed;
		Velocity.Y = Flat.Y * Speed;
		return;
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UGFCharacterMovementComponent::OnRep_StaminaGate()
{
	ApplyStaminaGate();
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
