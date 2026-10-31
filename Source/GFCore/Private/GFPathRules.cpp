#include "GFPathRules.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"

int32 FGFPathRules::SlotCount()
{
	return ClassCount;
}

bool FGFPathRules::PathFromSlot(int32 Slot, EGFPath& OutPath)
{
	switch (Slot)
	{
	case 0: OutPath = EGFPath::Delver; return true;
	case 1: OutPath = EGFPath::Wrangler; return true;
	case 2: OutPath = EGFPath::Scavenger; return true;
	case 3: OutPath = EGFPath::UnnamedFourth; return true;
	case 4: OutPath = EGFPath::UnnamedFifth; return true;
	default: return false;
	}
}

int32 FGFPathRules::SlotOf(EGFPath Path)
{
	switch (Path)
	{
	case EGFPath::Delver: return 0;
	case EGFPath::Wrangler: return 1;
	case EGFPath::Scavenger: return 2;
	case EGFPath::UnnamedFourth: return 3;
	case EGFPath::UnnamedFifth: return 4;
	default: return INDEX_NONE;
	}
}

bool FGFPathRules::IsNamed(EGFPath Path)
{
	return Path == EGFPath::Delver || Path == EGFPath::Wrangler || Path == EGFPath::Scavenger;
}

bool FGFPathRules::IsPlayable(EGFPath Path)
{
	return SlotOf(Path) != INDEX_NONE;
}

FString FGFPathRules::DisplayName(EGFPath Path)
{
	switch (Path)
	{
	case EGFPath::Delver: return TEXT("Delver");
	case EGFPath::Wrangler: return TEXT("Wrangler");
	case EGFPath::Scavenger: return TEXT("Scavenger");
	case EGFPath::UnnamedFourth:
	case EGFPath::UnnamedFifth:
		return TEXT("Name pending");
	default:
		return FString();
	}
}

FString FGFPathRules::IconId(EGFPath Path)
{
	switch (Path)
	{
	case EGFPath::Delver: return TEXT("Path.Delver");
	case EGFPath::Wrangler: return TEXT("Path.Wrangler");
	case EGFPath::Scavenger: return TEXT("Path.Scavenger");
	case EGFPath::UnnamedFourth: return TEXT("Path.PendingA");
	case EGFPath::UnnamedFifth: return TEXT("Path.PendingB");
	default: return FString();
	}
}

TArray<FGFPathCard> FGFPathRules::Cards()
{
	TArray<FGFPathCard> Out;
	for (int32 Slot = 0; Slot < ClassCount; ++Slot)
	{
		EGFPath Path = EGFPath::None;
		if (!PathFromSlot(Slot, Path))
		{
			continue;
		}
		FGFPathCard Card;
		Card.Path = Path;
		Card.Name = DisplayName(Path);
		Card.IconId = IconId(Path);
		Card.bNamePending = !IsNamed(Path);
		switch (Path)
		{
		case EGFPath::Delver:
			Card.Blurb = TEXT("Stone breaks faster, the hole goes two layers deeper, and lava and burn hurt half as much. Crystal, Prism Shard, Amber and Fossil turn up for you.");
			break;
		case EGFPath::Wrangler:
			Card.Blurb = TEXT("Taming takes less food and less time, rent is cheaper, and family reputation climbs faster. Scales, Bone, eggs, Hide and Egg Shell turn up for you.");
			break;
		case EGFPath::Scavenger:
			Card.Blurb = TEXT("Loot glints show from twice as far. Salvage, cloth, Rare Bloom and the rare dyes turn up for you.");
			break;
		default:
			Card.Blurb = TEXT("This class is open. Its name is not set yet. Its finds still count toward the endgame.");
			break;
		}
		Out.Add(Card);
	}
	return Out;
}

float FGFPathRules::MoveSpeedMultiplier(EGFPath)
{
	return 1.f;
}

float FGFPathRules::DigSpeedMultiplier(EGFPath Path)
{
	return Path == EGFPath::Delver ? DelverDigSpeed : 1.f;
}

int32 FGFPathRules::ExtraDigLayers(EGFPath Path)
{
	return Path == EGFPath::Delver ? DelverExtraDepth : 0;
}

float FGFPathRules::HazardDamageMultiplier(EGFPath Path)
{
	return Path == EGFPath::Delver ? DelverHazardDamage : 1.f;
}

