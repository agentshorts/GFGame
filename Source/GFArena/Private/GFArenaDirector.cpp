#include "GFArenaDirector.h"
#include "GFWager.h"
#include "GFEconomy.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFCompanionRoster.h"
#include "GFRecipeTypes.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

const FName AGFArenaDirector::ModeName(TEXT("Arena"));

namespace GFArenaVerbs
{
	static const FName Place(TEXT("Place"));
	static const FName AutoPlace(TEXT("AutoPlace"));
	static const FName Wager(TEXT("Wager"));
	static const FName Reroll(TEXT("Reroll"));
	static const FName Suggest(TEXT("Suggest"));
	static const FName NoWager(TEXT("NoWager"));
	static const FName HostWagersOff(TEXT("HostWagersOff"));
	static const FName Fight(TEXT("Fight"));
	static const FName SlowTime(TEXT("SlowTime"));
	static const FName Command(TEXT("Command"));
	static const FName Possess(TEXT("Possess"));
	static const FName Cycle(TEXT("Cycle"));
	static const FName Release(TEXT("Release"));
	static const FName Input(TEXT("Input"));
	static const FName Ok(TEXT("Ok"));
}

AGFArenaDirector::AGFArenaDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.f);
}

AGFArenaDirector* AGFArenaDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFArenaDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGFArenaDirector>(P);
	}
	return nullptr;
}

void AGFArenaDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFArenaDirector, Phase);
	DOREPLIFETIME(AGFArenaDirector, Units);
	DOREPLIFETIME(AGFArenaDirector, Entries);
	DOREPLIFETIME(AGFArenaDirector, Outcome);
	DOREPLIFETIME(AGFArenaDirector, FightTime);
	DOREPLIFETIME(AGFArenaDirector, Combos);
	DOREPLIFETIME(AGFArenaDirector, bSlowTimeActive);
	DOREPLIFETIME(AGFArenaDirector, SlowTimeRemaining);
	DOREPLIFETIME(AGFArenaDirector, SlowTimeCooldown);
	DOREPLIFETIME(AGFArenaDirector, OpponentBand);
	DOREPLIFETIME(AGFArenaDirector, bHostWagersOff);
}

void AGFArenaDirector::BeginPlay()
{
	Super::BeginPlay();
	LastRealTime = FPlatformTime::Seconds();
	if (HasAuthority())
	{
		if (AGFGameState* G = GS())
		{
			G->RegisterModeHandler(this);
		}
	}
}

void AGFArenaDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		AbortFight();
		SetWorldDilation(1.f); // never left stuck slow
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
	}
	Super::EndPlay(Reason);
}

AGFGameState* AGFArenaDirector::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

const FGFArenaEntry* AGFArenaDirector::FindEntry(const FString& Key) const
{
	return Entries.FindByPredicate([&](const FGFArenaEntry& E) { return E.PlayerKey == Key; });
}

FGFArenaEntry& AGFArenaDirector::EntryFor(const FString& Key)
{
	if (FGFArenaEntry* E = Entries.FindByPredicate([&](const FGFArenaEntry& X) { return X.PlayerKey == Key; }))
	{
		return *E;
	}
	FGFArenaEntry& E = Entries.AddDefaulted_GetRef();
	E.PlayerKey = Key;
	return E;
}

void AGFArenaDirector::SetWorldDilation(float D)
{
	if (bApplyWorldDilation && GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(GetWorld(), D);
	}
}

int32 AGFArenaDirector::BandOfEntries() const
{
	const UDataTable* Items = GS() ? GS()->GetItemTable() : nullptr;
	int32 Band = 0;
	for (const FGFArenaEntry& E : Entries)
	{
		if (E.Stake.Num() > 0)
		{
			Band = FMath::Max(Band, FGFEconomy::ValueBand(FGFEconomy::StacksValue(Items, E.Stake)));
		}
	}
	return Band;
}

