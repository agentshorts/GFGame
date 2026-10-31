#include "GFMagicRules.h"
#include "GFMagicTables.h"
#include "GFVoxelTypes.h"
#include "Engine/DataTable.h"

namespace
{
	void AddUnique(TArray<FString>& List, const FString& Key)
	{
		if (!List.Contains(Key))
		{
			List.Add(Key);
		}
	}

	FIntVector CellOf(const FVector& World, const FVector& VoxelOrigin)
	{
		const FVector Local = (World - VoxelOrigin) / GFBlockSize;
		return FIntVector(FMath::FloorToInt(Local.X), FMath::FloorToInt(Local.Y), FMath::FloorToInt(Local.Z));
	}

	void Orthogonal(const FIntVector& Cell, FIntVector (&Out)[6])
	{
		Out[0] = Cell + FIntVector(1, 0, 0);
		Out[1] = Cell + FIntVector(-1, 0, 0);
		Out[2] = Cell + FIntVector(0, 1, 0);
		Out[3] = Cell + FIntVector(0, -1, 0);
		Out[4] = Cell + FIntVector(0, 0, 1);
		Out[5] = Cell + FIntVector(0, 0, -1);
	}
}

float FGFMagicRules::IntegrityOf(float Hardness)
{
	return FMath::Max(0.f, Hardness) * 100.f;
}

bool FGFMagicRules::SmeltProcs(const FIntVector& Cell, float Chance)
{
	if (Chance <= 0.f)
	{
		return false;
	}
	if (Chance >= 1.f)
	{
		return true;
	}
	const uint32 Hash = HashCombine(GetTypeHash(Cell.X), HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Cell.Z)));
	return (Hash % 1000u) < static_cast<uint32>(Chance * 1000.f);
}

float FGFMagicRules::ChainScale(int32 Count, int32 Peak, float Bonus)
{
	if (Count <= 1 || Peak <= 0 || Bonus <= 0.f)
	{
		return 1.f;
	}
	const float T = FMath::Clamp(static_cast<float>(Count) / static_cast<float>(Peak), 0.f, 1.f);
	return 1.f + Bonus * T;
}

float FGFMagicRules::Multiplier(const FGFBlockAffinityRow& Affinity, EGFMagicElement Element)
{
	switch (Element)
	{
	case EGFMagicElement::Magma: return Affinity.Magma;
	case EGFMagicElement::Root: return Affinity.Root;
	case EGFMagicElement::Frost: return Affinity.Frost;
	default: return Affinity.Spark;
	}
}

FLinearColor FGFMagicRules::ElementColor(EGFMagicElement Element)
{
	switch (Element)
	{
	case EGFMagicElement::Magma: return FLinearColor(0.851f, 0.329f, 0.169f, 1.f);
	case EGFMagicElement::Root: return FLinearColor(0.30f, 0.62f, 0.28f, 1.f);
	case EGFMagicElement::Frost: return FLinearColor(0.62f, 0.82f, 0.95f, 1.f);
	default: return FLinearColor(0.95f, 0.86f, 0.45f, 1.f);
	}
}

FName FGFMagicRules::PathId(EGFPath Path)
{
	switch (Path)
	{
	case EGFPath::Delver: return TEXT("Delver");
	case EGFPath::Wrangler: return TEXT("Wrangler");
	case EGFPath::Scavenger: return TEXT("Scavenger");
	case EGFPath::UnnamedFourth: return TEXT("UnnamedFourth");
	case EGFPath::UnnamedFifth: return TEXT("UnnamedFifth");
	default: return NAME_None;
	}
}

EGFMagicElement FGFMagicRules::FactionElement(FName FactionId)
{
	const FGFClawcallFormRow* Row = FGFMagicCatalog::Form(FactionId);
	return Row ? Row->Element : EGFMagicElement::Spark;
}

FName FGFMagicRules::ClawForm(FName FactionId)
{
	const FGFClawcallFormRow* Row = FGFMagicCatalog::Form(FactionId);
	return Row && !Row->Form.IsNone() ? Row->Form : FName(TEXT("Crystal"));
}

