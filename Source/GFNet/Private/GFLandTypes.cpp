#include "GFLandTypes.h"
#include "GFEconomy.h"
#include "Engine/DataTable.h"

int32 FGFLandState::CountPlotsOf(const FString& Key) const
{
	int32 N = 0;
	for (const FGFLandPlot& P : Plots)
	{
		N += (!Key.IsEmpty() && P.TenantKey == Key) ? 1 : 0;
	}
	return N;
}

FGFLandState FGFLandRules::MakeDefaultLayout(const FIntVector& WorldSize)
{
	// 2 x 2: Home | PlotA / PlotB | Commons. 64 wide -> margin 4, plots 24, gap 8.
	const int32 Margin = 4;
	const int32 Gap = 8;
	const int32 Edge = FMath::Clamp(FMath::Min(WorldSize.X, WorldSize.Y) / 2 - Margin - Gap / 2, 4, PlotEdge);
	const int32 Far = Margin + Edge + Gap;
	FGFLandState Land;
	auto AddPlot = [&](FName Id, EGFPlotKind Kind, int32 X, int32 Y)
	{
		FGFLandPlot& P = Land.Plots.AddDefaulted_GetRef();
		P.PlotId = Id;
		P.Kind = Kind;
		P.Min = FIntVector(X, Y, 0);
		P.Size = FIntVector(Edge, Edge, FMath::Max(WorldSize.Z, 1));
		if (Kind == EGFPlotKind::Rentable)
		{
			P.bForRent = true;
			P.RentItem = TEXT("Wood");
			P.RentAmount = 5;
		}
		RefreshStatus(P);
	};
	AddPlot(HomePlotId(), EGFPlotKind::Home, Margin, Margin);
	AddPlot(PlotAId(), EGFPlotKind::Rentable, Far, Margin);
	AddPlot(PlotBId(), EGFPlotKind::Rentable, Margin, Far);
	return Land;
}

EGFRentStatus FGFLandRules::StatusFor(const FGFLandPlot& Plot)
{
	if (Plot.Kind == EGFPlotKind::Home)
	{
		return EGFRentStatus::Free;
	}
	if (!Plot.HasTenant())
	{
		return EGFRentStatus::Vacant;
	}
	if (Plot.OwedPeriods >= 3)
	{
		return EGFRentStatus::Faded;
	}
	if (Plot.OwedPeriods == 2)
	{
		return EGFRentStatus::Overgrown;
	}
	if (Plot.OwedPeriods == 1)
	{
		return EGFRentStatus::Due;
	}
	return Plot.ChargesRent() ? EGFRentStatus::Paid : EGFRentStatus::Free;
}

bool FGFLandRules::RefreshStatus(FGFLandPlot& Plot)
{
	const EGFRentStatus S = StatusFor(Plot);
	const bool bChanged = S != Plot.Status;
	Plot.Status = S;
	return bChanged;
}

int64 FGFLandRules::DayIndex(const FDateTime& LocalNow)
{
	return LocalNow.GetDate().GetTicks() / ETimespan::TicksPerDay;
}

int32 FGFLandRules::Accrue(FGFLandPlot& Plot, int64 Today)
{
	if (!Plot.ChargesRent() || Plot.bRentPaused || Plot.LastAccruedDay <= 0 || Today < Plot.LastAccruedDay)
	{
		Plot.LastAccruedDay = Today; // nothing accrues while free / paused / vacant, and no back-charging later
		RefreshStatus(Plot);
		return 0;
	}
	const int64 Days = Today - Plot.LastAccruedDay;
	Plot.LastAccruedDay = Today;
	const int32 Before = Plot.OwedPeriods;
	Plot.OwedPeriods = static_cast<int32>(FMath::Min<int64>(MaxOwedPeriods, Plot.OwedPeriods + Days));
	RefreshStatus(Plot);
	return Plot.OwedPeriods - Before;
}

int32 FGFLandRules::AffordablePeriods(const FGFLandPlot& Plot, int32 Have)
{
	if (!Plot.ChargesRent() || Plot.OwedPeriods <= 0)
	{
		return 0;
	}
	return FMath::Min(Plot.OwedPeriods, FMath::Max(Have, 0) / Plot.RentAmount);
}

