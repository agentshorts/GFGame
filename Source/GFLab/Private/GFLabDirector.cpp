#include "GFLabDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFCraftingSubsystem.h"
#include "GFRecipeTypes.h"
#include "GFDataTables.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

const FName AGFLabDirector::ModeName(TEXT("Lab"));

AGFLabDirector::AGFLabDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
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
		return World->SpawnActor<AGFLabDirector>(AGFLabDirector::StaticClass(), FTransform(At), P);
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
	const TArray<FGFItemStack> Home = FGFLabRules::Bank(Runs[i].Unbanked, How);
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
	}
	for (const FString& Key : Sealed)
	{
		EndRun(Key, EGFLabEnd::Lockdown);
	}
}
