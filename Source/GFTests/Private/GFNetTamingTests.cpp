// Taming over the network (PLAN.md M2 Net.Taming): aggro picks the empty-handed player, two clients feed one
// dino (starter + helper) and it joins the starter's roster on every machine, Field Journal entries are shared
// with friends within 30 m only, refused food isn't eaten, and a Nipper pack turns hostile together.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFVoxelWorld.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFDataTables.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace
{
	struct FTameRun
	{
		FString Host, C1, C2;
		FVector Origin = FVector::ZeroVector; // voxel world corner, ground top
		TWeakObjectPtr<AGFDino> Raptor;      // server copies
		TWeakObjectPtr<AGFDino> Nipper1;
		TWeakObjectPtr<AGFDino> Nipper2;
		int32 C1Offers = 0;
		int32 C2Offers = 0;
	};

	AGFPlayerState* ClientPS(int32 Index) { return LocalPlayerState(ClientWorlds()[Index]); }
	AGFPlayerState* ServerPS(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }

	/** Server: teleport a player's character to Origin + XY (keeps its height). */
	void PlaceOnServer(const FTameRun& Run, const FString& Key, float X, float Y)
	{
		AGFPlayerState* PS = ServerPS(Key);
		if (APawn* Pawn = PS ? PS->GetPawn() : nullptr)
		{
			const FVector L(Run.Origin.X + X, Run.Origin.Y + Y, Pawn->GetActorLocation().Z);
			Pawn->SetActorLocation(L, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	AGFDino* SpawnDino(const FTameRun& Run, FName Species, float X, float Y, float ThreatSeconds, FName PackId = NAME_None)
	{
		UWorld* World = ServerWorld();
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(Run.Origin.X + X, Run.Origin.Y + Y, Run.Origin.Z + 100.f), FRotator::ZeroRotator, P);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->SetSpecies(Species);
		Dino->Taming->PackId = PackId;
		// Stand still: distances in this test are exact.
		Dino->GetCharacterMovement()->GravityScale = 0.f;
		Dino->GetCharacterMovement()->DisableMovement();
		if (const FDinoDietRow* Row = Dino->Taming->GetDietRow())
		{
			FDinoDietRow Fast = *Row; // same diet, short timers so the test stays quick
			Fast.OfferCooldown = 0.2f;
			Fast.ThreatSeconds = ThreatSeconds;
			Dino->Taming->SetDietRowOverride(Fast);
		}
		return Dino;
	}

	AGFDino* FindDino(UWorld* World, FName Species)
	{
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			if (It->Taming && (Species.IsNone() || It->Taming->Species == Species))
			{
				return *It;
			}
		}
		return nullptr;
	}

	int32 Count(const FString& Key, FName Item)
	{
		const AGFPlayerState* PS = ServerPS(Key);
		return PS ? PS->Inventory->GetCount(Item) : -1;
	}

	const FGFDinoPreferenceRecord* Page(const AGFPlayerState* PS, FName Species)
	{
		return PS ? PS->Journal.Find(Species) : nullptr;
	}

	/** Server: remove the wild dino director and every dino it spawned, so only this test's dinos exist, and reset the journals. */
	void ClearWildDinos(UWorld* World)
	{
		UClass* DirectorClass = FindObject<UClass>(nullptr, TEXT("/Script/GFGame.GFWildDinoDirector"));
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((DirectorClass && It->IsA(DirectorClass)) || It->IsA<AGFDino>())
			{
				It->Destroy();
			}
		}
		// Anything a player sighted before the clear is forgotten, so the journal checks below only see this test.
		for (TActorIterator<AGFPlayerState> It(World); It; ++It)
		{
			It->Journal.Records.Reset();
			It->JournalChanged();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTamingTest, "GFGame.Net.Taming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetTamingTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FTameRun> Run = MakeShared<FTameRun>();

	// Layout (cm from the voxel world's corner; plot is 6400 x 6400):
	// Raptor at (4000, 4000); host parked at (600, 600), > 30 m from everyone else.
	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		Run->Host = LocalPlayerState(Server)->PlayerKey;
		Run->C1 = ClientPS(0)->PlayerKey;
		Run->C2 = ClientPS(1)->PlayerKey;
		const AGFVoxelWorld* VW = Voxel(Server);
		if (!TestNotNull(TEXT("Voxel world"), VW))
		{
			return;
		}
		Run->Origin = VW->CellToWorld(FIntVector(0, 0, VW->StarterGroundLayers)) - FVector(50.f, 50.f, 0.f);
		AGFGameState* GS = GameState(Server);
		GS->ServerMint(FGuid::NewGuid(), Run->C1, { FGFItemStack(TEXT("JerkyBites"), 5) }, TEXT("Test"));
		GS->ServerMint(FGuid::NewGuid(), Run->C2, { FGFItemStack(TEXT("Steakroot"), 5), FGFItemStack(TEXT("Berry"), 2) }, TEXT("Test"));
		ClearWildDinos(Server);
		PlaceOnServer(*Run, Run->Host, 600.f, 600.f);
		PlaceOnServer(*Run, Run->C1, 4900.f, 4000.f); // 9 m, empty-handed
		PlaceOnServer(*Run, Run->C2, 4250.f, 4000.f); // 2.5 m, will hold Steakroot
		ClientPS(0)->RequestSetHeldItem(NAME_None);
		ClientPS(1)->RequestSetHeldItem(TEXT("Steakroot"));
	});
	// The wild dino director spawns Raptors and Nippers at the faction camps. FindDino matches by species, so clear them first.
	WaitUntil(Ctx, TEXT("clients see no wild dinos"), 10.f, []()
	{
		return !FindDino(ClientWorlds()[0], NAME_None) && !FindDino(ClientWorlds()[1], NAME_None);
	});
	WaitUntil(Ctx, TEXT("client 2 holds Steakroot on the server"), 10.f, [Run]()
	{
		const AGFPlayerState* PS = ServerPS(Run->C2);
		return PS && PS->HeldItem == FName(TEXT("Steakroot"));
	});
	Do(Ctx, [this, Run]()
	{
		Run->Raptor = SpawnDino(*Run, TEXT("Raptor"), 4000.f, 4000.f, 0.5f);
		TestTrue(TEXT("Raptor spawned"), Run->Raptor.IsValid());
	});
	WaitUntil(Ctx, TEXT("both clients see the raptor"), 10.f, []()
	{
		return FindDino(ClientWorlds()[0], TEXT("Raptor")) && FindDino(ClientWorlds()[1], TEXT("Raptor"));
	});

	// 1) Aggro: the empty-handed player is the target, even though the feeder is closer.
	WaitUntil(Ctx, TEXT("raptor turns hostile"), 10.f, [Run]()
	{
		return Run->Raptor.IsValid() && Run->Raptor->Taming->GetTameState() == EGFTameState::Hostile;
	});
	WaitUntil(Ctx, TEXT("clients see the hostility"), 5.f, []()
	{
		const AGFDino* A = FindDino(ClientWorlds()[0], TEXT("Raptor"));
		const AGFDino* B = FindDino(ClientWorlds()[1], TEXT("Raptor"));
		return A && B && A->Taming->GetTameState() == EGFTameState::Hostile && B->Taming->GetTameState() == EGFTameState::Hostile;
	});
	// The dino body ticks every 0.1 s, so its attack target can trail the state by a tick.
	WaitUntil(Ctx, TEXT("dino attacks the target"), 2.f, [Run]()
	{
		return Run->Raptor.IsValid() && Run->Raptor->GetAttackTarget() != nullptr
			&& Run->Raptor->GetAttackTarget() == ServerPS(Run->C1)->GetPawn();
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Aggro target: the empty-handed player"), Run->Raptor->Taming->GetTargetKey(), Run->C1);
		TestEqual(TEXT("Clients agree on the target"), FindDino(ClientWorlds()[1], TEXT("Raptor"))->Taming->GetTargetKey(), Run->C1);
		const FGFDinoPreferenceRecord* C2Page = Page(ClientPS(1), TEXT("Raptor"));
		TestTrue(TEXT("Sighting reached client 2's journal"), C2Page && C2Page->bSighted);

		// 2) A favourite calms it. Client 1 steps in and offers Jerky Bites (its favourite).
		PlaceOnServer(*Run, Run->C1, 4200.f, 4000.f);
		ClientPS(0)->RequestSetHeldItem(TEXT("JerkyBites"));
		ClientPS(0)->RequestOfferFood(FindDino(ClientWorlds()[0], TEXT("Raptor")));
		Run->C1Offers = 1;
	});
	WaitUntil(Ctx, TEXT("client 1's first offer result"), 10.f, [Run]() { return ClientPS(0)->OfferResults.Num() >= Run->C1Offers; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Favourite"), static_cast<int32>(ClientPS(0)->OfferResults.Last()), static_cast<int32>(EGFOfferResult::Favourite));
		const UGFTamingComponent* T = Run->Raptor->Taming;
		TestEqual(TEXT("Calmed: Feeding"), static_cast<int32>(T->GetTameState()), static_cast<int32>(EGFTameState::Feeding));
		TestEqual(TEXT("Client 1 started the tame"), T->GetStarterKey(), Run->C1);
		TestEqual(TEXT("Favourite bond (3x)"), T->GetBond(), 37.f);
		TestEqual(TEXT("Jerky eaten"), Count(Run->C1, TEXT("JerkyBites")), 4);
		PlaceOnServer(*Run, Run->C2, 4000.f, 4250.f);
	});
	Wait(Ctx, 0.3f); // offer cooldown
	Do(Ctx, [Run]()
	{
		// 3) Client 2 helps (50%), then offers a food it refuses (not eaten) and goes back to Steakroot.
		AGFDino* Seen = FindDino(ClientWorlds()[1], TEXT("Raptor"));
		ClientPS(1)->RequestOfferFood(Seen);
		Run->C2Offers = 1;
	});
	WaitUntil(Ctx, TEXT("client 2's first offer result"), 10.f, [Run]() { return ClientPS(1)->OfferResults.Num() >= Run->C2Offers; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Helper's Steakroot accepted"), static_cast<int32>(ClientPS(1)->OfferResults.Last()), static_cast<int32>(EGFOfferResult::Accepted));
		TestEqual(TEXT("Helper adds 50% to the starter's bond"), Run->Raptor->Taming->GetBond(), 37.f + 7.5f);
		TestEqual(TEXT("Starter unchanged"), Run->Raptor->Taming->GetStarterKey(), Run->C1);
	});
	Wait(Ctx, 0.3f); // cooldown
	Do(Ctx, [Run]()
	{
		AGFDino* Seen = FindDino(ClientWorlds()[1], TEXT("Raptor"));
		// Reliable RPCs arrive in order: hold Berry, offer it, take the Steakroot back out.
		ClientPS(1)->RequestSetHeldItem(TEXT("Berry"));
		ClientPS(1)->RequestOfferFood(Seen);
		ClientPS(1)->RequestSetHeldItem(TEXT("Steakroot"));
		Run->C2Offers = 2;
	});
	WaitUntil(Ctx, TEXT("refusal result"), 10.f, [Run]() { return ClientPS(1)->OfferResults.Num() >= Run->C2Offers; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Berry refused"), static_cast<int32>(ClientPS(1)->OfferResults.Last()), static_cast<int32>(EGFOfferResult::Refused));
		TestEqual(TEXT("Refused food is not eaten"), Count(Run->C2, TEXT("Berry")), 2);
		TestEqual(TEXT("Refusal: bond unchanged"), Run->Raptor->Taming->GetBond(), 37.f + 7.5f);
	});
	Wait(Ctx, 0.3f);
	Do(Ctx, [Run]()
	{
		ClientPS(0)->RequestOfferFood(FindDino(ClientWorlds()[0], TEXT("Raptor")));
		Run->C1Offers = 2;
	});
	WaitUntil(Ctx, TEXT("client 1's second offer"), 10.f, [Run]() { return ClientPS(0)->OfferResults.Num() >= Run->C1Offers; });

	Wait(Ctx, 0.3f);
	// 4) Client 2 finishes the tame with three more Steakroots (7.5 each): 81.5 -> 104.
	Repeat(Ctx, 3, 0.35f, [Run](int32)
	{
		ClientPS(1)->RequestOfferFood(FindDino(ClientWorlds()[1], TEXT("Raptor")));
		++Run->C2Offers;
	});
	WaitUntil(Ctx, TEXT("all of client 2's results"), 10.f, [Run]() { return ClientPS(1)->OfferResults.Num() >= Run->C2Offers; });
	WaitUntil(Ctx, TEXT("tamed, seen by both clients"), 10.f, []()
	{
		const AGFDino* A = FindDino(ClientWorlds()[0], TEXT("Raptor"));
		const AGFDino* B = FindDino(ClientWorlds()[1], TEXT("Raptor"));
		return A && B && A->Taming->GetTameState() == EGFTameState::Tamed && B->Taming->GetTameState() == EGFTameState::Tamed;
	});
	WaitUntil(Ctx, TEXT("client 1's roster and journal replicate"), 10.f, []()
	{
		const AGFPlayerState* C1 = ClientPS(0);
		const FGFDinoPreferenceRecord* P = Page(C1, TEXT("Raptor"));
		return C1->Companions.Num() == 1 && P && P->KnownRejected.Contains(FName(TEXT("Berry")));
	});
	WaitUntil(Ctx, TEXT("follows its owner (body ticks every 0.1 s)"), 2.f, [Run]()
	{
		return Run->Raptor->GetLeader() != nullptr && Run->Raptor->GetLeader() == ServerPS(Run->C1)->GetPawn();
	});
	Do(Ctx, [this, Run]()
	{
		const UGFTamingComponent* T = Run->Raptor->Taming;
		TestEqual(TEXT("Owner is the starter (client 1), though client 2 finished it"), T->GetOwnerKey(), Run->C1);
		for (int32 i = 0; i < 2; ++i)
		{
			TestEqual(TEXT("Clients agree on the owner"), FindDino(ClientWorlds()[i], TEXT("Raptor"))->Taming->GetOwnerKey(), Run->C1);
		}
		const AGFPlayerState* C1 = ClientPS(0);
		TestTrue(TEXT("Raptor in client 1's roster"), C1->Companions[0].Species == FName(TEXT("Raptor")) && C1->Companions[0].bInParty);
		TestTrue(TEXT("Dino knows its roster entry"), FindDino(ClientWorlds()[0], TEXT("Raptor"))->Taming->CompanionId == C1->Companions[0].Id);
		TestEqual(TEXT("Client 2 owns nothing"), ServerPS(Run->C2)->Companions.Num(), 0);
		TestEqual(TEXT("Client 1 ate 2 jerky"), Count(Run->C1, TEXT("JerkyBites")), 3);
		TestEqual(TEXT("Client 2 fed 4 steakroot"), Count(Run->C2, TEXT("Steakroot")), 1);

		// Field Journal: shared within 30 m, owner-only replication, nothing for the far-away host.
		const FGFDinoPreferenceRecord* P1 = Page(C1, TEXT("Raptor"));
		const FGFDinoPreferenceRecord* P2 = Page(ClientPS(1), TEXT("Raptor"));
		if (TestNotNull(TEXT("Client 1's page"), P1) && TestNotNull(TEXT("Client 2's page"), P2))
		{
			TestTrue(TEXT("C1: own star"), P1->KnownFavorites.Contains(FName(TEXT("JerkyBites"))));
			TestTrue(TEXT("C1: shared check from C2"), P1->KnownAccepted.Contains(FName(TEXT("Steakroot"))));
			TestTrue(TEXT("C1: shared cross from C2"), P1->KnownRejected.Contains(FName(TEXT("Berry"))));
			TestTrue(TEXT("C2: shared star from C1"), P2->KnownFavorites.Contains(FName(TEXT("JerkyBites"))));
			TestTrue(TEXT("C2: own check"), P2->KnownAccepted.Contains(FName(TEXT("Steakroot"))));
		}
		TestNull(TEXT("Host (> 30 m away) learned nothing"), Page(ServerPS(Run->Host), TEXT("Raptor")));
		const AGFPlayerState* C1SeenByC2 = PlayerStateByKey(ClientWorlds()[1], Run->C1);
		TestTrue(TEXT("Journals are owner-only (client 2 can't read client 1's)"), C1SeenByC2 && C1SeenByC2->Journal.Records.Num() == 0);

		// 5) Pack: two Nippers 12 m apart; client 1 walks up to one empty-handed and the other joins in.
		PlaceOnServer(*Run, Run->C2, 6000.f, 6000.f);
		PlaceOnServer(*Run, Run->C1, 1100.f, 4500.f);
		ClientPS(0)->RequestSetHeldItem(NAME_None);
		Run->Nipper1 = SpawnDino(*Run, TEXT("Nipper"), 1500.f, 4500.f, 0.3f, TEXT("TestPack"));
		Run->Nipper2 = SpawnDino(*Run, TEXT("Nipper"), 2700.f, 4500.f, 0.3f, TEXT("TestPack"));
	});
	WaitUntil(Ctx, TEXT("the whole Nipper pack turns hostile at client 1"), 5.f, [Run]()
	{
		return Run->Nipper1.IsValid() && Run->Nipper2.IsValid()
			&& Run->Nipper1->Taming->GetTameState() == EGFTameState::Hostile && Run->Nipper2->Taming->GetTameState() == EGFTameState::Hostile
			&& Run->Nipper1->Taming->GetTargetKey() == Run->C1 && Run->Nipper2->Taming->GetTargetKey() == Run->C1;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("The tamed raptor stays tamed"), static_cast<int32>(Run->Raptor->Taming->GetTameState()), static_cast<int32>(EGFTameState::Tamed));
	});
	Stop(Ctx);
	return true;
}

#endif
