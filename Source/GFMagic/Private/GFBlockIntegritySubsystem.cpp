#include "GFBlockIntegritySubsystem.h"
#include "GFMagicActors.h"
#include "GFMagicShapes.h"
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
			FString Line = TEXT("gf.Magic.Field");
			for (const FString& Arg : Args)
			{
				Line += TEXT(" ");
				Line += Arg;
			}
			Magic->ExecuteConsole(Line);
		}
	}

	void CmdClaw(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->ExecuteConsole(TEXT("gf.Magic.Claw"));
		}
	}

	void CmdToggle(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->ExecuteConsole(TEXT("gf.Magic.Toggle"));
		}
	}

	void CmdOvercharge(const TArray<FString>&)
	{
		if (UGFMagicComponent* Magic = CommandMagic())
		{
			Magic->ExecuteConsole(TEXT("gf.Magic.Overcharge"));
		}
	}

	void CmdBuy(const TArray<FString>& Args)
	{
		if (Args.Num() > 0)
		{
			if (UGFMagicComponent* Magic = CommandMagic())
			{
				Magic->ExecuteConsole(FString::Printf(TEXT("gf.Magic.Buy %s"), *Args[0]));
			}
		}
	}

	static FAutoConsoleCommand GFieldCmd(TEXT("gf.Magic.Field"), TEXT("Hold the active mining spell. 0 releases it. gf.Magic.Field Spark forces the cone."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdField));
	static FAutoConsoleCommand GClawCmd(TEXT("gf.Magic.Claw"), TEXT("Spend a Clawcall charge."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdClaw));
	static FAutoConsoleCommand GToggleCmd(TEXT("gf.Magic.Toggle"), TEXT("Swap Sparkcone and the faction element."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdToggle));
	static FAutoConsoleCommand GOverCmd(TEXT("gf.Magic.Overcharge"), TEXT("Spend crystal for a short faster spell."), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdOvercharge));
	static FAutoConsoleCommand GBuyCmd(TEXT("gf.Magic.Buy"), TEXT("Buy a tier-1 spell upgrade by id. gf.Magic.Buy SmelterPlus"), FConsoleCommandWithArgsDelegate::CreateStatic(&CmdBuy));
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
	Freeze.Remove(Cell);
	Marks.Remove(Cell);
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
	MiningNow += GFMagic::TickSeconds;
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
		ApplySwings(Caster, Budget, Voxel);
	}
	TArray<FIntVector> Detonate;
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
		const float Cost = Caster->FieldCostPerSecond() * GFMagic::TickSeconds;
		if (Cost > 0.f && Caster->GetCrystalCharge() <= 0.f)
		{
			continue;
		}
		CastField(Caster, Budget, Voxel, Detonate);
	}
	if (Voxel)
	{
		StepGlobs(Voxel);
		StepPuddlesAndSmears(Budget, Voxel);
		StepFreeze(Budget, Voxel, Detonate, nullptr, {}, FIntVector::ZeroValue);
	}
}

FName UGFBlockIntegritySubsystem::ActiveShape(const UGFMagicComponent* Caster) const
{
	if (!Caster)
	{
		return NAME_None;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Caster->GetElement());
	return Spell ? Spell->Shape : NAME_None;
}

void UGFBlockIntegritySubsystem::RememberMark(const FIntVector& Cell, FName Mark, float Seconds)
{
	if (Mark.IsNone() || Seconds <= 0.f)
	{
		return;
	}
	FMagicMark& Slot = Marks.FindOrAdd(Cell);
	Slot.Name = Mark;
	Slot.Until = MiningNow + Seconds;
}

