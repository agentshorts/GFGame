#include "GFSideQuestEscort.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GFDino.h"
#include "GFQuestSubsystem.h"
#include "GFSideQuestLine.h"
#include "GFTamingComponent.h"
#include "GFWanderComponent.h"
#include "GFTheftDirector.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogGFSideEscort, Log, All);

FName FGFSideQuestEscortRules::Species()
{
	static const FName Tusksaw(TEXT("Tusksaw"));
	return Tusksaw;
}

bool FGFSideQuestEscortRules::Ask(TArray<FGFSideQuestEscort>& Escorts, const FString& PlayerKey, FName LineId, FName TownId)
{
	if (PlayerKey.IsEmpty())
	{
		return false;
	}
	for (const FGFSideQuestEscort& Escort : Escorts)
	{
		if (Escort.PlayerKey == PlayerKey && Escort.Phase != EGFSideQuestEscortPhase::Done)
		{
			return false;
		}
	}
	FGFSideQuestEscort& Added = Escorts.AddDefaulted_GetRef();
	Added.PlayerKey = PlayerKey;
	Added.LineId = LineId;
	Added.TownId = TownId;
	Added.Phase = EGFSideQuestEscortPhase::Pending;
	Added.Left = PendingSeconds;
	return true;
}

EGFSideQuestEscortAction FGFSideQuestEscortRules::Advance(FGFSideQuestEscort& Escort, float Dt, bool bHasBody, bool bHireAlive)
{
	const float Step = FMath::Max(0.f, Dt);
	switch (Escort.Phase)
	{
	case EGFSideQuestEscortPhase::Pending:
		if (bHasBody)
		{
			return EGFSideQuestEscortAction::Spawn;
		}
		Escort.Left -= Step;
		if (Escort.Left <= 0.f)
		{
			Escort.Phase = EGFSideQuestEscortPhase::Done;
			return EGFSideQuestEscortAction::GiveUp;
		}
		return EGFSideQuestEscortAction::None;
	case EGFSideQuestEscortPhase::Escorting:
		Escort.Left -= Step;
		if (Escort.Left <= 0.f || !bHasBody || !bHireAlive)
		{
			Escort.Phase = EGFSideQuestEscortPhase::Done;
			return EGFSideQuestEscortAction::StandDown;
		}
		return EGFSideQuestEscortAction::None;
	default:
		return EGFSideQuestEscortAction::None;
	}
}

void FGFSideQuestEscortRules::Spawned(FGFSideQuestEscort& Escort)
{
	Escort.Phase = EGFSideQuestEscortPhase::Escorting;
	Escort.Left = EscortSeconds;
	Escort.bFollowing = false;
}

bool FGFSideQuestEscortRules::ShouldFollow(bool bFollowing, float DistanceCm)
{
	// The tamed follower band: an Idle dino starts following past FollowStart, a following one stops inside FollowStop.
	const EGFDinoState Current = bFollowing ? EGFDinoState::Follow : EGFDinoState::Idle;
	const EGFDinoState Next = AGFDino::NextState(Current, true, false, 0.f, 0.f, true, DistanceCm, FollowStartCm, FollowStopCm);
	return Next == EGFDinoState::Follow;
}

bool FGFSideQuestEscortRules::ShouldRenewCalm(float Now, float CalmedUntil)
{
	return CalmedUntil - Now < CalmRenewBelowSeconds;
}

bool FGFSideQuestEscortRules::IsAligned(const TArray<FGFSideQuestEscort>& Escorts)
{
	TSet<FString> Open;
	for (const FGFSideQuestEscort& Escort : Escorts)
	{
		if (Escort.PlayerKey.IsEmpty())
		{
			return false;
		}
		if (Escort.Phase != EGFSideQuestEscortPhase::Done)
		{
			if (Escort.LineId.IsNone() || Open.Contains(Escort.PlayerKey))
			{
				return false;
			}
			Open.Add(Escort.PlayerKey);
		}
		const bool bHasHire = Escort.Hire.IsValid();
		if (bHasHire != (Escort.Phase == EGFSideQuestEscortPhase::Escorting))
		{
			return false;
		}
	}
	return true;
}

UGFSideQuestEscorts* UGFSideQuestEscorts::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGFSideQuestEscorts>() : nullptr;
}

