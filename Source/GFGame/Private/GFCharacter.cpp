#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFFerryRiderComponent.h"
#include "GFPlayerState.h"
#include "GFGameState.h"
#include "GFLocalProfileSubsystem.h"
#include "GFZoneSubsystem.h"
#include "GFVillainSession.h"
#include "GFSocialDirector.h"
#include "GFSocialComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFLog.h"
#include "GFAssetMap.h"
#include "GFAnimMap.h"
#include "GFSimpleAnimComponent.h"
#include "GFVoxelWorld.h"
#include "GFArenaDirector.h"
#include "Aim/GFAimSim.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

AGFCharacter::AGFCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGFCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Capsule per ASSET_BRIEFS (175 cm player): radius 40, half-height 88.
	GetCapsuleComponent()->InitCapsuleSize(40.f, 88.f);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->JumpZVelocity = 520.f; // matches UGFMovementTuning::JumpZVelocity, clears one block
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	// Walk up one-block (100 cm) steps without jumping. See PlayerMaxStepHeight for the capsule limit.
	GetCharacterMovement()->MaxStepHeight = PlayerMaxStepHeight;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	// Over-shoulder aim camera. Walk, jump, sprint, dash and the grapple are not tuned here.
	SpringArm->TargetArmLength = GFAim::ArmLength;
	SpringArm->SocketOffset = GFAim::ShoulderOffset();
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = GFAim::CameraLagSpeed;
	SpringArm->bEnableCameraRotationLag = false;
	SpringArm->bDoCollisionTest = true;
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	SpringArm->SetRelativeRotation(FRotator(-20.f, 0.f, 0.f));
	SpringArm->bUsePawnControlRotation = true;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	ThirdPersonCamera->FieldOfView = 90.f;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(10.f, 0.f, 70.f)); // eye height ~158 cm
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->FieldOfView = 90.f;
	FirstPersonCamera->SetAutoActivate(false);

	FerryRider = CreateDefaultSubobject<UGFFerryRiderComponent>(TEXT("FerryRider"));

	// Mannequin fallback (feet at the capsule bottom, mesh facing +Y). Player.Base overwrites this when the row loads.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
		MeshComp->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (const FGFAssetMapRow* Row = FGFAssetMap::Find(TEXT("Player.Base")))
		{
			MeshComp->SetRelativeLocation(Row->ArtOffset);
			MeshComp->SetRelativeRotation(Row->ArtRotation);
		}
	}
	SimpleAnim = CreateDefaultSubobject<UGFSimpleAnimComponent>(TEXT("SimpleAnim"));
}

void AGFCharacter::BeginPlay()
{
	Super::BeginPlay();
	FGFAssetMap::ApplySkeletal(GetMesh(), nullptr, TEXT("Player.Base"));
	// A replicated seat can arrive before the mesh exists. Play it once the skeletal row is on.
	if (FerryRider && !FerryRider->GetSeatAnim().IsNone())
	{
		FerryRider->BeginSeat();
	}
	ArmZoom = GFAim::ArmLength;
	SetFirstPerson(false);
	RefreshAimFromProfile();
	if (SelectedBlock.IsNone())
	{
		CycleBlock(0);
	}
}

void AGFCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Re-apply once config and Blueprint defaults have loaded, so a tuned PlayerMaxStepHeight reaches the movement component.
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->MaxStepHeight = PlayerMaxStepHeight;
		Move->ApplyBaseRunSpeed();
		Move->ApplyJumpTuning();
	}
	else if (UCharacterMovementComponent* BaseMove = GetCharacterMovement())
	{
		BaseMove->MaxStepHeight = PlayerMaxStepHeight;
	}
}

UGFCharacterMovementComponent* AGFCharacter::GetGFMovement() const
{
	return Cast<UGFCharacterMovementComponent>(GetCharacterMovement());
}

FKey AGFCharacter::DefaultShoulderSwapKey()
{
	return EKeys::MiddleMouseButton;
}

void AGFCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocallyControlled())
	{
		return;
	}
	RefreshAimFromProfile();
	SyncAimContext();
	GFAimAdvance(AimState, AimTuning, DeltaSeconds);
	ApplyShoulderOffset();
	ApplyAimView();
	UpdateCameraFade();
	UpdateFistPose();
}

void AGFCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshAimFromProfile();
}

void AGFCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshAimFromProfile();
}

void AGFCharacter::SetFirstPerson(bool bEnable)
{
	bFirstPerson = bEnable;
	ApplyAimView();
}

void AGFCharacter::BuildDefaultInput()
{
	auto MakeAction = [this](TObjectPtr<UInputAction>& Slot, const TCHAR* Name, EInputActionValueType Type)
	{
		if (!Slot)
		{
			Slot = NewObject<UInputAction>(this, Name);
			Slot->ValueType = Type;
		}
	};
	const bool bBuildContext = (MappingContext == nullptr);
	MakeAction(MoveAction, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	MakeAction(LookAction, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	MakeAction(JumpAction, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	MakeAction(PlaceAction, TEXT("IA_Place"), EInputActionValueType::Boolean);
	MakeAction(RemoveAction, TEXT("IA_Remove"), EInputActionValueType::Boolean);
	MakeAction(ToggleViewAction, TEXT("IA_ToggleView"), EInputActionValueType::Boolean);
	MakeAction(HotbarAction, TEXT("IA_Hotbar"), EInputActionValueType::Axis1D);
	MakeAction(SprintAction, TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	MakeAction(DashAction, TEXT("IA_Dash"), EInputActionValueType::Boolean);
	MakeAction(GrappleSwingAction, TEXT("IA_GrappleSwing"), EInputActionValueType::Boolean);
	MakeAction(GrappleYankAction, TEXT("IA_GrappleYank"), EInputActionValueType::Boolean);
	MakeAction(ShoulderSwapAction, TEXT("IA_ShoulderSwap"), EInputActionValueType::Boolean);
	if (!bBuildContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Hub"));
	auto Swizzle = [this](FEnhancedActionKeyMapping& M)
	{
		M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this)); // default YXZ: key value goes to Y (forward)
	};
	auto Negate = [this](FEnhancedActionKeyMapping& M)
	{
		M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	};
	// Move: X = right, Y = forward.
	{ FEnhancedActionKeyMapping& M = MappingContext->MapKey(MoveAction, EKeys::W); Swizzle(M); }
	{ FEnhancedActionKeyMapping& M = MappingContext->MapKey(MoveAction, EKeys::S); Swizzle(M); Negate(M); }
	MappingContext->MapKey(MoveAction, EKeys::D);
	{ FEnhancedActionKeyMapping& M = MappingContext->MapKey(MoveAction, EKeys::A); Negate(M); }
	MappingContext->MapKey(LookAction, EKeys::Mouse2D);
	MappingContext->MapKey(JumpAction, EKeys::SpaceBar);
	MappingContext->MapKey(PlaceAction, EKeys::RightMouseButton);
	MappingContext->MapKey(RemoveAction, EKeys::LeftMouseButton);
	MappingContext->MapKey(ToggleViewAction, EKeys::V);
	MappingContext->MapKey(HotbarAction, EKeys::MouseWheelAxis);
	MappingContext->MapKey(SprintAction, EKeys::LeftShift);
	MappingContext->MapKey(DashAction, EKeys::LeftControl);
	MappingContext->MapKey(GrappleSwingAction, EKeys::Q);
	MappingContext->MapKey(GrappleYankAction, EKeys::E);
	MappingContext->MapKey(ShoulderSwapAction, DefaultShoulderSwapKey());
}

void AGFCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	BuildDefaultInput();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(MappingContext, 0);
		}
	}
}

void AGFCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildDefaultInput();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC)
	{
		UE_LOG(LogGF, Error, TEXT("GFCharacter needs EnhancedInputComponent as the default input component class"));
		return;
	}
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AGFCharacter::OnMove);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AGFCharacter::OnLook);
	EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	EIC->BindAction(PlaceAction, ETriggerEvent::Started, this, &AGFCharacter::OnPlace);
	EIC->BindAction(PlaceAction, ETriggerEvent::Completed, this, &AGFCharacter::OnPlaceReleased);
	EIC->BindAction(PlaceAction, ETriggerEvent::Canceled, this, &AGFCharacter::OnPlaceReleased);
	EIC->BindAction(RemoveAction, ETriggerEvent::Started, this, &AGFCharacter::OnRemove);
	EIC->BindAction(ToggleViewAction, ETriggerEvent::Started, this, &AGFCharacter::OnToggleView);
	EIC->BindAction(HotbarAction, ETriggerEvent::Triggered, this, &AGFCharacter::OnHotbar);
	EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &AGFCharacter::OnSprint);
	EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AGFCharacter::OnSprintStopped);
	EIC->BindAction(SprintAction, ETriggerEvent::Canceled, this, &AGFCharacter::OnSprintStopped);
	EIC->BindAction(DashAction, ETriggerEvent::Started, this, &AGFCharacter::OnDash);
	EIC->BindAction(GrappleSwingAction, ETriggerEvent::Started, this, &AGFCharacter::OnGrappleSwing);
	EIC->BindAction(GrappleSwingAction, ETriggerEvent::Completed, this, &AGFCharacter::OnGrappleSwingStopped);
	EIC->BindAction(GrappleSwingAction, ETriggerEvent::Canceled, this, &AGFCharacter::OnGrappleSwingStopped);
	EIC->BindAction(GrappleYankAction, ETriggerEvent::Started, this, &AGFCharacter::OnGrappleYank);
	EIC->BindAction(ShoulderSwapAction, ETriggerEvent::Started, this, &AGFCharacter::OnShoulderSwap);
}

void AGFCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), V.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), V.X);
}

void AGFCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	const float Scale = GetAimLookScale();
	AddControllerYawInput(V.X * Scale);
	AddControllerPitchInput(-V.Y * Scale);
}

void AGFCharacter::OnToggleView(const FInputActionValue&)
{
	SetFirstPerson(!bFirstPerson);
}

void AGFCharacter::OnPlace(const FInputActionValue&)
{
	SyncAimContext();
	if (GFResolvePrimaryUse(AimState.Held, AimState.bSnoutZoom) == EGFTriggerUse::PlaceBlock)
	{
		TryPlaceBlock();
		return;
	}
	PushAim(EGFAimEvent::AimPressed);
}

void AGFCharacter::OnPlaceReleased(const FInputActionValue&)
{
	SyncAimContext();
	if (!AimUser.bHoldToAim || !AimState.bAdsIntent)
	{
		return;
	}
	PushAim(EGFAimEvent::AimReleased);
}

void AGFCharacter::OnRemove(const FInputActionValue&)
{
	SyncAimContext();
	if (GFResolvePrimaryUse(AimState.Held, AimState.bSnoutZoom) == EGFTriggerUse::PlaceBlock)
	{
		TryRemoveBlock();
		return;
	}
	PushAim(EGFAimEvent::FirePressed);
}

void AGFCharacter::OnSprint(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->SetSprintHeld(true);
	}
}

void AGFCharacter::OnSprintStopped(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->SetSprintHeld(false);
	}
}

void AGFCharacter::OnDash(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->NotifyDashPressed();
	}
}

void AGFCharacter::OnGrappleSwing(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->SetGrappleSwingHeld(true);
	}
}

void AGFCharacter::OnGrappleSwingStopped(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->SetGrappleSwingHeld(false);
	}
}

void AGFCharacter::OnGrappleYank(const FInputActionValue&)
{
	if (UGFCharacterMovementComponent* Move = GetGFMovement())
	{
		Move->NotifyGrappleYank();
	}
}

void AGFCharacter::OnHotbar(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (FMath::IsNearlyZero(Axis))
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const bool bAlt = PC && (PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt));
	if (bAlt)
	{
		ArmZoom = FMath::Clamp(ArmZoom + (Axis > 0.f ? GFAim::ArmLengthStep : -GFAim::ArmLengthStep), GFAim::ArmLengthMin, GFAim::ArmLengthMax);
		if (SpringArm)
		{
			SpringArm->TargetArmLength = ArmZoom;
		}
		return;
	}
	CycleBlock(Axis > 0.f ? 1 : -1);
}

void AGFCharacter::OnShoulderSwap(const FInputActionValue&)
{
	PushAim(EGFAimEvent::ShoulderSwap);
}

