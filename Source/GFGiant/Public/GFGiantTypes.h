#pragma once

// Big Grumble, the giant dino event (open world). Every few days of real time, after heavy play, a giant
// placeholder dino turns up while players are outside safe territory. It herds rather than hunts: it never deals
// damage, only shoves players toward safety, it talks in all chat, and it leaves after EventSeconds. It eats exactly
// one player (who faints through IGFFaintHandler, never through the ledger) when that player attacks it, or when its
// target stands still near it.
// Safety is always asked through IGFSafeZoneQuery (GFSafeZone.h): any number of regions, no hard-coded circle.
// Who it may take is asked per player (IGFSafeZoneQuery::IsSafeFor, i.e. protection): a player is "exposed" when
// that says unsafe. A jailed player is not protected, so they can be a target. A no-entry box (FGFNoEntry, e.g. a
// jail cell) still blocks the body and the bite: he paths to the edge, waits JailDwellSeconds, then picks someone
// else, and he never shoves or eats through the box. Movement: it skirts safe territory, except that it walks in
// after an exposed target standing there (an unprotected visitor on someone's land); it never enters a no-entry box.
// Handoff: an exposed player someone else just unprotected, or is holding with a grab, is taken ahead of anyone
// else. He thanks that player once (Handoff lines; {Dragger} and {Target}). A taunt (NoticeTaunt) pulls him toward
// that spot so he notices exposed players there. He still never takes a protected player or enters a no-entry box.
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
	Target,
	/** Someone else set this player up (protection just off, or a grab). */
	Handoff
};

/** Row for DT_GiantDinoLines (Data/CSV/DT_GiantDinoLines.csv). Row name = line id, e.g. GD_Circling_01. */
USTRUCT(BlueprintType)
struct GFGIANT_API FGFGiantLineRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") EGFGiantLinePhase Phase = EGFGiantLinePhase::Chase;
	/** May contain {Target} (the target's display name) and {Dragger} (who set them up). */
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
	/** Uniform scale of the greybox placeholder body. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") float MeshScale = 3.5f;
	/**
	 * Uniform scale of the imported art mesh (SK_Dino_BigGrumble, authored at T. rex size on SKEL_Dino_Rex).
	 * 4x gives about 38 m long (4x the T. rex); it stays a CSV value so the size can be tuned without code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") float ArtMeshScale = 4.f;
	/** Prowl_Circle ground speed at 1x (cm/s), measured from the planted feet. Scaled by ArtMeshScale on the art mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") float ProwlSpeed = 254.f;
	/** Waddle_Off ground speed at 1x (cm/s), measured from the planted feet. Scaled by ArtMeshScale on the art mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") float WaddleSpeed = 143.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Giant") FName ChatChannel = TEXT("All");

	/** Scale for the body actually shown: ArtMeshScale once the art mesh is wired in, else the greybox MeshScale. */
	float BodyScale(bool bArtMesh) const { return bArtMesh ? ArtMeshScale : MeshScale; }
	/** Clip speeds for the body shown: x ArtMeshScale on the art mesh, the 1x values otherwise. */
	float ProwlSpeedFor(bool bArtMesh) const { return bArtMesh ? ProwlSpeed * ArtMeshScale : ProwlSpeed; }
	float WaddleSpeedFor(bool bArtMesh) const { return bArtMesh ? WaddleSpeed * ArtMeshScale : WaddleSpeed; }
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
	/** The giant's body radius: while skirting safe territory its centre stays at least this far outside (also its no-entry margin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float GiantRadius = 350.f;
	/** Half height (cm) of its body around its location: no-entry boxes overlapping this height block it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circle") float BodyHalfHeight = 600.f;

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

	// ---- Handoff and taunt ----
	/** A protection revoke counts as a handoff for this long. A live grab counts the whole time it holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handoff") float HandoffWindowSeconds = 30.f;
	/** How long a taunt keeps his attention on that spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handoff") float TauntInterestSeconds = 15.f;
	/** Radius (cm) used when a taunt doesn't carry one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handoff") float TauntNoticeRadius = 4500.f;
	/**
	 * Once he has reached the edge of a no-entry box and the target is still inside, he waits this long, then
	 * leaves that player for someone else. While they stay in the box he does not come back this visit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Handoff") float JailDwellSeconds = 8.f;
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
	/** Who last turned this player's faction protection off (AGFPlayerState::LastDrag); fills {Dragger}. */
	FString Dragger;
	/** Key of that player. A revoke by this player themselves does not count as a handoff. */
	FString DraggerKey;
	int64 DraggedAtUtcTicks = 0;
	/**
	 * Seconds since the revoke. Negative means there is no revoke: a raw LastDrag tick is not enough, the event
	 * fills this from the clock. 0 .. HandoffWindowSeconds counts as "just".
	 */
	float DragAgeSeconds = -1.f;
	/** Someone else is grabbing them right now (the tether is still up). */
	bool bHeld = false;
	FString HolderKey;
	/** Display name of the holder. Fills {Dragger} when the revoke record has no name. */
	FString HolderName;
	/** Group (faction) id and host flag, for the per-player safety question (IGFSafeZoneQuery::IsSafeFor). */
	FName GroupId;
	bool bHost = false;

	FGFSafeSubject Subject() const { return FGFSafeSubject(Key, GroupId, bHost); }
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

