#pragma once

// Wall climb, mantle and wall run clips (#356, wallclimb_anims_v1): DT_AnimMap keys, notify names, and the pure driver
// that turns the movement state into claims on UGFSimpleAnimComponent. The clips are not imported yet; every claim
// is gated on the clip loading, so until the FBX land the body keeps playing locomotion exactly as before.
// Owners: EGFAnimOwner::Climbing (Enter, Loop, Exit_Fall), EGFAnimOwner::Mantling (Mantle_Up High / Low),
// EGFAnimOwner::WallRun (Loop_L / Loop_R). The owner stack decides who shows; nothing here second-guesses it.
// Decision #370: the wall run lean (about 45 deg) is authored in the clip, so code never tilts the mesh; the mantle
// splits into Low (lip up to 120 cm) and High (the 220 cm lip the current Mantle_Up clip is authored for).

#include "CoreMinimal.h"
#include "GFAnimOwnerStack.h"
#include "GFAnimMap.h"
#include "Templates/Function.h"

/** Which side of her the wall is on during a wall run. None when not wall running (or unknown on a remote copy). */
enum class EGFWallRunSide : uint8
{
	None = 0,
	/** Wall on her left: Player.WallRun_Loop_L. */
	Left = 1,
	/** Wall on her right: Player.WallRun_Loop_R. */
	Right = 2,
};

namespace GFTraversalAnim
{
	// DT_AnimMap keys (Data/CSV/DT_AnimMap.csv). Clips: A_Player_Base_<name> under /Game/Art/Characters/Player/Anims.
	inline FName ClimbEnter() { return FName(TEXT("Player.WallClimb_Enter")); }
	inline FName ClimbLoop() { return FName(TEXT("Player.WallClimb_Loop")); }
	inline FName ClimbExitFall() { return FName(TEXT("Player.WallClimb_Exit_Fall")); }
	/** Authored for the 220 cm lip (WallClimbMantleReach). Plays when Low is unavailable or the lip is above MantleLowMaxLipCm. */
	inline FName MantleHigh() { return FName(TEXT("Player.Mantle_Up")); }
	/** Lips up to MantleLowMaxLipCm. DT_AnimMap row present; clip import still pending (MantleKey falls back to High until then). */
	inline FName MantleLow() { return FName(TEXT("Player.Mantle_Up_Low")); }
	inline FName WallRunLoopL() { return FName(TEXT("Player.WallRun_Loop_L")); }
	inline FName WallRunLoopR() { return FName(TEXT("Player.WallRun_Loop_R")); }

	/** Manifest natural speeds, used when the row has no NaturalSpeedCms. Climb loop: stride 116.67 cm over 0.333 s. */
	constexpr float ClimbLoopNaturalSpeedCms = 350.f;
	/** Wall run loops: stride 240 cm over 0.533 s. */
	constexpr float WallRunLoopNaturalSpeedCms = 450.f;

	/** Decision #370: lips up to this height (cm above the feet) take the Low mantle. */
	constexpr float MantleLowMaxLipCm = 120.f;
	/** Decision #370: the High mantle is authored for this lip (cm above the feet). */
	constexpr float MantleHighLipCm = 220.f;

	/**
	 * Extra seconds on the timed Enter claim past its clip length. The driver swaps to the loop when the clip ends;
	 * the grace keeps the stack (which ticks after the character) from dropping Enter a frame before the loop lands.
	 */
	constexpr float EnterGraceSeconds = 0.1f;

	/** Code default natural speed for a traversal loop key, 0 for everything else (plays at 1.0). */
	inline float DefaultNaturalSpeedCms(FName Key)
	{
		if (Key == ClimbLoop())
		{
			return ClimbLoopNaturalSpeedCms;
		}
		if (Key == WallRunLoopL() || Key == WallRunLoopR())
		{
			return WallRunLoopNaturalSpeedCms;
		}
		return 0.f;
	}

	/**
	 * Mantle key by lip height. Low for a known lip at or under MantleLowMaxLipCm when a Low clip exists, otherwise
	 * High. A negative lip means unknown (a remote copy), which takes High, the clip that is authored today.
	 */
	inline FName MantleKey(float LipHeightCm, bool bLowClipAvailable)
	{
		return (bLowClipAvailable && LipHeightCm >= 0.f && LipHeightCm <= MantleLowMaxLipCm) ? MantleLow() : MantleHigh();
	}

	/** Loop_L for a wall on her left, Loop_R for her right, None when not wall running. */
	inline FName WallRunKey(EGFWallRunSide Side)
	{
		switch (Side)
		{
		case EGFWallRunSide::Left: return WallRunLoopL();
		case EGFWallRunSide::Right: return WallRunLoopR();
		default: return NAME_None;
		}
	}

