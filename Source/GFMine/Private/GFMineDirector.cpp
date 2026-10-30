#include "GFMineDirector.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFEconomy.h"
#include "EngineUtils.h"
#include "Engine/World.h"

const FName AGFMineDirector::ModeName(TEXT("Mine"));

AGFMineDirector::AGFMineDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Rng.Initialize(FMath::Rand());
}

AGFMineDirector* AGFMineDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFMineDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<AGFMineDirector>(P);
	}
	return nullptr;
}

void AGFMineDirector::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (AGFGameState* G = GS())
		{
			G->RegisterModeHandler(this);
		}
	}
}

void AGFMineDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
	}
	Super::EndPlay(Reason);
}

AGFGameState* AGFMineDirector::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

int64 AGFMineDirector::UtcNowTicks() const
{
	return UtcNowTicksOverride != 0 ? UtcNowTicksOverride : FDateTime::UtcNow().GetTicks();
}

FName AGFMineDirector::CodeOf(EGFMineOrder O)
{
	const UEnum* E = StaticEnum<EGFMineOrder>();
	return FName(*E->GetNameStringByValue(static_cast<int64>(O)));
}

FGFMineOffline AGFMineDirector::CollectOffline(AGFPlayerState* PS)
{
	FGFMineOffline Out;
	if (!PS)
	{
		return Out;
	}
	FGFMineCrew& Crew = PS->Progress.Crew;
	const int64 Now = UtcNowTicks();
	if (Crew.LastSeenUtcTicks > 0)
	{
		const double Gap = static_cast<double>(Now - Crew.LastSeenUtcTicks) / ETimespan::TicksPerSecond;
		const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(PS->PlayerKey), GetTypeHash(Crew.LastSeenUtcTicks)));
		Out = FGFMineRules::Offline(Crew, Gap, Seed, GS() ? GS()->GetItemTable() : nullptr);
		if (Out.Items.Num() > 0 && GS())
		{
			GS()->ServerMint(FGuid::NewGuid(), PS->PlayerKey, Out.Items, TEXT("MineOffline"));
		}
		Crew.LastOffline = Out.Items;
		Crew.LastOfflineHours = Out.Hours;
	}
	Crew.LastSeenUtcTicks = Now;
	PS->MarkCharacterChanged();
	return Out;
}

void AGFMineDirector::OnModePlayerReady(AGFPlayerState* PS)
{
	CollectOffline(PS);
}

void AGFMineDirector::OnModePlayerLeft(AGFPlayerState* PS)
{
	if (PS)
	{
		PS->Progress.Crew.LastSeenUtcTicks = UtcNowTicks(); // the offline clock starts now
		PS->MarkCharacterChanged();
	}
}

FGFModeResult AGFMineDirector::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& R)
{
	if (!By)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	FGFMineCrew& Crew = By->Progress.Crew;
	auto Done = [&](EGFMineOrder O)
	{
		if (O == EGFMineOrder::Ok)
		{
			By->MarkCharacterChanged();
			return FGFModeResult::Ok();
		}
		return FGFModeResult::Fail(CodeOf(O));
	};
	if (R.Verb == TEXT("Capture"))
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (const double* Until = HiddenUntil.Find(By->PlayerKey); Until && Now < *Until && By->Progress.bHasTamed)
		{
			return FGFModeResult::Fail(TEXT("Hiding"));
		}
		const EGFMineOrder O = FGFMineRules::CheckCapture(By->Progress, R.Float(0));
		if (O == EGFMineOrder::Startled)
		{
			HiddenUntil.Add(By->PlayerKey, Now + HideSeconds);
		}
		if (O != EGFMineOrder::Ok)
		{
			return Done(O);
		}
		FGFModeResult Res = Done(O);
		const FGFPebbling& P = FGFMineRules::AddPebbling(Crew);
		Res.Ints = { Crew.Pebblings.Num() };
		if (Crew.LastSeenUtcTicks == 0)
		{
			Crew.LastSeenUtcTicks = UtcNowTicks();
		}
		UE_LOG(LogTemp, Log, TEXT("Mine: %s befriended pebbling %s"), *By->PlayerKey, *P.Name.ToString());
		return Res;
	}
	if (R.Verb == TEXT("Assign"))
	{
		return Done(FGFMineRules::Assign(Crew, R.Id(0), R.Name(0)));
	}
	if (R.Verb == TEXT("Haul"))
	{
		return Done(FGFMineRules::Haul(Crew, R.Id(0)));
	}
	if (R.Verb == TEXT("Recall"))
	{
		return Done(FGFMineRules::Recall(Crew, R.Id(0)));
	}
	if (R.Verb == TEXT("Buy"))
	{
		EGFMineOrder Why;
		const FGFMineUpgradeRow* Row = FGFMineRules::NextUpgrade(Crew, R.Name(0), Why);
		if (!Row)
		{
			return Done(Why);
		}
		AGFGameState* G = GS();
		if (!G || G->ServerBurn(FGuid::NewGuid(), By->PlayerKey, Row->Cost, TEXT("MineUpgrade")) != EGFTxResult::Applied)
		{
			return FGFModeResult::Fail(TEXT("CantAfford"));
		}
		Crew.SetLevel(Row->Upgrade, Row->Level);
		FGFModeResult Res = Done(EGFMineOrder::Ok);
		Res.Ints = { Row->Level };
		return Res;
	}
	if (R.Verb == TEXT("Status"))
	{
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Stacks = Crew.LastOffline;
		Res.Ints = { FMath::RoundToInt(Crew.LastOfflineHours * 60.f) };
		return Res;
	}
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

void AGFMineDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AGFGameState* G = GS();
	if (!HasAuthority() || !G)
	{
		return;
	}
	SeenTimer += DeltaSeconds;
	const bool bStamp = SeenTimer >= 10.f;
	if (bStamp)
	{
		SeenTimer = 0.f;
	}
	for (TActorIterator<AGFPlayerState> It(GetWorld()); It; ++It)
	{
		AGFPlayerState* PS = *It;
		if (!PS || PS->bLoggedOut || PS->Progress.Crew.Pebblings.Num() == 0)
		{
			continue;
		}
		TArray<FGFItemStack> Home;
		const bool bChanged = FGFMineRules::Tick(PS->Progress.Crew, DeltaSeconds * SimSpeed, Rng, Home);
		if (Home.Num() > 0)
		{
			G->ServerMint(FGuid::NewGuid(), PS->PlayerKey, Home, TEXT("MineHaul"));
		}
		if (bStamp)
		{
			PS->Progress.Crew.LastSeenUtcTicks = UtcNowTicks();
		}
		if (bChanged || bStamp)
		{
			PS->MarkCharacterChanged();
		}
	}
}
