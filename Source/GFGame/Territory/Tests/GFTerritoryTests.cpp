// Faction territory. Pure map tests (no world, not run here).
//
// Overlap rule, also written on FGFTerritoryMap:
//   Inside several discs, the closest horizontal center wins.
//   Within FGFTerritoryMap::OverlapTieSlopCm (1 cm) it is a tie: more effective members wins,
//   then the case-insensitive smaller faction id. Array order does not matter.
//   The loser does not own the point and is not safe there. Overlap stays owned; it does not turn neutral.
//   Neutral is the optional disc only, and only on ground no faction owns.
//
// Growth uses FGFSafeRegion::Radius with the base shifted so radius = Base + PerMember * Members
// (the first member counts). A faction with no members has radius 0 and area 0.
// The member cap is FGFTerritoryLimits::CapMembers(session players, seats per faction), not a fixed total.

#include "Misc/AutomationTest.h"
#include "../GFTerritoryMap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTerritoryTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static float ExpectedRadius(float Base, float Per, float MaxRadius, int32 Members)
	{
		if (Members <= 0)
		{
			return 0.f;
		}
		const float Grown = Base + Per * static_cast<float>(Members);
		const float OneMember = FMath::Max(0.f, Base + Per);
		if (MaxRadius > 0.f)
		{
			return FMath::Max(0.f, FMath::Min(Grown, FMath::Max(MaxRadius, OneMember)));
		}
		return FMath::Max(0.f, Grown);
	}

	static FGFTerritoryFaction Disc(FName Id, const FVector& Center, int32 Members)
	{
		FGFTerritoryFaction Faction;
		Faction.Id = Id;
		Faction.Center = Center;
		Faction.Members = Members;
		return Faction;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryGrowthTest, "GFGame.Territory.Growth", GFTerritoryTests::Flags)
