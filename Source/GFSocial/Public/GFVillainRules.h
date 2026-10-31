#pragma once

// Opt-in Villain Mode. Both sides have to be in it, and it only lands in the wild.
// Jail does not block a hit: a jailed player can be targeted. A jailed player cannot arm.
// Duels are parked and never open a damage path. The choice is not saved.

#include "CoreMinimal.h"
#include "GFItemTypes.h"

class UDataTable;
struct FRandomStream;

enum class EGFVillainPlace : uint8
{
	Wild,
	/** A hazard box sitting in wild land. Lava does not care, and neither does a villain hit. */
	Hazard,
	Safe,
	Faction,
	Neutral,
	/** Cell and leash. Not a safe circle, so a villain here can still be hit. */
	Jail,
	MiniGame
};

struct GFSOCIAL_API FGFVillainSide
{
	FString Key;
	bool bVillain = false;
	bool bJailed = false;
	bool bDino = false;
	bool bRental = false;
	FString OwnerKey;
	EGFVillainPlace Place = EGFVillainPlace::Wild;
	/** World seconds. Negative means not in that window. */
	double WarmupUntil = -1.0;
	double ImmuneUntil = -1.0;
	float Health = 100.f;
	FVector Location = FVector::ZeroVector;
};

struct GFSOCIAL_API FGFVillainQuery
{
	double Now = 0.0;
	float RawDamage = 0.f;
};

struct GFSOCIAL_API FGFVillainHit
{
	bool bDamage = false;
	bool bBonk = false;
	float Damage = 0.f;
	FName Code;
	/** A landed hit restarts the disarm lock on both sides. */
	bool bRestartLock = false;
};

struct GFSOCIAL_API FGFVillainStatus
{
	FString Key;
	bool bOn = false;
	double WarmupUntil = -1.0;
	double LockUntil = -1.0;
	double DisarmUntil = -1.0;
	double ImmuneUntil = -1.0;
	int32 Title = INDEX_NONE;
	bool bTitleSpent = false;
	int32 WinStreak = 0;
	float Health = 100.f;
	/** Who hit this side, and when. Kept so the immunity window can name them. */
	TArray<TPair<FString, double>> RecentHits;
};

struct GFSOCIAL_API FGFVillainRules
{
	static constexpr float WarmupSeconds = 5.f;
	static constexpr float LockSeconds = 30.f;
	static constexpr float DisarmSeconds = 3.f;
	static constexpr float ImmuneSeconds = 10.f;
	static constexpr float RecentHitSeconds = 60.f;
	static constexpr float SightCm = 6000.f;
	static constexpr float FaintMaterialFraction = 0.1f;
	static constexpr int32 FaintDropCap = 50;
	static constexpr float StartHealth = 100.f;
	static constexpr int32 WantedStreak = 3;

	static float MoveSpeedMultiplier();
	static bool PersistsOnCharacter();
	static bool GrantsBuildRights();
	static bool ChangesGiantTargeting();
	static bool CountsAsDinoKill();
	static int32 RepDelta();
	static const TCHAR* RetireLine();
	static TArray<FString> Titles();
	static FString TitleAt(int32 Index);
	static FString DinobookLine(int32 WinStreak);

	/** Always parked. bStarted stays false. There is no duel damage exception. */
	static FName RequestDuel(bool& bStarted);

	static bool PlaceAllows(EGFVillainPlace Place);

	/** A tamed dino uses its owner's villain flag and warm-up. A rental still refuses. */
	static FGFVillainSide DinoSide(const FGFVillainSide& Owner, const FString& DinoKey, EGFVillainPlace Place, bool bRental);

	static FName TryArm(FGFVillainStatus& Status, double Now, bool bJailed, FRandomStream& Rng);
	/** Starts the disarm channel once the lock is clear. The flag stays on until Tick finishes it. */
	static FName TryDisarm(FGFVillainStatus& Status, double Now);
	/** Completes a finished channel. Returns true when the flag dropped. */
	static bool Tick(FGFVillainStatus& Status, double Now);
	static FName RerollTitle(FGFVillainStatus& Status, FRandomStream& Rng);

	static FGFVillainHit CanDamage(const FGFVillainSide& Attacker, const FGFVillainSide& Victim, const FGFVillainQuery& Query);

	/** Restarts both locks and remembers the attacker on the victim. */
	static void NoteHit(FGFVillainStatus& Attacker, FGFVillainStatus& Victim, const FGFVillainHit& Hit, double Now);

	/**
	 * 10% of each material stack, floored, capped at FaintDropCap units.
	 * Tools, outfits, furniture, seeds, treats, blocks and account-bound rows stay put.
	 */
	static TArray<FGFItemStack> FaintDrop(const FGFInventory& Bag, const UDataTable* Items);

	/** Health back to full, immunity window, streak cleared. The caller already removed the drop. */
	static void NoteFaint(FGFVillainStatus& Victim, FGFVillainStatus& Attacker, double Now);
};
