// Villain Mode: opt-in, wild and jail, warm-up, lock, bonk, faint drop. Duels stay parked.
#include "Misc/AutomationTest.h"
#include "GFVillainRules.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "GFItemTypes.h"
#include "Engine/DataTable.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFVillainTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

namespace
{
	FGFVillainSide Villain(const TCHAR* Key, bool bOn, EGFVillainPlace Place, double Warmup = -1.0, double Immune = -1.0)
	{
		FGFVillainSide Side;
		Side.Key = Key;
		Side.bVillain = bOn;
		Side.Place = Place;
		Side.bJailed = Place == EGFVillainPlace::Jail;
		Side.WarmupUntil = Warmup;
		Side.ImmuneUntil = Immune;
		Side.Health = 100.f;
		return Side;
	}

	FGFVillainQuery HitQ(double Now, float Damage = 12.f)
	{
		FGFVillainQuery Query;
		Query.Now = Now;
		Query.RawDamage = Damage;
		return Query;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVillainConsentTest, "GFGame.Villain.Consent", GFVillainTestFlags)
bool FGFVillainConsentTest::RunTest(const FString&)
{
	TestEqual(TEXT("Run speed"), FGFVillainRules::MoveSpeedMultiplier(), 1.f);
	TestFalse(TEXT("Not saved on the character"), FGFVillainRules::PersistsOnCharacter());
	TestFalse(TEXT("No build rights"), FGFVillainRules::GrantsBuildRights());
	TestFalse(TEXT("Big Grumble targeting is unchanged"), FGFVillainRules::ChangesGiantTargeting());
	TestFalse(TEXT("A faint is not a dino kill"), FGFVillainRules::CountsAsDinoKill());
	TestEqual(TEXT("No reputation change"), FGFVillainRules::RepDelta(), 0);
	TestEqual(TEXT("Sight"), FGFVillainRules::SightCm, 6000.f);
	TestEqual(TEXT("Retire line"), FString(FGFVillainRules::RetireLine()), FString(TEXT("has retired from villainy, for now")));

	const TArray<FString> Titles = FGFVillainRules::Titles();
	TestEqual(TEXT("Six titles"), Titles.Num(), 6);
	TestTrue(TEXT("the Unpleasant"), Titles.Contains(TEXT("the Unpleasant")));
	TestTrue(TEXT("Badlands Gremlin"), Titles.Contains(TEXT("Badlands Gremlin")));
	TestEqual(TEXT("Title index"), FGFVillainRules::TitleAt(2), FString(TEXT("Menace of the Flats")));
	TestTrue(TEXT("Bad index is blank"), FGFVillainRules::TitleAt(99).IsEmpty());
	TestEqual(TEXT("Dinobook"), FGFVillainRules::DinobookLine(1), FString(TEXT("A villain was seen in the badlands again. Typical.")));
	TestTrue(TEXT("Wanted poster at three"), FGFVillainRules::DinobookLine(3).Contains(TEXT("Wanted")));
	TestEqual(TEXT("Wanted streak"), FGFVillainRules::WantedStreak, 3);

	bool bStarted = true;
	TestEqual(TEXT("Duel is parked"), FGFVillainRules::RequestDuel(bStarted), FName(TEXT("Parked")));
	TestFalse(TEXT("A parked duel does not start"), bStarted);

	const double Now = 100.0;
	const FGFVillainSide A = Villain(TEXT("A"), true, EGFVillainPlace::Wild);
	const FGFVillainSide B = Villain(TEXT("B"), true, EGFVillainPlace::Wild);
	const FGFVillainSide Civilian = Villain(TEXT("C"), false, EGFVillainPlace::Wild);
	FGFVillainHit Landed = FGFVillainRules::CanDamage(A, B, HitQ(Now));
	TestTrue(TEXT("Both villains in the wild"), Landed.bDamage && Landed.Damage == 12.f && Landed.Code == TEXT("Ok") && Landed.bRestartLock && !Landed.bBonk);

	Landed = FGFVillainRules::CanDamage(Civilian, B, HitQ(Now));
	TestTrue(TEXT("A civilian swing does nothing"), !Landed.bDamage && !Landed.bBonk && Landed.Code == TEXT("NotVillain"));
	Landed = FGFVillainRules::CanDamage(A, Civilian, HitQ(Now));
	TestTrue(TEXT("A villain hitting a civilian bonks"), Landed.bBonk && !Landed.bDamage && !Landed.bRestartLock && Landed.Code == TEXT("Bonk"));

	Landed = FGFVillainRules::CanDamage(A, A, HitQ(Now));
	TestEqual(TEXT("Not yourself"), Landed.Code, FName(TEXT("Self")));
	Landed = FGFVillainRules::CanDamage(A, B, HitQ(Now, 0.f));
	TestEqual(TEXT("No damage to apply"), Landed.Code, FName(TEXT("None")));

	FGFVillainSide Rental = B;
	Rental.bRental = true;
	TestEqual(TEXT("Rentals refuse"), FGFVillainRules::CanDamage(A, Rental, HitQ(Now)).Code, FName(TEXT("Rental")));
	FGFVillainSide RentalAttacker = A;
	RentalAttacker.bRental = true;
	TestEqual(TEXT("A rental attacker also refuses"), FGFVillainRules::CanDamage(RentalAttacker, B, HitQ(Now)).Code, FName(TEXT("Rental")));

	// A parked duel does not open a second damage path.
	bStarted = true;
	FGFVillainRules::RequestDuel(bStarted);
	Landed = FGFVillainRules::CanDamage(Civilian, Villain(TEXT("D"), false, EGFVillainPlace::Wild), HitQ(Now));
	TestEqual(TEXT("Two civilians still cannot hurt each other"), Landed.Code, FName(TEXT("NotVillain")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVillainPlaceTest, "GFGame.Villain.Place", GFVillainTestFlags)
bool FGFVillainPlaceTest::RunTest(const FString&)
{
	TestTrue(TEXT("Wild"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Wild));
	TestTrue(TEXT("Hazard box in the wild"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Hazard));
	TestTrue(TEXT("Jail is not a safe circle"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Jail));
	TestFalse(TEXT("Safe"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Safe));
	TestFalse(TEXT("Faction"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Faction));
	TestFalse(TEXT("Neutral"), FGFVillainRules::PlaceAllows(EGFVillainPlace::Neutral));
	TestFalse(TEXT("Mini-game"), FGFVillainRules::PlaceAllows(EGFVillainPlace::MiniGame));

	const double Now = 50.0;
	const FGFVillainSide A = Villain(TEXT("A"), true, EGFVillainPlace::Wild);
	auto Blocked = [&](EGFVillainPlace Place)
	{
		const FGFVillainHit Hit = FGFVillainRules::CanDamage(A, Villain(TEXT("B"), true, Place), HitQ(Now));
		TestTrue(*FString::Printf(TEXT("%d passes through"), static_cast<int32>(Place)), Hit.Code == TEXT("Zone") && !Hit.bDamage && !Hit.bBonk);
	};
	Blocked(EGFVillainPlace::Safe);
	Blocked(EGFVillainPlace::Faction);
	Blocked(EGFVillainPlace::Neutral);
	Blocked(EGFVillainPlace::MiniGame);

	const FGFVillainSide SafeAttacker = Villain(TEXT("A"), true, EGFVillainPlace::Safe);
	const FGFVillainHit FromTown = FGFVillainRules::CanDamage(SafeAttacker, Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now));
	TestEqual(TEXT("Swinging from town does not land"), FromTown.Code, FName(TEXT("Zone")));

	const FGFVillainHit Hazard = FGFVillainRules::CanDamage(Villain(TEXT("A"), true, EGFVillainPlace::Hazard), Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now));
	TestEqual(TEXT("A hazard box still counts as wild"), Hazard.Code, FName(TEXT("Ok")));

	const FGFVillainHit Jailed = FGFVillainRules::CanDamage(A, Villain(TEXT("B"), true, EGFVillainPlace::Jail), HitQ(Now));
	TestTrue(TEXT("A jailed villain can be hit"), Jailed.bDamage && Jailed.Code == TEXT("Ok"));
	const FGFVillainHit JailedCivilian = FGFVillainRules::CanDamage(A, Villain(TEXT("C"), false, EGFVillainPlace::Jail), HitQ(Now));
	TestTrue(TEXT("Jail does not make a civilian a target"), JailedCivilian.bBonk && !JailedCivilian.bDamage && JailedCivilian.Code == TEXT("Bonk"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVillainArmTest, "GFGame.Villain.ArmLock", GFVillainTestFlags)
bool FGFVillainArmTest::RunTest(const FString&)
{
	FRandomStream Rng(163);
	FGFVillainStatus Status;
	TestEqual(TEXT("Jail cannot arm"), FGFVillainRules::TryArm(Status, 10.0, true, Rng), FName(TEXT("Jailed")));
	TestFalse(TEXT("Still off"), Status.bOn);

	TestEqual(TEXT("Arm"), FGFVillainRules::TryArm(Status, 10.0, false, Rng), FName(TEXT("Ok")));
	TestTrue(TEXT("On"), Status.bOn);
	TestEqual(TEXT("Warm-up"), Status.WarmupUntil, 15.0);
	TestTrue(TEXT("A title was rolled"), Status.Title != INDEX_NONE);
	const int32 FirstTitle = Status.Title;
	TestEqual(TEXT("Already on"), FGFVillainRules::TryArm(Status, 12.0, false, Rng), FName(TEXT("Already")));
	TestEqual(TEXT("Warm-up was not restarted"), Status.WarmupUntil, 15.0);

	TestEqual(TEXT("One reroll"), FGFVillainRules::RerollTitle(Status, Rng), FName(TEXT("Ok")));
	TestTrue(TEXT("Reroll spent"), Status.bTitleSpent);
	TestTrue(TEXT("Title changed or wrapped to a different index"), Status.Title != FirstTitle || FGFVillainRules::Titles().Num() == 1);
	TestEqual(TEXT("No second reroll"), FGFVillainRules::RerollTitle(Status, Rng), FName(TEXT("Spent")));

	FGFVillainSide Armed = Villain(TEXT("A"), true, EGFVillainPlace::Wild, Status.WarmupUntil);
	FGFVillainSide Other = Villain(TEXT("B"), true, EGFVillainPlace::Wild);
	TestEqual(TEXT("Warm-up blocks the attacker"), FGFVillainRules::CanDamage(Armed, Other, HitQ(14.0)).Code, FName(TEXT("Warmup")));
	Other.WarmupUntil = 20.0;
	Armed.WarmupUntil = -1.0;
	TestEqual(TEXT("Warm-up blocks the victim too"), FGFVillainRules::CanDamage(Armed, Other, HitQ(16.0)).Code, FName(TEXT("Warmup")));
	Other.WarmupUntil = -1.0;
	TestEqual(TEXT("After warm-up"), FGFVillainRules::CanDamage(Armed, Other, HitQ(16.0)).Code, FName(TEXT("Ok")));

	FGFVillainStatus Attacker = Status;
	Attacker.Key = TEXT("A");
	FGFVillainStatus Victim;
	Victim.Key = TEXT("B");
	Victim.bOn = true;
	const FGFVillainHit Landed = FGFVillainRules::CanDamage(Armed, Other, HitQ(16.0));
	FGFVillainRules::NoteHit(Attacker, Victim, Landed, 16.0);
	TestEqual(TEXT("Lock on the attacker"), Attacker.LockUntil, 46.0);
	TestEqual(TEXT("Lock on the victim"), Victim.LockUntil, 46.0);
	TestEqual(TEXT("Recent hitter recorded"), Victim.RecentHits.Num(), 1);
	Victim.RecentHits.Add(TPair<FString, double>(TEXT("Old"), 16.0 - 61.0));
	FGFVillainRules::NoteHit(Attacker, Victim, Landed, 20.0);
	TestEqual(TEXT("A new hit restarts the lock"), Attacker.LockUntil, 50.0);
	TestEqual(TEXT("Stale hitter dropped"), Victim.RecentHits.Num(), 1);
	TestEqual(TEXT("The current hitter remains"), Victim.RecentHits[0].Key, FString(TEXT("A")));

	const FGFVillainHit Bonk = FGFVillainRules::CanDamage(Armed, Villain(TEXT("C"), false, EGFVillainPlace::Wild), HitQ(20.0));
	const int32 HitsBefore = Victim.RecentHits.Num();
	const double LockBefore = Attacker.LockUntil;
	FGFVillainRules::NoteHit(Attacker, Victim, Bonk, 20.0);
	TestEqual(TEXT("A bonk does not restart the lock"), Attacker.LockUntil, LockBefore);
	TestEqual(TEXT("A bonk records nobody"), Victim.RecentHits.Num(), HitsBefore);

	TestEqual(TEXT("Cannot disarm while locked"), FGFVillainRules::TryDisarm(Attacker, 49.0), FName(TEXT("Locked")));
	TestTrue(TEXT("Still a villain"), Attacker.bOn);
	TestTrue(TEXT("No channel yet"), Attacker.DisarmUntil < 0.0);
	TestEqual(TEXT("Channel starts once the lock is clear"), FGFVillainRules::TryDisarm(Attacker, 50.0), FName(TEXT("Ok")));
	TestTrue(TEXT("Flag stays on during the channel"), Attacker.bOn);
	TestEqual(TEXT("Three seconds"), Attacker.DisarmUntil, 53.0);
	TestEqual(TEXT("Already channeling"), FGFVillainRules::TryDisarm(Attacker, 51.0), FName(TEXT("Disarming")));
	TestFalse(TEXT("Not finished early"), FGFVillainRules::Tick(Attacker, 52.9));
	TestTrue(TEXT("Still on"), Attacker.bOn);
	TestTrue(TEXT("Channel completes"), FGFVillainRules::Tick(Attacker, 53.0));
	TestFalse(TEXT("Off"), Attacker.bOn);
	TestEqual(TEXT("Title cleared"), Attacker.Title, INDEX_NONE);
	TestFalse(TEXT("Reroll flag cleared"), Attacker.bTitleSpent);
	TestEqual(TEXT("Off again"), FGFVillainRules::TryDisarm(Attacker, 60.0), FName(TEXT("NotVillain")));
	TestEqual(TEXT("Reroll while off"), FGFVillainRules::RerollTitle(Attacker, Rng), FName(TEXT("NotVillain")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVillainFaintTest, "GFGame.Villain.Faint", GFVillainTestFlags)
bool FGFVillainFaintTest::RunTest(const FString&)
{
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	FGFInventory Bag;
	Bag.Add(TEXT("Wood"), 600);
	Bag.Add(TEXT("Stone"), 100);
	Bag.Add(TEXT("Ribbon"), 100);
	Bag.Add(TEXT("Berry"), 80);
	Bag.Add(TEXT("LabCoat"), 4);
	Bag.Add(TEXT("CalmingMist"), 6);
	Bag.Add(TEXT("Chair"), 3);
	Bag.Add(TEXT("FerryPass"), 40);
	Bag.Add(TEXT("Amber"), 9);
	const TArray<FGFItemStack> Drop = FGFVillainRules::FaintDrop(Bag, Items);
	int32 Units = 0;
	for (const FGFItemStack& Stack : Drop)
	{
		Units += Stack.Count;
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Stack.ItemId);
		TestTrue(*FString::Printf(TEXT("%s is a material"), *Stack.ItemId.ToString()), Row && Row->Category == EGFItemCategory::Material && !Row->bAccountBound);
		TestFalse(TEXT("Not an outfit"), Stack.ItemId == TEXT("LabCoat"));
		TestFalse(TEXT("Not a tool"), Stack.ItemId == TEXT("CalmingMist"));
		TestFalse(TEXT("Not furniture"), Stack.ItemId == TEXT("Chair"));
		TestFalse(TEXT("Not a treat"), Stack.ItemId == TEXT("Berry"));
		TestFalse(TEXT("Not the account-bound pass"), Stack.ItemId == TEXT("FerryPass"));
	}
	TestEqual(TEXT("Cap is 50 units"), Units, 50);
	TestEqual(TEXT("Wood takes the cap first"), Drop.Num() > 0 ? Drop[0].Count : 0, 50);
	TestEqual(TEXT("Bag was not edited by the preview"), Bag.GetCount(TEXT("Wood")), 600);
	TestEqual(TEXT("Outfits stay in the preview's source"), Bag.GetCount(TEXT("LabCoat")), 4);

	FGFInventory Modest;
	Modest.Add(TEXT("Wood"), 9);
	Modest.Add(TEXT("Stone"), 25);
	Modest.Add(TEXT("LabCoat"), 2);
	const TArray<FGFItemStack> Small = FGFVillainRules::FaintDrop(Modest, Items);
	TestEqual(TEXT("Nine wood rounds down to nothing"), Small.Num(), 1);
	TestEqual(TEXT("A quarter of the stone, floored"), Small[0].ItemId, FName(TEXT("Stone")));
	TestEqual(TEXT("Two stone"), Small[0].Count, 2);

	FGFVillainStatus Victim;
	Victim.Key = TEXT("B");
	Victim.bOn = true;
	Victim.Health = 4.f;
	Victim.WinStreak = 2;
	Victim.LockUntil = 80.0;
	FGFVillainStatus Attacker;
	Attacker.Key = TEXT("A");
	Attacker.bOn = true;
	Attacker.WinStreak = 2;
	FGFVillainRules::NoteFaint(Victim, Attacker, 30.0);
	TestEqual(TEXT("Health returns"), Victim.Health, 100.f);
	TestEqual(TEXT("Immunity"), Victim.ImmuneUntil, 40.0);
	TestEqual(TEXT("Victim streak cleared"), Victim.WinStreak, 0);
	TestTrue(TEXT("Faint clears the victim lock"), Victim.LockUntil < 0.0);
	TestEqual(TEXT("Attacker streak"), Attacker.WinStreak, 3);
	TestTrue(TEXT("Three in a row is a wanted poster"), FGFVillainRules::DinobookLine(Attacker.WinStreak).Contains(TEXT("Wanted")));

	const FGFVillainSide A = Villain(TEXT("A"), true, EGFVillainPlace::Wild);
	FGFVillainSide B = Villain(TEXT("B"), true, EGFVillainPlace::Wild, -1.0, Victim.ImmuneUntil);
	TestEqual(TEXT("Immune"), FGFVillainRules::CanDamage(A, B, HitQ(39.0)).Code, FName(TEXT("Immune")));
	TestEqual(TEXT("Immunity ends"), FGFVillainRules::CanDamage(A, B, HitQ(40.0)).Code, FName(TEXT("Ok")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVillainDinoTest, "GFGame.Villain.Dinos", GFVillainTestFlags)
bool FGFVillainDinoTest::RunTest(const FString&)
{
	const double Now = 10.0;
	const FGFVillainSide Owner = Villain(TEXT("A"), true, EGFVillainPlace::Wild, 12.0);
	const FGFVillainSide Calm = Villain(TEXT("C"), false, EGFVillainPlace::Wild);
	const FGFVillainSide Dino = FGFVillainRules::DinoSide(Owner, TEXT("Dino"), EGFVillainPlace::Wild, false);
	TestTrue(TEXT("Dino follows the owner"), Dino.bDino && Dino.bVillain && Dino.WarmupUntil == Owner.WarmupUntil && Dino.OwnerKey == TEXT("A"));
	TestEqual(TEXT("Owner still warming up, so the dino is too"), FGFVillainRules::CanDamage(Dino, Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now)).Code, FName(TEXT("Warmup")));

	const FGFVillainSide Ready = Villain(TEXT("A"), true, EGFVillainPlace::Wild);
	const FGFVillainSide ReadyDino = FGFVillainRules::DinoSide(Ready, TEXT("Dino"), EGFVillainPlace::Wild, false);
	TestEqual(TEXT("A villain's dino can hit a villain"), FGFVillainRules::CanDamage(ReadyDino, Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now)).Code, FName(TEXT("Ok")));
	TestEqual(TEXT("And bonks a civilian"), FGFVillainRules::CanDamage(ReadyDino, Calm, HitQ(Now)).Code, FName(TEXT("Bonk")));

	const FGFVillainSide Rental = FGFVillainRules::DinoSide(Ready, TEXT("Rental"), EGFVillainPlace::Wild, true);
	TestEqual(TEXT("Rentals still refuse"), FGFVillainRules::CanDamage(Rental, Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now)).Code, FName(TEXT("Rental")));

	const FGFVillainSide Pacifist = FGFVillainRules::DinoSide(Calm, TEXT("Pet"), EGFVillainPlace::Wild, false);
	TestEqual(TEXT("A civilian's dino is not a villain"), FGFVillainRules::CanDamage(Pacifist, Villain(TEXT("B"), true, EGFVillainPlace::Wild), HitQ(Now)).Code, FName(TEXT("NotVillain")));
	return true;
}

#endif
