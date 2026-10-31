// Arena rules port: spell table, spark, hot sand, the 2 s lock, roster caps, rep, purse,
// anti-cheese, side pots, rental quits, ghost orders and a seed-901 replay.
// No golden hash: this agent cannot run the sim, so the replay has to match itself.
#include "Misc/AutomationTest.h"
#include "GFArenaRules.h"
#include "GFArenaTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFArenaRulesFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFArenaRulesTest
{
	static FGFArenaEntrant Dino(const FString& Owner, FName Species, int32 Level, int32 Spot)
	{
		FGFArenaEntrant E;
		E.OwnerKey = Owner;
		E.Species = Species;
		E.Level = Level;
		E.Spot = Spot;
		E.CompanionId = FGuid::NewGuid();
		return E;
	}

	static void Fill(FGFArenaSim& Sim, const FGFArenaData& Data, int32 Seed, EGFArenaAI Team1)
	{
		Sim.Init(Seed, EGFArenaAI::Normal, Team1);
		Sim.AddUnit(Data, 0, Dino(TEXT("A"), TEXT("Rex"), 1, 1));
		Sim.AddUnit(Data, 0, Dino(TEXT("A"), TEXT("Raptor"), 1, 4));
		Sim.AddUnit(Data, 1, Dino(FString(), TEXT("Hornback"), 1, 1));
		Sim.AddUnit(Data, 1, Dino(FString(), TEXT("Sailback"), 1, 4));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaSpellTableTest, "GFGame.Arena.Rules.Spells", GFArenaRulesFlags)
bool FGFArenaSpellTableTest::RunTest(const FString&)
{
	const FGFArenaData& Data = FGFArenaData::Get();
	TestEqual(TEXT("Eight spells"), Data.SpellOrder.Num(), 8);
	TestEqual(TEXT("SparkBolt cost"), Data.Spell(TEXT("SparkBolt")) ? Data.Spell(TEXT("SparkBolt"))->Cost : -1, 2);
	TestEqual(TEXT("FizzBomb cost"), Data.Spell(TEXT("FizzBomb")) ? Data.Spell(TEXT("FizzBomb"))->Cost : -1, 4);
	TestEqual(TEXT("RallyWhistle windows"), Data.Spell(TEXT("RallyWhistle")) ? Data.Spell(TEXT("RallyWhistle"))->WindowCooldown : -1, 2);
	TestTrue(TEXT("StoneWall blocks a lane"), Data.Spell(TEXT("StoneWall")) && Data.Spell(TEXT("StoneWall"))->bLaneBlock);
	const float Bolt = FGFArenaMath::SpellDamage(100.f, 3, false, false);
	TestEqual(TEXT("3 points is 24 percent of max HP"), Bolt, 24.f, 0.01f);

	FGFArenaSim Sim;
	Sim.Init(3, EGFArenaAI::Normal, EGFArenaAI::Normal);
	Sim.AddUnit(Data, 0, GFArenaRulesTest::Dino(TEXT("A"), TEXT("Rex"), 1, 1));
	const int32 Foe = Sim.AddUnit(Data, 1, GFArenaRulesTest::Dino(FString(), TEXT("Hornback"), 1, 1));
	TestEqual(TEXT("Spark starts at 3"), Sim.Spark[0], 3);
	TestEqual(TEXT("No window"), Sim.CastSpell(Data, 0, TEXT("SparkBolt"), Foe, false, true).ToString(), FString(TEXT("NoSlowTime")));
	Sim.bSpellsOpen = true;
	TestEqual(TEXT("Not in the loadout"), Sim.CastSpell(Data, 0, TEXT("SparkBolt"), Foe, false, true).ToString(), FString(TEXT("NoLoadout")));
	Sim.SetLoadout(0, { TEXT("SparkBolt"), TEXT("SparkBolt"), TEXT("BloomHeal"), TEXT("FizzBomb"), TEXT("VineSnare") });
	TestEqual(TEXT("Loadout keeps four unique names"), Sim.Loadout0.Num(), 4);
	TestFalse(TEXT("The fifth spell is left out"), Sim.Loadout0.Contains(TEXT("VineSnare")));
	Sim.Spark[0] = 1;
	TestEqual(TEXT("Not enough spark"), Sim.CastSpell(Data, 0, TEXT("SparkBolt"), Foe, false, true).ToString(), FString(TEXT("NoSpark")));
	Sim.Spark[0] = 3;
	const float Before = Sim.Find(Foe)->HP;
	TestEqual(TEXT("SparkBolt lands"), Sim.CastSpell(Data, 0, TEXT("SparkBolt"), Foe, false, true).ToString(), FString(TEXT("Ok")));
	TestTrue(TEXT("Bolt spent HP"), Sim.Find(Foe)->HP < Before - 1.f);
	TestEqual(TEXT("Spark spent"), Sim.Spark[0], 1);

	FGFArenaSim Whistle;
	Whistle.Init(4, EGFArenaAI::Champion, EGFArenaAI::Normal);
	Whistle.AddUnit(Data, 0, GFArenaRulesTest::Dino(TEXT("A"), TEXT("Rex"), 1, 0));
	Whistle.bSpellsOpen = true;
	Whistle.SetLoadout(0, { TEXT("RallyWhistle") });
	TestEqual(TEXT("Whistle"), Whistle.CastSpell(Data, 0, TEXT("RallyWhistle"), INDEX_NONE, false, true).ToString(), FString(TEXT("Ok")));
	TestEqual(TEXT("Whistle again is cooling"), Whistle.CastSpell(Data, 0, TEXT("RallyWhistle"), INDEX_NONE, false, false).ToString(), FString(TEXT("Cooldown")));
	Whistle.OnSlowWindowEnded();
	TestEqual(TEXT("One window is not enough"), Whistle.CastSpell(Data, 0, TEXT("RallyWhistle"), INDEX_NONE, false, false).ToString(), FString(TEXT("Cooldown")));
	Whistle.OnSlowWindowEnded();
	TestEqual(TEXT("Two windows clear the lock"), Whistle.CastSpell(Data, 0, TEXT("RallyWhistle"), INDEX_NONE, false, false).ToString(), FString(TEXT("NoSpark")));

	FGFArenaSim Wall;
	Wall.Init(5, EGFArenaAI::Normal, EGFArenaAI::Normal);
	const int32 Mover = Wall.AddUnit(Data, 0, GFArenaRulesTest::Dino(TEXT("A"), TEXT("Nipper"), 1, 1));
	const int32 Block = Wall.AddUnit(Data, 1, GFArenaRulesTest::Dino(FString(), TEXT("Hornback"), 1, 1));
	Wall.Find(Block)->HP = Wall.Find(Block)->MaxHP = 100000.f;
	Wall.Find(Block)->Status[static_cast<int32>(EGFArenaStatus::Stun)] = 30.f;
	Wall.SetLoadout(0, { TEXT("StoneWall") });
	TestEqual(TEXT("Wall"), Wall.CastSpell(Data, 0, TEXT("StoneWall"), Block, false, false).ToString(), FString(TEXT("Ok")));
	TestEqual(TEXT("Possess the nipper"), Wall.Possess(TEXT("A"), Mover).ToString(), FString(TEXT("Ok")));
	TestEqual(TEXT("Push across"), Wall.SetInput(TEXT("A"), FVector2D(1.f, 0.f), false, INDEX_NONE).ToString(), FString(TEXT("Ok")));
	for (int32 i = 0; i < 40 && !Wall.IsOver(); ++i)
	{
		Wall.Step(Data, FGFArenaSim::StepSeconds);
	}
	TestTrue(TEXT("The wall keeps them on their own side"), Wall.Find(Mover)->Pos.X <= 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaSparkSandTest, "GFGame.Arena.Rules.SparkAndSand", GFArenaRulesFlags)
bool FGFArenaSparkSandTest::RunTest(const FString&)
{
	const FGFArenaData& Data = FGFArenaData::Get();
	FGFArenaSim Sim;
	Sim.Init(8, EGFArenaAI::Normal, EGFArenaAI::Normal);
	const int32 A = Sim.AddUnit(Data, 0, GFArenaRulesTest::Dino(TEXT("A"), TEXT("Rex"), 1, 0));
	const int32 B = Sim.AddUnit(Data, 1, GFArenaRulesTest::Dino(FString(), TEXT("Sailback"), 1, 0));
	Sim.Find(A)->HP = Sim.Find(A)->MaxHP = 100000.f;
	Sim.Find(B)->HP = Sim.Find(B)->MaxHP = 100000.f;
	Sim.Find(A)->Status[static_cast<int32>(EGFArenaStatus::Stun)] = 60.f;
	Sim.Find(B)->Status[static_cast<int32>(EGFArenaStatus::Stun)] = 60.f;

	const int32 StepsToSpark = FMath::CeilToInt(FGFArenaSim::SparkEverySeconds / FGFArenaSim::StepSeconds);
	for (int32 i = 0; i < StepsToSpark; ++i)
	{
		Sim.Step(Data, FGFArenaSim::StepSeconds);
	}
	TestEqual(TEXT("Spark ticks up"), Sim.Spark[0], FGFArenaSim::SparkStart + 1);
	while (Sim.Spark[0] < FGFArenaSim::SparkCap && Sim.Time < 40.f && !Sim.IsOver())
	{
		Sim.Step(Data, FGFArenaSim::StepSeconds);
	}
	TestEqual(TEXT("Spark caps at 8"), Sim.Spark[0], FGFArenaSim::SparkCap);
	for (int32 i = 0; i < StepsToSpark; ++i)
	{
		Sim.Step(Data, FGFArenaSim::StepSeconds);
	}
	TestEqual(TEXT("Spark stays capped"), Sim.Spark[0], FGFArenaSim::SparkCap);

	FGFArenaSim Sand;
	Sand.Init(9, EGFArenaAI::Normal, EGFArenaAI::Normal);
	const int32 Left = Sand.AddUnit(Data, 0, GFArenaRulesTest::Dino(TEXT("A"), TEXT("Rex"), 1, 0));
	const int32 Right = Sand.AddUnit(Data, 1, GFArenaRulesTest::Dino(FString(), TEXT("Rex"), 1, 0));
	for (const int32 Id : { Left, Right })
	{
		Sand.Find(Id)->HP = Sand.Find(Id)->MaxHP = 100000.f;
		Sand.Find(Id)->Status[static_cast<int32>(EGFArenaStatus::Stun)] = 80.f;
	}
	while (Sand.Time < FGFArenaSim::HotSandSeconds - Sand.StepSeconds && !Sand.IsOver())
	{
		Sand.Step(Data, FGFArenaSim::StepSeconds);
	}
	TestTrue(TEXT("Quiet sand is still cool"), Sand.Find(Left)->Status[static_cast<int32>(EGFArenaStatus::Burn)] <= 0.f);
	Sand.Step(Data, FGFArenaSim::StepSeconds);
	TestTrue(TEXT("Hot sand burns anyone still standing"), Sand.Find(Left)->Status[static_cast<int32>(EGFArenaStatus::Burn)] > 0.f);
	Sand.Find(Left)->Status[static_cast<int32>(EGFArenaStatus::Burn)] = 0.f;
	Sand.LastDamageTime = Sand.Time;
	Sand.Step(Data, FGFArenaSim::StepSeconds);
	TestTrue(TEXT("A fresh hit resets the sand"), Sand.Find(Left)->Status[static_cast<int32>(EGFArenaStatus::Burn)] <= 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaRosterRepTest, "GFGame.Arena.Rules.RosterRepPurse", GFArenaRulesFlags)
bool FGFArenaRosterRepTest::RunTest(const FString&)
{
	using FGFArenaRosterRules;
	TestEqual(TEXT("Solo cap"), PerPlayerCap(1), 3);
	TestEqual(TEXT("Pair cap"), PerPlayerCap(2), 3);
	TestEqual(TEXT("Three players split the six"), PerPlayerCap(3), 2);
	TestEqual(TEXT("Six players bring one"), PerPlayerCap(6), 1);
	TestEqual(TEXT("Party split"), CheckPlacement(3, 0, {}, { TEXT("Rex"), TEXT("Rex"), TEXT("Nipper") }).ToString(), FString(TEXT("PartySplit")));
	TestEqual(TEXT("Side is full"), CheckPlacement(1, 6, { TEXT("Rex"), TEXT("Rex"), TEXT("Nipper"), TEXT("Nipper"), TEXT("Raptor"), TEXT("Raptor") }, { TEXT("Sailback") }).ToString(), FString(TEXT("TeamFull")));
	TestEqual(TEXT("Species cap"), CheckPlacement(1, 2, { TEXT("Rex"), TEXT("Rex") }, { TEXT("Rex") }).ToString(), FString(TEXT("SpeciesCap")));
	TestEqual(TEXT("Two of a kind still fit"), CheckPlacement(1, 0, {}, { TEXT("Rex"), TEXT("Rex") }).ToString(), FString(TEXT("Ok")));

	const TArray<FName> Species = { TEXT("Nipper"), TEXT("Raptor"), TEXT("Sailback") };
	TMap<FName, int32> Used;
	Used.Add(TEXT("Nipper"), 2);
	TestEqual(TEXT("Pick walks past a full species"), PickSpecies(Species, Used, 0).ToString(), FString(TEXT("Raptor")));

	TestEqual(TEXT("Friendly"), static_cast<int32>(FGFArenaRep::Standing(20)), static_cast<int32>(EGFArenaStanding::Friendly));
	TestEqual(TEXT("Neutral"), static_cast<int32>(FGFArenaRep::Standing(0)), static_cast<int32>(EGFArenaStanding::Neutral));
	TestEqual(TEXT("Wary"), static_cast<int32>(FGFArenaRep::Standing(-8)), static_cast<int32>(EGFArenaStanding::Wary));
	TestEqual(TEXT("Hostile beats wary"), static_cast<int32>(FGFArenaRep::Standing(-40)), static_cast<int32>(EGFArenaStanding::Hostile));
	TestFalse(TEXT("Hostile will not rent"), FGFArenaRep::CanRent(-40));
	TestEqual(TEXT("Wary price"), FGFArenaRep::RentalPriceMul(-8), 1.25f, 0.001f);
	TestTrue(TEXT("Friendly arrives fed"), FGFArenaRep::ArrivesWellFed(20));
	TestFalse(TEXT("Wary cannot be steered"), FGFArenaRep::CanControl(-8));
	TestTrue(TEXT("Hostile rival is a champion"), FGFArenaRep::ForcesChampion(-40));
	TestEqual(TEXT("Win, quit, insult"), FGFArenaRep::WinDelta + FGFArenaRep::QuitDelta + FGFArenaRep::InsultDelta, -3);

	TestEqual(TEXT("Purse is 5 percent"), FGFArenaPurse::Purse(100), 5);
	TestEqual(TEXT("Purse rounds down"), FGFArenaPurse::Purse(19), 0);
	TestEqual(TEXT("Elder cut is 10 percent"), FGFArenaPurse::ElderCut(100), 10);
	TestEqual(TEXT("Snack needs a favourite"), FGFArenaSnack::Check(true, false, true, false).ToString(), FString(TEXT("Refused")));
	TestEqual(TEXT("Snack once"), FGFArenaSnack::Check(true, true, true, true).ToString(), FString(TEXT("AlreadyFed")));
	TestEqual(TEXT("Brag kind"), FGFArenaPost::Brag(3).Kind.ToString(), FString(TEXT("Brag")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaCheesePotTest, "GFGame.Arena.Rules.CheeseAndPots", GFArenaRulesFlags)
bool FGFArenaCheesePotTest::RunTest(const FString&)
{
	const TArray<double> Two = { 10.0, 20.0 };
	const TArray<double> Three = { 10.0, 20.0, 30.0 };
	TestFalse(TEXT("Two wagers still leave room for a third"), FGFArenaCheese::WagersBlocked(Two, 25.0));
	TestTrue(TEXT("Three recorded wagers block the next one"), FGFArenaCheese::WagersBlocked(Three, 40.0));
	TestFalse(TEXT("An old pair can wager again"), FGFArenaCheese::WagersBlocked(Three, 30.0 + FGFArenaCheese::HourSeconds));
	TestEqual(TEXT("Tenth roll still uses the stars"), FGFArenaCheese::RollStars(3, 9), 3);
	TestEqual(TEXT("Past ten rolls the table is empty"), FGFArenaCheese::RollStars(3, 10), 0);

	TArray<FGFArenaSideBet> Bets;
	Bets.Add({ TEXT("Moth"), 0, 10 });
	Bets.Add({ TEXT("Ant"), 0, 25 });
	Bets.Add({ TEXT("Beetle"), 1, 40 });
	TestEqual(TEXT("Draw sends the pot home"), FGFArenaSidePots::PotWinner(EGFArenaOutcome::Draw, Bets), FString());
	TestEqual(TEXT("Largest stake on the winning side"), FGFArenaSidePots::PotWinner(EGFArenaOutcome::Team0Won, Bets), FString(TEXT("Ant")));
	Bets[0].Value = 25;
	TestEqual(TEXT("A value tie prefers the earlier key"), FGFArenaSidePots::PotWinner(EGFArenaOutcome::Team0Won, Bets), FString(TEXT("Ant")));
	Bets[0].Key = TEXT("Aardvark");
	TestEqual(TEXT("Same value, smaller key"), FGFArenaSidePots::PotWinner(EGFArenaOutcome::Team0Won, Bets), FString(TEXT("Aardvark")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArenaBrainParityTest, "GFGame.Arena.Rules.Brain", GFArenaRulesFlags)
bool FGFArenaBrainParityTest::RunTest(const FString&)
{
	using namespace GFArenaRulesTest;
	const FGFArenaData& Data = FGFArenaData::Get();

	FGFArenaSim Quit;
	Quit.Init(11, EGFArenaAI::Normal, EGFArenaAI::Normal);
	FGFArenaEntrant Hired = Dino(TEXT("A"), TEXT("Nipper"), 1, 0);
	Hired.bRental = true;
	Hired.Family = TEXT("Scale");
	const int32 QuitId = Quit.AddUnit(Data, 0, Hired);
	Quit.AddUnit(Data, 1, Dino(FString(), TEXT("Rex"), 1, 0));
	Quit.Find(QuitId)->HP = 0.5f;
	Quit.Find(QuitId)->Status[static_cast<int32>(EGFArenaStatus::Burn)] = 5.f;
	Quit.Step(Data, FGFArenaSim::StepSeconds);
	TestEqual(TEXT("A rental at 0 HP posts a quit"), Quit.Quits.Num(), 1);
	TestEqual(TEXT("Quit names the species"), Quit.Quits[0].Species.ToString(), FString(TEXT("Nipper")));
	TestTrue(TEXT("And it is down"), Quit.Find(QuitId)->bFainted);

	FGFArenaSim Ghosts;
	Fill(Ghosts, Data, 15, EGFArenaAI::Normal);
	TArray<FGFArenaCommand> First;
	TArray<FGFArenaCommand> Second;
	Ghosts.BuildGhostOrders(Data, 0, First);
	Ghosts.BuildGhostOrders(Data, 0, Second);
	TestTrue(TEXT("Ghosts exist"), First.Num() > 0);
	TestEqual(TEXT("Ghost count is stable"), First.Num(), Second.Num());
	for (int32 i = 0; i < First.Num() && i < Second.Num(); ++i)
	{
		TestEqual(TEXT("Same ghost dino"), First[i].UnitId, Second[i].UnitId);
		TestEqual(TEXT("Same ghost order"), First[i].Slot, Second[i].Slot);
	}

	TArray<FGFArenaPlan> Plans;
	TArray<uint32> Recorded;
	TArray<uint32> Replayed;
	FGFArenaSim Live;
	Fill(Live, Data, 901, EGFArenaAI::Normal);
	Live.PlanLog = &Plans;
	Live.HashLog = &Recorded;
	const EGFArenaOutcome FirstOutcome = Live.RunToEnd(Data);
	const uint32 FirstHash = Live.StateHash();
	TestTrue(TEXT("The brain wrote a plan"), Plans.Num() > 0);
	TestTrue(TEXT("The brain wrote hashes"), Recorded.Num() > 0);

	FGFArenaSim Replay;
	Fill(Replay, Data, 901, EGFArenaAI::Normal);
	Replay.PlanLog = &Plans;
	Replay.HashLog = &Replayed;
	Replay.bReplayPlans = true;
	const EGFArenaOutcome SecondOutcome = Replay.RunToEnd(Data);
	TestEqual(TEXT("Replay outcome"), static_cast<int32>(SecondOutcome), static_cast<int32>(FirstOutcome));
	TestEqual(TEXT("Replay hash"), Replay.StateHash(), FirstHash);
	TestEqual(TEXT("Hash tape length"), Replayed.Num(), Recorded.Num());
	bool bTape = Replayed.Num() == Recorded.Num();
	for (int32 i = 0; bTape && i < Recorded.Num(); ++i)
	{
		bTape = Replayed[i] == Recorded[i];
	}
	TestTrue(TEXT("Hash tape matches with no tolerance"), bTape);

	FGFArenaSim Clock;
	Fill(Clock, Data, 42, EGFArenaAI::Normal);
	// Design budget is 0.5 ms per dino. The ceiling here is loose because this agent cannot time the editor.
	TestTrue(TEXT("One decision stays under 2 ms"), Clock.DebugDecisionMillis(Data, 1) < 2.0);

	int32 Differ = 0;
	for (int32 Seed = 1; Seed <= 4; ++Seed)
	{
		FGFArenaSim Easy;
		FGFArenaSim Normal;
		Fill(Easy, Data, Seed, EGFArenaAI::Easy);
		Fill(Normal, Data, Seed, EGFArenaAI::Normal);
		Easy.RunToEnd(Data);
		Normal.RunToEnd(Data);
		Differ += (Easy.StateHash() != Normal.StateHash() || Easy.Outcome != Normal.Outcome) ? 1 : 0;
	}
	TestTrue(TEXT("Easy and Normal do not play the same four bouts"), Differ > 0);
	return true;
}

#endif
