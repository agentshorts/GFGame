#pragma once

// Big Grumble, the giant dino event (open world). Every few days of real time, after heavy play, a giant
// placeholder dino turns up while players are outside safe territory. It herds rather than hunts: it never deals
// damage, only shoves players toward safety, it can never enter safe territory, it talks in all chat, and it leaves
// after EventSeconds. It eats exactly one player (who faints through IGFFaintHandler, never through the ledger)
// when that player attacks it, or when its target stands still outside safety near it.
// Safety is always asked through IGFSafeZoneQuery (GFSafeZone.h): any number of regions, no hard-coded circle.
//
// Everything that decides behaviour lives in FGFGiantRules / FGFGiantSim (no world, deterministic for a seed), so
// headless tests can drive it directly; UGFGiantEventComponent (on AGFGiantDirector) applies the sim to the world on the
// server, driving a reusable AGFGiantDino body.
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"
#include "GFSafeZone.h"
#include "GFGiantTypes.generated.h"

/** Chat line phases (Phase column of DT_GiantDinoLines). */
UENUM(BlueprintType)
enum class EGFGiantLinePhase : uint8
{
	Spotted,
	Chase,
	CloseCall,
	Herded,
	Circling,
	Leaving,
	Return,
	Snacked,
	Target
};

/** Row for DT_GiantDinoLines (Data/CSV/DT_GiantDinoLines.csv). Row name = line id, e.g. GD_Circling_01. */
USTRUCT(BlueprintType)
struct GFGIANT_API FGFGiantLineRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") EGFGiantLinePhase Phase = EGFGiantLinePhase::Chase;
	/** May contain {Target} (replaced with the target player's display name). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") FString Line;
};

/** Row for DT_GiantDino (Data/CSV/DT_GiantDino.csv, row "Giant"): name and look live in data so renaming is a CSV edit. */
USTRUCT(BlueprintType)
struct GFGIANT_API FGFGiantInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") FString DisplayName;
	/** DT_AssetMap key for the body (greybox placeholder until the new skin lands). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") FName AssetKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") float MeshScale = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") FName ChatChannel = TEXT("All");
};

/** All tunables. Distances in cm, times in seconds. */
USTRUCT(BlueprintType)
struct GFGIANT_API FGFGiantTuning
{
	GENERATED_BODY()

	// ---- Trigger ----
	/** Real time between events (default 2 days). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger") double MinRealSecondsBetween = 2.0 * 24.0 * 3600.0;
	/** Total player play time since the last event (summed over players; default 3 h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger") double MinPlaySecondsSinceLast = 3.0 * 3600.0;
	/** How often the server checks the trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger") float TriggerCheckSeconds = 5.f;

	// ---- Event ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event") float EventSeconds = 300.f;
	/** After the Leaving line it walks off for this long, then despawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event") float LeaveWalkSeconds = 3.f;
	/** Spawn distance beyond the first target (away from the circle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event") float SpawnDistance = 3000.f;

	// ---- Default safe region (one circle at the director's origin, growing with the player count). Only used
	// until territory is supplied through UGFGiantEventComponent::SetSafeRegions / SetSafeZone. ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float CircleBaseRadius = 1200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float CircleRadiusPerExtraPlayer = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float CircleMaxRadius = 3200.f;
	/** The giant's body radius: its centre always stays at least this far outside safe territory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float GiantRadius = 350.f;

	// ---- Chase ----
	/** Fallback player run speed when the pawn has no character movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float PlayerRunSpeed = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float StartSpeedMul = 1.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float EndSpeedMul = 0.8f;
	/** Seconds of chasing one target to ease from StartSpeedMul to EndSpeedMul. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float SlowdownSeconds = 20.f;
	/** It aims for a point this far behind the target (away from safety), so it pushes the target toward safety. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float HerdOffset = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float ShoveRange = 700.f;
	/** Launch speed of a shove (toward the nearest safety). Never any damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float ShoveSpeed = 900.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float ShoveUpSpeed = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float ShoveCooldown = 1.5f;
	/** Walking speed along the safe border once everyone is inside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float CircleWalkSpeed = 400.f;
	/** Play times within this many seconds count as equally new (then most recent first join, then farthest out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float NewestTieSeconds = 60.f;

	// ---- Standing still outside the circle ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle") bool bEatIdleTarget = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle") float IdleSpeedThreshold = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle") float IdleWarnSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle") float IdleEatSeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Idle") float IdleRange = 1500.f;

	// ---- Attacks ----
	/** "Hit" requests farther than this from the giant are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack") float AttackRange = 2500.f;

	// ---- Eaten ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eaten") float BellyCamSeconds = 3.f;
	/** Respawn this far inside the nearest safe border. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eaten") float RespawnInset = 150.f;

	// ---- Chat ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float ChaseChatMinSeconds = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float ChaseChatMaxSeconds = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float CirclingChatMinSeconds = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float CirclingChatMaxSeconds = 9.f;
	/** Share of chase lines that are Target lines (the rest are Chase lines). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float TargetLineWeight = 0.7f;
	/** A line isn't reused while it's among the last N lines said. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") int32 NoRepeatCount = 4;
	/** A CloseCall line (after a shove) needs at least this gap since the last line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chat") float CloseCallMinGap = 2.f;
	/** Pacing amplitude (cm, along the border) while edge-walking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase") float EdgePaceDistance = 400.f;
};

/** Tunables as a data asset (create one in the editor to tweak; the director uses defaults if none is set). */
UCLASS(BlueprintType)
class GFGIANT_API UGFGiantTunables : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Giant", meta = (ShowOnlyInnerProperties))
	FGFGiantTuning Tuning;
};

