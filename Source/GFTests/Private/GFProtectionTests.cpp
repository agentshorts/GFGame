// Faction protection (option C), pure rules: members always safe on their own land and can't be toggled,
// the host always protected, visitors start unprotected, the leader toggles per player, the per-member grant (only the
// leader grants it; it lets a member toggle and set guests), guests get rights plus protection, toggles and Guest
// need the target on the setter's land, Unguest works from anywhere, visitors without guest status lose build / break / jail power on
// another faction's land. Also the IGFSafeZoneQuery defaults (IsSafeFor = IsInsideSafe, OwnerAt = None) and the
// {Dragger} line token. Faction ids here are PA / PB / PC; real ids live in DefaultGame.ini.
#include "Misc/AutomationTest.h"
#include "GFLineTokens.h"
#include "GFSafeZone.h"
#include "GFSocialTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFProtectionTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const FName PA(TEXT("PA"));
	static const FName PB(TEXT("PB"));
	static const FName PC(TEXT("PC"));

	static FGFSocialPlayer Who(const TCHAR* Key, FName Faction, bool bHost = false, bool bLeader = false)
	{
		return FGFSocialPlayer(Key, Faction, bHost, true, bLeader);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFProtectionRulesTest, "GFGame.Protection.Rules", GFProtectionTest::Flags)
