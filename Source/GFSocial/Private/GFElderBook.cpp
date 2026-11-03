#include "GFElderBook.h"
#include "GFLog.h"
#include "GFVillainRules.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	bool SameName(FName Id, const TCHAR* Text)
	{
		return Id == FName(Text);
	}

	const TCHAR* ChannelLabel(EGFElderChannel Channel)
	{
		switch (Channel)
		{
		case EGFElderChannel::Dinobook: return TEXT("Dinobook");
		case EGFElderChannel::RimeBook: return TEXT("RimeBook");
		case EGFElderChannel::VerdantBook: return TEXT("VerdantBook");
		case EGFElderChannel::CinderBook: return TEXT("CinderBook");
		default: return TEXT("None");
		}
	}

	bool SamePost(const FGFElderPost& A, const FGFElderPost& B)
	{
		return A.Id == B.Id
			&& A.Channel == B.Channel
			&& A.Kind == B.Kind
			&& A.Line == B.Line
			&& A.SubjectKey == B.SubjectKey
			&& A.FactionId == B.FactionId
			&& A.Time == B.Time;
	}
}

AGFElderBookRelay::AGFElderBookRelay()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	Holder = CreateDefaultSubobject<USceneComponent>(TEXT("Holder"));
	RootComponent = Holder;
}

void AGFElderBookRelay::BeginPlay()
{
	Super::BeginPlay();
	NotifyOwner();
}

void AGFElderBookRelay::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UGFElderBookSubsystem* Sub = World->GetSubsystem<UGFElderBookSubsystem>())
		{
			Sub->ClearRelay(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AGFElderBookRelay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFElderBookRelay, Posts);
}

void AGFElderBookRelay::SetPosts(const TArray<FGFElderPost>& In)
{
	if (Posts.Num() == In.Num())
	{
		bool bSame = true;
		for (int32 Index = 0; Index < Posts.Num(); ++Index)
		{
			if (!SamePost(Posts[Index], In[Index]))
			{
				bSame = false;
				break;
			}
		}
		if (bSame)
		{
			return;
		}
	}
	Posts = In;
	ForceNetUpdate();
}

void AGFElderBookRelay::OnRep_Posts()
{
	NotifyOwner();
}

void AGFElderBookRelay::NotifyOwner()
{
	if (UWorld* World = GetWorld())
	{
		if (UGFElderBookSubsystem* Sub = World->GetSubsystem<UGFElderBookSubsystem>())
		{
			Sub->NoteRelay(this);
		}
	}
}

FName FGFElderBook::ElderViewId()
{
	static const FName Id(TEXT("Elder"));
	return Id;
}

EGFElderChannel FGFElderBook::TownChannel(FName FactionId)
{
	if (SameName(FactionId, TEXT("Rime")))
	{
		return EGFElderChannel::RimeBook;
	}
	if (SameName(FactionId, TEXT("Verdant")))
	{
		return EGFElderChannel::VerdantBook;
	}
	if (SameName(FactionId, TEXT("Cinder")) || SameName(FactionId, TEXT("Cinderwrights")))
	{
		return EGFElderChannel::CinderBook;
	}
	return EGFElderChannel::Dinobook;
}

bool FGFElderBook::IsQuestKind(EGFElderKind Kind)
{
	switch (Kind)
	{
	case EGFElderKind::Theft:
	case EGFElderKind::Hire:
	case EGFElderKind::Joined:
	case EGFElderKind::RankUp:
	case EGFElderKind::ShowWin:
		return true;
	default:
		return false;
	}
}

FString FGFElderBook::LineFor(EGFElderKind Kind, const FString& DisplayName)
{
	const FString Name = DisplayName.IsEmpty() ? FString(TEXT("Someone")) : DisplayName;
	switch (Kind)
	{
	case EGFElderKind::Theft:
		return FString::Printf(TEXT("%s took a town crystal."), *Name);
	case EGFElderKind::Hire:
		return FString::Printf(TEXT("The elder hired dinos against %s."), *Name);
	case EGFElderKind::Joined:
		return FString::Printf(TEXT("%s joined a faction."), *Name);
	case EGFElderKind::RankUp:
		return FString::Printf(TEXT("%s rose in rank."), *Name);
	case EGFElderKind::ShowWin:
		return FString::Printf(TEXT("%s won the show and leads the faction."), *Name);
	default:
		return FString();
	}
}