AGFVoxelWorld* AGFCharacter::FindVoxelWorld() const
{
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFCharacter::TraceCameraAim(FHitResult& OutHit, FVector& OutAimPoint) const
{
	const bool bEye = bFirstPerson || AimState.bAdsIntent || AimState.Phase != EGFAdsPhase::Hip;
	const UCameraComponent* Cam = bEye ? FirstPersonCamera.Get() : ThirdPersonCamera.Get();
	const FVector Dir = (Cam ? Cam->GetForwardVector() : GetControlRotation().Vector()).GetSafeNormal();
	const FVector Start = Cam ? Cam->GetComponentLocation() : GetActorLocation();
	const FVector End = Start + Dir * GFAim::MaxAimRange;
	if (!GetWorld())
	{
		OutAimPoint = End;
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFAimTrace), false, this);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
	OutAimPoint = bHit ? OutHit.ImpactPoint : End;
	return bHit;
}

bool AGFCharacter::TraceBlock(FHitResult& OutHit) const
{
	FVector AimPoint;
	if (!TraceCameraAim(OutHit, AimPoint) || !Cast<AGFVoxelWorld>(OutHit.GetActor()))
	{
		return false;
	}
	return FVector::Dist(GetActorLocation(), OutHit.ImpactPoint) <= BuildReach + 5.f;
}

bool AGFCharacter::TryPlaceBlock()
{
	FHitResult Hit;
	AGFVoxelWorld* Voxel = FindVoxelWorld();
	if (!Voxel || SelectedBlock.IsNone() || SelectedBlock == GFAim::FistsId() || AimState.Held != EGFAimHeldKind::Block || !TraceBlock(Hit))
	{
		return false;
	}
	FIntVector HitCell, Target;
	Voxel->HitToCells(Hit.ImpactPoint, Hit.ImpactNormal, HitCell, Target);

	// Don't build inside yourself.
	const FIntVector Feet = Voxel->WorldToCell(GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.f));
	const FIntVector Head = Voxel->WorldToCell(GetActorLocation() + FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 1.f));
	if (Target == Feet || Target == Head)
	{
		return false;
	}
	// The server validates (reach, permission, items) and applies; clients see it at once (predicted).
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	if (!PS || !Voxel->GetBlock(Target).IsNone())
	{
		return false;
	}
	PS->RequestEdit(Target, SelectedBlock);
	return true;
}

bool AGFCharacter::TryRemoveBlock()
{
	FHitResult Hit;
	AGFVoxelWorld* Voxel = FindVoxelWorld();
	if (!Voxel || !TraceBlock(Hit))
	{
		return false;
	}
	FIntVector HitCell, Adjacent;
	Voxel->HitToCells(Hit.ImpactPoint, Hit.ImpactNormal, HitCell, Adjacent);
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	if (!PS || Voxel->GetBlock(HitCell).IsNone())
	{
		return false;
	}
	PS->RequestEdit(HitCell, NAME_None);
	return true;
}

void AGFCharacter::CycleBlock(int32 Direction)
{
	bHeldOverride = false;
	const TArray<FName> Ids = GetHotbarIds();
	if (Ids.Num() == 0)
	{
		return;
	}
	const int32 Current = Ids.IndexOfByKey(SelectedBlock);
	const int32 Next = Current == INDEX_NONE ? 0 : (Current + Direction + Ids.Num()) % Ids.Num();
	SelectedBlock = Ids[Next];
	SyncAimContext();
}

namespace
{
	EGFVillainPlace VillainPlaceFor(UWorld* World, const AGFPlayerState* PS, const FVector& Location)
	{
		if (!World || !PS)
		{
			return EGFVillainPlace::Wild;
		}
		if (UGFSocialComponent* Social = AGFSocialDirector::GetSocial(World, false))
		{
			if (Social->IsJailed(PS->PlayerKey))
			{
				return EGFVillainPlace::Jail;
			}
			const FName Mode = Social->GetPlayerMode(PS->PlayerKey);
			if (!Mode.IsNone() && Mode != UGFSocialComponent::DefaultPlayerMode && Mode != UGFSocialComponent::ModeName)
			{
				return EGFVillainPlace::MiniGame;
			}
		}
		if (UGFTerritorySubsystem* Territory = World->GetSubsystem<UGFTerritorySubsystem>())
		{
			const FGFTerritoryZoneHit Hit = Territory->GetZoneAt(Location);
			if (Hit.Zone == EGFTerritoryZone::Faction)
			{
				return EGFVillainPlace::Faction;
			}
			if (Hit.Zone == EGFTerritoryZone::Neutral)
			{
				return EGFVillainPlace::Neutral;
			}
			if (Territory->IsSafeFor(PS, Location))
			{
				return EGFVillainPlace::Safe;
			}
		}
		return EGFVillainPlace::Wild;
	}
}

float AGFCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || DamageAmount <= 0.f)
	{
		return Applied;
	}
	AGFPlayerState* Victim = GetPlayerState<AGFPlayerState>();
	if (!Victim)
	{
		return Applied;
	}
	const APawn* InstigatorPawn = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	AGFPlayerState* Attacker = InstigatorPawn && InstigatorPawn != this ? InstigatorPawn->GetPlayerState<AGFPlayerState>() : nullptr;
	if (!Attacker || !GetWorld())
	{
		Victim->NotifyTookDamage(false);
		return Applied;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const EGFVillainPlace VictimPlace = VillainPlaceFor(GetWorld(), Victim, GetActorLocation());
	const EGFVillainPlace AttackerPlace = VillainPlaceFor(GetWorld(), Attacker, InstigatorPawn->GetActorLocation());
	const UDataTable* Items = nullptr;
	if (AGFGameState* GS = Victim->GetGFGameState())
	{
		Items = GS->GetItemTable();
	}
	const FGFVillainHit Landed = FGFVillainSession::Resolve(*Attacker, *Victim, AttackerPlace, VictimPlace, Now, DamageAmount, Items);
	Victim->NotifyTookDamage(Landed.bDamage);
	if (Landed.bDamage)
	{
		Attacker->NotifyTookDamage(true);
	}
	return Landed.bDamage ? Landed.Damage : 0.f;
}

TArray<FName> AGFCharacter::GetHotbarIds() const
{
	TArray<FName> Ids;
	if (const AGFVoxelWorld* Voxel = FindVoxelWorld())
	{
		Ids = Voxel->GetPlaceableBlockIds();
	}
	if (Ids.Num() > 0)
	{
		Ids.Add(GFAim::FistsId());
	}
	return Ids;
}

bool AGFCharacter::IsCrosshairSpreadActive() const
{
	return GetVelocity().Size2D() > 20.f || AimState.SpreadFireLeft > 0.f;
}

void AGFCharacter::EquipAimHeld(EGFAimHeldKind Kind, EGFWeaponProfile Profile)
{
	bHeldOverride = Kind == EGFAimHeldKind::Weapon || Kind == EGFAimHeldKind::Ability;
	AimState.Held = Kind;
	AimState.Weapon = Profile;
	if (Kind == EGFAimHeldKind::Fists)
	{
		bHeldOverride = false;
		SelectedBlock = GFAim::FistsId();
	}
}

void AGFCharacter::RequestAimWeaponSwap()
{
	PushAim(EGFAimEvent::SwapWeapon);
}

void AGFCharacter::RequestAimAbility()
{
	PushAim(EGFAimEvent::UseAbility);
}

void AGFCharacter::SetCrosshairSettings(const FGFCrosshairSettings& In)
{
	Crosshair = In;
	Crosshair.Clamp();
	CommitAimLook();
}

void AGFCharacter::SetAimUserSettings(const FGFAimUserSettings& In)
{
	AimUser = In;
	AimUser.Clamp();
	CommitAimLook();
}