void UGFSideQuestEscorts::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGFQuestSubsystem* Quest = Collection.InitializeDependency<UGFQuestSubsystem>())
	{
		AskHandle = Quest->OnSideHireAsked.AddUObject(this, &UGFSideQuestEscorts::HandleHireAsked);
	}
}

void UGFSideQuestEscorts::Deinitialize()
{
	if (UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(this))
	{
		Quest->OnSideHireAsked.Remove(AskHandle);
	}
	AskHandle.Reset();
	for (int32 Index = Escorts.Num() - 1; Index >= 0; --Index)
	{
		if (Escorts[Index].Phase == EGFSideQuestEscortPhase::Escorting)
		{
			StandDown(Index, true);
		}
	}
	Escorts.Reset();
	EnsureAligned();
	Super::Deinitialize();
}

void UGFSideQuestEscorts::Tick(float DeltaSeconds)
{
	TickEscorts(DeltaSeconds);
}

TStatId UGFSideQuestEscorts::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGFSideQuestEscorts, STATGROUP_Tickables);
}

bool UGFSideQuestEscorts::IsTickable() const
{
	const UWorld* World = GetWorld();
	return World && World->GetAuthGameMode() != nullptr && Escorts.Num() > 0;
}

bool UGFSideQuestEscorts::IsServer() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UGFSideQuestEscorts::HandleHireAsked(const FString& PlayerKey, FName LineId, FName TownId)
{
	// Queued, not spawned here: the ask fires from inside ApplySideEvent, and the spawn raises HireSpawned, which
	// applies another side event. The next tick spawns, so the board is never written from inside its own write.
	Ask(PlayerKey, LineId, TownId);
}

bool UGFSideQuestEscorts::Ask(const FString& PlayerKey, FName LineId, FName TownId)
{
	if (!IsServer() || !FGFSideQuestEscortRules::Ask(Escorts, PlayerKey, LineId, TownId))
	{
		return false;
	}
	EnsureAligned();
	return true;
}

AGFDino* UGFSideQuestEscorts::HireFor(const FString& PlayerKey) const
{
	for (const FGFSideQuestEscort& Escort : Escorts)
	{
		if (Escort.PlayerKey == PlayerKey && Escort.Phase == EGFSideQuestEscortPhase::Escorting)
		{
			return Escort.Hire.Get();
		}
	}
	return nullptr;
}

bool UGFSideQuestEscorts::GiveUp(const FString& PlayerKey)
{
	if (!IsServer() || PlayerKey.IsEmpty())
	{
		return false;
	}
	const int32 Index = Escorts.IndexOfByPredicate([&PlayerKey](const FGFSideQuestEscort& Escort)
	{
		return Escort.PlayerKey == PlayerKey && Escort.Phase != EGFSideQuestEscortPhase::Done;
	});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FName LineId = Escorts[Index].LineId;
	if (Escorts[Index].Hire.IsValid())
	{
		StandDown(Index, false);
	}
	Escorts.RemoveAt(Index);
	EnsureAligned();
	// The hire step may already be Done. Put it back to a wait so rejoin can ask. Asking stays on player ready.
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(this);
	return Quest && Quest->ReopenHireWait(PlayerKey, LineId);
}

void UGFSideQuestEscorts::TickEscorts(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !IsServer())
	{
		return;
	}
	for (int32 Index = 0; Index < Escorts.Num(); ++Index)
	{
		FGFSideQuestEscort& Escort = Escorts[Index];
		APawn* Body = UGFTamingComponent::FindTamerPawn(World, Escort.PlayerKey);
		AGFDino* Hire = Escort.Hire.Get();
		const bool bHireAlive = Hire && !Hire->IsActorBeingDestroyed() && Hire->GetLife() == EGFDinoLife::Alive;
		switch (FGFSideQuestEscortRules::Advance(Escort, DeltaSeconds, Body != nullptr, bHireAlive))
		{
		case EGFSideQuestEscortAction::Spawn:
			if (!SpawnFor(Index, Body))
			{
				// No body came out (missing species, no world). Count it against the pending wait, then give up.
				Escort.Left -= FMath::Max(0.f, DeltaSeconds);
				if (Escort.Left <= 0.f)
				{
					Escort.Phase = EGFSideQuestEscortPhase::Done;
					UE_LOG(LogGFSideEscort, Warning, TEXT("Escort hire for %s could not spawn; dropped."), *Escort.PlayerKey);
				}
			}
			break;
		case EGFSideQuestEscortAction::StandDown:
			StandDown(Index, false);
			break;
		case EGFSideQuestEscortAction::GiveUp:
			UE_LOG(LogGFSideEscort, Log, TEXT("Escort hire for %s dropped: no body to walk with."), *Escort.PlayerKey);
			break;
		default:
			if (Escort.Phase == EGFSideQuestEscortPhase::Escorting)
			{
				EscortStep(Index, Body, DeltaSeconds);
			}
			break;
		}
	}
	for (int32 Index = Escorts.Num() - 1; Index >= 0; --Index)
	{
		if (Escorts[Index].Phase == EGFSideQuestEscortPhase::Done)
		{
			// The hire lives on this row. Let it go before the row goes, so a drop cannot leave it behind.
			if (Escorts[Index].Hire.IsValid())
			{
				StandDown(Index, false);
			}
			Escorts.RemoveAt(Index);
		}
	}
	EnsureAligned();
}

