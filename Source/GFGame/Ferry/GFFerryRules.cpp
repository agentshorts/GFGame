#include "GFFerryRules.h"

const FName FGFFerryCodes::Ok(TEXT("Ok"));
const FName FGFFerryCodes::UnknownStop(TEXT("UnknownStop"));
const FName FGFFerryCodes::NotAtStop(TEXT("NotAtStop"));
const FName FGFFerryCodes::CantAfford(TEXT("CantAfford"));
const FName FGFFerryCodes::AlreadyHasPass(TEXT("AlreadyHasPass"));
const FName FGFFerryCodes::InFlight(TEXT("InFlight"));
const FName FGFFerryCodes::NotAuthority(TEXT("NotAuthority"));
const FName FGFFerryCodes::FlightFailed(TEXT("FlightFailed"));
const FName FGFFerryCodes::NoRoute(TEXT("NoRoute"));

namespace GFFerryRulesPrivate
{
	constexpr uint32 PassBurnSalt = 0x50535331;
	constexpr uint32 PassMintSalt = 0x50535332;
	constexpr uint32 FareRefundSalt = 0x46524652;
	constexpr uint32 PassRollbackSalt = 0x50535242;

	FGFFerryResult FerryRulesFail(FName Code)
	{
		FGFFerryResult Result;
		Result.bOk = false;
		Result.Code = Code;
		return Result;
	}

	FGFFerryResult FerryRulesSucceed()
	{
		FGFFerryResult Result;
		Result.bOk = true;
		Result.Code = FGFFerryCodes::Ok;
		return Result;
	}

	bool HasPass(const FGFInventory& Inventory, const FGFFerryTuning& Tuning)
	{
		return !Tuning.PassItem.IsNone() && Inventory.GetCount(Tuning.PassItem) > 0;
	}

	FGFFerryResult GateAtStop(bool bAuthority, bool bInFlight, const FGFFerryStop* Stop, const FVector& RiderLocation, const FGFFerryTuning& Tuning)
	{
		if (!bAuthority)
		{
			return FerryRulesFail(FGFFerryCodes::NotAuthority);
		}
		if (bInFlight)
		{
			return FerryRulesFail(FGFFerryCodes::InFlight);
		}
		if (!Stop || Stop->Id.IsNone())
		{
			return FerryRulesFail(FGFFerryCodes::UnknownStop);
		}
		if (!FGFFerryRules::IsWithinBoardRadius(RiderLocation, Stop->Location, Tuning.BoardRadius))
		{
			return FerryRulesFail(FGFFerryCodes::NotAtStop);
		}
		return FerryRulesSucceed();
	}

	bool BurnApplied(EGFTxResult Result)
	{
		return Result == EGFTxResult::Applied || Result == EGFTxResult::AlreadyApplied;
	}
}

FName FGFFerryRules::FareReason()
{
	return TEXT("Ferry.Fare");
}

FName FGFFerryRules::FareRefundReason()
{
	return TEXT("Ferry.FareRefund");
}

FName FGFFerryRules::PassBurnReason()
{
	return TEXT("Ferry.Pass");
}

FName FGFFerryRules::PassGrantReason()
{
	return TEXT("Ferry.PassGrant");
}

FName FGFFerryRules::PassRollbackReason()
{
	return TEXT("Ferry.PassRollback");
}

FGuid FGFFerryRules::FareTxId(const FGuid& RequestId)
{
	return RequestId.IsValid() ? RequestId : FGuid();
}

FGuid FGFFerryRules::FareRefundTx(const FGuid& RequestId)
{
	return RequestId.IsValid() ? DeriveTxId(RequestId, GFFerryRulesPrivate::FareRefundSalt) : FGuid();
}

FGuid FGFFerryRules::PassRollbackTx(const FGuid& RequestId)
{
	return RequestId.IsValid() ? DeriveTxId(RequestId, GFFerryRulesPrivate::PassRollbackSalt) : FGuid();
}

int32 FGFFerryRules::Quote(const FGFInventory& Inventory, const FGFFerryTuning& Tuning)
{
	if (GFFerryRulesPrivate::HasPass(Inventory, Tuning))
	{
		return 0;
	}
	return FMath::Max(0, Tuning.FareCount);
}