FMagicWorldView UGFBlockIntegritySubsystem::MakeView(AGFPlayerState* PS, AGFVoxelWorld* Voxel) const
{
	FMagicWorldView View;
	View.BlockAt = [Voxel](const FIntVector& Cell) { return Voxel ? Voxel->GetBlock(Cell) : NAME_None; };
	View.IsPlaced = [this](const FIntVector& Cell) { return IsPlaced(Cell); };
	View.IsBlockedLand = [PS, Voxel](const FIntVector& Cell)
	{
		if (!PS || !Voxel)
		{
			return false;
		}
		FString Why;
		const bool bBlocked = !PS->CanMineAt(Voxel->CellToWorld(Cell), Cell, Why);
		if (bBlocked)
		{
			UE_LOG(LogGF, Verbose, TEXT("Magic: %s cannot mine %s (%s)"), *PS->PlayerKey, *Cell.ToString(), *Why);
		}
		return bBlocked;
	};
	View.Hardness = [](FName Id) { return FGFMagicCatalog::BlockHardness(Id); };
	View.DropOf = [](FName Id) { return FGFMagicCatalog::BlockDrop(Id); };
	View.Affinity = [](FName Id) { return FGFMagicCatalog::AffinityOrDefault(Id); };
	return View;
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
	Request.bImpulse = Deferred.bImpulse;
	Request.ImpulsePower = Deferred.ImpulsePower;
	Request.CellScale = Deferred.CellScale;
	Request.ConductiveCap = Deferred.ConductiveCap;
	Request.SmeltChance = Deferred.SmeltChance;
	Request.Dt = GFMagic::TickSeconds;
	Request.IntegrityPerSecond = 100.f;
	if (!Deferred.bFlat && !Deferred.bImpulse)
	{
		if (const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Deferred.Element))
		{
			const float Power = Spell->ConePower > 0.f ? Spell->ConePower : Spell->IntegrityPerSecond;
			Request.IntegrityPerSecond = Power;
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
		const bool bBlocked = !PS->CanMineAt(Voxel->CellToWorld(Cell), Cell, Why);
		if (bBlocked)
		{
			UE_LOG(LogGF, Verbose, TEXT("Magic: %s cannot mine %s (%s)"), *PS->PlayerKey, *Cell.ToString(), *Why);
		}
		return bBlocked;
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
		const double Now = MiningNow;
		if (!Break.TransformTo.IsNone())
		{
			if (!Voxel->ServerApplyEdit(Break.Cell, Break.TransformTo, nullptr, &Reason))
			{
				UE_LOG(LogGF, Warning, TEXT("Magic: quench to %s failed (%s)"), *Break.TransformTo.ToString(), *Reason);
			}
		}
		for (const FString& Key : Break.Contributors)
		{
			if (UGFMagicComponent* Who = FindCaster(Key))
			{
				Who->NoteBreak(Now);
				if (Break.TransformTo.IsNone())
				{
					Who->ReceiveDrop(Break.DropItem, Break.DropCount, From);
				}
			}
		}
	}
}

void UGFBlockIntegritySubsystem::ApplySwings(UGFMagicComponent* Caster, int32& Budget, AGFVoxelWorld* Voxel)
{
	if (!Caster || !Voxel)
	{
		return;
	}
	TArray<FMagicSwing> Swings = MoveTemp(Caster->Swings());
	Caster->Swings().Reset();
	if (Swings.Num() == 0)
	{
		return;
	}
	const FGFMagicModifiers Mods = Caster->Modifiers();
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(EGFMagicElement::Root);
	const float MarkFor = Spell ? Spell->MarkSeconds : 4.f;
	for (const FMagicSwing& Swing : Swings)
	{
		FVector Facing = Swing.bPitch ? Swing.AimDir : Swing.Forward;
		if (Facing.IsNearlyZero())
		{
			Facing = FVector::ForwardVector;
		}
		Facing = Facing.GetSafeNormal();
		const FVector Origin = Caster->CastOrigin() + Facing * FMath::Max(0.f, Swing.LungeCm);
		const FIntVector Feet = Voxel->WorldToCell(Origin);
		FMagicDeferredCast Deferred;
		Deferred.Cells = FGFMagicShapes::MeleeCells(Feet, Facing, Swing.Width, Swing.Height, Swing.Depth);
		Deferred.Element = EGFMagicElement::Root;
		Deferred.bImpulse = true;
		Deferred.ImpulsePower = Swing.Power * (Swing.Rhythm > 0.f ? Swing.Rhythm : 1.f);
		Deferred.Rate = Caster->FieldRate();
		Deferred.bSoilChain = Mods.bSoilChain;
		Deferred.ExtraDrop = Mods.ExtraDrop;
		Deferred.bJackpot = Mods.bJackpot;
		Deferred.bRooted = Swing.bRooted;
		const FName Key = Caster->ContributorKey();
		if (!Key.IsNone())
		{
			Deferred.Contributors.Add(Key.ToString());
		}
		if (Swing.bRooted)
		{
			for (const FIntVector& Cell : Deferred.Cells)
			{
				RememberMark(Cell, TEXT("Rooted"), MarkFor);
			}
		}
		ApplyCast(Caster, MoveTemp(Deferred), Budget, Voxel);
	}
}