bool FGFLandRules::CanEdit(const FGFLandState& Land, const FString& Key, bool bIsHost, const FIntVector& Cell, bool bPlacing, FString& OutReason)
{
	if (bIsHost)
	{
		return true; // she can fix or decorate anything, guest plots included
	}
	const FGFLandPlot* Plot = Land.PlotAt(Cell);
	if (!Plot)
	{
		if (Land.IsBuilder(Key))
		{
			return true;
		}
		OutReason = TEXT("commons: builders only");
		return false;
	}
	if (Plot->Kind == EGFPlotKind::Home)
	{
		OutReason = TEXT("her home plot");
		return false;
	}
	if (Plot->TenantKey != Key || Key.IsEmpty())
	{
		OutReason = TEXT("not your plot");
		return false;
	}
	if (bPlacing && (Plot->Status == EGFRentStatus::Overgrown || Plot->Status == EGFRentStatus::Faded))
	{
		OutReason = TEXT("rent overdue: pay to place new blocks");
		return false;
	}
	return true;
}

EGFLandResult FGFLandRules::ValidateRent(const UDataTable* Items, FName ItemId, int32 Amount)
{
	if (Amount <= 0)
	{
		return EGFLandResult::Ok; // free
	}
	if (ItemId.IsNone() || !FGFEconomy::IsRentable(Items, ItemId))
	{
		return EGFLandResult::NotRentable;
	}
	const int64 Value = static_cast<int64>(FMath::Max(FGFEconomy::UnitValue(Items, ItemId), 1)) * Amount;
	return Value > MaxRentValuePerPeriod ? EGFLandResult::RentTooHigh : EGFLandResult::Ok;
}

int32 FGFLandRules::SuggestedAmount(const UDataTable* Items, FName ItemId)
{
	if (!FGFEconomy::IsRentable(Items, ItemId))
	{
		return 0;
	}
	const int32 Unit = FMath::Max(FGFEconomy::UnitValue(Items, ItemId), 1);
	return FMath::Clamp(SuggestedRentValue / Unit, 1, FMath::Max(MaxRentValuePerPeriod / Unit, 1));
}

int64 FGFLandRules::PlotHash(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot)
{
	return static_cast<int64>(Grid.GetRegionHash(Plot.Min, Plot.Max()));
}

FGFPlotBlueprint FGFLandRules::MakeBlueprint(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot, const FString& HostWorldId, const FDateTime& Now)
{
	FGFPlotBlueprint BP;
	BP.HostWorldId = HostWorldId;
	BP.PlotId = Plot.PlotId;
	BP.Size = Plot.Size;
	BP.Voxels = Grid.ExtractRegion(Plot.Min, Plot.Max());
	BP.Hash = PlotHash(Grid, Plot);
	BP.BlockCount = Grid.CountSolidInRegion(Plot.Min, Plot.Max());
	BP.CreatedUtc = Now;
	return BP;
}

bool FGFLandRules::IsBuiltOn(const FGFVoxelGrid& Grid, const FGFLandPlot& Plot)
{
	return Grid.CountSolidInRegion(Plot.Min + FIntVector(0, 0, 1), Plot.Max()) > 0;
}

TArray<FGFItemStack> FGFLandRules::RebuildCost(const FGFPlotBlueprint& Blueprint, TFunctionRef<FName(FName)> ItemForBlock)
{
	FGFVoxelGrid Temp(Blueprint.Size);
	Temp.FromSaveData(Blueprint.Voxels);
	TMap<FName, int32> Counts;
	Temp.ForEachSolid([&](const FIntVector& C, FName Id)
	{
		if (C.Z >= 1)
		{
			Counts.FindOrAdd(ItemForBlock(Id)) += 1;
		}
	});
	TArray<FGFItemStack> Out;
	for (const TPair<FName, int32>& P : Counts)
	{
		Out.Emplace(P.Key, P.Value);
	}
	Out.Sort([](const FGFItemStack& A, const FGFItemStack& B) { return A.ItemId.LexicalLess(B.ItemId); });
	return Out;
}

bool FGFLandRules::StoreBlueprint(TArray<FGFPlotBlueprint>& Book, const FGFPlotBlueprint& Blueprint)
{
	for (FGFPlotBlueprint& Existing : Book)
	{
		if (Existing.SameSlot(Blueprint.HostWorldId, Blueprint.PlotId))
		{
			if (Existing.Hash == Blueprint.Hash && Existing.BlockCount == Blueprint.BlockCount)
			{
				return false;
			}
			Existing = Blueprint;
			return true;
		}
	}
	Book.Add(Blueprint);
	return true;
}

const FGFPlotBlueprint* FGFLandRules::FindBlueprint(const TArray<FGFPlotBlueprint>& Book, const FString& WorldId, FName PlotId)
{
	return Book.FindByPredicate([&](const FGFPlotBlueprint& B) { return B.SameSlot(WorldId, PlotId); });
}
