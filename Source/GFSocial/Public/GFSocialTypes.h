#pragma once

// Pure social rules and state (no world), so the headless tests can drive them directly. UGFSocialComponent applies
// them on the server. Factions are opaque ids (FName) here; no group names, no product or mode names from elsewhere.
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GFSocialTypes.generated.h"

/** One emote (Data/CSV/DT_Emotes.csv). Add a row to add an emote. */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFEmoteRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Shown in the emote wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Emote") FString Label;
	/** DT_AnimMap key to play (Player.Emote_Wave ...). None, or a key whose clip isn't there, falls back to the icon bubble. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Emote") FName AnimKey;
	/** Short text / icon id for the bubble over the player's head. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Emote") FString Icon;
	/** How long the bubble (or a looping clip) shows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Emote") float Seconds = 2.5f;
};

/** Per-mode social rules (Data/CSV/DT_ModeRules.csv), keyed by mode id. Unknown modes: no noise. */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFModeRulesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Emoting outside safe territory makes noise that draws nearby hostile dinos (free-roaming enemies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Social") bool bEmoteAttractsDanger = false;
	/** cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GF|Social") float NoiseRadius = 2500.f;
};

/** Safe-zone sandbox powers. */
UENUM(BlueprintType)
enum class EGFSocialPower : uint8
{
	Grow,
	Shrink,
	/** Tether another player and pull them along. */
	Grab
};

/** Live tuning (server). */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFSocialTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "GF|Social") float MinScale = 0.5f;
	UPROPERTY(EditAnywhere, Category = "GF|Social") float MaxScale = 2.f;
	/** Each Grow multiplies by this, each Shrink divides. */
	UPROPERTY(EditAnywhere, Category = "GF|Social") float ScaleStep = 1.25f;
	/** Per caster, per power. */
	UPROPERTY(EditAnywhere, Category = "GF|Social") float PowerCooldownSeconds = 2.f;
	UPROPERTY(EditAnywhere, Category = "GF|Social") float PowerRange = 1500.f;
	/** Grab: the target is held this far in front of the caster. */
	UPROPERTY(EditAnywhere, Category = "GF|Social") float TetherLength = 250.f;
	UPROPERTY(EditAnywhere, Category = "GF|Social") float PullSpeed = 900.f;
	UPROPERTY(EditAnywhere, Category = "GF|Social") float TetherMaxSeconds = 8.f;
	UPROPERTY(EditAnywhere, Category = "GF|Social") float EmoteCooldownSeconds = 1.5f;
	/** The underground cell (far below the hub). */
	UPROPERTY(EditAnywhere, Category = "GF|Social") FVector JailCellLocation = FVector(0.f, 0.f, -5000.f);
	/** A jailed pawn farther than this from the cell is put back. */
	UPROPERTY(EditAnywhere, Category = "GF|Social") float JailLeash = 600.f;
};

/** What the rules need to know about one player. */
struct GFSOCIAL_API FGFSocialPlayer
{
	FString Key;
	FName Faction;
	bool bHost = false;
	/** Inside safe territory right now. */
	bool bSafe = false;
	bool bJailed = false;
	/** Leader of their own faction. */
	bool bLeader = false;
	/** First claim of Faction. 0 = not recorded, so ChooseLeader sorts them last. */
	int32 ClaimOrder = 0;

	FGFSocialPlayer() = default;
	FGFSocialPlayer(const FString& InKey, FName InFaction, bool bInHost = false, bool bInSafe = true, bool bInLeader = false)
		: Key(InKey), Faction(InFaction), bHost(bInHost), bSafe(bInSafe), bLeader(bInLeader) {}
};

/** Result codes (FName so they travel in FGFModeResult). */
struct GFSOCIAL_API FGFSocialCodes
{
	static const FName Ok;
	static const FName NotSafe;       // the caster isn't in safe territory
	static const FName TargetNotSafe;
	static const FName SameFaction;   // powers only work on other factions
	static const FName NoFaction;
	static const FName Self;
	static const FName Jailed;        // the caster is jailed
	static const FName TargetJailed;
	static const FName Cooldown;
	static const FName OutOfRange;
	static const FName NotAllowed;
	static const FName TargetIsHost;
	static const FName AlreadyJailed;
	static const FName NotJailed;
	static const FName NoSuchPlayer;
	static const FName UnknownEmote;
	static const FName NotBanned;
	static const FName Muted;
};

struct GFSOCIAL_API FGFSocialRules
{
	/** Grow / Shrink / Grab: both in safe territory, different factions (both in one), in range, not jailed, off cooldown. */
	static FName CheckPower(const FGFSocialPlayer& By, const FGFSocialPlayer& Target, float Distance, bool bOnCooldown, const FGFSocialTuning& T);
	static float ClampScale(float Scale, const FGFSocialTuning& T);
	/** Current scale after one Grow (or Shrink), clamped. */
	static float StepScale(float Current, bool bGrow, const FGFSocialTuning& T);