bool FGFProtectionRulesTest::RunTest(const FString&)
{
	using namespace GFProtectionTest;
	using C = FGFSocialCodes;
	using R = FGFProtectionRules;
	FGFFactionAccess A;
	const FGFSocialPlayer Leader = Who(TEXT("L"), PA, false, true);
	const FGFSocialPlayer Member = Who(TEXT("M"), PA);
	const FGFSocialPlayer Member2 = Who(TEXT("M2"), PA);
	FGFSocialPlayer Visitor = Who(TEXT("V"), PB);
	Visitor.LandOwner = PA; // standing on the leader's land
	FGFSocialPlayer Loner = Who(TEXT("N"), NAME_None);
	Loner.LandOwner = PA;
	const FGFSocialPlayer Host = Who(TEXT("H"), PB, true);

	// Members: always safe on their own land; nobody can turn it off.
	TestTrue(TEXT("Member is safe on own land"), R::IsProtectedOn(Member.Key, PA, false, PA, A));
	TestEqual(TEXT("Leader can't unprotect a member"), R::CanToggle(Leader, Member, A, false), C::SameFaction);
	TestEqual(TEXT("...or protect one"), R::CanToggle(Leader, Member, A, true), C::SameFaction);
	TestFalse(TEXT("Neutral / wild land is never safe, even for a member"), R::IsProtectedOn(Member.Key, PA, false, NAME_None, A));

	// Host: always protected on faction land, can't be toggled or guested.
	TestTrue(TEXT("Host is protected on another faction's land"), R::IsProtectedOn(Host.Key, PB, true, PA, A));
	TestTrue(TEXT("Host with no faction is protected on faction land"), R::IsProtectedOn(Host.Key, NAME_None, true, PA, A));
	TestFalse(TEXT("Host is not safe in the wild"), R::IsProtectedOn(Host.Key, PB, true, NAME_None, A));
	TestEqual(TEXT("Host can't be unprotected"), R::CanToggle(Leader, Host, A, false), C::TargetIsHost);
	TestEqual(TEXT("Host can't be made a guest"), R::CanSetGuest(Leader, Host, A), C::TargetIsHost);
	TestTrue(TEXT("Host keeps rights on other land"), R::HasVisitorRights(Host.Key, PB, true, PA, A));

	// Visitors start unprotected.
	TestFalse(TEXT("Visitor starts unprotected"), R::IsProtectedOn(Visitor.Key, PB, false, PA, A));
	TestFalse(TEXT("No-faction visitor starts unprotected"), R::IsProtectedOn(Loner.Key, NAME_None, false, PA, A));
	TestFalse(TEXT("No entry"), A.Find(PA, Visitor.Key) != nullptr);

	// The leader can always toggle, per player, on their own land.
	TestEqual(TEXT("Leader may protect a visitor"), R::CanToggle(Leader, Visitor, A, true), C::Ok);
	TestEqual(TEXT("Leader may protect a no-faction player"), R::CanToggle(Leader, Loner, A, true), C::Ok);
	A.FindOrAdd(PA, Visitor.Key).bProtected = true;
	TestTrue(TEXT("Protected visitor is safe on that land"), R::IsProtectedOn(Visitor.Key, PB, false, PA, A));
	TestFalse(TEXT("...only for that player"), R::IsProtectedOn(Loner.Key, NAME_None, false, PA, A));
	TestFalse(TEXT("...only on that faction's land"), R::IsProtectedOn(Visitor.Key, PB, false, PC, A));
	TestEqual(TEXT("Leader may unprotect"), R::CanToggle(Leader, Visitor, A, false), C::Ok);
	FGFSocialPlayer Jailed = Leader;
	Jailed.bJailed = true;
	TestEqual(TEXT("Jailed leader can't toggle"), R::CanToggle(Jailed, Visitor, A, false), C::Jailed);
	TestEqual(TEXT("No faction, no land to toggle"), R::CanToggle(Loner, Visitor, A, false), C::NoFaction);
	TestEqual(TEXT("Not on yourself"), R::CanToggle(Leader, Leader, A, false), C::Self);
	FGFSocialPlayer Away = Visitor;
	Away.LandOwner = PC;
	TestEqual(TEXT("Target on another faction's land"), R::CanToggle(Leader, Away, A, true), C::NotOnYourLand);
	Away.LandOwner = NAME_None;
	TestEqual(TEXT("Target on unowned land"), R::CanToggle(Leader, Away, A, false), C::NotOnYourLand);

	// Per-member toggle permission: only the leader grants or revokes it.
	TestEqual(TEXT("Member without the grant can't toggle"), R::CanToggle(Member, Visitor, A, false), C::NotAllowed);
	TestEqual(TEXT("Member can't grant"), R::CanGrantToggle(Member, Member2), C::NotAllowed);
	TestEqual(TEXT("Leader can't grant an outsider"), R::CanGrantToggle(Leader, Visitor), C::NotMember);
	TestEqual(TEXT("Leader can't grant themselves"), R::CanGrantToggle(Leader, Leader), C::Self);
	TestEqual(TEXT("Leader grants a member"), R::CanGrantToggle(Leader, Member), C::Ok);
	TestTrue(TEXT("Grant set"), A.SetToggleGrant(PA, Member.Key, true));
	TestFalse(TEXT("Granting twice changes nothing"), A.SetToggleGrant(PA, Member.Key, true));
	TestEqual(TEXT("Granted member can toggle"), R::CanToggle(Member, Visitor, A, false), C::Ok);
	TestFalse(TEXT("Grant is per faction"), A.HasToggleGrant(PB, Member.Key));
	TestEqual(TEXT("Granted member still can't grant others"), R::CanGrantToggle(Member, Member2), C::NotAllowed);
	TestEqual(TEXT("Granted member can't toggle a member either"), R::CanToggle(Member, Member2, A, false), C::SameFaction);
	TestTrue(TEXT("Leader revokes"), A.SetToggleGrant(PA, Member.Key, false));
	TestEqual(TEXT("Revoked member can't toggle"), R::CanToggle(Member, Visitor, A, false), C::NotAllowed);
	A.SetToggleGrant(PA, Member.Key, true);
	A.OnFactionChanged(Member.Key, PB);
	TestFalse(TEXT("Changing faction drops the grant"), A.HasToggleGrant(PA, Member.Key));

	// Guests: the leader, or a member the leader granted (the same grant as the toggle). Guest needs the target on the
	// setter's faction land. Unguest does not. Full rights plus protection; unguest before unprotect.
	A.FindOrAdd(PA, Visitor.Key).bProtected = false;
	TestFalse(TEXT("Plain visitor has no rights on other land"), R::HasVisitorRights(Visitor.Key, PB, false, PA, A));
	TestTrue(TEXT("...but full rights on their own"), R::HasVisitorRights(Visitor.Key, PB, false, PB, A));
	TestTrue(TEXT("...and on unowned land"), R::HasVisitorRights(Visitor.Key, PB, false, NAME_None, A));
	TestEqual(TEXT("Member without the grant can't make a guest"), R::CanSetGuest(Member, Visitor, A), C::NotAllowed);
	TestEqual(TEXT("Members aren't guests"), R::CanSetGuest(Leader, Member, A), C::SameFaction);
	TestEqual(TEXT("Leader makes a guest"), R::CanSetGuest(Leader, Visitor, A), C::Ok);
	A.SetToggleGrant(PA, Member.Key, true);
	TestEqual(TEXT("Granted member can Guest / Unguest"), R::CanSetGuest(Member, Visitor, A), C::Ok);
	TestEqual(TEXT("Granted member can't guest a member"), R::CanSetGuest(Member, Member2, A), C::SameFaction);
	TestEqual(TEXT("Granted member still can't grant others"), R::CanGrantToggle(Member, Member2), C::NotAllowed);
	FGFSocialPlayer GuestAway = Visitor;
	GuestAway.LandOwner = PC;
	TestEqual(TEXT("Guest: target on another faction's land"), R::CanSetGuest(Leader, GuestAway, A, true), C::NotOnYourLand);
	TestEqual(TEXT("...for a granted member too"), R::CanSetGuest(Member, GuestAway, A, true), C::NotOnYourLand);
	TestEqual(TEXT("Unguest from another faction's land"), R::CanSetGuest(Leader, GuestAway, A, false), C::Ok);
	TestEqual(TEXT("...for a granted member too"), R::CanSetGuest(Member, GuestAway, A, false), C::Ok);
	GuestAway.LandOwner = NAME_None;
	TestEqual(TEXT("Guest: target on unowned land"), R::CanSetGuest(Leader, GuestAway, A, true), C::NotOnYourLand);
	TestEqual(TEXT("Unguest from unowned land"), R::CanSetGuest(Leader, GuestAway, A, false), C::Ok);
	A.SetToggleGrant(PA, Member.Key, false);
	TestEqual(TEXT("Revoked member can't Guest"), R::CanSetGuest(Member, Visitor, A, true), C::NotAllowed);
	TestEqual(TEXT("Revoked member can't Unguest"), R::CanSetGuest(Member, GuestAway, A, false), C::NotAllowed);
	A.FindOrAdd(PA, Visitor.Key).bGuest = true;
	TestTrue(TEXT("Guest is protected"), R::IsProtectedOn(Visitor.Key, PB, false, PA, A));
	TestTrue(TEXT("Guest has full rights"), R::HasVisitorRights(Visitor.Key, PB, false, PA, A));
	TestEqual(TEXT("A guest can't be unprotected"), R::CanToggle(Leader, Visitor, A, false), C::TargetIsGuest);
	TestEqual(TEXT("...or Protected on top (already protected; Unguest decides)"), R::CanToggle(Leader, Visitor, A, true), C::TargetIsGuest);
	A.OnFactionChanged(Visitor.Key, PA);
	TestTrue(TEXT("Joining the faction drops the visitor entry"), A.Find(PA, Visitor.Key) == nullptr);

	// Jail power: none for a visitor without guest status on another faction's land.
	FGFSocialPlayer LeaderB = Who(TEXT("LB"), PB, false, true);
	const FGFSocialPlayer MemberB = Who(TEXT("MB"), PB);
	TestEqual(TEXT("Leader jails own member at home"), FGFSocialRules::JailPower(LeaderB, MemberB), C::Ok);
	LeaderB.bVisitorNoRights = true;
	TestEqual(TEXT("...not as a visitor on other land"), FGFSocialRules::JailPower(LeaderB, MemberB), C::VisitorNoRights);
	FGFSocialPlayer HostVisiting = Host;
	HostVisiting.bVisitorNoRights = true;
	TestEqual(TEXT("Host keeps jail power"), FGFSocialRules::JailPower(HostVisiting, MemberB), C::Ok);

	// Powers: the target must be on safe land and, for a visitor, unprotected. Protected targets (members at home,
	// the host, guests, protected visitors) are immune.
	FGFSocialTuning Tun;
	FGFSocialPlayer Caster = Who(TEXT("C"), PA);
	FGFSocialPlayer Target = Who(TEXT("T"), PB);
	TestEqual(TEXT("Unprotected visitor on safe land: power ok"), FGFSocialRules::CheckPower(Caster, Target, 100.f, false, Tun), C::Ok);
	Target.bProtected = true;
	TestEqual(TEXT("Protected target: immune"), FGFSocialRules::CheckPower(Caster, Target, 100.f, false, Tun), C::TargetProtected);
	Target.bProtected = false;
	Target.bSafe = false;
	TestEqual(TEXT("Target off safe land"), FGFSocialRules::CheckPower(Caster, Target, 100.f, false, Tun), C::TargetNotSafe);
	FGFSocialPlayer HostTarget = Who(TEXT("HT"), PB, true);
	HostTarget.bProtected = true;
	TestEqual(TEXT("Host target: immune"), FGFSocialRules::CheckPower(Caster, HostTarget, 100.f, false, Tun), C::TargetProtected);
	TestEqual(TEXT("Own faction: still SameFaction"), FGFSocialRules::CheckPower(Caster, Who(TEXT("C2"), PA), 100.f, false, Tun), C::SameFaction);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFProtectionQueryDefaultsTest, "GFGame.Protection.QueryDefaults", GFProtectionTest::Flags)
