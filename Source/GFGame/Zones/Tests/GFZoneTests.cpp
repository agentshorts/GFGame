// World zones: priority, hysteresis, PvP rows, layout, Shardhop, gates, scatter, save.
// Territory, overworld Make(), and GFFormatZoneLabel stay on their own tests.

#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFMiniGameGateSubsystem.h"
#include "GFHUDFormat.h"
#include "GFOverworldBuild.h"
#include "GFOverworldGround.h"
#include "GFSaveGame.h"
#include "GFShardTravelSubsystem.h"
#include "GFShardhop.h"
#include "GFTerritorySubsystem.h"
#include "GFZoneCatalog.h"
#include "GFZoneLayout.h"
#include "GFZoneTypes.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFZoneTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFZoneRow Circle(const TCHAR* Id, int32 Priority, int32 Tier, bool bPvP, float Radius, const FVector& Center = FVector::ZeroVector)
	{
		FGFZoneRow Row;
		Row.Id = Id;
		Row.Shape = EGFZoneShape::Circle;
		Row.Priority = Priority;
		Row.DangerTier = Tier;
		Row.bPvPAllowed = bPvP;
		Row.Radius = Radius;
		Row.Center = Center;
		Row.DisplayName = Id;
		Row.ResourceBias = TEXT("1;1;1");
		return Row;
	}

	static bool LoadCatalog(FAutomationTestBase& Test, FGFZoneCatalog& Catalog)
	{
		FGFDataTables::ResetCache();
		Catalog = FGFZoneCatalog::Load();
		Test.TestEqual(TEXT("Zone rows"), Catalog.Zones.Num(), 22);
		Test.TestEqual(TEXT("Ambience rows"), Catalog.Ambience.Num(), 14);
		Test.TestEqual(TEXT("Stones"), Catalog.Stones.Num(), 9);
		return Catalog.Zones.Num() == 22 && Catalog.Stones.Num() == 9;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZonePriorityTest, "GFGame.World.Zones.Priority", GFZoneTests::Flags)
