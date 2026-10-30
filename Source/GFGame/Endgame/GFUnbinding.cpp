#include "GFUnbinding.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"

const FName FGFUnbindingCodes::Ok(TEXT("Ok"));
const FName FGFUnbindingCodes::TooFewPlayers(TEXT("TooFewPlayers"));
const FName FGFUnbindingCodes::NoRecipient(TEXT("NoRecipient"));
const FName FGFUnbindingCodes::RecipientAbsent(TEXT("RecipientAbsent"));
const FName FGFUnbindingCodes::NoMaterial(TEXT("NoMaterial"));
const FName FGFUnbindingCodes::NotSpawned(TEXT("NotSpawned"));
const FName FGFUnbindingCodes::NotBigGrumble(TEXT("NotBigGrumble"));

namespace
{
	FGFUnbindingResult Fail(FName Code)
	{
		FGFUnbindingResult Result;
		Result.bOk = false;
		Result.Code = Code;
		return Result;
	}

	FGFUnbindingResult Succeed(const FString& Recipient)
	{
		FGFUnbindingResult Result;
		Result.bOk = true;
		Result.Code = FGFUnbindingCodes::Ok;
		Result.RecipientKey = Recipient;
		return Result;
	}

	const FGFUnbindingPlayer* FindPlayer(const TArray<FGFUnbindingPlayer>& Players, const FString& Key)
	{
		for (const FGFUnbindingPlayer& Player : Players)
		{
			if (Player.Key == Key)
			{
				return &Player;
			}
		}
		return nullptr;
	}

	bool PickRecipient(const TArray<FGFUnbindingPlayer>& Players, const FGFUnbindingTuning& Tuning, const FString& PreviousRecipient, FString& OutKey, FName& OutFail)
	{
		OutKey.Reset();
		if (Tuning.RecipientRule == EGFUnbindingRecipient::Explicit)
		{
			if (Tuning.ExplicitRecipientKey.IsEmpty())
			{
				OutFail = FGFUnbindingCodes::NoRecipient;
				return false;
			}
			const FGFUnbindingPlayer* Named = FindPlayer(Players, Tuning.ExplicitRecipientKey);
			if (!Named)
			{
				OutFail = FGFUnbindingCodes::NoRecipient;
				return false;
			}
			if (!FGFUnbindingRules::IsPresent(*Named))
			{
				OutFail = FGFUnbindingCodes::RecipientAbsent;
				return false;
			}
			OutKey = Named->Key;
			return true;
		}

		TArray<int32> Present;
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			if (FGFUnbindingRules::IsPresent(Players[Index]))
			{
				Present.Add(Index);
			}
		}
		if (Present.Num() == 0)
		{
			OutFail = FGFUnbindingCodes::NoRecipient;
			return false;
		}

		if (Tuning.RecipientRule == EGFUnbindingRecipient::NextPresent)
		{
			int32 Previous = INDEX_NONE;
			for (int32 Index = 0; Index < Present.Num(); ++Index)
			{
				if (Players[Present[Index]].Key == PreviousRecipient)
				{
					Previous = Index;
					break;
				}
			}
			const int32 Pick = (Previous == INDEX_NONE) ? 0 : (Previous + 1) % Present.Num();
			OutKey = Players[Present[Pick]].Key;
			return true;
		}

		for (int32 Index : Present)
		{
			if (Players[Index].bHost)
			{
				OutKey = Players[Index].Key;
				return true;
			}
		}
		OutFail = FGFUnbindingCodes::NoRecipient;
		return false;
	}
}

FGFUnbindingPlayer FGFUnbindingPlayer::FromPlayerState(const AGFPlayerState& PS)
{
	FGFUnbindingPlayer Player;
	Player.Key = PS.PlayerKey;
	Player.FactionId = PS.FactionId;
	Player.bHost = PS.bIsHost;
	Player.bLoggedOut = PS.bLoggedOut;
	Player.bPresent = !PS.bLoggedOut && !PS.PlayerKey.IsEmpty();
	Player.ColorIndex = PS.ColorIndex;
	const FUniqueNetIdRepl& NetId = PS.GetUniqueId();
	Player.NetId = NetId.IsValid() ? NetId->ToString() : FString();
	return Player;
}

int32 FGFUnbindingRules::SessionMaxPlayers(const AGFGameState* GS)
{
	if (const UWorld* World = GS ? GS->GetWorld() : nullptr)
	{
		const AGameModeBase* GM = World->GetAuthGameMode();
		if (GM && GM->GameSession && GM->GameSession->MaxPlayers > 0)
		{
			return GM->GameSession->MaxPlayers;
		}
	}
	const AGameSession* Defaults = GetDefault<AGameSession>();
	if (Defaults && Defaults->MaxPlayers > 0)
	{
		return Defaults->MaxPlayers;
	}
	return DefaultSessionMaxPlayers;
}

int32 FGFUnbindingRules::RequiredCount(const FGFUnbindingTuning& Tuning, const AGFGameState* GS)
{
	if (Tuning.RequiredPlayers > 0)
	{
		return Tuning.RequiredPlayers;
	}
	return SessionMaxPlayers(GS);
}

bool FGFUnbindingRules::IsPresent(const FGFUnbindingPlayer& Player)
{
	return Player.bPresent && !Player.bLoggedOut && !Player.Key.IsEmpty();
}