bool FGFFerryRules::IsWithinBoardRadius(const FVector& RiderLocation, const FVector& StopLocation, float BoardRadius)
{
	return FVector::Dist2D(RiderLocation, StopLocation) <= FMath::Max(0.f, BoardRadius);
}

FGFFerryResult FGFFerryRules::CanBoard(
	bool bAuthority,
	bool bInFlight,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFInventory& Inventory,
	const FGFFerryTuning& Tuning)
{
	const FGFFerryResult Gate = GFFerryRulesPrivate::GateAtStop(bAuthority, bInFlight, Stop, RiderLocation, Tuning);
	if (!Gate.bOk)
	{
		return Gate;
	}
	const int32 Cost = Quote(Inventory, Tuning);
	if (Cost > 0 && (Tuning.FareItem.IsNone() || Inventory.GetCount(Tuning.FareItem) < Cost))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
	}
	return GFFerryRulesPrivate::FerryRulesSucceed();
}

TArray<FGFItemStack> FGFFerryRules::ChargeFare(const FGFInventory& Inventory, const FGFFerryTuning& Tuning)
{
	TArray<FGFItemStack> Stacks;
	const int32 Cost = Quote(Inventory, Tuning);
	if (Cost > 0 && !Tuning.FareItem.IsNone())
	{
		Stacks.Add(FGFItemStack(Tuning.FareItem, Cost));
	}
	return Stacks;
}

FGFFerryResult FGFFerryRules::CanBuyPass(
	bool bAuthority,
	bool bInFlight,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFInventory& Inventory,
	const FGFFerryTuning& Tuning)
{
	const FGFFerryResult Gate = GFFerryRulesPrivate::GateAtStop(bAuthority, bInFlight, Stop, RiderLocation, Tuning);
	if (!Gate.bOk)
	{
		return Gate;
	}
	if (GFFerryRulesPrivate::HasPass(Inventory, Tuning))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::AlreadyHasPass);
	}
	const int32 Price = FMath::Max(0, Tuning.PassPrice);
	if (Price > 0 && (Tuning.PassMaterialItem.IsNone() || Inventory.GetCount(Tuning.PassMaterialItem) < Price))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
	}
	if (Tuning.PassItem.IsNone())
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
	}
	return GFFerryRulesPrivate::FerryRulesSucceed();
}

FGFFerryFarePlan FGFFerryRules::PlanFare(
	bool bAuthority,
	bool bInFlight,
	bool bTxAlreadyApplied,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFInventory& Inventory,
	const FGFFerryTuning& Tuning)
{
	FGFFerryFarePlan Plan;
	if (!bAuthority)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::NotAuthority);
		return Plan;
	}
	if (bTxAlreadyApplied)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesSucceed();
		return Plan;
	}
	Plan.Result = CanBoard(bAuthority, bInFlight, Stop, RiderLocation, Inventory, Tuning);
	if (!Plan.Result.bOk)
	{
		return Plan;
	}
	Plan.Stacks = ChargeFare(Inventory, Tuning);
	Plan.bCharge = Plan.Stacks.Num() > 0;
	return Plan;
}

FGFFerryResult FGFFerryRules::ApplyFare(
	IGFLedgerAccounts& Accounts,
	FGFTransferLedger& Ledger,
	const FGuid& TxId,
	const FString& Account,
	bool bAuthority,
	bool bInFlight,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFFerryTuning& Tuning)
{
	FGFInventory Inventory;
	if (!Accounts.ReadAccount(Account, Inventory))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::NotAuthority);
	}
	const FGFFerryFarePlan Plan = PlanFare(bAuthority, bInFlight, Ledger.WasApplied(TxId), Stop, RiderLocation, Inventory, Tuning);
	if (!Plan.Result.bOk || !Plan.bCharge)
	{
		return Plan.Result;
	}
	if (!GFFerryRulesPrivate::BurnApplied(Ledger.Burn(Accounts, TxId, Account, Plan.Stacks, FareReason())))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
	}
	return GFFerryRulesPrivate::FerryRulesSucceed();
}

