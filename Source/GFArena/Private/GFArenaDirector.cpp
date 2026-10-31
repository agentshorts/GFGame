#include "GFArenaDirector.h"
#include "GFArenaRules.h"
#include "GFArenaSite.h"
#include "GFMiniGameCatalog.h"
#include "GFWager.h"
#include "GFEconomy.h"
#include "GFDataTables.h"
#include "GFDinoTypes.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFCompanionRoster.h"
#include "GFRecipeTypes.h"
#include "Components/SceneComponent.h"
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
	static const FName Snack(TEXT("Snack"));
	static const FName Loadout(TEXT("Loadout"));
	static const FName SideBet(TEXT("SideBet"));
	static const FName Spell(TEXT("Spell"));
	static const FName AcceptGhost(TEXT("AcceptGhost"));
	static const FName Ok(TEXT("Ok"));
}

namespace
{
	static FString PairKey(const FString& A, const FString& B)
	{
		return A < B ? A + TEXT("|") + B : B + TEXT("|") + A;
	}

	static bool FoodIsFavourite(FName Species, FName Food)
	{
		const UDataTable* Diet = FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
		const FDinoDietRow* Row = Diet ? Diet->FindRow<FDinoDietRow>(Species, TEXT("ArenaSnack"), false) : nullptr;
		return Row && Row->IsFavourite(Food);
	}
}

AGFArenaDirector::AGFArenaDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.f);

	// GetActorLocation reads the root. Without one it is always the origin, so the four
	// sims look stacked even when Get() asked for the pad. Movable so the hub can park it later.
	USceneComponent* PadRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PadRoot"));
	PadRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = PadRoot;
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
		const FVector At = FGFMiniGameCatalog::DefaultSpawn(ModeName);
		AGFArenaDirector* Director = World->SpawnActor<AGFArenaDirector>(AGFArenaDirector::StaticClass(), FTransform(At), P);
		if (Director)
		{
			Director->SetActorLocation(At);
		}
		return Director;
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
		EnsurePlayableSite();
	}
}

void AGFArenaDirector::EnsurePlayableSite()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	// The pad shares the hub. Slow Time must not dilate everyone standing on Rime.
	bApplyWorldDilation = false;
	if (AGFGameState* G = GS())
	{
		G->RegisterModeHandler(this);
	}
	AGFArenaSite* Site = AGFArenaSite::Get(World);
	if (!Site)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Site = World->SpawnActor<AGFArenaSite>(AGFArenaSite::StaticClass(), FTransform(GetActorLocation()), Params);
	}
	if (!Site)
	{
		return;
	}
	Site->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Site->BuildIfNeeded();
}

