#pragma once

#include "CoreMinimal.h"

/**
 * Who is driving a single-node art mesh. Higher value wins. Locomotion is the implicit fallback (idle / move picked
 * by UGFSimpleAnimComponent) and is never claimed.
 * Order: ferry seat > mantling > wall run > climbing > fist scope > emote > mining > Beat Hop clip > locomotion.
 * Mining has its own owner so an emote over a held mining loop restores the loop when it ends, and ending the
 * mining field never cuts an emote.
 * Traversal (#356) sits above fist scope, emote and mining: a body on a wall must show the wall clip, and a held fist
 * scope or mining loop underneath comes back when the climb, mantle or wall run lets go (a timed emote just expires
 * underneath). Mantling beats wall run beats climbing, so a mantle that ends a climb, or a wall run a climb hands
 * off to, is never hidden by the climb's own Enter / Exit_Fall one-shot. The ferry seat stays on top: nobody climbs
 * while seated. Plain enum, never saved or replicated, so these values can move.
 */
enum class EGFAnimOwner : uint8
{
	Locomotion = 0,
	BeatHop = 1,
	Mining = 2,
	Emote = 3,
	FistScope = 4,
	/** #356 wall climb: Player.WallClimb_Enter, Player.WallClimb_Loop, Player.WallClimb_Exit_Fall. */
	Climbing = 5,
	/** #356 wall run: Player.WallRun_Loop_L / Player.WallRun_Loop_R by the side the wall is on. */
	WallRun = 6,
	/** #356 climb-exit mantle: Player.Mantle_Up (High), Player.Mantle_Up_Low (Low, reserved). */
	Mantling = 7,
	FerrySeat = 8,
};

/** Debug name for an owner (logs and test messages). */
GFCORE_API const TCHAR* GFAnimOwnerName(EGFAnimOwner Owner);

/** One owner's clip request. */
struct FGFAnimClaim
{
	EGFAnimOwner Owner = EGFAnimOwner::Locomotion;
	/** DT_AnimMap key. */
	FName Key = NAME_None;
	bool bLoop = true;
	float PlayRate = 1.f;
	/** Seconds left. Zero or less means "held until Release". */
	float Remaining = 0.f;
	bool bTimed = false;
	/** Bumped by every Claim, so a re-claim (or a restore to this owner) is seen as a new clip to play. */
	uint32 Serial = 0;
};

/**
 * Priority stack of clip owners, one entry per owner. Replaces the old single "pause timer" where the last caller won.
 * Claim adds or replaces the caller's entry; Release removes it and the next-highest owner is on top again.
 * Plain data so it can be tested without a world or a mesh.
 */
struct GFCORE_API FGFAnimOwnerStack
{
	/**
	 * Adds or replaces Owner's claim. Seconds > 0 expires on its own after that long (Tick); zero or less is held until
	 * Release. Locomotion and a None key are ignored. Returns true when Owner is on top afterwards.
	 */
	bool Claim(EGFAnimOwner Owner, FName Key, bool bLoop, float Seconds = 0.f, float PlayRate = 1.f);

	/** Removes Owner's claim. Returns true when it had one. */
	bool Release(EGFAnimOwner Owner);

	/** Removes Owner's claim only while it is playing Key, so a caller can't drop a clip someone else put there. */
	bool ReleaseKey(EGFAnimOwner Owner, FName Key);

	/** Resets a timed claim's time left (a lease). Untimed claims stay untimed. Returns false when Owner has no claim. */
	bool Renew(EGFAnimOwner Owner, float Seconds);

	/** Changes Owner's play rate without restarting the clip. Returns false when Owner has no claim. */
	bool SetPlayRate(EGFAnimOwner Owner, float PlayRate);

	/** Counts down timed claims, buried ones included, and drops the ones that ran out. Returns true when any did. */
	bool Tick(float DeltaSeconds);

	/** The highest claim, or nullptr when locomotion drives the mesh. */
	const FGFAnimClaim* Top() const;

	/** Top()'s owner, or Locomotion. */
	EGFAnimOwner TopOwner() const;

	const FGFAnimClaim* Find(EGFAnimOwner Owner) const;
	bool Has(EGFAnimOwner Owner) const { return Find(Owner) != nullptr; }
	int32 Num() const { return Claims.Num(); }
	void Reset() { Claims.Reset(); }

private:
	/** Sorted by owner, highest first. */
	TArray<FGFAnimClaim, TInlineAllocator<4>> Claims;
	uint32 NextSerial = 1;
};