FGuid FGFFerryRules::DeriveTxId(const FGuid& RequestId, uint32 Salt)
{
	FGuid Out = RequestId;
	Out.D ^= Salt;
	if (!Out.IsValid())
	{
		Out.A = Salt | 1u;
	}
	return Out;
}

FGFFerryPassPlan FGFFerryRules::PlanPass(
	bool bAuthority,
	bool bInFlight,
	bool bBurnAlreadyApplied,
	bool bMintAlreadyApplied,
	bool bRolledBack,
	const FGuid& RequestId,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFInventory& Inventory,
	const FGFFerryTuning& Tuning)
{
	FGFFerryPassPlan Plan;
	Plan.BurnTx = DeriveTxId(RequestId, GFFerryRulesPrivate::PassBurnSalt);
	Plan.MintTx = DeriveTxId(RequestId, GFFerryRulesPrivate::PassMintSalt);
	if (!bAuthority)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::NotAuthority);
		return Plan;
	}
	if (GFFerryRulesPrivate::HasPass(Inventory, Tuning) || bMintAlreadyApplied)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::AlreadyHasPass);
		return Plan;
	}
	if (bRolledBack)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
		return Plan;
	}
	if (bBurnAlreadyApplied)
	{
		Plan.Result = GFFerryRulesPrivate::FerryRulesSucceed();
		Plan.bMint = !Tuning.PassItem.IsNone();
		if (Plan.bMint)
		{
			Plan.MintStacks.Add(FGFItemStack(Tuning.PassItem, 1));
		}
		return Plan;
	}
	Plan.Result = CanBuyPass(bAuthority, bInFlight, Stop, RiderLocation, Inventory, Tuning);
	if (!Plan.Result.bOk)
	{
		return Plan;
	}
	const int32 Price = FMath::Max(0, Tuning.PassPrice);
	if (Price > 0)
	{
		Plan.bBurn = true;
		Plan.BurnStacks.Add(FGFItemStack(Tuning.PassMaterialItem, Price));
	}
	Plan.bMint = true;
	Plan.MintStacks.Add(FGFItemStack(Tuning.PassItem, 1));
	return Plan;
}

FGFFerryResult FGFFerryRules::ApplyPass(
	IGFLedgerAccounts& Accounts,
	FGFTransferLedger& Ledger,
	const FGuid& RequestId,
	const FString& Account,
	bool bAuthority,
	bool bInFlight,
	const FGFFerryStop* Stop,
	const FVector& RiderLocation,
	const FGFFerryTuning& Tuning)
{
	if (!RequestId.IsValid())
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::NotAuthority);
	}
	FGFInventory Inventory;
	if (!Accounts.ReadAccount(Account, Inventory))
	{
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::NotAuthority);
	}
	const FGuid BurnTx = DeriveTxId(RequestId, GFFerryRulesPrivate::PassBurnSalt);
	const FGuid MintTx = DeriveTxId(RequestId, GFFerryRulesPrivate::PassMintSalt);
	const FGuid RollbackTx = PassRollbackTx(RequestId);
	const FGFFerryPassPlan Plan = PlanPass(
		bAuthority,
		bInFlight,
		Ledger.WasApplied(BurnTx),
		Ledger.WasApplied(MintTx),
		Ledger.WasApplied(RollbackTx),
		RequestId,
		Stop,
		RiderLocation,
		Inventory,
		Tuning);
	if (!Plan.Result.bOk)
	{
		return Plan.Result;
	}
	EGFTxResult BurnResult = EGFTxResult::AlreadyApplied;
	if (Plan.bBurn)
	{
		BurnResult = Ledger.Burn(Accounts, Plan.BurnTx, Account, Plan.BurnStacks, PassBurnReason());
		if (!GFFerryRulesPrivate::BurnApplied(BurnResult))
		{
			return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
		}
	}
	if (Plan.bMint && !GFFerryRulesPrivate::BurnApplied(Ledger.Mint(Accounts, Plan.MintTx, Account, Plan.MintStacks, PassGrantReason())))
	{
		if (BurnResult == EGFTxResult::Applied && Plan.BurnStacks.Num() > 0)
		{
			Ledger.Mint(Accounts, RollbackTx, Account, Plan.BurnStacks, PassRollbackReason());
		}
		return GFFerryRulesPrivate::FerryRulesFail(FGFFerryCodes::CantAfford);
	}
	return GFFerryRulesPrivate::FerryRulesSucceed();
}