bool UGFSideQuestEscorts::SpawnFor(int32 Index, APawn* Body)
{
	UWorld* World = GetWorld();
	if (!World || !Body || !Escorts.IsValidIndex(Index))
	{
		return false;
	}
	FGFSideQuestEscort& Escort = Escorts[Index];
	// The theft director's hire spawn: same body, ring pick, home and walk speed as a case hire. No case, no heat.
	AGFDino* Hire = AGFTheftDirector::SpawnHireBody(World, Body->GetActorLocation(), FGFSideQuestEscortRules::Species(),
		FGFSideQuestEscortRules::SpawnMinCm, FGFSideQuestEscortRules::SpawnMaxCm);
	if (!Hire)
	{
		return false;
	}
	// A hire, not a kill: hits spend no HP and pay nothing, same as a rental mount.
	Hire->SetRental(true);
	if (Hire->Taming)
	{
		Hire->Taming->ServerEscortCalm(FGFSideQuestEscortRules::CalmSeconds);
	}
	Escort.Hire = Hire;
	FGFSideQuestEscortRules::Spawned(Escort);
	// Raised only now that the body is in the world, so the HireSpawned step means a hire actually came.
	FGFSideQuestEvents::Raise(World, FGFSideQuestEvents::Progress(Escort.PlayerKey, EGFSideQuestEventKind::HireSpawned, Escort.TownId));
	return true;
}

void UGFSideQuestEscorts::EscortStep(int32 Index, APawn* Body, float DeltaSeconds)
{
	(void)DeltaSeconds;
	AGFDino* Hire = Escorts.IsValidIndex(Index) ? Escorts[Index].Hire.Get() : nullptr;
	const UWorld* World = GetWorld();
	if (!Hire || !Body || !World)
	{
		return;
	}
	FGFSideQuestEscort& Escort = Escorts[Index];
	if (Hire->Taming && Hire->Taming->GetTameState() == EGFTameState::Tamed)
	{
		// Tamed on the walk: the dino's own follower logic has it now.
		return;
	}
	if (Hire->Taming && FGFSideQuestEscortRules::ShouldRenewCalm(World->GetTimeSeconds(), Hire->Taming->CalmedUntil))
	{
		Hire->Taming->ServerEscortCalm(FGFSideQuestEscortRules::CalmSeconds);
	}
	// Its idle wander roams around the player instead of where it spawned.
	if (Hire->Wander)
	{
		Hire->Wander->SetHome(Body->GetActorLocation());
	}
	const FVector ToPlayer = Body->GetActorLocation() - Hire->GetActorLocation();
	Escort.bFollowing = FGFSideQuestEscortRules::ShouldFollow(Escort.bFollowing, ToPlayer.Size2D());
	if (Escort.bFollowing)
	{
		Hire->AddMovementInput(ToPlayer.GetSafeNormal2D(), 1.f);
	}
}

void UGFSideQuestEscorts::StandDown(int32 Index, bool bWorldEnding)
{
	if (!Escorts.IsValidIndex(Index))
	{
		return;
	}
	if (AGFDino* Hire = Escorts[Index].Hire.Get())
	{
		// Let go the way a theft case lets go of a hire spawn: despawned, unless the player tamed it on the walk.
		AGFTheftDirector::LetGoHire(Hire, true, bWorldEnding);
	}
	Escorts[Index].Hire.Reset();
}

void UGFSideQuestEscorts::EnsureAligned() const
{
	checkf(FGFSideQuestEscortRules::IsAligned(Escorts), TEXT("Side quest escort and its hire are out of line."));
}
