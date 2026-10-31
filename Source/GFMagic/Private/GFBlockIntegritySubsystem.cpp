#include "GFBlockIntegritySubsystem.h"
#include "GFMagicActors.h"
#include "GFMagicTables.h"
#include "GFPlayerState.h"
#include "GFGameState.h"
#include "GFVoxelWorld.h"
#include "GFLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	UGFMagicComponent* CommandMagic()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		UGFMagicComponent* Fallback = nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
			{
				continue;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			if (!PC || !PC->IsLocalController() || !PC->PlayerState)
			{
				continue;
			}
			UGFMagicComponent* Magic = PC->PlayerState->FindComponentByClass<UGFMagicComponent>();
			if (!Magic)
			{
				continue;
			}
			if (World->GetNetMode() == NM_Client)
			{
				return Magic;
			}
			Fallback = Magic;
		}
		return Fallback;
	}

	void CmdField(const TArray<FString>& Args)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			const bool bHeld = Args.Num() == 0 || Args[0] != TEXT("0");
			Magic->SetFieldHeld(bHeld);
		}
	}

	void CmdClaw(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->RequestClaw();
		}
	}

	void CmdToggle(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->RequestToggle();
		}
	}

	void CmdOvercharge(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->RequestOvercharge();
		}
	}

	void CmdBuy(const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			if (UGFMagicComponent* Magic = CommandMagic())
			{
				Magic->RequestBuy(FName(*Args[0]));
			}
		}
	}

	static FAutoConsoleCommand GFieldCmd(TEXT("gf.Magic.Field"), TEXT("Hold the sparkfield. 0 releases it. The mining range calls this later."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdField));
	static FAutoConsoleCommand GClawCmd(TEXT("gf.Magic.Claw"), TEXT("Spend a Clawcall charge."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdClaw));
	static FAutoConsoleCommand GToggleCmd(TEXT("gf.Magic.Toggle"), TEXT("Swap Spark and the faction element."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdToggle));
	static FAutoConsoleCommand GOverCmd(TEXT("gf.Magic.Overcharge"), TEXT("Spend crystal for a short faster field."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdOvercharge));
	static FAutoConsoleCommand GBuyCmd(TEXT("gf.Magic.Buy"), TEXT("Buy a tier-1 spell upgrade by id. gf.Magic.Buy LavaLance"), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdBuy));
}

bool UGFBlockIntegritySubsystem::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && !IsTemplate();
}

ETickableTickType UGFBlockIntegritySubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

bool UGFBlockIntegritySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

TStatId UGFBlockIntegritySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFBlockIntegritySubsystem, STATGROUP_Tickables);
}

void UGFBlockIntegritySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (AGFGameState* GS = World->GetGameState<AGFGameState>())
		{
			GS->OnPlayerVoxelEdit.Remove(EditHandle);
			GS->OnBlueprintStamped.Remove(StampHandle);
		}
	}
	bBound = false;
	Super::Deinitialize();
}

void UGFBlockIntegritySubsystem::EnsureBound()
{
	if (bBound)
	{
		return;
	}
	UWorld* World = GetWorld();
	AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	EditHandle = GS->OnPlayerVoxelEdit.AddLambda([this](AGFPlayerState*, FIntVector Cell, FName BlockId)
	{
		if (BlockId.IsNone())
		{
			NoteCleared(Cell);
		}
		else
		{
			NotePlaced(Cell);
		}
	});
	StampHandle = GS->OnBlueprintStamped.AddLambda([this](FIntVector Min, FIntVector Max)
	{
		NotePlacedRegion(Min, Max);
	});
	bBound = true;
}

void UGFBlockIntegritySubsystem::NotePlaced(const FIntVector& Cell)
{
	Placed.Add(Cell);
}

void UGFBlockIntegritySubsystem::NoteCleared(const FIntVector& Cell)
{
	Placed.Remove(Cell);
	Wounds.Remove(Cell);
}

