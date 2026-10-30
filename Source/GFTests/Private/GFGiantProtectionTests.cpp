// Big Grumble and faction protection, no world: who it may take (IGFSafeZoneQuery::IsSafeFor, so protection counts,
// and never anyone inside a no-entry box), the trigger, targets, shoves and eating by protection, walking into land
// after an unprotected visitor (never otherwise), protected attackers never eaten, no-entry boxes (it comes up to one
// but never into it; boxes above or below its body don't block), and {Dragger} for an unprotected target on land.
#include "GFGiantTypes.h"
#include "GFSafeZone.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantProtTest
{
	static const FName LandA(TEXT("LandA"));
	static const FName LandB(TEXT("LandB"));

	/** Circle regions owned by their Id: members (GroupId == Id), the host and listed protected keys are safe there. */
	struct FProtLand : public FGFSafeCircles
	{
		TSet<FString> Protected;

		virtual FName OwnerAt(const FVector& L) const override
		{
			for (const FGFSafeRegion& R : Regions)
			{
				if (FVector::Dist2D(L, R.Center) <= R.Radius())
				{
					return R.Id;
				}
			}
			return NAME_None;
		}
		virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& L) const override
		{
			const FName Owner = OwnerAt(L);
			return !Owner.IsNone() && (Who.bHost || Who.GroupId == Owner || Protected.Contains(Who.Key));
		}
	};

	static FGFSafeRegion Land(FName Id, const FVector& Center, float Radius = 1000.f)
	{
		FGFSafeRegion R;
		R.Id = Id;
		R.Center = Center;
		R.Members = 1;
		R.BaseRadius = Radius;
		R.RadiusPerExtraMember = 0.f;
		R.MaxRadius = Radius;
		return R;
	}

	static FGFGiantPlayerView Who(const TCHAR* Key, const FVector& At, FName Group, double Play, int64 FirstSeen = 0, bool bHost = false)
	{
		FGFGiantPlayerView P;
		P.Key = Key;
		P.Name = FString(Key) + TEXT("Name");
		P.Location = At;
		P.PlaySeconds = Play;
		P.FirstSeenTicks = FirstSeen;
		P.RunSpeed = 600.f;
		P.GroupId = Group;
		P.bHost = bHost;
		return P;
	}

	static int32 IndexOf(const TArray<FGFGiantPlayerView>& P, const TCHAR* Key)
	{
		return P.IndexOfByPredicate([&](const FGFGiantPlayerView& V) { return V.Key == Key; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantProtectionRulesTest, "GFGame.Giant.Protection.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantProtectionRulesTest::RunTest(const FString&)
{
	using namespace GFGiantProtTest;
	FGFGiantTuning T;
	FProtLand Zone;
	Zone.Regions = { Land(LandA, FVector::ZeroVector) };
	const TArray<FBox> None;
	TArray<FGFGiantPlayerView> P = {
		Who(TEXT("M"), FVector(200.f, 0.f, 0.f), LandA, 50.0, 1),        // member at home: safe, and the newest
		Who(TEXT("V"), FVector(300.f, 100.f, 0.f), LandB, 100.0, 2),     // unprotected visitor on LandA
		Who(TEXT("H"), FVector(-200.f, 0.f, 0.f), NAME_None, 10.0, 3, true), // the host
		Who(TEXT("W"), FVector(3000.f, 0.f, 0.f), LandB, 9000.0, 4) };   // out in the wild
	TestFalse(TEXT("Member at home is not exposed"), FGFGiantRules::IsExposed(Zone, None, P[0]));
	TestTrue(TEXT("Unprotected visitor on faction land is exposed"), FGFGiantRules::IsExposed(Zone, None, P[1]));
	TestFalse(TEXT("Host is not exposed"), FGFGiantRules::IsExposed(Zone, None, P[2]));
	TestTrue(TEXT("Player in the wild is exposed"), FGFGiantRules::IsExposed(Zone, None, P[3]));
	TestEqual(TEXT("Targets the unprotected visitor inside the land (newest exposed)"), FGFGiantRules::PickTarget(T, Zone, P), IndexOf(P, TEXT("V")));

	TArray<FGFGiantPlayerView> InLand = { P[0], P[1], P[2] };
	TestTrue(TEXT("Trigger counts an unprotected visitor inside land"), FGFGiantRules::AnyoneOutside(Zone, InLand));
	Zone.Protected.Add(TEXT("V"));
	TestFalse(TEXT("Protected visitor is not exposed"), FGFGiantRules::IsExposed(Zone, None, P[1]));
	TestFalse(TEXT("Nobody exposed once the visitor is protected"), FGFGiantRules::AnyoneOutside(Zone, InLand));
	TestEqual(TEXT("No target among protected players"), FGFGiantRules::PickTarget(T, Zone, InLand), int32(INDEX_NONE));
	TestEqual(TEXT("Falls back to the player in the wild"), FGFGiantRules::PickTarget(T, Zone, P), IndexOf(P, TEXT("W")));

	// No-entry boxes: anyone inside is out of reach, whatever their protection.
	const TArray<FBox> Cell = { FBox(FVector(2700.f, -300.f, -100.f), FVector(3300.f, 300.f, 500.f)) };
	TestFalse(TEXT("A player inside a no-entry box is not exposed"), FGFGiantRules::IsExposed(Zone, Cell, P[3]));
	TestEqual(TEXT("Nobody to target: everyone protected or in the box"), FGFGiantRules::PickTarget(T, Zone, P, Cell), int32(INDEX_NONE));
	TestFalse(TEXT("Trigger ignores a player in the box"), FGFGiantRules::AnyoneOutside(Zone, P, Cell));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantProtectionChaseTest, "GFGame.Giant.Protection.Chase", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantProtectionChaseTest::RunTest(const FString&)
{
	using namespace GFGiantProtTest;
	const FGFGiantLines Lines = FGFGiantLines::Load();
	FGFGiantTuning T;
	T.EventSeconds = 120.f;
	FProtLand Zone;
	Zone.Regions = { Land(LandA, FVector::ZeroVector, 1500.f) };

	// An unprotected visitor standing still deep in LandA, next to a member who also stands still: it walks into the
	// land, shoves only the visitor, and eats the visitor (never the member).
	{
		TArray<FGFGiantPlayerView> P = { Who(TEXT("V"), FVector(0.f, 0.f, 0.f), LandB, 10.0, 2), Who(TEXT("M"), FVector(250.f, 150.f, 0.f), LandA, 20.0, 1) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Lines, Zone, P, false, 7, Out);
		TestEqual(TEXT("Targets the visitor"), Sim.TargetKey, FString(TEXT("V")));
		TestTrue(TEXT("Arrives outside the land"), FGFSafeZone::IsClear(Zone, Sim.Location, T.GiantRadius));
		bool bEntered = false;
		int32 MemberShoves = 0, VisitorShoves = 0;
		FString Eaten;
		for (int32 Step = 0; Step < 1200 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			bEntered |= Zone.IsInsideSafe(Sim.Location);
			for (const FGFGiantShove& S : O.Shoves)
			{
				MemberShoves += S.Key == TEXT("M") ? 1 : 0;
				VisitorShoves += S.Key == TEXT("V") ? 1 : 0;
			}
			if (!O.Eaten.IsEmpty())
			{
				Eaten = O.Eaten;
			}
		}
		TestTrue(TEXT("Walked into the land after the visitor"), bEntered);
		TestTrue(TEXT("Shoved the visitor"), VisitorShoves > 0);
		TestEqual(TEXT("Never shoved the member at home"), MemberShoves, 0);
		TestEqual(TEXT("Ate the visitor who stood still"), Eaten, FString(TEXT("V")));
	}

	// Same spot, but the visitor is protected: nobody is exposed, it never enters, never shoves, never eats.
	{
		FProtLand Safe = Zone;
		Safe.Protected.Add(TEXT("V"));
		TArray<FGFGiantPlayerView> P = { Who(TEXT("V"), FVector(0.f, 0.f, 0.f), LandB, 10.0, 2), Who(TEXT("M"), FVector(250.f, 150.f, 0.f), LandA, 20.0, 1) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Lines, Safe, P, false, 7, Out);
		TestTrue(TEXT("No target"), Sim.TargetKey.IsEmpty());
		int32 Inside = 0, Shoves = 0;
		for (int32 Step = 0; Step < 600 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			Inside += FGFSafeZone::IsClear(Safe, Sim.Location, T.GiantRadius) ? 0 : 1;
			Shoves += O.Shoves.Num();
			TestTrue(TEXT("Nobody eaten"), O.Eaten.IsEmpty());
		}
		TestTrue(TEXT("Circles the land"), Sim.State == EGFGiantState::Circling);
		TestEqual(TEXT("Never enters land with nobody to take"), Inside, 0);
		TestEqual(TEXT("Never shoves protected players"), Shoves, 0);
	}

	// Attacks: a protected attacker (a member at home) is never eaten; an exposed one is.
	{
		TArray<FGFGiantPlayerView> P = { Who(TEXT("V"), FVector(3000.f, 0.f, 0.f), LandB, 10.0, 2), Who(TEXT("M"), FVector(300.f, 0.f, 0.f), LandA, 20.0, 1) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Lines, Zone, P, false, 3, Out);
		FGFGiantStepOut O;
		TestFalse(TEXT("Protected attacker isn't eaten"), Sim.Attacked(TEXT("M"), P, O));
		TestTrue(TEXT("Nothing happened"), O.Eaten.IsEmpty() && !O.bEnded && Sim.IsActive());
		TestFalse(TEXT("Unknown attacker isn't eaten"), Sim.Attacked(TEXT("Nobody"), P, O));
		TestTrue(TEXT("Exposed attacker is eaten"), Sim.Attacked(TEXT("V"), P, O));
		TestEqual(TEXT("Eaten: the exposed attacker"), O.Eaten, FString(TEXT("V")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantProtectionNoEntryTest, "GFGame.Giant.Protection.NoEntry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantProtectionNoEntryTest::RunTest(const FString&)
{
	using namespace GFGiantProtTest;
	FGFGiantTuning T;
	T.EventSeconds = 200.f;
	T.bEatIdleTarget = true;

	// Helpers.
	const FBox Box(FVector(-300.f, -300.f, -100.f), FVector(300.f, 300.f, 500.f));
	const TArray<FBox> Boxes = { Box };
	TestTrue(TEXT("Inside the box"), FGFNoEntry::IsInside(Boxes, FVector(0.f, 0.f, 100.f)));
	TestFalse(TEXT("Above the box"), FGFNoEntry::IsInside(Boxes, FVector(0.f, 0.f, 900.f)));
	const FVector Pushed = FGFNoEntry::KeepOut(Boxes, FVector(250.f, 20.f, 0.f), 350.f, 600.f);
	TestEqual(TEXT("Pushed out the nearest side (+X), clear by the body radius"), double(Pushed.X), 650.5, 0.6);
	TestEqual(TEXT("Y kept"), double(Pushed.Y), 20.0, 0.01);
	const TArray<FBox> Deep = { Box.ShiftBy(FVector(0.f, 0.f, -5000.f)) };
	TestEqual(TEXT("A box far below the body doesn't block"), FGFNoEntry::KeepOut(Deep, FVector(10.f, 10.f, 0.f), 350.f, 600.f), FVector(10.f, 10.f, 0.f));

	// The jailed player (inside the box) is the newest; a free player circles the box. It goes for the free one, comes
	// up to the box but never into it, and never touches the jailed one. No safe land at all (everyone unprotected).
	FProtLand Wild; // no regions: nobody is safe anywhere
	TArray<FGFGiantPlayerView> P = { Who(TEXT("J"), FVector(0.f, 0.f, 100.f), LandB, 1.0, 9), Who(TEXT("F"), FVector(900.f, 0.f, 0.f), LandB, 500.0, 1) };
	FGFGiantSim Sim;
	Sim.NoEntry = Boxes;
	FGFGiantStepOut Out;
	Sim.Begin(T, FGFGiantLines::Load(), Wild, P, false, 13, Out);
	TestEqual(TEXT("Targets the free player, not the jailed newbie"), Sim.TargetKey, FString(TEXT("F")));
	int32 Violations = 0, JailedTouched = 0;
	float Closest = TNumericLimits<float>::Max();
	for (int32 Step = 0; Step < 1200 && Sim.IsActive(); ++Step)
	{
		// F walks around the box at 900 cm, so the herding goal keeps crossing it.
		const float A = Step * 0.02f;
		P[1].Location = FVector(900.f * FMath::Cos(A), 900.f * FMath::Sin(A), 0.f);
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		const float DX = FMath::Max(FMath::Abs(Sim.Location.X) - 300.f, 0.f);
		const float DY = FMath::Max(FMath::Abs(Sim.Location.Y) - 300.f, 0.f);
		const float Gap = FMath::Max(DX, DY);
		Closest = FMath::Min(Closest, Gap);
		Violations += Gap < T.GiantRadius - 1.f ? 1 : 0;
		for (const FGFGiantShove& S : O.Shoves)
		{
			JailedTouched += S.Key == TEXT("J") ? 1 : 0;
		}
		JailedTouched += O.Eaten == TEXT("J") ? 1 : 0;
	}
	TestEqual(TEXT("Never inside the no-entry box (incl. its body radius)"), Violations, 0);
	TestTrue(TEXT("Came up to the box"), Closest < T.GiantRadius + 300.f);
	TestEqual(TEXT("Never shoved or ate the jailed player"), JailedTouched, 0);
	FGFGiantStepOut O;
	TestFalse(TEXT("A jailed attacker is out of reach"), Sim.IsActive() && Sim.Attacked(TEXT("J"), P, O));
	AddInfo(FString::Printf(TEXT("Closest approach to the box: %.1f cm"), Closest));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantProtectionDraggerTest, "GFGame.Giant.Protection.Dragger", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantProtectionDraggerTest::RunTest(const FString&)
{
	using namespace GFGiantProtTest;
	FGFGiantLines Custom;
	Custom.ByPhase.Add(EGFGiantLinePhase::Spotted, { { TEXT("S1"), TEXT("{Dragger} left {Target} out here") } });
	Custom.ByPhase.Add(EGFGiantLinePhase::Target, { { TEXT("T1"), TEXT("Thanks, {Dragger}. Hi {Target}") } });
	FGFGiantTuning T;
	T.ShoveRange = 0.f;
	T.bEatIdleTarget = false;
	T.TargetLineWeight = 1.f;
	FProtLand Zone;
	Zone.Regions = { Land(LandA, FVector::ZeroVector) };
	// The visitor was unprotected on LandA by someone (their LastDrag) and is still standing there.
	TArray<FGFGiantPlayerView> P = { Who(TEXT("V"), FVector(100.f, 0.f, 0.f), LandB, 10.0, 1) };
	P[0].Name = TEXT("Pip");
	P[0].Dragger = TEXT("Rex");
	P[0].DraggedAtUtcTicks = 1;
	FGFGiantSim Sim;
	FGFGiantStepOut Out;
	Sim.Begin(T, Custom, Zone, P, false, 2, Out);
	TestEqual(TEXT("Takes the unprotected target"), Sim.TargetKey, FString(TEXT("V")));
	TestEqual(TEXT("Opening line names the dragger"), Out.Chat.Num() > 0 ? Out.Chat[0].Text : FString(), FString(TEXT("Rex left Pip out here")));
	bool bTargetLine = false;
	for (int32 Step = 0; Step < 400 && !bTargetLine; ++Step)
	{
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		for (const FGFGiantChatLine& L : O.Chat)
		{
			if (L.Phase == EGFGiantLinePhase::Target)
			{
				bTargetLine = true;
				TestEqual(TEXT("Target line names the dragger"), L.Text, FString(TEXT("Thanks, Rex. Hi Pip")));
			}
		}
	}
	TestTrue(TEXT("Said a target line"), bTargetLine);
	return true;
}

#endif