bool AGFArenaDirector::ResolvePlayableBout()
{
	if (!HasAuthority() || Phase != EGFArenaPhase::Fighting)
	{
		return false;
	}
	Sim.RunToEnd(FGFArenaData::Get());
	FinishFight();
	return Phase == EGFArenaPhase::Finished;
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

void AGFArenaDirector::SetFamilyRep(FName Family, int32 Rep)
{
	if (Family.IsNone())
	{
		return;
	}
	RepBook.Add(Family, Rep);
}

int32 AGFArenaDirector::GetFamilyRep(FName Family) const
{
	const int32* Found = RepBook.Find(Family);
	return Found ? *Found : 0;
}

int32 AGFArenaDirector::GetRentalPriceStep(FName Family) const
{
	const int32* Found = RentalPriceSteps.Find(Family);
	return Found ? *Found : 0;
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
		int32 OtherPlayers = 0;
		TMap<FName, int32> SpeciesCount;
		if (AGFGameState* G = GS())
		{
			for (const FGFArenaEntry& E : Entries)
			{
				if (E.PlayerKey == Key || E.CompanionIds.Num() == 0)
				{
					continue;
				}
				++OtherPlayers;
				if (const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey))
				{
					for (const FGuid& Id : E.CompanionIds)
					{
						if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, Id))
						{
							SpeciesCount.FindOrAdd(C->Species) += 1;
						}
					}
				}
			}
		}
		const int32 Cap = FGFArenaRosterRules::PerPlayerCap(OtherPlayers + 1);
		TArray<FGuid> Ids;
		TArray<int32> Spots;
		int32 Front = 0, Back = 3;
		for (const FGFCompanionEntry& C : Roster)
		{
			if (C.bElder || Ids.Num() >= Cap)
			{
				continue;
			}
			const int32 Have = SpeciesCount.FindRef(C.Species);
			if (Have >= FGFArenaRosterRules::SpeciesCap)
			{
				continue;
			}
			const FGFArenaSpeciesRow* Row = Data.Species.Find(C.Species);
			const bool bFrontRole = Row && (Row->Role == EGFArenaRole::Tank || Row->Role == EGFArenaRole::Bruiser);
			Ids.Add(C.Id);
			Spots.Add(bFrontRole || Back > 5 ? Front++ : Back++); // tanks in front, fragile ones behind
			SpeciesCount.FindOrAdd(C.Species) += 1;
		}
		if (Ids.Num() == 0)
		{
			return FGFModeResult::Fail(TEXT("NotPlaced"));
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
	if (R.Verb == Snack)
	{
		return bFighting ? FGFModeResult::Fail(TEXT("Fighting")) : DoSnack(By, R.Id(0), R.Name(0));
	}
	if (R.Verb == Loadout)
	{
		return bFighting ? FGFModeResult::Fail(TEXT("Fighting")) : DoLoadout(By, R.Names);
	}
	if (R.Verb == SideBet)
	{
		return bFighting ? FGFModeResult::Fail(TEXT("Fighting")) : DoSideBet(By, R.Stacks, R.Int(0));
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
		Ghosts.Reset();
		Sim.bSpellsOpen = true;
		Sim.BuildGhostOrders(Data, 0, Ghosts);
		SetWorldDilation(FGFSlowTime::Dilation);
		if (AGFGameState* G = GS())
		{
			if (AGFPlayerState* PS = G->FindPlayerByKey(Key))
			{
				++PS->Progress.ArenaSlowUses;
				PS->MarkCharacterChanged();
			}
		}
		RefreshViews();
		FGFModeResult Opened = FGFModeResult::Ok();
		Opened.Ints = { Ghosts.Num() };
		return Opened;
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
		// Every living dino on the player's side has an order: time resumes straight away.
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
	if (R.Verb == Spell)
	{
		return DoSpell(Key, R.Name(0), R.Int(0, INDEX_NONE), R.Int(1) != 0);
	}
	if (R.Verb == AcceptGhost)
	{
		return DoAcceptGhost(Key, R.Int(0));
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
	TArray<FName> Adding;
	for (int32 i = 0; i < Ids.Num(); ++i)
	{
		const FGFCompanionEntry* Mine = FGFCompanions::Find(By->Companions, Ids[i]);
		if (!Mine)
		{
			return FGFModeResult::Fail(TEXT("NotYourDino"));
		}
		if (Mine->bElder)
		{
			return FGFModeResult::Fail(TEXT("Elder"));
		}
		if (Spots[i] < 0 || Spots[i] > 5 || SeenSpots.Contains(Spots[i]) || SeenIds.Contains(Ids[i]))
		{
			return FGFModeResult::Fail(TEXT("BadPlacement"));
		}
		SeenSpots.Add(Spots[i]);
		SeenIds.Add(Ids[i]);
		Adding.Add(Mine->Species);
	}
	int32 Others = 0;
	int32 OtherPlayers = 0;
	TArray<FName> SideSpecies;
	if (AGFGameState* G = GS())
	{
		for (const FGFArenaEntry& E : Entries)
		{
			if (E.PlayerKey == By->PlayerKey || E.CompanionIds.Num() == 0)
			{
				continue;
			}
			++OtherPlayers;
			Others += E.CompanionIds.Num();
			if (const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey))
			{
				for (const FGuid& Id : E.CompanionIds)
				{
					if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, Id))
					{
						SideSpecies.Add(C->Species);
					}
				}
			}
		}
	}
	else
	{
		for (const FGFArenaEntry& E : Entries)
		{
			if (E.PlayerKey != By->PlayerKey)
			{
				Others += E.CompanionIds.Num();
				OtherPlayers += E.CompanionIds.Num() > 0 ? 1 : 0;
			}
		}
	}
	const FName Placement = FGFArenaRosterRules::CheckPlacement(OtherPlayers + 1, Others, SideSpecies, Adding);
	if (Placement != GFArenaVerbs::Ok)
	{
		return FGFModeResult::Fail(Placement);
	}
	if (Phase == EGFArenaPhase::Finished)
	{
		Phase = EGFArenaPhase::Idle;
	}
	FGFArenaEntry& E = EntryFor(By->PlayerKey);
	E.CompanionIds = Ids;
	E.Spots = Spots;
	E.FedIds.RemoveAll([&](const FGuid& Id) { return !SeenIds.Contains(Id); });
	By->Progress.ArenaFormationIds = Ids; // their last formation is remembered
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
	const double Now = FPlatformTime::Seconds();
	for (const FGFArenaEntry& Other : Entries)
	{
		if (Other.PlayerKey == By->PlayerKey || Other.Stake.Num() == 0)
		{
			continue;
		}
		const TArray<double>* Times = PairWagerTimes.Find(PairKey(By->PlayerKey, Other.PlayerKey));
		if (Times && FGFArenaCheese::WagersBlocked(*Times, Now))
		{
			return FGFModeResult::Fail(TEXT("WagerLimit"));
		}
	}
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