bool FGFTerritoryGrowthTest::RunTest(const FString&)
{
	using namespace GFTerritoryTests;

	TestEqual(TEXT("No world, no session cap"), FGFTerritoryLimits::SessionMaxPlayers(nullptr), 0);
	TestEqual(TEXT("No game state, no seat cap"), FGFTerritoryLimits::FactionSeatCap(nullptr), 0);
	TestEqual(TEXT("Missing both caps does not invent a total"), FGFTerritoryLimits::CapMembers(0, 0), 0);
	TestEqual(TEXT("Seat cap alone"), FGFTerritoryLimits::CapMembers(0, 4), 4);
	TestEqual(TEXT("Session cap alone"), FGFTerritoryLimits::CapMembers(6, 0), 6);
	TestEqual(TEXT("Tighter cap wins"), FGFTerritoryLimits::CapMembers(4, 6), 4);
	TestEqual(TEXT("Tighter seat cap wins"), FGFTerritoryLimits::CapMembers(16, 5), 5);

	FGFTerritoryMap Map;
	Map.Curve.BaseRadius = 100.f;
	Map.Curve.RadiusPerMember = 50.f;
	Map.Curve.MaxRadius = 0.f;
	FGFTerritoryFaction Faction = Disc(FGFTerritoryFactions::Verdant, FVector::ZeroVector, 0);
	Map.Factions = { Faction };

	TestEqual(TEXT("No members, no radius"), Map.RadiusOf(Map.Factions[0]), 0.f);
	TestEqual(TEXT("No members, no area"), Map.AreaOf(Map.Factions[0]), 0.f);
	TestFalse(TEXT("No members, no safe land"), Map.HasAnySafe());
	FVector EdgePoint, EdgeNormal;
	TestFalse(TEXT("No members, no edge"), Map.ClosestEdge(FVector::ZeroVector, EdgePoint, EdgeNormal));
	TestEqual(TEXT("No members, closest safe point is the query"), Map.ClosestSafePoint(FVector(20.f, 0.f, 5.f)), FVector(20.f, 0.f, 5.f));

	const int32 Steps[] = { 1, 2, 4 };
	float PreviousArea = 0.f;
	for (int32 Members : Steps)
	{
		Map.Factions[0].Members = Members;
		const float Radius = ExpectedRadius(100.f, 50.f, 0.f, Members);
		TestEqual(*FString::Printf(TEXT("%d members radius"), Members), Map.RadiusOf(Map.Factions[0]), Radius);
		TestEqual(*FString::Printf(TEXT("%d members uses FGFSafeRegion"), Members), Map.RadiusOf(Map.Factions[0]), Map.RegionFor(Map.Factions[0]).Radius());
		const float Area = Map.AreaOf(Map.Factions[0]);
		TestEqual(*FString::Printf(TEXT("%d members area"), Members), double(Area), double(UE_PI * Radius * Radius), 0.5);
		TestTrue(*FString::Printf(TEXT("%d members area grew"), Members), Area > PreviousArea);
		PreviousArea = Area;
	}

	// One member is base + per member. The unshifted safe-region formula would have left this at the base only.
	Map.Factions[0].Members = 1;
	TestEqual(TEXT("First member adds a step"), Map.RadiusOf(Map.Factions[0]), 150.f);
	TestTrue(TEXT("First member is past the base"), Map.RadiusOf(Map.Factions[0]) > Map.Curve.BaseRadius);

	Map.Curve.MaxRadius = 250.f;
	Map.Factions[0].Members = 3;
	TestEqual(TEXT("Cap at three members"), Map.RadiusOf(Map.Factions[0]), ExpectedRadius(100.f, 50.f, 250.f, 3));
	Map.Factions[0].Members = 4;
	TestEqual(TEXT("Further members stay capped"), Map.RadiusOf(Map.Factions[0]), 250.f);
	TestEqual(TEXT("Capped radius still matches FGFSafeRegion"), Map.RadiusOf(Map.Factions[0]), Map.RegionFor(Map.Factions[0]).Radius());

	Map.Curve.MaxRadius = 50.f;
	Map.Factions[0].Members = 1;
	TestEqual(TEXT("Cap cannot shrink the one-member radius"), Map.RadiusOf(Map.Factions[0]), ExpectedRadius(100.f, 50.f, 50.f, 1));

	Map.Curve.MaxRadius = 0.f;
	Map.MaxMembers = FGFTerritoryLimits::CapMembers(4, 6);
	Map.Factions[0].Members = 6;
	TestEqual(TEXT("Player cap stops growth"), Map.RadiusOf(Map.Factions[0]), ExpectedRadius(100.f, 50.f, 0.f, 4));
	const float CappedArea = Map.AreaOf(Map.Factions[0]);
	Map.MaxMembers = 0;
	TestTrue(TEXT("Clearing the cap lets the sixth member grow land"), Map.RadiusOf(Map.Factions[0]) > FMath::Sqrt(CappedArea / UE_PI));

	Map.Factions[0].Members = 2;
	const FVector Inside(0.f, 0.f, 40.f);
	TestTrue(TEXT("Inside the disc"), Map.IsInsideSafe(Inside));
	TestEqual(TEXT("Inside point is already safe"), Map.ClosestSafePoint(Inside), Inside);
	TestTrue(TEXT("Safe query agrees with the zone"), Map.IsInsideSafe(Inside) == (Map.GetZoneAt(Inside).Zone == EGFTerritoryZone::Faction));

	FGFTerritoryMap Defaults;
	Defaults.SetDefaultFactions();
	TestEqual(TEXT("Default faction count"), Defaults.Factions.Num(), 3);
	TestEqual(TEXT("Cinderwrights"), Defaults.Factions[0].Id, FGFTerritoryFactions::Cinderwrights);
	TestEqual(TEXT("Verdant"), Defaults.Factions[1].Id, FGFTerritoryFactions::Verdant);
	TestEqual(TEXT("Rime"), Defaults.Factions[2].Id, FGFTerritoryFactions::Rime);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryNeutralTest, "GFGame.Territory.Neutral", GFTerritoryTests::Flags)
