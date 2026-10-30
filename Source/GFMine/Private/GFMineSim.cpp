#include "GFMineTypes.h"
#include "GFDataTables.h"
#include "GFEconomy.h"

namespace GFMineSim
{
	static const TCHAR* Names[] = { TEXT("Pip"), TEXT("Nib"), TEXT("Tumble"), TEXT("Mote"), TEXT("Cobble"), TEXT("Pebbleberry"), TEXT("Grit"), TEXT("Dusty") };

	static int32 Total(const TArray<FGFItemStack>& S)
	{
		int32 N = 0;
		for (const FGFItemStack& X : S)
		{
			N += X.Count;
		}
		return N;
	}
}

FGFMineStats FGFMineStats::For(const FGFMineCrew& Crew)
{
	FGFMineStats S;
	S.WorkSeconds = BaseWorkSeconds / (1.f + 0.25f * Crew.Level(GFMineUpgrades::Picks));
	S.TravelSeconds = BaseTravelSeconds * (1.f - 0.3f * Crew.Level(GFMineUpgrades::Rails));
	S.CarryCap = 12 + 8 * Crew.Level(GFMineUpgrades::Satchels);
	S.CrewCap = 2 + Crew.Level(GFMineUpgrades::CrewQuarters);
	S.bAutoHaul = Crew.Level(GFMineUpgrades::AutoHaul) > 0;
	S.OfflineCapHours = 4.f + 4.f * Crew.Level(GFMineUpgrades::NightShift);
	S.OfflineRate = S.bAutoHaul ? 0.75f : 0.5f;
	return S;
}

const FGFMineData& FGFMineData::Get()
{
	static FGFMineData Data;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		if (const UDataTable* T = FGFDataTables::Get<FGFMineRoomRow>(TEXT("DT_MineRooms")))
		{
			T->ForeachRow<FGFMineRoomRow>(TEXT("Mine"), [&](const FName& Name, const FGFMineRoomRow& Row) { Data.Rooms.Add(Name, Row); });
		}
		if (const UDataTable* T = FGFDataTables::Get<FGFMineUpgradeRow>(TEXT("DT_MineUpgrades")))
		{
			T->ForeachRow<FGFMineUpgradeRow>(TEXT("Mine"), [&](const FName&, const FGFMineUpgradeRow& Row) { Data.Upgrades.Add(Row); });
		}
	}
	return Data;
}

const FGFMineUpgradeRow* FGFMineData::Upgrade(FName Id, int32 Level) const
{
	return Upgrades.FindByPredicate([&](const FGFMineUpgradeRow& R) { return R.Upgrade == Id && R.Level == Level; });
}

int32 FGFMineData::MaxLevel(FName Id) const
{
	int32 M = 0;
	for (const FGFMineUpgradeRow& R : Upgrades)
	{
		if (R.Upgrade == Id)
		{
			M = FMath::Max(M, R.Level);
		}
	}
	return M;
}

EGFMineOrder FGFMineRules::CheckCapture(const FGFPlayerProgress& Progress, float ApproachSpeed)
{
	if (!Progress.bHasTamed)
	{
		return EGFMineOrder::Locked;
	}
	if (Progress.Crew.Pebblings.Num() >= FGFMineStats::For(Progress.Crew).CrewCap)
	{
		return EGFMineOrder::CrewFull;
	}
	return ApproachSpeed > SneakSpeed ? EGFMineOrder::Startled : EGFMineOrder::Ok;
}

FGFPebbling& FGFMineRules::AddPebbling(FGFMineCrew& Crew)
{
	FGFPebbling& P = Crew.Pebblings.AddDefaulted_GetRef();
	P.Id = FGuid::NewGuid();
	const int32 N = Crew.Pebblings.Num() - 1;
	P.Name = FName(GFMineSim::Names[N % UE_ARRAY_COUNT(GFMineSim::Names)]);
	P.State = EGFPebblingState::Home;
	return P;
}

bool FGFMineRules::RoomOpen(const FGFMineCrew& Crew, FName Room)
{
	const FGFMineRoomRow* R = FGFMineData::Get().Room(Room);
	return R && (R->NeedsUpgrade.IsNone() || Crew.Level(R->NeedsUpgrade) > 0);
}