FVector FGFFerryRules::FlightControl(const FVector& A, const FVector& B, float Altitude)
{
	const FVector Mid = (A + B) * 0.5f;
	FVector Side(- (B.Y - A.Y), B.X - A.X, 0.f);
	Side = Side.GetSafeNormal2D();
	if (Side.IsNearlyZero())
	{
		Side = FVector(0.f, 1.f, 0.f);
	}
	else if (Side.Y < 0.f)
	{
		Side = -Side;
	}
	FVector Control = Mid + Side * FGFFerryArt::RouteBulgeCm;
	Control.Z = Mid.Z + 2.f * Altitude;
	return Control;
}

FVector FGFFerryRules::FlightPosition(const FVector& A, const FVector& B, float Alpha, float Altitude)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	if (T <= 0.f)
	{
		return A;
	}
	if (T >= 1.f)
	{
		return B;
	}
	const FVector Control = FlightControl(A, B, Altitude);
	const float U = 1.f - T;
	return (U * U) * A + (2.f * U * T) * Control + (T * T) * B;
}

FVector FGFFerryRules::FlightTangent(const FVector& A, const FVector& B, float Alpha, float Altitude)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	const FVector Control = FlightControl(A, B, Altitude);
	const FVector Deriv = (2.f * (1.f - T) * (Control - A)) + (2.f * T * (B - Control));
	return Deriv.GetSafeNormal();
}

FVector FGFFerryRules::DropPoint(const FVector& From, const FVector& To, const FVector& RiderLocation, float BoardRadius)
{
	FVector Dir = (To - From).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector(1.f, 0.f, 0.f);
	}
	FVector Drop = To + Dir * (FMath::Max(0.f, BoardRadius) * 0.5f);
	Drop.Z = To.Z + (RiderLocation.Z - From.Z);
	return Drop;
}

const FGFFerryStop* FGFFerryRules::FindStop(const TArray<FGFFerryStop>& Stops, FName Id)
{
	if (Id.IsNone())
	{
		return nullptr;
	}
	return Stops.FindByPredicate([Id](const FGFFerryStop& Stop) { return Stop.Id == Id; });
}

const FGFFerryRoute* FGFFerryRules::FindRouteFrom(const TArray<FGFFerryRoute>& Routes, FName StopId)
{
	if (StopId.IsNone())
	{
		return nullptr;
	}
	return Routes.FindByPredicate([StopId](const FGFFerryRoute& Route)
	{
		return Route.StopA == StopId || Route.StopB == StopId;
	});
}

const FGFFerryRoute* FGFFerryRules::FindRouteBetween(const TArray<FGFFerryRoute>& Routes, FName StopA, FName StopB)
{
	if (StopA.IsNone() || StopB.IsNone() || StopA == StopB)
	{
		return nullptr;
	}
	return Routes.FindByPredicate([StopA, StopB](const FGFFerryRoute& Route)
	{
		return (Route.StopA == StopA && Route.StopB == StopB) || (Route.StopA == StopB && Route.StopB == StopA);
	});
}

FName FGFFerryRules::OtherStop(const FGFFerryRoute& Route, FName FromStopId)
{
	if (Route.StopA == FromStopId)
	{
		return Route.StopB;
	}
	if (Route.StopB == FromStopId)
	{
		return Route.StopA;
	}
	return NAME_None;
}

void FGFFerryRules::CollectDestinations(const TArray<FGFFerryStop>& Stops, const TArray<FGFFerryRoute>& Routes, FName FromStopId, TArray<FName>& OutStopIds)
{
	OutStopIds.Reset();
	if (FromStopId.IsNone())
	{
		return;
	}
	for (const FGFFerryStop& Stop : Stops)
	{
		if (Stop.Id.IsNone() || Stop.Id == FromStopId)
		{
			continue;
		}
		if (FindRouteBetween(Routes, FromStopId, Stop.Id))
		{
			OutStopIds.Add(Stop.Id);
		}
	}
}

