#pragma once

// Hallow Night (the Depths, PLAN.md M7): a 2.5D side-on dungeon. Everything here is pure and deterministic:
// the mover (X/Z only, there is no Y to drift into), shared Z/X/C platforms whose position is a function of
// server time, the run rules (5 HP, 3 heals per section, 2 checkpoints per depth, half of unbanked loot lost
// on death, Lift Home banks everything) and the greybox level layout.
#include "CoreMinimal.h"
#include "Math/Box2D.h"
#include "GFItemTypes.h"
#include "GFRewards.h"
#include "Engine/DataTable.h"
#include "GFDepthsTypes.generated.h"

/** DT_DepthsRewards: what a loot pickup rolls at each depth (row Depth1..Depth3). The only source of tier-4 minerals. */
USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsRewardRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depths") int32 Depth = 1;
	/** A loot pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depths") TArray<FGFRewardEntry> Pickup;
	/** A defeated shade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depths") TArray<FGFRewardEntry> Enemy;
	/** Reaching the exit lift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Depths") TArray<FGFRewardEntry> Exit;
};

/** Tuning for the custom controls (cm, seconds). */
struct GFDEPTHS_API FGFDepthsMoveParams
{
	float RunSpeed = 500.f;
	float GroundAccel = 9000.f;  // tight: full speed in ~0.06 s
	float AirAccel = 6000.f;
	float Gravity = 2400.f;
	float JumpSpeed = 950.f;
	float JumpCutSpeed = 380.f;  // releasing jump early cuts the rise
	float MaxFall = 1600.f;
	float CoyoteTime = 0.08f;
	float JumpBuffer = 0.10f;
	float DashDistance = 300.f;
	float DashTime = 0.15f;
	float DashCooldown = 0.6f;
	float WallSlideSpeed = 250.f;
	float WallJumpX = 520.f;
	float WallJumpZ = 900.f;
	float WallJumpLock = 0.15f;  // input ignored briefly so the kick-off sticks
	float KnockbackX = 450.f;
	float KnockbackZ = 450.f;
	float KnockbackLock = 0.2f;
	float Invulnerable = 0.8f;
	float HalfWidth = 30.f;
	float HalfHeight = 60.f;
	bool bDoubleJump = false;    // from depth 2
	bool bWallJump = true;

	static FGFDepthsMoveParams ForDepth(int32 Depth);
};

/** One frame of input. Pressed flags are edges (one frame), Held is the button state. */
struct GFDEPTHS_API FGFDepthsInput
{
	float MoveX = 0.f;
	bool bJumpPressed = false;
	bool bJumpHeld = false;
	bool bDashPressed = false;
};

/** A player's body on the side plane: X right, Z up. */
USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsBody
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D Pos = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D Vel = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") bool bGrounded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Facing = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 WallDir = 0;
	float Coyote = 0.f;
	float Buffer = 0.f;
	int32 JumpsUsed = 0;
	float DashLeft = 0.f;
	int32 DashDir = 1;
	float DashCooldown = 0.f;
	float LockLeft = 0.f;
	float InvulnLeft = 0.f;
	/** Platform it stands on (INDEX_NONE = static ground or air). */
	int32 OnPlatform = INDEX_NONE;
};

/** A Z/X/C platform: slides between A and B when its key is worked. */
USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsPlatform
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") FName Key;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D A = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D B = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FVector2D HalfSize = FVector2D(100.f, 20.f);
};

/** Shared state of one key (Z, X or C): everyone sees and works the same platforms. */
USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsSwitch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") FName Key;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") bool bAtB = false;
	/** Server time of the last change; the platform travels from there. */
	UPROPERTY(BlueprintReadOnly, Category = "Depths") double ChangedAt = -1000.0;
	/** Server time of the last press (the 0.4 s cooldown). */
	UPROPERTY(BlueprintReadOnly, Category = "Depths") double PressedAt = -1000.0;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") FString LastBy;
};

USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsEnemyDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Depths") float X1 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") float X2 = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") float Z = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") float Speed = 150.f;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 BaseHP = 2;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Section = 0;
};

/** The greybox layout of one depth. */
struct GFDEPTHS_API FGFDepthsLevel
{
	int32 Depth = 1;
	TArray<FBox2D> Solids;
	TArray<FBox2D> Hazards;
	TArray<FGFDepthsPlatform> Platforms;
	/** Two checkpoints per depth (each also a lift home); X on the floor. */
	TArray<FVector2D> Checkpoints;
	FVector2D Entrance = FVector2D::ZeroVector;
	FVector2D Exit = FVector2D::ZeroVector;
	TArray<FVector2D> Pickups;
	TArray<FGFDepthsEnemyDef> Enemies;
	/** Which section a point is in (0 = before checkpoint 1, 1, 2 = after checkpoint 2). */
	int32 SectionAt(float X) const;

