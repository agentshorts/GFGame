// Pebble Mine (PLAN.md M8): shy pebblings unlock after the first tame, each mines only its assigned room and
// carries the haul home on order (or by itself with Auto-Haul), upgrades change the numbers, offline gathering
// is capped and deterministic, and the mine stays well below the main gathering loop. All pure (no world).
#include "Misc/AutomationTest.h"
#include "GFMineTypes.h"
#include "GFEconomy.h"
#include "GFRewards.h"
#include "GFItemTypes.h"
#include "GFDataTables.h"
#include "Engine/DataTable.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFMineTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace GFMineTestUtil
{
	static const UDataTable* Items() { return FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items")); }

	static int32 Total(const TArray<FGFItemStack>& S)
	{
		int32 N = 0;
		for (const FGFItemStack& X : S)
		{
			N += X.Count;
		}
		return N;
	}

	static bool OnlyFrom(FName Room, const TArray<FGFItemStack>& S)
	{
		const FGFMineRoomRow* R = FGFMineData::Get().Room(Room);
		for (const FGFItemStack& X : S)
		{
			if (!R || !R->Yields.ContainsByPredicate([&](const FGFRewardEntry& E) { return E.ItemId == X.ItemId; }))
			{
				return false;
			}
		}
		return true;
	}

	static void Run(FGFMineCrew& Crew, float Seconds, FRandomStream& Rng, TArray<FGFItemStack>& Home, float Step = 0.5f)
	{
		for (float T = 0.f; T < Seconds; T += Step)
		{
			FGFMineRules::Tick(Crew, Step, Rng, Home);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineCaptureTest, "GFGame.Mine.Capture", GFMineTestFlags)
bool FGFMineCaptureTest::RunTest(const FString&)
{
	FGFPlayerProgress P;
	TestTrue(TEXT("Locked before the player's first tame"), FGFMineRules::CheckCapture(P, 50.f) == EGFMineOrder::Locked);
	P.bHasTamed = true;
	TestTrue(TEXT("Rushing in startles it"), FGFMineRules::CheckCapture(P, 400.f) == EGFMineOrder::Startled);
	TestTrue(TEXT("Sneaking up works"), FGFMineRules::CheckCapture(P, 150.f) == EGFMineOrder::Ok);
	const FGFPebbling& A = FGFMineRules::AddPebbling(P.Crew);
	TestTrue(TEXT("Named and waiting at the mouth"), !A.Name.IsNone() && A.State == EGFPebblingState::Home && A.Id.IsValid());
	FGFMineRules::AddPebbling(P.Crew);
	TestTrue(TEXT("Crew of 2 to start"), FGFMineRules::CheckCapture(P, 100.f) == EGFMineOrder::CrewFull);
	P.Crew.SetLevel(GFMineUpgrades::CrewQuarters, 2);
	TestTrue(TEXT("Crew Quarters make room"), FGFMineRules::CheckCapture(P, 100.f) == EGFMineOrder::Ok);
	TestEqual(TEXT("Crew cap 4 at most"), FGFMineStats::For(P.Crew).CrewCap, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineCrewTest, "GFGame.Mine.Crew", GFMineTestFlags)
bool FGFMineCrewTest::RunTest(const FString&)
{
	using namespace GFMineTestUtil;
	FGFMineCrew Crew;
	const FGuid A = FGFMineRules::AddPebbling(Crew).Id;
	const FGuid B = FGFMineRules::AddPebbling(Crew).Id;
	TestTrue(TEXT("Crystal Gallery needs Tunnels"), FGFMineRules::Assign(Crew, A, TEXT("CrystalGallery")) == EGFMineOrder::RoomLocked);
	TestTrue(TEXT("Unknown room"), FGFMineRules::Assign(Crew, A, TEXT("Nowhere")) == EGFMineOrder::UnknownRoom);
	TestTrue(TEXT("A to the Shallows"), FGFMineRules::Assign(Crew, A, TEXT("Shallows")) == EGFMineOrder::Ok);
	Crew.SetLevel(GFMineUpgrades::Lanterns, 1);
	TestTrue(TEXT("B to the Glow Grotto"), FGFMineRules::Assign(Crew, B, TEXT("GlowGrotto")) == EGFMineOrder::Ok);
	TestTrue(TEXT("Walking there"), Crew.Find(A)->State == EGFPebblingState::Travel);

	FRandomStream Rng(5);
	TArray<FGFItemStack> Home;
	Run(Crew, 20.f + 60.f + 1.f, Rng, Home);
	TestTrue(TEXT("Digging after one load"), Crew.Find(A)->State == EGFPebblingState::Work && Crew.Find(A)->Loads == 1);
	Run(Crew, 3600.f, Rng, Home);
	TestTrue(TEXT("Satchel full: waits for the player's order"), Crew.Find(A)->State == EGFPebblingState::Full && Crew.Find(B)->State == EGFPebblingState::Full);
	TestEqual(TEXT("Nothing comes home without an order"), Home.Num(), 0);
	TestTrue(TEXT("A mined only the Shallows"), OnlyFrom(TEXT("Shallows"), Crew.Find(A)->Carry));
	TestTrue(TEXT("B mined only the Glow Grotto"), OnlyFrom(TEXT("GlowGrotto"), Crew.Find(B)->Carry));
	TestTrue(TEXT("Full = at least 12"), Total(Crew.Find(A)->Carry) >= 12);

	const TArray<FGFItemStack> ACarry = Crew.Find(A)->Carry;
	TestTrue(TEXT("Haul order"), FGFMineRules::Haul(Crew, A) == EGFMineOrder::Ok);
	Run(Crew, 10.f, Rng, Home);
	TestEqual(TEXT("Still on the way"), Home.Num(), 0);
	Run(Crew, 11.f, Rng, Home);
	TestEqual(TEXT("A's haul arrives after the walk"), Total(Home), Total(ACarry));
	TestTrue(TEXT("A walks back to its room"), Crew.Find(A)->State == EGFPebblingState::Return && Crew.Find(A)->Carry.Num() == 0);
	TestTrue(TEXT("B still waits"), Crew.Find(B)->State == EGFPebblingState::Full);
	TestTrue(TEXT("Nothing to haul from an empty satchel"), FGFMineRules::Haul(Crew, A) == EGFMineOrder::NothingToHaul);

	// Automation: with Auto-Haul the full satchel goes home by itself.
	Crew.SetLevel(GFMineUpgrades::AutoHaul, 1);
	Home.Reset();
	Run(Crew, 21.f, Rng, Home);
	TestTrue(TEXT("Auto-Haul delivers B's load"), Total(Home) >= 12 && OnlyFrom(TEXT("GlowGrotto"), Home));

	// Reassigning never mixes rooms: the old load goes home first.
	Run(Crew, 200.f, Rng, Home);
	FGFPebbling* PA = Crew.Find(A);
	PA->State = EGFPebblingState::Work;
	PA->Carry = { FGFItemStack(TEXT("Stone"), 3) };
	Home.Reset();
	TestTrue(TEXT("A to the Glow Grotto"), FGFMineRules::Assign(Crew, A, TEXT("GlowGrotto")) == EGFMineOrder::Ok);
	TestTrue(TEXT("Carries the Shallows load home first"), PA->State == EGFPebblingState::Carry);
	Run(Crew, 21.f, Rng, Home);
	TestEqual(TEXT("Old load delivered"), FGFRewards::CountOf(Home, TEXT("Stone")), 3);
	TestEqual(TEXT("Then heads to the new room"), PA->Room, FName(TEXT("GlowGrotto")));

	TestTrue(TEXT("Recall"), FGFMineRules::Recall(Crew, B) == EGFMineOrder::Ok);
	Run(Crew, 30.f, Rng, Home);
	TestTrue(TEXT("Back at the mouth"), Crew.Find(B)->State == EGFPebblingState::Home && Crew.Find(B)->Room.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineUpgradeTest, "GFGame.Mine.Upgrades", GFMineTestFlags)
bool FGFMineUpgradeTest::RunTest(const FString&)
{
	using namespace GFMineTestUtil;
	const FGFMineData& D = FGFMineData::Get();
	TestEqual(TEXT("Three rooms"), D.Rooms.Num(), 3);
	const UDataTable* ItemsT = Items();
	for (const FGFMineUpgradeRow& R : D.Upgrades)
	{
		TestTrue(*FString::Printf(TEXT("%s %d costs materials"), *R.Upgrade.ToString(), R.Level), R.Cost.Num() > 0);
		for (const FGFItemStack& C : R.Cost)
		{
			TestNotNull(*FString::Printf(TEXT("%s is a real item"), *C.ItemId.ToString()), FGFEconomy::FindItem(ItemsT, C.ItemId));
		}
		for (int32 L = 1; L < R.Level; ++L)
		{
			TestNotNull(*FString::Printf(TEXT("%s has every level below %d"), *R.Upgrade.ToString(), R.Level), D.Upgrade(R.Upgrade, L));
		}
	}
	FGFMineCrew Crew;
	EGFMineOrder Why;
	const FGFMineUpgradeRow* Next = FGFMineRules::NextUpgrade(Crew, GFMineUpgrades::Picks, Why);
	TestTrue(TEXT("Picks 1 next"), Next && Next->Level == 1);
	Crew.SetLevel(GFMineUpgrades::Picks, 3);
	TestNull(TEXT("Picks max 3"), FGFMineRules::NextUpgrade(Crew, GFMineUpgrades::Picks, Why));
	TestTrue(TEXT("MaxLevel"), Why == EGFMineOrder::MaxLevel);
	TestNull(TEXT("Unknown"), FGFMineRules::NextUpgrade(Crew, TEXT("Rockets"), Why));
	Crew.SetLevel(GFMineUpgrades::Satchels, 2);
	Crew.SetLevel(GFMineUpgrades::Rails, 2);
	Crew.SetLevel(GFMineUpgrades::NightShift, 2);
	Crew.SetLevel(GFMineUpgrades::AutoHaul, 1);
	const FGFMineStats S = FGFMineStats::For(Crew);
	TestTrue(TEXT("Picks: 60 s -> 34 s"), FMath::IsNearlyEqual(S.WorkSeconds, 60.f / 1.75f, 0.01f));
	TestEqual(TEXT("Satchels: 28"), S.CarryCap, 28);
	TestTrue(TEXT("Rails: 20 s -> 8 s"), FMath::IsNearlyEqual(S.TravelSeconds, 8.f, 0.01f));
	TestTrue(TEXT("Night Shift: 12 h cap"), FMath::IsNearlyEqual(S.OfflineCapHours, 12.f));
	TestTrue(TEXT("Auto-Haul: offline at 75 %"), FMath::IsNearlyEqual(S.OfflineRate, 0.75f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineOfflineTest, "GFGame.Mine.Offline", GFMineTestFlags)
bool FGFMineOfflineTest::RunTest(const FString&)
{
	using namespace GFMineTestUtil;
	const UDataTable* ItemsT = Items();
	FGFMineCrew Crew;
	const FGuid A = FGFMineRules::AddPebbling(Crew).Id;
	FGFMineRules::AddPebbling(Crew); // stays home: gathers nothing
	FGFMineRules::Assign(Crew, A, TEXT("Shallows"));

	const FGFMineOffline H1 = FGFMineRules::Offline(Crew, 3600.0, 11, ItemsT);
	TestTrue(TEXT("An hour away brings something"), H1.Value > 0 && FMath::IsNearlyEqual(H1.Hours, 1.f));
	TestTrue(TEXT("Half rate: about 30 loads' worth or less"), H1.Value <= FMath::CeilToInt(FGFMineRules::ExpectedLoadValue(TEXT("Shallows"), ItemsT) * 30.f * 1.5f));
	TestTrue(TEXT("Only its room"), OnlyFrom(TEXT("Shallows"), H1.Items));
	const FGFMineOffline Again = FGFMineRules::Offline(Crew, 3600.0, 11, ItemsT);
	TestTrue(TEXT("Deterministic"), Again.Value == H1.Value && Total(Again.Items) == Total(H1.Items));
	const FGFMineOffline Week = FGFMineRules::Offline(Crew, 7.0 * 86400.0, 11, ItemsT);
	TestTrue(TEXT("Capped at 4 hours"), FMath::IsNearlyEqual(Week.Hours, 4.f));
	TestTrue(TEXT("Never above 150 per capped hour"), Week.Value <= 600);
	TestEqual(TEXT("Clock moved back: nothing"), FGFMineRules::Offline(Crew, -500.0, 11, ItemsT).Value, 0);

	// A big crew with every upgrade still respects the value cap.
	Crew.SetLevel(GFMineUpgrades::CrewQuarters, 2);
	Crew.SetLevel(GFMineUpgrades::Lanterns, 1);
	Crew.SetLevel(GFMineUpgrades::NightShift, 2);
	Crew.SetLevel(GFMineUpgrades::AutoHaul, 1);
	Crew.SetLevel(GFMineUpgrades::Picks, 3);
	while (Crew.Pebblings.Num() < 4)
	{
		FGFMineRules::AddPebbling(Crew);
	}
	for (const FGFPebbling& P : TArray<FGFPebbling>(Crew.Pebblings))
	{
		FGFMineRules::Assign(Crew, P.Id, TEXT("GlowGrotto"));
	}
	const FGFMineOffline Max = FGFMineRules::Offline(Crew, 30.0 * 3600.0, 3, ItemsT);
	AddInfo(FString::Printf(TEXT("Max crew offline: %.0f h, value %d"), Max.Hours, Max.Value));
	TestTrue(TEXT("12 h cap"), FMath::IsNearlyEqual(Max.Hours, 12.f));
	TestTrue(TEXT("Value cap 1800"), Max.Value <= 1800 && Max.Value > 1500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineEconomyTest, "GFGame.Mine.Economy", GFMineTestFlags)
bool FGFMineEconomyTest::RunTest(const FString&)
{
	using namespace GFMineTestUtil;
	const UDataTable* ItemsT = Items();
	// Reference: hand-gathering in the hub is ~80 value a minute (PLAN.md section 2). The mine must stay <= 60 %.
	constexpr float Reference = 80.f;
	FGFMineCrew Start;
	FGFMineCrew Maxed;
	for (const FName U : { GFMineUpgrades::Picks, GFMineUpgrades::Satchels, GFMineUpgrades::Rails, GFMineUpgrades::CrewQuarters, GFMineUpgrades::Tunnels, GFMineUpgrades::Lanterns, GFMineUpgrades::AutoHaul, GFMineUpgrades::NightShift })
	{
		Maxed.SetLevel(U, FGFMineData::Get().MaxLevel(U));
	}
	const float A = FGFMineRules::MaxOnlineValuePerMinute(Start, ItemsT);
	const float B = FGFMineRules::MaxOnlineValuePerMinute(Maxed, ItemsT);
	AddInfo(FString::Printf(TEXT("Mine value/min: start %.1f, fully upgraded %.1f (limit %.0f)"), A, B, Reference * 0.6f));
	TestTrue(TEXT("Upgrades help"), B > A * 2.f);
	TestTrue(TEXT("Fully upgraded mine <= 60 % of hand gathering"), B <= Reference * 0.6f);
	// Every room yields only materials, never money.
	for (const TPair<FName, FGFMineRoomRow>& R : FGFMineData::Get().Rooms)
	{
		for (const FGFRewardEntry& E : R.Value.Yields)
		{
			const FGFItemRow* I = FGFEconomy::FindItem(ItemsT, E.ItemId);
			TestTrue(*FString::Printf(TEXT("%s yields real materials"), *R.Key.ToString()), I && (I->Category == EGFItemCategory::Material || I->Category == EGFItemCategory::Treat));
			TestTrue(*FString::Printf(TEXT("%s: no mode exclusives"), *R.Key.ToString()), I && I->Exclusive == EGFExclusive::None);
		}
	}
	return true;
}

#endif