float FGFFerryRouteSpawnRules::ScaledAttackDamage(float BaseDamage, int32 MobLevel, float MobDamagePerLevel)
{
	return BaseDamage * (1.f + MobDamagePerLevel * static_cast<float>(MobLevel - 1));
}

float FGFFerryRouteSpawnRules::DistanceToBox(const FGFTerritoryHazard& Box, const FVector& Point)
{
	const float DX = FMath::Max(FMath::Abs(Point.X - Box.Center.X) - Box.HalfExtent.X, 0.f);
	const float DY = FMath::Max(FMath::Abs(Point.Y - Box.Center.Y) - Box.HalfExtent.Y, 0.f);
	return FMath::Sqrt(DX * DX + DY * DY);
}

bool FGFFerryRouteSpawnRules::ContainsHorizontal(const FGFTerritoryHazard& Box, const FVector& Point)
{
	return FMath::Abs(Point.X - Box.Center.X) <= Box.HalfExtent.X
		&& FMath::Abs(Point.Y - Box.Center.Y) <= Box.HalfExtent.Y;
}

bool FGFFerryRouteSpawnRules::IsRouteActive(const FGFTerritoryHazard& Box, float Margin, const TArray<FGFFerryRoutePerson>& People)
{
	const float Limit = FMath::Max(0.f, Margin);
	for (const FGFFerryRoutePerson& Person : People)
	{
		if (Person.bFlying)
		{
			continue;
		}
		if (DistanceToBox(Box, Person.Location) <= Limit)
		{
			return true;
		}
	}
	return false;
}

bool FGFFerryRouteSpawnRules::CanSpawn(int32 Alive, int32 MaxAlive, bool bActive)
{
	return bActive && MaxAlive > 0 && Alive < MaxAlive;
}

bool FGFFerryRouteSpawnRules::ShouldDespawn(
	const FGFTerritoryHazard& Box,
	const FVector& MobLocation,
	float Margin,
	float AgeSeconds,
	float IdleDespawnSeconds,
	bool bAnyoneNear)
{
	if (DistanceToBox(Box, MobLocation) > FMath::Max(0.f, Margin))
	{
		return true;
	}
	return !bAnyoneNear && AgeSeconds >= IdleDespawnSeconds;
}

bool FGFFerryRouteSpawnRules::IsLegalSpawnPoint(const FVector& Point, FName RequiredHazardId, TFunctionRef<FName(const FVector&)> HazardAt)
{
	return !RequiredHazardId.IsNone() && HazardAt(Point) == RequiredHazardId;
}

bool FGFFerryRouteSpawnRules::PickSpawnPoint(
	const FGFTerritoryHazard& Box,
	FName RequiredHazardId,
	FRandomStream& Rng,
	TFunctionRef<FName(const FVector&)> HazardAt,
	FVector& OutPoint)
{
	if (RequiredHazardId.IsNone() || Box.HalfExtent.X < 0.f || Box.HalfExtent.Y < 0.f)
	{
		return false;
	}
	for (int32 Attempt = 0; Attempt < SampleAttempts; ++Attempt)
	{
		const float X = Rng.FRandRange(Box.Center.X - Box.HalfExtent.X, Box.Center.X + Box.HalfExtent.X);
		const float Y = Rng.FRandRange(Box.Center.Y - Box.HalfExtent.Y, Box.Center.Y + Box.HalfExtent.Y);
		const FVector Point(X, Y, Box.Center.Z);
		if (IsLegalSpawnPoint(Point, RequiredHazardId, HazardAt))
		{
			OutPoint = Point;
			return true;
		}
	}
	return false;
}

FName FGFFerryRouteSpawnRules::PickSpecies(const TArray<FName>& Species, FRandomStream& Rng)
{
	if (Species.Num() == 0)
	{
		return NAME_None;
	}
	return Species[Rng.RandRange(0, Species.Num() - 1)];
}