	/**
	 * Wall on her left or right from the run direction and the wall normal (pointing off the wall, toward her). Her
	 * right is the run direction turned +90 deg yaw; a normal pointing to her left means the wall is on her right.
	 * None when either vector is flat zero or the normal runs along the run direction.
	 */
	inline EGFWallRunSide WallRunSideOf(const FVector& RunDir, const FVector& WallNormal)
	{
		const FVector2D Dir(RunDir.X, RunDir.Y);
		const FVector2D Normal(WallNormal.X, WallNormal.Y);
		if (Dir.IsNearlyZero() || Normal.IsNearlyZero())
		{
			return EGFWallRunSide::None;
		}
		const FVector2D Right(-Dir.Y, Dir.X);
		const double Dot = FVector2D::DotProduct(Right.GetSafeNormal(), Normal.GetSafeNormal());
		if (FMath::Abs(Dot) < 0.05)
		{
			return EGFWallRunSide::None;
		}
		return Dot > 0.0 ? EGFWallRunSide::Left : EGFWallRunSide::Right;
	}

	/**
	 * Proposed anim notify names (wallclimb_anims_v1 manifest). Code-side names only: the import pass adds them to the
	 * sequences as plain named notifies at the frames in ProposedNotifies. Nothing listens to them yet.
	 */
	namespace Notify
	{
		inline FName ClimbHandPlantL() { return FName(TEXT("Climb_HandPlant_L")); }
		inline FName ClimbHandPlantR() { return FName(TEXT("Climb_HandPlant_R")); }
		inline FName ClimbFootPlantL() { return FName(TEXT("Climb_FootPlant_L")); }
		inline FName ClimbFootPlantR() { return FName(TEXT("Climb_FootPlant_R")); }
		inline FName ClimbPushOff() { return FName(TEXT("Climb_PushOff")); }
		inline FName WallRunFootPlantL() { return FName(TEXT("WallRun_FootPlant_L")); }
		inline FName WallRunFootPlantR() { return FName(TEXT("WallRun_FootPlant_R")); }
		inline FName MantleHandsOnLip() { return FName(TEXT("Mantle_HandsOnLip")); }
		inline FName MantleHandsOff() { return FName(TEXT("Mantle_HandsOff")); }
	}

	/** One proposed notify: on Key's clip at Frame (1-based, 30 fps, as the manifest numbers them). */
	struct FProposedNotify
	{
		FName Key;
		FName Notify;
		int32 Frame = 1;
	};

	/** The manifest's notify table, clip by clip. */
	inline TArray<FProposedNotify> ProposedNotifies()
	{
		return {
			{ ClimbEnter(), Notify::ClimbHandPlantR(), 9 },
			{ ClimbLoop(), Notify::ClimbHandPlantR(), 1 },
			{ ClimbLoop(), Notify::ClimbFootPlantL(), 2 },
			{ ClimbLoop(), Notify::ClimbHandPlantL(), 6 },
			{ ClimbLoop(), Notify::ClimbFootPlantR(), 7 },
			{ ClimbExitFall(), Notify::ClimbPushOff(), 4 },
			{ WallRunLoopL(), Notify::WallRunFootPlantL(), 1 },
			{ WallRunLoopR(), Notify::WallRunFootPlantR(), 1 },
			{ MantleHigh(), Notify::MantleHandsOnLip(), 4 },
			{ MantleHigh(), Notify::MantleHandsOff(), 10 },
		};
	}
}

/** What the driver learned about a clip: its length, and the natural speed its play rate is measured against. */
struct FGFTraversalClipInfo
{
	/** Seconds. Zero or less: the clip is not there (no row, or not imported), so it is never claimed. */
	float Seconds = 0.f;
	/** cm/s. Zero or less plays at 1.0. */
	float NaturalSpeedCms = 0.f;

	bool IsAvailable() const { return Seconds > 0.f; }
};

/** One frame of movement state, read from UGFCharacterMovementComponent. */
struct FGFTraversalAnimInput
{
	bool bClimbing = false;
	bool bMantling = false;
	EGFWallRunSide WallRunSide = EGFWallRunSide::None;
	/** Lip height above the feet for the current mantle, cm. Negative when unknown. */
	float MantleLipCm = -1.f;
	/** Climb rise speed, cm/s (the loop's play rate is this / its natural speed). */
	float ClimbSpeedCms = 0.f;
	/** Wall run speed along the wall, cm/s. */
	float WallRunSpeedCms = 0.f;
	float DeltaSeconds = 0.f;
};

