// Acceleration Hop v2 (issue 342) tests: the lit target tile. The pick, the 1.5x gain, the pulse, and the road actor
// lighting, paying and putting out a tile.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.AccelHop.Lit;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "GFAccelHop.h"
#include "GFAccelHopRoad.h"
#include "GFAccelHopTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAccelHopLitTestsPrivate
{
	static constexpr EAutomationTestFlags AccelHopLitFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FAccelHopLitTestWorld
	{
		UWorld* World = nullptr;

		explicit FAccelHopLitTestWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FAccelHopLitTestWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static FGFAccelHopHit AccelHopLitHitAt(double NowBeat)
	{
		const FGFAccelHopTuning Tuning;
		return FGFAccelHopRules::JudgeLanding(NowBeat, 120.f, FGFAccelHopRules::WindowsFor(EGFBeatHopMode::Songs, Tuning), Tuning.BeatsPerBar);
	}

	/** The real road's bar of 8 (Downbeat, five Standard, Boost, Boost) every 200 cm, with Landing tiles at the ends. */
	static TArray<FGFHopTileSlot> AccelHopLitRoadSlots(int32 Bars)
	{
		TArray<FGFHopTileSlot> Slots;
		auto Add = [&Slots](EGFHopTileKind Kind)
		{
			FGFHopTileSlot Slot;
			Slot.Along = 200.f * static_cast<float>(Slots.Num());
			Slot.Kind = Kind;
			Slots.Add(Slot);
		};
		Add(EGFHopTileKind::Landing);
		Add(EGFHopTileKind::Landing);
		for (int32 Bar = 0; Bar < Bars; ++Bar)
		{
			Add(EGFHopTileKind::Downbeat);
			for (int32 Index = 0; Index < 5; ++Index)
			{
				Add(EGFHopTileKind::Standard);
			}
			Add(EGFHopTileKind::Boost);
			Add(EGFHopTileKind::Boost);
		}
		Add(EGFHopTileKind::Landing);
		Add(EGFHopTileKind::Landing);
		return Slots;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLitGainTest, "GFGame.AccelHop.Lit.Gain", GFAccelHopLitTestsPrivate::AccelHopLitFlags)
bool FGFAccelHopLitGainTest::RunTest(const FString&)
{
	using namespace GFAccelHopLitTestsPrivate;
	const FGFAccelHopTuning Tuning;
	TestTrue(TEXT("Lit tiles pay about half again by default"), FMath::IsNearlyEqual(Tuning.LitBoostScale, 1.5f));
	const FGFAccelHopHit Perfect = AccelHopLitHitAt(9.0);
	const FGFAccelHopHit Miss = AccelHopLitHitAt(9.5);
	for (EGFHopTileKind Kind : { EGFHopTileKind::Standard, EGFHopTileKind::Downbeat, EGFHopTileKind::Boost })
	{
		const float Normal = FGFAccelHopRules::GainFor(Perfect, Kind, Tuning);
		const float Lit = FGFAccelHopRules::GainFor(Perfect, Kind, Tuning, true);
		TestTrue(*FString::Printf(TEXT("Lit gain is 1.5x on kind %d (%.3f vs %.3f)"), static_cast<int32>(Kind), Lit, Normal), FMath::IsNearlyEqual(Lit, Normal * 1.5f, 1.0e-4f));
	}
	TestEqual(TEXT("A Miss on a lit tile is still nothing"), FGFAccelHopRules::GainFor(Miss, EGFHopTileKind::Boost, Tuning, true), 0.f);
	TestTrue(TEXT("The unlit default keeps the v1 gain"), FMath::IsNearlyEqual(FGFAccelHopRules::GainFor(Perfect, EGFHopTileKind::Boost, Tuning), Tuning.PerfectBoost + Tuning.BoostTileBonus, 1.0e-4f));

	FGFAccelHopBoost Normal;
	FGFAccelHopBoost Lit;
	TestTrue(TEXT("Normal hop applies"), FGFAccelHopRules::ApplyHit(Normal, Perfect, EGFHopTileKind::Boost, 10.0, 0.5f, Tuning));
	TestTrue(TEXT("Lit hop applies"), FGFAccelHopRules::ApplyHit(Lit, Perfect, EGFHopTileKind::Boost, 10.0, 0.5f, Tuning, true));
	TestTrue(*FString::Printf(TEXT("Lit boost is 1.5x the normal boost (%.3f vs %.3f)"), Lit.Multiplier - 1.f, Normal.Multiplier - 1.f), FMath::IsNearlyEqual(Lit.Multiplier - 1.f, (Normal.Multiplier - 1.f) * 1.5f, 1.0e-4f));
	TestTrue(TEXT("Still capped"), Lit.Multiplier <= Tuning.MaxMultiplier + 1.0e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLitPickTest, "GFGame.AccelHop.Lit.Pick", GFAccelHopLitTestsPrivate::AccelHopLitFlags)
bool FGFAccelHopLitPickTest::RunTest(const FString&)
{
	using namespace GFAccelHopLitTestsPrivate;
	const FGFAccelHopLitTuning Tuning;
	const TArray<FGFHopTileSlot> Slots = AccelHopLitRoadSlots(6);
	const float MinAhead = Tuning.MinAheadHops * Tuning.HopLengthCm;
	const float MaxAhead = Tuning.MaxAheadHops * Tuning.HopLengthCm;

	int32 Boost = 0;
	int32 Downbeat = 0;
	int32 Standard = 0;
	int32 Landing = 0;
	bool bAllInWindow = true;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		const float Anchor = 400.f + 37.f * static_cast<float>(Seed % 20);
		const int32 Pick = FGFAccelHopLitRules::PickLitIndex(Slots, Anchor, 1.f, INDEX_NONE, Seed, Tuning);
		if (!Slots.IsValidIndex(Pick))
		{
			bAllInWindow = false;
			continue;
		}
		const float Ahead = Slots[Pick].Along - Anchor;
		bAllInWindow &= Ahead >= MinAhead && Ahead <= MaxAhead;
		switch (Slots[Pick].Kind)
		{
		case EGFHopTileKind::Boost: ++Boost; break;
		case EGFHopTileKind::Downbeat: ++Downbeat; break;
		case EGFHopTileKind::Landing: ++Landing; break;
		default: ++Standard; break;
		}
	}
	TestTrue(TEXT("Every pick is a few hops ahead"), bAllInWindow);
	TestEqual(TEXT("Landing tiles never light"), Landing, 0);
	TestTrue(*FString::Printf(TEXT("Boost is favoured (%d Boost, %d Downbeat, %d Standard)"), Boost, Downbeat, Standard), Boost > Downbeat && Boost > Standard);
	TestTrue(TEXT("Downbeat gets picked too"), Downbeat > 0);
	TestTrue(TEXT("Downbeat beats Standard"), Downbeat >= Standard);

	const int32 Same = FGFAccelHopLitRules::PickLitIndex(Slots, 1000.f, 1.f, INDEX_NONE, 7, Tuning);
	TestEqual(TEXT("Same inputs, same pick"), FGFAccelHopLitRules::PickLitIndex(Slots, 1000.f, 1.f, INDEX_NONE, 7, Tuning), Same);
	TestNotEqual(TEXT("The avoided tile is skipped"), FGFAccelHopLitRules::PickLitIndex(Slots, 1000.f, 1.f, Same, 7, Tuning), Same);

	const float End = Slots.Last().Along;
	const int32 Back = FGFAccelHopLitRules::PickLitIndex(Slots, End - 200.f, -1.f, INDEX_NONE, 3, Tuning);
	TestTrue(TEXT("Heading back toward the start picks behind on the road"), Slots.IsValidIndex(Back) && Slots[Back].Along < End - 200.f - MinAhead + 1.f);
	TestEqual(TEXT("Past the last tile there is nothing to light"), FGFAccelHopLitRules::PickLitIndex(Slots, End + 10.f, 1.f, INDEX_NONE, 3, Tuning), INDEX_NONE);
	TestEqual(TEXT("An empty road lights nothing"), FGFAccelHopLitRules::PickLitIndex(TArray<FGFHopTileSlot>(), 0.f, 1.f, INDEX_NONE, 0, Tuning), INDEX_NONE);

	TestTrue(TEXT("Keep a tile still ahead"), FGFAccelHopLitRules::KeepLit(Slots, 10, Slots[10].Along - 600.f, 1.f, Tuning));
	TestTrue(TEXT("Keep it while standing on it"), FGFAccelHopLitRules::KeepLit(Slots, 10, Slots[10].Along + 50.f, 1.f, Tuning));
	TestFalse(TEXT("Drop it once well past"), FGFAccelHopLitRules::KeepLit(Slots, 10, Slots[10].Along + Tuning.KeepBehindCm + 50.f, 1.f, Tuning));
	TestFalse(TEXT("Drop it after turning round"), FGFAccelHopLitRules::KeepLit(Slots, 10, Slots[10].Along - 600.f, -1.f, Tuning));
	TestFalse(TEXT("No tile, nothing to keep"), FGFAccelHopLitRules::KeepLit(Slots, INDEX_NONE, 0.f, 1.f, Tuning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLitPulseTest, "GFGame.AccelHop.Lit.Pulse", GFAccelHopLitTestsPrivate::AccelHopLitFlags)
bool FGFAccelHopLitPulseTest::RunTest(const FString&)
{
	TestTrue(TEXT("Full on the beat"), FMath::IsNearlyEqual(FGFAccelHopLitRules::LitPulse(4.0, 0.3f), 1.f));
	TestTrue(TEXT("Falling inside the pulse"), FGFAccelHopLitRules::LitPulse(4.1, 0.3f) < 1.f && FGFAccelHopLitRules::LitPulse(4.1, 0.3f) > 0.f);
	TestEqual(TEXT("Gone after the pulse width"), FGFAccelHopLitRules::LitPulse(4.35, 0.3f), 0.f);
	TestEqual(TEXT("No clock, no pulse"), FGFAccelHopLitRules::LitPulse(-1.0, 0.3f), 0.f);
	TestTrue(TEXT("Sharper than the road-wide pulse"), FGFAccelHopLitRules::LitPulse(4.2, 0.3f) < FGFAccelHopRules::BeatPulse(4.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAccelHopLitRoadTest, "GFGame.AccelHop.Lit.Road", GFAccelHopLitTestsPrivate::AccelHopLitFlags)
bool FGFAccelHopLitRoadTest::RunTest(const FString&)
{
	using namespace GFAccelHopLitTestsPrivate;
	FAccelHopLitTestWorld W(TEXT("GFAccelHopLitRoadWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Spawned out of order on purpose: the road sorts along its own X.
	const EGFHopTileKind Kinds[] = { EGFHopTileKind::Standard, EGFHopTileKind::Boost, EGFHopTileKind::Standard, EGFHopTileKind::Downbeat, EGFHopTileKind::Standard, EGFHopTileKind::Landing };
	const float Xs[] = { 1400.f, 1200.f, 200.f, 1000.f, 600.f, 1800.f };
	TArray<AGFAccelHopTile*> Spawned;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		AGFAccelHopTile* Tile = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(Xs[Index], 0.0, 0.0)), Params);
		if (!TestNotNull(TEXT("Tile"), Tile))
		{
			return false;
		}
		Tile->SetKind(Kinds[Index]);
		Spawned.Add(Tile);
	}
	AGFAccelHopTile* Outside = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(0.0, 5000.0, 0.0)), Params);
	AGFAccelHopRoad* Road = W.World->SpawnActor<AGFAccelHopRoad>(AGFAccelHopRoad::StaticClass(), FTransform(FVector(1000.0, 0.0, 0.0)), Params);
	if (!TestNotNull(TEXT("Road"), Road) || !TestNotNull(TEXT("Outside tile"), Outside))
	{
		return false;
	}
	Road->SetAutoPick(false);
	TestTrue(TEXT("The road replicates its pick"), Road->GetIsReplicated());
	TestTrue(TEXT("The road is always relevant (it is longer than the cull distance)"), Road->bAlwaysRelevant);
	Road->RefreshTiles();
	TestEqual(TEXT("Six tiles inside the box, the far one left out"), Road->GetTiles().Num(), 6);
	TestNull(TEXT("The far tile has no road"), Outside->GetRoad());
	bool bSorted = true;
	for (int32 Index = 1; Index < Road->GetTiles().Num(); ++Index)
	{
		bSorted &= Road->GetTiles()[Index - 1]->GetActorLocation().X < Road->GetTiles()[Index]->GetActorLocation().X;
		bSorted &= Road->GetTiles()[Index]->GetRoadIndex() == Index && Road->GetTiles()[Index]->GetRoad() == Road;
	}
	TestTrue(TEXT("Sorted along the road, each tile knows its index"), bSorted);
	TestNull(TEXT("Nothing lit yet"), Road->GetLitTile());

	// Anchor at world X 200 (box local -800), heading +X: the window is 900..1800 cm ahead, so world 1100..2000.
	const double Now = 5.0;
	Road->ServerPickFrom(Road->AlongOf(FVector(200.0, 0.0, 0.0)), 1.f, Now);
	AGFAccelHopTile* Lit = Road->GetLitTile();
	if (!TestNotNull(TEXT("A tile is lit"), Lit))
	{
		return false;
	}
	TestTrue(TEXT("It is a few hops ahead"), Lit->GetActorLocation().X >= 1100.0 && Lit->GetActorLocation().X <= 2000.0);
	TestTrue(TEXT("It is the Boost (Landing never lights; Standard cannot outscore Boost)"), Lit->GetKind() == EGFHopTileKind::Boost);
	TestTrue(TEXT("It is drawn lit"), Lit->IsLitVisual());
	TestEqual(TEXT("Only one tile is lit"), Road->GetTiles().FilterByPredicate([](const TObjectPtr<AGFAccelHopTile>& T) { return T && T->IsLitVisual(); }).Num(), 1);
	const int32 Serial = Road->GetLitState().Serial;

	Road->ServerPickFrom(Road->AlongOf(FVector(400.0, 0.0, 0.0)), 1.f, Now + 0.5);
	TestTrue(TEXT("The next beat keeps a tile that is still ahead"), Road->GetLitTile() == Lit);
	TestEqual(TEXT("No re-send for a kept tile"), Road->GetLitState().Serial, Serial);

	AGFAccelHopTile* Plain = Spawned[0];
	TestTrue(TEXT("The lit tile is lit on this machine"), Road->IsTileLit(Lit));
	TestFalse(TEXT("A plain tile is not"), Road->IsTileLit(Plain));
	TestTrue(TEXT("Server: landing on it is a lit landing"), Road->ServerIsLitLanding(Lit, Now + 0.6));
	TestFalse(TEXT("Server: a plain tile is not"), Road->ServerIsLitLanding(Plain, Now + 0.6));

	Road->ServerConsumeLit(Lit, Now + 0.6);
	TestNull(TEXT("Paid: the tile goes dark"), Road->GetLitTile());
	TestFalse(TEXT("And is drawn dark"), Lit->IsLitVisual());
	TestFalse(TEXT("Paid once: no grace for a second landing"), Road->ServerIsLitLanding(Lit, Now + 0.7));
	TestTrue(TEXT("The change bumps the serial"), Road->GetLitState().Serial > Serial);

	// Re-pick, then move the anchor well past it: the next beat drops it, but a landing in the grace window still pays.
	Road->ServerPickFrom(Road->AlongOf(FVector(200.0, 0.0, 0.0)), 1.f, Now + 1.0);
	AGFAccelHopTile* Again = Road->GetLitTile();
	if (!TestNotNull(TEXT("Lit again"), Again))
	{
		return false;
	}
	Road->ServerPickFrom(Road->AlongOf(Again->GetActorLocation() + FVector(400.0, 0.0, 0.0)), 1.f, Now + 1.5);
	TestTrue(TEXT("Passed: a new tile (or none) is lit"), Road->GetLitTile() != Again);
	TestFalse(TEXT("The passed tile is drawn dark"), Again->IsLitVisual());
	TestTrue(TEXT("Grace: a landing that raced the re-pick still pays"), Road->ServerIsLitLanding(Again, Now + 1.5 + 0.5 * Road->GetLitTuning().ServerGraceSeconds));
	TestFalse(TEXT("After the grace it does not"), Road->ServerIsLitLanding(Again, Now + 1.6 + Road->GetLitTuning().ServerGraceSeconds));

	Road->ServerSetLit(INDEX_NONE, Now + 3.0);
	bool bAnyLit = false;
	for (AGFAccelHopTile* Tile : Spawned)
	{
		bAnyLit |= Tile->IsLitVisual();
	}
	TestFalse(TEXT("Clearing puts every tile out"), bAnyLit);
	return true;
}

#endif