bool FGFTerritoryNeutralTest::RunTest(const FString&)
{
	using namespace GFTerritoryTests;

	FGFTerritoryMap Map;
	Map.SetDefaultFactions();
	for (FGFTerritoryFaction& Faction : Map.Factions)
	{
		Faction.Members = 1;
	}
	Map.Neutral.Center = FVector::ZeroVector;
	Map.Neutral.Radius = 800.f;
	Map.Neutral.bEnabled = false;

	const FVector Hub(0.f, 0.f, 250.f);
	TestTrue(TEXT("Neutral off: hub is wild"), Map.GetZoneAt(Hub).Zone == EGFTerritoryZone::Wild);
	TestFalse(TEXT("Neutral off: hub is not safe land"), Map.IsInsideSafe(Hub));

	Map.Neutral.bEnabled = true;
	TestTrue(TEXT("Neutral on: hub is neutral"), Map.GetZoneAt(Hub).Zone == EGFTerritoryZone::Neutral);
	TestTrue(TEXT("Neutral has no faction id"), Map.GetZoneAt(Hub).FactionId.IsNone());
	TestTrue(TEXT("Neutral disc edge is neutral"), Map.GetZoneAt(FVector(800.f, 0.f, 0.f)).Zone == EGFTerritoryZone::Neutral);
	TestTrue(TEXT("Just outside the disc is wild"), Map.GetZoneAt(FVector(801.f, 0.f, 0.f)).Zone == EGFTerritoryZone::Wild);
	TestFalse(TEXT("Neutral is not the giant's safe union"), Map.IsInsideSafe(Hub));
	TestTrue(TEXT("Height does not change the hub"), Map.GetZoneAt(FVector(0.f, 0.f, 5000.f)).Zone == EGFTerritoryZone::Neutral);

	Map.Factions = { Disc(FGFTerritoryFactions::Rime, FVector::ZeroVector, 1) };
	Map.Curve.BaseRadius = 0.f;
	Map.Curve.RadiusPerMember = 100.f;
	Map.Curve.MaxRadius = 0.f;
	TestTrue(TEXT("Faction land inside the disc stays faction"), Map.GetZoneAt(Hub).Zone == EGFTerritoryZone::Faction);
	TestEqual(TEXT("Owner is Rime"), Map.GetZoneAt(Hub).FactionId, FGFTerritoryFactions::Rime);
	TestTrue(TEXT("Ring of the disc outside the faction is neutral"), Map.GetZoneAt(FVector(400.f, 0.f, 12.f)).Zone == EGFTerritoryZone::Neutral);
	TestTrue(TEXT("Past both is wild"), Map.GetZoneAt(FVector(900.f, 0.f, 0.f)).Zone == EGFTerritoryZone::Wild);

	Map.Neutral.bEnabled = false;
	TestTrue(TEXT("Disabling neutral returns the ring to wild"), Map.GetZoneAt(FVector(400.f, 0.f, 12.f)).Zone == EGFTerritoryZone::Wild);
	TestTrue(TEXT("Disabling neutral leaves faction land"), Map.GetZoneAt(Hub).Zone == EGFTerritoryZone::Faction);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryOverlapTest, "GFGame.Territory.Overlap", GFTerritoryTests::Flags)