/** Why PickInterest chose (or didn't choose) a player. */
enum class EGFGiantInterest : uint8
{
	None,
	/** Taunt is up and nobody exposed is in its radius: walk to the spot. */
	Look,
	Normal,
	Taunt,
	Handoff
};

/** An active taunt the sim should prefer, when one is up. */
struct GFGIANT_API FGFGiantNotice
{
	bool bActive = false;
	FVector At = FVector::ZeroVector;
	float Radius = 0.f;
};

/** Result of FGFGiantRules::PickInterest. */
struct GFGIANT_API FGFGiantPick
{
	int32 Index = INDEX_NONE;
	EGFGiantInterest Reason = EGFGiantInterest::None;
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
	/**
	 * The giant may target P: not safe for P where they stand (Zone.IsSafeFor, so protection counts). A jailed player
	 * is not protected, so the no-entry box does not hide them. Shoves and bites use CanTouch.
	 */
	static bool IsExposed(const IGFSafeZoneQuery& Zone, const TArray<FBox>& NoEntry, const FGFGiantPlayerView& P);
	/** Exposed, and not standing inside a no-entry box. The only players he shoves or eats. */
	static bool CanTouch(const IGFSafeZoneQuery& Zone, const TArray<FBox>& NoEntry, const FGFGiantPlayerView& P);
	/**
	 * Where he stands to reach a target inside a no-entry box: the nearest point on that box, GiantRadius outside it.
	 * Target's own location when they are not in a box.
	 */
	static FVector NoEntryApproach(const FGFGiantTuning& T, const TArray<FBox>& NoEntry, const FVector& From, const FVector& Target);
	/** Anyone exposed (the trigger's "someone is out there", including a player inside a no-entry box). */
	static bool AnyoneOutside(const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry = TArray<FBox>());
	/** Moves P (horizontally) out until its body (GiantRadius) is clear of safe territory. */
	static FVector ClampOutside(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const FVector& P);

	/** Newest exposed player: lowest play time (within NewestTieSeconds), then latest first join, then farthest from safety. INDEX_NONE if nobody is exposed. */
	static int32 PickTarget(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry = TArray<FBox>());
	/**
	 * A live grab by someone else, or a revoke by someone else within HandoffWindowSeconds. Exposure is the caller's job.
	 */
	static bool IsHandoffCandidate(const FGFGiantTuning& T, const FGFGiantPlayerView& P);
	/** Name for {Dragger} on a handoff line: the revoke name, else the holder's name. */
	static FString SetupName(const FGFGiantPlayerView& P);
	/**
	 * Who he goes for. Handoff candidates first (a hold beats a revoke, a newer revoke beats an older one, then the
	 * usual newest-exposed tiebreak), then exposed players inside an active taunt, then PickTarget. A taunt with
	 * nobody exposed inside it returns Look and no index: walk to the spot, don't peel off to someone farther away.
	 * Skip names players he already gave up on at a no-entry box; they stay skipped while they remain inside one.
	 */
	static FGFGiantPick PickInterest(const FGFGiantTuning& T, const IGFSafeZoneQuery& Zone, const TArray<FGFGiantPlayerView>& Players, const TArray<FBox>& NoEntry = TArray<FBox>(), const FGFGiantNotice* Notice = nullptr, const TSet<FString>* Skip = nullptr);
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
 * The zone the giant decides with in a world: shapes (inside, edges, closest points) from Base (its own regions or a
 * custom zone), but per-player safety and owners from the land owner first (UGFSafeZoneSubsystem::IsSafeFor /
 * OwnerAt, so territory protection counts), falling back to Base on unowned land.
 */
struct GFGIANT_API FGFGiantLandZone : public IGFSafeZoneQuery
{
	const IGFSafeZoneQuery* Base = nullptr;
	TWeakObjectPtr<const UGFSafeZoneSubsystem> Lands;