float FGFPathRules::TameFoodMultiplier(EGFPath Path)
{
	return Path == EGFPath::Wrangler ? WranglerTameScale : 1.f;
}

float FGFPathRules::TameTimeMultiplier(EGFPath Path)
{
	return Path == EGFPath::Wrangler ? WranglerTameScale : 1.f;
}

float FGFPathRules::WarningTimeMultiplier(EGFPath Path)
{
	return Path == EGFPath::Wrangler ? WranglerWarningScale : 1.f;
}

float FGFPathRules::RentMultiplier(EGFPath Path)
{
	return Path == EGFPath::Wrangler ? WranglerRentScale : 1.f;
}

float FGFPathRules::RepGainMultiplier(EGFPath Path)
{
	return Path == EGFPath::Wrangler ? WranglerRepScale : 1.f;
}

float FGFPathRules::GlintRangeMultiplier(EGFPath Path)
{
	return Path == EGFPath::Scavenger ? ScavengerGlintScale : 1.f;
}

int32 FGFPathRules::CrewSlotBonus(EGFPath)
{
	return HeldCrewSlots;
}

float FGFPathRules::CartBonus(EGFPath)
{
	return HeldCartBonus;
}

float FGFPathRules::KeepChance(EGFPath Finder, EGFPath ItemPath, EGFPathLootSource Source)
{
	if (Source != EGFPathLootSource::OpenWorld || ItemPath == EGFPath::None || Finder == ItemPath)
	{
		return 1.f;
	}
	return OffPathRate;
}

bool FGFPathRules::KeepsDrop(EGFPath Finder, EGFPath ItemPath, EGFPathLootSource Source, FRandomStream& Rng)
{
	const float Chance = KeepChance(Finder, ItemPath, Source);
	if (Chance >= 1.f)
	{
		return true;
	}
	if (Chance <= 0.f)
	{
		return false;
	}
	return Rng.FRand() < Chance;
}

bool FGFPathRules::CanHold(EGFPath, EGFPath)
{
	return true;
}

bool FGFPathRules::CanTransfer(EGFPath, EGFPath)
{
	return true;
}

namespace
{
	void CollectPaths(const FGFInventory& Bag, const UDataTable* Items, TSet<EGFPath>& Out)
	{
		for (const FGFItemStack& Stack : Bag.Items)
		{
			if (Stack.Count <= 0)
			{
				continue;
			}
			const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
			if (Row && Row->PathTag != EGFPath::None)
			{
				Out.Add(Row->PathTag);
			}
		}
	}

	bool TakeOne(FGFInventory& Bag, const UDataTable* Items, EGFPath Path)
	{
		for (const FGFItemStack& Stack : Bag.Items)
		{
			if (Stack.Count <= 0)
			{
				continue;
			}
			const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
			if (Row && Row->PathTag == Path)
			{
				return Bag.Remove(Stack.ItemId, 1);
			}
		}
		return false;
	}
}

int32 FGFPathRules::DistinctPaths(const TArray<FGFItemStack>& Stacks, const UDataTable* Items)
{
	FGFInventory Bag;
	Bag.Items = Stacks;
	TSet<EGFPath> Paths;
	CollectPaths(Bag, Items, Paths);
	return Paths.Num();
}

int32 FGFPathRules::DistinctPaths(const TArray<FGFInventory>& Bags, const UDataTable* Items)
{
	TSet<EGFPath> Paths;
	for (const FGFInventory& Bag : Bags)
	{
		CollectPaths(Bag, Items, Paths);
	}
	return Paths.Num();
}

bool FGFPathRules::MeetsEndgame(const TArray<FGFInventory>& Bags, const UDataTable* Items)
{
	return Items && DistinctPaths(Bags, Items) >= EndgamePaths;
}

bool FGFPathRules::SpendEndgame(TArray<FGFInventory>& Bags, const UDataTable* Items)
{
	if (!MeetsEndgame(Bags, Items))
	{
		return false;
	}
	TSet<EGFPath> Have;
	for (const FGFInventory& Bag : Bags)
	{
		CollectPaths(Bag, Items, Have);
	}
	TArray<EGFPath> Order;
	for (int32 Slot = 0; Slot < ClassCount && Order.Num() < EndgamePaths; ++Slot)
	{
		EGFPath Path = EGFPath::None;
		if (PathFromSlot(Slot, Path) && Have.Contains(Path))
		{
			Order.Add(Path);
		}
	}
	if (Order.Num() < EndgamePaths)
	{
		return false;
	}

	TArray<FGFInventory> Copy = Bags;
	for (EGFPath Path : Order)
	{
		bool bTook = false;
		for (FGFInventory& Bag : Copy)
		{
			if (TakeOne(Bag, Items, Path))
			{
				bTook = true;
				break;
			}
		}
		if (!bTook)
		{
			return false;
		}
	}
	Bags = MoveTemp(Copy);
	return true;
}