void UGFBlockIntegritySubsystem::CastField(UGFMagicComponent* Caster, int32& Budget, AGFVoxelWorld* Voxel, TArray<FIntVector>& Detonate)
{
	if (!Caster || !Voxel)
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Caster->GetElement());
	if (!Spell)
	{
		return;
	}
	const float Cost = Spell->CostPerSecond * GFMagic::TickSeconds;
	if (Cost > 0.f)
	{
		if (Caster->GetCrystalCharge() <= 0.f)
		{
			return;
		}
		Caster->SpendCharge(Cost);
	}
	Caster->SyncMarker(Caster->CastOrigin());
	const FName Shape = Spell->Shape.IsNone() ? FName(TEXT("Cone")) : Spell->Shape;
	if (Shape == TEXT("Spray"))
	{
		SpawnGlobs(Caster, *Spell, Voxel);
		return;
	}
	if (Shape == TEXT("Melee"))
	{
		return;
	}
	const FVector Origin = Caster->CastOrigin();
	const FVector Aim = Caster->GetAimPoint();
	auto BlockAt = [Voxel](const FIntVector& Cell) { return Voxel->GetBlock(Cell); };
	const FIntVector AimCell = FGFMagicRules::ResolveCenter(Origin, Aim, Voxel->GetActorLocation(), BlockAt);
	if (Shape == TEXT("Freeze"))
	{
		TArray<FIntVector> Beam;
		Beam.Add(AimCell);
		const FIntVector Neighbours[6] = {
			AimCell + FIntVector(1, 0, 0), AimCell + FIntVector(-1, 0, 0),
			AimCell + FIntVector(0, 1, 0), AimCell + FIntVector(0, -1, 0),
			AimCell + FIntVector(0, 0, 1), AimCell + FIntVector(0, 0, -1)
		};
		for (const FIntVector& Cell : Neighbours)
		{
			Beam.Add(Cell);
		}
		StepFreeze(Budget, Voxel, {}, Caster, Beam, AimCell);
		return;
	}

	FVector Dir = (Aim - Origin).GetSafeNormal();
	float Half = Spell->ConeHalfAngle;
	if (Caster->IsOvercharged())
	{
		if (const FGFMiningSpellRow* Over = FGFMagicCatalog::Spell(TEXT("Overcharge")))
		{
			Half += FMath::Max(0.f, Over->OverchargeHalfAngle);
		}
	}
	auto Solid = [Voxel](const FIntVector& Cell) { return !Voxel->GetBlock(Cell).IsNone(); };
	auto Blocker = [this, Voxel](const FIntVector& Cell)
	{
		const FName Id = Voxel->GetBlock(Cell);
		return IsPlaced(Cell) || (!Id.IsNone() && IsSpellProof(Id));
	};
	const float Range = Caster->FieldRange();
	const TArray<FMagicConeHit> Hits = FGFMagicShapes::GatherCone(
		Origin, Dir, Range, Half, Spell->Penetration, Spell->CellCap,
		Voxel->GetActorLocation(), Solid, Blocker);

	FMagicDeferredCast Deferred;
	Deferred.Element = Caster->GetElement();
	Deferred.Rate = Caster->FieldRate();
	Deferred.ConductiveCap = Spell->ConductiveCap;
	const FGFMagicModifiers Mods = Caster->Modifiers();
	Deferred.bSoilChain = Mods.bSoilChain;
	Deferred.ExtraDrop = Mods.ExtraDrop;
	Deferred.bJackpot = Mods.bJackpot;
	Deferred.SmeltChance = Mods.SmeltChance;
	const FName Key = Caster->ContributorKey();
	if (!Key.IsNone())
	{
		Deferred.Contributors.Add(Key.ToString());
	}
	for (const FMagicConeHit& Hit : Hits)
	{
		if (const FMagicFreezeCell* Frozen = Freeze.Find(Hit.Cell))
		{
			if (Frozen->bLocked)
			{
				Detonate.AddUnique(Hit.Cell);
				continue;
			}
		}
		Deferred.Cells.Add(Hit.Cell);
		Deferred.CellScale.Add(Hit.Cell, FGFMagicShapes::SparkFalloff(Hit.DistanceCm, Spell->FalloffStartCm, Range, Spell->FalloffMin));
		RememberMark(Hit.Cell, TEXT("Charged"), Spell->MarkSeconds);
	}
	ApplyCast(Caster, MoveTemp(Deferred), Budget, Voxel);
}