TArray<FIntVector> FGFMagicRules::FieldCells(const FIntVector& Center, int32 Radius)
{
	TArray<FIntVector> Out;
	const int32 R = FMath::Max(0, Radius);
	const int32 Limit = R * R;
	for (int32 Z = -R; Z <= R; ++Z)
	{
		for (int32 Y = -R; Y <= R; ++Y)
		{
			for (int32 X = -R; X <= R; ++X)
			{
				if (X * X + Y * Y + Z * Z <= Limit)
				{
					Out.Add(Center + FIntVector(X, Y, Z));
				}
			}
		}
	}
	return Out;
}

TArray<FIntVector> FGFMagicRules::ClawTunnel(const FIntVector& Start, const FVector& Dir, int32 Length, int32 Width, int32 Height)
{
	TArray<FIntVector> Out;
	if (Length <= 0 || Width <= 0 || Height <= 0)
	{
		return Out;
	}
	FVector D = Dir.GetSafeNormal();
	if (D.IsNearlyZero())
	{
		D = FVector::ForwardVector;
	}
	int32 Axis = 0;
	float Best = -1.f;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Abs = FMath::Abs(D[Index]);
		if (Abs > Best)
		{
			Best = Abs;
			Axis = Index;
		}
	}
	const int32 Sign = D[Axis] >= 0.f ? 1 : -1;
	const int32 WidthAxis = Axis == 0 ? 1 : 0;
	const int32 HeightAxis = Axis == 2 ? 1 : 2;
	const int32 WidthOrigin = -(Width / 2);
	Out.Reserve(Length * Width * Height);
	for (int32 Step = 0; Step < Length; ++Step)
	{
		for (int32 W = 0; W < Width; ++W)
		{
			for (int32 H = 0; H < Height; ++H)
			{
				FIntVector Cell = Start;
				Cell[Axis] += Sign * Step;
				Cell[WidthAxis] += WidthOrigin + W;
				Cell[HeightAxis] += H;
				Out.Add(Cell);
			}
		}
	}
	return Out;
}

FVector FGFMagicRules::ClampRange(const FVector& Origin, const FVector& Point, float RangeCm)
{
	const FVector Delta = Point - Origin;
	const float Range = FMath::Max(0.f, RangeCm);
	if (Delta.SizeSquared() <= Range * Range)
	{
		return Point;
	}
	return Origin + Delta.GetSafeNormal() * Range;
}

FIntVector FGFMagicRules::ResolveCenter(const FVector& Origin, const FVector& Point, const FVector& VoxelOrigin, TFunctionRef<FName(const FIntVector&)> BlockAt)
{
	const FVector Delta = Point - Origin;
	if (Delta.IsNearlyZero())
	{
		return CellOf(Point, VoxelOrigin);
	}
	const float Length = Delta.Size();
	const FVector Dir = Delta / Length;
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / (GFBlockSize * 0.5f)));
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector At = Origin + Dir * (Length * static_cast<float>(Step) / static_cast<float>(Steps));
		const FIntVector Cell = CellOf(At, VoxelOrigin);
		if (!BlockAt(Cell).IsNone())
		{
			return Cell;
		}
	}
	return CellOf(Point, VoxelOrigin);
}