FGFPathChange FGFPathRules::Evaluate(const FGFPathRecord& Record, EGFPath NewPath, int64 NowUnix, int32 AvailableValue)
{
	FGFPathChange Change;
	if (!IsPlayable(NewPath))
	{
		Change.Code = TEXT("BadPath");
		return Change;
	}
	if (!Record.bChosen)
	{
		Change.bOk = true;
		Change.Code = TEXT("Ok");
		return Change;
	}
	if (Record.Path == NewPath)
	{
		Change.Code = TEXT("SamePath");
		return Change;
	}
	if (Record.Respecs < FreeRespecs)
	{
		Change.bOk = true;
		Change.Code = TEXT("Ok");
		return Change;
	}
	if (Record.RespecUnix > 0 && NowUnix < Record.RespecUnix + RespecCooldownSeconds)
	{
		Change.Code = TEXT("Cooldown");
		return Change;
	}
	Change.CostValue = RespecCostValue;
	if (AvailableValue < RespecCostValue)
	{
		Change.Code = TEXT("Cost");
		return Change;
	}
	Change.bOk = true;
	Change.Code = TEXT("Ok");
	return Change;
}

void FGFPathRules::Commit(FGFPathRecord& Record, EGFPath NewPath, int64 NowUnix)
{
	const bool bFirst = !Record.bChosen;
	Record.Path = NewPath;
	Record.bChosen = true;
	if (!bFirst)
	{
		Record.Respecs += 1;
		Record.RespecUnix = NowUnix;
	}
}

int32 FGFPathRules::TradeableValue(const UDataTable* Items, const FGFInventory& Bag)
{
	int32 Total = 0;
	for (const FGFItemStack& Stack : Bag.Items)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
		if (!Row || Row->bAccountBound || Row->Value <= 0 || Stack.Count <= 0)
		{
			continue;
		}
		if (Row->Category != EGFItemCategory::Material && Row->Category != EGFItemCategory::Treat
			&& Row->Category != EGFItemCategory::Seed && Row->Category != EGFItemCategory::Block)
		{
			continue;
		}
		Total += Row->Value * Stack.Count;
	}
	return Total;
}

bool FGFPathRules::PayValue(const UDataTable* Items, FGFInventory& Bag, int32 Cost)
{
	if (Cost <= 0)
	{
		return true;
	}
	if (TradeableValue(Items, Bag) < Cost)
	{
		return false;
	}
	struct FPile
	{
		FName Id;
		int32 Value = 0;
		int32 Count = 0;
	};
	TArray<FPile> Piles;
	for (const FGFItemStack& Stack : Bag.Items)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
		if (!Row || Row->bAccountBound || Row->Value <= 0 || Stack.Count <= 0)
		{
			continue;
		}
		if (Row->Category != EGFItemCategory::Material && Row->Category != EGFItemCategory::Treat
			&& Row->Category != EGFItemCategory::Seed && Row->Category != EGFItemCategory::Block)
		{
			continue;
		}
		FPile Pile;
		Pile.Id = Stack.ItemId;
		Pile.Value = Row->Value;
		Pile.Count = Stack.Count;
		Piles.Add(Pile);
	}
	Piles.Sort([](const FPile& A, const FPile& B)
	{
		if (A.Value != B.Value)
		{
			return A.Value < B.Value;
		}
		return A.Id.LexicalLess(B.Id);
	});

	FGFInventory Next = Bag;
	int32 Left = Cost;
	for (const FPile& Pile : Piles)
	{
		if (Left <= 0)
		{
			break;
		}
		const int32 Need = (Left + Pile.Value - 1) / Pile.Value;
		const int32 Take = FMath::Min(Pile.Count, Need);
		if (Take > 0 && Next.Remove(Pile.Id, Take))
		{
			Left -= Take * Pile.Value;
		}
	}
	if (Left > 0)
	{
		return false;
	}
	Bag = Next;
	return true;
}