void UGFBlockIntegritySubsystem::SpawnGlobs(UGFMagicComponent* Caster, const FGFMiningSpellRow& Spell, AGFVoxelWorld* Voxel)
{
	if (!Caster || !Voxel)
	{
		return;
	}
	const FName Key = Caster->ContributorKey();
	const FString Owner = Key.IsNone() ? FString() : Key.ToString();
	float& Fraction = GlobFraction.FindOrAdd(Owner);
	const float Rate = Spell.GlobRate > 0.f ? Spell.GlobRate : 12.f;
	const float Count = Rate * GFMagic::TickSeconds + Fraction;
	const int32 Spawn = FMath::FloorToInt(Count);
	Fraction = Count - static_cast<float>(Spawn);
	FVector Dir = (Caster->GetAimPoint() - Caster->CastOrigin()).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector(0.f, 0.f, -1.f);
	}
	const FGFMagicModifiers Mods = Caster->Modifiers();
	const float Speed = Spell.GlobSpeed > 0.f ? Spell.GlobSpeed : 1600.f;
	for (int32 Index = 0; Index < Spawn; ++Index)
	{
		FMagicGlob Glob;
		Glob.Pos = Caster->CastOrigin();
		Glob.Vel = Dir * Speed;
		Glob.Volume = Spell.GlobVolume > 0.f ? Spell.GlobVolume : 0.25f;
		Glob.Owner = Owner;
		Glob.Rate = Caster->FieldRate();
		Glob.bSticky = Mods.bSticky;
		Globs.Add(Glob);
	}
}

