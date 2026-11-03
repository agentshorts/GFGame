// Elder Book rules. No world: channels, the ring, and the town / elder views.
#include "Misc/AutomationTest.h"
#include "GFArenaRules.h"
#include "GFElderBook.h"
#include "GFVillainRules.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFElderBookFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace
{
	const FName RimeId(TEXT("Rime"));
	const FName VerdantId(TEXT("Verdant"));
	const FName CinderId(TEXT("Cinderwrights"));

	bool HasChannel(const TArray<FGFElderPost>& Posts, EGFElderChannel Channel)
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

	bool HasLine(const TArray<FGFElderPost>& Posts, const FString& Line)
	{
		for (const FGFElderPost& Post : Posts)
		{
			if (Post.Line == Line)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderBookChannelsTest, "GFGame.ElderBook.Channels", GFElderBookFlags)
bool FGFElderBookChannelsTest::RunTest(const FString&)
{
	TestFalse(TEXT("No world, no post"), FGFElderBook::Post(nullptr, EGFElderChannel::RimeBook, EGFElderKind::Theft, TEXT("Miner"), TEXT("P1"), RimeId, 1.0));

	TestEqual(TEXT("Theft line"), FGFElderBook::LineFor(EGFElderKind::Theft, TEXT("Miner")), FString(TEXT("Miner took a town crystal.")));
	TestEqual(TEXT("Hire line"), FGFElderBook::LineFor(EGFElderKind::Hire, TEXT("Miner")), FString(TEXT("The elder hired dinos against Miner.")));
	TestEqual(TEXT("Joined line"), FGFElderBook::LineFor(EGFElderKind::Joined, TEXT("Miner")), FString(TEXT("Miner joined a faction.")));
	TestEqual(TEXT("Rank line"), FGFElderBook::LineFor(EGFElderKind::RankUp, TEXT("Miner")), FString(TEXT("Miner rose in rank.")));
	TestEqual(TEXT("Show line"), FGFElderBook::LineFor(EGFElderKind::ShowWin, TEXT("Miner")), FString(TEXT("Miner won the show and leads the faction.")));
	TestTrue(TEXT("Blank name still reads as someone"), FGFElderBook::LineFor(EGFElderKind::Theft, FString()).Contains(TEXT("Someone")));
	TestTrue(TEXT("Villain kind has no quest line"), FGFElderBook::LineFor(EGFElderKind::Villain, TEXT("Miner")).IsEmpty());

	FGFElderBook Book;
	const FGFElderPost Theft = FGFElderBook::Make(EGFElderChannel::RimeBook, EGFElderKind::Theft, TEXT("Miner"), TEXT("P1"), RimeId, 1.0, 1);
	const FGFElderPost Hire = FGFElderBook::Make(EGFElderChannel::VerdantBook, EGFElderKind::Hire, TEXT("Miner"), TEXT("P1"), VerdantId, 2.0, 2);
	const FGFElderPost Joined = FGFElderBook::Make(EGFElderChannel::CinderBook, EGFElderKind::Joined, TEXT("Miner"), TEXT("P1"), CinderId, 3.0, 3);
	const FGFElderPost Rank = FGFElderBook::Make(EGFElderChannel::RimeBook, EGFElderKind::RankUp, TEXT("Miner"), TEXT("P1"), RimeId, 4.0, 4);
	const FGFElderPost Show = FGFElderBook::Make(EGFElderChannel::Dinobook, EGFElderKind::ShowWin, TEXT("Miner"), TEXT("P1"), RimeId, 5.0, 5);
	TestTrue(TEXT("Theft lands"), Book.Add(Theft));
	TestTrue(TEXT("Hire lands"), Book.Add(Hire));
	TestTrue(TEXT("Joined lands"), Book.Add(Joined));
	TestTrue(TEXT("Rank lands"), Book.Add(Rank));
	TestTrue(TEXT("Show lands"), Book.Add(Show));

	TestEqual(TEXT("Rime holds theft and rank"), Book.ChannelPosts(EGFElderChannel::RimeBook).Num(), 2);
	TestEqual(TEXT("Rime theft stays on Rime"), static_cast<int32>(Book.ChannelPosts(EGFElderChannel::RimeBook)[0].Kind), static_cast<int32>(EGFElderKind::Theft));
	TestEqual(TEXT("Verdant holds the hire"), Book.ChannelPosts(EGFElderChannel::VerdantBook).Num(), 1);
	TestEqual(TEXT("Cinder holds the join"), Book.ChannelPosts(EGFElderChannel::CinderBook)[0].Line, Joined.Line);
	TestEqual(TEXT("Dinobook holds the show"), static_cast<int32>(Book.ChannelPosts(EGFElderChannel::Dinobook)[0].Kind), static_cast<int32>(EGFElderKind::ShowWin));
	TestFalse(TEXT("Theft did not spill into Dinobook"), HasLine(Book.ChannelPosts(EGFElderChannel::Dinobook), Theft.Line));

	const FGFElderPost Seen = FGFElderBook::MakeVillain(1, TEXT("P1"), RimeId, 6.0, 6);
	const FGFElderPost Wanted = FGFElderBook::MakeVillain(FGFVillainRules::WantedStreak, TEXT("P1"), RimeId, 7.0, 7);
	TestEqual(TEXT("Villain channel"), static_cast<int32>(Seen.Channel), static_cast<int32>(EGFElderChannel::Dinobook));
	TestEqual(TEXT("Villain kind"), static_cast<int32>(Seen.Kind), static_cast<int32>(EGFElderKind::Villain));
	TestEqual(TEXT("Villain line is the existing Dinobook line"), Seen.Line, FGFVillainRules::DinobookLine(1));
	TestEqual(TEXT("Wanted line is the existing poster"), Wanted.Line, FGFVillainRules::DinobookLine(3));
	TestTrue(TEXT("Villain line lands on Dinobook"), Book.Add(Seen));
	TestEqual(TEXT("Dinobook grew by the villain line"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), 2);

	const FGFArenaPost Brag = FGFArenaPost::Brag(3);
	const FGFArenaPost Quit = FGFArenaPost::RentalQuit(TEXT("Nipper"));
	const FGFArenaPost Nice = FGFArenaPost::ElderNice(TEXT("Scale"));
	const FGFElderPost BragPost = FGFElderBook::MakeArena(Brag.Kind, Brag.Line, TEXT("P1"), RimeId, 8.0, 8);
	const FGFElderPost QuitPost = FGFElderBook::MakeArena(Quit.Kind, Quit.Line, TEXT("P1"), RimeId, 9.0, 9);
	const FGFElderPost NicePost = FGFElderBook::MakeArena(Nice.Kind, Nice.Line, TEXT("P1"), RimeId, 10.0, 10);
	TestEqual(TEXT("Brag kind"), static_cast<int32>(BragPost.Kind), static_cast<int32>(EGFElderKind::Brag));
	TestEqual(TEXT("Quit kind"), static_cast<int32>(QuitPost.Kind), static_cast<int32>(EGFElderKind::RentalQuit));
	TestEqual(TEXT("Elder kind"), static_cast<int32>(NicePost.Kind), static_cast<int32>(EGFElderKind::ElderNice));
	TestEqual(TEXT("Brag line is the arena line"), BragPost.Line, Brag.Line);
	TestEqual(TEXT("Quit line is the arena line"), QuitPost.Line, Quit.Line);
	TestEqual(TEXT("Elder line is the arena line"), NicePost.Line, Nice.Line);
	TestEqual(TEXT("Arena posts use Dinobook"), static_cast<int32>(BragPost.Channel), static_cast<int32>(EGFElderChannel::Dinobook));
	TestTrue(TEXT("Brag lands"), Book.Add(BragPost));
	TestTrue(TEXT("Quit lands"), Book.Add(QuitPost));
	TestTrue(TEXT("Elder nice lands"), Book.Add(NicePost));
	TestEqual(TEXT("Dinobook holds the arena lines"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), 5);

	const FGFElderPost Bogus = FGFElderBook::MakeArena(TEXT("NotAKind"), TEXT("nope"), TEXT("P1"), RimeId, 11.0, 11);
	TestEqual(TEXT("Unknown arena kind is refused"), static_cast<int32>(Bogus.Kind), static_cast<int32>(EGFElderKind::None));
	TestFalse(TEXT("None does not land"), Book.Add(Bogus));

	TestEqual(TEXT("Authority starts serving"), static_cast<int32>(FGFElderBook::Transition(EGFElderBookState::Idle, EGFElderBookEvent::BeganAuthority)), static_cast<int32>(EGFElderBookState::Serving));
	TestEqual(TEXT("A client with a relay mirrors"), static_cast<int32>(FGFElderBook::Transition(EGFElderBookState::Idle, EGFElderBookEvent::BeganMirror)), static_cast<int32>(EGFElderBookState::Mirroring));
	TestEqual(TEXT("The server relay stays serving"), static_cast<int32>(FGFElderBook::Transition(EGFElderBookState::Serving, EGFElderBookEvent::BeganMirror)), static_cast<int32>(EGFElderBookState::Serving));
	TestEqual(TEXT("Losing the relay drops a mirror"), static_cast<int32>(FGFElderBook::Transition(EGFElderBookState::Mirroring, EGFElderBookEvent::LostRelay)), static_cast<int32>(EGFElderBookState::Idle));
	TestEqual(TEXT("Losing the relay keeps the server"), static_cast<int32>(FGFElderBook::Transition(EGFElderBookState::Serving, EGFElderBookEvent::LostRelay)), static_cast<int32>(EGFElderBookState::Serving));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderBookCapTest, "GFGame.ElderBook.Cap", GFElderBookFlags)
bool FGFElderBookCapTest::RunTest(const FString&)
{
	FGFElderBook Book;
	for (int32 Id = 1; Id <= FGFElderBook::PostsPerChannel; ++Id)
	{
		const FGFElderPost Post = FGFElderBook::Make(EGFElderChannel::Dinobook, EGFElderKind::Joined, TEXT("Miner"), TEXT("P1"), RimeId, static_cast<double>(Id), Id);
		TestTrue(*FString::Printf(TEXT("Post %d lands"), Id), Book.Add(Post));
	}
	TestEqual(TEXT("The ring is full"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), FGFElderBook::PostsPerChannel);
	TestEqual(TEXT("The earliest post is still first"), Book.ChannelPosts(EGFElderChannel::Dinobook)[0].Id, 1);

	const FGFElderPost Extra = FGFElderBook::Make(EGFElderChannel::Dinobook, EGFElderKind::Joined, TEXT("Miner"), TEXT("P1"), RimeId, 51.0, 51);
	TestTrue(TEXT("The next post still lands"), Book.Add(Extra));
	const TArray<FGFElderPost>& Lane = Book.ChannelPosts(EGFElderChannel::Dinobook);
	TestEqual(TEXT("The cap stays at 50"), Lane.Num(), FGFElderBook::PostsPerChannel);
	TestEqual(TEXT("The oldest id was dropped"), Lane[0].Id, 2);
	TestEqual(TEXT("The newest id is last"), Lane.Last().Id, 51);
	TestFalse(TEXT("The dropped id is gone"), Book.Contains(1));

	const FGFElderPost Town = FGFElderBook::Make(EGFElderChannel::RimeBook, EGFElderKind::Theft, TEXT("Miner"), TEXT("P1"), RimeId, 1.0, 80);
	TestTrue(TEXT("Another channel still accepts"), Book.Add(Town));
	TestEqual(TEXT("The other channel was not trimmed"), Book.ChannelPosts(EGFElderChannel::RimeBook).Num(), 1);
	TestEqual(TEXT("Dinobook was not trimmed by the town post"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), FGFElderBook::PostsPerChannel);

	const int32 Before = Book.ChannelPosts(EGFElderChannel::Dinobook).Num();
	const FGFElderPost Duplicate = FGFElderBook::Make(EGFElderChannel::VerdantBook, EGFElderKind::Hire, TEXT("Miner"), TEXT("P2"), VerdantId, 90.0, 51);
	TestFalse(TEXT("A live id is refused"), Book.Add(Duplicate));
	TestTrue(TEXT("The live id stays on Dinobook"), Book.Contains(51));
	TestEqual(TEXT("Verdant stayed empty"), Book.ChannelPosts(EGFElderChannel::VerdantBook).Num(), 0);
	TestEqual(TEXT("A refused id does not drop the oldest"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), Before);
	TestEqual(TEXT("The oldest id is still 2"), Book.ChannelPosts(EGFElderChannel::Dinobook)[0].Id, 2);

	const FGFElderPost Again = FGFElderBook::Make(EGFElderChannel::Dinobook, EGFElderKind::RankUp, TEXT("Miner"), TEXT("P1"), RimeId, 99.0, 1);
	TestTrue(TEXT("A dropped id can be used again"), Book.Add(Again));
	TestEqual(TEXT("The reused id is the new last post"), Book.ChannelPosts(EGFElderChannel::Dinobook).Last().Id, 1);
	TestEqual(TEXT("The cap still holds"), Book.ChannelPosts(EGFElderChannel::Dinobook).Num(), FGFElderBook::PostsPerChannel);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFElderBookViewsTest, "GFGame.ElderBook.Views", GFElderBookFlags)
bool FGFElderBookViewsTest::RunTest(const FString&)
{
	FGFElderBook Book;
	const FGFElderPost Dino = FGFElderBook::Make(EGFElderChannel::Dinobook, EGFElderKind::ShowWin, TEXT("Miner"), TEXT("P1"), RimeId, 5.0, 1);
	const FGFElderPost Rime = FGFElderBook::Make(EGFElderChannel::RimeBook, EGFElderKind::Theft, TEXT("Miner"), TEXT("P1"), RimeId, 1.0, 2);
	const FGFElderPost Verdant = FGFElderBook::Make(EGFElderChannel::VerdantBook, EGFElderKind::Hire, TEXT("Miner"), TEXT("P2"), RimeId, 3.0, 3);
	const FGFElderPost Cinder = FGFElderBook::Make(EGFElderChannel::CinderBook, EGFElderKind::RankUp, TEXT("Miner"), TEXT("P3"), CinderId, 4.0, 4);
	TestTrue(TEXT("Dinobook"), Book.Add(Dino));
	TestTrue(TEXT("Rime"), Book.Add(Rime));
	TestTrue(TEXT("Verdant, even with a Rime faction id"), Book.Add(Verdant));
	TestTrue(TEXT("Cinder"), Book.Add(Cinder));

	const TArray<FGFElderPost> RimeFeed = Book.Feed(RimeId);
	TestEqual(TEXT("Rime sees two posts"), RimeFeed.Num(), 2);
	TestTrue(TEXT("Rime sees Dinobook"), HasChannel(RimeFeed, EGFElderChannel::Dinobook));
	TestTrue(TEXT("Rime sees Rime"), HasChannel(RimeFeed, EGFElderChannel::RimeBook));
	TestFalse(TEXT("Rime does not see Verdant"), HasChannel(RimeFeed, EGFElderChannel::VerdantBook));
	TestFalse(TEXT("Rime does not see Cinder"), HasChannel(RimeFeed, EGFElderChannel::CinderBook));
	TestFalse(TEXT("A Verdant line stays out of Rime"), HasLine(RimeFeed, Verdant.Line));
	TestEqual(TEXT("Rime feed is earliest time first"), RimeFeed[0].Id, Rime.Id);
	TestEqual(TEXT("Dinobook follows the earlier town post"), RimeFeed[1].Id, Dino.Id);

	const TArray<FGFElderPost> VerdantFeed = Book.Feed(VerdantId);
	TestEqual(TEXT("Verdant sees two posts"), VerdantFeed.Num(), 2);
	TestTrue(TEXT("Verdant sees its hire"), HasLine(VerdantFeed, Verdant.Line));
	TestFalse(TEXT("Verdant does not see the Rime theft"), HasLine(VerdantFeed, Rime.Line));
	TestFalse(TEXT("Verdant does not see Cinder"), HasChannel(VerdantFeed, EGFElderChannel::CinderBook));

	const TArray<FGFElderPost> CinderFeed = Book.Feed(CinderId);
	const TArray<FGFElderPost> ShortCinder = Book.Feed(FName(TEXT("Cinder")));
	TestEqual(TEXT("Cinderwrights sees two posts"), CinderFeed.Num(), 2);
	TestTrue(TEXT("Cinderwrights sees CinderBook"), HasLine(CinderFeed, Cinder.Line));
	TestFalse(TEXT("Cinderwrights does not see Rime"), HasLine(CinderFeed, Rime.Line));
	TestEqual(TEXT("Cinder is the same view as Cinderwrights"), ShortCinder.Num(), CinderFeed.Num());
	TestEqual(TEXT("Same first post"), ShortCinder[0].Id, CinderFeed[0].Id);
	TestEqual(TEXT("Same second post"), ShortCinder[1].Id, CinderFeed[1].Id);

	const TArray<FGFElderPost> Elder = Book.Feed(FGFElderBook::ElderViewId());
	TestEqual(TEXT("The elder sees all four"), Elder.Num(), 4);
	TestTrue(TEXT("Elder sees Dinobook"), HasChannel(Elder, EGFElderChannel::Dinobook));
	TestTrue(TEXT("Elder sees Rime"), HasChannel(Elder, EGFElderChannel::RimeBook));
	TestTrue(TEXT("Elder sees Verdant"), HasChannel(Elder, EGFElderChannel::VerdantBook));
	TestTrue(TEXT("Elder sees Cinder"), HasChannel(Elder, EGFElderChannel::CinderBook));
	TestEqual(TEXT("Elder order starts with the earliest time"), Elder[0].Id, Rime.Id);
	TestEqual(TEXT("Then Verdant"), Elder[1].Id, Verdant.Id);
	TestEqual(TEXT("Then Cinder"), Elder[2].Id, Cinder.Id);
	TestEqual(TEXT("Then Dinobook"), Elder[3].Id, Dino.Id);

	const TArray<FGFElderPost> Outsider = Book.Feed(NAME_None);
	TestEqual(TEXT("No faction sees only Dinobook"), Outsider.Num(), 1);
	TestEqual(TEXT("That one post is the shared book"), static_cast<int32>(Outsider[0].Channel), static_cast<int32>(EGFElderChannel::Dinobook));
	return true;
}

#endif