	/**
	 * Jail power, the one matrix shared by jail and release: the host over anyone but themselves; a faction leader over
	 * members of their own faction, never the host; a jailed player has none; everyone else is denied.
	 */
	static FName JailPower(const FGFSocialPlayer& By, const FGFSocialPlayer& Target);
	/** Jail: JailPower on a target who isn't jailed yet. */
	static FName CanJail(const FGFSocialPlayer& By, const FGFSocialPlayer& Target);
	/**
	 * Release: JailPower on a jailed target (design rule: not limited to the original jailer). There is no timer: a
	 * prisoner stays jailed until someone with jail power releases them (or the session ends).
	 */
	static FName CanRelease(const FGFSocialPlayer& By, const FGFSocialPlayer& Target);
	/**
	 * Leader among online members of one faction (pass only those). Earliest ClaimOrder wins. Jailed members are
	 * skipped unless every member is jailed. ClaimOrder <= 0 sorts last. Empty if Online is empty.
	 */
	static FString ChooseLeader(const TArray<FGFSocialPlayer>& Online);
	/** Kick and ban (and unban: Target = the banned player). The one place to change who may do it: host only today. */
	static FName CanKickOrBan(const FGFSocialPlayer& By, const FGFSocialPlayer& Target);

	/** DT_ModeRules row for Mode (null if none). */
	static const FGFModeRulesRow* FindModeRules(FName Mode);
	static bool EmoteAttractsDanger(FName Mode);
	/** Does an emote here make noise? Only outside safe territory, in a mode whose flag is on. */
	static bool EmoteMakesNoise(bool bSafe, FName Mode);
	static const FGFEmoteRow* FindEmote(FName EmoteId);
	/** All emote ids in the table, in row order. */
	static TArray<FName> EmoteIds();
	/** The DT_AnimMap key an emote plays, or None when it shows the icon bubble (no key, or the key isn't in DT_AnimMap). */
	static FName EmoteAnimKey(const FGFEmoteRow& Row);
};

/** Per-key, per-action cooldowns (server seconds). */
struct GFSOCIAL_API FGFCooldowns
{
	TMap<FString, double> Until;

	static FString Slot(const FString& Key, FName Action) { return Key + TEXT("|") + Action.ToString(); }
	bool IsCooling(const FString& Key, FName Action, double Now) const;
	void Start(const FString& Key, FName Action, double Now, double Seconds);
};

/** A jailed player (replicated; kept for the whole session, also across a rejoin). */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFJailEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FString JailerKey;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FVector ReturnLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") float Since = 0.f;
};

/** A banned player (saved with the host's world). */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFBanEntry
{
	GENERATED_BODY()

	/** Stable player id (AGFPlayerState::PlayerKey: the platform net id). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Social") FString Key;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Social") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Social") FString ByKey;
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "GF|Social") int64 AtUtcTicks = 0;
};

/** Mode blob "Social" in the world save. */
USTRUCT()
struct GFSOCIAL_API FGFSocialSave
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) int32 Version = 1;
	UPROPERTY(SaveGame) TArray<FGFBanEntry> Bans;
};

USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFScaleEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") float Scale = 1.f;
};

/** Pure per-player scale book: Grow / Shrink steps, clamped, and the revert when someone leaves safety. */
struct GFSOCIAL_API FGFScaleBook
{
	TArray<FGFScaleEntry> Entries;

	float Get(const FString& Key) const;
	/** Returns the new scale. */
	float Step(const FString& Key, bool bGrow, const FGFSocialTuning& T);
	void Reset(const FString& Key);
	/** Back to 1x for everyone IsSafe says is outside safety. Returns the keys reverted. */
	TArray<FString> RevertUnsafe(TFunctionRef<bool(const FString&)> IsSafe);
};

/** Grab tether (replicated so clients can draw the line). */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFTether
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FString CasterKey;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Social") FString TargetKey;
	UPROPERTY() float Age = 0.f;
};

/** One emote played (replicated log, newest last). */
USTRUCT(BlueprintType)
struct GFSOCIAL_API FGFEmoteEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") int32 Seq = 0;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") FName EmoteId;
	/** None: show Icon in a bubble instead. */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") FName AnimKey;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") FString Icon;
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") float Seconds = 0.f;
	/** It made noise (outside safety in a mode with free-roaming enemies). */
	UPROPERTY(BlueprintReadOnly, Category = "GF|Emote") bool bNoise = false;
};
