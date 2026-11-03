#pragma once

// Wild kill rules. Hit points, an explicit life state, and one drop payment.
// Tamed and rented dinos never enter Downed or Dead and never pay.
// Pure: tests drive FGFDinoKillMachine with no world. AGFDino runs one on the server.

#include "CoreMinimal.h"
#include "GFItemTypes.h"
#include "GFDinoKill.generated.h"

class AGFDino;

/** Alive takes hits. Downed has paid and is waiting out the destroy delay. Dead is ready to destroy. */
UENUM(BlueprintType)
enum class EGFDinoLife : uint8
{
	Alive,
	Downed,
	Dead
};

/** Quest and Elder Book listen here. Raised once on the server when a wild dino enters Downed. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGFOnDinoKilled, AGFDino* /*Dino*/, const FString& /*KillerKey*/, FName /*Species*/);

/** What one ApplyHit or Advance call changed. Stacks is empty unless this call is the one payment. */
struct GFCOMPANION_API FGFDinoKillResult
{
	EGFDinoLife Life = EGFDinoLife::Alive;
	float HitPoints = 0.f;
	/** Hit points actually removed. Zero when this dino cannot die or the hit missed a living body. */
	float Applied = 0.f;
	/** True only on the Alive to Downed transition. A repeat call stays false. */
	bool bKilled = false;
	/** True only on the Downed to Dead transition. */
	bool bNowDead = false;
	FString KillerKey;
	TArray<FGFItemStack> Stacks;
};

/** Drop table and the life transition. No actor, no clock of its own. */
struct GFCOMPANION_API FGFDinoKill
{
	static constexpr float DefaultHitPoints = 60.f;
	/** Seconds spent Downed before the body may be destroyed. The director respawns after that. */
	static constexpr float DownedSeconds = 2.f;
	static constexpr float SkinChance = 0.30f;
	static constexpr int32 MaterialCount = 2;

	/** Hide, Scales, or None when this species has no table. */
	static FName MaterialItem(FName Species);
	/** Skin_<Species>, or None when this species has no table. */
	static FName SkinItem(FName Species);

	/** First draw of FRandomStream(Seed). The skin drops when this is strictly below SkinChance. */
	static float SkinFraction(int32 Seed);
	/** 2 material, plus one skin when the seeded draw hits. Empty when the species has no table. */
	static TArray<FGFItemStack> Roll(FName Species, int32 Seed);

	/** Largest share. A tie goes to KillingBlow, then to the earlier key in sort order. Empty keys are ignored. */
	static FString PickKiller(const TMap<FString, float>& Shares, const FString& KillingBlow);

	/**
	 * The only life transition.
	 * Dead stays Dead. A dino that cannot die stays Alive (or Downed, if it was already down).
	 * Alive with no hit points left becomes Downed. Downed becomes Dead once StateSeconds reaches DownedSeconds.
	 */
	static EGFDinoLife NextLife(EGFDinoLife Current, float HitPoints, float StateSeconds, bool bCanDie);

	static bool CanDie(bool bTamed, bool bRented) { return !bTamed && !bRented; }

	static FGFOnDinoKilled& OnDinoKilled();
};

/** Server-side vitals for one dino. Reset before the first hit. */
struct GFCOMPANION_API FGFDinoKillMachine
{
	EGFDinoLife Life = EGFDinoLife::Alive;
	float HitPoints = FGFDinoKill::DefaultHitPoints;
	float MaxHitPoints = FGFDinoKill::DefaultHitPoints;
	/** Seconds in the current life state. The Downed delay reads this. */
	float StateSeconds = 0.f;
	bool bPaid = false;
	TMap<FString, float> Shares;
	FString KillingBlow;

	void Reset(float MaxHp);
	/**
	 * Damage is the full swing, counted in Shares even past the last hit point.
	 * bTamed or bRented: no hit points change, no payment.
	 * Seed is only read on the payment.
	 */
	FGFDinoKillResult ApplyHit(FName Species, float Damage, const FString& AttackerKey, int32 Seed, bool bTamed, bool bRented);
	/** Moves Downed toward Dead. Does not pay. */
	FGFDinoKillResult Advance(float DeltaSeconds);
};
