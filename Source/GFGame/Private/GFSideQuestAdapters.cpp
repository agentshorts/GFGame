#include "GFSideQuestAdapters.h"

#include "GFCharacterMovementComponent.h"
#include "GFFerryDirector.h"
#include "GFMagicComponent.h"
#include "GFPlayerState.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestLine.h"
#include "GFTerritorySubsystem.h"
#include "GFTheftDirector.h"
#include "GFTownNodes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

const TCHAR* FGFSideQuestAdapterRules::ClimbTagPrefix()
{
	return TEXT("GFQuestClimb_");
}

FName FGFSideQuestAdapterRules::ClimbTownFromTags(const TArray<FName>& Tags)
{
	const FString Prefix = ClimbTagPrefix();
	for (const FName Tag : Tags)
	{
		const FString Text = Tag.ToString();
		if (Text.StartsWith(Prefix, ESearchCase::IgnoreCase) && Text.Len() > Prefix.Len())
		{
			return FName(*Text.RightChop(Prefix.Len()));
		}
	}
	return NAME_None;
}

FName FGFSideQuestAdapterRules::ClimbTownAt(const TArray<FGFQuestClimbLip>& Lips, const FVector& At, float RadiusCm)
{
	const float Limit = FMath::Max(0.f, RadiusCm);
	float Best = TNumericLimits<float>::Max();
	FName Town = NAME_None;
	for (const FGFQuestClimbLip& Lip : Lips)
	{
		const float Distance = FVector::Dist(Lip.Location, At);
		if (!Lip.TownId.IsNone() && Distance <= Limit && Distance < Best)
		{
			Best = Distance;
			Town = Lip.TownId;
		}
	}
	return Town;
}

bool FGFSideQuestAdapterRules::HopCounts(EGFJudgement Judgement, bool bApplied)
{
	return bApplied && (Judgement == EGFJudgement::Perfect || Judgement == EGFJudgement::Good);
}

int32 FGFSideQuestAdapterRules::HopScore(EGFJudgement Judgement)
{
	switch (Judgement)
	{
	case EGFJudgement::Perfect:
		return 2;
	case EGFJudgement::Good:
		return 1;
	default:
		return 0;
	}
}

FName FGFSideQuestAdapterRules::HopTileId(EGFHopTileKind Kind)
{
	switch (Kind)
	{
	case EGFHopTileKind::Standard:
		return TEXT("Standard");
	case EGFHopTileKind::Downbeat:
		return TEXT("Downbeat");
	case EGFHopTileKind::Boost:
		return TEXT("Boost");
	case EGFHopTileKind::Landing:
		return TEXT("Landing");
	default:
		return NAME_None;
	}
}

bool FGFSideQuestAdapterRules::IsTownMember(FName FactionId, EGFFactionRank Rank, FName TownId)
{
	return FGFSideQuestCatalog::SameTown(FactionId, TownId) && static_cast<uint8>(Rank) >= static_cast<uint8>(EGFFactionRank::Member);
}

FName FGFSideQuestAdapterRules::CrystalItemId()
{
	return TEXT("Crystal");
}

UGFSideQuestAdapters* UGFSideQuestAdapters::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFSideQuestAdapters>() : nullptr;
}

void UGFSideQuestAdapters::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TreeHandle = FGFTownNodeEvents::OnTreeBroken().AddUObject(this, &UGFSideQuestAdapters::HandleTreeBroken);
	CrystalHandle = FGFTownNodeEvents::OnCrystalTaken().AddUObject(this, &UGFSideQuestAdapters::HandleCrystalTaken);
	DropHandle = UGFMagicComponent::OnDropReceived().AddUObject(this, &UGFSideQuestAdapters::HandleDrop);
	HopHandle = UGFCharacterMovementComponent::OnServerAccelHop.AddUObject(this, &UGFSideQuestAdapters::HandleHop);
	MantleHandle = UGFCharacterMovementComponent::OnServerMantleFinished.AddUObject(this, &UGFSideQuestAdapters::HandleMantle);
	FerryHandle = AGFFerryDirector::OnFlightArrived().AddUObject(this, &UGFSideQuestAdapters::HandleFerry);
}

