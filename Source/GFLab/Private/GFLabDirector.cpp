#include "GFLabDirector.h"
#include "GFLabAnomalyMarker.h"
#include "GFMiniGameCatalog.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFCraftingSubsystem.h"
#include "GFRecipeTypes.h"
#include "GFDataTables.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

const FName AGFLabDirector::ModeName(TEXT("Lab"));

AGFLabDirector::AGFLabDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;

	// GetActorLocation reads the root. Without one it is always the origin, so the four
	// sims look stacked even when Get() asked for the pad. Movable so the hub can park it later.
	USceneComponent* PadRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PadRoot"));
	PadRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = PadRoot;
}

void AGFLabDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFLabDirector, Runs);
}

AGFLabDirector* AGFLabDirector::Get(UWorld* World, bool bSpawn)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFLabDirector> It(World); It; ++It)
	{
		return *It;
	}
	if (bSpawn && World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector At = FGFMiniGameCatalog::DefaultSpawn(ModeName);
		AGFLabDirector* Director = World->SpawnActor<AGFLabDirector>(AGFLabDirector::StaticClass(), FTransform(At), P);
		if (Director)
		{
			Director->SetActorLocation(At);
		}
		return Director;
	}
	return nullptr;
}

void AGFLabDirector::BeginPlay()
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

void AGFLabDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority())
	{
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
		for (const FAnomalyMarkerRef& M : AnomalyMarkers)
		{
			if (AActor* Actor = M.Actor.Get())
			{
				Actor->Destroy();
			}
		}
		AnomalyMarkers.Reset();
	}
	Super::EndPlay(Reason);
}

AGFGameState* AGFLabDirector::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

FName AGFLabDirector::CodeOf(EGFLabOrder O)
{
	return FName(*StaticEnum<EGFLabOrder>()->GetNameStringByValue(static_cast<int64>(O)));
}

TArray<FGFItemStack> AGFLabDirector::EndRun(const FString& Key, EGFLabEnd How)
{
	const int32 i = Runs.IndexOfByPredicate([&](const FGFLabRun& R) { return R.Key == Key; });
	if (i == INDEX_NONE)
	{
		return {};
	}
	const FString EndedKey = Runs[i].Key;
	const TArray<FGFItemStack> Home = FGFLabRules::Bank(Runs[i].Unbanked, How);
	DestroyAnomalyMarkers(EndedKey);
	Runs.RemoveAt(i);
	if (Home.Num() > 0)
	{
		if (AGFGameState* G = GS())
		{
			G->ServerMint(FGuid::NewGuid(), Key, Home, How == EGFLabEnd::Extract ? TEXT("LabExtract") : TEXT("LabEvacuate"));
		}
	}
	LastBanked.Add(Key, Home);
	LastEnd.Add(Key, How);
	ForceNetUpdate();
	return Home;
}

FGFModeResult AGFLabDirector::Craft(AGFPlayerState* By, FName RecipeId)
{
	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	const FGFRecipeRow* Row = Recipes ? Recipes->FindRow<FGFRecipeRow>(RecipeId, TEXT("Lab"), false) : nullptr;
	if (!Row)
	{
		return FGFModeResult::Fail(TEXT("UnknownRecipe"));
	}
	if (Row->Station != FGFLabRules::Station)
	{
		return FGFModeResult::Fail(TEXT("WrongStation"));
	}
	const EGFCraftResult Check = UGFCraftingSubsystem::Check(*Row, FGFLabRules::Station, FGameplayTagContainer(), By->Inventory->GetInventory());
	AGFGameState* G = GS();
	if (Check != EGFCraftResult::Crafted || !G)
	{
		return FGFModeResult::Fail(Check == EGFCraftResult::Locked ? TEXT("Locked") : TEXT("MissingInputs"));
	}
	// Through the ledger: inputs burned in one transaction, then the output minted.
	if (G->ServerBurn(FGuid::NewGuid(), By->PlayerKey, Row->Inputs, TEXT("LabCraft")) != EGFTxResult::Applied)
	{
		return FGFModeResult::Fail(TEXT("MissingInputs"));
	}
	G->ServerMint(FGuid::NewGuid(), By->PlayerKey, { Row->Output }, TEXT("LabCraft"));
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Stacks = { Row->Output };
	return Res;
}