/** Saved with the world (mode blob "Giant"): trigger clock and who is new. Parallel arrays (blob-friendly). */
USTRUCT()
struct GFGIANT_API FGFGiantSave
{
	GENERATED_BODY()

	/** UTC ticks of the last event (or of the first time this world ran the director). 0 = not started. */
	UPROPERTY(SaveGame) int64 LastEventUtcTicks = 0;
	/** Player-seconds played since the last event (two players for an hour = 7200). */
	UPROPERTY(SaveGame) double PlaySecondsSinceLast = 0.0;
	UPROPERTY(SaveGame) int32 EventCount = 0;
	UPROPERTY(SaveGame) TArray<FString> PlayerKeys;
	UPROPERTY(SaveGame) TArray<double> PlayerSeconds;
	UPROPERTY(SaveGame) TArray<int64> FirstSeenUtcTicks;

	int32 IndexOf(const FString& Key) const { return PlayerKeys.IndexOfByKey(Key); }
	/** Adds the player if new (first join = NowTicks). Returns the index. */
	int32 Touch(const FString& Key, int64 NowTicks);
	double SecondsOf(const FString& Key) const;
	int64 FirstSeenOf(const FString& Key) const;
};

/** Server phase of a running giant (replicated for UI and tests). */
UENUM(BlueprintType)
enum class EGFGiantState : uint8
{
	None,
	Chase,
	Circling,
	Leaving,
	Gone
};

/** One line said in chat. */
USTRUCT(BlueprintType)
struct GFGIANT_API FGFGiantChatLine
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Giant") int32 Seq = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Giant") FName LineId;
	UPROPERTY(BlueprintReadOnly, Category = "Giant") EGFGiantLinePhase Phase = EGFGiantLinePhase::Chase;
	/** Final text ({Target} filled in). */
	UPROPERTY(BlueprintReadOnly, Category = "Giant") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "Giant") FString Speaker;
	UPROPERTY(BlueprintReadOnly, Category = "Giant") FName Channel;
	/** Player the line is aimed at (empty = everyone). */
	UPROPERTY(BlueprintReadOnly, Category = "Giant") FString TargetKey;
	/** Event time (seconds since the giant arrived). */
	UPROPERTY(BlueprintReadOnly, Category = "Giant") float At = 0.f;
};

/** What the sim sees of one player each step. */
struct GFGIANT_API FGFGiantPlayerView
{
	FString Key;
	FString Name;
	FVector Location = FVector::ZeroVector;
	double PlaySeconds = 0.0;
	int64 FirstSeenTicks = 0;
	/** In a menu or cutscene: never eaten for standing still. */
	bool bBusy = false;
	/** Run speed (cm/s) of this player's pawn; <= 0 uses the tuning fallback. */
	float RunSpeed = 0.f;
};

struct FGFGiantShove
{
	FString Key;
	/** Launch velocity (toward safety). A shove is the only thing the giant ever does to a player. */
	FVector Velocity = FVector::ZeroVector;
};

/** What happened in one sim step (the director applies it). */
struct GFGIANT_API FGFGiantStepOut
{
	TArray<FGFGiantChatLine> Chat;
	TArray<FGFGiantShove> Shoves;
	/** Key of the player eaten this step (empty = none). */
	FString Eaten;
	/** The event is over this step (eaten, or walked off). */
	bool bEnded = false;
};

/** A phase's pool of lines. */
struct GFGIANT_API FGFGiantLines
{
	struct FLine { FName Id; FString Text; };
	TMap<EGFGiantLinePhase, TArray<FLine>> ByPhase;

	/** From DT_GiantDinoLines (CSV fallback). */
	static FGFGiantLines Load();
	static FGFGiantLines FromTable(const class UDataTable* Table);
	int32 Num(EGFGiantLinePhase Phase) const { const TArray<FLine>* L = ByPhase.Find(Phase); return L ? L->Num() : 0; }
	int32 Total() const;
};

/** Pure rules (no world). */
struct GFGIANT_API FGFGiantRules
{
	/** Placeholder token for the target's name in a line. */
	static const TCHAR* TargetToken;