/** One change to apply to UGFSimpleAnimComponent. */
struct FGFTraversalAnimCue
{
	enum class EKind : uint8
	{
		Claim,
		Release,
		SetPlayRate,
	};
	EKind Kind = EKind::Claim;
	EGFAnimOwner Owner = EGFAnimOwner::Climbing;
	FName Key = NAME_None;
	bool bLoop = false;
	/** Claim: > 0 lets the claim expire on its own (one-shots), zero or less holds it until released. */
	float Seconds = 0.f;
	float PlayRate = 1.f;
};

/**
 * Turns movement edges into traversal claims. Pure rules, so a test drives it with no world, mesh or data table.
 * Climbing: rising edge claims Enter (timed, its clip length plus EnterGraceSeconds), then Loop held at climb speed / 350. Leaving the climb
 * without a mantle or a wall run claims Exit_Fall (timed); into a wall run or a mantle it just releases.
 * Mantling: releases the climb, claims MantleKey(lip) held until the mantle ends, then releases it.
 * Wall run: claims Loop_L / Loop_R held at wall run speed / 450, swaps on a side change, releases when it ends.
 * A missing clip is tried once per edge and never claimed, so the body never freezes on a clip that is not there.
 */
struct FGFTraversalAnimDriver
{
	using FCues = TArray<FGFTraversalAnimCue, TInlineAllocator<4>>;
	using FClipInfoFn = TFunctionRef<FGFTraversalClipInfo(FName)>;

	enum class EClimbPose : uint8
	{
		None,
		Enter,
		Loop,
	};

	EClimbPose ClimbPose = EClimbPose::None;
	float EnterSecondsLeft = 0.f;
	bool bWasClimbing = false;
	bool bWasMantling = false;
	/** The mantle key claimed for this mantle, or None. */
	FName MantleHeld = NAME_None;
	/** The wall run key tried for this side (claimed or missing), so a missing clip is not reloaded every frame. */
	FName WallRunTried = NAME_None;
	bool bWallRunHeld = false;

	FCues Tick(const FGFTraversalAnimInput& In, FClipInfoFn ClipInfo)
	{
		FCues Cues;
		const bool bClimbing = In.bClimbing && !In.bMantling;
		const bool bWallRun = In.WallRunSide != EGFWallRunSide::None && !bClimbing && !In.bMantling;

		// Mantle edges first: a mantle ends the climb that led to it.
		if (In.bMantling && !bWasMantling)
		{
			ReleaseClimb(Cues);
			const bool bLow = ClipInfo(GFTraversalAnim::MantleLow()).IsAvailable();
			const FName Key = GFTraversalAnim::MantleKey(In.MantleLipCm, bLow);
			if (ClipInfo(Key).IsAvailable())
			{
				Cues.Add(Claim(EGFAnimOwner::Mantling, Key, false, 0.f, 1.f));
				MantleHeld = Key;
			}
		}
		else if (!In.bMantling && bWasMantling)
		{
			ReleaseMantle(Cues);
		}

		if (bClimbing)
		{
			if (!bWasClimbing)
			{
				StartClimb(In, ClipInfo, Cues);
			}
			else if (ClimbPose == EClimbPose::Enter)
			{
				EnterSecondsLeft -= In.DeltaSeconds;
				if (EnterSecondsLeft <= 0.f)
				{
					StartLoop(In, ClipInfo, Cues);
				}
			}
			else if (ClimbPose == EClimbPose::Loop)
			{
				Cues.Add(Rate(EGFAnimOwner::Climbing, ClimbRate(In, ClipInfo)));
			}
		}
		else if (bWasClimbing)
		{
			// Climbing -> nothing: timed out, let go, or lost the wall. Into a mantle or a wall run: no fall clip.
			if (!In.bMantling && !bWallRun)
			{
				const FGFTraversalClipInfo Exit = ClipInfo(GFTraversalAnim::ClimbExitFall());
				if (Exit.IsAvailable())
				{
					Cues.Add(Claim(EGFAnimOwner::Climbing, GFTraversalAnim::ClimbExitFall(), false, Exit.Seconds, 1.f));
					ClimbPose = EClimbPose::None; // the timed claim runs out on its own
					EnterSecondsLeft = 0.f;
				}
				else
				{
					ReleaseClimb(Cues);
				}
			}
			else if (!In.bMantling)
			{
				ReleaseClimb(Cues);
			}
		}

		const FName RunKey = bWallRun ? GFTraversalAnim::WallRunKey(In.WallRunSide) : NAME_None;
		if (RunKey != WallRunTried)
		{
			if (bWallRunHeld)
			{
				Cues.Add(Release(EGFAnimOwner::WallRun));
				bWallRunHeld = false;
			}
			WallRunTried = RunKey;
			if (!RunKey.IsNone())
			{
				const FGFTraversalClipInfo Info = ClipInfo(RunKey);
				if (Info.IsAvailable())
				{
					Cues.Add(Claim(EGFAnimOwner::WallRun, RunKey, true, 0.f,
						FGFAnimMap::RateFromNaturalSpeed(In.WallRunSpeedCms, Info.NaturalSpeedCms)));
					bWallRunHeld = true;
				}
			}
		}
		else if (bWallRunHeld)
		{
			Cues.Add(Rate(EGFAnimOwner::WallRun,
				FGFAnimMap::RateFromNaturalSpeed(In.WallRunSpeedCms, ClipInfo(RunKey).NaturalSpeedCms)));
		}

		bWasClimbing = bClimbing;
		bWasMantling = In.bMantling;
		return Cues;
	}