FGFModeResult AGFArenaDirector::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& R)
{
	using namespace GFArenaVerbs;
	const FString Key = By->PlayerKey;
	const double Now = FPlatformTime::Seconds();
	const FGFArenaData& Data = FGFArenaData::Get();
	const bool bFighting = Phase == EGFArenaPhase::Fighting;
	auto FromName = [](FName N) { return N == GFArenaVerbs::Ok ? FGFModeResult::Ok() : FGFModeResult::Fail(N); };

	if (R.Verb == Place)
	{
		return bFighting ? FGFModeResult::Fail(TEXT("Fighting")) : DoPlace(By, R.Ids, R.Ints);
	}
	if (R.Verb == AutoPlace)
	{
		if (bFighting)
		{
			return FGFModeResult::Fail(TEXT("Fighting"));
		}
		TArray<FGFCompanionEntry> Roster = By->Companions;
		Roster.StableSort([](const FGFCompanionEntry& A, const FGFCompanionEntry& B) { return A.bInParty != B.bInParty ? A.bInParty : A.Level > B.Level; });
		TArray<FGuid> Ids;
		TArray<int32> Spots;
		int32 Front = 0, Back = 3;
		for (const FGFCompanionEntry& C : Roster)
		{
			if (Ids.Num() >= 3)
			{
				break;
			}
			const FGFArenaSpeciesRow* Row = Data.Species.Find(C.Species);
			const bool bFrontRole = Row && (Row->Role == EGFArenaRole::Tank || Row->Role == EGFArenaRole::Bruiser);
			Ids.Add(C.Id);
			Spots.Add(bFrontRole || Back > 5 ? Front++ : Back++); // tanks in front, fragile ones behind
		}
		FGFModeResult Res = DoPlace(By, Ids, Spots);
		Res.Ints = Spots;
		return Res;
	}
	if (R.Verb == Wager || R.Verb == Reroll)
	{
		if (bFighting)
		{
			return FGFModeResult::Fail(TEXT("Fighting"));
		}
		const bool bReroll = R.Verb == Reroll;
		const FGFArenaEntry* E = FindEntry(Key);
		if (bReroll && (!E || E->Stake.Num() == 0))
		{
			return FGFModeResult::Fail(TEXT("NoStake"));
		}
		const TArray<FGFItemStack> Stake = bReroll ? E->Stake : R.Stacks;
		return DoWager(By, Stake, bReroll);
	}
	if (R.Verb == Suggest)
	{
		FGFInventory Inv;
		if (!GS() || !GS()->Accounts().ReadAccount(Key, Inv))
		{
			return FGFModeResult::Fail(TEXT("NotReady"));
		}
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Stacks = FGFWagerRules::Suggest(GS()->GetItemTable(), Inv, AGFGameState::FindRecipe(By->Progress.PinnedRecipe));
		return Res;
	}
	if (R.Verb == NoWager)
	{
		if (bFighting)
		{
			return FGFModeResult::Fail(TEXT("Fighting"));
		}
		FGFArenaEntry& E = EntryFor(Key);
		E.Stake.Reset();
		E.Counter.Reset();
		return FGFModeResult::Ok();
	}
	if (R.Verb == HostWagersOff)
	{
		if (!By->bIsHost)
		{
			return FGFModeResult::Fail(TEXT("NotHost"));
		}
		bHostWagersOff = R.Int(0) != 0;
		return FGFModeResult::Ok();
	}
	if (R.Verb == Fight)
	{
		return bFighting ? FGFModeResult::Fail(TEXT("Fighting")) : DoFight(By);
	}

	// ---- in-fight verbs ----
	if (!bFighting)
	{
		return FGFModeResult::Fail(TEXT("NotFighting"));
	}
	const bool bInFight = Entries.ContainsByPredicate([&](const FGFArenaEntry& E) { return E.PlayerKey == Key && E.CompanionIds.Num() > 0; });
	if (!bInFight)
	{
		return FGFModeResult::Fail(TEXT("NotInFight"));
	}
	if (R.Verb == SlowTime)
	{
		if (Slow.bActive)
		{
			EndSlowTime(Now); // second press ends it early
			RefreshViews();
			return FGFModeResult::Ok();
		}
		if (!Slow.TryActivate(Now))
		{
			return FGFModeResult::Fail(TEXT("Cooldown"));
		}
		Queued.Reset();
		SetWorldDilation(FGFSlowTime::Dilation);
		RefreshViews();
		return FGFModeResult::Ok();
	}
	if (R.Verb == Command)
	{
		if (!Slow.bActive)
		{
			return FGFModeResult::Fail(TEXT("NoSlowTime"));
		}
		FGFArenaCommand Cmd;
		Cmd.IssuerKey = Key;
		Cmd.UnitId = R.Int(0);
		Cmd.Slot = R.Int(1);
		Cmd.TargetId = R.Int(2, INDEX_NONE);
		const FName Check = Sim.CheckCommand(Data, Cmd);
		if (Check != Ok)
		{
			return FGFModeResult::Fail(Check);
		}
		if (Queued.ContainsByPredicate([&](const FGFArenaCommand& Q) { return Q.UnitId == Cmd.UnitId; }))
		{
			return FGFModeResult::Fail(TEXT("AlreadyCommanded")); // one order per dino
		}
		Queued.Add(Cmd);
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Ints = { Queued.Num() };
		// Every living dino on her side has an order: time resumes straight away.
		if (Queued.Num() >= Sim.AliveCount(0))
		{
			EndSlowTime(Now);
			RefreshViews();
		}
		return Res;
	}
	if (R.Verb == Possess)
	{
		const FName Res = Sim.Possess(Key, R.Int(0));
		RefreshViews();
		return FromName(Res);
	}
	if (R.Verb == Cycle)
	{
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Ints = { Sim.Cycle(Key) };
		RefreshViews();
		return Res;
	}
	if (R.Verb == Release)
	{
		Sim.Release(Key);
		RefreshViews();
		return FGFModeResult::Ok();
	}
	if (R.Verb == Input)
	{
		return FromName(Sim.SetInput(Key, FVector2D(R.Float(0), R.Float(1)), R.Int(0) != 0, R.Int(1, INDEX_NONE)));
	}
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

FGFModeResult AGFArenaDirector::DoPlace(AGFPlayerState* By, const TArray<FGuid>& Ids, const TArray<int32>& Spots)
{
	if (Ids.Num() == 0 || Ids.Num() > 3 || Ids.Num() != Spots.Num())
	{
		return FGFModeResult::Fail(TEXT("BadPlacement"));
	}
	TSet<int32> SeenSpots;
	TSet<FGuid> SeenIds;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		if (!FGFCompanions::Find(By->Companions, Ids[i]))
		{
			return FGFModeResult::Fail(TEXT("NotYourDino"));
		}
		if (Spots[i] < 0 || Spots[i] > 5 || SeenSpots.Contains(Spots[i]) || SeenIds.Contains(Ids[i]))
		{
			return FGFModeResult::Fail(TEXT("BadPlacement"));
		}
		SeenSpots.Add(Spots[i]);
		SeenIds.Add(Ids[i]);
	}
	int32 Others = 0;
	for (const FGFArenaEntry& E : Entries)
	{
		Others += E.PlayerKey != By->PlayerKey ? E.CompanionIds.Num() : 0;
	}
	if (Others + Ids.Num() > 6)
	{
		return FGFModeResult::Fail(TEXT("TeamFull")); // team cap 6
	}
	if (Phase == EGFArenaPhase::Finished)
	{
		Phase = EGFArenaPhase::Idle;
	}
	FGFArenaEntry& E = EntryFor(By->PlayerKey);
	E.CompanionIds = Ids;
	E.Spots = Spots;
	By->Progress.ArenaFormationIds = Ids; // her last formation is remembered
	By->Progress.ArenaFormationSpots = Spots;
	By->MarkCharacterChanged();
	return FGFModeResult::Ok();
}