FGFModeResult AGFArenaDirector::DoSnack(AGFPlayerState* By, const FGuid& CompanionId, FName Food)
{
	AGFGameState* G = GS();
	const FGFArenaEntry* E = FindEntry(By->PlayerKey);
	const bool bPlaced = E && E->CompanionIds.Contains(CompanionId);
	const bool bFed = E && E->FedIds.Contains(CompanionId);
	const FGFCompanionEntry* Companion = FGFCompanions::Find(By->Companions, CompanionId);
	FGFInventory Inv;
	const bool bRead = G && G->Accounts().ReadAccount(By->PlayerKey, Inv);
	const bool bHas = bRead && Inv.Has(Food, 1);
	const bool bFavourite = Companion && FoodIsFavourite(Companion->Species, Food);
	const FName Check = FGFArenaSnack::Check(bPlaced, bFed, bHas, bFavourite);
	if (Check != GFArenaVerbs::Ok)
	{
		return FGFModeResult::Fail(Check);
	}
	if (G->ServerBurn(FGuid::NewGuid(), By->PlayerKey, { FGFItemStack(Food, 1) }, TEXT("ArenaSnack")) != EGFTxResult::Applied)
	{
		return FGFModeResult::Fail(TEXT("NoFood"));
	}
	EntryFor(By->PlayerKey).FedIds.Add(CompanionId);
	return FGFModeResult::Ok();
}

FGFModeResult AGFArenaDirector::DoLoadout(AGFPlayerState* By, const TArray<FName>& Spells)
{
	const FGFArenaData& Data = FGFArenaData::Get();
	if (Spells.Num() > 4)
	{
		return FGFModeResult::Fail(TEXT("LoadoutFull"));
	}
	TArray<FName> Kept;
	for (const FName Id : Spells)
	{
		if (Id.IsNone() || !Data.Spell(Id))
		{
			return FGFModeResult::Fail(TEXT("BadSpell"));
		}
		if (!Kept.Contains(Id))
		{
			Kept.Add(Id);
		}
	}
	EntryFor(By->PlayerKey).Loadout = Kept;
	return FGFModeResult::Ok();
}