bool FGFTerritoryOverlapTest::RunTest(const FString&)
{
	using namespace GFTerritoryTests;

	// Radius = 1000 * Members (base 0, per member 1000, no cap). Centers 1000 cm apart.
	// Two members → radius 2000, so the midpoint is inside both.
	FGFTerritoryMap Map;
	Map.Curve.BaseRadius = 0.f;
	Map.Curve.RadiusPerMember = 1000.f;
	Map.Curve.MaxRadius = 0.f;
	Map.Neutral.bEnabled = true;
	Map.Neutral.Center = FVector::ZeroVector;
	Map.Neutral.Radius = 5000.f;

	const FVector Left = FVector::ZeroVector;
	const FVector Right(1000.f, 0.f, 0.f);
	// Verdant is first in the array. A "first one wins" bug would give Verdant the midpoint.
	Map.Factions = {
		Disc(FGFTerritoryFactions::Verdant, Right, 2),
		Disc(FGFTerritoryFactions::Cinderwrights, Left, 2)
	};

	const FVector NearLeft(100.f, 0.f, 80.f);
	const FVector NearRight(900.f, 0.f, -20.f);
	const FVector Mid(500.f, 0.f, 15.f);
	const FVector AlmostLeft(499.f, 0.f, 0.f);

	TestEqual(TEXT("Closer center wins on the left"), Map.GetZoneAt(NearLeft).FactionId, FGFTerritoryFactions::Cinderwrights);
	TestEqual(TEXT("Closer center wins on the right"), Map.GetZoneAt(NearRight).FactionId, FGFTerritoryFactions::Verdant);
	TestTrue(TEXT("Two centimetres is not a tie"), Map.GetZoneAt(AlmostLeft).FactionId == FGFTerritoryFactions::Cinderwrights);
	TestEqual(TEXT("Exact midpoint tie: smaller name wins"), Map.GetZoneAt(Mid).FactionId, FGFTerritoryFactions::Cinderwrights);
	TestTrue(TEXT("Overlap is still faction, not neutral"), Map.GetZoneAt(Mid).Zone == EGFTerritoryZone::Faction);
	TestTrue(TEXT("Overlap is inside the safe union"), Map.IsInsideSafe(Mid));

	Map.Factions[0].Members = 4;
	TestEqual(TEXT("Tie goes to the larger faction"), Map.GetZoneAt(Mid).FactionId, FGFTerritoryFactions::Verdant);
	TestEqual(TEXT("Closer still beats a larger far faction"), Map.GetZoneAt(NearLeft).FactionId, FGFTerritoryFactions::Cinderwrights);

	Map.Factions[0].Members = 2;
	Map.Factions[1].Id = FGFTerritoryFactions::Rime;
	TestEqual(TEXT("Rime beats Verdant on a name tie"), Map.GetZoneAt(Mid).FactionId, FGFTerritoryFactions::Rime);

	const FVector Outside(6000.f, 0.f, 0.f);
	TestTrue(TEXT("Outside every disc is not faction"), Map.GetZoneAt(Outside).Zone != EGFTerritoryZone::Faction);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritorySafeTest, "GFGame.Territory.Safe", GFTerritoryTests::Flags)
