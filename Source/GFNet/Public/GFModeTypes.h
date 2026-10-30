#pragma once

// Generic request channel for the mini-game modes (arena, Depths, mine, lab). A mode's server-side director
// registers itself with the game state as an IGFModeHandler; players send FGFModeRequest through their player
// state and get one FGFModeResult back. The server always acts as the sending player's key, so nobody can act
// for anyone else. GFNet knows nothing about the modes themselves (they depend on it, not the other way round).
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GFItemTypes.h"
#include "GFModeTypes.generated.h"

class AGFPlayerState;

USTRUCT(BlueprintType)
struct GFNET_API FGFModeRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") int32 RequestId = 0;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") FName Mode;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") FName Verb;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") TArray<int32> Ints;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") TArray<float> Floats;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") TArray<FName> Names;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") TArray<FGuid> Ids;
	UPROPERTY(BlueprintReadWrite, Category = "GF|Mode") TArray<FGFItemStack> Stacks;

	static FGFModeRequest Make(FName InMode, FName InVerb)
	{
		FGFModeRequest R;
		R.Mode = InMode;
		R.Verb = InVerb;
		return R;
	}
	int32 Int(int32 i, int32 Default = 0) const { return Ints.IsValidIndex(i) ? Ints[i] : Default; }
	float Float(int32 i, float Default = 0.f) const { return Floats.IsValidIndex(i) ? Floats[i] : Default; }
	FName Name(int32 i) const { return Names.IsValidIndex(i) ? Names[i] : NAME_None; }
	FGuid Id(int32 i) const { return Ids.IsValidIndex(i) ? Ids[i] : FGuid(); }
};

USTRUCT(BlueprintType)
struct GFNET_API FGFModeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") int32 RequestId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") FName Mode;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") FName Verb;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") bool bOk = false;
	/** "Ok" or why not ("NotOwner", "OverCap", ...). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") FName Code;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") TArray<int32> Ints;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Mode") TArray<FGFItemStack> Stacks;

	static FGFModeResult Ok() { FGFModeResult R; R.bOk = true; R.Code = TEXT("Ok"); return R; }
	static FGFModeResult Fail(FName Why) { FGFModeResult R; R.bOk = false; R.Code = Why; return R; }
};

/** Saved mode state (the host world keeps one blob per mode; nested containers can't be UPROPERTYs). */
USTRUCT()
struct GFNET_API FGFModeBlob
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) TArray<uint8> Bytes;
};

/**
 * Factions are opaque ids (FName) in code; names, colours and lore live in data and art. A faction's leader is the
 * first player to claim it this session (GFSocial's jail permissions read this).
 */
USTRUCT(BlueprintType)
struct GFNET_API FGFFactionLeader
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") FName FactionId;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") FString LeaderKey;
};

/** One text chat message (all chat). */
USTRUCT(BlueprintType)
struct GFNET_API FGFChatMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Chat") int32 Seq = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Chat") FString SenderKey;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Chat") FString SenderName;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Chat") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Chat") FName Channel;
};

/**
 * Who last turned this player's protection off on another faction's land, and when. Replicated on the player
 * state so chat line templates can fill {Dragger} next to {Target}. Cleared when protection comes back on.
 */
USTRUCT(BlueprintType)
struct GFNET_API FGFDragRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") FString DraggerKey;
	/** Display name at the time it happened. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") FString DraggerName;
	/** Whose land the protection was for. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") FName LandFaction;
	/** FDateTime::UtcNow().GetTicks() on the server. 0 = no record. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Faction") int64 AtUtcTicks = 0;

	bool IsSet() const { return AtUtcTicks != 0; }
};

/** Per-player progress in the modes; travels with the character (FGFCharacterData). */
/** What a pebbling is doing (Pebble Mine, PLAN.md M8). */
UENUM(BlueprintType)
enum class EGFPebblingState : uint8
{
	Home,    // idle at the mine mouth (no room)
	Travel,  // walking to its room
	Work,    // mining its room
	Full,    // satchel full: waits in the room for a haul order
	Carry,   // carrying the haul home
	Return   // walking back to its room after a haul
};

