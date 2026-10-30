#include "GFTamingComponent.h"
#include "GFTamer.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

UGFTamingComponent::UGFTamingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
	SetIsReplicatedByDefault(true);
}

void UGFTamingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFTamingComponent, Species);
	DOREPLIFETIME(UGFTamingComponent, CompanionId);
	DOREPLIFETIME(UGFTamingComponent, RepState);
	DOREPLIFETIME(UGFTamingComponent, RepBond);
	DOREPLIFETIME(UGFTamingComponent, RepStarterKey);
	DOREPLIFETIME(UGFTamingComponent, RepOwnerKey);
	DOREPLIFETIME(UGFTamingComponent, RepTargetKey);
}

const FDinoDietRow* UGFTamingComponent::GetDietRow() const
{
	if (bHasDietOverride)
	{
		return &DietOverride;
	}
	const UDataTable* Table = DietTable ? DietTable.Get() : FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
	return Table ? Table->FindRow<FDinoDietRow>(Species, TEXT("GFTaming"), false) : nullptr;
}

TArray<IGFTamer*> UGFTamingComponent::GetTamers(const UWorld* World)
{
	TArray<IGFTamer*> Out;
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (GS)
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (IGFTamer* T = Cast<IGFTamer>(PS))
			{
				Out.Add(T);
			}
		}
	}
	return Out;
}

IGFTamer* UGFTamingComponent::FindTamer(const UWorld* World, const FString& Key)
{
	if (Key.IsEmpty())
	{
		return nullptr;
	}
	for (IGFTamer* T : GetTamers(World))
	{
		if (T->GetTamerKey() == Key)
		{
			return T;
		}
	}
	return nullptr;
}

APawn* UGFTamingComponent::FindTamerPawn(const UWorld* World, const FString& Key)
{
	IGFTamer* T = FindTamer(World, Key);
	return T ? T->GetTamerPawn() : nullptr;
}

void UGFTamingComponent::SyncReplicated()
{
	const EGFTameState Old = RepState;
	RepState = Machine.State;
	RepBond = Machine.Bond;
	RepStarterKey = Machine.StarterKey;
	RepOwnerKey = Machine.OwnerKey;
	RepTargetKey = Machine.TargetKey;
	if (Old != RepState)
	{
		OnTameStateChanged.Broadcast(RepState);
	}
}

void UGFTamingComponent::OnRep_State()
{
	OnTameStateChanged.Broadcast(RepState);
}

void UGFTamingComponent::RecordAndShare(IGFTamer* Source, TFunctionRef<bool(FGFDiscoveredDinoPreferences&)> Fn)
{
	FGFDiscoveredDinoPreferences* Book = Source ? Source->GetMutableJournal() : nullptr;
	if (!Book || !Fn(*Book))
	{
		return;
	}
	if (const FDinoDietRow* Row = GetDietRow())
	{
		FGFDinoDiet::UpdateCompletion(*Book, *Row);
	}
	Source->JournalChanged();

	const APawn* SourcePawn = Source->GetTamerPawn();
	const FGFDinoPreferenceRecord* Rec = Book->Find(Species);
	if (!SourcePawn || !Rec || !Source->SharesJournal())
	{
		return;
	}
	const FGFDinoPreferenceRecord Copy = *Rec;
	for (IGFTamer* Other : GetTamers(GetWorld()))
	{
		const APawn* OtherPawn = Other->GetTamerPawn();
		FGFDiscoveredDinoPreferences* OtherBook = Other->GetMutableJournal();
		if (Other == Source || !OtherPawn || !OtherBook || !Other->SharesJournal()
			|| FVector::Dist(OtherPawn->GetActorLocation(), SourcePawn->GetActorLocation()) > JournalShareRadius)
		{
			continue;
		}
		if (FGFDinoDiet::MergeInto(*OtherBook, Copy) > 0)
		{
			if (const FDinoDietRow* Row = GetDietRow())
			{
				FGFDinoDiet::UpdateCompletion(*OtherBook, *Row);
			}
			Other->JournalChanged();
		}
	}
}