FGFModeResult AGFArenaDirector::DoWager(AGFPlayerState* By, const TArray<FGFItemStack>& InStake, bool bReroll)
{
	AGFGameState* G = GS();
	FGFInventory Inv;
	if (!G || !G->Accounts().ReadAccount(By->PlayerKey, Inv))
	{
		return FGFModeResult::Fail(TEXT("NotReady"));
	}
	const FGFRecipeRow* Pinned = AGFGameState::FindRecipe(By->Progress.PinnedRecipe);
	const EGFWagerCheck Check = FGFWagerRules::CheckStake(G->GetItemTable(), Inv, InStake, Pinned, By->Progress.bWagersOff || bHostWagersOff);
	if (Check != EGFWagerCheck::Ok)
	{
		return FGFModeResult::Fail(FGFWagerRules::ToString(Check));
	}
	TArray<FGFItemStack> Stake;
	FGFEconomy::NormalizeStacks(InStake, Stake);
	FGFArenaEntry& E = EntryFor(By->PlayerKey);
	if (bReroll)
	{
		if (E.bRerollUsed)
		{
			return FGFModeResult::Fail(TEXT("RerollUsed"));
		}
		E.bRerollUsed = true;
		++E.RerollSeed;
	}
	const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(By->PlayerKey), HashCombine(GetTypeHash(By->Progress.ArenaMatches), GetTypeHash(E.RerollSeed))));
	const TArray<FGFItemStack> Counter = FGFWagerRules::MatchCounter(G->GetItemTable(), Stake, FGFWagerRules::Needs(Inv, Pinned), Seed);
	if (Counter.Num() == 0)
	{
		return FGFModeResult::Fail(TEXT("NoFairCounter"));
	}
	E.Stake = Stake;
	E.Counter = Counter;
	const int32 Band = FGFEconomy::ValueBand(FGFEconomy::StacksValue(G->GetItemTable(), Stake));
	const FGFWagerBandRow* BandRow = FGFArenaData::Get().BandRow(Band);
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Stacks = Counter;
	Res.Ints = { Band, BandRow ? BandRow->Claws : Band };
	return Res;
}

