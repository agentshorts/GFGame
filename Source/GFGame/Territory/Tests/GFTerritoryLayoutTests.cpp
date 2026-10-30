// Shipped faction layout. Reads UGFTerritorySubsystem defaults from DefaultGame.ini.
// Ids here are the layout ids. Production territory code does not name factions.
//
// Touch: both radii are greater than zero and the horizontal distance between centers is <= rA + rB.
// Rime to Verdant touches only when the larger count is 4 and the smaller count is at least 1.
// Cinderwrights touches neither neighbor at any count from 0 through 4.
// A set RadiusByMembers table is the radius. An empty table uses the shared curve.
// GetHazardAt returns a box only where GetZoneAt is wild.

#include "Misc/AutomationTest.h"
#include "GFTerritorySubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFTerritoryLayoutTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Cinder(TEXT("Cinderwrights"));
	static const FName LavaBridge(TEXT("LavaBridge"));
	static const FName DeadlyForest(TEXT("DeadlyForest"));

	static bool Load(FGFTerritoryMap& Map)
	{
		const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
		if (!Defaults)
		{
			return false;
		}
		Map = FGFTerritoryMap();
		Map.Curve = Defaults->Curve;
		Map.Neutral = Defaults->NeutralZone;
		Map.Factions = Defaults->FactionLayout;
		Map.Hazards = Defaults->Hazards;
		return Map.FindFaction(Rime) && Map.FindFaction(Verdant) && Map.FindFaction(Cinder);
	}

	static bool SetCounts(FGFTerritoryMap& Map, int32 RimeMembers, int32 VerdantMembers, int32 CinderMembers)
	{
		return Map.SetMembers(Rime, RimeMembers) && Map.SetMembers(Verdant, VerdantMembers) && Map.SetMembers(Cinder, CinderMembers);
	}

	static float CurveRadius(const FGFTerritoryCurve& Curve, int32 Members)
	{
		if (Members <= 0)
		{
			return 0.f;
		}
		const float Grown = Curve.BaseRadius + Curve.RadiusPerMember * static_cast<float>(Members);
		const float OneMember = FMath::Max(0.f, Curve.BaseRadius + Curve.RadiusPerMember);
		if (Curve.MaxRadius > 0.f)
		{
			return FMath::Max(0.f, FMath::Min(Grown, FMath::Max(Curve.MaxRadius, OneMember)));
		}
		return FMath::Max(0.f, Grown);
	}

	static const FGFTerritoryHazard* FindHazard(const FGFTerritoryMap& Map, FName Id)
	{
		return Map.Hazards.FindByPredicate([Id](const FGFTerritoryHazard& Hazard) { return Hazard.Id == Id; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryLayoutRimeVerdantTest, "GFGame.Territory.Layout.RimeVerdant", GFTerritoryLayoutTests::Flags)
bool FGFTerritoryLayoutRimeVerdantTest::RunTest(const FString&)
{
	using namespace GFTerritoryLayoutTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory config"), Load(Map)))
	{
		return false;
	}

	for (int32 RimeMembers = 0; RimeMembers <= 4; ++RimeMembers)
	{
		for (int32 VerdantMembers = 0; VerdantMembers <= 4; ++VerdantMembers)
		{
			TestTrue(TEXT("Layout ids accept member counts"), SetCounts(Map, RimeMembers, VerdantMembers, 0));
			const bool Expected = FMath::Max(RimeMembers, VerdantMembers) == 4 && FMath::Min(RimeMembers, VerdantMembers) >= 1;
			const FString What = FString::Printf(TEXT("Rime %d Verdant %d"), RimeMembers, VerdantMembers);
			TestTrue(*What, Map.Touches(Rime, Verdant) == Expected);
			TestTrue(*FString::Printf(TEXT("%s reversed"), *What), Map.Touches(Verdant, Rime) == Expected);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryLayoutCinderAloneTest, "GFGame.Territory.Layout.CinderAlone", GFTerritoryLayoutTests::Flags)
bool FGFTerritoryLayoutCinderAloneTest::RunTest(const FString&)
{
	using namespace GFTerritoryLayoutTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory config"), Load(Map)))
	{
		return false;
	}

	for (int32 RimeMembers = 0; RimeMembers <= 4; ++RimeMembers)
	{
		for (int32 VerdantMembers = 0; VerdantMembers <= 4; ++VerdantMembers)
		{
			for (int32 CinderMembers = 0; CinderMembers <= 4; ++CinderMembers)
			{
				TestTrue(TEXT("Layout ids accept member counts"), SetCounts(Map, RimeMembers, VerdantMembers, CinderMembers));
				const FString What = FString::Printf(TEXT("Rime %d Verdant %d Cinderwrights %d"), RimeMembers, VerdantMembers, CinderMembers);
				TestFalse(*FString::Printf(TEXT("%s touches Rime"), *What), Map.Touches(Cinder, Rime));
				TestFalse(*FString::Printf(TEXT("%s Rime touches Cinderwrights"), *What), Map.Touches(Rime, Cinder));
				TestFalse(*FString::Printf(TEXT("%s touches Verdant"), *What), Map.Touches(Cinder, Verdant));
				TestFalse(*FString::Printf(TEXT("%s Verdant touches Cinderwrights"), *What), Map.Touches(Verdant, Cinder));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryLayoutRimeMostLandTest, "GFGame.Territory.Layout.RimeMostLand", GFTerritoryLayoutTests::Flags)
bool FGFTerritoryLayoutRimeMostLandTest::RunTest(const FString&)
{
	using namespace GFTerritoryLayoutTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory config"), Load(Map)))
	{
		return false;
	}

	Map.MaxMembers = 0;
	for (int32 Members = 1; Members <= 4; ++Members)
	{
		TestTrue(TEXT("Layout ids accept member counts"), SetCounts(Map, Members, Members, Members));
		const float RimeArea = Map.AreaOf(*Map.FindFaction(Rime));
		TestTrue(*FString::Printf(TEXT("Equal count %d, Rime area is larger than Verdant"), Members), RimeArea > Map.AreaOf(*Map.FindFaction(Verdant)));
		TestTrue(*FString::Printf(TEXT("Equal count %d, Rime area is larger than Cinderwrights"), Members), RimeArea > Map.AreaOf(*Map.FindFaction(Cinder)));
	}

	TestTrue(TEXT("Full Rime"), Map.SetMembers(Rime, 4));
	const float FullRime = Map.AreaOf(*Map.FindFaction(Rime));
	const FName Ids[] = { Rime, Verdant, Cinder };
	const int32 Counts[] = { 0, 1, 2, 3, 4, 8, 20 };
	const int32 Caps[] = { 0, 4 };
	for (int32 Cap : Caps)
	{
		Map.MaxMembers = Cap;
		for (const FName Id : Ids)
		{
			for (int32 Count : Counts)
			{
				TestTrue(TEXT("Layout id accepts a member count"), Map.SetMembers(Id, Count));
				const float Area = Map.AreaOf(*Map.FindFaction(Id));
				TestTrue(*FString::Printf(TEXT("%s at %d with cap %d is within full Rime"), *Id.ToString(), Count, Cap), Area <= FullRime);
				if (Id != Rime)
				{
					TestTrue(*FString::Printf(TEXT("%s at %d with cap %d is under full Rime"), *Id.ToString(), Count, Cap), Area < FullRime);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryLayoutTableTest, "GFGame.Territory.Layout.Table", GFTerritoryLayoutTests::Flags)
bool FGFTerritoryLayoutTableTest::RunTest(const FString&)
{
	using namespace GFTerritoryLayoutTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory config"), Load(Map)))
	{
		return false;
	}

	TestEqual(TEXT("Fallback curve base"), Map.Curve.BaseRadius, 1200.f);
	TestEqual(TEXT("Fallback curve step"), Map.Curve.RadiusPerMember, 400.f);
	TestEqual(TEXT("Fallback curve cap"), Map.Curve.MaxRadius, 2800.f);

	for (FGFTerritoryFaction& Faction : Map.Factions)
	{
		const TArray<float> Shipped = Faction.RadiusByMembers;
		TestEqual(*FString::Printf(TEXT("%s table covers counts 0 through 4"), *Faction.Id.ToString()), Shipped.Num(), 5);
		if (Shipped.Num() == 0)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s index 0 is no land"), *Faction.Id.ToString()), Shipped[0], 0.f);

		Map.MaxMembers = 0;
		for (int32 Count = 0; Count < Shipped.Num(); ++Count)
		{
			Faction.Members = Count;
			const float Radius = FMath::Max(0.f, Shipped[Count]);
			const FGFSafeRegion Region = Map.RegionFor(Faction);
			TestEqual(*FString::Printf(TEXT("%s count %d radius"), *Faction.Id.ToString(), Count), Map.RadiusOf(Faction), Radius);
			TestEqual(*FString::Printf(TEXT("%s count %d region members"), *Faction.Id.ToString(), Count), Region.Members, Count);
			TestEqual(*FString::Printf(TEXT("%s count %d region base"), *Faction.Id.ToString(), Count), Region.BaseRadius, Radius);
			TestEqual(*FString::Printf(TEXT("%s count %d region cap"), *Faction.Id.ToString(), Count), Region.MaxRadius, Radius);
			TestEqual(*FString::Printf(TEXT("%s count %d region step"), *Faction.Id.ToString(), Count), Region.RadiusPerExtraMember, 0.f);
			TestEqual(*FString::Printf(TEXT("%s count %d safe radius"), *Faction.Id.ToString(), Count), Region.Radius(), Radius);
		}

		Faction.Members = Shipped.Num() + 3;
		TestEqual(*FString::Printf(TEXT("%s past the table uses the last entry"), *Faction.Id.ToString()), Map.RadiusOf(Faction), FMath::Max(0.f, Shipped.Last()));

		TArray<float> Extended = Shipped;
		const float PastCap = Shipped.Last() + 5000.f;
		Extended.Add(PastCap);
		Faction.RadiusByMembers = Extended;
		Map.MaxMembers = 4;
		Faction.Members = 7;
		const FGFSafeRegion Capped = Map.RegionFor(Faction);
		TestEqual(*FString::Printf(TEXT("%s above MaxMembers uses count 4"), *Faction.Id.ToString()), Capped.Members, 4);
		TestEqual(*FString::Printf(TEXT("%s above MaxMembers keeps the count-4 radius"), *Faction.Id.ToString()), Map.RadiusOf(Faction), FMath::Max(0.f, Shipped.Last()));
		Map.MaxMembers = 0;
		Faction.Members = Shipped.Num();
		TestEqual(*FString::Printf(TEXT("%s uncapped count reads the extra entry"), *Faction.Id.ToString()), Map.RadiusOf(Faction), PastCap);
		Faction.Members = Extended.Num() + 4;
		TestEqual(*FString::Printf(TEXT("%s past the extended table uses its last entry"), *Faction.Id.ToString()), Map.RadiusOf(Faction), PastCap);

		Faction.RadiusByMembers.Empty();
		Map.MaxMembers = 0;
		for (int32 Count = 0; Count <= 5; ++Count)
		{
			Faction.Members = Count;
			TestEqual(*FString::Printf(TEXT("%s empty table count %d follows the curve"), *Faction.Id.ToString(), Count), Map.RadiusOf(Faction), CurveRadius(Map.Curve, Count));
		}
		Map.MaxMembers = 4;
		Faction.Members = 6;
		TestEqual(*FString::Printf(TEXT("%s empty table clamps to MaxMembers"), *Faction.Id.ToString()), Map.RadiusOf(Faction), CurveRadius(Map.Curve, 4));
		if (Map.Curve.RadiusPerMember != 0.f)
		{
			Faction.Members = 2;
			Map.MaxMembers = 0;
			const FGFSafeRegion CurveRegion = Map.RegionFor(Faction);
			TestEqual(*FString::Printf(TEXT("%s empty table keeps the curve step"), *Faction.Id.ToString()), CurveRegion.RadiusPerExtraMember, Map.Curve.RadiusPerMember);
			TestEqual(*FString::Printf(TEXT("%s empty table base is the one-member radius"), *Faction.Id.ToString()), CurveRegion.BaseRadius, FMath::Max(0.f, Map.Curve.BaseRadius + Map.Curve.RadiusPerMember));
		}

		Faction.RadiusByMembers = Shipped;
		Faction.Members = 0;
	}

	for (FGFTerritoryFaction& Faction : Map.Factions)
	{
		Faction.Members = 0;
	}
	Map.MaxMembers = 0;
	Map.Neutral.bEnabled = false;
	FGFTerritoryFaction* ProbeFaction = Map.FindFaction(Rime);
	if (!TestNotNull(TEXT("Rime is on the shipped layout"), ProbeFaction))
	{
		return false;
	}
	ProbeFaction->RadiusByMembers = { 0.f, 500.f };
	ProbeFaction->Members = 1;
	const FVector Probe(ProbeFaction->Center.X + 400.f, ProbeFaction->Center.Y, 25.f);
	TestEqual(TEXT("Probe starts inside the table disc"), Map.GetZoneAt(Probe).FactionId, Rime);
	ProbeFaction->RadiusByMembers[1] = 100.f;
	TestTrue(TEXT("A table edit with the same member count updates the zone"), Map.GetZoneAt(Probe).Zone == EGFTerritoryZone::Wild);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTerritoryLayoutHazardsTest, "GFGame.Territory.Layout.Hazards", GFTerritoryLayoutTests::Flags)
bool FGFTerritoryLayoutHazardsTest::RunTest(const FString&)
{
	using namespace GFTerritoryLayoutTests;

	FGFTerritoryMap Map;
	if (!TestTrue(TEXT("Shipped territory config"), Load(Map)))
	{
		return false;
	}

	const FGFTerritoryHazard* Lava = FindHazard(Map, LavaBridge);
	const FGFTerritoryHazard* Forest = FindHazard(Map, DeadlyForest);
	const bool bHasLava = TestNotNull(TEXT("LavaBridge is on the shipped layout"), Lava);
	const bool bHasForest = TestNotNull(TEXT("DeadlyForest is on the shipped layout"), Forest);
	if (!bHasLava || !bHasForest)
	{
		return false;
	}
	TestEqual(TEXT("LavaBridge kind"), Lava->Kind, LavaBridge);
	TestEqual(TEXT("LavaBridge sits beside Rime"), Lava->BetweenA, Rime);
	TestEqual(TEXT("LavaBridge sits beside Cinderwrights"), Lava->BetweenB, Cinder);
	TestEqual(TEXT("DeadlyForest kind"), Forest->Kind, DeadlyForest);
	TestEqual(TEXT("DeadlyForest sits beside Rime"), Forest->BetweenA, Rime);
	TestEqual(TEXT("DeadlyForest sits beside Verdant"), Forest->BetweenB, Verdant);

	for (const FGFTerritoryHazard& Hazard : Map.Hazards)
	{
		TestFalse(*FString::Printf(TEXT("%s BetweenA is set"), *Hazard.Id.ToString()), Hazard.BetweenA.IsNone());
		TestFalse(*FString::Printf(TEXT("%s BetweenB is set"), *Hazard.Id.ToString()), Hazard.BetweenB.IsNone());
		TestTrue(*FString::Printf(TEXT("%s BetweenA is a layout id"), *Hazard.Id.ToString()), Map.FindFaction(Hazard.BetweenA) != nullptr);
		TestTrue(*FString::Printf(TEXT("%s BetweenB is a layout id"), *Hazard.Id.ToString()), Map.FindFaction(Hazard.BetweenB) != nullptr);
	}

	const FVector LavaPoint(3800.f, 0.f, 0.f);
	for (int32 RimeMembers = 0; RimeMembers <= 4; ++RimeMembers)
	{
		for (int32 VerdantMembers = 0; VerdantMembers <= 4; ++VerdantMembers)
		{
			for (int32 CinderMembers = 0; CinderMembers <= 4; ++CinderMembers)
			{
				TestTrue(TEXT("Layout ids accept member counts"), SetCounts(Map, RimeMembers, VerdantMembers, CinderMembers));
				const FGFTerritoryHazardHit Hit = Map.GetHazardAt(LavaPoint);
				const FString What = FString::Printf(TEXT("Lava point at %d %d %d"), RimeMembers, VerdantMembers, CinderMembers);
				TestTrue(*What, Hit.bHazard);
				TestEqual(*FString::Printf(TEXT("%s id"), *What), Hit.Id, LavaBridge);
				TestEqual(*FString::Printf(TEXT("%s kind"), *What), Hit.Kind, LavaBridge);
			}
		}
	}
	TestTrue(TEXT("Zero the layout"), SetCounts(Map, 0, 0, 0));
	const FGFTerritoryHazardHit LavaHigh = Map.GetHazardAt(FVector(3800.f, 0.f, 5000.f));
	TestTrue(TEXT("Lava box ignores Z"), LavaHigh.bHazard);
	TestEqual(TEXT("High lava point kind"), LavaHigh.Kind, LavaBridge);

	TestTrue(TEXT("Forest at three and three"), SetCounts(Map, 3, 3, 0));
	const FVector ForestPoint(-2300.f, 0.f, 0.f);
	const FGFTerritoryHazardHit ForestHit = Map.GetHazardAt(ForestPoint);
	TestTrue(TEXT("Forest point is a hazard at three and three"), ForestHit.bHazard);
	TestEqual(TEXT("Forest point kind"), ForestHit.Kind, DeadlyForest);
	TestEqual(TEXT("Forest point id"), ForestHit.Id, DeadlyForest);
	TestTrue(TEXT("Forest point is wild at three and three"), Map.GetZoneAt(ForestPoint).Zone == EGFTerritoryZone::Wild);
	const FGFTerritoryHazardHit ForestHigh = Map.GetHazardAt(FVector(-2300.f, 0.f, 900.f));
	TestEqual(TEXT("Forest box ignores Z"), ForestHigh.Kind, DeadlyForest);

	TestTrue(TEXT("Forest becomes owned land"), SetCounts(Map, 4, 1, 0));
	const FGFTerritoryHazardHit OwnedForest = Map.GetHazardAt(ForestPoint);
	TestFalse(TEXT("Owned forest point is not a hazard"), OwnedForest.bHazard);
	TestTrue(TEXT("Owned forest point is faction land"), Map.GetZoneAt(ForestPoint).Zone == EGFTerritoryZone::Faction);

	for (int32 RimeMembers = 0; RimeMembers <= 4; ++RimeMembers)
	{
		for (int32 VerdantMembers = 0; VerdantMembers <= 4; ++VerdantMembers)
		{
			for (int32 CinderMembers = 0; CinderMembers <= 4; ++CinderMembers)
			{
				TestTrue(TEXT("Layout ids accept member counts"), SetCounts(Map, RimeMembers, VerdantMembers, CinderMembers));
				const FName Ids[] = { Rime, Verdant, Cinder };
				for (const FName Id : Ids)
				{
					const FGFTerritoryFaction* Faction = Map.FindFaction(Id);
					const float Radius = Map.RadiusOf(*Faction);
					if (Radius <= 0.f)
					{
						continue;
					}
					const float Offsets[] = { 0.f, Radius * 0.5f, -Radius * 0.5f, Radius * 0.99f, -Radius * 0.99f };
					for (float Offset : Offsets)
					{
						const FVector Point(Faction->Center.X + Offset, Faction->Center.Y, 80.f);
						TestTrue(*FString::Printf(TEXT("%s sample stays owned"), *Id.ToString()), Map.GetZoneAt(Point).Zone == EGFTerritoryZone::Faction);
						TestFalse(*FString::Printf(TEXT("%s sample is not a hazard"), *Id.ToString()), Map.GetHazardAt(Point).bHazard);
					}
				}
			}
		}
	}
	return true;
}

#endif