FGFModeResult AGFLabDirector::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& R)
{
	if (!By)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	FGFLabRun* Run = EditRunForTests(By->PlayerKey);
	if (R.Verb == TEXT("Enter"))
	{
		if (Run)
		{
			return FGFModeResult::Fail(CodeOf(EGFLabOrder::AlreadyIn));
		}
		const int32 Seed = ForcedSeed != 0 ? ForcedSeed : FMath::Rand();
		Runs.Add(FGFLabRules::StartRun(By->PlayerKey, Seed));
		ForceNetUpdate();
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Ints = { FGFLabRules::PileCount };
		return Res;
	}
	if (R.Verb == TEXT("Craft"))
	{
		return Craft(By, R.Name(0));
	}
	if (!Run)
	{
		return FGFModeResult::Fail(CodeOf(EGFLabOrder::NotInRun));
	}
	if (R.Verb == TEXT("Search"))
	{
		const EGFLabOrder O = FGFLabRules::BeginSearch(*Run, R.Int(0));
		ForceNetUpdate();
		return O == EGFLabOrder::Ok ? FGFModeResult::Ok() : FGFModeResult::Fail(CodeOf(O));
	}
	if (R.Verb == TEXT("Contain"))
	{
		const EGFLabOrder O = FGFLabRules::BeginContain(*Run, R.Int(0));
		ForceNetUpdate();
		return O == EGFLabOrder::Ok ? FGFModeResult::Ok() : FGFModeResult::Fail(CodeOf(O));
	}
	if (R.Verb == TEXT("Extract"))
	{
		FGFModeResult Res = FGFModeResult::Ok();
		Res.Stacks = EndRun(By->PlayerKey, EGFLabEnd::Extract);
		return Res;
	}
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

void AGFLabDirector::OnModePlayerLeft(AGFPlayerState* PS)
{
	if (PS && FindRun(PS->PlayerKey))
	{
		EndRun(PS->PlayerKey, EGFLabEnd::Evacuate);
	}
}

void AGFLabDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || Runs.Num() == 0)
	{
		return;
	}
	TArray<FString> Sealed;
	for (FGFLabRun& Run : Runs)
	{
		if (FGFLabRules::Tick(Run, DeltaSeconds * SimSpeed))
		{
			Sealed.Add(Run.Key);
		}
		SyncAnomalyMarkers(Run);
	}
	for (const FString& Key : Sealed)
	{
		EndRun(Key, EGFLabEnd::Lockdown);
	}
}

void AGFLabDirector::DestroyAnomalyMarkers(const FString& Key)
{
	for (int32 i = AnomalyMarkers.Num() - 1; i >= 0; --i)
	{
		if (AnomalyMarkers[i].Key != Key)
		{
			continue;
		}
		if (AActor* Actor = AnomalyMarkers[i].Actor.Get())
		{
			Actor->Destroy();
		}
		AnomalyMarkers.RemoveAt(i);
	}
}

void AGFLabDirector::SyncAnomalyMarkers(const FGFLabRun& Run)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	for (int32 i = 0; i < Run.Anomalies.Num(); ++i)
	{
		int32 Found = INDEX_NONE;
		for (int32 m = 0; m < AnomalyMarkers.Num(); ++m)
		{
			if (AnomalyMarkers[m].Key == Run.Key && AnomalyMarkers[m].Index == i)
			{
				Found = m;
				break;
			}
		}
		const bool bWant = Run.Anomalies[i].bActive;
		if (!bWant)
		{
			if (Found != INDEX_NONE)
			{
				if (AActor* Actor = AnomalyMarkers[Found].Actor.Get())
				{
					Actor->Destroy();
				}
				AnomalyMarkers.RemoveAt(Found);
			}
			continue;
		}
		if (Found != INDEX_NONE && AnomalyMarkers[Found].Actor.IsValid())
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		const FVector Where = GetActorLocation() + FVector(180.f + i * 140.f, 0.f, 40.f);
		AGFLabAnomalyMarker* Marker = World->SpawnActor<AGFLabAnomalyMarker>(Where, FRotator::ZeroRotator, Params);
		if (!Marker)
		{
			continue;
		}
		Marker->Setup(Run.Anomalies[i].Kind);
		if (Found == INDEX_NONE)
		{
			FAnomalyMarkerRef& Ref = AnomalyMarkers.AddDefaulted_GetRef();
			Ref.Key = Run.Key;
			Ref.Index = i;
			Ref.Actor = Marker;
		}
		else
		{
			AnomalyMarkers[Found].Actor = Marker;
		}
	}
}