void UGFBlockIntegritySubsystem::NotePlacedRegion(const FIntVector& Min, const FIntVector& Max)
{
	if (Max.X < Min.X || Max.Y < Min.Y || Max.Z < Min.Z)
	{
		return;
	}
	int32 Left = 200000;
	for (int32 Z = Min.Z; Z <= Max.Z && Left > 0; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y && Left > 0; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X && Left > 0; ++X)
			{
				Placed.Add(FIntVector(X, Y, Z));
				--Left;
			}
		}
	}
}

bool UGFBlockIntegritySubsystem::IsPlaced(const FIntVector& Cell) const
{
	return Placed.Contains(Cell);
}

bool UGFBlockIntegritySubsystem::IsSpellProof(FName BlockId) const
{
	return FGFMagicCatalog::AffinityOrDefault(BlockId).bSpellProof;
}

void UGFBlockIntegritySubsystem::SetStairwell(TFunction<TArray<FIntVector>(const APawn*, const FVector&, const AGFVoxelWorld*)> InFn)
{
	StairwellFn = MoveTemp(InFn);
}

AGFVoxelWorld* UGFBlockIntegritySubsystem::FindVoxel() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFVoxelWorld> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

TArray<UGFMagicComponent*> UGFBlockIntegritySubsystem::Casters() const
{
	TArray<UGFMagicComponent*> Out;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Out;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UGFMagicComponent*> Found;
		It->GetComponents(Found);
		for (UGFMagicComponent* Magic : Found)
		{
			if (Magic && Magic->GetOwner() && Magic->GetOwner()->HasAuthority())
			{
				Out.Add(Magic);
			}
		}
	}
	return Out;
}

UGFMagicComponent* UGFBlockIntegritySubsystem::FindCaster(const FString& Key) const
{
	for (UGFMagicComponent* Magic : Casters())
	{
		if (Magic->ContributorKey().ToString() == Key)
		{
			return Magic;
		}
	}
	return nullptr;
}

void UGFBlockIntegritySubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || DeltaTime <= 0.f)
	{
		return;
	}
	Accumulator += DeltaTime;
	int32 Steps = 0;
	while (Accumulator >= GFMagic::TickSeconds && Steps < 3)
	{
		Accumulator -= GFMagic::TickSeconds;
		Step();
		++Steps;
	}
	if (Steps == 3)
	{
		Accumulator = FMath::Min(Accumulator, GFMagic::TickSeconds);
	}
}

void UGFBlockIntegritySubsystem::Advance(float Seconds)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	float Left = Seconds;
	int32 Guard = 0;
	while (Left + KINDA_SMALL_NUMBER >= GFMagic::TickSeconds && Guard < 10000)
	{
		Left -= GFMagic::TickSeconds;
		Step();
		++Guard;
	}
}

void UGFBlockIntegritySubsystem::Step()
{
	EnsureBound();
	AGFVoxelWorld* Voxel = FindVoxel();
	const TArray<UGFMagicComponent*> List = Casters();
	for (UGFMagicComponent* Caster : List)
	{
		Caster->TickMagic(GFMagic::TickSeconds);
	}
	int32 Budget = GFMagic::CellsPerTick;
	for (UGFMagicComponent* Caster : List)
	{
		TArray<FMagicDeferredCast> Pending = MoveTemp(Caster->Deferred());
		Caster->Deferred().Reset();
		for (FMagicDeferredCast& Deferred : Pending)
		{
			ApplyCast(Caster, MoveTemp(Deferred), Budget, Voxel);
		}
	}
	for (UGFMagicComponent* Caster : List)
	{
		if (!Caster->IsFieldHeld())
		{
			Caster->SyncMarker(Caster->GetAimPoint());
			continue;
		}
		if (!Voxel)
		{
			continue;
		}
		const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Caster->GetElement());
		const int32 Radius = Spell ? Spell->RadiusCells : 1;
		const FVector Origin = Caster->CastOrigin();
		const FVector Aim = Caster->GetAimPoint();
		const FIntVector Center = FGFMagicRules::ResolveCenter(Origin, Aim, Voxel->GetActorLocation(), [Voxel](const FIntVector& Cell)
		{
			return Voxel->GetBlock(Cell);
		});
		FMagicDeferredCast Deferred;
		Deferred.Cells = FGFMagicRules::FieldCells(Center, Radius);
		Deferred.Element = Caster->GetElement();
		Deferred.Rate = Caster->FieldRate();
		const FGFMagicModifiers Mods = Caster->Modifiers();
		Deferred.bSoilChain = Mods.bSoilChain;
		Deferred.ExtraDrop = Mods.ExtraDrop;
		Deferred.bJackpot = Mods.bJackpot;
		const FName Key = Caster->ContributorKey();
		if (!Key.IsNone())
		{
			Deferred.Contributors.Add(Key.ToString());
		}
		ApplyCast(Caster, MoveTemp(Deferred), Budget, Voxel);
		Caster->SyncMarker(Voxel->CellToWorld(Center));
	}
}