EGFMineOrder FGFMineRules::Assign(FGFMineCrew& Crew, const FGuid& Id, FName Room)
{
	FGFPebbling* P = Crew.Find(Id);
	if (!P)
	{
		return EGFMineOrder::NoPebbling;
	}
	if (!FGFMineData::Get().Room(Room))
	{
		return EGFMineOrder::UnknownRoom;
	}
	if (!RoomOpen(Crew, Room))
	{
		return EGFMineOrder::RoomLocked;
	}
	const FGFMineStats S = FGFMineStats::For(Crew);
	const bool bChanged = P->Room != Room;
	P->Room = Room;
	if (P->State == EGFPebblingState::Carry)
	{
		return EGFMineOrder::Ok; // it heads to the new room after dropping the haul
	}
	if (P->Carry.Num() > 0 && bChanged)
	{
		P->State = EGFPebblingState::Carry; // never mixes rooms: the old haul goes home first
		P->StateLeft = S.TravelSeconds;
	}
	else if (P->State == EGFPebblingState::Home || bChanged)
	{
		P->State = EGFPebblingState::Travel;
		P->StateLeft = S.TravelSeconds;
	}
	return EGFMineOrder::Ok;
}

EGFMineOrder FGFMineRules::Haul(FGFMineCrew& Crew, const FGuid& Id)
{
	const FGFMineStats S = FGFMineStats::For(Crew);
	bool bAny = false;
	for (FGFPebbling& P : Crew.Pebblings)
	{
		if ((!Id.IsValid() || P.Id == Id) && P.Carry.Num() > 0 && (P.State == EGFPebblingState::Work || P.State == EGFPebblingState::Full))
		{
			P.State = EGFPebblingState::Carry;
			P.StateLeft = S.TravelSeconds;
			bAny = true;
		}
	}
	if (Id.IsValid() && !Crew.Find(Id))
	{
		return EGFMineOrder::NoPebbling;
	}
	return bAny ? EGFMineOrder::Ok : EGFMineOrder::NothingToHaul;
}

EGFMineOrder FGFMineRules::Recall(FGFMineCrew& Crew, const FGuid& Id)
{
	FGFPebbling* P = Crew.Find(Id);
	if (!P)
	{
		return EGFMineOrder::NoPebbling;
	}
	P->Room = NAME_None;
	if (P->State != EGFPebblingState::Home && P->State != EGFPebblingState::Carry)
	{
		P->State = EGFPebblingState::Carry; // walks home (with whatever it has)
		P->StateLeft = FGFMineStats::For(Crew).TravelSeconds;
	}
	return EGFMineOrder::Ok;
}

bool FGFMineRules::Tick(FGFMineCrew& Crew, float Dt, FRandomStream& Rng, TArray<FGFItemStack>& OutDelivered)
{
	const FGFMineStats S = FGFMineStats::For(Crew);
	const FGFMineData& Data = FGFMineData::Get();
	bool bChanged = false;
	for (FGFPebbling& P : Crew.Pebblings)
	{
		float Left = Dt;
		// Several transitions can happen in one long tick (fast-forward); bounded so a bad Dt can't spin.
		for (int32 Guard = 0; Guard < 64 && Left > 0.f; ++Guard)
		{
			if (P.State == EGFPebblingState::Home)
			{
				break;
			}
			if (P.State == EGFPebblingState::Full)
			{
				if (S.bAutoHaul)
				{
					P.State = EGFPebblingState::Carry;
					P.StateLeft = S.TravelSeconds;
					bChanged = true;
					continue;
				}
				break; // waits for her order
			}
			const float Use = FMath::Min(Left, P.StateLeft);
			P.StateLeft -= Use;
			Left -= Use;
			if (P.StateLeft > 0.f)
			{
				break;
			}
			bChanged = true;
			switch (P.State)
			{
			case EGFPebblingState::Travel:
			case EGFPebblingState::Return:
				P.State = EGFPebblingState::Work;
				P.StateLeft = S.WorkSeconds;
				break;
			case EGFPebblingState::Work:
				if (const FGFMineRoomRow* Room = Data.Room(P.Room))
				{
					FGFRewards::Roll(Room->Yields, Rng, 1.f, P.Carry); // only its own room
					++P.Loads;
				}
				if (GFMineSim::Total(P.Carry) >= S.CarryCap)
				{
					P.State = EGFPebblingState::Full;
					P.StateLeft = 0.f;
				}
				else
				{
					P.StateLeft = S.WorkSeconds;
				}
				break;
			case EGFPebblingState::Carry:
				FGFRewards::AddAll(OutDelivered, P.Carry);
				P.Carry.Reset();
				if (P.Room.IsNone())
				{
					P.State = EGFPebblingState::Home;
					P.StateLeft = 0.f;
				}
				else
				{
					P.State = EGFPebblingState::Return;
					P.StateLeft = S.TravelSeconds;
				}
				break;
			default:
				break;
			}
		}
	}
	return bChanged;
}

