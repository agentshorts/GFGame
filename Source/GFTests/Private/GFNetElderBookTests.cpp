// Late joiner receives the Elder Book ring. Needs the editor (one-process PIE).
#include "GFNetTestUtils.h"
#include "GFElderBook.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

static constexpr EAutomationTestFlags GFElderBookNetFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

namespace
{
	FString PostDigest(const TArray<FGFElderPost>& Posts)
	{
		FString Out;
		for (const FGFElderPost& Post : Posts)
		{
			Out += FString::Printf(TEXT("%d|%d|%d|%s|%s|%s|%.3f;"),
				Post.Id,
				static_cast<int32>(Post.Channel),
				static_cast<int32>(Post.Kind),
				*Post.Line,
				*Post.SubjectKey,
				*Post.FactionId.ToString(),
				Post.Time);
		}
		return Out;
	}

	bool FeedHasChannel(const TArray<FGFElderPost>& Posts, EGFElderChannel Channel)
	{
		for (const FGFElderPost& Post : Posts)
		{
			if (Post.Channel == Channel)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetElderBookLateJoinTest, "GFGame.Net.ElderBook.LateJoin", GFElderBookNetFlags)
bool FGFNetElderBookLateJoinTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 1);
	Do(Ctx, [this]()
	{
		UWorld* Server = ServerWorld();
		UGFElderBookSubsystem* Sub = Server ? Server->GetSubsystem<UGFElderBookSubsystem>() : nullptr;
		TestNotNull(TEXT("Server book subsystem"), Sub);
		if (!Sub)
		{
			return;
		}
		TestEqual(TEXT("Server starts serving"), static_cast<int32>(Sub->GetState()), static_cast<int32>(EGFElderBookState::Serving));

		TestTrue(TEXT("Rime theft"), FGFElderBook::Post(Server, EGFElderChannel::RimeBook, EGFElderKind::Theft, TEXT("Miner"), TEXT("P1"), FName(TEXT("Rime")), 1.0, 100));
		for (int32 Id = 1; Id <= FGFElderBook::PostsPerChannel + 1; ++Id)
		{
			const bool bLanded = FGFElderBook::Post(Server, EGFElderChannel::Dinobook, EGFElderKind::Joined, TEXT("Miner"), TEXT("P1"), FName(TEXT("Rime")), 10.0 + Id, Id);
			TestTrue(*FString::Printf(TEXT("Dinobook %d"), Id), bLanded);
		}
		TestFalse(TEXT("Duplicate id is refused on the server"), FGFElderBook::Post(Server, EGFElderChannel::VerdantBook, EGFElderKind::Hire, TEXT("Miner"), TEXT("P2"), FName(TEXT("Verdant")), 80.0, 100));
		TestTrue(TEXT("Verdant hire"), FGFElderBook::Post(Server, EGFElderChannel::VerdantBook, EGFElderKind::Hire, TEXT("Miner"), TEXT("P2"), FName(TEXT("Verdant")), 80.0, 200));
		TestTrue(TEXT("Cinder show"), FGFElderBook::Post(Server, EGFElderChannel::CinderBook, EGFElderKind::ShowWin, TEXT("Miner"), TEXT("P3"), FName(TEXT("Cinderwrights")), 90.0, 300));

		const TArray<FGFElderPost>& Dino = Sub->GetBook().ChannelPosts(EGFElderChannel::Dinobook);
		TestEqual(TEXT("Server cap held"), Dino.Num(), FGFElderBook::PostsPerChannel);
		TestEqual(TEXT("Server dropped the oldest dinobook id"), Dino[0].Id, 2);
		TestEqual(TEXT("Server kept the newest dinobook id"), Dino.Last().Id, FGFElderBook::PostsPerChannel + 1);
		TestEqual(TEXT("Server kept the other towns"), Sub->GetBook().Feed(FGFElderBook::ElderViewId()).Num(), FGFElderBook::PostsPerChannel + 3);
	});
	LateJoin(Ctx);
	WaitUntil(Ctx, TEXT("late joiner mirrors the ring"), 20.f, []()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		if (Clients.Num() == 0 || !Clients.Last())
		{
			return false;
		}
		const UGFElderBookSubsystem* Sub = Clients.Last()->GetSubsystem<UGFElderBookSubsystem>();
		if (!Sub || Sub->GetState() != EGFElderBookState::Mirroring)
		{
			return false;
		}
		return Sub->GetBook().ChannelPosts(EGFElderChannel::Dinobook).Num() == FGFElderBook::PostsPerChannel
			&& Sub->GetBook().ChannelPosts(EGFElderChannel::RimeBook).Num() == 1
			&& Sub->GetBook().ChannelPosts(EGFElderChannel::VerdantBook).Num() == 1
			&& Sub->GetBook().ChannelPosts(EGFElderChannel::CinderBook).Num() == 1;
	});
	Do(Ctx, [this]()
	{
		UWorld* Server = ServerWorld();
		const TArray<UWorld*> Clients = ClientWorlds();
		TestTrue(TEXT("Late joiner world"), Clients.Num() > 0 && Clients.Last() != nullptr);
		if (Clients.Num() == 0 || !Clients.Last() || !Server)
		{
			return;
		}
		const UGFElderBookSubsystem* ServerBook = Server->GetSubsystem<UGFElderBookSubsystem>();
		const UGFElderBookSubsystem* ClientBook = Clients.Last()->GetSubsystem<UGFElderBookSubsystem>();
		TestNotNull(TEXT("Server book still there"), ServerBook);
		TestNotNull(TEXT("Client book"), ClientBook);
		if (!ServerBook || !ClientBook)
		{
			return;
		}
		TestEqual(TEXT("Late joiner is mirroring"), static_cast<int32>(ClientBook->GetState()), static_cast<int32>(EGFElderBookState::Mirroring));
		TestEqual(TEXT("Late joiner has the same posts"), PostDigest(ClientBook->GetBook().Snapshot()), PostDigest(ServerBook->GetBook().Snapshot()));
		TestEqual(TEXT("Oldest dinobook id on the late joiner"), ClientBook->GetBook().ChannelPosts(EGFElderChannel::Dinobook)[0].Id, 2);

		const TArray<FGFElderPost> Rime = ClientBook->GetBook().Feed(FName(TEXT("Rime")));
		TestTrue(TEXT("Late joiner town feed has Dinobook"), FeedHasChannel(Rime, EGFElderChannel::Dinobook));
		TestTrue(TEXT("Late joiner town feed has Rime"), FeedHasChannel(Rime, EGFElderChannel::RimeBook));
		TestFalse(TEXT("Late joiner town feed skips Verdant"), FeedHasChannel(Rime, EGFElderChannel::VerdantBook));
		TestFalse(TEXT("Late joiner town feed skips Cinder"), FeedHasChannel(Rime, EGFElderChannel::CinderBook));

		const TArray<FGFElderPost> Elder = ClientBook->GetBook().Feed(FGFElderBook::ElderViewId());
		TestEqual(TEXT("Late joiner elder view is all four channels"), Elder.Num(), FGFElderBook::PostsPerChannel + 3);
		TestTrue(TEXT("Elder view has Verdant"), FeedHasChannel(Elder, EGFElderChannel::VerdantBook));
		TestTrue(TEXT("Elder view has Cinder"), FeedHasChannel(Elder, EGFElderChannel::CinderBook));
	});
	Stop(Ctx);
	return true;
}

#endif