void UGFBlockIntegritySubsystem::ApplyCast(UGFMagicComponent* Caster, FMagicDeferredCast Deferred, int32& Budget, AGFVoxelWorld* Voxel)
{
	if (!Caster || !Voxel || Deferred.Cells.Num() == 0)
	{
		return;
	}
	if (Budget <= 0)
	{
		Caster->Deferred().Add(MoveTemp(Deferred));
		return;
	}
	AGFPlayerState* PS = Cast<AGFPlayerState>(Caster->GetOwner());
	FMagicHitRequest Request;
	Request.Cells = Deferred.Cells;
	Request.Element = Deferred.Element;
	Request.Contributors = Deferred.Contributors;
	Request.bFlat = Deferred.bFlat;
	Request.FlatIntegrity = Deferred.FlatIntegrity;
	Request.Dt = GFMagic::TickSeconds;
	Request.IntegrityPerSecond = 100.f;
	if (!Deferred.bFlat)
	{
		if (const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Deferred.Element))
		{
			Request.IntegrityPerSecond = Spell->IntegrityPerSecond;
		}
	}
	Request.Rate = Deferred.Rate > 0.f ? Deferred.Rate : 1.f;
	Request.Budget = Budget;
	Request.bSoilChain = Deferred.bSoilChain;
	Request.ExtraDrop = Deferred.ExtraDrop;
	Request.bJackpot = Deferred.bJackpot;

	const TArray<FIntVector> Original = Deferred.Cells;
	FMagicWorldView View;
	View.BlockAt = [Voxel](const FIntVector& Cell) { return Voxel->GetBlock(Cell); };
	View.IsPlaced = [this](const FIntVector& Cell) { return IsPlaced(Cell); };
	View.IsBlockedLand = [PS, Voxel](const FIntVector& Cell)
	{
		if (!PS)
		{
			return false;
		}
		FString Why;
		return !PS->CanMineAt(Voxel->CellToWorld(Cell), Cell, Why);
	};
	View.Hardness = [](FName Id) { return FGFMagicCatalog::BlockHardness(Id); };
	View.DropOf = [](FName Id) { return FGFMagicCatalog::BlockDrop(Id); };
	View.Affinity = [](FName Id) { return FGFMagicCatalog::AffinityOrDefault(Id); };

	const FMagicHitOutput Out = FGFMagicRules::Apply(Wounds, Request, View);
	Budget = FMath::Max(0, Budget - Out.Spent);
	Commit(Voxel, Out);

	TArray<FIntVector> Again;
	if (Deferred.bFlat)
	{
		for (const FIntVector& Cell : Original)
		{
			if (const FMagicWound* Wound = Wounds.Find(Cell))
			{
				if (Wound->Remaining > 0.f)
				{
					Again.Add(Cell);
				}
			}
		}
	}
	for (const FIntVector& Cell : Out.Deferred)
	{
		Again.AddUnique(Cell);
	}
	if (Again.Num() > 0)
	{
		Deferred.Cells = MoveTemp(Again);
		Caster->Deferred().Add(MoveTemp(Deferred));
	}
}