const FGFMineUpgradeRow* FGFMineRules::NextUpgrade(const FGFMineCrew& Crew, FName Upgrade, EGFMineOrder& OutWhy)
{
	const FGFMineData& Data = FGFMineData::Get();
	const int32 Max = Data.MaxLevel(Upgrade);
	if (Max == 0)
	{
		OutWhy = EGFMineOrder::UnknownUpgrade;
		return nullptr;
	}
	const int32 Next = Crew.Level(Upgrade) + 1;
	if (Next > Max)
	{
		OutWhy = EGFMineOrder::MaxLevel;
		return nullptr;
	}
	OutWhy = EGFMineOrder::Ok;
	return Data.Upgrade(Upgrade, Next);
}

float FGFMineRules::ExpectedLoadValue(FName Room, const UDataTable* Items)
{
	const FGFMineRoomRow* R = FGFMineData::Get().Room(Room);
	float V = 0.f;
	if (R)
	{
		for (const FGFRewardEntry& E : R->Yields)
		{
			V += E.Chance * 0.5f * (E.Min + E.Max) * FGFEconomy::UnitValue(Items, E.ItemId);
		}
	}
	return V;
}

float FGFMineRules::MaxOnlineValuePerMinute(const FGFMineCrew& Crew, const UDataTable* Items)
{
	const FGFMineStats S = FGFMineStats::For(Crew);
	float Best = 0.f;
	for (const TPair<FName, FGFMineRoomRow>& R : FGFMineData::Get().Rooms)
	{
		if (RoomOpen(Crew, R.Key))
		{
			Best = FMath::Max(Best, ExpectedLoadValue(R.Key, Items));
		}
	}
	return S.CrewCap * Best * 60.f / S.WorkSeconds;
}

FGFMineOffline FGFMineRules::Offline(const FGFMineCrew& Crew, double GapSeconds, int32 Seed, const UDataTable* Items)
{
	FGFMineOffline Out;
	if (GapSeconds <= 0.0)
	{
		return Out; // the clock went backwards: nothing
	}
	const FGFMineStats S = FGFMineStats::For(Crew);
	const double Capped = FMath::Min(GapSeconds, static_cast<double>(S.OfflineCapHours) * 3600.0);
	Out.Hours = static_cast<float>(Capped / 3600.0);
	const int32 ValueCap = FMath::FloorToInt(FGFMineStats::OfflineValuePerHour * Out.Hours);
	FRandomStream Rng(Seed);
	const FGFMineData& Data = FGFMineData::Get();
	// Loads per pebbling at the reduced rate; rolled in pebbling order so the cap cuts evenly.
	const int32 Loads = FMath::FloorToInt(static_cast<float>(Capped) * S.OfflineRate / S.WorkSeconds);
	for (int32 L = 0; L < Loads; ++L)
	{
		for (const FGFPebbling& P : Crew.Pebblings)
		{
			const FGFMineRoomRow* Room = P.Room.IsNone() ? nullptr : Data.Room(P.Room);
			if (!Room)
			{
				continue;
			}
			TArray<FGFItemStack> Load;
			FGFRewards::Roll(Room->Yields, Rng, 1.f, Load);
			const int32 V = static_cast<int32>(FGFEconomy::StacksValue(Items, Load));
			if (Out.Value + V > ValueCap)
			{
				return Out;
			}
			Out.Value += V;
			FGFRewards::AddAll(Out.Items, Load);
		}
	}
	return Out;
}