void AGFCharacter::RefreshAimFromProfile()
{
	if (!IsLocallyControlled() || bAimPanelOpen)
	{
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UGFLocalProfileSubsystem* Profile = GI ? GI->GetSubsystem<UGFLocalProfileSubsystem>() : nullptr;
	if (!Profile)
	{
		return;
	}
	const FGFCharacterData& Data = Profile->GetCharacter();
	if (bAimProfileReady && Data.Revision == AimProfileRevision)
	{
		return;
	}
	Crosshair = Data.Crosshair;
	Crosshair.Clamp();
	AimUser = Data.AimUser;
	AimUser.Clamp();
	AimState.bHoldToAim = AimUser.bHoldToAim;
	AimState.SensitivityScale = AimUser.SensitivityScale;
	AimState.bScopeOverlayEnabled = AimUser.bScopeOverlay;
	AimProfileRevision = Data.Revision;
	bAimProfileReady = true;
}

void AGFCharacter::CommitAimLook()
{
	AimState.bHoldToAim = AimUser.bHoldToAim;
	AimState.SensitivityScale = AimUser.SensitivityScale;
	AimState.bScopeOverlayEnabled = AimUser.bScopeOverlay;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGFLocalProfileSubsystem* Profile = GI->GetSubsystem<UGFLocalProfileSubsystem>())
		{
			FGFCharacterData Data = Profile->GetCharacter();
			Data.Crosshair = Crosshair;
			Data.AimUser = AimUser;
			Profile->SetCharacter(Data);
			AimProfileRevision = Data.Revision;
			bAimProfileReady = true;
		}
	}
	if (AGFPlayerState* PS = GetPlayerState<AGFPlayerState>())
	{
		PS->RequestStoreAimLook(Crosshair, AimUser);
	}
}

void AGFCharacter::SyncAimContext()
{
	if (!bHeldOverride)
	{
		AimState.Held = (SelectedBlock == GFAim::FistsId()) ? EGFAimHeldKind::Fists : EGFAimHeldKind::Block;
	}
	AimState.bSnoutZoom = IsAimingDownASnout();
	AimState.bHoldToAim = AimUser.bHoldToAim;
	AimState.SensitivityScale = AimUser.SensitivityScale;
	AimState.bScopeOverlayEnabled = AimUser.bScopeOverlay;
	AimState.bMoving = GetVelocity().Size2D() > 20.f;
}

bool AGFCharacter::IsAimingDownASnout() const
{
	const AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	const UWorld* World = GetWorld();
	if (!PS || !World || PS->PlayerKey.IsEmpty())
	{
		return false;
	}
	for (TActorIterator<AGFArenaDirector> It(World); It; ++It)
	{
		for (const FGFArenaUnitView& Unit : It->Units)
		{
			if (!Unit.bFainted && Unit.PossessedBy == PS->PlayerKey)
			{
				return true;
			}
		}
		break;
	}
	return false;
}

void AGFCharacter::PushAim(EGFAimEvent Event)
{
	const bool bWasFist = AimState.bFistScope;
	const FGFAimStepOut Out = GFAimPush(AimState, AimTuning, Event);
	ApplyShoulderOffset();
	ApplyAimView();
	UpdateFistPose();
	if (Out.bFistCue && !bWasFist)
	{
		PlayFistScopeCue();
	}
	if (Out.bFired)
	{
		ApplyAimShot(Out);
	}
}

void AGFCharacter::ApplyAimView()
{
	const bool bAimCamera = AimState.Phase != EGFAdsPhase::Hip;
	const bool bFp = bFirstPerson || bAimCamera;
	if (FirstPersonCamera)
	{
		FirstPersonCamera->SetActive(bFp);
		FirstPersonCamera->SetFieldOfView(bAimCamera ? AimState.Fov : GFAim::HipFov);
	}
	if (ThirdPersonCamera)
	{
		ThirdPersonCamera->SetActive(!bFp);
		ThirdPersonCamera->SetFieldOfView(GFAim::HipFov);
	}
	if (bUseControllerRotationYaw != bFp)
	{
		bUseControllerRotationYaw = bFp;
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		if (Move->bOrientRotationToMovement != !bFp)
		{
			Move->bOrientRotationToMovement = !bFp;
		}
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetOwnerNoSee(bFp);
	}
}

void AGFCharacter::ApplyShoulderOffset()
{
	if (SpringArm)
	{
		SpringArm->SocketOffset = GFShoulderSocketOffset(AimState, AimTuning);
	}
}

