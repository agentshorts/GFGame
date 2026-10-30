// Spawn defaults. New players with no session record join Rime (#113). A stored None means they left.
// A full Rime leaves them unaffiliated. StarterFallback is the ferry empty-map point: anchor centre + 120 cm.
// Rival respawn (#137) prefers own land, then Rime. The three-argument RespawnPoint is the old nearest border.

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFGiantTypes.h"
#include "GFSafeZone.h"
#include "GFSpawnDefaults.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSpawnDefaultTestImpl
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));

	struct FSpawnTestWorld
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		int32 NextId = 400;

		bool Init(FAutomationTestBase& T, const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			return T.TestNotNull(TEXT("Game state"), GS);
		}

		AGFPlayerState* Add(const FString& Key)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
			if (PS)
			{
				PS->PlayerKey = Key;
				PS->SetPlayerId(++NextId);
				PS->SetPlayerName(Key);
				if (GS->PlayerArray.Find(PS) == INDEX_NONE)
				{
					GS->AddPlayerState(PS);
				}
			}
			return PS;
		}

		~FSpawnTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Shipped camps: Rime (0, 0) r 1600, Verdant (-4400, 0) r 1400. They do not overlap. */
	struct FRivalZone : public FGFSafeCircles
	{
		FBox Hazard;
		bool bProtectedOnRime = false;

		FRivalZone()
		{
			auto Disc = [](FName Id, const FVector& Center, float Radius)
			{
				FGFSafeRegion Region;
				Region.Id = Id;
				Region.Center = Center;
				Region.Members = 1;
				Region.BaseRadius = Radius;
				Region.RadiusPerExtraMember = 0.f;
				Region.MaxRadius = Radius;
				return Region;
			};
			Regions = {
				Disc(Rime, FVector::ZeroVector, 1600.f),
				Disc(Verdant, FVector(-4400.f, 0.f, 0.f), 1400.f),
			};
		}

		virtual FName OwnerAt(const FVector& Location) const override
		{
			FName Best = NAME_None;
			float BestD = TNumericLimits<float>::Max();
			for (const FGFSafeRegion& Region : Regions)
			{
				const float Dist = FVector::DistSquared2D(Region.Center, Location);
				if (Dist <= FMath::Square(Region.Radius()) && Dist < BestD)
				{
					BestD = Dist;
					Best = Region.Id;
				}
			}
			return Best;
		}

		virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& Location) const override
		{
			const FName Owner = OwnerAt(Location);
			if (Owner.IsNone())
			{
				return false;
			}
			if (Who.GroupId.IsNone())
			{
				return Owner == Rime;
			}
			if (Owner == Who.GroupId)
			{
				return true;
			}
			return bProtectedOnRime && Owner == Rime;
		}

		virtual bool IsHazardAt(const FVector& Location) const override
		{
			return Hazard.IsValid && Hazard.IsInsideOrOn(Location);
		}
	};

	static bool OnDisc(const FVector& Point, const FVector& Center, float Radius)
	{
		return FVector::Dist2D(Point, Center) <= Radius + 1.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAutoJoinRimeTest, "GFGame.Spawn.AutoJoinRime", GFSpawnDefaultTestImpl::Flags)