FMagicHitOutput FGFMagicRules::Apply(TMap<FIntVector, FMagicWound>& Wounds, const FMagicHitRequest& Request, const FMagicWorldView& View)
{
	FMagicHitOutput Out;
	TArray<FIntVector> Queue = Request.Cells;
	TSet<FIntVector> Seen;
	Seen.Reserve(Queue.Num());
	for (const FIntVector& Cell : Queue)
	{
		Seen.Add(Cell);
	}
	TSet<FIntVector> Instant;
	int32 Budget = FMath::Max(0, Request.Budget);
	int32 ConductiveLeft = Request.ConductiveCap;
	int32 Index = 0;
	auto Consider = [&](const FIntVector& Next, bool bInstant)
	{
		if (Seen.Contains(Next))
		{
			if (bInstant)
			{
				Instant.Add(Next);
			}
			return;
		}
		Seen.Add(Next);
		Queue.Add(Next);
		if (bInstant)
		{
			Instant.Add(Next);
		}
	};

	while (Index < Queue.Num())
	{
		if (Budget <= 0)
		{
			for (int32 Deferred = Index; Deferred < Queue.Num(); ++Deferred)
			{
				Out.Deferred.Add(Queue[Deferred]);
			}
			break;
		}
		const FIntVector Cell = Queue[Index++];
		const FName BlockId = View.BlockAt ? View.BlockAt(Cell) : NAME_None;
		if (BlockId.IsNone())
		{
			continue;
		}
		const bool bPlaced = View.IsPlaced && View.IsPlaced(Cell);
		const FGFBlockAffinityRow Affinity = View.Affinity ? View.Affinity(BlockId) : FGFMagicCatalog::AffinityOrDefault(BlockId);
		if (bPlaced || Affinity.bSpellProof)
		{
			continue;
		}
		if (View.IsBlockedLand && View.IsBlockedLand(Cell))
		{
			continue;
		}
		if (Request.bFlat && Affinity.bClawcallImmune)
		{
			continue;
		}
		const float Mult = Multiplier(Affinity, Request.Element);
		if (Mult <= 0.f)
		{
			continue;
		}

		FMagicWound& Wound = Wounds.FindOrAdd(Cell);
		if (!Wound.bStarted)
		{
			const float Hardness = View.Hardness ? View.Hardness(BlockId) : FGFMagicCatalog::BlockHardness(BlockId);
			Wound.Remaining = IntegrityOf(Hardness);
			Wound.bStarted = true;
		}
		if (Wound.Remaining <= 0.f)
		{
			continue;
		}
		--Budget;
		for (const FString& Who : Request.Contributors)
		{
			AddUnique(Wound.Contributors, Who);
		}
		const float Scale = Request.CellScale.Contains(Cell) ? Request.CellScale.FindChecked(Cell) : 1.f;
		float Damage = Request.bFlat ? Request.FlatIntegrity : (Request.IntegrityPerSecond * Mult * Request.Rate * Request.Dt * Scale);
		if (Request.bImpulse && !Request.bFlat)
		{
			Damage = Request.ImpulsePower * Mult * Request.Rate * Scale;
		}
		if (Instant.Contains(Cell))
		{
			Damage = FMath::Max(Damage, Wound.Remaining);
		}
		Wound.Remaining -= Damage;

		if (Request.Element == EGFMagicElement::Spark && Affinity.bConductive && View.BlockAt)
		{
			FIntVector Neighbours[6];
			Orthogonal(Cell, Neighbours);
			for (const FIntVector& Next : Neighbours)
			{
				if (Request.ConductiveCap > 0 && ConductiveLeft <= 0)
				{
					break;
				}
				if (View.BlockAt(Next) == BlockId)
				{
					const int32 Before = Queue.Num();
					Consider(Next, false);
					if (Queue.Num() > Before)
					{
						--ConductiveLeft;
					}
				}
			}
		}
		if (Request.bSoilChain && Request.Element == EGFMagicElement::Root && Affinity.bSoil && View.BlockAt)
		{
			FIntVector Neighbours[6];
			Orthogonal(Cell, Neighbours);
			for (const FIntVector& Next : Neighbours)
			{
				const FName NextId = View.BlockAt(Next);
				if (!NextId.IsNone())
				{
					const FGFBlockAffinityRow NextAffinity = View.Affinity ? View.Affinity(NextId) : FGFMagicCatalog::AffinityOrDefault(NextId);
					if (NextAffinity.bSoil)
					{
						Consider(Next, false);
					}
				}
			}
		}

		if (Wound.Remaining > 0.f)
		{
			continue;
		}

		FMagicBreak Break;
		Break.Cell = Cell;
		Break.BlockId = BlockId;
		Break.Contributors = Wound.Contributors;
		if (Request.Element == EGFMagicElement::Frost && !Affinity.FrostTransform.IsNone())
		{
			Break.TransformTo = Affinity.FrostTransform;
			Break.DropCount = 0;
		}
		else
		{
			FName Drop = View.DropOf ? View.DropOf(BlockId) : FGFMagicCatalog::BlockDrop(BlockId);
			if (Request.Element == EGFMagicElement::Magma && !Affinity.MagmaDrop.IsNone())
			{
				Drop = Affinity.MagmaDrop;
			}
			Break.DropItem = Drop;
			int32 Count = 1 + FMath::Max(0, Request.ExtraDrop) + ((Request.bJackpot && Affinity.bOre) ? 1 : 0);
			if (Request.Element == EGFMagicElement::Magma && !Affinity.MagmaDrop.IsNone() && SmeltProcs(Cell, Request.SmeltChance))
			{
				++Count;
			}
			Break.DropCount = Count;
		}
		Out.Breaks.Add(Break);
		// Contributors were copied onto the break. The wound reference is dead after this.
		Wounds.Remove(Cell);

		if (Request.Element == EGFMagicElement::Root && Affinity.bTree && View.BlockAt)
		{
			FIntVector Neighbours[6];
			Orthogonal(Cell, Neighbours);
			for (const FIntVector& Next : Neighbours)
			{
				const FName NextId = View.BlockAt(Next);
				if (NextId.IsNone())
				{
					continue;
				}
				const FGFBlockAffinityRow NextAffinity = View.Affinity ? View.Affinity(NextId) : FGFMagicCatalog::AffinityOrDefault(NextId);
				if (NextAffinity.bTree)
				{
					Consider(Next, true);
				}
			}
		}
	}
	Out.Spent = FMath::Max(0, Request.Budget) - Budget;
	return Out;
}