int32 FGFUnbindingRules::PresentCount(const TArray<FGFUnbindingPlayer>& Players)
{
	int32 Count = 0;
	for (const FGFUnbindingPlayer& Player : Players)
	{
		Count += IsPresent(Player) ? 1 : 0;
	}
	return Count;
}

bool FGFUnbindingRules::IsBigGrumbleName(const FString& DisplayName)
{
	return DisplayName == GFUnbindingIds::GiantDisplayName();
}

bool FGFUnbindingRules::IsCompanionSpecies(FName Species)
{
	// Every AGFDino species (addressed as AGFDino::AssetKey, "Dino.Raptor", "Dino.Rex", ...) is a companion.
	// Rex shares the giant's skeleton; he is still a companion, not Big Grumble.
	return !Species.IsNone();
}

TArray<FGFUnbindingPlayer> FGFUnbindingRules::ReadSession(const AGFGameState& GS)
{
	TArray<FGFUnbindingPlayer> Players;
	for (APlayerState* PS : GS.PlayerArray)
	{
		if (const AGFPlayerState* GPS = Cast<AGFPlayerState>(PS))
		{
			Players.Add(FGFUnbindingPlayer::FromPlayerState(*GPS));
		}
	}
	return Players;
}

int32 FGFUnbinding::MaterialOf(const FString& Key) const
{
	const FGFInventory* Bag = Bags.Find(Key);
	return Bag ? Bag->GetCount(GFUnbindingIds::MaterialId()) : 0;
}

bool FGFUnbinding::HasTamed(const FString& Key) const
{
	return TameCount(Key) > 0;
}

int32 FGFUnbinding::TameCount(const FString& Key) const
{
	const int32* Count = TamedCounts.Find(Key);
	return Count ? *Count : 0;
}

TArray<FString> FGFUnbinding::Tamers() const
{
	TArray<FString> Keys;
	for (const TPair<FString, int32>& Pair : TamedCounts)
	{
		if (Pair.Value > 0)
		{
			Keys.Add(Pair.Key);
		}
	}
	Keys.Sort();
	return Keys;
}

FGFUnbindingResult FGFUnbinding::TryComplete(const TArray<FGFUnbindingPlayer>& Players)
{
	const int32 Need = FGFUnbindingRules::RequiredCount(Tuning);
	if (FGFUnbindingRules::PresentCount(Players) < Need)
	{
		return Fail(FGFUnbindingCodes::TooFewPlayers);
	}

	const FString Previous = Completions.Num() > 0 ? Completions.Last().RecipientKey : FString();
	FString Recipient;
	FName Why = FGFUnbindingCodes::NoRecipient;
	if (!PickRecipient(Players, Tuning, Previous, Recipient, Why))
	{
		return Fail(Why);
	}

	Bags.FindOrAdd(Recipient).Add(GFUnbindingIds::MaterialId(), FGFUnbindingRules::MaterialPerCompletion);

	FGFUnbindingCompletion Completion;
	Completion.Index = Completions.Num() + 1;
	Completion.RecipientKey = Recipient;
	Completion.Rule = Tuning.RecipientRule;
	Completions.Add(Completion);
	return Succeed(Recipient);
}

bool FGFUnbinding::NotifySpawned(const FString& DisplayName)
{
	if (!FGFUnbindingRules::IsBigGrumbleName(DisplayName))
	{
		return false;
	}
	bGiantSpawned = true;
	return true;
}

void FGFUnbinding::NotifyGone()
{
	bGiantSpawned = false;
}

FGFUnbindingResult FGFUnbinding::AttemptTame(const FString& ByKey)
{
	if (!bGiantSpawned)
	{
		return Fail(FGFUnbindingCodes::NotSpawned);
	}
	if (ByKey.IsEmpty() || MaterialOf(ByKey) < FGFUnbindingRules::MaterialPerCompletion)
	{
		return Fail(FGFUnbindingCodes::NoMaterial);
	}

	FGFInventory& Bag = Bags.FindOrAdd(ByKey);
	if (!Bag.Remove(GFUnbindingIds::MaterialId(), FGFUnbindingRules::MaterialPerCompletion))
	{
		return Fail(FGFUnbindingCodes::NoMaterial);
	}

	TamedCounts.FindOrAdd(ByKey) += 1;
	FGFUnbindingResult Result = Succeed(ByKey);
	Result.bConsumed = true;
	return Result;
}

FGFUnbindingResult FGFUnbinding::RejectCompanion(const AGFDino* Dino) const
{
	// Diet taming stays on UGFTamingComponent. The species read is how an AGFDino is identified;
	// holding the material does not tame one, and this call does not touch bags or the tamed list.
	// AGFDino is never Big Grumble: an empty body and every companion species take the same refusal.
	(void)Dino;
	return Fail(FGFUnbindingCodes::NotBigGrumble);
}

bool FGFUnbinding::PushMaterial(AGFPlayerState& PS) const
{
	if (!PS.HasAuthority() || !PS.Inventory)
	{
		return false;
	}
	const FName Item = GFUnbindingIds::MaterialId();
	const int32 Want = MaterialOf(PS.PlayerKey);
	const int32 Have = PS.Inventory->GetCount(Item);
	if (Want == Have)
	{
		return true;
	}
	if (Want > Have)
	{
		return PS.Inventory->AddItem(Item, Want - Have);
	}
	return PS.Inventory->RemoveItem(Item, Have - Want);
}
