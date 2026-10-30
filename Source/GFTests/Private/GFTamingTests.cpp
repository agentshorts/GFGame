// Taming (PLAN.md M2): diet math, the state machine, aggro, the Field Journal and the diet data.
#include "Misc/AutomationTest.h"
#include "GFDinoTypes.h"
#include "GFDino.h"
#include "GFDataTables.h"
#include "GFItemTypes.h"
#include "GFRecipeTypes.h"
#include "GFGardenTypes.h"
#include "GFSaveGame.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TameFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	template <typename T> int32 E(T V) { return static_cast<int32>(V); }

	const FDinoDietRow* DietRow(FName Species)
	{
		const UDataTable* T = FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
		return T ? T->FindRow<FDinoDietRow>(Species, TEXT("Test"), false) : nullptr;
	}

	/** Feeds Food from Key until tamed (or Max offers); returns the offers used, -1 if never tamed. */
	int32 OffersToTame(const FDinoDietRow& Row, FName Food, int32 Max = 50)
	{
		FGFTameMachine M;
		float Now = 0.f;
		for (int32 i = 1; i <= Max; ++i)
		{
			Now += Row.OfferCooldown;
			if (M.OffersThisVisit >= Row.Fullness)
			{
				M.OffersThisVisit = 0; // pretend the player came back another visit
			}
			M.Offer(Row, Now, TEXT("A"), Food);
			if (M.IsTamed())
			{
				return i;
			}
		}
		return -1;
	}

	void TickFor(FGFTameMachine& M, const FDinoDietRow& Row, float& Now, float Seconds, const TArray<FGFTamerView>& Players)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / 0.1f);
		for (int32 i = 0; i < Steps; ++i)
		{
			Now += 0.1f;
			M.Tick(Row, Now, 0.1f, Players);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTameDietMathTest, "GFGame.Tame.DietMath", TameFlags)