EGFElderKind FGFElderBook::ArenaKind(FName Kind)
{
	if (SameName(Kind, TEXT("Brag")))
	{
		return EGFElderKind::Brag;
	}
	if (SameName(Kind, TEXT("RentalQuit")))
	{
		return EGFElderKind::RentalQuit;
	}
	if (SameName(Kind, TEXT("ElderNice")))
	{
		return EGFElderKind::ElderNice;
	}
	return EGFElderKind::None;
}

EGFElderBookState FGFElderBook::Transition(EGFElderBookState From, EGFElderBookEvent Event)
{
	switch (Event)
	{
	case EGFElderBookEvent::BeganAuthority:
		return EGFElderBookState::Serving;
	case EGFElderBookEvent::BeganMirror:
		// The server's own relay must not knock it out of Serving.
		return From == EGFElderBookState::Serving ? EGFElderBookState::Serving : EGFElderBookState::Mirroring;
	case EGFElderBookEvent::LostRelay:
		return From == EGFElderBookState::Serving ? EGFElderBookState::Serving : EGFElderBookState::Idle;
	default:
		return From;
	}
}

FGFElderPost FGFElderBook::Make(EGFElderChannel Channel, EGFElderKind Kind, const FString& DisplayName, const FString& SubjectKey, FName FactionId, double Time, int32 Id)
{
	FGFElderPost Post;
	Post.Id = Id;
	Post.Channel = Channel;
	Post.Kind = Kind;
	Post.Line = LineFor(Kind, DisplayName);
	Post.SubjectKey = SubjectKey;
	Post.FactionId = FactionId;
	Post.Time = Time;
	return Post;
}

FGFElderPost FGFElderBook::MakeVillain(int32 WinStreak, const FString& SubjectKey, FName FactionId, double Time, int32 Id)
{
	FGFElderPost Post;
	Post.Id = Id;
	Post.Channel = EGFElderChannel::Dinobook;
	Post.Kind = EGFElderKind::Villain;
	Post.Line = FGFVillainRules::DinobookLine(WinStreak);
	Post.SubjectKey = SubjectKey;
	Post.FactionId = FactionId;
	Post.Time = Time;
	return Post;
}

FGFElderPost FGFElderBook::MakeArena(FName ArenaKindName, const FString& Line, const FString& SubjectKey, FName FactionId, double Time, int32 Id)
{
	FGFElderPost Post;
	Post.Id = Id;
	Post.Channel = EGFElderChannel::Dinobook;
	Post.Kind = ArenaKind(ArenaKindName);
	Post.Line = Line;
	Post.SubjectKey = SubjectKey;
	Post.FactionId = FactionId;
	Post.Time = Time;
	return Post;
}

bool FGFElderBook::Post(UWorld* World, EGFElderChannel Channel, EGFElderKind Kind, const FString& DisplayName, const FString& SubjectKey, FName FactionId, double Time, int32 Id)
{
	return Post(World, Make(Channel, Kind, DisplayName, SubjectKey, FactionId, Time, Id));
}

bool FGFElderBook::Post(UWorld* World, const FGFElderPost& Prepared)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	UGFElderBookSubsystem* Sub = World->GetSubsystem<UGFElderBookSubsystem>();
	return Sub && Sub->Push(Prepared);
}

void FGFElderBook::Reset()
{
	for (TArray<FGFElderPost>& Lane : Lanes)
	{
		Lane.Reset();
	}
	NextId = 1;
}

int32 FGFElderBook::LaneIndex(EGFElderChannel Channel)
{
	const int32 Index = static_cast<int32>(Channel);
	return Index >= 0 && Index < ChannelCount ? Index : 0;
}

bool FGFElderBook::Contains(int32 Id) const
{
	if (Id == 0)
	{
		return false;
	}
	for (const TArray<FGFElderPost>& Lane : Lanes)
	{
		for (const FGFElderPost& Post : Lane)
		{
			if (Post.Id == Id)
			{
				return true;
			}
		}
	}
	return false;
}