FGFModeResult AGFArenaDirector::DoFight(AGFPlayerState* By)
{
	AGFGameState* G = GS();
	const FGFArenaEntry* Mine = FindEntry(By->PlayerKey);
	if (!G || !Mine || Mine->CompanionIds.Num() == 0)
	{
		return FGFModeResult::Fail(TEXT("NotPlaced"));
	}
	const FGFArenaData& Data = FGFArenaData::Get();
	const UDataTable* Items = G->GetItemTable();

	// Stakes go into escrow first; any failure refunds the ones already in and calls the fight off.
	TArray<int32> Escrowed;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		FGFArenaEntry& E = Entries[i];
		E.Rewards.Reset();
		E.WagerResult = NAME_None;
		E.Stars = 0;
		E.LastStake = E.Stake;
		E.LastCounter = E.Counter;
		if (E.Stake.Num() == 0 || E.CompanionIds.Num() == 0)
		{
			continue;
		}
		AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey);
		FGFInventory Inv;
		const bool bRead = PS && G->Accounts().ReadAccount(E.PlayerKey, Inv);
		const EGFWagerCheck Check = !bRead ? EGFWagerCheck::Insufficient
			: FGFWagerRules::CheckStake(Items, Inv, E.Stake, AGFGameState::FindRecipe(PS->Progress.PinnedRecipe), PS->Progress.bWagersOff || bHostWagersOff);
		E.EscrowId = FGuid::NewGuid();
		if (Check != EGFWagerCheck::Ok || G->ServerDeposit(FGuid::NewGuid(), E.EscrowId, TEXT("Wager"), E.PlayerKey, E.Stake, TEXT("ArenaStake")) != EGFTxResult::Applied)
		{
			for (int32 j : Escrowed)
			{
				G->ServerRefund(FGuid::NewGuid(), Entries[j].EscrowId, TEXT("ArenaCalledOff"));
				Entries[j].bEscrowOpen = false;
			}
			return FGFModeResult::Fail(Check != EGFWagerCheck::Ok ? FName(FGFWagerRules::ToString(Check)) : FName(TEXT("EscrowFailed")));
		}
		E.bEscrowOpen = true;
		Escrowed.Add(i);
	}

	OpponentBand = BandOfEntries();
	const FGFWagerBandRow* BandRow = Data.BandRow(OpponentBand);
	const int32 Seed = ForcedSeed != 0 ? ForcedSeed + MatchCounter : static_cast<int32>(FPlatformTime::Cycles() & 0x3fffffff);
	++MatchCounter;
	Sim.Init(Seed, EGFArenaAI::Normal, BandRow ? BandRow->AI : EGFArenaAI::Normal);
	int32 LevelSum = 0, Count = 0;
	for (const FGFArenaEntry& E : Entries)
	{
		const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey);
		for (int32 i = 0; PS && i < E.CompanionIds.Num(); ++i)
		{
			if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, E.CompanionIds[i]))
			{
				FGFArenaEntrant W;
				W.OwnerKey = E.PlayerKey;
				W.CompanionId = C->Id;
				W.Species = C->Species;
				W.Level = C->Level;
				W.Spot = E.Spots[i];
				Sim.AddUnit(Data, 0, W);
				LevelSum += C->Level;
				++Count;
			}
		}
	}
	if (Count == 0)
	{
		for (FGFArenaEntry& E : Entries)
		{
			SettleWager(E, false, false);
		}
		return FGFModeResult::Fail(TEXT("NotPlaced"));
	}
	// Opponents: as many as her team (at least 3), level from the band.
	const TArray<FName> SpeciesIds = Data.SpeciesIds();
	const int32 AvgLevel = FMath::Max(1, FMath::RoundToInt(float(LevelSum) / Count));
	const int32 Level = FMath::Max(1, AvgLevel + (BandRow ? BandRow->LevelDelta : 0));
	const int32 NumOpp = FMath::Clamp(Count, 3, 6);
	for (int32 i = 0; i < NumOpp; ++i)
	{
		FGFArenaEntrant W;
		W.Species = SpeciesIds.Num() > 0 ? SpeciesIds[Sim.Rng.RandHelper(SpeciesIds.Num())] : NAME_None;
		W.Level = Level;
		W.Spot = i;
		Sim.AddUnit(Data, 1, W);
	}
	Phase = EGFArenaPhase::Fighting;
	Outcome = EGFArenaOutcome::None;
	Slow = FGFSlowTime();
	Queued.Reset();
	LastRealTime = FPlatformTime::Seconds();
	RefreshViews();
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Ints = { OpponentBand, Seed };
	return Res;
}

