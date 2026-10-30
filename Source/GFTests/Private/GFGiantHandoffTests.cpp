// Big Grumble handoff and taunt, no world: the Handoff lines and their tokens, who he prefers (a recent unprotect or a
// live grab, ahead of a taunt, ahead of his current target), one thanks line, protected players staying safe, and the
// jail box still keeping him out.
#include "GFGiantTypes.h"
#include "GFSocialTypes.h"
#include "GFDataTables.h"
#include "GFLineTokens.h"
#include "GFSafeZone.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFGiantHandoffTest
{
	static const FName LandA(TEXT("LandA"));
	static const FName LandB(TEXT("LandB"));

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

	static FGFSafeRegion Land(FName Id, const FVector& Center, float Radius = 1500.f)
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

	static FGFGiantPlayerView Who(const TCHAR* Key, const FVector& At, FName Group, double Play, int64 FirstSeen = 0)
	{
		FGFGiantPlayerView P;
		P.Key = Key;
		P.Name = FString(Key) + TEXT("Name");
		P.Location = At;
		P.PlaySeconds = Play;
		P.FirstSeenTicks = FirstSeen;
		P.RunSpeed = 600.f;
		P.GroupId = Group;
		return P;
	}

	static FGFGiantTuning Quiet()
	{
		FGFGiantTuning T;
		T.ShoveRange = 0.f;
		T.bEatIdleTarget = false;
		T.EventSeconds = 120.f;
		return T;
	}

	static FGFGiantLines Script()
	{
		FGFGiantLines L;
		L.ByPhase.Add(EGFGiantLinePhase::Spotted, { { TEXT("S"), TEXT("Hi {Target}") } });
		L.ByPhase.Add(EGFGiantLinePhase::Handoff, { { TEXT("GD_Handoff_04"), TEXT("{Dragger}, you get a gold star. {Target}, you get eaten.") } });
		L.ByPhase.Add(EGFGiantLinePhase::Chase, { { TEXT("C"), TEXT("Run") } });
		L.ByPhase.Add(EGFGiantLinePhase::Target, { { TEXT("T"), TEXT("Hey {Target}") } });
		L.ByPhase.Add(EGFGiantLinePhase::Herded, { { TEXT("H"), TEXT("Home") } });
		L.ByPhase.Add(EGFGiantLinePhase::Circling, { { TEXT("R"), TEXT("Wait") } });
		L.ByPhase.Add(EGFGiantLinePhase::Leaving, { { TEXT("L"), TEXT("Bye") } });
		L.ByPhase.Add(EGFGiantLinePhase::Snacked, { { TEXT("K"), TEXT("Nom {Target}") } });
		return L;
	}

	static int32 CountPhase(const TArray<FGFGiantChatLine>& Chat, EGFGiantLinePhase Phase)
	{
		int32 N = 0;
		for (const FGFGiantChatLine& L : Chat)
		{
			N += L.Phase == Phase ? 1 : 0;
		}
		return N;
	}

	static void Offer(FGFGiantPlayerView& P, const TCHAR* DraggerKey, const TCHAR* DraggerName, float Age)
	{
		P.DraggerKey = DraggerKey;
		P.Dragger = DraggerName;
		P.DragAgeSeconds = Age;
	}

	static void Hold(FGFGiantPlayerView& P, const TCHAR* HolderKey, const TCHAR* HolderName)
	{
		P.bHeld = true;
		P.HolderKey = HolderKey;
		P.HolderName = HolderName;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantHandoffLinesTest, "GFGame.Giant.Handoff.Lines", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantHandoffLinesTest::RunTest(const FString&)
{
	using namespace GFGiantHandoffTest;
	FGFDataTables::ResetCache();
	const FGFGiantLines L = FGFGiantLines::Load();
	const TArray<FGFGiantLines::FLine>* Pool = L.ByPhase.Find(EGFGiantLinePhase::Handoff);
	if (!TestNotNull(TEXT("Handoff pool"), Pool))
	{
		return false;
	}
	TestEqual(TEXT("14 handoff lines"), Pool->Num(), 14);

	struct FRow { const TCHAR* Id; const TCHAR* Text; };
	const FRow Expect[] = {
		{ TEXT("GD_Handoff_01"), TEXT("Thanks! You're a real pal, {Dragger}.") },
		{ TEXT("GD_Handoff_02"), TEXT("Oh, delivery! Thank you, {Dragger}!") },
		{ TEXT("GD_Handoff_03"), TEXT("Gift wrapped? You shouldn't have, {Dragger}.") },
		{ TEXT("GD_Handoff_04"), TEXT("{Dragger}, you get a gold star. {Target}, you get eaten.") },
		{ TEXT("GD_Handoff_05"), TEXT("Hold 'em right there. Perfect. Thanks!") },
		{ TEXT("GD_Handoff_06"), TEXT("Teamwork! Mostly mine, but still.") },
		{ TEXT("GD_Handoff_07"), TEXT("Somebody ordered takeout. Was it me? It was me.") },
		{ TEXT("GD_Handoff_08"), TEXT("Ooh, room service. Five stars, {Dragger}.") },
		{ TEXT("GD_Handoff_09"), TEXT("{Target}, your friends don't like you much, huh?") },
		{ TEXT("GD_Handoff_10"), TEXT("No safety? No running? No problem. Thanks!") },
		{ TEXT("GD_Handoff_11"), TEXT("Aww, they saved you for me, {Target}.") },
		{ TEXT("GD_Handoff_12"), TEXT("I'd tip you, {Dragger}, but I ate my wallet.") },
		{ TEXT("GD_Handoff_13"), TEXT("Best neighbors ever. Same time tomorrow?") },
		{ TEXT("GD_Handoff_14"), TEXT("Thanks for the snack! Gulp. Okay, bye!") },
	};
	for (const FRow& Row : Expect)
	{
		const FGFGiantLines::FLine* Found = Pool->FindByPredicate([&](const FGFGiantLines::FLine& Line) { return Line.Id == Row.Id; });
		if (TestNotNull(*FString::Printf(TEXT("%s present"), Row.Id), Found))
		{
			TestEqual(*FString::Printf(TEXT("%s text"), Row.Id), Found->Text, FString(Row.Text));
		}
	}

	FGFGiantTuning T;
	FRandomStream Rng(3);
	TArray<FName> Recent;
	for (int32 i = 0; i < 30; ++i)
	{
		FName Id;
		FString Text;
		TestTrue(TEXT("Picked a handoff line"), FGFGiantRules::PickLine(T, L, EGFGiantLinePhase::Handoff, Recent, Rng, Id, Text));
		TestTrue(TEXT("Id is a handoff id"), Id.ToString().StartsWith(TEXT("GD_Handoff_")));
		Recent.Add(Id);
	}

	TestEqual(TEXT("Both tokens"), FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Expect[3].Text, TEXT("Pip")), TEXT("Rex")),
		FString(TEXT("Rex, you get a gold star. Pip, you get eaten.")));
	TestEqual(TEXT("Dragger only"), FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Expect[0].Text, TEXT("Pip")), TEXT("Rex")),
		FString(TEXT("Thanks! You're a real pal, Rex.")));
	TestEqual(TEXT("Target only"), FGFLineTokens::FillDragger(FGFGiantRules::FillTarget(Expect[8].Text, TEXT("Pip")), TEXT("Rex")),
		FString(TEXT("Pip, your friends don't like you much, huh?")));
	TestEqual(TEXT("No dragger drops the lead-in"), FGFLineTokens::FillDragger(Expect[0].Text, FString()),
		FString(TEXT("Thanks! You're a real pal.")));

	const FGFEmoteRow* Taunt = FGFSocialRules::FindEmote(TEXT("Taunt"));
	const FGFEmoteRow* Heart = FGFSocialRules::FindEmote(TEXT("Heart"));
	if (TestNotNull(TEXT("Taunt emote"), Taunt) && TestNotNull(TEXT("Heart emote"), Heart))
	{
		TestTrue(TEXT("Taunt calls him"), FGFSocialRules::EmoteTauntsGiant(*Taunt));
		TestEqual(TEXT("Taunt radius lives in the table"), Taunt->TauntRadius, 4500.f);
		TestFalse(TEXT("Heart does not call him"), FGFSocialRules::EmoteTauntsGiant(*Heart));
		TestEqual(TEXT("Heart radius"), Heart->TauntRadius, 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantHandoffPriorityTest, "GFGame.Giant.Handoff.Priority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantHandoffPriorityTest::RunTest(const FString&)
{
	using namespace GFGiantHandoffTest;
	FGFGiantTuning T = Quiet();
	TestEqual(TEXT("Window default"), T.HandoffWindowSeconds, 30.f);
	FProtLand Zone;
	Zone.Regions = { Land(LandA, FVector::ZeroVector) };
	const TArray<FBox> None;

	FGFGiantPlayerView Fresh = Who(TEXT("V"), FVector(200.f, 0.f, 0.f), LandB, 9000.0, 1);
	Fresh.Name = TEXT("Pip");
	Offer(Fresh, TEXT("R"), TEXT("Rex"), 1.f);
	TestTrue(TEXT("A fresh revoke is a handoff"), FGFGiantRules::IsHandoffCandidate(T, Fresh));
	Fresh.DragAgeSeconds = 30.f;
	TestTrue(TEXT("The window is inclusive"), FGFGiantRules::IsHandoffCandidate(T, Fresh));
	Fresh.DragAgeSeconds = 30.01f;
	TestFalse(TEXT("A stale revoke is not"), FGFGiantRules::IsHandoffCandidate(T, Fresh));
	Fresh.DragAgeSeconds = 1.f;
	Fresh.DraggerKey = Fresh.Key;
	TestFalse(TEXT("Unprotecting yourself is not a handoff"), FGFGiantRules::IsHandoffCandidate(T, Fresh));
	FGFGiantPlayerView Held = Who(TEXT("V"), FVector(200.f, 0.f, 0.f), LandB, 9000.0, 1);
	Hold(Held, TEXT("R"), TEXT("Rex"));
	TestTrue(TEXT("A live grab is a handoff with no revoke"), FGFGiantRules::IsHandoffCandidate(T, Held));
	TestEqual(TEXT("Holder name fills {Dragger} when nobody revoked"), FGFGiantRules::SetupName(Held), FString(TEXT("Rex")));
	Offer(Held, TEXT("R"), TEXT("Nia"), 4.f);
	TestEqual(TEXT("A revoke name wins over the holder"), FGFGiantRules::SetupName(Held), FString(TEXT("Nia")));

	// The visitor was just unprotected on the land. A newer player is out in the wild. He goes for the visitor.
	{
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("W"), FVector(5000.f, 0.f, 0.f), LandB, 10.0, 2),
			Who(TEXT("V"), FVector(200.f, 0.f, 0.f), LandB, 9000.0, 1) };
		P[1].Name = TEXT("Pip");
		Offer(P[1], TEXT("R"), TEXT("Rex"), 2.f);
		TestEqual(TEXT("Without the handoff rule the wild player wins"), FGFGiantRules::PickTarget(T, Zone, P, None), 0);
		const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, Zone, P, None, nullptr);
		TestEqual(TEXT("Handoff reason"), int32(Pick.Reason), int32(EGFGiantInterest::Handoff));
		TestEqual(TEXT("He takes the visitor"), Pick.Index, 1);

		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Script(), Zone, P, false, 4, Out);
		TestEqual(TEXT("Arrives on the visitor"), Sim.TargetKey, FString(TEXT("V")));
		TestEqual(TEXT("Opens with Spotted"), Out.Chat.Num() > 0 ? int32(Out.Chat[0].Phase) : -1, int32(EGFGiantLinePhase::Spotted));
		TestEqual(TEXT("Then one thanks"), CountPhase(Out.Chat, EGFGiantLinePhase::Handoff), 1);
		TestEqual(TEXT("Thanks names both"), Out.Chat.Num() > 1 ? Out.Chat[1].Text : FString(),
			FString(TEXT("Rex, you get a gold star. Pip, you get eaten.")));
		TestEqual(TEXT("All chat"), Out.Chat.Num() > 1 ? Out.Chat[1].Channel : FName(), FName(TEXT("All")));
		int32 Extra = 0;
		for (int32 Step = 0; Step < 80 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			Extra += CountPhase(O.Chat, EGFGiantLinePhase::Handoff);
		}
		TestEqual(TEXT("No second thanks for the same target"), Extra, 0);
		TestEqual(TEXT("Still on the visitor"), Sim.TargetKey, FString(TEXT("V")));
	}

	// He is already chasing the wild player. A hold appears. He switches, and the thanks names the holder.
	{
		TArray<FGFGiantPlayerView> P = { Who(TEXT("W"), FVector(5000.f, 0.f, 0.f), LandB, 10.0, 2) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Script(), Zone, P, false, 4, Out);
		TestEqual(TEXT("Starts on the wild player"), Sim.TargetKey, FString(TEXT("W")));
		TestEqual(TEXT("No thanks yet"), CountPhase(Out.Chat, EGFGiantLinePhase::Handoff), 0);
		P.Add(Who(TEXT("V"), FVector(250.f, 40.f, 0.f), LandB, 9000.0, 1));
		P.Last().Name = TEXT("Pip");
		Hold(P.Last(), TEXT("R"), TEXT("Rex"));
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		TestEqual(TEXT("Switches off his current target"), Sim.TargetKey, FString(TEXT("V")));
		TestEqual(TEXT("One thanks for the hold"), CountPhase(O.Chat, EGFGiantLinePhase::Handoff), 1);
		TestEqual(TEXT("Holder is thanked"), O.Chat.Num() > 0 ? O.Chat[0].Text : FString(),
			FString(TEXT("Rex, you get a gold star. Pip, you get eaten.")));
	}

	// A hold beats a newer revoke. A stale revoke does not beat the newest wild player.
	{
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("A"), FVector(4000.f, 0.f, 0.f), LandB, 8000.0, 1),
			Who(TEXT("B"), FVector(4200.f, 100.f, 0.f), LandB, 10.0, 9) };
		Hold(P[0], TEXT("R"), TEXT("Rex"));
		Offer(P[1], TEXT("Q"), TEXT("Quin"), 0.5f);
		const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, Zone, P, None, nullptr);
		TestEqual(TEXT("The held player wins"), Pick.Index, 0);

		P[0].bHeld = false;
		P[0].HolderKey.Reset();
		P[0].HolderName.Reset();
		Offer(P[0], TEXT("R"), TEXT("Rex"), 31.f);
		TArray<FGFGiantPlayerView> Stale = { P[0], Who(TEXT("W"), FVector(4500.f, 0.f, 0.f), LandB, 10.0, 3) };
		TestEqual(TEXT("A stale revoke loses to the newest"), FGFGiantRules::PickInterest(T, Zone, Stale, None, nullptr).Index, 1);
		TestEqual(TEXT("Same as the ordinary pick"), FGFGiantRules::PickInterest(T, Zone, Stale, None, nullptr).Index, FGFGiantRules::PickTarget(T, Zone, Stale, None));
	}

	// A second setup gets its own thanks. The first target is gone.
	{
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("A"), FVector(300.f, 0.f, 0.f), LandB, 50.0, 1),
			Who(TEXT("B"), FVector(3600.f, 0.f, 0.f), LandB, 80.0, 2) };
		P[0].Name = TEXT("Ada");
		P[1].Name = TEXT("Bea");
		Hold(P[0], TEXT("R"), TEXT("Rex"));
		Offer(P[1], TEXT("Q"), TEXT("Quin"), 3.f);
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Script(), Zone, P, false, 1, Out);
		TestEqual(TEXT("Hold wins at the start"), Sim.TargetKey, FString(TEXT("A")));
		TArray<FGFGiantPlayerView> OnlyB = { P[1] };
		FGFGiantStepOut O;
		Sim.Step(0.1f, OnlyB, O);
		TestEqual(TEXT("The other setup is next"), Sim.TargetKey, FString(TEXT("B")));
		TestEqual(TEXT("A second thanks"), CountPhase(O.Chat, EGFGiantLinePhase::Handoff), 1);
		TestTrue(TEXT("Names the second dragger"), O.Chat.Num() > 0 && O.Chat[0].Text.Contains(TEXT("Quin")) && O.Chat[0].Text.Contains(TEXT("Bea")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantHandoffTauntTest, "GFGame.Giant.Handoff.Taunt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantHandoffTauntTest::RunTest(const FString&)
{
	using namespace GFGiantHandoffTest;
	FGFGiantTuning T = Quiet();
	TestEqual(TEXT("Taunt interest default"), T.TauntInterestSeconds, 15.f);
	TestEqual(TEXT("Fallback radius default"), T.TauntNoticeRadius, 4500.f);
	FProtLand Zone;
	Zone.Regions = { Land(LandA, FVector::ZeroVector, 2000.f) };

	// Two exposed players. He starts on the newest, then a taunt at the other one pulls him over.
	{
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("Near"), FVector(3500.f, 0.f, 0.f), LandB, 10.0, 1),
			Who(TEXT("Far"), FVector(9000.f, 0.f, 0.f), LandB, 8000.0, 2) };
		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.NoticeTaunt(P[1].Location, 800.f);
		TestFalse(TEXT("A taunt before he is out does nothing"), Sim.bTaunt);
		Sim.Begin(T, Script(), Zone, P, false, 5, Out);
		TestEqual(TEXT("Starts on the newest"), Sim.TargetKey, FString(TEXT("Near")));
		const float Started = Sim.Location.X;
		Sim.NoticeTaunt(P[1].Location, 800.f);
		TestTrue(TEXT("Taunt sticks while he is out"), Sim.bTaunt);
		TestEqual(TEXT("Uses the call's radius"), Sim.TauntRadius, 800.f);
		for (int32 Step = 0; Step < 8 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			TestTrue(TEXT("Nobody eaten"), O.Eaten.IsEmpty());
		}
		TestEqual(TEXT("He retargets into the taunt"), Sim.TargetKey, FString(TEXT("Far")));
		TestTrue(TEXT("He moves toward that player"), Sim.Location.X > Started + 100.f);
	}

	// A handoff anywhere beats the taunt.
	{
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("V"), FVector(3600.f, 0.f, 0.f), LandB, 9000.0, 1),
			Who(TEXT("Far"), FVector(9000.f, 0.f, 0.f), LandB, 10.0, 2) };
		Offer(P[0], TEXT("R"), TEXT("Rex"), 1.f);
		FGFGiantNotice Notice;
		Notice.bActive = true;
		Notice.At = P[1].Location;
		Notice.Radius = 1000.f;
		const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, Zone, P, TArray<FBox>(), &Notice);
		TestEqual(TEXT("Handoff beats the taunt"), Pick.Index, 0);
		TestEqual(TEXT("Reason stays handoff"), int32(Pick.Reason), int32(EGFGiantInterest::Handoff));
	}

	// Taunt on the land, only protected players there, an exposed player far away. He walks up to the land and does
	// not enter it, and he never shoves or eats the protected players.
	{
		T.ShoveRange = 700.f;
		T.TauntInterestSeconds = 20.f;
		T.EventSeconds = 40.f;
		Zone.Protected.Add(TEXT("Guest"));
		TArray<FGFGiantPlayerView> P = {
			Who(TEXT("Home"), FVector(0.f, 0.f, 0.f), LandA, 5.0, 1),
			Who(TEXT("Guest"), FVector(300.f, 0.f, 0.f), LandB, 6.0, 2),
			Who(TEXT("Wild"), FVector(8000.f, 0.f, 0.f), LandB, 10.0, 3) };
		Hold(P[1], TEXT("Home"), TEXT("HomeName"));
		Offer(P[1], TEXT("Home"), TEXT("HomeName"), 1.f);
		TestFalse(TEXT("Member is not exposed"), FGFGiantRules::IsExposed(Zone, TArray<FBox>(), P[0]));
		TestFalse(TEXT("Protected guest is not exposed"), FGFGiantRules::IsExposed(Zone, TArray<FBox>(), P[1]));
		const FGFGiantPick Ignored = FGFGiantRules::PickInterest(T, Zone, P, TArray<FBox>(), nullptr);
		TestEqual(TEXT("Ordinary pick is the wild player"), Ignored.Index, 2);

		FGFGiantSim Sim;
		FGFGiantStepOut Out;
		Sim.Begin(T, Script(), Zone, P, false, 6, Out);
		TestEqual(TEXT("Arrives on the wild player"), Sim.TargetKey, FString(TEXT("Wild")));
		const float Started = Sim.Location.X;
		Sim.NoticeTaunt(FVector::ZeroVector, 1500.f);
		int32 Inside = 0, Shoves = 0;
		for (int32 Step = 0; Step < 140 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
			Inside += Zone.IsInsideSafe(Sim.Location) ? 1 : 0;
			Inside += FGFSafeZone::IsClear(Zone, Sim.Location, T.GiantRadius) ? 0 : 1;
			Shoves += O.Shoves.Num();
			TestTrue(TEXT("Protected players are not eaten"), O.Eaten.IsEmpty());
			TestTrue(TEXT("He does not peel off to the wild player during the look"), Sim.TargetKey.IsEmpty());
		}
		TestEqual(TEXT("Never enters the land"), Inside, 0);
		TestEqual(TEXT("Never shoves a protected player"), Shoves, 0);
		TestTrue(TEXT("He closed on the taunt"), Sim.Location.X < Started - 1000.f);
		FGFGiantStepOut Hit;
		TestFalse(TEXT("A protected attacker is not eaten"), Sim.Attacked(TEXT("Home"), P, Hit));
		TestFalse(TEXT("A protected guest who hits him is not eaten"), Sim.Attacked(TEXT("Guest"), P, Hit));
		TestTrue(TEXT("Still running"), Sim.IsActive() && Hit.Eaten.IsEmpty());

		// The interest ends. He goes back to the exposed player. Begin copied the tuning, so the short window is set on the sim.
		Sim.T.TauntInterestSeconds = 0.4f;
		Sim.NoticeTaunt(FVector::ZeroVector, 1500.f);
		for (int32 Step = 0; Step < 8 && Sim.IsActive(); ++Step)
		{
			FGFGiantStepOut O;
			Sim.Step(0.1f, P, O);
		}
		TestEqual(TEXT("After the taunt he hunts the exposed player again"), Sim.TargetKey, FString(TEXT("Wild")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGiantHandoffJailTest, "GFGame.Giant.Handoff.Jail", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFGiantHandoffJailTest::RunTest(const FString&)
{
	using namespace GFGiantHandoffTest;
	FGFGiantTuning T = Quiet();
	T.EventSeconds = 40.f;
	T.TauntInterestSeconds = 30.f;
	T.ShoveRange = 900.f;
	const FBox Box(FVector(-300.f, -300.f, -100.f), FVector(300.f, 300.f, 500.f));
	const TArray<FBox> Boxes = { Box };
	FProtLand Wild;

	// The jailed player is the newest and a handoff. The free player is neither. The box still wins.
	TArray<FGFGiantPlayerView> P = {
		Who(TEXT("J"), FVector(0.f, 0.f, 100.f), LandB, 1.0, 9),
		Who(TEXT("F"), FVector(8000.f, 0.f, 0.f), LandB, 500.0, 1) };
	P[0].Name = TEXT("Joe");
	Hold(P[0], TEXT("R"), TEXT("Rex"));
	Offer(P[0], TEXT("R"), TEXT("Rex"), 0.f);
	TestFalse(TEXT("Jailed player is not exposed"), FGFGiantRules::IsExposed(Wild, Boxes, P[0]));
	TestTrue(TEXT("They would have been a handoff in the open"), FGFGiantRules::IsHandoffCandidate(T, P[0]));
	const FGFGiantPick Pick = FGFGiantRules::PickInterest(T, Wild, P, Boxes, nullptr);
	TestEqual(TEXT("The free player is the target"), Pick.Index, 1);
	TestEqual(TEXT("Not a handoff"), int32(Pick.Reason), int32(EGFGiantInterest::Normal));

	FGFGiantSim Sim;
	Sim.NoEntry = Boxes;
	FGFGiantStepOut Out;
	Sim.Begin(T, Script(), Wild, P, false, 11, Out);
	TestEqual(TEXT("Arrives on the free player"), Sim.TargetKey, FString(TEXT("F")));
	TestEqual(TEXT("No thanks for someone he can't reach"), CountPhase(Out.Chat, EGFGiantLinePhase::Handoff), 0);

	// A taunt inside the cell. He walks up to the box and does not go in, and he never takes the jailed player.
	Sim.NoticeTaunt(FVector::ZeroVector, 400.f);
	int32 Violations = 0, JailedTouched = 0;
	float Closest = TNumericLimits<float>::Max();
	for (int32 Step = 0; Step < 220 && Sim.IsActive(); ++Step)
	{
		FGFGiantStepOut O;
		Sim.Step(0.1f, P, O);
		TestNotEqual(TEXT("Never targets the jailed player"), Sim.TargetKey, FString(TEXT("J")));
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
	TestEqual(TEXT("Never inside the jail (body included)"), Violations, 0);
	TestTrue(TEXT("Came up to the jail"), Closest < T.GiantRadius + 300.f);
	TestEqual(TEXT("Never shoved or ate the jailed player"), JailedTouched, 0);
	FGFGiantStepOut Hit;
	TestFalse(TEXT("A jailed attacker is still out of reach"), Sim.IsActive() && Sim.Attacked(TEXT("J"), P, Hit));
	AddInfo(FString::Printf(TEXT("Closest approach to the jail: %.1f cm"), Closest));
	return true;
}

#endif