bool FGFElderBook::Add(const FGFElderPost& In)
{
	if (In.Kind == EGFElderKind::None || In.Line.IsEmpty())
	{
		return false;
	}
	if (In.Id != 0 && Contains(In.Id))
	{
		return false;
	}
	FGFElderPost Post = In;
	if (Post.Id == 0)
	{
		Post.Id = NextId++;
	}
	else
	{
		NextId = FMath::Max(NextId, Post.Id + 1);
	}
	TArray<FGFElderPost>& Lane = Lanes[LaneIndex(Post.Channel)];
	if (Lane.Num() >= PostsPerChannel)
	{
		Lane.RemoveAt(0, 1, EAllowShrinking::No);
	}
	Lane.Add(MoveTemp(Post));
	return true;
}

void FGFElderBook::Replace(const TArray<FGFElderPost>& In)
{
	Reset();
	for (const FGFElderPost& Post : In)
	{
		Add(Post);
	}
}

const TArray<FGFElderPost>& FGFElderBook::ChannelPosts(EGFElderChannel Channel) const
{
	return Lanes[LaneIndex(Channel)];
}

TArray<FGFElderPost> FGFElderBook::Snapshot() const
{
	TArray<FGFElderPost> Out;
	for (const TArray<FGFElderPost>& Lane : Lanes)
	{
		Out.Append(Lane);
	}
	return Out;
}

TArray<FGFElderPost> FGFElderBook::Feed(FName FactionId) const
{
	TArray<EGFElderChannel> Channels;
	Channels.Add(EGFElderChannel::Dinobook);
	if (FactionId == ElderViewId())
	{
		Channels.Add(EGFElderChannel::RimeBook);
		Channels.Add(EGFElderChannel::VerdantBook);
		Channels.Add(EGFElderChannel::CinderBook);
	}
	else
	{
		const EGFElderChannel Town = TownChannel(FactionId);
		if (Town != EGFElderChannel::Dinobook)
		{
			Channels.Add(Town);
		}
	}

	TArray<FGFElderPost> Out;
	for (EGFElderChannel Channel : Channels)
	{
		Out.Append(ChannelPosts(Channel));
	}
	Out.Sort([](const FGFElderPost& A, const FGFElderPost& B)
	{
		if (A.Time < B.Time)
		{
			return true;
		}
		if (B.Time < A.Time)
		{
			return false;
		}
		return A.Id < B.Id;
	});
	return Out;
}

bool UGFElderBookSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UGFElderBookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() != NM_Client)
		{
			Apply(EGFElderBookEvent::BeganAuthority);
		}
	}
}

UGFElderBookSubsystem* UGFElderBookSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UGFElderBookSubsystem>() : nullptr;
}

bool UGFElderBookSubsystem::Push(const FGFElderPost& Post)
{
	if (State != EGFElderBookState::Serving)
	{
		return false;
	}
	if (!Book.Add(Post))
	{
		return false;
	}
	const FGFElderPost& Stored = Book.ChannelPosts(Post.Channel).Last();
	UE_LOG(LogGF, Log, TEXT("Elder Book %s #%d %s"), ChannelLabel(Stored.Channel), Stored.Id, *Stored.Line);
	Publish();
	return true;
}

void UGFElderBookSubsystem::NoteRelay(AGFElderBookRelay* InRelay)
{
	if (!InRelay)
	{
		return;
	}
	Relay = InRelay;
	Apply(EGFElderBookEvent::BeganMirror);
	if (State == EGFElderBookState::Mirroring)
	{
		Book.Replace(InRelay->GetPosts());
	}
}

void UGFElderBookSubsystem::ClearRelay(AGFElderBookRelay* Which)
{
	if (Relay.Get() != Which)
	{
		return;
	}
	Relay = nullptr;
	Apply(EGFElderBookEvent::LostRelay);
	if (State == EGFElderBookState::Idle)
	{
		Book.Reset();
	}
}

void UGFElderBookSubsystem::Apply(EGFElderBookEvent Event)
{
	State = FGFElderBook::Transition(State, Event);
}

void UGFElderBookSubsystem::Publish()
{
	UWorld* World = GetWorld();
	if (!World || State != EGFElderBookState::Serving)
	{
		return;
	}
	AGFElderBookRelay* Live = Relay.Get();
	if (!Live)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Live = World->SpawnActor<AGFElderBookRelay>(AGFElderBookRelay::StaticClass(), FTransform::Identity, Params);
		Relay = Live;
	}
	if (!Live)
	{
		return;
	}
	Live->SetPosts(Book.Snapshot());
}