EGFOfferResult UGFTamingComponent::ServerOffer(UObject* TamerObj, FName Food)
{
	IGFTamer* Tamer = Cast<IGFTamer>(TamerObj);
	const FDinoDietRow* Row = GetDietRow();
	const AActor* Owner = GetOwner();
	if (!Tamer || !Row || !Owner || !Owner->HasAuthority())
	{
		return EGFOfferResult::NotTameable;
	}
	const APawn* Pawn = Tamer->GetTamerPawn();
	if (!Pawn || FVector::Dist(Pawn->GetActorLocation(), Owner->GetActorLocation()) > OfferRange + OfferRangeSlack)
	{
		return EGFOfferResult::NotNow;
	}
	const FString Key = Tamer->GetTamerKey();
	const EGFOfferResult Result = Machine.Offer(*Row, GetWorld()->GetTimeSeconds(), Key, Food);
	if (Result == EGFOfferResult::Accepted || Result == EGFOfferResult::Favourite || Result == EGFOfferResult::Refused)
	{
		const EGFFeedResult Feed = FGFDinoDiet::ToFeedResult(Result);
		const FName Sp = Species;
		RecordAndShare(Tamer, [Sp, Food, Feed](FGFDiscoveredDinoPreferences& B) { return FGFDinoDiet::RecordDiscovery(B, Sp, Food, Feed); });
	}
	SyncReplicated();
	return Result;
}

void UGFTamingComponent::ServerForceHostile(const FString& Key)
{
	Machine.ForceHostile(Key);
	SyncReplicated();
}

void UGFTamingComponent::CallPack(const FString& Key)
{
	const AActor* Owner = GetOwner();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		UGFTamingComponent* Other = *It != Owner ? It->FindComponentByClass<UGFTamingComponent>() : nullptr;
		if (Other && Other->PackId == PackId && Other->Species == Species
			&& FVector::Dist(It->GetActorLocation(), Owner->GetActorLocation()) <= PackCallRadius)
		{
			Other->ServerForceHostile(Key);
		}
	}
}

void UGFTamingComponent::TickServer(float DeltaTime)
{
	const FDinoDietRow* Row = GetDietRow();
	const AActor* Owner = GetOwner();
	if (!Row || !Owner || Machine.IsTamed())
	{
		return;
	}
	TArray<FGFTamerView> Views;
	TArray<IGFTamer*> Tamers;
	for (IGFTamer* T : GetTamers(GetWorld()))
	{
		const APawn* Pawn = T->GetTamerPawn();
		if (Pawn && !T->GetTamerKey().IsEmpty())
		{
			Views.Emplace(T->GetTamerKey(), FVector::Dist(Pawn->GetActorLocation(), Owner->GetActorLocation()), T->GetTamerHeldItem(), T->IsTamerCrouching());
			Tamers.Add(T);
		}
	}
	const EGFTameState Before = Machine.State;
	Machine.Tick(*Row, GetWorld()->GetTimeSeconds(), DeltaTime, Views);

	// Journal: sighting inside WarnRadius; the sniff (entering Alert) half-reveals interesting held food.
	const FName Sp = Species;
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		if (Views[i].Distance > Row->WarnRadius())
		{
			continue;
		}
		RecordAndShare(Tamers[i], [Sp](FGFDiscoveredDinoPreferences& B) { return FGFDinoDiet::RecordSighting(B, Sp); });
		if (Machine.State == EGFTameState::Alert && Before != EGFTameState::Alert && Row->Accepts(Views[i].Held))
		{
			const FName Held = Views[i].Held;
			RecordAndShare(Tamers[i], [Sp, Held](FGFDiscoveredDinoPreferences& B) { return FGFDinoDiet::RecordInterest(B, Sp, Held); });
		}
	}
	if (Row->bPack && !PackId.IsNone() && Machine.State == EGFTameState::Hostile && Before != EGFTameState::Hostile)
	{
		CallPack(Machine.TargetKey);
	}
	SyncReplicated();
}

void UGFTamingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TickServer(DeltaTime);
	}
}