void AGFCharacter::UpdateCameraFade()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || !ThirdPersonCamera)
	{
		return;
	}
	const bool bFp = bFirstPerson || AimState.Phase != EGFAdsPhase::Hip;
	if (bFp)
	{
		MeshComp->SetScalarParameterValueOnMaterials(TEXT("Opacity"), 1.f);
		return;
	}
	const float Distance = FVector::Dist(ThirdPersonCamera->GetComponentLocation(), GetActorLocation());
	const float Alpha = GFCameraFadeAlpha(Distance);
	MeshComp->SetScalarParameterValueOnMaterials(TEXT("Opacity"), Alpha);
	MeshComp->SetOwnerNoSee(Alpha <= 0.02f);
}

void AGFCharacter::UpdateFistPose()
{
	if (!SimpleAnim)
	{
		return;
	}
	if (AimState.bFistScope == bFistPose)
	{
		return;
	}
	bFistPose = AimState.bFistScope;
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (bFistPose)
	{
		if (UAnimSequenceBase* Clip = FGFAnimMap::Load(GFAim::FistScopeAnim()))
		{
			if (MeshComp)
			{
				MeshComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				MeshComp->PlayAnimation(Clip, true);
				MeshComp->SetPlayRate(1.f);
			}
			SimpleAnim->PauseFor(10000.f);
			bFistClipHeld = true;
		}
		return;
	}
	if (bFistClipHeld)
	{
		bFistClipHeld = false;
		SimpleAnim->PauseFor(0.01f);
	}
}

void AGFCharacter::PlayFistScopeCue()
{
	if (!FistScopeSound.IsValid() && !bFistScopeSoundSearched)
	{
		bFistScopeSoundSearched = true;
		FistScopeSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SFX_FistScope.SFX_FistScope"), nullptr, LOAD_NoWarn);
	}
	if (USoundBase* Sound = FistScopeSound.Get())
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

FVector AGFCharacter::AimMuzzleLocation() const
{
	if (const USkeletalMeshComponent* MeshComp = GetMesh())
	{
		static const FName Sockets[] = { TEXT("hand_r"), TEXT("RightHand"), TEXT("Hand_R") };
		for (const FName Socket : Sockets)
		{
			if (MeshComp->DoesSocketExist(Socket))
			{
				return MeshComp->GetSocketLocation(Socket);
			}
		}
	}
	const FVector Forward = GetControlRotation().Vector();
	return GetActorLocation() + Forward * 40.f + FVector(0.f, 0.f, 20.f);
}

void AGFCharacter::ApplyAimShot(const FGFAimStepOut& Shot)
{
	if (!Shot.bFired || !GetWorld())
	{
		return;
	}
	FHitResult CameraHit;
	FVector AimPoint;
	TraceCameraAim(CameraHit, AimPoint);
	const FVector Muzzle = AimMuzzleLocation();
	FHitResult ShotHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFAimShot), false, this);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(ShotHit, Muzzle, AimPoint, ECC_Visibility, Params);
	AActor* Target = bHit ? ShotHit.GetActor() : nullptr;
	if (!Target || Target == this || !Target->IsA<APawn>() || Shot.Damage <= 0.f)
	{
		return;
	}
	const bool bFists = AimState.Held == EGFAimHeldKind::Fists;
	const float Reach = bFists ? AimTuning.FistReach : AimTuning.MaxAimRange;
	if (FVector::Dist(GetActorLocation(), ShotHit.ImpactPoint) > Reach)
	{
		return;
	}
	if (HasAuthority())
	{
		UGameplayStatics::ApplyDamage(Target, Shot.Damage, GetController(), this, nullptr);
	}
	else
	{
		ServerApplyAimDamage(Target, Shot.Damage, bFists);
	}
}

void AGFCharacter::ServerApplyAimDamage_Implementation(AActor* Target, float Damage, bool bFists)
{
	if (!Target || Target == this || !Target->IsA<APawn>())
	{
		return;
	}
	const float Cap = bFists ? GFAim::HipPunchDamage : GFAim::WeaponDamage;
	const float Reach = bFists ? GFAim::FistReach : GFAim::MaxAimRange;
	if (Damage <= 0.f || Damage > Cap + 0.01f)
	{
		return;
	}
	if (FVector::Dist(GetActorLocation(), Target->GetActorLocation()) > Reach + 50.f)
	{
		return;
	}
	UGameplayStatics::ApplyDamage(Target, FMath::Min(Damage, Cap), GetController(), this, nullptr);
}
