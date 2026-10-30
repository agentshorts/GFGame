// Unbinding: attendance, a single recipient's taming material, and a separate tame that spends it.
#include "Misc/AutomationTest.h"
#include "GFDino.h"
#include "GFGameState.h"
#include "../GFUnbinding.h"

#if WITH_DEV_AUTOMATION_TESTS

/** Test-only faction ids (opaque to GFNet), cycled over the roster. */
namespace GFUnbindingFactions
{
	inline FName F1() { return TEXT("F1"); }
	inline FName F2() { return TEXT("F2"); }
	inline FName F3() { return TEXT("F3"); }

	inline const TArray<FName>& All()
	{
		static const TArray<FName> Ids = { F1(), F2(), F3() };
		return Ids;
	}
}

namespace
{
	constexpr EAutomationTestFlags EndgameFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool SameNetwork(const TArray<FGFUnbindingPlayer>& Before, const TArray<FGFUnbindingPlayer>& After)
	{
		if (Before.Num() != After.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			if (!Before[Index].SameNetwork(After[Index]))
			{
				return false;
			}
		}
		return true;
	}

	/** Host at [0] ("H"), then guests. Factions cycle the roster. Everyone is tied to the host, and the host to them. */
	TArray<FGFUnbindingPlayer> Roster(int32 Count)
	{
		const TArray<FName>& Factions = GFUnbindingFactions::All();
		TArray<FGFUnbindingPlayer> Players;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FGFUnbindingPlayer Player;
			Player.Key = (Index == 0) ? FString(TEXT("H")) : FString::Printf(TEXT("C%d"), Index);
			Player.FactionId = Factions.Num() > 0 ? Factions[Index % Factions.Num()] : NAME_None;
			Player.bHost = (Index == 0);
			Player.bPresent = true;
			Player.ColorIndex = Index;
			Player.NetId = Player.Key;
			Players.Add(Player);
		}
		for (int32 Index = 0; Index < Players.Num(); ++Index)
		{
			if (Index == 0)
			{
				for (int32 Other = 1; Other < Players.Num(); ++Other)
				{
					Players[Index].TieKeys.Add(Players[Other].Key);
				}
			}
			else
			{
				Players[Index].TieKeys.Add(TEXT("H"));
			}
		}
		return Players;
	}

	void SeedWood(FGFUnbinding& Gathering, const TArray<FGFUnbindingPlayer>& Players, int32 Count)
	{
		for (const FGFUnbindingPlayer& Player : Players)
		{
			Gathering.Bags.FindOrAdd(Player.Key).Add(TEXT("Wood"), Count);
		}
	}

	bool WoodUntouched(const FGFUnbinding& Gathering, const TArray<FGFUnbindingPlayer>& Players, int32 Count)
	{
		for (const FGFUnbindingPlayer& Player : Players)
		{
			const FGFInventory* Bag = Gathering.Bags.Find(Player.Key);
			if (!Bag || Bag->GetCount(TEXT("Wood")) != Count)
			{
				return false;
			}
		}
		return true;
	}

	int32 SessionCap(FAutomationTestBase& Test)
	{
		const int32 Cap = FGFUnbindingRules::SessionMaxPlayers();
		// The game's max players (GameSession MaxPlayers=10 in DefaultGame.ini), not 9, 12 or 0.
		Test.TestEqual(TEXT("Session cap is the game's max players"), Cap, 10);
		Test.TestEqual(TEXT("Fallback is also 10"), FGFUnbindingRules::DefaultSessionMaxPlayers, 10);
		Test.TestEqual(TEXT("Default required is the session cap"), FGFUnbindingRules::RequiredCount(FGFUnbindingTuning()), 10);
		const AGFGameState* Defaults = GetDefault<AGFGameState>();
		if (Defaults)
		{
			// 4 per faction (4/3/3); the headcount still comes from MaxPlayers, not 3 x 4 = 12.
			Test.TestEqual(TEXT("Per-faction seats are 4 and don't set the headcount"), Defaults->MaxPerFaction, 4);
		}
		return Cap;
	}

	bool RequireCap(FAutomationTestBase& Test, int32& OutCap, int32 Min)
	{
		OutCap = SessionCap(Test);
		if (OutCap < Min)
		{
			Test.AddError(TEXT("Session cap is smaller than this case"));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgameTooFewTest, "GFGame.Endgame.TooFewPlayers", EndgameFlags)
bool FGFEndgameTooFewTest::RunTest(const FString&)
{
	int32 Cap = 0;
	if (!RequireCap(*this, Cap, 2))
	{
		return false;
	}
	FGFUnbinding Gathering;
	TArray<FGFUnbindingPlayer> Short = Roster(Cap - 1);
	Short.Last().bJailed = true;
	const TArray<FGFUnbindingPlayer> Before = Short;

	const FGFUnbindingResult Result = Gathering.TryComplete(Short);
	TestEqual(TEXT("Nine present"), FGFUnbindingRules::PresentCount(Short), Cap - 1);
	TestFalse(TEXT("Short of the session cap"), Result.bOk);
	TestEqual(TEXT("Too few"), Result.Code, FGFUnbindingCodes::TooFewPlayers);
	TestEqual(TEXT("No completion recorded"), Gathering.Completions.Num(), 0);
	TestEqual(TEXT("Host got nothing"), Gathering.MaterialOf(TEXT("H")), 0);
	TestTrue(TEXT("Jailed player still jailed"), Short.Last().bJailed);
	TestTrue(TEXT("Network unchanged"), SameNetwork(Before, Short));
	TestEqual(TEXT("Nobody tamed"), Gathering.Tamers().Num(), 0);

	// A logged-out seat does not count, even when the array is otherwise full.
	TArray<FGFUnbindingPlayer> Full = Roster(Cap);
	const TArray<FGFUnbindingPlayer> FullBefore = Full;
	Full.Last().bLoggedOut = true;
	TArray<FGFUnbindingPlayer> LoggedOut = Full;
	const FGFUnbindingResult Absent = Gathering.TryComplete(Full);
	TestFalse(TEXT("Logged-out player is not present"), Absent.bOk);
	TestEqual(TEXT("Still too few"), Absent.Code, FGFUnbindingCodes::TooFewPlayers);
	TestEqual(TEXT("Still no material"), Gathering.MaterialOf(TEXT("H")), 0);
	TestTrue(TEXT("Logging state was already the caller's"), SameNetwork(LoggedOut, Full));
	TestTrue(TEXT("The roster we started from is intact aside from that flag"), FullBefore.Num() == Full.Num() && FullBefore[0].SameNetwork(Full[0]));

	// The requirement is a knob. A smaller bar passes, and one under that bar still fails.
	Gathering.Tuning.RequiredPlayers = 2;
	TArray<FGFUnbindingPlayer> Pair = Roster(2);
	const TArray<FGFUnbindingPlayer> PairBefore = Pair;
	const FGFUnbindingResult PairResult = Gathering.TryComplete(Pair);
	TestTrue(TEXT("Tuned bar of two"), PairResult.bOk);
	TestEqual(TEXT("Default rule still names the host"), PairResult.RecipientKey, FString(TEXT("H")));
	TestEqual(TEXT("One material"), Gathering.MaterialOf(TEXT("H")), 1);
	TestTrue(TEXT("Pair network unchanged"), SameNetwork(PairBefore, Pair));

	TArray<FGFUnbindingPlayer> One = Roster(1);
	const FGFUnbindingResult OneResult = Gathering.TryComplete(One);
	TestFalse(TEXT("Under the tuned bar"), OneResult.bOk);
	TestEqual(TEXT("Tuned failure"), OneResult.Code, FGFUnbindingCodes::TooFewPlayers);
	TestEqual(TEXT("No second completion"), Gathering.Completions.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgameRecipientTest, "GFGame.Endgame.MaterialOnlyToRecipient", EndgameFlags)
bool FGFEndgameRecipientTest::RunTest(const FString&)
{
	int32 Cap = 0;
	if (!RequireCap(*this, Cap, 2))
	{
		return false;
	}
	TArray<FGFUnbindingPlayer> Players = Roster(Cap);
	Players[1].bJailed = true;
	const TArray<FGFUnbindingPlayer> Before = Players;
	const FString Guest = Players[1].Key;

	FGFUnbinding Gathering;
	Gathering.Tuning.RecipientRule = EGFUnbindingRecipient::Explicit;
	Gathering.Tuning.ExplicitRecipientKey = Guest;
	SeedWood(Gathering, Players, 3);

	const FGFUnbindingResult Result = Gathering.TryComplete(Players);
	TestTrue(TEXT("Full roster"), Result.bOk);
	TestEqual(TEXT("Names the guest"), Result.RecipientKey, Guest);
	TestEqual(TEXT("One completion"), Gathering.Completions.Num(), 1);
	TestEqual(TEXT("Recorded recipient"), Gathering.Completions[0].RecipientKey, Guest);
	for (const FGFUnbindingPlayer& Player : Players)
	{
		const int32 Expect = (Player.Key == Guest) ? 1 : 0;
		TestEqual(FString::Printf(TEXT("Material for %s"), *Player.Key), Gathering.MaterialOf(Player.Key), Expect);
	}
	TestTrue(TEXT("Wood untouched"), WoodUntouched(Gathering, Players, 3));
	TestTrue(TEXT("Guest still jailed"), Players[1].bJailed);
	TestTrue(TEXT("Still present"), Players[1].bPresent && !Players[1].bLoggedOut);
	TestEqual(TEXT("Faction unchanged"), Players[1].FactionId, GFUnbindingFactions::F2());
	TestTrue(TEXT("Host flag unchanged"), Players[0].bHost && !Players[1].bHost);
	TestTrue(TEXT("Ties unchanged"), SameNetwork(Before, Players));
	TestEqual(TEXT("Not tamed by the grant"), Gathering.Tamers().Num(), 0);

	// No present host: the default rule names nobody, so nobody receives the material.
	FGFUnbinding NoHostGathering;
	TArray<FGFUnbindingPlayer> NoHost = Players;
	for (FGFUnbindingPlayer& Player : NoHost)
	{
		Player.bHost = false;
	}
	const TArray<FGFUnbindingPlayer> NoHostBefore = NoHost;
	const FGFUnbindingResult Missed = NoHostGathering.TryComplete(NoHost);
	TestFalse(TEXT("No host to name"), Missed.bOk);
	TestEqual(TEXT("No recipient"), Missed.Code, FGFUnbindingCodes::NoRecipient);
	TestEqual(TEXT("No grant"), NoHostGathering.Completions.Num(), 0);
	for (const FGFUnbindingPlayer& Player : NoHost)
	{
		TestEqual(TEXT("Still empty"), NoHostGathering.MaterialOf(Player.Key), 0);
	}
	TestTrue(TEXT("Still jailed, still here"), SameNetwork(NoHostBefore, NoHost));

	// An explicit player who has logged out is not given the material.
	FGFUnbinding AbsentGathering;
	AbsentGathering.Tuning.RecipientRule = EGFUnbindingRecipient::Explicit;
	AbsentGathering.Tuning.ExplicitRecipientKey = Guest;
	TArray<FGFUnbindingPlayer> Away = Roster(Cap);
	Away[1].bLoggedOut = true;
	Away[1].bPresent = false;
	const FGFUnbindingResult AwayResult = AbsentGathering.TryComplete(Away);
	TestFalse(TEXT("Absent recipient"), AwayResult.bOk);
	TestEqual(TEXT("Absent code"), AwayResult.Code, FGFUnbindingCodes::RecipientAbsent);
	TestEqual(TEXT("Guest still has none"), AbsentGathering.MaterialOf(Guest), 0);
	TestEqual(TEXT("Host was not the stand-in"), AbsentGathering.MaterialOf(TEXT("H")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgameTameRequiresTest, "GFGame.Endgame.TamingRequiresMaterial", EndgameFlags)
bool FGFEndgameTameRequiresTest::RunTest(const FString&)
{
	int32 Cap = 0;
	if (!RequireCap(*this, Cap, 2))
	{
		return false;
	}
	TArray<FGFUnbindingPlayer> Players = Roster(Cap);
	const TArray<FGFUnbindingPlayer> Before = Players;
	FGFUnbinding Gathering;
	TestTrue(TEXT("Grant to the host"), Gathering.TryComplete(Players).bOk);
	TestEqual(TEXT("Host is holding it"), Gathering.MaterialOf(TEXT("H")), 1);

	const FGFUnbindingResult Early = Gathering.AttemptTame(TEXT("H"));
	TestFalse(TEXT("Before he spawns"), Early.bOk);
	TestEqual(TEXT("Not spawned"), Early.Code, FGFUnbindingCodes::NotSpawned);
	TestFalse(TEXT("Material kept"), Early.bConsumed);
	TestEqual(TEXT("Still holding it"), Gathering.MaterialOf(TEXT("H")), 1);
	TestFalse(TEXT("Not tamed yet"), Gathering.HasTamed(TEXT("H")));

	// A companion species, including Rex (shared placeholder asset), is not the spawn.
	TestTrue(TEXT("Rex is a companion species"), FGFUnbindingRules::IsCompanionSpecies(TEXT("Rex")));
	TestFalse(TEXT("Rex's asset key is not the giant's name"), FGFUnbindingRules::IsBigGrumbleName(AGFDino::AssetKey(TEXT("Rex")).ToString()));
	TestFalse(TEXT("Raptor spawn ignored"), Gathering.NotifySpawned(TEXT("Raptor")));
	TestFalse(TEXT("Still not spawned"), Gathering.bGiantSpawned);
	const FGFUnbindingResult Companion = Gathering.RejectCompanion(nullptr);
	TestEqual(TEXT("Companion body refused"), Companion.Code, FGFUnbindingCodes::NotBigGrumble);
	TestEqual(TEXT("Refusal spends nothing"), Gathering.MaterialOf(TEXT("H")), 1);

	TestTrue(TEXT("Big Grumble spawns"), Gathering.NotifySpawned(GFUnbindingIds::GiantDisplayName()));
	TestTrue(TEXT("Spawned"), Gathering.bGiantSpawned);
	TestFalse(TEXT("Spawn does not tame"), Gathering.HasTamed(TEXT("H")));
	TestEqual(TEXT("Spawn does not spend"), Gathering.MaterialOf(TEXT("H")), 1);

	const FString Guest = Players[1].Key;
	const FGFUnbindingResult GuestTry = Gathering.AttemptTame(Guest);
	TestFalse(TEXT("Guest has no material"), GuestTry.bOk);
	TestEqual(TEXT("Needs the material"), GuestTry.Code, FGFUnbindingCodes::NoMaterial);
	TestFalse(TEXT("Guest not tamed"), Gathering.HasTamed(Guest));
	TestEqual(TEXT("Host still holding it"), Gathering.MaterialOf(TEXT("H")), 1);
	TestTrue(TEXT("Players unchanged"), SameNetwork(Before, Players));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgameConsumedTest, "GFGame.Endgame.MaterialConsumed", EndgameFlags)
bool FGFEndgameConsumedTest::RunTest(const FString&)
{
	int32 Cap = 0;
	if (!RequireCap(*this, Cap, 2))
	{
		return false;
	}
	TArray<FGFUnbindingPlayer> Players = Roster(Cap);
	const TArray<FGFUnbindingPlayer> Before = Players;
	FGFUnbinding Gathering;
	SeedWood(Gathering, Players, 4);
	TestTrue(TEXT("Completed"), Gathering.TryComplete(Players).bOk);
	TestTrue(TEXT("He is here"), Gathering.NotifySpawned(GFUnbindingIds::GiantDisplayName()));

	const FGFUnbindingResult Tame = Gathering.AttemptTame(TEXT("H"));
	TestTrue(TEXT("Tame"), Tame.bOk);
	TestTrue(TEXT("Consumed"), Tame.bConsumed);
	TestEqual(TEXT("Material gone"), Gathering.MaterialOf(TEXT("H")), 0);
	TestTrue(TEXT("Host has tamed one"), Gathering.HasTamed(TEXT("H")));
	TestEqual(TEXT("One tame"), Gathering.TameCount(TEXT("H")), 1);
	const TArray<FString> Who = Gathering.Tamers();
	TestEqual(TEXT("One tamer"), Who.Num(), 1);
	if (Who.Num() == 1)
	{
		TestEqual(TEXT("Only the host"), Who[0], FString(TEXT("H")));
	}
	TestTrue(TEXT("Wood remains"), WoodUntouched(Gathering, Players, 4));

	const FGFUnbindingResult Again = Gathering.AttemptTame(TEXT("H"));
	TestFalse(TEXT("Nothing left to spend"), Again.bOk);
	TestEqual(TEXT("No material"), Again.Code, FGFUnbindingCodes::NoMaterial);
	TestFalse(TEXT("Not consumed"), Again.bConsumed);
	TestEqual(TEXT("Still zero"), Gathering.MaterialOf(TEXT("H")), 0);
	TestEqual(TEXT("Still one tame"), Gathering.TameCount(TEXT("H")), 1);

	Gathering.NotifyGone();
	TestFalse(TEXT("Left"), Gathering.bGiantSpawned);
	TestEqual(TEXT("Leaving did not give the material back"), Gathering.MaterialOf(TEXT("H")), 0);
	TestTrue(TEXT("The tame is remembered"), Gathering.HasTamed(TEXT("H")));
	TestTrue(TEXT("Nobody left the session"), SameNetwork(Before, Players));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFEndgameRepeatTest, "GFGame.Endgame.RepeatCompletions", EndgameFlags)
bool FGFEndgameRepeatTest::RunTest(const FString&)
{
	int32 Cap = 0;
	if (!RequireCap(*this, Cap, 3))
	{
		return false;
	}
	TArray<FGFUnbindingPlayer> Players = Roster(Cap);
	Players[2].bJailed = true;
	const TArray<FGFUnbindingPlayer> Before = Players;
	const FString AfterHost = Players[1].Key;
	const FString Guest = Players[2].Key;

	FGFUnbinding Gathering;
	TestEqual(TEXT("Default rule is the host"), static_cast<int32>(Gathering.Tuning.RecipientRule), static_cast<int32>(EGFUnbindingRecipient::HostOnly));

	const FGFUnbindingResult First = Gathering.TryComplete(Players);
	const FGFUnbindingResult Second = Gathering.TryComplete(Players);
	TestTrue(TEXT("First"), First.bOk);
	TestTrue(TEXT("Second"), Second.bOk);
	TestEqual(TEXT("Two completions"), Gathering.Completions.Num(), 2);
	TestEqual(TEXT("First names the host"), First.RecipientKey, FString(TEXT("H")));
	TestEqual(TEXT("Second names the host again"), Second.RecipientKey, FString(TEXT("H")));
	TestEqual(TEXT("Index 1"), Gathering.Completions[0].Index, 1);
	TestEqual(TEXT("Index 2"), Gathering.Completions[1].Index, 2);
	TestEqual(TEXT("Host holds both"), Gathering.MaterialOf(TEXT("H")), 2);
	for (const FGFUnbindingPlayer& Player : Players)
	{
		if (Player.Key != TEXT("H"))
		{
			TestEqual(TEXT("Others have none"), Gathering.MaterialOf(Player.Key), 0);
		}
	}
	TestEqual(TEXT("Grants are not tames"), Gathering.Tamers().Num(), 0);
	TestTrue(TEXT("Still jailed after two gatherings"), Players[2].bJailed);
	TestTrue(TEXT("Network after two"), SameNetwork(Before, Players));

	// The next completion names whoever the rule says now, which can be someone else.
	Gathering.Tuning.RecipientRule = EGFUnbindingRecipient::NextPresent;
	const FGFUnbindingResult Third = Gathering.TryComplete(Players);
	TestTrue(TEXT("Third"), Third.bOk);
	TestEqual(TEXT("Three records"), Gathering.Completions.Num(), 3);
	TestEqual(TEXT("Names the next present player"), Third.RecipientKey, AfterHost);
	TestEqual(TEXT("Named on the record"), Gathering.Completions[2].RecipientKey, Third.RecipientKey);
	TestEqual(TEXT("New recipient holds one"), Gathering.MaterialOf(Third.RecipientKey), 1);
	TestEqual(TEXT("Host still holds two"), Gathering.MaterialOf(TEXT("H")), 2);
	TestTrue(TEXT("Still jailed"), Players[2].bJailed);
	TestTrue(TEXT("Network after the third"), SameNetwork(Before, Players));

	// Spawn still does not tame. Spending is separate, and one spend leaves the spare material.
	TestTrue(TEXT("Spawns"), Gathering.NotifySpawned(GFUnbindingIds::GiantDisplayName()));
	TestEqual(TEXT("No automatic tame"), Gathering.Tamers().Num(), 0);
	const FGFUnbindingResult Spent = Gathering.AttemptTame(TEXT("H"));
	TestTrue(TEXT("Host spends one"), Spent.bOk && Spent.bConsumed);
	TestEqual(TEXT("One left"), Gathering.MaterialOf(TEXT("H")), 1);
	TestTrue(TEXT("Host tracked"), Gathering.HasTamed(TEXT("H")));
	TestFalse(TEXT("The other recipient has not tamed"), Gathering.HasTamed(Third.RecipientKey));
	TestEqual(TEXT("Their material is still there"), Gathering.MaterialOf(Third.RecipientKey), 1);

	const FGFUnbindingResult Fourth = Gathering.TryComplete(Players);
	TestTrue(TEXT("Still repeatable after a tame"), Fourth.bOk);
	TestEqual(TEXT("Four completions"), Gathering.Completions.Num(), 4);
	TestEqual(TEXT("Names the jailed player, still without releasing them"), Fourth.RecipientKey, Guest);
	TestEqual(TEXT("That player holds one"), Gathering.MaterialOf(Guest), 1);
	TestTrue(TEXT("Everyone still here"), SameNetwork(Before, Players));
	TestTrue(TEXT("Jailed player was not released"), Players[2].bJailed);
	return true;
}

#endif