void UGFBlockIntegritySubsystem::StepGlobs(AGFVoxelWorld* Voxel)
{
	if (!Voxel)
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(EGFMagicElement::Magma);
	if (!Spell)
	{
		Globs.Reset();
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = MiningNow;
	const float Gravity = (World ? World->GetGravityZ() : -980.f) * (Spell->GlobGravityScale > 0.f ? Spell->GlobGravityScale : 0.5f);
	TArray<FMagicGlob> Kept;
	for (const FMagicGlob& Glob : Globs)
	{
		AGFPlayerState* OwnerState = nullptr;
		if (UGFMagicComponent* Who = FindCaster(Glob.Owner))
		{
			OwnerState = Cast<AGFPlayerState>(Who->GetOwner());
		}
		FMagicWorldView View = MakeView(OwnerState, Voxel);
		const FMagicGlobHit Hit = FGFMagicShapes::IntegrateGlob(Glob.Pos, Glob.Vel, GFMagic::TickSeconds, Gravity, Voxel->GetActorLocation(), View.BlockAt);
		if (!Hit.bHit)
		{
			if (Hit.Pos.Z > -4000.f && Hit.Pos.SizeSquared() < FMath::Square(20000.f))
			{
				FMagicGlob Next = Glob;
				Next.Pos = Hit.Pos;
				Next.Vel = Hit.Vel;
				Kept.Add(Next);
			}
			continue;
		}
		TArray<FString> Owners;
		if (!Glob.Owner.IsEmpty())
		{
			Owners.Add(Glob.Owner);
		}
		const bool bWall = !Hit.bTop;
		if (!bWall || Glob.bSticky)
		{
			FIntVector Sink(0, 0, -1);
			if (bWall && Glob.bSticky)
			{
				Sink = FIntVector::ZeroValue;
				const int32 Axis = FMath::Clamp(Hit.EntryAxis, 0, 2);
				Sink[Axis] = Hit.EntryStep == 0 ? -1 : Hit.EntryStep;
				if (Sink.IsZero())
				{
					Sink = FIntVector(0, 0, -1);
				}
			}
			FGFMagicShapes::FeedPuddle(Puddles, Hit.Cell, Glob.Volume, Owners, Sink, Glob.Rate, Now, *Spell, View);
		}
		else
		{
			FMagicSmear Smear;
			Smear.Cell = Hit.Cell;
			Smear.Until = Now + (Spell->SmearSeconds > 0.f ? Spell->SmearSeconds : 2.f);
			Smear.PerSecond = 0.5f * (Spell->PuddleDissolve > 0.f ? Spell->PuddleDissolve : 80.f);
			Smear.Owners = Owners;
			Smear.Rate = Glob.Rate;
			Smears.Add(Smear);
			RememberMark(Hit.Cell, TEXT("Burning"), Spell->SmearSeconds);
			for (int32 Drop = 1; Drop <= 8; ++Drop)
			{
				const FIntVector Below = Hit.Cell + FIntVector(0, 0, -Drop);
				FName BelowId;
				BelowId = View.BlockAt ? View.BlockAt(Below) : NAME_None;
				if (BelowId.IsNone() || BelowId == TEXT("Lava") || IsPlaced(Below) || IsSpellProof(BelowId))
				{
					continue;
				}
				FGFMagicShapes::FeedPuddle(Puddles, Below, Glob.Volume, Owners, FIntVector(0, 0, -1), Glob.Rate, Now, *Spell, View);
				break;
			}
		}
	}
	Globs = MoveTemp(Kept);
}

void UGFBlockIntegritySubsystem::StepPuddlesAndSmears(int32& Budget, AGFVoxelWorld* Voxel)
{
	if (!Voxel)
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(EGFMagicElement::Magma);
	if (!Spell)
	{
		return;
	}
	const double Now = MiningNow;
	float Smelt = 0.f;
	AGFPlayerState* OwnerState = nullptr;
	for (const FMagicPuddle& Puddle : Puddles)
	{
		for (const FString& Owner : Puddle.Owners)
		{
			if (UGFMagicComponent* Who = FindCaster(Owner))
			{
				Smelt = FMath::Max(Smelt, Who->Modifiers().SmeltChance);
				if (!OwnerState)
				{
					OwnerState = Cast<AGFPlayerState>(Who->GetOwner());
				}
			}
		}
	}
	FMagicWorldView View = MakeView(OwnerState, Voxel);
	const FMagicHitOutput PuddleHits = FGFMagicShapes::TickPuddles(Puddles, Wounds, Now, GFMagic::TickSeconds, *Spell, View, Budget, Smelt);
	Budget = FMath::Max(0, Budget - PuddleHits.Spent);
	Commit(Voxel, PuddleHits);
	for (const FMagicPuddle& Puddle : Puddles)
	{
		RememberMark(Puddle.Cell, TEXT("Burning"), Spell->MarkSeconds > 0.f ? Spell->MarkSeconds : 2.f);
	}
	const FMagicHitOutput SmearHits = FGFMagicShapes::TickSmears(Smears, Wounds, Now, GFMagic::TickSeconds, View, Budget, Smelt);
	Budget = FMath::Max(0, Budget - SmearHits.Spent);
	Commit(Voxel, SmearHits);
}

void UGFBlockIntegritySubsystem::StepFreeze(int32& Budget, AGFVoxelWorld* Voxel, const TArray<FIntVector>& Detonate, UGFMagicComponent* BeamCaster, const TArray<FIntVector>& Beam, const FIntVector& AimCell)
{
	if (!Voxel)
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(EGFMagicElement::Frost);
	if (!Spell)
	{
		return;
	}
	UGFMagicComponent* Who = BeamCaster;
	if (!Who)
	{
		for (const TPair<FIntVector, FMagicFreezeCell>& Pair : Freeze)
		{
			if (Pair.Value.Contributors.Num() > 0)
			{
				Who = FindCaster(Pair.Value.Contributors[0]);
				if (Who)
				{
					break;
				}
			}
		}
	}
	float Rate = 1.f;
	int32 Quench = Spell->QuenchRadius;
	int32 Bonus = 0;
	TArray<FString> Contributors;
	if (Who)
	{
		Rate = Who->FieldRate();
		const FGFMagicModifiers Mods = Who->Modifiers();
		if (Mods.QuenchRadius > 0)
		{
			Quench = FMath::Max(Quench, Mods.QuenchRadius);
		}
		Bonus = Mods.FrostBonus;
		const FName Key = Who->ContributorKey();
		if (!Key.IsNone())
		{
			Contributors.Add(Key.ToString());
		}
	}
	const double Now = MiningNow;
	FMagicWorldView View = MakeView(Who ? Cast<AGFPlayerState>(Who->GetOwner()) : nullptr, Voxel);
	const FMagicHitOutput Out = FGFMagicShapes::TickFreeze(
		Freeze, FreezeChains, Beam, Detonate, AimCell, Contributors, Now, GFMagic::TickSeconds,
		Rate, Quench, Bonus, *Spell, View, NextChainId);
	Budget = FMath::Max(0, Budget - Out.Breaks.Num());
	Commit(Voxel, Out);
	for (const TPair<FIntVector, FMagicFreezeCell>& Pair : Freeze)
	{
		if (Pair.Value.bLocked)
		{
			RememberMark(Pair.Key, TEXT("Frozen"), Spell->ContractionSeconds);
		}
	}
}

void UGFBlockIntegritySubsystem::CastClaw(UGFMagicComponent* Caster)
{
	AGFVoxelWorld* Voxel = FindVoxel();
	if (!Caster)
	{
		UE_LOG(LogGF, Verbose, TEXT("Magic: claw cast skipped, no caster"));
		return;
	}
	if (!Voxel)
	{
		UE_LOG(LogGF, Verbose, TEXT("Magic: claw cast skipped, no voxel"));
		return;
	}
	if (!Caster->GetOwner() || !Caster->GetOwner()->HasAuthority())
	{
		UE_LOG(LogGF, Verbose, TEXT("Magic: claw cast skipped, no authority"));
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
	UE_LOG(LogGF, Verbose, TEXT("Magic: claw from %s toward %s starts at %s (%d cells, dir %s)"),
		*Origin.ToString(), *Aim.ToString(), *Start.ToString(), Cells.Num(), *Dir.ToString());
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