bool FGFProtectionQueryDefaultsTest::RunTest(const FString&)
{
	FGFSafeRegion Region;
	Region.Id = TEXT("Home");
	Region.BaseRadius = 1000.f;
	const FGFSafeCircles Circles(TArray<FGFSafeRegion>{ Region });
	const IGFSafeZoneQuery& Q = Circles;
	const FGFSafeSubject Anyone(TEXT("K"), TEXT("PA"));
	const FGFSafeSubject Host(TEXT("H"), NAME_None, true);
	for (const FVector& At : { FVector(0.f, 0.f, 0.f), FVector(900.f, 0.f, 50.f), FVector(1100.f, 0.f, 0.f), FVector(-5000.f, 3000.f, 0.f) })
	{
		TestEqual(TEXT("Default IsSafeFor is IsInsideSafe"), Q.IsSafeFor(Anyone, At), Q.IsInsideSafe(At));
		TestEqual(TEXT("...for the host too"), Q.IsSafeFor(Host, At), Q.IsInsideSafe(At));
		TestTrue(TEXT("Plain circles have no owners"), Q.OwnerAt(At).IsNone());
	}

	// {Dragger} next to {Target}.
	TestEqual(TEXT("Dragger filled"), FGFLineTokens::FillDragger(TEXT("{Dragger} left {Target} out here"), TEXT("Rex")), FString(TEXT("Rex left {Target} out here")));
	TestEqual(TEXT("Case-insensitive"), FGFLineTokens::FillDragger(TEXT("Thank {dragger}!"), TEXT("Rex")), FString(TEXT("Thank Rex!")));
	TestEqual(TEXT("No dragger: comma lead-in dropped"), FGFLineTokens::FillDragger(TEXT("Out you go, {Dragger}!"), FString()), FString(TEXT("Out you go!")));
	TestEqual(TEXT("No dragger: leading token reads as someone"), FGFLineTokens::FillDragger(TEXT("{Dragger} did this"), FString()), FString(TEXT("someone did this")));
	TestEqual(TEXT("No dragger: mid-line token reads as someone"), FGFLineTokens::FillDragger(TEXT("Left here by {Dragger}."), FString()), FString(TEXT("Left here by someone.")));
	TestEqual(TEXT("Target token via Fill"), FGFLineTokens::Fill(TEXT("Hey {Target}"), FGFLineTokens::Target, TEXT("Pip")), FString(TEXT("Hey Pip")));
	return true;
}

#endif
