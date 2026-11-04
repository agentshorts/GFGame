#pragma once

// Mining clips (mining_anims_v1): DT_AnimMap keys and the small choices AGFCharacter makes before playing one.
// The clips play on the local body through UGFSimpleAnimComponent::ClaimClip as EGFAnimOwner::Mining, under emotes, the
// fist scope and the ferry seat. The owner stack decides who shows; nothing here second-guesses it.

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
}

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