void UGFSideQuestAdapters::Deinitialize()
{
	FGFTownNodeEvents::OnTreeBroken().Remove(TreeHandle);
	FGFTownNodeEvents::OnCrystalTaken().Remove(CrystalHandle);
	UGFMagicComponent::OnDropReceived().Remove(DropHandle);
	UGFCharacterMovementComponent::OnServerAccelHop.Remove(HopHandle);
	UGFCharacterMovementComponent::OnServerMantleFinished.Remove(MantleHandle);
	AGFFerryDirector::OnFlightArrived().Remove(FerryHandle);
	TreeHandle.Reset();
	CrystalHandle.Reset();
	DropHandle.Reset();
	HopHandle.Reset();
	MantleHandle.Reset();
	FerryHandle.Reset();
	Super::Deinitialize();
}

void UGFSideQuestAdapters::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	RefreshClimbLips();
}

bool UGFSideQuestAdapters::IsServerWorld(const UWorld* World) const
{
	return World && World == GetWorld() && World->GetNetMode() != NM_Client;
}

UGFQuestSubsystem* UGFSideQuestAdapters::Quest() const
{
	return UGFQuestSubsystem::Get(GetWorld());
}

void UGFSideQuestAdapters::RaiseSide(const FGFSideQuestEvent& Event) const
{
	FGFSideQuestEvents::Raise(GetWorld(), Event);
}

FString UGFSideQuestAdapters::KeyOf(const APawn* Pawn)
{
	const AGFPlayerState* PS = Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
	return PS ? PS->PlayerKey : FString();
}

int32 UGFSideQuestAdapters::CreditTree(const TArray<FString>& Keys, const FVector& Location)
{
	const FName TownId = AGFTheftDirector::TownAt(GetWorld(), Location);
	if (TownId.IsNone())
	{
		return 0;
	}
	TArray<FString> Done;
	for (const FString& Key : Keys)
	{
		if (Key.IsEmpty() || Done.Contains(Key))
		{
			continue;
		}
		Done.Add(Key);
		RaiseSide(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::TreeBroken, TownId));
	}
	return Done.Num();
}

int32 UGFSideQuestAdapters::CreditCrystalTaken(const TArray<FString>& Keys, const FVector& Location)
{
	const FName TownId = AGFTheftDirector::TownAt(GetWorld(), Location);
	const UGFQuestSubsystem* QuestSystem = Quest();
	if (TownId.IsNone() || !QuestSystem)
	{
		return 0;
	}
	TArray<FString> Done;
	for (const FString& Key : Keys)
	{
		if (Key.IsEmpty() || Done.Contains(Key))
		{
			continue;
		}
		const FGFQuestProgress* Errand = QuestSystem->FindProgress(Key);
		if (!Errand || !FGFSideQuestAdapterRules::IsTownMember(Errand->FactionId, Errand->Rank, TownId))
		{
			continue;
		}
		Done.Add(Key);
		RaiseSide(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::CrystalTaken, TownId));
	}
	return Done.Num();
}

int32 UGFSideQuestAdapters::CreditDrop(const FString& Key, FName ItemId, int32 Count, const FVector& From)
{
	if (Key.IsEmpty() || Count <= 0 || ItemId != FGFSideQuestAdapterRules::CrystalItemId())
	{
		return 0;
	}
	FGFSideQuestEvent Event = FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::CrystalGathered, AGFTheftDirector::TownAt(GetWorld(), From), Count);
	Event.ItemId = ItemId;
	RaiseSide(Event);
	return 1;
}

int32 UGFSideQuestAdapters::CreditHop(const FString& Key, EGFJudgement Judgement, EGFHopTileKind Kind, bool bApplied)
{
	if (Key.IsEmpty() || !FGFSideQuestAdapterRules::HopCounts(Judgement, bApplied))
	{
		return 0;
	}
	FGFSideQuestEvent Event = FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::HopLanded);
	Event.ItemId = FGFSideQuestAdapterRules::HopTileId(Kind);
	Event.Score = FGFSideQuestAdapterRules::HopScore(Judgement);
	RaiseSide(Event);
	return 1;
}

