#pragma once

// Elder Book. Pure rules (no world) plus the server owner that replicates them.
// Channels: Dinobook (shared), RimeBook, VerdantBook, CinderBook.
// A town feed is that town's book plus Dinobook. Feed(Elder) is all four.
// The ring keeps the last PostsPerChannel posts per channel and drops the earliest one first.
// An id already sitting in any channel is refused. An id the cap has dropped can be used again.
// UGFElderBookSubsystem serves the book on the server and mirrors AGFElderBookRelay on clients.

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFElderBook.generated.h"

class UWorld;

UENUM()
enum class EGFElderChannel : uint8
{
	Dinobook = 0,
	RimeBook = 1,
	VerdantBook = 2,
	CinderBook = 3
};

/** Quest kinds for this line, plus the villain and arena lines that already exist. */
UENUM()
enum class EGFElderKind : uint8
{
	None,
	Theft,
	Hire,
	Joined,
	RankUp,
	ShowWin,
	Villain,
	Brag,
	RentalQuit,
	ElderNice,
	/** Side quest board (QL-a stub): a player took on a town task. Fixed generic line, town channel. */
	SideAccept,
	/** Side quest board (QL-a stub): a player finished a town task. Fixed generic line, town channel. */
	SideComplete
};

/** Who owns the replicated book. Change it only through FGFElderBook::Transition. */
UENUM()
enum class EGFElderBookState : uint8
{
	Idle,
	Serving,
	Mirroring
};

UENUM()
enum class EGFElderBookEvent : uint8
{
	BeganAuthority,
	BeganMirror,
	LostRelay
};

USTRUCT()
struct GFSOCIAL_API FGFElderPost
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	EGFElderChannel Channel = EGFElderChannel::Dinobook;

	UPROPERTY()
	EGFElderKind Kind = EGFElderKind::None;

	UPROPERTY()
	FString Line;

	UPROPERTY()
	FString SubjectKey;

	UPROPERTY()
	FName FactionId;

	UPROPERTY()
	double Time = 0.0;
};

/** Replicates the latest posts. The server writes them. Clients copy them into the local book. */
UCLASS()
class GFSOCIAL_API AGFElderBookRelay : public AActor
{
	GENERATED_BODY()

public:
	AGFElderBookRelay();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetPosts(const TArray<FGFElderPost>& In);
	const TArray<FGFElderPost>& GetPosts() const { return Posts; }

private:
	UFUNCTION()
	void OnRep_Posts();

	void NotifyOwner();

	UPROPERTY(VisibleAnywhere, Category = "GF|ElderBook")
	TObjectPtr<USceneComponent> Holder;

	UPROPERTY(ReplicatedUsing = OnRep_Posts)
	TArray<FGFElderPost> Posts;
};

/**
 * The book itself. Tests drive Add, Feed and Transition with no world.
 * Post(UWorld*, ...) is the entry other pieces call. It lands on the server subsystem.
 */
struct GFSOCIAL_API FGFElderBook
{
	static constexpr int32 PostsPerChannel = 50;
	static constexpr int32 ChannelCount = 4;

	/** Elder view: every channel. A town id is not this. */
	static FName ElderViewId();

	/** Rime, Verdant, Cinder, Cinderwright or Cinderwrights. Anything else has no town book. */
	static EGFElderChannel TownChannel(FName FactionId);

	/** Fixed line for Theft, Hire, Joined, RankUp, ShowWin, SideAccept and SideComplete. Empty for every other kind. */
	static FString LineFor(EGFElderKind Kind, const FString& DisplayName);

	static bool IsQuestKind(EGFElderKind Kind);

	/** Arena post kinds Brag, RentalQuit and ElderNice. Anything else is None. */
	static EGFElderKind ArenaKind(FName Kind);

	static EGFElderBookState Transition(EGFElderBookState From, EGFElderBookEvent Event);

	static FGFElderPost Make(EGFElderChannel Channel, EGFElderKind Kind, const FString& DisplayName, const FString& SubjectKey, FName FactionId, double Time, int32 Id = 0);
	/** Dinobook channel. The line is FGFVillainRules::DinobookLine. */
	static FGFElderPost MakeVillain(int32 WinStreak, const FString& SubjectKey, FName FactionId, double Time, int32 Id = 0);
	/** Dinobook channel. The line is the arena post's own line. */
	static FGFElderPost MakeArena(FName ArenaKindName, const FString& Line, const FString& SubjectKey, FName FactionId, double Time, int32 Id = 0);

	/**
	 * Server entry. Fills the quest line from Kind and DisplayName.
	 * Refuses a client world, a missing subsystem, a duplicate id, and kinds that are not quest kinds.
	 */
	static bool Post(UWorld* World, EGFElderChannel Channel, EGFElderKind Kind, const FString& DisplayName, const FString& SubjectKey, FName FactionId, double Time, int32 Id = 0);
	/** Server entry for a post that already has its line (villain and arena adapters). */
	static bool Post(UWorld* World, const FGFElderPost& Prepared);

	void Reset();
	/** False when the id is already stored, the kind is None, or the line is empty. Drops the earliest post in that channel when the ring is full. */
	bool Add(const FGFElderPost& In);
	/** Rebuild from a relay snapshot. Same rules as Add, so a bad snapshot still cannot pass the cap. */
	void Replace(const TArray<FGFElderPost>& In);

	bool Contains(int32 Id) const;
	const TArray<FGFElderPost>& ChannelPosts(EGFElderChannel Channel) const;
	/** Oldest first inside each channel, Dinobook then the town books. This is what the relay sends. */
	TArray<FGFElderPost> Snapshot() const;
	/**
	 * ElderViewId: all four channels, earliest time first.
	 * A town id: that town's book plus Dinobook. Another town's book is left out.
	 * Any other id: Dinobook only.
	 */
	TArray<FGFElderPost> Feed(FName FactionId) const;

private:
	static int32 LaneIndex(EGFElderChannel Channel);

	TArray<FGFElderPost> Lanes[ChannelCount];
	int32 NextId = 1;
};

UCLASS()
class GFSOCIAL_API UGFElderBookSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	static UGFElderBookSubsystem* Get(const UWorld* World);

	bool Push(const FGFElderPost& Post);
	const FGFElderBook& GetBook() const { return Book; }
	EGFElderBookState GetState() const { return State; }

	void NoteRelay(AGFElderBookRelay* InRelay);
	void ClearRelay(AGFElderBookRelay* Which);

private:
	void Apply(EGFElderBookEvent Event);
	void Publish();

	FGFElderBook Book;
	EGFElderBookState State = EGFElderBookState::Idle;
	TWeakObjectPtr<AGFElderBookRelay> Relay;
};