bool FGFAutoJoinRimeTest::RunTest(const FString&)
{
	using namespace GFSpawnDefaultTestImpl;

	FSpawnTestWorld W;
	if (!W.Init(*this, TEXT("GFSpawnAutoJoin")))
	{
		return false;
	}
	TestEqual(TEXT("Starter faction defaults to Rime"), W.GS->StarterFaction, Rime);

	AGFPlayerState* Newcomer = W.Add(TEXT("New"));
	if (!TestNotNull(TEXT("Newcomer"), Newcomer))
	{
		return false;
	}
	TestTrue(TEXT("Newcomer starts unaffiliated"), Newcomer->FactionId.IsNone());
	W.GS->NotifyModesPlayerReady(Newcomer);
	TestEqual(TEXT("A player with no session record joins Rime"), Newcomer->FactionId, Rime);
	TestEqual(TEXT("They lead Rime"), W.GS->FactionLeaderOf(Rime), FString(TEXT("New")));

	// Rejoin keeps a real session faction. Clearing the live id without touching the session is that path.
	Newcomer->FactionId = NAME_None;
	W.GS->NotifyModesPlayerReady(Newcomer);
	TestEqual(TEXT("A rejoin restores Rime"), Newcomer->FactionId, Rime);

	TestEqual(TEXT("Leaving stores None"), W.GS->ServerClaimFaction(Newcomer, NAME_None), FName(TEXT("Ok")));
	TestTrue(TEXT("They are out"), Newcomer->FactionId.IsNone());
	W.GS->NotifyModesPlayerReady(Newcomer);
	TestTrue(TEXT("A stored None is not pulled back into Rime"), Newcomer->FactionId.IsNone());

	AGFPlayerState* VerdantPlayer = W.Add(TEXT("Leaf"));
	TestEqual(TEXT("They can still pick Verdant"), W.GS->ServerClaimFaction(VerdantPlayer, Verdant), FName(TEXT("Ok")));
	VerdantPlayer->FactionId = NAME_None;
	W.GS->NotifyModesPlayerReady(VerdantPlayer);
	TestEqual(TEXT("A rejoin restores Verdant, not Rime"), VerdantPlayer->FactionId, Verdant);

	W.GS->StarterFaction = NAME_None;
	AGFPlayerState* Skipped = W.Add(TEXT("Skip"));
	W.GS->NotifyModesPlayerReady(Skipped);
	TestTrue(TEXT("Starter None does not auto-join"), Skipped->FactionId.IsNone());

	W.GS->StarterFaction = Rime;
	W.GS->MaxPerFaction = 4;
	for (int32 Seat = 0; Seat < 4; ++Seat)
	{
		AGFPlayerState* Member = W.Add(FString::Printf(TEXT("R%d"), Seat));
		TestEqual(TEXT("Rime seat"), W.GS->ServerClaimFaction(Member, Rime), FName(TEXT("Ok")));
	}
	AGFPlayerState* Fifth = W.Add(TEXT("Fifth"));
	W.GS->NotifyModesPlayerReady(Fifth);
	TestTrue(TEXT("A full Rime leaves the new player unaffiliated"), Fifth->FactionId.IsNone());
	TestEqual(TEXT("Claim reports Full"), W.GS->ServerClaimFaction(Fifth, Rime), FName(TEXT("Full")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFStarterFallbackTest, "GFGame.Spawn.StarterFallback", GFSpawnDefaultTestImpl::Flags)
bool FGFStarterFallbackTest::RunTest(const FString&)
{
	using namespace GFSpawnDefaultTestImpl;

	TestEqual(TEXT("Starter id is Rime"), FGFSpawnDefaults::StarterFactionId(), Rime);
	TestTrue(TEXT("Fallback height is 120 cm"), FMath::IsNearlyEqual(FGFSpawnDefaults::FallbackZ, 120.f));

	FGFSpawnAnchor Shifted;
	Shifted.Id = Rime;
	Shifted.Center = FVector(10.f, 20.f, 0.f);
	TArray<FGFSpawnAnchor> ShiftedAnchors;
	ShiftedAnchors.Add(Shifted);
	const FVector At = FGFSpawnDefaults::StarterFallback(Rime, ShiftedAnchors);
	TestEqual(TEXT("Shifted anchor X"), At.X, 10.0);
	TestEqual(TEXT("Shifted anchor Y"), At.Y, 20.0);
	TestEqual(TEXT("Shifted anchor Z"), At.Z, 120.0);

	FGFSpawnAnchor Origin;
	Origin.Id = Rime;
	Origin.Center = FVector::ZeroVector;
	TArray<FGFSpawnAnchor> OriginAnchors;
	OriginAnchors.Add(Origin);
	const TArray<FGFSpawnAnchor> NoAnchors;
	TestEqual(TEXT("Shipped Rime centre is the origin plus 120"), FGFSpawnDefaults::StarterFallback(Rime, OriginAnchors), FVector(0.f, 0.f, 120.f));
	TestEqual(TEXT("A missing anchor is the origin plus 120"), FGFSpawnDefaults::StarterFallback(Rime, NoAnchors), FVector(0.f, 0.f, 120.f));
	TestEqual(TEXT("An empty id is the origin plus 120"), FGFSpawnDefaults::StarterFallback(NAME_None, ShiftedAnchors), FVector(0.f, 0.f, 120.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRespawnPreferenceTest, "GFGame.Giant.RespawnPreference", GFSpawnDefaultTestImpl::Flags)
bool FGFRespawnPreferenceTest::RunTest(const FString&)
{
	using namespace GFSpawnDefaultTestImpl;

	const FGFGiantTuning Tuning;
	FRivalZone Zone;
	Zone.bProtectedOnRime = true;
	const FVector RimeCenter = FVector::ZeroVector;
	const FVector VerdantCenter(-4400.f, 0.f, 0.f);
	const FVector BesideRime(-1610.f, 0.f, 40.f);

	FGFRespawnAim VerdantAim;
	VerdantAim.Who = FGFSafeSubject(TEXT("V"), Verdant);
	VerdantAim.OwnFaction = Verdant;
	VerdantAim.StarterFaction = Rime;

	const FVector OldBeside = FGFGiantRules::RespawnPoint(Tuning, Zone, BesideRime);
	const FVector OwnLand = FGFGiantRules::RespawnPoint(Tuning, Zone, BesideRime, VerdantAim);
	TestTrue(TEXT("Old nearest border is on Rime"), OnDisc(OldBeside, RimeCenter, 1600.f));
	TestFalse(TEXT("Old nearest border is not on Verdant"), OnDisc(OldBeside, VerdantCenter, 1400.f));
	TestTrue(TEXT("Verdant member respawns on Verdant"), OnDisc(OwnLand, VerdantCenter, 1400.f));
	TestTrue(TEXT("That point is past Rime's disc"), OwnLand.X < -3000.f);
	TestFalse(TEXT("They do not land on Rime"), OnDisc(OwnLand, RimeCenter, 1600.f));
	TestFalse(TEXT("Own land wins over the old nearest border"), OwnLand.Equals(OldBeside));
	TestEqual(TEXT("Height is kept"), double(OwnLand.Z), 40.0, 0.01);
	TestTrue(TEXT("The point is safe for them"), Zone.IsSafeFor(VerdantAim.Who, OwnLand));

	// Safe on Rime as a visitor, and still sent home first. Nearest safe ground would be Rime.
	TestTrue(TEXT("They are also safe on the old Rime point"), Zone.IsSafeFor(VerdantAim.Who, OldBeside));

	Zone.bProtectedOnRime = false;
	const FVector OwnOnly = FGFGiantRules::RespawnPoint(Tuning, Zone, BesideRime, VerdantAim);
	TestTrue(TEXT("Without Rime protection they still land on Verdant"), OnDisc(OwnOnly, VerdantCenter, 1400.f));
	TestFalse(TEXT("Still not the old Rime point"), OwnOnly.Equals(OldBeside));

	// A visitor who is safe on Rime, eaten nearer Verdant. The old border is Verdant. They may land on Rime.
	const FVector NearVerdant(-2900.f, 0.f, 40.f);
	FGFRespawnAim Guest;
	Guest.Who = FGFSafeSubject(TEXT("G"), NAME_None);
	Guest.OwnFaction = NAME_None;
	Guest.StarterFaction = Rime;
	const FVector OldGuest = FGFGiantRules::RespawnPoint(Tuning, Zone, NearVerdant);
	const FVector GuestAt = FGFGiantRules::RespawnPoint(Tuning, Zone, NearVerdant, Guest);
	TestTrue(TEXT("Old border for that death is Verdant"), OnDisc(OldGuest, VerdantCenter, 1400.f));
	TestTrue(TEXT("Visitor safe on Rime respawns on Rime"), OnDisc(GuestAt, RimeCenter, 1600.f));
	TestFalse(TEXT("Visitor does not take the nearer Verdant border"), OnDisc(GuestAt, VerdantCenter, 1400.f));
	TestTrue(TEXT("Rime is safe for them"), Zone.IsSafeFor(Guest.Who, GuestAt));

	const FVector OnRime(-100.f, 0.f, 40.f);
	const FVector EatenOnRime = FGFGiantRules::RespawnPoint(Tuning, Zone, OnRime, Guest);
	TestTrue(TEXT("Eaten on Rime, a protected visitor may respawn on Rime"), OnDisc(EatenOnRime, RimeCenter, 1600.f));

	// Hazard covers Verdant's rim insets and leaves the centre. The rim must not win.
	Zone.Hazard = FBox(FVector(-4000.f, -200.f, -50.f), FVector(-3100.f, 200.f, 200.f));
	const FVector AroundHazard = FGFGiantRules::RespawnPoint(Tuning, Zone, BesideRime, VerdantAim);
	TestFalse(TEXT("Hazard rim is skipped"), Zone.IsHazardAt(AroundHazard));
	TestTrue(TEXT("Centre of Verdant is the safe inset"), FVector::Dist2D(AroundHazard, VerdantCenter) < 50.f);
	TestTrue(TEXT("Still on Verdant"), OnDisc(AroundHazard, VerdantCenter, 1400.f));

	Zone.Hazard = FBox();
	VerdantAim.Blocked.Reset();
	VerdantAim.Blocked.Add(FBox(FVector(-3180.f, -40.f, -10.f), FVector(-3120.f, 40.f, 80.f)));
	const FVector AroundBox = FGFGiantRules::RespawnPoint(Tuning, Zone, BesideRime, VerdantAim);
	TestFalse(TEXT("No-entry box is skipped"), FGFNoEntry::IsInside(VerdantAim.Blocked, AroundBox));
	TestTrue(TEXT("Next Verdant inset is still on Verdant"), OnDisc(AroundBox, VerdantCenter, 1400.f));
	TestTrue(TEXT("It sits deeper than the blocked rim"), AroundBox.X < -3200.f);

	struct FNeverSafe : public FRivalZone
	{
		virtual bool IsSafeFor(const FGFSafeSubject&, const FVector&) const override { return false; }
	};
	FNeverSafe Closed;
	const FVector FellBack = FGFGiantRules::RespawnPoint(Tuning, Closed, BesideRime, VerdantAim);
	const FVector Classic = FGFGiantRules::RespawnPoint(Tuning, Closed, BesideRime);
	TestEqual(TEXT("Nothing safe falls back to the nearest border"), FellBack, Classic);
	TestFalse(TEXT("That fallback is not safe for them"), Closed.IsSafeFor(VerdantAim.Who, FellBack));

	return true;
}

#endif