	static const FGFDepthsLevel& Get(int32 Depth);
	static constexpr int32 MaxDepth = 3;
	static FGFDepthsLevel MakeGreybox(int32 Depth);
};

/** Shared platforms: the switch states plus where every platform is at a time. */
struct GFDEPTHS_API FGFDepthsPlatforms
{
	static constexpr double SwitchCooldown = 0.4;
	/** Every platform takes this long end to end; working a key mid-travel turns it around from where it is. */
	static constexpr double TravelSeconds = 1.0;
	static const FName Keys[3];

	static bool IsKey(FName Key);
	/** Toggles Key at Now unless it was worked < 0.4 s ago. Anyone may work any key. */
	static bool Press(TArray<FGFDepthsSwitch>& Switches, FName Key, double Now, const FString& By);
	static const FGFDepthsSwitch* Find(const TArray<FGFDepthsSwitch>& Switches, FName Key);
	/** Platform position at server time Now (eased travel from its last change). */
	/** 0 at the resting end, 1 at the worked end. */
	static double Alpha(const FGFDepthsSwitch* S, double Now);
	static FVector2D PositionAt(const FGFDepthsPlatform& P, const TArray<FGFDepthsSwitch>& Switches, double Now);
	static FBox2D BoxAt(const FGFDepthsPlatform& P, const TArray<FGFDepthsSwitch>& Switches, double Now);
};

/** The custom 2.5D mover (fixed step). */
struct GFDEPTHS_API FGFDepthsMover
{
	static constexpr float StepSeconds = 1.f / 60.f;

	/** One fixed step at server time Now (platforms are read at Now - Dt and Now so riders get carried). */
	static void Step(const FGFDepthsMoveParams& P, const FGFDepthsLevel& L, const TArray<FGFDepthsSwitch>& Switches, double Now, const FGFDepthsInput& In, FGFDepthsBody& B, float Dt);
	/** Knocked back away from FromX, with invulnerability. False if still invulnerable. */
	static bool Knockback(const FGFDepthsMoveParams& P, FGFDepthsBody& B, float FromX);
	static FBox2D BodyBox(const FGFDepthsMoveParams& P, const FVector2D& Pos);
};

/** A player's run in a depth. */
USTRUCT(BlueprintType)
struct GFDEPTHS_API FGFDepthsRunState
{
	GENERATED_BODY()

	static constexpr int32 MaxHP = 5;
	static constexpr int32 HealsPerSection = 3;
	static constexpr int32 HealAmount = 2;
	static constexpr float DownedSeconds = 8.f;
	static constexpr int32 ReviveHP = 2;

	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 HP = MaxHP;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Heals = HealsPerSection;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Section = 0;
	/** Last checkpoint reached (-1 = the entrance). */
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Checkpoint = INDEX_NONE;
	/** Picked up but not banked yet (not in the player's bag until a checkpoint or Lift Home). */
	UPROPERTY(BlueprintReadOnly, Category = "Depths") TArray<FGFItemStack> Unbanked;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") bool bDowned = false;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") float DownedLeft = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") int32 Deaths = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Depths") bool bReachedExit = false;
};

struct GFDEPTHS_API FGFDepthsRules
{
	/** Lowers HP; true when this hit takes them to 0. */
	static bool Damage(FGFDepthsRunState& S, int32 Amount);
	/** One of the section's heals: +2 HP (not past 5). */
	static bool Heal(FGFDepthsRunState& S);
	/** Checkpoint Index reached: the new section gets 3 fresh heals; returns what gets banked now (and clears it). */
	static bool ReachCheckpoint(FGFDepthsRunState& S, int32 Index, TArray<FGFItemStack>& OutBank);
	/** Fainted: loses floor(half) of every unbanked stack (returned), keeps the rest unbanked; full HP and heals at the checkpoint. */
	static TArray<FGFItemStack> Die(FGFDepthsRunState& S);
	/** Lift Home: everything unbanked gets banked (returned); the run ends. */
	static TArray<FGFItemStack> LiftHome(FGFDepthsRunState& S);
	/** Enemy HP for a depth and party size. */
	static int32 EnemyHP(int32 BaseHP, int32 Depth, int32 Players);
	/** Rolls a drop at Depth (Kind: Pickup, Enemy, Exit). */
	static void RollLoot(int32 Depth, FName Kind, FRandomStream& Rng, TArray<FGFItemStack>& Out);
	static const FGFDepthsRewardRow* RewardRow(int32 Depth);
};
