#pragma once

// Mining clips (mining_anims_v1): DT_AnimMap keys, the tap and field choices, and contact times for one-shot lands.
// The clips play on the local body through UGFSimpleAnimComponent::ClaimClip as EGFAnimOwner::Mining, under emotes, the
// fist scope and the ferry seat. The owner stack decides who shows; nothing here second-guesses it.
// Contact times are mining_anims_v2 frames at 30 fps. The character waits on this table. It does not need an anim
// notify, so a clip that fails to load can still land immediately.

#include "CoreMinimal.h"

namespace GFMiningAnim
{
	inline FName Punch() { return FName(TEXT("Player.Mine_Punch")); }
	inline FName Dig() { return FName(TEXT("Player.Mine_Dig")); }
	inline FName Claw() { return FName(TEXT("Player.Mine_Claw")); }
	inline FName Overcharge() { return FName(TEXT("Player.Mine_Overcharge")); }
	inline FName ElementSwap() { return FName(TEXT("Player.Cast_Element_Swap")); }
	inline FName FieldStart() { return FName(TEXT("Player.Mine_Field_Start")); }
	inline FName FieldLoop() { return FName(TEXT("Player.Mine_Field_Loop")); }
	inline FName FieldEnd() { return FName(TEXT("Player.Mine_Field_End")); }

	/** A tap hit this close above the feet (or on a block's top face) is dug, not punched. */
	constexpr float DigAboveFeetCm = 30.f;

	/** Tap to mine: Dig for the ground or a block below, Punch for anything in front. */
	inline FName TapAnim(float ImpactZ, float ImpactNormalZ, float FeetZ)
	{
		return (ImpactNormalZ > 0.7f || ImpactZ < FeetZ + DigAboveFeetCm) ? Dig() : Punch();
	}

	/** The field clips this file owns. Only these are released when the field ends, never a tap or claw one-shot. */
	inline bool IsFieldKey(FName Key)
	{
		return Key == FieldStart() || Key == FieldLoop();
	}

	/** mining_anims_v2 contact frames are authored at this rate. */
	constexpr float ClipFps = 30.f;

	/**
	 * Frame the one-shot connects, or 0 when the clip has no contact.
	 * Punch 6, Dig 9, Claw 7, Overcharge 13, element swap 6. Field clips and FistScope stay 0.
	 */
	inline int32 ContactFrame(FName Key)
	{
		if (Key == Punch() || Key == ElementSwap())
		{
			return 6;
		}
		if (Key == Dig())
		{
			return 9;
		}
		if (Key == Claw())
		{
			return 7;
		}
		if (Key == Overcharge())
		{
			return 13;
		}
		return 0;
	}

	/** Seconds from the start of the clip until ContactFrame. 0 when the clip has no contact. */
	inline float ContactSeconds(FName Key)
	{
		const int32 Frame = ContactFrame(Key);
		return Frame > 0 ? static_cast<float>(Frame) / ClipFps : 0.f;
	}

	/**
	 * Delay the land only when the clip claimed and the key has a contact time.
	 * A miss, or a 0 contact, keeps the land immediate so mining does not wait on a clip that is not there.
	 */
	inline bool DelayLand(bool bClipClaimed, float InContactSeconds)
	{
		return bClipClaimed && InContactSeconds > 0.f;
	}
}

/** What a mining one-shot does when its contact time arrives. */
enum class EGFMiningContactLand : uint8
{
	None,
	RemoveBlock,
	Claw,
	ElementSwap,
	Overcharge
};

/**
 * One land waiting on a Mining one-shot. The clip is claimed first. Tick returns the land once the contact
 * time has passed and the Mining claim is still that key. NoteClaim (the Mining claim was replaced) or a
 * missing key (released or expired) drops it. A 0 contact is not armed: the caller runs the land immediately.
 */
struct FGFMiningContact
{
	bool IsPending() const { return bPending; }
	FName PendingKey() const { return bPending ? Key : NAME_None; }

	/** A Mining claim was just added or replaced. Any new claim drops the land that was waiting on the old one. */
	void NoteClaim(FName)
	{
		if (bPending)
		{
			Clear();
		}
	}

	/** Wait InContactSeconds, then fire InLand, while the Mining claim stays on InKey. */
	void Arm(FName InKey, float InContactSeconds, EGFMiningContactLand InLand)
	{
		bPending = InContactSeconds > 0.f && InLand != EGFMiningContactLand::None && !InKey.IsNone();
		Key = bPending ? InKey : NAME_None;
		SecondsLeft = bPending ? InContactSeconds : 0.f;
		Land = bPending ? InLand : EGFMiningContactLand::None;
	}