void UGFBlockIntegritySubsystem::Commit(AGFVoxelWorld* Voxel, const FMagicHitOutput& Out)
{
	for (const FMagicBreak& Break : Out.Breaks)
	{
		FString Reason;
		if (!Voxel->ServerApplyEdit(Break.Cell, NAME_None, nullptr, &Reason))
		{
			continue;
		}
		NoteCleared(Break.Cell);
		Wounds.Remove(Break.Cell);
		const FVector From = Voxel->CellToWorld(Break.Cell);
		if (!Break.TransformTo.IsNone())
		{
			if (!Voxel->ServerApplyEdit(Break.Cell, Break.TransformTo, nullptr, &Reason))
			{
				UE_LOG(LogGF, Warning, TEXT("Magic: quench to %s failed (%s)"), *Break.TransformTo.ToString(), *Reason);
			}
			continue;
		}
		for (const FString& Key : Break.Contributors)
		{
			if (UGFMagicComponent* Who = FindCaster(Key))
			{
				Who->ReceiveDrop(Break.DropItem, Break.DropCount, From);
			}
		}
	}
}

void UGFBlockIntegritySubsystem::CastClaw(UGFMagicComponent* Caster)
{
	AGFVoxelWorld* Voxel = FindVoxel();
	if (!Caster || !Voxel || !Caster->GetOwner() || !Caster->GetOwner()->HasAuthority())
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::Spell(TEXT("Clawcall"));
	const FGFMagicModifiers Mods = Caster->Modifiers();
	const int32 Length = (Spell ? Spell->TunnelLength : 6) + Mods.ClawLengthBonus;
	const int32 Width = Spell ? Spell->TunnelWidth : 3;
	const int32 Height = Spell ? Spell->TunnelHeight : 2;
	const FVector Origin = Caster->CastOrigin();
	const FVector Aim = Caster->GetAimPoint();
	FVector Dir = (Aim - Origin).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const FIntVector Start = FGFMagicRules::ResolveCenter(Origin, Aim, Voxel->GetActorLocation(), [Voxel](const FIntVector& Cell)
	{
		return Voxel->GetBlock(Cell);
	});
	TArray<FIntVector> Cells = FGFMagicRules::ClawTunnel(Start, Dir, Length, Width, Height);
	const APawn* Pawn = nullptr;
	if (const APlayerState* PS = Cast<APlayerState>(Caster->GetOwner()))
	{
		Pawn = PS->GetPawn();
	}
	if (Mods.bStairwell && StairwellFn)
	{
		Cells.Append(StairwellFn(Pawn, Aim, Voxel));
	}

	FMagicDeferredCast Deferred;
	Deferred.Cells = Cells;
	Deferred.bFlat = true;
	Deferred.FlatIntegrity = Spell ? Spell->FlatIntegrity : 250.f;
	Deferred.Rate = 1.f;
	Deferred.Element = Caster->GetElement();
	if (const AGFPlayerState* PS = Cast<AGFPlayerState>(Caster->GetOwner()))
	{
		if (const FGFClawcallFormRow* Form = FGFMagicCatalog::Form(PS->FactionId))
		{
			Deferred.Element = Form->Element;
		}
	}
	const FName Key = Caster->ContributorKey();
	if (!Key.IsNone())
	{
		Deferred.Contributors.Add(Key.ToString());
	}
	int32 Budget = GFMagic::CellsPerTick;
	ApplyCast(Caster, Deferred, Budget, Voxel);

	if (Cells.Num() > 0 && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector From = Voxel->CellToWorld(Cells[0]);
		const FVector To = Voxel->CellToWorld(Cells.Last());
		AGFMagicClaw* Claw = GetWorld()->SpawnActor<AGFMagicClaw>(AGFMagicClaw::StaticClass(), From, FRotator::ZeroRotator, Params);
		if (Claw)
		{
			Claw->Setup(From, To, Caster->GetClawForm(), FGFMagicRules::ElementColor(Deferred.Element));
		}
	}
}