	virtual bool IsInsideSafe(const FVector& L) const override { return Base && Base->IsInsideSafe(L); }
	virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& L) const override;
	virtual FName OwnerAt(const FVector& L) const override;
	virtual FVector ClosestSafePoint(const FVector& L) const override { return Base ? Base->ClosestSafePoint(L) : L; }
	virtual bool ClosestEdge(const FVector& L, FVector& OutPoint, FVector& OutOutward) const override { return Base && Base->ClosestEdge(L, OutPoint, OutOutward); }
	virtual bool HasAnySafe() const override { return Base && Base->HasAnySafe(); }
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
	/** No-entry boxes (FGFNoEntry; e.g. jail cells): it never walks into one. Players inside can be targets; he cannot shove or eat them. Caller-set. */
	TArray<FBox> NoEntry;
	FVector Location = FVector::ZeroVector;
	EGFGiantState State = EGFGiantState::None;
	float Elapsed = 0.f;
	bool bReturnVisit = false;

	FString TargetKey;
	float TimeOnTarget = 0.f;
	/** A player taunt is pulling him toward TauntAt until TauntLeft runs out. */
	bool bTaunt = false;
	FVector TauntAt = FVector::ZeroVector;
	float TauntRadius = 0.f;
	float TauntLeft = 0.f;

	/** Server or tests: pull him toward At for TauntInterestSeconds. Ignored unless he is chasing or circling. */
	void NoticeTaunt(const FVector& At, float Radius);
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
	/** Targets already thanked this visit (one Handoff line each). */
	TSet<FString> Thanked;
	/** Target he is waiting on at a no-entry edge, and how long he has been standing there. */
	FString BlockedKey;
	float BlockedSeconds = 0.f;
	/** Players he gave up on at a box this visit. Dropped when they leave the box. */
	TSet<FString> Passed;
	/** Exposed on the previous step. A player who leaves this set by becoming safe was herded. */
	TSet<FString> WasExposed;
	bool bHerdedSomeone = false;
	/** Last chase speed (tests). */
	float LastSpeed = 0.f;
	/** It walked into safe territory after a target and is still in (it walks back out instead of jumping). */
	bool bWalkingOut = false;

	void Begin(const FGFGiantTuning& InT, const FGFGiantLines& InLines, const IGFSafeZoneQuery& InZone, const TArray<FGFGiantPlayerView>& Players, bool bInReturnVisit, int32 Seed, FGFGiantStepOut& Out);
	void Step(float Dt, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);
	/** A player attacked it: an exposed attacker is eaten and it leaves at once (only ever one); it never eats a protected or unreachable one. Returns true if eaten. */
	bool Attacked(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);

	bool IsActive() const { return State == EGFGiantState::Chase || State == EGFGiantState::Circling || State == EGFGiantState::Leaving; }

private:
	/** DraggerName fills {Dragger} (FGFLineTokens); empty drops the token like an empty {Target}. False if the pool is empty. */
	bool Say(EGFGiantLinePhase Phase, const FString& TargetName, const FString& AimedAt, FGFGiantStepOut& Out, const FString& DraggerName = FString());
	/** One Handoff line for this target this visit. False if already thanked or the pool is empty. */
	bool Thank(const FGFGiantPlayerView& Target, FGFGiantStepOut& Out);
	void ChaseChat(const FGFGiantPlayerView* Target, FGFGiantStepOut& Out);
	bool Eat(const FString& Key, const TArray<FGFGiantPlayerView>& Players, FGFGiantStepOut& Out);
	void StartLeaving(FGFGiantStepOut& Out);
	/**
	 * One step of StepLen toward Goal. bMayEnterSafe: straight line, safe territory or not. Otherwise it skirts safe
	 * territory (StepOutside); if it chased someone in, it first walks out the shortest way instead of jumping. Never into a no-entry box.
	 */
	void MoveToward(const FVector& Goal, float StepLen, bool bMayEnterSafe);
	const FGFGiantPlayerView* Find(const TArray<FGFGiantPlayerView>& Players, const FString& Key) const;
	/** Sets bHerdedSomeone when a player who was exposed is now safe. */
	void NoteHomecomings(const TArray<FGFGiantPlayerView>& Players);
};