FGFModeResult AGFArenaDirector::DoSideBet(AGFPlayerState* By, const TArray<FGFItemStack>& InStake, int32 Side)
{
	if (Side != 0 && Side != 1)
	{
		return FGFModeResult::Fail(TEXT("BadSide"));
	}
	if (const FGFArenaEntry* E = FindEntry(By->PlayerKey))
	{
		if (E->CompanionIds.Num() > 0)
		{
			return FGFModeResult::Fail(TEXT("NotSpectator"));
		}
	}
	for (const FGFArenaSideEscrow& Bet : SideEscrow)
	{
		if (Bet.Key == By->PlayerKey && Bet.bOpen)
		{
			return FGFModeResult::Fail(TEXT("AlreadyBet"));
		}
	}
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
	FGFArenaSideEscrow Bet;
	Bet.Key = By->PlayerKey;
	Bet.Side = Side;
	Bet.Stake = Stake;
	Bet.EscrowId = FGuid::NewGuid();
	if (G->ServerDeposit(FGuid::NewGuid(), Bet.EscrowId, TEXT("SideBet"), Bet.Key, Bet.Stake, TEXT("ArenaSideBet")) != EGFTxResult::Applied)
	{
		return FGFModeResult::Fail(TEXT("EscrowFailed"));
	}
	Bet.bOpen = true;
	SideEscrow.Add(Bet);
	return FGFModeResult::Ok();
}

FGFModeResult AGFArenaDirector::DoSpell(const FString& Key, FName SpellId, int32 TargetId, bool bAlt)
{
	if (!Slow.bActive)
	{
		return FGFModeResult::Fail(TEXT("NoSlowTime"));
	}
	const FName Code = Sim.CastSpell(FGFArenaData::Get(), 0, SpellId, TargetId, bAlt, true);
	RefreshViews();
	return Code == GFArenaVerbs::Ok ? FGFModeResult::Ok() : FGFModeResult::Fail(Code);
}