	/**
	 * One frame. MiningKey is the Mining owner's claim this frame, or None when that claim is gone.
	 * Read it before the owner stack expires timed claims, so a clip that still has time can still connect.
	 */
	EGFMiningContactLand Tick(float DeltaSeconds, FName MiningKey)
	{
		if (!bPending)
		{
			return EGFMiningContactLand::None;
		}
		if (MiningKey != Key)
		{
			Clear();
			return EGFMiningContactLand::None;
		}
		if (DeltaSeconds > 0.f)
		{
			SecondsLeft -= DeltaSeconds;
		}
		if (SecondsLeft > 0.f)
		{
			return EGFMiningContactLand::None;
		}
		const EGFMiningContactLand Fired = Land;
		Clear();
		return Fired;
	}

private:
	void Clear()
	{
		bPending = false;
		Key = NAME_None;
		SecondsLeft = 0.f;
		Land = EGFMiningContactLand::None;
	}

	bool bPending = false;
	FName Key;
	float SecondsLeft = 0.f;
	EGFMiningContactLand Land = EGFMiningContactLand::None;
};

/** Where the held mining field pose is. */
enum class EGFMiningFieldPose : uint8
{
	None,
	FieldStart,
	FieldLoop
};

/** What the character should do with its Mining claim after a field step. */
enum class EGFMiningFieldCue : uint8
{
	Nothing,
	PlayStart,
	PlayLoop,
	PlayEnd,
	/** Drop the Mining claim if it is a field key (GFMiningAnim::IsFieldKey). */
	ReleaseField
};

/**
 * The held mining field (LMB hold): Start once past the tap time, then Loop until release, then End.
 * Pure rules so the pose and the claim can't drift apart: every way out of FieldStart or FieldLoop hands back a cue
 * that drops the field claim. The character applies the cue and reports what played with Played().
 */
struct FGFMiningField
{
	EGFMiningFieldPose Pose = EGFMiningFieldPose::None;
	/** Time left on Mine_Field_Start before the loop takes over. */
	float StartSecondsLeft = 0.f;
	/** The start clip was tried for this press, so a missing clip is not retried every frame. */
	bool bTried = false;

	/** A new LMB press. */
	void Press()
	{
		bTried = false;
	}

	/**
	 * One frame. bScoped: the fist scope holds the body. MiningKey: the Mining owner's current claim key (None once a
	 * one-shot expired or nothing is claimed).
	 */
	EGFMiningFieldCue Tick(float DeltaSeconds, bool bHeld, float HeldSeconds, float TapSeconds, bool bScoped, FName MiningKey)
	{
		if (bScoped)
		{
			// The scope takes the body. Let go of the field claim now; it must not come back when the scope ends.
			if (Pose != EGFMiningFieldPose::None)
			{
				Pose = EGFMiningFieldPose::None;
				return EGFMiningFieldCue::ReleaseField;
			}
			return EGFMiningFieldCue::Nothing;
		}
		if (bHeld && !bTried && HeldSeconds >= TapSeconds)
		{
			bTried = true;
			return EGFMiningFieldCue::PlayStart;
		}
		if (Pose == EGFMiningFieldPose::FieldStart)
		{
			StartSecondsLeft -= DeltaSeconds;
			if (StartSecondsLeft <= 0.f)
			{
				return EGFMiningFieldCue::PlayLoop;
			}
		}
		else if (Pose == EGFMiningFieldPose::FieldLoop && MiningKey.IsNone())
		{
			// A one-shot (claw, element swap) replaced the loop and ran out. The field is still held: loop again.
			return EGFMiningFieldCue::PlayLoop;
		}
		return EGFMiningFieldCue::Nothing;
	}

	/** The character applied Cue. bPlayed: the clip loaded and was claimed. Seconds: its length (one-shots). */
	void Played(EGFMiningFieldCue Cue, bool bPlayed, float Seconds)
	{
		switch (Cue)
		{
		case EGFMiningFieldCue::PlayStart:
			Pose = bPlayed ? EGFMiningFieldPose::FieldStart : EGFMiningFieldPose::None;
			StartSecondsLeft = bPlayed ? Seconds : 0.f;
			break;
		case EGFMiningFieldCue::PlayLoop:
			Pose = bPlayed ? EGFMiningFieldPose::FieldLoop : EGFMiningFieldPose::None;
			break;
		default:
			break;
		}
	}

	/** LMB released. PlayEnd when a field pose was up (drop the field claim if End can't play), else ReleaseField. */
	EGFMiningFieldCue End()
	{
		const bool bWasUp = Pose != EGFMiningFieldPose::None;
		Pose = EGFMiningFieldPose::None;
		StartSecondsLeft = 0.f;
		return bWasUp ? EGFMiningFieldCue::PlayEnd : EGFMiningFieldCue::ReleaseField;
	}
};
