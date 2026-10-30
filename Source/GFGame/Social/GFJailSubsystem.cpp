#include "GFJailSubsystem.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFLog.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

namespace
{
	FString ClaimKey(const FString& PlayerId, FName FactionId)
	{
		return PlayerId + TEXT("|") + FactionId.ToString();
	}
}

UGFJailSubsystem::UGFJailSubsystem()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UGFJailSubsystem* UGFJailSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	return GS ? GS->FindComponentByClass<UGFJailSubsystem>() : nullptr;
}

UGFJailSubsystem* UGFJailSubsystem::Ensure(AGFGameState* GS)
{
	if (!GS)
	{
		return nullptr;
	}
	if (UGFJailSubsystem* Existing = GS->FindComponentByClass<UGFJailSubsystem>())
	{
		return Existing;
	}
	UGFJailSubsystem* Jail = NewObject<UGFJailSubsystem>(GS, TEXT("Jail"));
	if (!Jail)
	{
		return nullptr;
	}
	Jail->SetIsReplicated(true);
	GS->AddInstanceComponent(Jail);
	Jail->RegisterComponent();
	return Jail;
}

void UGFJailSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFJailSubsystem, JailedFlags);
}

void UGFJailSubsystem::BeginPlay()
{
	Super::BeginPlay();
	if (AGFGameState* GS = Cast<AGFGameState>(GetOwner()))
	{
		FactionHandle = GS->OnFactionChanged.AddUObject(this, &UGFJailSubsystem::NoteFactionClaim);
	}
	if (HasJailAuthority())
	{
		SyncRoster();
	}
}

void UGFJailSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AGFGameState* GS = Cast<AGFGameState>(GetOwner()))
	{
		GS->OnFactionChanged.Remove(FactionHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UGFJailSubsystem::HasJailAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UGFJailSubsystem::NoteFactionClaim(AGFPlayerState* Player, FName FactionId)
{
	if (!HasJailAuthority() || !Player || Player->PlayerKey.IsEmpty() || FactionId.IsNone())
	{
		return;
	}
	const FString Key = ClaimKey(Player->PlayerKey, FactionId);
	if (!ClaimOrder.Contains(Key))
	{
		ClaimOrder.Add(Key, NextClaimOrder++);
	}
}

void UGFJailSubsystem::NotePlayerJoined(AGFPlayerState* Player)
{
	if (!HasJailAuthority() || !Player)
	{
		return;
	}
	NoteFactionClaim(Player, Player->FactionId);
	if (IsJailed(Player->PlayerKey))
	{
		Player->SetJailed(true);
	}
}

void UGFJailSubsystem::SyncRoster()
{
	AGFGameState* GS = Cast<AGFGameState>(GetOwner());
	if (!GS)
	{
		return;
	}
	for (APlayerState* Other : GS->PlayerArray)
	{
		AGFPlayerState* PS = Cast<AGFPlayerState>(Other);
		if (PS && !PS->bLoggedOut)
		{
			NoteFactionClaim(PS, PS->FactionId);
		}
	}
}

int32 UGFJailSubsystem::JoinOrderOf(const FString& PlayerId, FName FactionId) const
{
	if (PlayerId.IsEmpty() || FactionId.IsNone())
	{
		return 0;
	}
	const int32* Order = ClaimOrder.Find(ClaimKey(PlayerId, FactionId));
	return Order ? *Order : 0;
}

FGFJailActor UGFJailSubsystem::Describe(const AGFPlayerState* PS) const
{
	FGFJailActor Actor;
	if (!PS)
	{
		return Actor;
	}
	Actor.PlayerId = PS->PlayerKey;
	Actor.FactionId = PS->FactionId;
	Actor.bHost = PS->bIsHost;
	Actor.bOnline = !PS->bLoggedOut;
	Actor.JoinOrder = JoinOrderOf(PS->PlayerKey, PS->FactionId);
	return Actor;
}

FGFJailContext UGFJailSubsystem::MakeContext() const
{
	FGFJailContext Context;
	const AGFGameState* GS = Cast<AGFGameState>(GetOwner());
	if (GS)
	{
		for (const APlayerState* Other : GS->PlayerArray)
		{
			if (const AGFPlayerState* PS = Cast<AGFPlayerState>(Other))
			{
				Context.Players.Add(Describe(PS));
			}
		}
	}
	for (const FGFJailPlayerFlag& Flag : JailedFlags)
	{
		if (Flag.bJailed)
		{
			Context.JailedIds.Add(Flag.PlayerId);
		}
	}
	return Context;
}

FString UGFJailSubsystem::LeaderOf(FName FactionId)
{
	SyncRoster();
	return FGFJailRules::LeaderOf(FactionId, MakeContext());
}

bool UGFJailSubsystem::IsJailed(const FString& PlayerId) const
{
	const FGFJailPlayerFlag* Flag = JailedFlags.FindByPredicate([&](const FGFJailPlayerFlag& Entry)
	{
		return Entry.PlayerId == PlayerId;
	});
	return Flag && Flag->bJailed;
}

bool UGFJailSubsystem::IsMuted(const FString& PlayerId) const
{
	return FGFJailRules::IsMuted(PlayerId, MakeContext());
}

bool UGFJailSubsystem::CanEmote(const FString& PlayerId) const
{
	return FGFJailRules::CanEmote(PlayerId, MakeContext());
}

void UGFJailSubsystem::WriteFlag(const FString& PlayerId, bool bJailed)
{
	if (!HasJailAuthority() || PlayerId.IsEmpty())
	{
		return;
	}
	FGFJailPlayerFlag* Flag = JailedFlags.FindByPredicate([&](const FGFJailPlayerFlag& Entry)
	{
		return Entry.PlayerId == PlayerId;
	});
	if (Flag)
	{
		Flag->bJailed = bJailed;
	}
	else if (bJailed)
	{
		FGFJailPlayerFlag NewFlag;
		NewFlag.PlayerId = PlayerId;
		NewFlag.bJailed = true;
		JailedFlags.Add(NewFlag);
	}
	if (AGFGameState* GS = Cast<AGFGameState>(GetOwner()))
	{
		if (AGFPlayerState* PS = GS->FindPlayerByKey(PlayerId))
		{
			PS->SetJailed(bJailed);
		}
	}
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

FName UGFJailSubsystem::Jail(AGFPlayerState* Actor, AGFPlayerState* Target)
{
	if (!HasJailAuthority())
	{
		return FGFJailCodes::NotAuthority;
	}
	if (!Actor || !Target)
	{
		return FGFJailCodes::NoSuchPlayer;
	}
	SyncRoster();
	const FName Code = FGFJailRules::CanJail(Describe(Actor), Describe(Target), MakeContext());
	if (Code != FGFJailCodes::Ok)
	{
		return Code;
	}
	WriteFlag(Target->PlayerKey, true);
	UE_LOG(LogGF, Log, TEXT("Jail: %s jailed %s"), *Actor->PlayerKey, *Target->PlayerKey);
	return FGFJailCodes::Ok;
}

FName UGFJailSubsystem::Release(AGFPlayerState* Actor, AGFPlayerState* Target)
{
	if (!HasJailAuthority())
	{
		return FGFJailCodes::NotAuthority;
	}
	if (!Actor || !Target)
	{
		return FGFJailCodes::NoSuchPlayer;
	}
	SyncRoster();
	const FName Code = FGFJailRules::CanRelease(Describe(Actor), Describe(Target), MakeContext());
	if (Code != FGFJailCodes::Ok)
	{
		return Code;
	}
	WriteFlag(Target->PlayerKey, false);
	UE_LOG(LogGF, Log, TEXT("Jail: %s released %s"), *Actor->PlayerKey, *Target->PlayerKey);
	return FGFJailCodes::Ok;
}

void UGFJailSubsystem::OnRep_JailedFlags()
{
	// Clients observe JailedFlags. This does not write the set or the player flag back to the server.
}
