#include "GFCharacter.h"
#include "GFFerryRiderComponent.h"
#include "GFPlayerState.h"
#include "GFZoneSubsystem.h"
#include "GFLog.h"
#include "GFAssetMap.h"
#include "GFSimpleAnimComponent.h"
#include "GFVoxelWorld.h"
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
#include "InputMappingContext.h"
#include "InputModifiers.h"

AGFCharacter::AGFCharacter()
{
	// Capsule per ASSET_BRIEFS (175 cm player): radius 40, half-height 88.
	GetCapsuleComponent()->InitCapsuleSize(40.f, 88.f);

	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->JumpZVelocity = 520.f; // clears one 100 cm block
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	// Walk up one-block (100 cm) steps without jumping. See PlayerMaxStepHeight for the capsule limit.
	GetCharacterMovement()->MaxStepHeight = PlayerMaxStepHeight;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 450.f;
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
	SetFirstPerson(false);
	if (SelectedBlock.IsNone())
	{
		CycleBlock(0);
	}
}

void AGFCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Re-apply once config and Blueprint defaults have loaded, so a tuned PlayerMaxStepHeight reaches the movement component.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxStepHeight = PlayerMaxStepHeight;
	}
}

void AGFCharacter::SetFirstPerson(bool bEnable)
{
	bFirstPerson = bEnable;
	FirstPersonCamera->SetActive(bEnable);
	ThirdPersonCamera->SetActive(!bEnable);
	bUseControllerRotationYaw = bEnable;
	GetCharacterMovement()->bOrientRotationToMovement = !bEnable;
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetOwnerNoSee(bEnable);
	}
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
	EIC->BindAction(RemoveAction, ETriggerEvent::Started, this, &AGFCharacter::OnRemove);
	EIC->BindAction(ToggleViewAction, ETriggerEvent::Started, this, &AGFCharacter::OnToggleView);
	EIC->BindAction(HotbarAction, ETriggerEvent::Triggered, this, &AGFCharacter::OnHotbar);
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
	AddControllerYawInput(V.X);
	AddControllerPitchInput(-V.Y);
}

void AGFCharacter::OnToggleView(const FInputActionValue&)
{
	SetFirstPerson(!bFirstPerson);
}

void AGFCharacter::OnPlace(const FInputActionValue&)
{
	TryPlaceBlock();
}

void AGFCharacter::OnRemove(const FInputActionValue&)
{
	TryRemoveBlock();
}

void AGFCharacter::OnHotbar(const FInputActionValue& Value)
{
	const float Axis = Value.Get<float>();
	if (!FMath::IsNearlyZero(Axis))
	{
		CycleBlock(Axis > 0.f ? 1 : -1);
	}
}

AGFVoxelWorld* AGFCharacter::FindVoxelWorld() const
{
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFCharacter::TraceBlock(FHitResult& OutHit) const
{
	const UCameraComponent* Cam = bFirstPerson ? FirstPersonCamera.Get() : ThirdPersonCamera.Get();
	const FVector Start = Cam->GetComponentLocation();
	const FVector End = Start + GetControlRotation().Vector() * (BuildReach + (bFirstPerson ? 0.f : SpringArm->TargetArmLength));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GFBuildTrace), false, this);
	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)
		&& Cast<AGFVoxelWorld>(OutHit.GetActor()) != nullptr;
}

bool AGFCharacter::TryPlaceBlock()
{
	FHitResult Hit;
	AGFVoxelWorld* Voxel = FindVoxelWorld();
	if (!Voxel || SelectedBlock.IsNone() || !TraceBlock(Hit))
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
	const AGFVoxelWorld* Voxel = FindVoxelWorld();
	const TArray<FName> Ids = Voxel ? Voxel->GetPlaceableBlockIds() : TArray<FName>();
	if (Ids.Num() == 0)
	{
		return;
	}
	const int32 Current = Ids.IndexOfByKey(SelectedBlock);
	const int32 Next = Current == INDEX_NONE ? 0 : (Current + Direction + Ids.Num()) % Ids.Num();
	SelectedBlock = Ids[Next];
}

float AGFCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || DamageAmount <= 0.f)
	{
		return Applied;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	if (!PS)
	{
		return Applied;
	}
	bool bPvP = false;
	const APawn* InstigatorPawn = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	if (InstigatorPawn && InstigatorPawn != this && InstigatorPawn->GetPlayerState())
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGFZoneSubsystem* Zones = World->GetSubsystem<UGFZoneSubsystem>())
			{
				bPvP = Zones->IsPvPAllowedAt(GetActorLocation());
			}
		}
	}
	PS->NotifyTookDamage(bPvP);
	return Applied;
}