void AGFArenaDirector::EndSlowTime(double Now)
{
	if (Slow.End(Now))
	{
		SetWorldDilation(1.f);
		Sim.ApplyCommands(FGFArenaData::Get(), Queued); // in the order they were given
		Queued.Reset();
	}
}

void AGFArenaDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	const float RealDt = static_cast<float>(FMath::Clamp(Now - LastRealTime, 0.0, 0.25));
	LastRealTime = Now;
	if (!HasAuthority() || Phase != EGFArenaPhase::Fighting)
	{
		return;
	}
	if (Slow.bActive && Now >= Slow.EndsAt)
	{
		EndSlowTime(Now);
	}
	Sim.Advance(FGFArenaData::Get(), RealDt * SimSpeed * Slow.CurrentDilation());
	if (Sim.IsOver())
	{
		FinishFight();
	}
	RefreshViews();
}

void AGFArenaDirector::RefreshViews()
{
	const double Now = FPlatformTime::Seconds();
	Units.SetNum(Sim.Units.Num());
	for (int32 i = 0; i < Sim.Units.Num(); ++i)
	{
		const FGFArenaUnit& U = Sim.Units[i];
		FGFArenaUnitView& V = Units[i];
		V.Id = U.Id;
		V.Team = U.Team;
		V.OwnerKey = U.OwnerKey;
		V.Species = U.Species;
		V.Level = U.Level;
		V.HP = U.HP;
		V.MaxHP = U.MaxHP;
		V.Pos = U.Pos;
		V.bFainted = U.bFainted;
		V.PossessedBy = U.PossessedBy;
		V.StatusMask = U.StatusMask();
		V.Cooldowns = U.Cooldowns;
		V.AbilityCount = U.AbilityCount;
	}
	FightTime = Sim.Time;
	Combos = Sim.Combos;
	bSlowTimeActive = Slow.bActive;
	SlowTimeRemaining = static_cast<float>(Slow.Remaining(Now));
	SlowTimeCooldown = static_cast<float>(Slow.CooldownLeft(Now));
}

void AGFArenaDirector::SettleWager(FGFArenaEntry& E, bool bWon, bool bLost)
{
	AGFGameState* G = GS();
	if (!G || !E.bEscrowOpen)
	{
		return;
	}
	E.bEscrowOpen = false;
	if (bWon)
	{
		// She keeps her stake and gets the opponent's counter-bet.
		G->ServerRefund(FGuid::NewGuid(), E.EscrowId, TEXT("ArenaStakeBack"));
		G->ServerMint(FGuid::NewGuid(), E.PlayerKey, E.Counter, TEXT("ArenaWagerWin"));
		E.WagerResult = TEXT("Won");
	}
	else if (bLost)
	{
		G->ServerForfeit(FGuid::NewGuid(), E.EscrowId, TEXT("ArenaWagerLost")); // only her stake
		E.WagerResult = TEXT("Lost");
	}
	else
	{
		G->ServerRefund(FGuid::NewGuid(), E.EscrowId, TEXT("ArenaWagerReturned"));
		E.WagerResult = TEXT("Returned");
	}
}