int32 UGFSideQuestAdapters::CreditMantle(const FString& Key, const FVector& Lip)
{
	const FName TownId = FGFSideQuestAdapterRules::ClimbTownAt(ClimbLips, Lip, ClimbLipRadiusCm);
	if (Key.IsEmpty() || TownId.IsNone())
	{
		return 0;
	}
	RaiseSide(FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::ClimbMantled, TownId));
	return 1;
}

int32 UGFSideQuestAdapters::CreditFerry(const FString& Key, FName FromAnchorId, FName ToStopId)
{
	if (Key.IsEmpty())
	{
		return 0;
	}
	FGFSideQuestEvent Event = FGFSideQuestEvents::Progress(Key, EGFSideQuestEventKind::FerryArrived, FromAnchorId);
	Event.ItemId = ToStopId;
	RaiseSide(Event);
	return 1;
}

void UGFSideQuestAdapters::RefreshClimbLips()
{
	ClimbLips.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TSet<FName> Tagged;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		const FName TownId = Actor ? FGFSideQuestAdapterRules::ClimbTownFromTags(Actor->Tags) : NAME_None;
		if (TownId.IsNone())
		{
			continue;
		}
		FGFQuestClimbLip& Lip = ClimbLips.AddDefaulted_GetRef();
		Lip.TownId = TownId;
		Lip.Location = Actor->GetActorLocation();
		Tagged.Add(TownId);
	}
	if (ClimbLipFallbacks.Num() == 0)
	{
		return;
	}
	const UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
	const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
	const TArray<FGFTerritoryFaction>* Factions = (Territory && Territory->FactionLayout.Num() > 0) ? &Territory->FactionLayout
		: (Defaults ? &Defaults->FactionLayout : nullptr);
	if (!Factions)
	{
		return;
	}
	for (const FGFQuestClimbLipConfig& Config : ClimbLipFallbacks)
	{
		bool bHasTagged = false;
		for (const FName Town : Tagged)
		{
			bHasTagged |= FGFSideQuestCatalog::SameTown(Town, Config.TownId);
		}
		if (Config.TownId.IsNone() || bHasTagged)
		{
			continue;
		}
		for (const FGFTerritoryFaction& Faction : *Factions)
		{
			if (FGFSideQuestCatalog::SameTown(Faction.Id, Config.TownId))
			{
				FGFQuestClimbLip& Lip = ClimbLips.AddDefaulted_GetRef();
				Lip.TownId = Config.TownId;
				Lip.Location = Faction.Center + Config.Offset;
				break;
			}
		}
	}
}

void UGFSideQuestAdapters::HandleTreeBroken(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& Keys)
{
	if (IsServerWorld(World))
	{
		CreditTree(Keys, Location);
	}
}

void UGFSideQuestAdapters::HandleCrystalTaken(UWorld* World, const FString& NodeKey, FVector Location, const TArray<FString>& Keys)
{
	if (IsServerWorld(World))
	{
		CreditCrystalTaken(Keys, Location);
	}
}

void UGFSideQuestAdapters::HandleDrop(UWorld* World, const FString& Key, FName ItemId, int32 Count, FVector From)
{
	if (IsServerWorld(World))
	{
		CreditDrop(Key, ItemId, Count, From);
	}
}

void UGFSideQuestAdapters::HandleHop(const UGFCharacterMovementComponent* Movement, const FGFAccelHopHit& Hit, EGFHopTileKind Kind, bool bApplied)
{
	const ACharacter* Body = Movement ? Movement->GetCharacterOwner() : nullptr;
	if (Body && IsServerWorld(Body->GetWorld()))
	{
		CreditHop(KeyOf(Body), Hit.Judgement, Kind, bApplied);
	}
}

void UGFSideQuestAdapters::HandleMantle(const UGFCharacterMovementComponent* Movement, FVector Lip)
{
	const ACharacter* Body = Movement ? Movement->GetCharacterOwner() : nullptr;
	if (Body && IsServerWorld(Body->GetWorld()))
	{
		CreditMantle(KeyOf(Body), Lip);
	}
}

void UGFSideQuestAdapters::HandleFerry(UWorld* World, APawn* Rider, const FGFFerryStop& From, const FGFFerryStop& To)
{
	if (IsServerWorld(World))
	{
		CreditFerry(KeyOf(Rider), From.AnchorId, To.Id);
	}
}