bool FGFTameDietMathTest::RunTest(const FString&)
{
	const FDinoDietRow* RaptorPtr = DietRow(TEXT("Raptor"));
	if (!TestNotNull(TEXT("Raptor diet row"), RaptorPtr))
	{
		return false;
	}
	const FDinoDietRow& Raptor = *RaptorPtr;
	TestEqual(TEXT("Favourite"), E(FGFDinoDiet::Classify(Raptor, TEXT("JerkyBites"))), E(EGFFeedResult::Favorite));
	TestEqual(TEXT("Accepted"), E(FGFDinoDiet::Classify(Raptor, TEXT("Steakroot"))), E(EGFFeedResult::Accepted));
	TestEqual(TEXT("Refused"), E(FGFDinoDiet::Classify(Raptor, TEXT("Berry"))), E(EGFFeedResult::Rejected));
	TestEqual(TEXT("Nothing in hand is refused"), E(FGFDinoDiet::Classify(Raptor, NAME_None)), E(EGFFeedResult::Rejected));
	TestEqual(TEXT("Steakroot gain from the table"), FGFDinoDiet::BondGain(Raptor, TEXT("Steakroot")), 15.f);
	TestEqual(TEXT("Favourite = 3x mean accepted (12.33 -> 37)"), FGFDinoDiet::BondGain(Raptor, TEXT("JerkyBites")), 37.f);
	TestEqual(TEXT("Refused gives nothing"), FGFDinoDiet::BondGain(Raptor, TEXT("Berry")), 0.f);

	// Favourites fill the bar about 3x faster.
	const int32 ByFavourite = OffersToTame(Raptor, TEXT("JerkyBites"));
	const int32 ByFish = OffersToTame(Raptor, TEXT("Fish"));
	TestEqual(TEXT("Raptor: 3 favourites"), ByFavourite, 3);
	TestEqual(TEXT("Raptor: 9 river fish"), ByFish, 9);
	TestTrue(TEXT("Favourites ~3x faster"), ByFish >= ByFavourite * 2.5f);

	// Starter and helper: a friend's offers add 50% to the starter's bond; the starter owns the tame.
	FGFTameMachine M;
	float Now = 10.f;
	TestEqual(TEXT("A starts"), E(M.Offer(Raptor, Now, TEXT("A"), TEXT("JerkyBites"))), E(EGFOfferResult::Favourite));
	TestEqual(TEXT("Starter"), M.StarterKey, FString(TEXT("A")));
	TestEqual(TEXT("Too soon"), E(M.Offer(Raptor, Now + 1.f, TEXT("B"), TEXT("Steakroot"))), E(EGFOfferResult::Cooldown));
	Now += Raptor.OfferCooldown;
	TestEqual(TEXT("B helps"), E(M.Offer(Raptor, Now, TEXT("B"), TEXT("Steakroot"))), E(EGFOfferResult::Accepted));
	TestEqual(TEXT("Helper adds half"), M.Bond, 37.f + 7.5f);
	TestEqual(TEXT("Helper contribution"), M.Contributions.FindRef(TEXT("B")), 7.5f);
	Now += Raptor.OfferCooldown;
	M.Offer(Raptor, Now, TEXT("A"), TEXT("JerkyBites"));
	for (int32 i = 0; i < 3 && !M.IsTamed(); ++i)
	{
		Now += Raptor.OfferCooldown;
		M.Offer(Raptor, Now, TEXT("B"), TEXT("Steakroot"));
	}
	TestTrue(TEXT("Tamed (finished by the helper)"), M.IsTamed());
	TestEqual(TEXT("Joins the starter's roster"), M.OwnerKey, FString(TEXT("A")));
	TestEqual(TEXT("Bond capped"), M.Bond, 100.f);
	TestEqual(TEXT("Tamed: no more taming"), E(M.Offer(Raptor, Now + 10.f, TEXT("B"), TEXT("Steakroot"))), E(EGFOfferResult::NotTameable));

	// Fullness per visit, and decay while nobody is near.
	const FDinoDietRow* NipperPtr = DietRow(TEXT("Nipper"));
	if (TestNotNull(TEXT("Nipper diet row"), NipperPtr))
	{
		const FDinoDietRow& Nipper = *NipperPtr;
		FGFTameMachine N;
		float T = 0.f;
		for (int32 i = 0; i < Nipper.Fullness; ++i)
		{
			T += Nipper.OfferCooldown;
			TestEqual(TEXT("Treat accepted"), E(N.Offer(Nipper, T, TEXT("A"), TEXT("DinoTreat"))), E(EGFOfferResult::Accepted));
		}
		T += Nipper.OfferCooldown;
		TestEqual(TEXT("Full after Fullness offers"), E(N.Offer(Nipper, T, TEXT("A"), TEXT("DinoTreat"))), E(EGFOfferResult::Full));
		const float Before = N.Bond;
		TickFor(N, Nipper, T, 6.f, {}); // everyone left: the visit ends after 5 s
		TestTrue(TEXT("Bond decays while alone"), FMath::IsNearlyEqual(N.Bond, Before - 6.f * Nipper.BondDecayPerSec, 0.05f));
		TestEqual(TEXT("Visit over -> Wild"), E(N.State), E(EGFTameState::Wild));
		TestEqual(TEXT("Hungry again next visit"), E(N.Offer(Nipper, T + Nipper.OfferCooldown, TEXT("A"), TEXT("DinoTreat"))), E(EGFOfferResult::Accepted));
		TickFor(N, Nipper, T, 200.f, {});
		TestEqual(TEXT("Bond decays to 0"), N.Bond, 0.f);
		TestTrue(TEXT("Forgotten tame attempt clears the starter"), N.StarterKey.IsEmpty());
	}
	const FDinoDietRow* RexPtr = DietRow(TEXT("Rex"));
	TestTrue(TEXT("Rex decays slower"), RexPtr && RexPtr->BondDecayPerSec < Raptor.BondDecayPerSec);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTameStateFlowTest, "GFGame.Tame.StateFlow", TameFlags)