void AGFArenaDirector::FinishFight()
{
	AGFGameState* G = GS();
	EndSlowTime(FPlatformTime::Seconds());
	Slow = FGFSlowTime();
	Queued.Reset();
	SetWorldDilation(1.f);
	Outcome = Sim.Outcome;
	const bool bWon = Outcome == EGFArenaOutcome::Team0Won;
	const bool bLost = Outcome == EGFArenaOutcome::Team1Won;
	const int32 Stars = Sim.StarsFor(0);
	const FGFArenaData& Data = FGFArenaData::Get();
	FRandomStream Rng(Sim.Seed ^ 0x5eed);
	for (FGFArenaEntry& E : Entries)
	{
		if (E.CompanionIds.Num() == 0)
		{
			continue;
		}
		SettleWager(E, bWon, bLost);
		E.Stars = Stars;
		// The free reward roll: every match, win or not.
		E.Rewards.Reset();
		if (const FGFArenaRewardRow* Row = Data.RewardFor(Stars))
		{
			FGFRewards::Roll(Row->Rolls, Rng, 1.f, E.Rewards);
		}
		if (G && E.Rewards.Num() > 0)
		{
			G->ServerMint(FGuid::NewGuid(), E.PlayerKey, E.Rewards, TEXT("ArenaReward"));
		}
		AGFPlayerState* PS = G ? G->FindPlayerByKey(E.PlayerKey) : nullptr;
		if (PS && !PS->bLoggedOut)
		{
			for (const FGuid& Id : E.CompanionIds)
			{
				if (FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, Id))
				{
					FGFCompanions::AddXP(*C, 1 + Stars); // ability 3 at level 3
				}
			}
			++PS->Progress.ArenaMatches;
			PS->Progress.ArenaWins += bWon ? 1 : 0;
			PS->MarkCharacterChanged();
			PS->NotifyLand(bWon ? FString::Printf(TEXT("Arena win! %d star(s)"), Stars) : (bLost ? FString(TEXT("Arena: so close! Rewards still earned.")) : FString(TEXT("Arena: a draw. Stakes returned."))));
		}
		E.Stake.Reset();
		E.Counter.Reset();
		E.bRerollUsed = false;
	}
	Phase = EGFArenaPhase::Finished;
	RefreshViews();
}

void AGFArenaDirector::AbortFight()
{
	if (Phase != EGFArenaPhase::Fighting)
	{
		for (FGFArenaEntry& E : Entries)
		{
			SettleWager(E, false, false);
		}
		return;
	}
	Sim.Outcome = EGFArenaOutcome::Draw;
	FinishFight();
}

void AGFArenaDirector::OnModePlayerLeft(AGFPlayerState* PS)
{
	if (!PS)
	{
		return;
	}
	const FString Key = PS->PlayerKey;
	if (FGFArenaEntry* E = Entries.FindByPredicate([&](const FGFArenaEntry& X) { return X.PlayerKey == Key; }))
	{
		SettleWager(*E, false, false); // a disconnect or quit refunds the stake
		E->Stake.Reset();
		E->Counter.Reset();
		E->CompanionIds.Reset(); // their dinos fight on under AI; no payout for a player who left
		if (Phase != EGFArenaPhase::Fighting)
		{
			Entries.RemoveAll([&](const FGFArenaEntry& X) { return X.PlayerKey == Key; });
		}
	}
	Sim.Release(Key);
	if (Phase == EGFArenaPhase::Fighting)
	{
		const bool bAnyone = Entries.ContainsByPredicate([](const FGFArenaEntry& X) { return X.CompanionIds.Num() > 0; });
		if (!bAnyone)
		{
			AbortFight(); // nobody left to play for: a draw
		}
		else if (Slow.bActive)
		{
			EndSlowTime(FPlatformTime::Seconds()); // never left stuck slow
		}
	}
	RefreshViews();
}