bool FGFZonePriorityTest::RunTest(const FString&)
{
	FGFZoneRow Badlands = GFZoneTests::Circle(TEXT("Zone.Badlands"), 10, 2, true, 1000.f);
	FGFZoneRow Hazard = GFZoneTests::Circle(TEXT("Zone.Hazard.LavaBridge"), 50, 3, true, 80.f, FVector(50.f, 0.f, 0.f));
	Hazard.Shape = EGFZoneShape::Box;
	Hazard.HalfExtent = FVector(80.f, 80.f, 10.f);
	Hazard.Radius = 0.f;
	FGFZoneRow Porch = GFZoneTests::Circle(TEXT("Zone.Porch.Arena"), 60, 1, false, 100.f);
	const TArray<FGFZoneRow> Rows = { Badlands, Hazard, Porch };

	const FGFZoneHit Center = FGFZoneCatalog::ResolveRows(Rows, FVector::ZeroVector, nullptr);
	TestEqual(TEXT("Porch beats hazard and badlands"), Center.ZoneId.ToString(), FString(TEXT("Zone.Porch.Arena")));
	TestEqual(TEXT("Porch tier"), Center.DangerTier, 1);
	TestFalse(TEXT("Porch is not PvP"), Center.bPvPAllowed);

	const FGFZoneHit InHazard = FGFZoneCatalog::ResolveRows(Rows, FVector(120.f, 0.f, 40.f), nullptr);
	TestEqual(TEXT("Hazard beats badlands"), InHazard.ZoneId.ToString(), FString(TEXT("Zone.Hazard.LavaBridge")));

	const FGFZoneHit OnlyBad = FGFZoneCatalog::ResolveRows(Rows, FVector(200.f, 0.f, 0.f), nullptr);
	TestEqual(TEXT("Badlands fills the rest"), OnlyBad.ZoneId.ToString(), FString(TEXT("Zone.Badlands")));

	FGFTerritoryZoneHit Faction;
	Faction.Zone = EGFTerritoryZone::Faction;
	Faction.FactionId = TEXT("Rime");
	const FGFZoneHit Land = FGFZoneCatalog::ResolveRows(Rows, FVector::ZeroVector, &Faction);
	TestEqual(TEXT("Faction beats every row"), Land.ZoneId.ToString(), FString(TEXT("Faction.Rime")));
	TestEqual(TEXT("Faction tier"), Land.DangerTier, 0);
	TestFalse(TEXT("Faction is not PvP"), Land.bPvPAllowed);

	FGFZoneCatalog Catalog;
	if (!GFZoneTests::LoadCatalog(*this, Catalog))
	{
		return false;
	}
	const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
	if (!TestNotNull(TEXT("Territory defaults"), Defaults))
	{
		return false;
	}
	FGFTerritoryMap Map;
	Map.Curve = Defaults->Curve;
	Map.Factions = Defaults->FactionLayout;
	Map.Hazards = Defaults->Hazards;
	Map.SetMembers(TEXT("Rime"), 0);
	const FGFTerritoryZoneHit Empty = Map.GetZoneAt(FVector::ZeroVector);
	TestEqual(TEXT("Empty origin is Kindle"), Catalog.ResolveAt(FVector::ZeroVector, &Empty).ZoneId.ToString(), FString(TEXT("Zone.Kindle")));

	Map.SetMembers(TEXT("Rime"), 1);
	const FGFTerritoryZoneHit Owned = Map.GetZoneAt(FVector::ZeroVector);
	TestEqual(TEXT("Rime origin"), Catalog.ResolveAt(FVector::ZeroVector, &Owned).ZoneId.ToString(), FString(TEXT("Faction.Rime")));

	const FGFTerritoryZoneHit Lava = Map.GetZoneAt(FVector(3700.f, 0.f, 0.f));
	TestEqual(TEXT("Lava bridge loses to Kindle"), Catalog.ResolveAt(FVector(3700.f, 0.f, 9.f), &Lava).ZoneId.ToString(), FString(TEXT("Zone.Kindle")));

	const FVector PorchCenter = FGFZoneLayout::BearingPoint(0.f, FGFZoneConstants::PorchRingCm);
	TestEqual(TEXT("Porch core"), Catalog.ResolveAt(PorchCenter, nullptr).ZoneId.ToString(), FString(TEXT("Zone.Porch.Arena.Core")));
	TestEqual(TEXT("Core tier"), Catalog.ResolveAt(PorchCenter, nullptr).DangerTier, 0);
	const FVector PorchRing = PorchCenter + FVector(5000.f, 0.f, 80.f);
	TestEqual(TEXT("Porch outside the core"), Catalog.ResolveAt(PorchRing, nullptr).ZoneId.ToString(), FString(TEXT("Zone.Porch.Arena")));
	TestEqual(TEXT("Porch tier"), Catalog.ResolveAt(PorchRing, nullptr).DangerTier, 1);

	TestEqual(TEXT("Open badlands"), Catalog.ResolveAt(FGFZoneLayout::BearingPoint(10.f, 40000.f), nullptr).ZoneId.ToString(), FString(TEXT("Zone.Badlands")));
	TestEqual(TEXT("Just inside deep"), Catalog.ResolveAt(FGFZoneLayout::BearingPoint(10.f, 104900.f), nullptr).ZoneId.ToString(), FString(TEXT("Zone.Badlands")));
	TestEqual(TEXT("Deep rim"), Catalog.ResolveAt(FGFZoneLayout::BearingPoint(10.f, FGFZoneConstants::DeepInnerCm), nullptr).ZoneId.ToString(), FString(TEXT("Zone.DeepBadlands")));
	TestEqual(TEXT("Past the rim still badlands"), Catalog.ResolveAt(FGFZoneLayout::BearingPoint(10.f, 130000.f), nullptr).ZoneId.ToString(), FString(TEXT("Zone.Badlands")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneHysteresisTest, "GFGame.World.Zones.Hysteresis", GFZoneTests::Flags)
bool FGFZoneHysteresisTest::RunTest(const FString&)
{
	FGFZoneCatalog Catalog;
	if (!GFZoneTests::LoadCatalog(*this, Catalog))
	{
		return false;
	}
	const FVector Stay = FGFZoneLayout::BearingPoint(10.f, FGFZoneConstants::KindleRadiusCm + 100.f);
	const FVector Leave = FGFZoneLayout::BearingPoint(10.f, FGFZoneConstants::KindleRadiusCm + 210.f);
	TestEqual(TEXT("1 m past stays"), Catalog.Advance(TEXT("Zone.Kindle"), Stay, nullptr, nullptr).ToString(), FString(TEXT("Zone.Kindle")));
	TestEqual(TEXT("2.1 m past leaves"), Catalog.Advance(TEXT("Zone.Kindle"), Leave, nullptr, nullptr).ToString(), FString(TEXT("Zone.Fringe")));

	const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
	if (!TestNotNull(TEXT("Territory defaults"), Defaults))
	{
		return false;
	}
	FGFTerritoryMap Map;
	Map.Curve = Defaults->Curve;
	Map.Factions = Defaults->FactionLayout;
	Map.SetMembers(TEXT("Rime"), 1);
	const FGFTerritoryFaction* Rime = Map.FindFaction(TEXT("Rime"));
	if (!TestNotNull(TEXT("Rime"), Rime))
	{
		return false;
	}
	FGFTerritoryZoneHit Owned = Map.GetZoneAt(FVector::ZeroVector);
	TestEqual(TEXT("Inside the disc, faction wins at once"), Catalog.Advance(TEXT("Zone.Kindle"), FVector::ZeroVector, &Owned, &Map).ToString(), FString(TEXT("Faction.Rime")));

	const float Radius = Map.RadiusOf(*Rime);
	const FVector Near = FVector(Radius + 100.f, 0.f, 0.f);
	const FVector Far = FVector(Radius + 210.f, 0.f, 0.f);
	const FGFTerritoryZoneHit NearHit = Map.GetZoneAt(Near);
	const FGFTerritoryZoneHit FarHit = Map.GetZoneAt(Far);
	TestEqual(TEXT("1 m outside the disc stays faction"), Catalog.Advance(TEXT("Faction.Rime"), Near, &NearHit, &Map).ToString(), FString(TEXT("Faction.Rime")));
	TestEqual(TEXT("2.1 m outside the disc is Kindle"), Catalog.Advance(TEXT("Faction.Rime"), Far, &FarHit, &Map).ToString(), FString(TEXT("Zone.Kindle")));

	TestEqual(TEXT("Leaving home"), Catalog.ToastFor(TEXT("Zone.Kindle"), TEXT("Zone.Fringe"), nullptr), FString(GFZoneCopy::LeavingKindle()));
	TestEqual(TEXT("Coming home"), Catalog.ToastFor(TEXT("Zone.Badlands"), TEXT("Faction.Rime"), &Map), FString(GFZoneCopy::WelcomeHome()));
	TestTrue(TEXT("First sight is quiet"), Catalog.ToastFor(NAME_None, TEXT("Zone.Kindle"), nullptr).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneRowsTest, "GFGame.World.Zones.PvPAndRows", GFZoneTests::Flags)
bool FGFZoneRowsTest::RunTest(const FString&)
{
	FGFZoneCatalog Catalog;
	if (!GFZoneTests::LoadCatalog(*this, Catalog))
	{
		return false;
	}
	for (const FGFZoneRow& Row : Catalog.Zones)
	{
		TestTrue(*(FString(TEXT("Shape ")) + Row.Id.ToString()), Row.IsShapeValid());
		float Delver = 0.f;
		float Wrangler = 0.f;
		float Scavenger = 0.f;
		TestTrue(*(FString(TEXT("Bias ")) + Row.Id.ToString()), Row.ParseBias(Delver, Wrangler, Scavenger));
		TestTrue(*(FString(TEXT("PvP ")) + Row.Id.ToString()), Row.bPvPAllowed == (Row.DangerTier >= 2));
		TestFalse(*(FString(TEXT("No hold slot ")) + Row.Id.ToString()), Row.Id.ToString().Contains(TEXT("PushTheCart")));
		TestNotNull(*(FString(TEXT("Ambience ")) + Row.Id.ToString()), Catalog.FindAmbience(Row.AmbienceId));
	}
	for (const FGFZoneAmbienceRow& Row : Catalog.Ambience)
	{
		TestTrue(*(FString(TEXT("Exposure ")) + Row.Id.ToString()), FMath::Abs(Row.ExposureOffset) <= FGFZoneConstants::ExposureLimit + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Ambient loop stays empty"), Row.AmbientLoop.IsEmpty());
	}
	TestFalse(TEXT("Tier 0"), Catalog.IsPvPAllowedAt(FVector::ZeroVector, nullptr));
	TestFalse(TEXT("Tier 1 fringe"), Catalog.IsPvPAllowedAt(FGFZoneLayout::BearingPoint(10.f, 16000.f), nullptr));
	TestTrue(TEXT("Tier 2"), Catalog.IsPvPAllowedAt(FGFZoneLayout::BearingPoint(10.f, 40000.f), nullptr));
	TestTrue(TEXT("Tier 3"), Catalog.IsPvPAllowedAt(FGFZoneLayout::BearingPoint(10.f, FGFZoneConstants::DeepInnerCm), nullptr));
	const FGFShardstoneRow* Kindle = Catalog.FindStone(TEXT("Stone.Kindle"));
	if (TestNotNull(TEXT("Kindle stone"), Kindle))
	{
		TestTrue(TEXT("Kindle starts unlocked"), Catalog.IsUnlocked(*Kindle, {}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneLayoutTest, "GFGame.World.Zones.Layout", GFZoneTests::Flags)
bool FGFZoneLayoutTest::RunTest(const FString&)
{
	FGFZoneCatalog Catalog;
	if (!GFZoneTests::LoadCatalog(*this, Catalog))
	{
		return false;
	}
	TestEqual(TEXT("Eight porches"), FGFZoneLayout::Regions().Num(), 8);
	const TArray<FGFPorchDisc> Discs = Catalog.PorchDiscs();
	TestEqual(TEXT("Eight porch discs"), Discs.Num(), 8);
	for (const FGFPorchDisc& Disc : Discs)
	{
		TestTrue(TEXT("Porch inside the rim"), Disc.Center.Size() + Disc.RadiusCm < FGFZoneConstants::RimRadiusCm);
		TestTrue(TEXT("Porch misses the Kindle Ring"), Disc.Center.Size() - Disc.RadiusCm > FGFZoneConstants::KindleRadiusCm);
		for (const FGFPorchDisc& Other : Discs)
		{
			if (Other.Id == Disc.Id)
			{
				continue;
			}
			TestTrue(TEXT("Porches do not overlap"), FVector2D::Distance(Disc.Center, Other.Center) > Disc.RadiusCm + Other.RadiusCm);
		}
	}
	for (const FGFShardstoneRow& Stone : Catalog.Stones)
	{
		TestTrue(TEXT("Hub to stone"), FVector2D(Stone.Location.X, Stone.Location.Y).Size() <= FGFZoneConstants::HubToStoneMaxCm);
	}
	for (const FGFRegionSlot& Slot : FGFZoneLayout::Regions())
	{
		const FGFZoneRow* Row = Catalog.FindZone(*FString::Printf(TEXT("Zone.Porch.%s"), *Slot.RegionId.ToString().RightChop(7)));
		const FVector Expected = FGFZoneLayout::BearingPoint(Slot.BearingDeg, FGFZoneConstants::PorchRingCm);
		if (TestNotNull(*Slot.RegionId.ToString(), Row))
		{
			TestTrue(*Slot.RegionId.ToString(), FVector::Dist2D(Row->Center, Expected) <= 1.f);
		}
		if (const FGFShardstoneRow* Stone = Catalog.FindStone(Slot.StoneId))
		{
			TestTrue(TEXT("Stone on the porch"), FVector::Dist2D(Stone->Location, Expected) <= 1.f);
		}
	}

	float Worst = 0.f;
	for (float X = -FGFZoneConstants::RimRadiusCm; X <= FGFZoneConstants::RimRadiusCm; X += FGFZoneConstants::SampleGridCm)
	{
		for (float Y = -FGFZoneConstants::RimRadiusCm; Y <= FGFZoneConstants::RimRadiusCm; Y += FGFZoneConstants::SampleGridCm)
		{
			float Best = TNumericLimits<float>::Max();
			for (const FGFShardstoneRow& Stone : Catalog.Stones)
			{
				Best = FMath::Min(Best, FVector::Dist2D(Stone.Location, FVector(X, Y, 0.f)));
			}
			Worst = FMath::Max(Worst, Best);
		}
	}
	TestTrue(TEXT("Every sample is within a 4 minute walk"), Worst <= FGFZoneConstants::MaxWalkFromStoneCm);
	AddInfo(FString::Printf(TEXT("Worst sample is %.0f cm from a Shardstone"), Worst));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneShardhopTest, "GFGame.World.Zones.Shardhop", GFZoneTests::Flags)
bool FGFZoneShardhopTest::RunTest(const FString&)
{
	FGFZoneCatalog Catalog;
	FGFShardstoneRow Kindle;
	Kindle.Id = TEXT("Stone.Kindle");
	Kindle.bStartsUnlocked = true;
	Kindle.Location = FVector::ZeroVector;
	Kindle.ArrivalOffset = FVector(400.f, 0.f, 120.f);
	FGFShardstoneRow Arena;
	Arena.Id = TEXT("Stone.Arena");
	Arena.Location = FVector(90000.f, 0.f, 0.f);
	Arena.ArrivalOffset = Kindle.ArrivalOffset;
	Catalog.Stones = { Kindle, Arena };

	FGFShardhopView View;
	View.Location = FVector::ZeroVector;
	View.NowSeconds = 100.0;
	View.LastPvPDamageSeconds = -1.0;
	FGFShardhopChannel Channel;

	TestEqual(TEXT("Dark stone"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Locked));
	View.Location = FVector(10000.f, 0.f, 0.f);
	TestEqual(TEXT("Too far from a stone"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Kindle.Id, Channel)), static_cast<int32>(EGFShardhopResult::TooFar));
	View.Location = FVector::ZeroVector;
	View.bVillainMode = true;
	TestEqual(TEXT("Villain cannot start"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Kindle.Id, Channel)), static_cast<int32>(EGFShardhopResult::Villain));
	View.bVillainMode = false;
	View.LastPvPDamageSeconds = 95.0;
	TestEqual(TEXT("Recent PvP"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Kindle.Id, Channel)), static_cast<int32>(EGFShardhopResult::RecentPvP));
	View.LastPvPDamageSeconds = 90.0;
	View.Unlocked.Add(Arena.Id);
	TestEqual(TEXT("Started"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Started));
	TestEqual(TEXT("Busy"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Busy));

	View.NowSeconds = 102.0;
	TestEqual(TEXT("Still channeling"), static_cast<int32>(FGFShardhop::Tick(Catalog, View, Channel)), static_cast<int32>(EGFShardhopResult::Running));
	View.DamageSerial = 1;
	TestEqual(TEXT("Damage cancels"), static_cast<int32>(FGFShardhop::Tick(Catalog, View, Channel)), static_cast<int32>(EGFShardhopResult::Cancelled));

	View.DamageSerial = 0;
	TestEqual(TEXT("Restart"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Started));
	View.bVillainMode = true;
	TestEqual(TEXT("Villain cancels"), static_cast<int32>(FGFShardhop::Tick(Catalog, View, Channel)), static_cast<int32>(EGFShardhopResult::Cancelled));

	View.bVillainMode = false;
	TestEqual(TEXT("Restart after villain"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Started));
	View.Location = FVector(10000.f, 0.f, 0.f);
	TestEqual(TEXT("Walking off cancels"), static_cast<int32>(FGFShardhop::Tick(Catalog, View, Channel)), static_cast<int32>(EGFShardhopResult::Cancelled));

	View.Location = FVector(100.f, 0.f, 0.f);
	TestEqual(TEXT("Restart to finish"), static_cast<int32>(FGFShardhop::TryStart(Catalog, View, Arena.Id, Channel)), static_cast<int32>(EGFShardhopResult::Started));
	View.NowSeconds = Channel.StartedAt + static_cast<double>(FGFZoneConstants::ChannelSeconds);
	const FName Dest = Channel.DestId;
	TestEqual(TEXT("Three seconds finishes"), static_cast<int32>(FGFShardhop::Tick(Catalog, View, Channel)), static_cast<int32>(EGFShardhopResult::Completed));
	TestEqual(TEXT("Destination kept"), Dest.ToString(), Arena.Id.ToString());

	TArray<FVector> Arrivals;
	for (int32 Slot = 0; Slot < 4; ++Slot)
	{
		Arrivals.Add(FGFShardhop::ArrivalPoint(Arena, Slot));
	}
	for (int32 A = 0; A < Arrivals.Num(); ++A)
	{
		for (int32 B = A + 1; B < Arrivals.Num(); ++B)
		{
			TestTrue(TEXT("Arrivals stay apart"), FVector::Dist2D(Arrivals[A], Arrivals[B]) >= 300.f);
		}
	}
	TestTrue(TEXT("Companion comes"), FGFShardhop::ShouldBringCompanion(true, TEXT("Ada"), TEXT("Ada"), 2000.f));
	TestFalse(TEXT("Wild stays"), FGFShardhop::ShouldBringCompanion(false, TEXT("Ada"), TEXT("Ada"), 100.f));
	TestFalse(TEXT("Far companion stays"), FGFShardhop::ShouldBringCompanion(true, TEXT("Ada"), TEXT("Ada"), 2001.f));
	TestFalse(TEXT("Someone else's companion stays"), FGFShardhop::ShouldBringCompanion(true, TEXT("Bea"), TEXT("Ada"), 100.f));
	TestEqual(TEXT("Look toward Arena"), FGFShardhop::PickDestination(Catalog, View.Unlocked, FVector::ZeroVector, 0.f).ToString(), Arena.Id.ToString());

	UGameInstance* TravelOuter = NewObject<UGameInstance>(GetTransientPackage());
	UGFShardTravelSubsystem* Travel = NewObject<UGFShardTravelSubsystem>(TravelOuter);
	Travel->SetPending(TEXT("Stone.Arena"));
	FName First;
	FName Second;
	TestTrue(TEXT("First spawn consumes"), Travel->Consume(TEXT("Ada"), First));
	TestEqual(TEXT("Pending stone"), First.ToString(), FString(TEXT("Stone.Arena")));
	TestFalse(TEXT("Respawn does not snap back"), Travel->Consume(TEXT("Ada"), Second));
	TestTrue(TEXT("Late joiner still gets it"), Travel->Consume(TEXT("Bea"), Second));
	TestTrue(TEXT("Ada already arrived"), Travel->ConsumedStone(TEXT("Ada"), First));
	Travel->SetPending(TEXT("Stone.Valley"));
	TestTrue(TEXT("Next porch arms everyone again"), Travel->Consume(TEXT("Ada"), First));
	TestEqual(TEXT("New stone"), First.ToString(), FString(TEXT("Stone.Valley")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneGateTest, "GFGame.World.Zones.Gates", GFZoneTests::Flags)
bool FGFZoneGateTest::RunTest(const FString&)
{
	for (const FGFRegionSlot& Slot : FGFZoneLayout::Regions())
	{
		if (!Slot.bEnabled)
		{
			TestTrue(TEXT("Splitcrystal has no gate"), Slot.GateId.IsNone());
			const FGFRegionTravelPlan Held = FGFZoneLayout::PlanTravel(Slot, true);
			TestFalse(TEXT("Held porch does not travel"), Held.bTravel);
			TestTrue(TEXT("Held porch sets no stone"), Held.PendingStoneId.IsNone());
			TestTrue(TEXT("Held porch stays quiet"), Held.Message.IsEmpty());
			continue;
		}
		UClass* Class = FGFZoneLayout::FindGateClass(Slot.GateId);
		if (!TestNotNull(*Slot.GateId.ToString(), Class))
		{
			continue;
		}
		const UGFMiniGameGateSubsystem* CDO = Cast<UGFMiniGameGateSubsystem>(Class->GetDefaultObject());
		if (!TestNotNull(TEXT("Gate CDO"), CDO))
		{
			continue;
		}
		TestEqual(TEXT("Gate id"), CDO->GetGateId().ToString(), Slot.GateId.ToString());
		TestFalse(TEXT("Moba keeps L_Arena"), Slot.GateId == FName(TEXT("Arena")) && CDO->GetMapName() == FName(TEXT("/Game/Maps/L_Arena")));
		const FGFRegionTravelPlan Missing = FGFZoneLayout::PlanTravel(Slot, false);
		TestFalse(TEXT("Missing package stays"), Missing.bTravel);
		TestTrue(TEXT("Missing package sets no stone"), Missing.PendingStoneId.IsNone());
		TestEqual(TEXT("Under construction"), Missing.Message, FString(GFZoneCopy::UnderConstruction()));
		const FGFRegionTravelPlan Ready = FGFZoneLayout::PlanTravel(Slot, true);
		TestTrue(TEXT("Ready package travels"), Ready.bTravel);
		TestEqual(TEXT("Return stone"), Ready.PendingStoneId.ToString(), Slot.StoneId.ToString());
		TestEqual(TEXT("Map"), Ready.MapPackage.ToString(), CDO->GetMapName().ToString());
	}
	TestNull(TEXT("Moba is not a porch"), FGFZoneLayout::FindByGate(TEXT("Moba")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZonePoiTest, "GFGame.World.Zones.POI", GFZoneTests::Flags)
bool FGFZonePoiTest::RunTest(const FString&)
{
	FGFZoneCatalog Catalog;
	if (!GFZoneTests::LoadCatalog(*this, Catalog))
	{
		return false;
	}
	const TArray<FGFPOIPoint> A = Catalog.ScatterPOI(FGFZoneConstants::PoiSeed);
	const TArray<FGFPOIPoint> Again = Catalog.ScatterPOI(FGFZoneConstants::PoiSeed);
	const TArray<FGFPOIPoint> Other = Catalog.ScatterPOI(FGFZoneConstants::PoiSeed + 1);
	TestTrue(TEXT("Scatter found places"), A.Num() > 0);
	TestEqual(TEXT("Same seed, same count"), A.Num(), Again.Num());
	bool bSame = A.Num() == Again.Num();
	for (int32 Index = 0; bSame && Index < A.Num(); ++Index)
	{
		bSame = A[Index].Location.Equals(Again[Index].Location) && A[Index].Kind == Again[Index].Kind;
	}
	TestTrue(TEXT("Same seed, same points"), bSame);
	TestTrue(TEXT("Other seed differs"), Other.Num() != A.Num() || (Other.Num() > 0 && !Other[0].Location.Equals(A[0].Location)));
	const TArray<FGFPorchDisc> Discs = Catalog.PorchDiscs();
	int32 InsideKindle = 0;
	int32 OutsideRim = 0;
	int32 InPorch = 0;
	float Closest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		const float Rim = FVector2D(A[Index].Location.X, A[Index].Location.Y).Size();
		InsideKindle += Rim <= FGFZoneConstants::KindleRadiusCm ? 1 : 0;
		OutsideRim += Rim > FGFZoneConstants::RimRadiusCm ? 1 : 0;
		for (const FGFPorchDisc& Disc : Discs)
		{
			if (FVector2D::Distance(Disc.Center, FVector2D(A[Index].Location.X, A[Index].Location.Y)) <= Disc.RadiusCm)
			{
				++InPorch;
			}
		}
		for (int32 OtherIndex = Index + 1; OtherIndex < A.Num(); ++OtherIndex)
		{
			Closest = FMath::Min(Closest, FVector::Dist2D(A[Index].Location, A[OtherIndex].Location));
		}
	}
	TestEqual(TEXT("None in the Kindle Ring"), InsideKindle, 0);
	TestEqual(TEXT("None past the rim"), OutsideRim, 0);
	TestEqual(TEXT("None in a porch"), InPorch, 0);
	TestTrue(TEXT("At least 120 m apart"), Closest >= FGFZoneConstants::PoiMinSpacingCm);
	AddInfo(FString::Printf(TEXT("%d points, closest %.0f cm"), A.Num(), Closest));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneSaveTest, "GFGame.World.Zones.Save", GFZoneTests::Flags)
bool FGFZoneSaveTest::RunTest(const FString&)
{
	UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
	Save->Character.Progress.UnlockedShardstones.Add(TEXT("Stone.Arena"));
	TArray<uint8> Bytes;
	TestTrue(TEXT("Save"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	const UGFCharacterSave* Back = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("Loaded"), Back))
	{
		return false;
	}
	TestTrue(TEXT("Stone survives"), Back->Character.Progress.UnlockedShardstones.Contains(FName(TEXT("Stone.Arena"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFZoneStampTest, "GFGame.World.Zones.KindleStamp", GFZoneTests::Flags)
bool FGFZoneStampTest::RunTest(const FString&)
{
	const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
	const AGFOverworldGround* Ground = GetDefault<AGFOverworldGround>();
	if (!TestNotNull(TEXT("Territory"), Defaults) || !TestNotNull(TEXT("Ground"), Ground))
	{
		return false;
	}
	const FGFOverworldLayout Plain = FGFOverworldBuild::Make(Defaults->Curve, Defaults->FactionLayout, Defaults->Hazards, Ground->Margin);
	const FGFOverworldLayout Ring = FGFOverworldBuild::MakeKindleRing(Defaults->Curve, Defaults->FactionLayout, Defaults->Hazards, Ground->Margin);
	TestFalse(TEXT("Make stays wild stone"), Plain.bKindleGrass);
	TestTrue(TEXT("Ring is grass"), Ring.bKindleGrass);
	TestEqual(TEXT("Ring X"), Ring.GridSize.X, FGFZoneConstants::KindleStampBlocks);
	TestEqual(TEXT("Ring Y"), Ring.GridSize.Y, FGFZoneConstants::KindleStampBlocks);
	TestEqual(TEXT("Ring Z"), Ring.GridSize.Z, FGFOverworldLayout::GridZ);
	TestTrue(TEXT("Origin is on the ring"), Ring.IsInside(Ring.WorldToCell(FVector::ZeroVector)));
	TestTrue(TEXT("Make is the smaller plot"), Plain.GridSize.X != Ring.GridSize.X && Plain.GridSize.Y != Ring.GridSize.Y);
	const FIntVector Wild = Ring.SurfaceCell(FVector(16000.f, 0.f, 0.f));
	TestEqual(TEXT("Kindle grass"), Ring.BlockAt(Wild).ToString(), GFOverworldBlock::Grass().ToString());
	TestEqual(TEXT("Legacy wild label"), GFFormatZoneLabel(EGFTerritoryZone::Wild, NAME_None, false, false, NAME_None), FString(TEXT("Wild")));
	TestEqual(TEXT("World safe label"), GFFormatWorldZoneLabel(TEXT("Kindle Ring"), 0), FString(TEXT("Kindle Ring \u00B7 safe")));
	TestEqual(TEXT("World wild label"), GFFormatWorldZoneLabel(TEXT("Badlands"), 2), FString(TEXT("Badlands")));
	const float Mid = GFCompassIconX(0.f, 1000.f);
	const float Right = GFCompassIconX(100.f, 1000.f);
	const float Left = GFCompassIconX(-100.f, 1000.f);
	TestTrue(TEXT("Ahead is centred"), FMath::Abs(Mid - 500.f) < 1.f);
	TestTrue(TEXT("Right edge"), Right > Mid);
	TestTrue(TEXT("Left edge"), Left < Mid);
	TestEqual(TEXT("Behind pins to the edge"), GFCompassIconX(180.f, 1000.f), Right);
	return true;
}

#endif