	static constexpr double TicksPerSecond = 10000000.0;

	/** Accumulates play time: NumPlayers players played Dt seconds. */
	static void AccumulatePlay(FGFGiantSave& Save, double Dt, int32 NumPlayers);
	/** Only called with the server's UTC "now"; first call starts the real-time clock. */
	static void StartClockIfNeeded(FGFGiantSave& Save, int64 NowTicks);
	/** Enough real time and play since the last event, and somebody is outside safety. */
	static bool ShouldTrigger(const FGFGiantTuning& T, const FGFGiantSave& Save, int64 NowTicks, bool bAnyoneOutside);
	/** Records an event start (resets the play accumulator). Returns true if this is a return visit. */
	static bool MarkEventStarted(FGFGiantSave& Save, int64 NowTicks);

	/** The default region (tunables) at Center for Members players. */
	static FGFSafeRegion DefaultRegion(const FGFGiantTuning& T, const FVector& Center, int32 Members);
	static bool AnyoneOutside(const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players);
	/** Moves P (horizontally) out until its body (GiantRadius) is clear of safe territory. */
	static FVector ClampOutside(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P);

	/** Newest outside player: lowest play time (within NewestTieSeconds), then latest first join, then farthest from safety. INDEX_NONE if nobody is outside. */
	static int32 PickTarget(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players);
	/** Where it goes to herd Target: behind it, on the side away from the nearest safety. */
	static FVector HerdGoal(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& Target);
	/** Shove launch velocity for a player at P: horizontally toward the nearest safety. */
	static FVector ShoveVelocity(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P);
	/** Chase speed after chasing one target for TimeOnTarget seconds. */
	static float ChaseSpeed(const FGFGiantTuning& T, float RunSpeed, float TimeOnTarget);
	/** Replaces {Target} (any case) with Name; with no name the token and its lead-in (", " / " ") are dropped. */
	static FString FillTarget(const FString& Line, const FString& Name);
	/** Where an eaten player respawns: just inside the safe border nearest to where they were. */
	static FVector RespawnPoint(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& From);
	/**
	 * Picks a line from Phase, avoiding the last NoRepeatCount ids in Recent when the pool allows. Returns false if
	 * the pool is empty.
	 */
	static bool PickLine(const FGFGiantTuning& T, const FGFGiantLines& Lines, EGFGiantLinePhase Phase, const TArray<FName>& Recent, FRandomStream& Rng, FName& OutId, FString& OutText);
};

/**
 * One giant event, deterministic for a seed. Drive with Begin, then Step every frame (Zone must stay valid) (players as seen this frame),
 * Attacked when a player hits it. Read State/Location; apply the step output.
 */
struct GFGIANT_API FGFGiantSim
{
	FGFGiantTuning T;
	FGFGiantLines Lines;
	FString Speaker = TEXT("Giant");
	FName Channel = TEXT("All");
	FRandomStream Rng;

	/** Safe territory (not owned; the caller keeps it alive and may swap it between steps). */
	const IGFSafeZoneQuery* Zone = nullptr;
	FVector Location = FVector::ZeroVector;
	EGFGiantState State = EGFGiantState::None;
	float Elapsed = 0.f;
	bool bReturnVisit = false;

	FString TargetKey;
	float TimeOnTarget = 0.f;
	float ChatIn = 0.f;
	float SinceLastLine = 1000.f;
	int32 NextSeq = 1;
	TArray<FName> RecentLines;
	TMap<FString, float> ShoveCooldowns;

	/** Standing-still tracking (target only). */
	TMap<FString, FVector> LastPositions;
	float IdleSeconds = 0.f;
	bool bIdleWarned = false;

	FString EatenKey;
	float LeaveLeft = 0.f;
	/** Last chase speed (tests). */
	float LastSpeed = 0.f;

	void Begin(const FGFGiantTuning& InT, const FGFGiantLines& InLines, const IGFSafeZoneQuery& InZone, const TArray<FGFGiantPlayerView>& Players, bool bInReturnVisit, int32 Seed, FGFGiantStepOut& Out);
	void Step(float Dt, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);
	/** A player attacked it: that player is eaten and it leaves at once (only ever one). Returns true if eaten. */
	bool Attacked(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);

	bool IsActive() const { return State == EGFGiantState::Chase || State == EGFGiantState::Circling || State == EGFGiantState::Leaving; }

private:
	void Say(EGFGiantLinePhase Phase, const FString& TargetName, const FString& AimedAt, FGFGiantStepOut& Out);
	void ChaseChat(const FGFGiantPlayerView* Target, FGFGiantStepOut& Out);
	bool Eat(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);
	void StartLeaving(FGFGiantStepOut& Out);
	const FGFGiantPlayerView* Find(const TArray<FGFGiantPlayerView>& Players, const FString& Key) const;
};