bool FGFTameStateFlowTest::RunTest(const FString&)
{
	const FDinoDietRow* RowPtr = DietRow(TEXT("Raptor"));
	if (!TestNotNull(TEXT("Raptor diet row"), RowPtr))
	{
		return false;
	}
	const FDinoDietRow& R = *RowPtr; // aggro 12 m, warn 15 m, calm 22.5 m, threat 2 s
	FGFTameMachine M;
	float Now = 0.f;

	TickFor(M, R, Now, 0.5f, { FGFTamerView(TEXT("A"), 3000.f, TEXT("Steakroot")) });
	TestEqual(TEXT("Far away: Wild"), E(M.State), E(EGFTameState::Wild));
	M.Tick(R, Now += 0.1f, 0.1f, { FGFTamerView(TEXT("A"), 1400.f, TEXT("Steakroot")) });
	TestEqual(TEXT("Inside warn radius: Alert (sniff)"), E(M.State), E(EGFTameState::Alert));
	M.Tick(R, Now += 0.1f, 0.1f, { FGFTamerView(TEXT("A"), 1400.f, TEXT("Steakroot")) });
	TestEqual(TEXT("Acceptable food in hand: Curious"), E(M.State), E(EGFTameState::Curious));
	Now += 3.f;
	TestEqual(TEXT("Feed"), E(M.Offer(R, Now, TEXT("A"), TEXT("Steakroot"))), E(EGFOfferResult::Accepted));
	TestEqual(TEXT("Feeding"), E(M.State), E(EGFTameState::Feeding));

	// Two refusals in a row: Threat.
	Now += 3.f;
	TestEqual(TEXT("Refused"), E(M.Offer(R, Now, TEXT("A"), TEXT("Berry"))), E(EGFOfferResult::Refused));
	TestEqual(TEXT("One refusal: still Feeding"), E(M.State), E(EGFTameState::Feeding));
	Now += 3.f;
	M.Offer(R, Now, TEXT("A"), TEXT("Berry"));
	TestEqual(TEXT("Second refusal: Threat"), E(M.State), E(EGFTameState::Threat));
	TestEqual(TEXT("Threat at the offerer"), M.TargetKey, FString(TEXT("A")));
	M.Tick(R, Now += 0.1f, 0.1f, { FGFTamerView(TEXT("A"), 300.f, TEXT("Steakroot")) });
	TestEqual(TEXT("Good food back in hand: Feeding again"), E(M.State), E(EGFTameState::Feeding));

	// Empty hands inside the aggro radius: Threat, then Hostile after ThreatSeconds.
	const TArray<FGFTamerView> Empty = { FGFTamerView(TEXT("A"), 1000.f, NAME_None) };
	M.Tick(R, Now += 0.1f, 0.1f, Empty);
	TestEqual(TEXT("Empty-handed: Threat"), E(M.State), E(EGFTameState::Threat));
	TickFor(M, R, Now, R.ThreatSeconds - 0.3f, Empty);
	TestEqual(TEXT("Still warning"), E(M.State), E(EGFTameState::Threat));
	TickFor(M, R, Now, 0.5f, Empty);
	TestEqual(TEXT("Hostile"), E(M.State), E(EGFTameState::Hostile));
	Now += 3.f;
	TestEqual(TEXT("Hostile ignores plain food"), E(M.Offer(R, Now, TEXT("A"), TEXT("Steakroot"))), E(EGFOfferResult::NotNow));
	TestEqual(TEXT("A favourite calms it instantly"), E(M.Offer(R, Now, TEXT("A"), TEXT("JerkyBites"))), E(EGFOfferResult::Favourite));
	TestEqual(TEXT("Calmed: Feeding"), E(M.State), E(EGFTameState::Feeding));

	// Hostile, then the player switches to an acceptable food: Alert after 3 s without being hit.
	M.ForceHostile(TEXT("A"));
	const TArray<FGFTamerView> Holding = { FGFTamerView(TEXT("A"), 1000.f, TEXT("Steakroot")) };
	TickFor(M, R, Now, 2.f, Holding);
	M.NotifyHit();
	TickFor(M, R, Now, 2.f, Holding);
	TestEqual(TEXT("Being hit resets the calm-down"), E(M.State), E(EGFTameState::Hostile));
	TickFor(M, R, Now, 1.2f, Holding);
	TestNotEqual(TEXT("Calmed after 3 s"), E(M.State), E(EGFTameState::Hostile));
	M.Tick(R, Now += 0.1f, 0.1f, Holding);
	TestEqual(TEXT("...and back to Feeding (the player fed it this visit)"), E(M.State), E(EGFTameState::Feeding));

	// Hostile too long: it flees and the bond resets; it wanders back later.
	const float BondBefore = M.Bond;
	TestTrue(TEXT("Had some bond"), BondBefore > 0.f);
	M.ForceHostile(TEXT("A"));
	TickFor(M, R, Now, FGFTameMachine::FleeAfterHostileSeconds + 0.2f, Empty);
	TestEqual(TEXT("Fled"), E(M.State), E(EGFTameState::Fled));
	TestEqual(TEXT("Fled resets the bond"), M.Bond, 0.f);
	TestTrue(TEXT("Fled clears the starter"), M.StarterKey.IsEmpty());
	TestEqual(TEXT("Fled: can't feed"), E(M.Offer(R, Now + 5.f, TEXT("A"), TEXT("JerkyBites"))), E(EGFOfferResult::NotTameable));
	TickFor(M, R, Now, FGFTameMachine::FledRecoverSeconds + 0.2f, {});
	TestEqual(TEXT("Back to Wild"), E(M.State), E(EGFTameState::Wild));

	// Tamed is final.
	FGFTameMachine T;
	float TN = 0.f;
	while (!T.IsTamed() && TN < 100.f)
	{
		TN += R.OfferCooldown;
		T.Offer(R, TN, TEXT("A"), TEXT("JerkyBites"));
	}
	TestTrue(TEXT("Tamed"), T.IsTamed());
	TickFor(T, R, TN, 2.f, Empty);
	TestEqual(TEXT("Tamed never turns hostile"), E(T.State), E(EGFTameState::Tamed));

	// Companion behaviour (follow / attack) transitions.
	TestEqual(TEXT("Target -> Attack"), E(AGFDino::NextState(EGFDinoState::Idle, true, true, 100.f, 200.f, true, 100.f, 800.f, 300.f)), E(EGFDinoState::Attack));
	TestEqual(TEXT("Far leader -> Follow"), E(AGFDino::NextState(EGFDinoState::Idle, true, false, 0.f, 200.f, true, 900.f, 800.f, 300.f)), E(EGFDinoState::Follow));
	TestEqual(TEXT("Keep following until stop distance"), E(AGFDino::NextState(EGFDinoState::Follow, true, false, 0.f, 200.f, true, 500.f, 800.f, 300.f)), E(EGFDinoState::Follow));
	TestEqual(TEXT("Close -> Idle"), E(AGFDino::NextState(EGFDinoState::Follow, true, false, 0.f, 200.f, true, 200.f, 800.f, 300.f)), E(EGFDinoState::Idle));
	TestEqual(TEXT("Wild never follows"), E(AGFDino::NextState(EGFDinoState::Idle, false, false, 0.f, 200.f, true, 900.f, 800.f, 300.f)), E(EGFDinoState::Idle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTameAggroTest, "GFGame.Tame.Aggro", TameFlags)
bool FGFTameAggroTest::RunTest(const FString&)
{
	const FDinoDietRow* RowPtr = DietRow(TEXT("Raptor"));
	if (!TestNotNull(TEXT("Raptor diet row"), RowPtr))
	{
		return false;
	}
	const FDinoDietRow& R = *RowPtr;
	TestEqual(TEXT("Warn radius = 1.25x aggro"), R.WarnRadius(), 1500.f);

	TArray<FGFTamerView> P = {
		FGFTamerView(TEXT("Far"), 1100.f, NAME_None),
		FGFTamerView(TEXT("Fed"), 400.f, TEXT("Steakroot")),
		FGFTamerView(TEXT("Near"), 900.f, TEXT("Berry")), // holding something it refuses counts as empty-handed
	};
	TestEqual(TEXT("Nearest empty-handed player, not the nearest player"), FGFDinoDiet::PickAggroTarget(R, P), 2);
	P[2].bCrouching = true;
	TestEqual(TEXT("Crouching shrinks the radius (12 m -> 8.4 m)"), FGFDinoDiet::PickAggroTarget(R, P), 0);
	P[0].Distance = 1300.f;
	TestEqual(TEXT("Nobody threatening in range"), FGFDinoDiet::PickAggroTarget(R, P), INDEX_NONE);
	TestEqual(TEXT("Warn-radius scale reaches farther (crouched 900 cm < 10.5 m)"), FGFDinoDiet::PickAggroTarget(R, P, 1.25f), 2);

	// The machine aggros at the empty-handed player even when a feeder is closer.
	FGFTameMachine M;
	float Now = 0.f;
	const TArray<FGFTamerView> Two = { FGFTamerView(TEXT("Feeder"), 300.f, TEXT("Steakroot")), FGFTamerView(TEXT("Empty"), 900.f, NAME_None) };
	TickFor(M, R, Now, R.ThreatSeconds + 0.5f, Two);
	TestEqual(TEXT("Hostile"), E(M.State), E(EGFTameState::Hostile));
	TestEqual(TEXT("At the empty-handed player"), M.TargetKey, FString(TEXT("Empty")));
	// The player walks away (out of the calm radius); it settles after 5 s alone.
	TickFor(M, R, Now, 5.3f, { FGFTamerView(TEXT("Empty"), 4000.f, NAME_None) });
	TestEqual(TEXT("Calm again"), E(M.State), E(EGFTameState::Wild));

	// Pack data: Nippers aggro together at 6 m.
	const FDinoDietRow* Nipper = DietRow(TEXT("Nipper"));
	TestTrue(TEXT("Nipper is a pack species"), Nipper && Nipper->bPack && Nipper->AggroRadius == 600.f);
	FGFTameMachine Packmate;
	Packmate.ForceHostile(TEXT("Empty"));
	TestEqual(TEXT("Pack call-in: hostile at the same player"), Packmate.TargetKey, FString(TEXT("Empty")));
	FGFTameMachine TamedOne;
	TamedOne.State = EGFTameState::Tamed;
	TamedOne.ForceHostile(TEXT("Empty"));
	TestEqual(TEXT("A tamed dino ignores the pack"), E(TamedOne.State), E(EGFTameState::Tamed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFTameJournalTest, "GFGame.Tame.Journal", TameFlags)
bool FGFTameJournalTest::RunTest(const FString&)
{
	const FDinoDietRow* RowPtr = DietRow(TEXT("Raptor"));
	if (!TestNotNull(TEXT("Raptor diet row"), RowPtr))
	{
		return false;
	}
	FGFDiscoveredDinoPreferences A;
	TestTrue(TEXT("Sighting is new"), FGFDinoDiet::RecordSighting(A, TEXT("Raptor")));
	TestFalse(TEXT("Second sighting isn't"), FGFDinoDiet::RecordSighting(A, TEXT("Raptor")));
	TestTrue(TEXT("Sniff half-reveal"), FGFDinoDiet::RecordInterest(A, TEXT("Raptor"), TEXT("JerkyBites")));
	TestTrue(TEXT("Star"), FGFDinoDiet::RecordDiscovery(A, TEXT("Raptor"), TEXT("JerkyBites"), EGFFeedResult::Favorite));
	TestFalse(TEXT("Same star again is not new"), FGFDinoDiet::RecordDiscovery(A, TEXT("Raptor"), TEXT("JerkyBites"), EGFFeedResult::Favorite));
	TestTrue(TEXT("Cross"), FGFDinoDiet::RecordDiscovery(A, TEXT("Raptor"), TEXT("Berry"), EGFFeedResult::Rejected));
	TestTrue(TEXT("Check"), FGFDinoDiet::RecordDiscovery(A, TEXT("Raptor"), TEXT("Fish"), EGFFeedResult::Accepted));
	const FGFDinoPreferenceRecord* Rec = A.Find(TEXT("Raptor"));
	if (TestNotNull(TEXT("Raptor page"), Rec))
	{
		TestTrue(TEXT("Half-reveal replaced by the star"), Rec->Interested.IsEmpty());
		TestFalse(TEXT("A star is not also a check"), Rec->KnownAccepted.Contains(FName(TEXT("JerkyBites"))));
		TestEqual(TEXT("Facts"), A.CountFacts(), 4); // sighted, star, check, cross
	}
	TestTrue(TEXT("Clue"), FGFDinoDiet::RecordClue(A, TEXT("Raptor"), TEXT("Clue.RaptorJerky")));

	// Sharing merges a friend's page.
	FGFDiscoveredDinoPreferences B;
	FGFDinoDiet::RecordDiscovery(B, TEXT("Raptor"), TEXT("Fish"), EGFFeedResult::Accepted);
	TestEqual(TEXT("Merge adds only what B lacks"), FGFDinoDiet::MergeInto(B, *A.Find(TEXT("Raptor"))), 3); // star, cross, clue (sighted+check known)
	TestEqual(TEXT("Merging twice adds nothing"), FGFDinoDiet::MergeInto(B, *A.Find(TEXT("Raptor"))), 0);

	// A full page earns the bandana.
	TestFalse(TEXT("Not complete yet"), FGFDinoDiet::UpdateCompletion(B, *RowPtr));
	for (const FGFDietEntry& Entry : RowPtr->Accepted)
	{
		FGFDinoDiet::RecordDiscovery(B, TEXT("Raptor"), Entry.Food, EGFFeedResult::Accepted);
	}
	for (FName F : RowPtr->Favourites)
	{
		FGFDinoDiet::RecordDiscovery(B, TEXT("Raptor"), F, EGFFeedResult::Favorite);
	}
	TestTrue(TEXT("Completed now"), FGFDinoDiet::UpdateCompletion(B, *RowPtr));
	TestFalse(TEXT("Completes once"), FGFDinoDiet::UpdateCompletion(B, *RowPtr));

	// Saved in the character save (round trip through the serializer).
	UGFCharacterSave* Save = NewObject<UGFCharacterSave>();
	Save->Character.Journal = B;
	FGFDinoDiet::RecordInterest(Save->Character.Journal, TEXT("Rex"), TEXT("Steakroot"));
	TArray<uint8> Bytes;
	TestTrue(TEXT("Save"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	const UGFCharacterSave* Back = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (TestNotNull(TEXT("Loaded"), Back))
	{
		const FGFDinoPreferenceRecord* R2 = Back->Character.Journal.Find(TEXT("Raptor"));
		const FGFDinoPreferenceRecord* Rex = Back->Character.Journal.Find(TEXT("Rex"));
		TestTrue(TEXT("Bandana survives"), R2 && R2->bComplete);
		TestTrue(TEXT("Clue survives"), R2 && R2->Clues.Contains(FName(TEXT("Clue.RaptorJerky"))));
		TestTrue(TEXT("Half-reveal survives"), Rex && Rex->Interested.Contains(FName(TEXT("Steakroot"))));
		TestEqual(TEXT("Same fact count"), Back->Character.Journal.CountFacts(), Save->Character.Journal.CountFacts());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDataDietValidateTest, "GFGame.Data.DietValidate", TameFlags)
bool FGFDataDietValidateTest::RunTest(const FString&)
{
	const UDataTable* Diet = FGFDataTables::Get<FDinoDietRow>(TEXT("DT_DinoDiet"));
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	const UDataTable* Plants = FGFDataTables::Get<FGFPlantRow>(TEXT("DT_Plants"));
	if (!TestNotNull(TEXT("DT_DinoDiet"), Diet) || !TestNotNull(TEXT("DT_Items"), Items) || !TestNotNull(TEXT("DT_Recipes"), Recipes) || !TestNotNull(TEXT("DT_Plants"), Plants))
	{
		return false;
	}
	// Where foods come from (PLAN.md M2 food sources).
	TSet<FName> Garden, Cookpot;
	Plants->ForeachRow<FGFPlantRow>(TEXT("Test"), [&](const FName&, const FGFPlantRow& P)
	{
		Garden.Add(P.YieldItem);
		TestNotNull(*FString::Printf(TEXT("Seed item %s exists"), *P.SeedItem.ToString()), Items->FindRow<FGFItemRow>(P.SeedItem, TEXT("Test"), false));
	});
	Recipes->ForeachRow<FGFRecipeRow>(TEXT("Test"), [&](const FName& Name, const FGFRecipeRow& R)
	{
		if (R.Station == FName(TEXT("Cookpot")))
		{
			Cookpot.Add(R.Output.ItemId);
			for (const FGFItemStack& In : R.Inputs)
			{
				TestNotNull(*FString::Printf(TEXT("Cookpot %s input %s exists"), *Name.ToString(), *In.ItemId.ToString()), Items->FindRow<FGFItemRow>(In.ItemId, TEXT("Test"), false));
			}
		}
	});
	const TSet<FName> Foraged = { TEXT("Berry"), TEXT("Fish"), TEXT("GoldenCarp"), TEXT("Glowgrub"), TEXT("WildEgg") };
	const TSet<FName> ModeRewards = { TEXT("Fizzberry"), TEXT("PetitCake"), TEXT("SmokedHaunch") };
	for (FName Crop : { FName(TEXT("Steakroot")), FName(TEXT("Emberpepper")), FName(TEXT("Thornmelon")) })
	{
		TestTrue(*FString::Printf(TEXT("New M2 crop %s"), *Crop.ToString()), Garden.Contains(Crop));
	}

	TArray<FName> Species;
	Diet->ForeachRow<FDinoDietRow>(TEXT("Test"), [&](const FName& Name, const FDinoDietRow& Row)
	{
		const FString S = Name.ToString();
		Species.Add(Name);
		TestTrue(*(S + TEXT(": Species matches the row name")), Row.Species == Name);
		TestTrue(*(S + TEXT(": accepts something")), Row.Accepted.Num() > 0);
		TestTrue(*(S + TEXT(": has a favourite")), Row.Favourites.Num() > 0);
		TestTrue(*(S + TEXT(": aggro radius")), Row.AggroRadius > 0.f);
		TestEqual(*(S + TEXT(": favourites are 3x")), Row.FavouriteMultiplier, 3.f);
		TSet<FName> Seen;
		auto CheckFood = [&](FName Food, bool bFavourite)
		{
			const FString F = S + TEXT(" ") + Food.ToString();
			bool bDup = false;
			Seen.Add(Food, &bDup);
			TestFalse(*(F + TEXT(": listed once (accepted and favourites never overlap)")), bDup);
			const FGFItemRow* Item = Items->FindRow<FGFItemRow>(Food, TEXT("Test"), false);
			if (TestNotNull(*(F + TEXT(": is an item")), Item))
			{
				TestTrue(*(F + TEXT(": is food (or a rare catch)")), Item->MaterialCategory == EGFMaterialCategory::Food || Item->MaterialCategory == EGFMaterialCategory::Rare);
			}
			const bool bHomeSource = Garden.Contains(Food) || Cookpot.Contains(Food) || Foraged.Contains(Food);
			TestTrue(*(F + TEXT(": has a source")), bHomeSource || ModeRewards.Contains(Food));
			if (bFavourite)
			{
				TestTrue(*(F + TEXT(": favourite obtainable without the arena (garden, foraging or Cookpot)")), bHomeSource);
			}
		};
		for (const FGFDietEntry& Entry : Row.Accepted)
		{
			TestTrue(*(S + TEXT(": positive bond gain")), Entry.BondGain > 0.f);
			CheckFood(Entry.Food, false);
		}
		for (FName F : Row.Favourites)
		{
			CheckFood(F, true);
		}
		// Tuning: 2-4 favourites or 4-10 average offers per tame, and a favourites-only tame fits in one visit.
		const int32 ByFav = FMath::CeilToInt(100.f / Row.FavouriteGain());
		const int32 ByMean = FMath::CeilToInt(100.f / Row.MeanAcceptedGain());
		TestTrue(*FString::Printf(TEXT("%s: %d favourites to tame"), *S, ByFav), ByFav >= 2 && ByFav <= 4);
		TestTrue(*FString::Printf(TEXT("%s: %d average offers to tame"), *S, ByMean), ByMean >= 4 && ByMean <= 10);
		TestTrue(*(S + TEXT(": fullness allows a favourites-only tame")), Row.Fullness >= ByFav);
	});
	for (const TCHAR* Expected : { TEXT("Nipper"), TEXT("Raptor"), TEXT("Sailback"), TEXT("Rex"), TEXT("Hornback") })
	{
		TestTrue(*FString::Printf(TEXT("Species %s"), Expected), Species.Contains(FName(Expected)));
	}
	return true;
}

#endif