FGFMagicModifiers FGFMagicRules::ModifiersFor(const TArray<FName>& Owned)
{
	FGFMagicModifiers Mods;
	for (const FName& Id : Owned)
	{
		const FGFSpellUpgradeRow* Row = FGFMagicCatalog::Upgrade(Id);
		if (!Row)
		{
			continue;
		}
		if (Row->Effect == TEXT("RangeCm"))
		{
			Mods.RangeBonusCm += Row->Magnitude;
		}
		else if (Row->Effect == TEXT("ClawLength"))
		{
			Mods.ClawLengthBonus += FMath::RoundToInt(Row->Magnitude);
		}
		else if (Row->Effect == TEXT("IntegrityRate"))
		{
			Mods.IntegrityRate *= Row->Magnitude;
		}
		else if (Row->Effect == TEXT("SoilChain"))
		{
			Mods.bSoilChain = true;
		}
		else if (Row->Effect == TEXT("Stairwell"))
		{
			Mods.bStairwell = true;
		}
		else if (Row->Effect == TEXT("ExtraDrop"))
		{
			Mods.ExtraDrop += FMath::RoundToInt(Row->Magnitude);
		}
		else if (Row->Effect == TEXT("OreGlint"))
		{
			Mods.bOreGlint = true;
		}
		else if (Row->Effect == TEXT("BondCall"))
		{
			Mods.bBondCall = true;
		}
		else if (Row->Effect == TEXT("WorkingDinos"))
		{
			Mods.bWorkingDinos = true;
		}
		else if (Row->Effect == TEXT("Jackpot"))
		{
			Mods.bJackpot = true;
		}
		else if (Row->Effect == TEXT("SmeltBonus"))
		{
			Mods.SmeltChance = FMath::Max(Mods.SmeltChance, Row->Magnitude);
		}
		else if (Row->Effect == TEXT("Sticky"))
		{
			Mods.bSticky = true;
		}
		else if (Row->Effect == TEXT("BarkAxe"))
		{
			Mods.MeleeForm = TEXT("BarkAxe");
		}
		else if (Row->Effect == TEXT("Stonebreaker"))
		{
			Mods.MeleeForm = TEXT("Stonebreaker");
		}
		else if (Row->Effect == TEXT("FrostBonus"))
		{
			Mods.FrostBonus += FMath::RoundToInt(Row->Magnitude);
		}
		else if (Row->Effect == TEXT("QuenchRadius"))
		{
			Mods.QuenchRadius = FMath::Max(Mods.QuenchRadius, FMath::RoundToInt(Row->Magnitude));
		}
	}
	return Mods;
}