/** One pebbling in a player's crew. Saved with their character. */
USTRUCT(BlueprintType)
struct GFNET_API FGFPebbling
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") FGuid Id;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") FName Name;
	/** The only room it mines (None = not assigned). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") FName Room;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") EGFPebblingState State = EGFPebblingState::Home;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") float StateLeft = 0.f;
	/** Its satchel (not the player's until it's carried home). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") TArray<FGFItemStack> Carry;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") int32 Loads = 0;
};

/** A player's pebbling crew, mine upgrades and the offline clock. */
USTRUCT(BlueprintType)
struct GFNET_API FGFMineCrew
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") TArray<FGFPebbling> Pebblings;
	/** Upgrade levels (parallel arrays). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") TArray<FName> UpgradeIds;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") TArray<int32> UpgradeLevels;
	/** UTC ticks when the crew was last seen working (offline gathering starts here; 0 = never). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") int64 LastSeenUtcTicks = 0;
	/** The last offline haul, for the "while you were away" card. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") TArray<FGFItemStack> LastOffline;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Mine") float LastOfflineHours = 0.f;

	int32 Level(FName Upgrade) const
	{
		const int32 i = UpgradeIds.IndexOfByKey(Upgrade);
		return i != INDEX_NONE && UpgradeLevels.IsValidIndex(i) ? UpgradeLevels[i] : 0;
	}
	void SetLevel(FName Upgrade, int32 L)
	{
		const int32 i = UpgradeIds.IndexOfByKey(Upgrade);
		if (i == INDEX_NONE)
		{
			UpgradeIds.Add(Upgrade);
			UpgradeLevels.Add(L);
		}
		else
		{
			UpgradeLevels.SetNum(UpgradeIds.Num());
			UpgradeLevels[i] = L;
		}
	}
	FGFPebbling* Find(const FGuid& Id) { return Pebblings.FindByPredicate([&](const FGFPebbling& P) { return P.Id == Id; }); }
};

USTRUCT(BlueprintType)
struct GFNET_API FGFPlayerProgress
{
	GENERATED_BODY()

	/** The recipe the player is working toward ("pinned"): arena wagers can't stake what it still needs. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") FName PinnedRecipe;
	/** Settings: "Wagers off" (the wager screen is skipped and stakes are refused). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") bool bWagersOff = false;
	/** Last arena formation (companion id per spot; parallel arrays). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") TArray<FGuid> ArenaFormationIds;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") TArray<int32> ArenaFormationSpots;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") int32 ArenaMatches = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") int32 ArenaWins = 0;
	/** Set on the first tame ever (opens the Mine Mouth). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") bool bHasTamed = false;
	/** Deepest Hallow Night depth finished (0 = none yet; depth N+1 opens after N). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") int32 DepthsReached = 0;
	/** Pebble Mine crew (unlocks after the first tame). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") FGFMineCrew Crew;
	/** Shardstones this player may hop to. Empty on older saves. A row that starts unlocked still counts. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "GF|Progress") TArray<FName> UnlockedShardstones;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGFModeHandler : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by each mode's server director actor. All calls happen on the server. */
class GFNET_API IGFModeHandler
{
	GENERATED_BODY()

public:
	virtual FName GetModeName() const = 0;
	virtual FGFModeResult HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request) = 0;
	/** The player's character is in (rejoin: refresh views, collect offline gains). */
	virtual void OnModePlayerReady(AGFPlayerState* PS) {}
	/** The player is leaving (refund wagers, send crews home, ...). Their account still works (guest record). */
	virtual void OnModePlayerLeft(AGFPlayerState* PS) {}
	/** World save: serialise persistent mode state (empty = nothing to keep). */
	virtual void CaptureModeState(TArray<uint8>& Out) const {}
	virtual void RestoreModeState(const TArray<uint8>& In) {}
	/** Chat gate: a reason the player may not send chat right now (e.g. "Muted"), or None. */
	virtual FName ChatBlockReason(const AGFPlayerState* By) const { return NAME_None; }
	/**
	 * Build/break gate (AGFPlayerState::ValidateEdit): a reason By may not place or remove a block at Location
	 * (e.g. a visitor on another faction's land), or None. Paint does not go through this gate.
	 */
	virtual FName EditBlockReason(const AGFPlayerState* By, const FVector& Location) const { return NAME_None; }
	/** Join gate (PreLogin / PostLogin): true to refuse this player key, with a reason shown to them. */
	virtual bool RefuseJoin(const FString& PlayerKey, FString& OutWhy) const { return false; }
};

/** Helpers to (de)serialise a USTRUCT to bytes with names as strings (stable across runs). */
struct GFNET_API FGFModeBytes
{
	static void Write(UScriptStruct* Struct, const void* Data, TArray<uint8>& Out);
	static bool Read(UScriptStruct* Struct, void* Data, const TArray<uint8>& In);

	template <typename T> static void Write(const T& Data, TArray<uint8>& Out) { Write(T::StaticStruct(), &Data, Out); }
	template <typename T> static bool Read(T& Data, const TArray<uint8>& In) { return Read(T::StaticStruct(), &Data, In); }
};