FGFModeResult AGFArenaDirector::DoAcceptGhost(const FString& Key, int32 UnitId)
{
	if (!Slow.bActive)
	{
		return FGFModeResult::Fail(TEXT("NoSlowTime"));
	}
	const FGFArenaCommand* Ghost = Ghosts.FindByPredicate([&](const FGFArenaCommand& Cmd) { return Cmd.UnitId == UnitId; });
	if (!Ghost || Ghost->Slot < 0)
	{
		return FGFModeResult::Fail(TEXT("NoOrder"));
	}
	FGFArenaCommand Cmd = *Ghost;
	Cmd.IssuerKey = Key;
	const FName Check = Sim.CheckCommand(FGFArenaData::Get(), Cmd);
	if (Check != GFArenaVerbs::Ok)
	{
		return FGFModeResult::Fail(Check);
	}
	if (Queued.ContainsByPredicate([&](const FGFArenaCommand& Q) { return Q.UnitId == Cmd.UnitId; }))
	{
		return FGFModeResult::Fail(TEXT("AlreadyCommanded"));
	}
	Queued.Add(Cmd);
	if (Queued.Num() >= Sim.AliveCount(0))
	{
		EndSlowTime(FPlatformTime::Seconds());
		RefreshViews();
	}
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Ints = { Cmd.Slot, Cmd.TargetId };
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

	TSet<FName> Insulted;
	for (const FGFArenaEntry& E : Entries)
	{
		if (E.Stake.Num() == 0)
		{
			continue;
		}
		const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey);
		for (int32 i = 0; PS && i < E.CompanionIds.Num(); ++i)
		{
			const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, E.CompanionIds[i]);
			if (!C || !C->bRental || C->Family.IsNone() || Insulted.Contains(C->Family))
			{
				continue;
			}
			for (const FGFItemStack& Stack : E.Stake)
			{
				if (FoodIsFavourite(C->Species, Stack.ItemId))
				{
					RepBook.FindOrAdd(C->Family) += FGFArenaRep::InsultDelta;
					Insulted.Add(C->Family);
					break;
				}
			}
		}
	}

	EGFArenaAI RivalAI = BandRow ? BandRow->AI : EGFArenaAI::Normal;
	for (const FGFArenaEntry& E : Entries)
	{
		const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey);
		for (int32 i = 0; PS && i < E.CompanionIds.Num(); ++i)
		{
			if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, E.CompanionIds[i]))
			{
				if (!C->Family.IsNone() && FGFArenaRep::ForcesChampion(GetFamilyRep(C->Family)))
				{
					RivalAI = EGFArenaAI::Champion;
				}
			}
		}
	}
	Sim.Init(Seed, EGFArenaAI::Normal, RivalAI);
	int32 LevelSum = 0, Count = 0;
	TArray<FName> PlayerSpells;
	for (FGFArenaEntry& E : Entries)
	{
		E.Posts.Reset();
		E.PurseCredited = 0;
		E.ElderCut = 0;
		const AGFPlayerState* PS = G->FindPlayerByKey(E.PlayerKey);
		for (const FName SpellId : E.Loadout)
		{
			if (PlayerSpells.Num() >= 4)
			{
				break;
			}
			if (!SpellId.IsNone() && !PlayerSpells.Contains(SpellId))
			{
				PlayerSpells.Add(SpellId);
			}
		}
		for (int32 i = 0; PS && i < E.CompanionIds.Num(); ++i)
		{
			if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, E.CompanionIds[i]))
			{
				const int32 Rep = GetFamilyRep(C->Family);
				FGFArenaEntrant W;
				W.OwnerKey = E.PlayerKey;
				W.CompanionId = C->Id;
				W.Species = C->Species;
				W.Level = C->Level;
				W.Spot = E.Spots[i];
				W.bRental = C->bRental;
				W.Family = C->Family;
				W.bWellFed = E.FedIds.Contains(C->Id) || (C->bRental && FGFArenaRep::ArrivesWellFed(Rep));
				W.bControlBlocked = C->bRental && !FGFArenaRep::CanControl(Rep);
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
	const double Now = FPlatformTime::Seconds();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].Stake.Num() == 0)
		{
			continue;
		}
		for (int32 j = i + 1; j < Entries.Num(); ++j)
		{
			if (Entries[j].Stake.Num() == 0)
			{
				continue;
			}
			PairWagerTimes.FindOrAdd(PairKey(Entries[i].PlayerKey, Entries[j].PlayerKey)).Add(Now);
		}
	}
	// Opponents: as many as the player's team (at least 3), level from the band.
	const TArray<FName> SpeciesIds = Data.SpeciesIds();
	const int32 AvgLevel = FMath::Max(1, FMath::RoundToInt(float(LevelSum) / Count));
	const int32 Level = FMath::Max(1, AvgLevel + (BandRow ? BandRow->LevelDelta : 0));
	const int32 NumOpp = FMath::Clamp(Count, 3, 6);
	TMap<FName, int32> UsedSpecies;
	for (int32 i = 0; i < NumOpp; ++i)
	{
		FGFArenaEntrant W;
		const int32 Roll = SpeciesIds.Num() > 0 ? Sim.Rng.RandHelper(SpeciesIds.Num()) : 0;
		W.Species = FGFArenaRosterRules::PickSpecies(SpeciesIds, UsedSpecies, Roll);
		UsedSpecies.FindOrAdd(W.Species) += 1;
		W.Level = Level;
		W.Spot = i;
		Sim.AddUnit(Data, 1, W);
	}
	Sim.SetLoadout(0, PlayerSpells);
	if (RivalAI == EGFArenaAI::Champion && BandRow && BandRow->RivalSpells > 0)
	{
		TArray<FName> RivalSpells;
		for (const FName SpellId : Data.SpellOrder)
		{
			if (RivalSpells.Num() >= BandRow->RivalSpells)
			{
				break;
			}
			RivalSpells.Add(SpellId);
		}
		Sim.SetLoadout(1, RivalSpells);
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
		Sim.bSpellsOpen = false;
		Sim.ApplyCommands(FGFArenaData::Get(), Queued); // in the order they were given
		Queued.Reset();
		Ghosts.Reset();
		Sim.OnSlowWindowEnded();
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
	const float SimDt = RealDt * SimSpeed * Slow.CurrentDilation();
	Sim.Advance(FGFArenaData::Get(), SimDt);
	for (const FGFArenaUnit& U : Sim.Units)
	{
		if (!U.PossessedBy.IsEmpty())
		{
			ControlSeconds.FindOrAdd(U.PossessedBy) += SimDt;
		}
	}
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

void AGFArenaDirector::RefundSide(FGFArenaSideEscrow& Bet, FName Reason)
{
	if (!Bet.bOpen)
	{
		return;
	}
	Bet.bOpen = false;
	if (AGFGameState* G = GS())
	{
		G->ServerRefund(FGuid::NewGuid(), Bet.EscrowId, Reason);
	}
}

void AGFArenaDirector::SettleSideBets()
{
	AGFGameState* G = GS();
	const UDataTable* Items = G ? G->GetItemTable() : nullptr;
	TArray<FGFArenaSideBet> Views;
	for (const FGFArenaSideEscrow& Bet : SideEscrow)
	{
		if (!Bet.bOpen || Bet.Key.IsEmpty())
		{
			continue;
		}
		FGFArenaSideBet View;
		View.Key = Bet.Key;
		View.Side = Bet.Side;
		View.Value = FGFEconomy::StacksValue(Items, Bet.Stake);
		Views.Add(View);
	}
	const FString Winner = FGFArenaSidePots::PotWinner(Outcome, Views);
	TArray<FGFItemStack> Pot;
	for (FGFArenaSideEscrow& Bet : SideEscrow)
	{
		if (!Bet.bOpen)
		{
			continue;
		}
		const bool bWinningSide = (Outcome == EGFArenaOutcome::Team0Won && Bet.Side == 0) || (Outcome == EGFArenaOutcome::Team1Won && Bet.Side == 1);
		if (Winner.IsEmpty() || bWinningSide)
		{
			RefundSide(Bet, TEXT("ArenaSideReturned"));
			continue;
		}
		if (G)
		{
			G->ServerForfeit(FGuid::NewGuid(), Bet.EscrowId, TEXT("ArenaSideLost"));
		}
		Pot.Append(Bet.Stake);
		Bet.bOpen = false;
	}
	if (G && !Winner.IsEmpty() && Pot.Num() > 0)
	{
		TArray<FGFItemStack> Paid;
		FGFEconomy::NormalizeStacks(Pot, Paid);
		G->ServerMint(FGuid::NewGuid(), Winner, Paid, TEXT("ArenaSidePot"));
	}
	SideEscrow.Reset();
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
		// The player keeps their stake and gets the opponent's counter-bet.
		G->ServerRefund(FGuid::NewGuid(), E.EscrowId, TEXT("ArenaStakeBack"));
		G->ServerMint(FGuid::NewGuid(), E.PlayerKey, E.Counter, TEXT("ArenaWagerWin"));
		E.WagerResult = TEXT("Won");
	}
	else if (bLost)
	{
		G->ServerForfeit(FGuid::NewGuid(), E.EscrowId, TEXT("ArenaWagerLost")); // only their stake
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
		// The free reward roll: every match, win or not. After the hourly cap the table is the 0-star row.
		// The stars on the entry stay the real ones.
		const double Now = FPlatformTime::Seconds();
		TArray<double>& Rolls = RollStamps.FindOrAdd(E.PlayerKey);
		const int32 TableStars = FGFArenaCheese::RollStars(Stars, FGFArenaCheese::CountInWindow(Rolls, Now, FGFArenaCheese::HourSeconds));
		Rolls.Add(Now);
		E.Rewards.Reset();
		if (const FGFArenaRewardRow* Row = Data.RewardFor(TableStars))
		{
			FGFRewards::Roll(Row->Rolls, Rng, 1.f, E.Rewards);
		}
		if (G && E.Rewards.Num() > 0)
		{
			G->ServerMint(FGuid::NewGuid(), E.PlayerKey, E.Rewards, TEXT("ArenaReward"));
		}
		const int32 RewardValue = G ? static_cast<int32>(FGFEconomy::StacksValue(G->GetItemTable(), E.Rewards)) : 0;
		E.PurseCredited = FGFArenaPurse::Purse(RewardValue);
		int32 Winnings = RewardValue;
		if (bWon && G)
		{
			Winnings += static_cast<int32>(FGFEconomy::StacksValue(G->GetItemTable(), E.LastCounter));
		}
		bool bAnyRental = false;
		AGFPlayerState* PS = G ? G->FindPlayerByKey(E.PlayerKey) : nullptr;
		if (PS && !PS->bLoggedOut)
		{
			for (const FGuid& Id : E.CompanionIds)
			{
				if (FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, Id))
				{
					FGFCompanions::AddXP(*C, 1 + Stars); // ability 3 at level 3
					C->LifetimeEarnings += E.PurseCredited;
					bAnyRental = bAnyRental || C->bRental;
				}
			}
			++PS->Progress.ArenaMatches;
			PS->Progress.ArenaWins += bWon ? 1 : 0;
			PS->Progress.ArenaStars += Stars;
			if (bWon)
			{
				PS->Progress.ArenaWagerWon += FGFEconomy::StacksValue(G->GetItemTable(), E.LastCounter);
			}
			else if (bLost)
			{
				PS->Progress.ArenaWagerLost += FGFEconomy::StacksValue(G->GetItemTable(), E.LastStake);
			}
			PS->MarkCharacterChanged();
			PS->NotifyLand(bWon ? FString::Printf(TEXT("Arena win! %d star(s)"), Stars) : (bLost ? FString(TEXT("Arena: so close! Rewards still earned.")) : FString(TEXT("Arena: a draw. Stakes returned."))));
		}
		else if (PS)
		{
			for (const FGuid& Id : E.CompanionIds)
			{
				if (const FGFCompanionEntry* C = FGFCompanions::Find(PS->Companions, Id))
				{
					bAnyRental = bAnyRental || C->bRental;
				}
			}
		}
		E.ElderCut = bAnyRental ? FGFArenaPurse::ElderCut(Winnings) : 0;
		for (const FGFArenaQuit& Quit : Sim.Quits)
		{
			if (Quit.OwnerKey != E.PlayerKey)
			{
				continue;
			}
			if (!Quit.Family.IsNone())
			{
				RepBook.FindOrAdd(Quit.Family) += FGFArenaRep::QuitDelta;
				RentalPriceSteps.FindOrAdd(Quit.Family) += 1;
			}
			if (PS && !PS->bLoggedOut)
			{
				++PS->Progress.ArenaRentalsQuit;
				PS->MarkCharacterChanged();
			}
			E.Posts.Add(FGFArenaPost::RentalQuit(Quit.Species).Line);
		}
		if (bWon)
		{
			TSet<FName> PaidWin;
			TSet<FName> Nice;
			for (const FGFArenaUnit& U : Sim.Units)
			{
				if (U.OwnerKey != E.PlayerKey || !U.bRental || !U.IsAlive() || U.Family.IsNone() || PaidWin.Contains(U.Family))
				{
					continue;
				}
				PaidWin.Add(U.Family);
				RepBook.FindOrAdd(U.Family) += FGFArenaRep::WinDelta;
			}
			for (const FGuid& Id : E.CompanionIds)
			{
				const FGFCompanionEntry* C = PS ? FGFCompanions::Find(PS->Companions, Id) : nullptr;
				if (!C || C->Family.IsNone() || Nice.Contains(C->Family))
				{
					continue;
				}
				if (FGFArenaRep::Standing(GetFamilyRep(C->Family)) == EGFArenaStanding::Friendly)
				{
					Nice.Add(C->Family);
					E.Posts.Add(FGFArenaPost::ElderNice(C->Family).Line);
				}
			}
		}
		if (Stars >= 3)
		{
			E.Posts.Add(FGFArenaPost::Brag(Stars).Line);
		}
		E.FedIds.Reset();
		E.Stake.Reset();
		E.Counter.Reset();
		E.bRerollUsed = false;
	}
	SettleSideBets();
	if (G)
	{
		for (const TPair<FString, float>& Pair : ControlSeconds)
		{
			if (AGFPlayerState* Who = G->FindPlayerByKey(Pair.Key))
			{
				Who->Progress.ArenaControlSeconds += Pair.Value;
				Who->MarkCharacterChanged();
			}
		}
	}
	ControlSeconds.Reset();
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
		for (FGFArenaSideEscrow& Bet : SideEscrow)
		{
			RefundSide(Bet, TEXT("ArenaSideReturned"));
		}
		SideEscrow.Reset();
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
	for (FGFArenaSideEscrow& Bet : SideEscrow)
	{
		if (Bet.Key == Key)
		{
			RefundSide(Bet, TEXT("ArenaSideReturned"));
		}
	}
	SideEscrow.RemoveAll([&](const FGFArenaSideEscrow& Bet) { return Bet.Key == Key; });
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