FMagicUpgradeDecision FGFMagicRules::TryBuy(const FMagicUpgradeState& State, FName UpgradeId)
{
	FMagicUpgradeDecision Decision;
	Decision.Owned = State.Owned;
	Decision.Bag = State.Bag;
	const FGFSpellUpgradeRow* Row = FGFMagicCatalog::Upgrade(UpgradeId);
	if (!Row)
	{
		Decision.Code = TEXT("Unknown");
		return Decision;
	}
	const bool bFactionTree = Row->Tree == TEXT("Faction");
	const bool bPathTree = Row->Tree == TEXT("Path");
	const bool bOwner = (bFactionTree && Row->Owner == State.FactionId) || (bPathTree && Row->Owner == PathId(State.Path));
	if (!bOwner)
	{
		Decision.Code = TEXT("WrongTree");
		return Decision;
	}
	if (Decision.Owned.Contains(UpgradeId))
	{
		Decision.Code = TEXT("TierPicked");
		return Decision;
	}
	if (const UDataTable* Table = FGFMagicCatalog::UpgradeTable())
	{
		for (const FName& OwnedId : Decision.Owned)
		{
			const FGFSpellUpgradeRow* Owned = Table->FindRow<FGFSpellUpgradeRow>(OwnedId, TEXT("MagicUpgrade"), false);
			if (Owned && Owned->Tree == Row->Tree && Owned->Owner == Row->Owner && Owned->Tier == Row->Tier)
			{
				Decision.Code = TEXT("TierPicked");
				return Decision;
			}
		}
	}
	auto Have = [&Decision](FName Item, int32 Count)
	{
		if (Item.IsNone() || Count <= 0)
		{
			return true;
		}
		const int32* Found = Decision.Bag.Find(Item);
		return Found && *Found >= Count;
	};
	if (!Have(Row->CostItemA, Row->CostCountA) || !Have(Row->CostItemB, Row->CostCountB))
	{
		Decision.Code = TEXT("CantAfford");
		return Decision;
	}
	auto Pay = [&Decision](FName Item, int32 Count)
	{
		if (!Item.IsNone() && Count > 0)
		{
			int32& Left = Decision.Bag.FindOrAdd(Item);
			Left -= Count;
			if (Left <= 0)
			{
				Decision.Bag.Remove(Item);
			}
		}
	};
	Pay(Row->CostItemA, Row->CostCountA);
	Pay(Row->CostItemB, Row->CostCountB);
	Decision.Owned.Add(UpgradeId);
	Decision.Code = TEXT("Ok");
	return Decision;
}

TArray<FIntVector> FGFMagicRules::CapStair(const TArray<FIntVector>& Planned, const FIntVector& FeetCell, const FVector& Dir, int32 Cap)
{
	TArray<FIntVector> Out;
	const FVector Aim = Dir.GetSafeNormal();
	const int32 Limit = FMath::Max(0, Cap);
	for (const FIntVector& Cell : Planned)
	{
		if (Out.Num() >= Limit)
		{
			break;
		}
		const FVector Delta(Cell.X - FeetCell.X, Cell.Y - FeetCell.Y, Cell.Z - FeetCell.Z);
		if (!Aim.IsNearlyZero() && FVector::DotProduct(FVector(Delta.X, Delta.Y, 0.f).GetSafeNormal(), FVector(Aim.X, Aim.Y, 0.f).GetSafeNormal()) < 0.f)
		{
			continue;
		}
		if (FMath::Abs(Delta.X) > 8 || FMath::Abs(Delta.Y) > 8 || Delta.Z > 4 || Delta.Z < -8)
		{
			continue;
		}
		Out.Add(Cell);
	}
	return Out;
}
