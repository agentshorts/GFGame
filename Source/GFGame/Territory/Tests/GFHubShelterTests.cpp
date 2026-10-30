// Hub shelter (#136 option 1). A subject with no faction is safe inside HubRadius.
// IsInsideSafe does not grow, so the giant's pathing shape stays the faction discs.
// Faction members do not get the circle, including when they stand on someone else's land.
// IsSafeFor(FName) and IsSafeFor(FGFSocialPlayer) stay own-land only (dinos use those).

#include "Misc/AutomationTest.h"
#include "GFTerritoryMap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFHubShelterTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Cinder(TEXT("Cinderwrights"));

	static FGFTerritoryFaction Camp(FName Id, const FVector& Center, int32 Members)
	{
		FGFTerritoryFaction Faction;
		Faction.Id = Id;
		Faction.Center = Center;
		Faction.Members = Members;
		Faction.RadiusByMembers = { 0.f, 1600.f, 1600.f, 1600.f, 1600.f };
		return Faction;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubShelterTest, "GFGame.Territory.HubShelter", GFHubShelterTests::Flags)
bool FGFHubShelterTest::RunTest(const FString&)
{
	using namespace GFHubShelterTests;

	FGFTerritoryMap Map;
	Map.Factions = {
		Camp(Rime, FVector::ZeroVector, 0),
		Camp(Verdant, FVector(-4400.f, 0.f, 0.f), 1),
		Camp(Cinder, FVector(7200.f, 0.f, 0.f), 1),
	};
	Map.HubCenter = FVector::ZeroVector;
	Map.HubRadius = 1200.f;

	const FVector Hub(400.f, 0.f, 20.f);
	const FVector Origin(0.f, 0.f, 20.f);
	const FGFSafeSubject None(TEXT("N"), NAME_None);
	const FGFSafeSubject VerdantMember(TEXT("V"), Verdant);
	const FGFSocialPlayer DinoNone(TEXT("D"), NAME_None);

	// Rime has no members, so this point is wild. The hub still shelters a factionless player.
	TestFalse(TEXT("Hub point is not a pathing disc before Rime has land"), Map.IsInsideSafe(Hub));
	TestTrue(TEXT("Factionless player is safe in the hub"), Map.IsSafeFor(None, Hub));
	TestFalse(TEXT("Verdant member is not sheltered by the hub"), Map.IsSafeFor(VerdantMember, Hub));
	TestFalse(TEXT("Name query ignores the hub"), Map.IsSafeFor(NAME_None, Hub));
	TestFalse(TEXT("Social-player query ignores the hub"), Map.IsSafeFor(DinoNone, Hub));
	TestTrue(TEXT("Owner of a wild hub point stays None"), Map.OwnerAt(Hub).IsNone());

	TestTrue(TEXT("Rime gains a member"), Map.SetMembers(Rime, 1));
	TestTrue(TEXT("Rime land is a pathing disc"), Map.IsInsideSafe(Origin));
	TestEqual(TEXT("Rime owns its centre"), Map.OwnerAt(Origin), Rime);
	TestTrue(TEXT("Factionless player stays safe on Rime's hub"), Map.IsSafeFor(None, Origin));
	TestFalse(TEXT("Verdant member is not safe on Rime's hub"), Map.IsSafeFor(VerdantMember, Origin));
	TestTrue(TEXT("Verdant member is safe at home"), Map.IsSafeFor(VerdantMember, FVector(-4400.f, 0.f, 20.f)));

	// Inside Rime's disc, outside the hub: factionless players are not safe on faction land.
	const FVector RimeRim(1500.f, 0.f, 20.f);
	TestTrue(TEXT("Rime rim is inside the land disc"), Map.IsInsideSafe(RimeRim));
	TestFalse(TEXT("Factionless player is not safe on Rime outside the hub"), Map.IsSafeFor(None, RimeRim));

	// Wild ground inside a larger hub is safe for no faction and still not a pathing disc.
	Map.HubRadius = 5000.f;
	const FVector WildHub(2000.f, 2000.f, 20.f);
	TestFalse(TEXT("Wild hub point is outside every disc"), Map.IsInsideSafe(WildHub));
	TestTrue(TEXT("Factionless player is safe in the wider hub"), Map.IsSafeFor(None, WildHub));
	TestFalse(TEXT("Cinderwrights member is not safe there"), Map.IsSafeFor(FGFSafeSubject(TEXT("C"), Cinder), WildHub));

	// The circle also counts on owned land that is not theirs.
	const FVector OnVerdant(-4400.f, 0.f, 20.f);
	TestTrue(TEXT("Verdant centre is inside the wide hub"), FVector::Dist2D(OnVerdant, Map.HubCenter) <= Map.HubRadius);
	TestTrue(TEXT("Factionless player is safe on Verdant while inside the hub"), Map.IsSafeFor(None, OnVerdant));
	TestFalse(TEXT("Cinderwrights member is not safe on Verdant"), Map.IsSafeFor(FGFSafeSubject(TEXT("C"), Cinder), OnVerdant));

	Map.HubRadius = 0.f;
	TestFalse(TEXT("Radius 0 turns the hub off"), Map.IsSafeFor(None, Origin));
	TestTrue(TEXT("Rime's disc is still a pathing shape"), Map.IsInsideSafe(Origin));

	return true;
}

#endif