	/** Drops every traversal claim this driver holds (respawn, possession change). */
	FCues Reset()
	{
		FCues Cues;
		ReleaseClimb(Cues);
		ReleaseMantle(Cues);
		if (bWallRunHeld)
		{
			Cues.Add(Release(EGFAnimOwner::WallRun));
		}
		bWallRunHeld = false;
		WallRunTried = NAME_None;
		bWasClimbing = false;
		bWasMantling = false;
		return Cues;
	}

private:
	static FGFTraversalAnimCue Claim(EGFAnimOwner Owner, FName Key, bool bLoop, float Seconds, float PlayRate)
	{
		FGFTraversalAnimCue Cue;
		Cue.Kind = FGFTraversalAnimCue::EKind::Claim;
		Cue.Owner = Owner;
		Cue.Key = Key;
		Cue.bLoop = bLoop;
		Cue.Seconds = Seconds;
		Cue.PlayRate = PlayRate;
		return Cue;
	}

	static FGFTraversalAnimCue Release(EGFAnimOwner Owner)
	{
		FGFTraversalAnimCue Cue;
		Cue.Kind = FGFTraversalAnimCue::EKind::Release;
		Cue.Owner = Owner;
		return Cue;
	}

	static FGFTraversalAnimCue Rate(EGFAnimOwner Owner, float PlayRate)
	{
		FGFTraversalAnimCue Cue;
		Cue.Kind = FGFTraversalAnimCue::EKind::SetPlayRate;
		Cue.Owner = Owner;
		Cue.PlayRate = PlayRate;
		return Cue;
	}

	static float ClimbRate(const FGFTraversalAnimInput& In, FClipInfoFn ClipInfo)
	{
		return FGFAnimMap::RateFromNaturalSpeed(In.ClimbSpeedCms, ClipInfo(GFTraversalAnim::ClimbLoop()).NaturalSpeedCms);
	}

	void StartClimb(const FGFTraversalAnimInput& In, FClipInfoFn ClipInfo, FCues& Cues)
	{
		const FGFTraversalClipInfo Enter = ClipInfo(GFTraversalAnim::ClimbEnter());
		if (Enter.IsAvailable())
		{
			Cues.Add(Claim(EGFAnimOwner::Climbing, GFTraversalAnim::ClimbEnter(), false, Enter.Seconds + GFTraversalAnim::EnterGraceSeconds, 1.f));
			ClimbPose = EClimbPose::Enter;
			EnterSecondsLeft = Enter.Seconds;
			return;
		}
		StartLoop(In, ClipInfo, Cues);
	}

	void StartLoop(const FGFTraversalAnimInput& In, FClipInfoFn ClipInfo, FCues& Cues)
	{
		EnterSecondsLeft = 0.f;
		if (ClipInfo(GFTraversalAnim::ClimbLoop()).IsAvailable())
		{
			Cues.Add(Claim(EGFAnimOwner::Climbing, GFTraversalAnim::ClimbLoop(), true, 0.f, ClimbRate(In, ClipInfo)));
			ClimbPose = EClimbPose::Loop;
			return;
		}
		// No loop clip: let the Enter claim (if any) run out and locomotion take the body back.
		ClimbPose = EClimbPose::None;
	}

	void ReleaseClimb(FCues& Cues)
	{
		Cues.Add(Release(EGFAnimOwner::Climbing));
		ClimbPose = EClimbPose::None;
		EnterSecondsLeft = 0.f;
	}

	void ReleaseMantle(FCues& Cues)
	{
		if (!MantleHeld.IsNone())
		{
			Cues.Add(Release(EGFAnimOwner::Mantling));
			MantleHeld = NAME_None;
		}
	}
};