bool FGFTerritorySafeTest::RunTest(const FString&)
{
	using namespace GFTerritoryTests;

	FGFTerritoryMap Map;
	Map.Curve.BaseRadius = 0.f;
	Map.Curve.RadiusPerMember = 100.f;
	Map.Curve.MaxRadius = 0.f;
	Map.Neutral.bEnabled = true;
	Map.Neutral.Center = FVector::ZeroVector;
	Map.Neutral.Radius = 400.f;
	Map.Factions = {
		Disc(FGFTerritoryFactions::Cinderwrights, FVector(1000.f, 0.f, 0.f), 2),
		Disc(FGFTerritoryFactions::Verdant, FVector(-1000.f, 0.f, 0.f), 2)
	};

	const FVector CinderHome(1000.f, 0.f, 30.f);
	const FVector VerdantHome(-1000.f, 0.f, 30.f);
	const FVector Hub(0.f, 0.f, 30.f);
	const FVector Wilds(5000.f, 0.f, 30.f);

	const FGFTerritorySubject Cinder = FGFTerritorySubject::Player(FGFTerritoryFactions::Cinderwrights, false);
	const FGFTerritorySubject CinderHost = FGFTerritorySubject::Player(FGFTerritoryFactions::Cinderwrights, true);
	const FGFTerritorySubject Verdant = FGFTerritorySubject::Player(FGFTerritoryFactions::Verdant, false);
	const FGFTerritorySubject HostAlone = FGFTerritorySubject::Player(NAME_None, true);
	const FGFTerritorySubject Unaffiliated = FGFTerritorySubject::Player(NAME_None, false);

	TestTrue(TEXT("Own land is safe"), Map.IsSafeFor(Cinder, CinderHome));
	TestTrue(TEXT("Host on their own faction land is safe"), Map.IsSafeFor(CinderHost, CinderHome));
	TestFalse(TEXT("Other faction land is not safe"), Map.IsSafeFor(Cinder, VerdantHome));
	TestFalse(TEXT("Neutral is not safe"), Map.IsSafeFor(Cinder, Hub));
	TestFalse(TEXT("Wild is not safe"), Map.IsSafeFor(Cinder, Wilds));
	TestFalse(TEXT("Host with no faction is not safe on faction land"), Map.IsSafeFor(HostAlone, CinderHome));
	TestFalse(TEXT("Host with no faction is not safe in neutral"), Map.IsSafeFor(HostAlone, Hub));
	TestFalse(TEXT("Unaffiliated player is not safe"), Map.IsSafeFor(Unaffiliated, CinderHome));
	TestTrue(TEXT("Zone on home is that faction"), Map.GetZoneAt(CinderHome).FactionId == FGFTerritoryFactions::Cinderwrights);

	TestFalse(TEXT("Wild dino is not safe on faction land"), Map.IsSafeFor(FGFTerritorySubject::WildDino(), CinderHome));
	TestTrue(TEXT("Tamed dino is safe on the owner's land"), Map.IsSafeFor(FGFTerritorySubject::TamedDino(FGFTerritoryFactions::Cinderwrights), CinderHome));
	TestFalse(TEXT("Tamed dino is not safe on enemy land"), Map.IsSafeFor(FGFTerritorySubject::TamedDino(FGFTerritoryFactions::Cinderwrights), VerdantHome));
	TestFalse(TEXT("Tamed dino with no owner faction is not safe"), Map.IsSafeFor(FGFTerritorySubject::TamedDino(NAME_None), CinderHome));
	TestFalse(TEXT("Tamed dino is not safe in the wild"), Map.IsSafeFor(FGFTerritorySubject::TamedDino(FGFTerritoryFactions::Verdant, true), Wilds));

	// Radius at 1 member is 100, at 2 members is 200. 150 cm out from Cinderwrights starts wild, then becomes theirs.
	const FVector GrowsIn(1150.f, 0.f, 0.f);
	Map.Factions[0].Members = 1;
	TestTrue(TEXT("Before growth the point is wild"), Map.GetZoneAt(GrowsIn).Zone == EGFTerritoryZone::Wild);
	TestFalse(TEXT("Before growth it is not safe"), Map.IsSafeFor(Cinder, GrowsIn));
	Map.Factions[0].Members = 2;
	TestEqual(TEXT("After growth it is their land"), Map.GetZoneAt(GrowsIn).FactionId, FGFTerritoryFactions::Cinderwrights);
	TestTrue(TEXT("After growth it is safe"), Map.IsSafeFor(Cinder, GrowsIn));
	TestFalse(TEXT("After growth it is still not safe for the other faction"), Map.IsSafeFor(Verdant, GrowsIn));

	// Midpoint between the two homes is 0, which is also the neutral center. Both radii are 200, centers 2000 apart,
	// so the midpoint is outside both discs and stays neutral: nobody is safe there.
	TestTrue(TEXT("Gap between factions stays neutral"), Map.GetZoneAt(Hub).Zone == EGFTerritoryZone::Neutral);
	TestFalse(TEXT("Neither faction is safe in the gap"), Map.IsSafeFor(Cinder, Hub) || Map.IsSafeFor(Verdant, Hub));
	return true;
}

#endif
