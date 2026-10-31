// Overtime over the network: each player runs their own shift (friends see each other's), searches take time and
// one pile at a time, extracting brings everything home, a drop mid-shift keeps 75 %, lockdown seals the doors
// at 75 %, anomalies wake and containment replicates, the Lab Bench turns junk into gear through the ledger,
// and a Calming Mist settles a hostile dino.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFLabDirector.h"
#include "GFLabTypes.h"
#include "GFModeTypes.h"
#include "GFRewards.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetLabTest
{
	struct FLabRun
	{
		FString C1, C2;
		TMap<FName, int32> Want;
		TArray<FGFItemStack> Before;
		int32 AnomalyIndex = 0;
		TWeakObjectPtr<AGFDino> Near, Far;
	};

	static const FName Lab(TEXT("Lab"));

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	AGFLabDirector* ServerDirector() { return AGFLabDirector::Get(ServerWorld(), false); }
	AGFLabDirector* ClientDirector(int32 i) { return AGFLabDirector::Get(ClientWorlds()[i], false); }

	void Send(FLabRun& Run, int32 ClientIndex, const TCHAR* Label, FName Verb, TFunction<void(FGFModeRequest&)> Fill = nullptr)
	{
		FGFModeRequest R = FGFModeRequest::Make(Lab, Verb);
		if (Fill)
		{
			Fill(R);
		}
		Run.Want.Add(FName(Label), Client(ClientIndex)->RequestMode(R) + ClientIndex * 100000);
	}

	const FGFModeResult* Result(const FLabRun& Run, const TCHAR* Label)
	{
		const int32* Packed = Run.Want.Find(FName(Label));
		const AGFPlayerState* PS = Packed ? Client(*Packed / 100000) : nullptr;
		return PS ? PS->ModeResults.Find(*Packed % 100000) : nullptr;
	}

	bool HaveAll(const FLabRun& Run, std::initializer_list<const TCHAR*> Labels)
	{
		for (const TCHAR* L : Labels)
		{
			if (!Result(Run, L))
			{
				return false;
			}
		}
		return true;
	}

	FString Code(const FLabRun& Run, const TCHAR* Label)
	{
		const FGFModeResult* R = Result(Run, Label);
		return !R ? FString(TEXT("<none>")) : (R->bOk ? FString(TEXT("Ok")) : R->Code.ToString());
	}

	int32 Bag(int32 ClientIndex, FName Item) { return Client(ClientIndex)->Inventory->GetInventory().GetCount(Item); }

	AGFDino* SpawnRaptorNear(const FString& Key, float Offset)
	{
		AGFPlayerState* PS = OnServer(Key);
		APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (!Pawn)
		{
			return nullptr;
		}
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = ServerWorld()->SpawnActor<AGFDino>(AGFDino::StaticClass(), Pawn->GetActorLocation() + FVector(Offset, 0.f, 0.f), FRotator::ZeroRotator, P);
		if (Dino)
		{
			Dino->SetSpecies(TEXT("Raptor"));
			Dino->GetCharacterMovement()->GravityScale = 0.f;
			Dino->GetCharacterMovement()->DisableMovement();
		}
		return Dino;
	}

	AActor* OnClient(int32 ClientIndex, const AGFDino* ServerDino)
	{
		if (!ServerDino)
		{
			return nullptr;
		}
		for (TActorIterator<AGFDino> It(ClientWorlds()[ClientIndex]); It; ++It)
		{
			if (It->GetActorLocation().Equals(ServerDino->GetActorLocation(), 5.f))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetLabTest, "GFGame.Net.Lab", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetLabTest::RunTest(const FString&)
{
	using namespace GFNetLabTest;
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FLabRun> Run = MakeShared<FLabRun>();

	Do(Ctx, [this, Run]()
	{
		Run->C1 = Client(0)->PlayerKey;
		Run->C2 = Client(1)->PlayerKey;
		for (const FString& K : { Run->C1, Run->C2 })
		{
			AGFPlayerState* P = OnServer(K);
			if (!TestNotNull(TEXT("Client player on the server"), P))
			{
				return;
			}
			const TArray<FGFItemStack> All = P->Inventory->GetInventory().Items;
			if (All.Num() > 0)
			{
				GameState(ServerWorld())->ServerBurn(FGuid::NewGuid(), K, All, TEXT("Test"));
			}
		}
		AGFLabDirector* D = AGFLabDirector::Get(ServerWorld());
		if (TestNotNull(TEXT("Lab director"), D))
		{
			D->ForcedSeed = 4242;
			D->SimSpeed = 10.f; // an 8 s search takes 0.8 s
		}
	});
	WaitUntil(Ctx, TEXT("empty bags and the director on clients"), 15.f, []()
	{
		return Client(0) && Client(1) && Client(0)->Inventory->GetInventory().Items.Num() == 0 && Client(1)->Inventory->GetInventory().Items.Num() == 0
			&& ClientDirector(0) && ClientDirector(1);
	});

	// ---- shifts ----
	Do(Ctx, [Run]()
	{
		Send(*Run, 1, TEXT("NotIn"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 0 }; });
		Send(*Run, 0, TEXT("Enter1"), TEXT("Enter"));
		Send(*Run, 0, TEXT("Twice"), TEXT("Enter"));
		Send(*Run, 0, TEXT("Search0"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 0 }; });
		Send(*Run, 0, TEXT("Busy"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
		Send(*Run, 0, TEXT("Bad"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 99 }; });
		Send(*Run, 1, TEXT("Enter2"), TEXT("Enter"));
	});
	WaitUntil(Ctx, TEXT("enter answers"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("NotIn"), TEXT("Enter1"), TEXT("Twice"), TEXT("Search0"), TEXT("Busy"), TEXT("Bad"), TEXT("Enter2") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Not in a shift"), Code(*Run, TEXT("NotIn")), FString(TEXT("NotInRun")));
		TestEqual(TEXT("Shift starts"), Code(*Run, TEXT("Enter1")), FString(TEXT("Ok")));
		TestEqual(TEXT("One shift at a time"), Code(*Run, TEXT("Twice")), FString(TEXT("AlreadyIn")));
		TestEqual(TEXT("Searching"), Code(*Run, TEXT("Search0")), FString(TEXT("Ok")));
		TestEqual(TEXT("One pile at a time"), Code(*Run, TEXT("Busy")), FString(TEXT("Busy")));
		TestEqual(TEXT("No such pile"), Code(*Run, TEXT("Bad")), FString(TEXT("BadPile")));
		TestEqual(TEXT("Friend starts their own"), Code(*Run, TEXT("Enter2")), FString(TEXT("Ok")));
	});
	WaitUntil(Ctx, TEXT("both clients see pile 0 searched"), 10.f, [Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const AGFLabDirector* D = ClientDirector(i);
			const FGFLabRun* R = D ? D->FindRun(Run->C1) : nullptr;
			if (!R || R->Piles.Num() != FGFLabRules::PileCount || !R->Piles[0].bSearched || !D->FindRun(Run->C2))
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("Again"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 0 }; });
		Send(*Run, 0, TEXT("Search1"), TEXT("Search"), [](FGFModeRequest& R) { R.Ints = { 1 }; });
	});
	WaitUntil(Ctx, TEXT("pile 1 searched on the server"), 10.f, [Run]()
	{
		const FGFLabRun* R = ServerDirector()->FindRun(Run->C1);
		return HaveAll(*Run, { TEXT("Again"), TEXT("Search1") }) && R && R->Piles[1].bSearched;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Each pile once"), Code(*Run, TEXT("Again")), FString(TEXT("Searched")));
		FGFLabRun* R = ServerDirector()->EditRunForTests(Run->C1);
		R->Unbanked.Add(FGFItemStack(TEXT("CircuitInk"), 2)); // something certain to bring home
		Run->Before = R->Unbanked;
		Send(*Run, 0, TEXT("Extract"), TEXT("Extract"));
	});
	WaitUntil(Ctx, TEXT("extract lands in the player's bag"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Extract") }) && Bag(0, TEXT("CircuitInk")) >= 2; });
	Do(Ctx, [this, Run]()
	{
		const FGFModeResult* R = Result(*Run, TEXT("Extract"));
		TestTrue(TEXT("Extract ok"), R && R->bOk);
		for (const FGFItemStack& S : Run->Before)
		{
			TestEqual(*FString::Printf(TEXT("Extract keeps every %s"), *S.ItemId.ToString()), Bag(0, S.ItemId), S.Count);
			TestEqual(*FString::Printf(TEXT("Result lists %s"), *S.ItemId.ToString()), R ? FGFRewards::CountOf(R->Stacks, S.ItemId) : -1, S.Count);
		}
		TestNull(TEXT("Shift over"), ServerDirector()->FindRun(Run->C1));
		const TArray<FGFItemStack> All = Client(0)->Inventory->GetInventory().Items;
		GameState(ServerWorld())->ServerBurn(FGuid::NewGuid(), Run->C1, All, TEXT("Test"));
	});
	WaitUntil(Ctx, TEXT("bag cleared, run gone on clients"), 10.f, [Run]()
	{
		return Client(0)->Inventory->GetInventory().Items.Num() == 0 && !ClientDirector(1)->FindRun(Run->C1);
	});

	// ---- lockdown ----
	Do(Ctx, [Run]() { Send(*Run, 0, TEXT("Enter3"), TEXT("Enter")); });
	WaitUntil(Ctx, TEXT("second shift"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Enter3") }); });
	Do(Ctx, [this, Run]()
	{
		ServerDirector()->SimSpeed = 1.f;
		FGFLabRun* R = ServerDirector()->EditRunForTests(Run->C1);
		if (TestNotNull(TEXT("Second shift"), R))
		{
			R->Clock = FGFLabRules::LockdownSeconds - 0.3f;
			R->Unbanked = { FGFItemStack(TEXT("ScrapWire"), 4), FGFItemStack(TEXT("PolymerGel"), 3) };
		}
	});
	WaitUntil(Ctx, TEXT("lockdown"), 10.f, [Run]() { return !ServerDirector()->FindRun(Run->C1) && Bag(0, TEXT("ScrapWire")) > 0; });
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Ended by lockdown"), ServerDirector()->LastEnd.FindRef(Run->C1) == EGFLabEnd::Lockdown);
		TestEqual(TEXT("Lockdown keeps 75 %: 4 -> 3"), Bag(0, TEXT("ScrapWire")), 3);
		TestEqual(TEXT("Lockdown keeps 75 %: 3 -> 2"), Bag(0, TEXT("PolymerGel")), 2);
	});

	// ---- anomalies replicate ----
	Do(Ctx, [Run]()
	{
		AGFLabDirector* D = ServerDirector();
		if (D)
		{
			D->SimSpeed = 1.f;
			D->ForcedSeed = 9001;
		}
		Send(*Run, 0, TEXT("EnterA"), TEXT("Enter"));
	});
	WaitUntil(Ctx, TEXT("anomaly shift started"), 10.f, [Run]()
	{
		const FGFLabRun* R = ServerDirector() ? ServerDirector()->FindRun(Run->C1) : nullptr;
		return HaveAll(*Run, { TEXT("EnterA") }) && R && R->Anomalies.Num() > 0;
	});
	Do(Ctx, [this, Run]()
	{
		FGFLabRun* R = ServerDirector()->EditRunForTests(Run->C1);
		if (!TestNotNull(TEXT("Anomaly shift"), R) || R->Anomalies.Num() == 0)
		{
			return;
		}
		Run->AnomalyIndex = 0;
		R->Clock = R->Anomalies[0].StartTime - 0.2f;
	});
	WaitUntil(Ctx, TEXT("clients see the anomaly wake"), 10.f, [Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const AGFLabDirector* D = ClientDirector(i);
			const FGFLabRun* R = D ? D->FindRun(Run->C1) : nullptr;
			if (!R || !R->Anomalies.IsValidIndex(Run->AnomalyIndex) || !R->Anomalies[Run->AnomalyIndex].bActive)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [Run]()
	{
		Send(*Run, 0, TEXT("ContainA"), TEXT("Contain"), [Run](FGFModeRequest& R) { R.Ints = { Run->AnomalyIndex }; });
	});
	WaitUntil(Ctx, TEXT("clients see it contained"), 10.f, [Run]()
	{
		for (int32 i = 0; i < 2; ++i)
		{
			const AGFLabDirector* D = ClientDirector(i);
			const FGFLabRun* R = D ? D->FindRun(Run->C1) : nullptr;
			if (!R || !R->Anomalies.IsValidIndex(Run->AnomalyIndex) || !R->Anomalies[Run->AnomalyIndex].bContained)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [this, Run]()
	{
		const FGFLabRun* ServerRun = ServerDirector()->FindRun(Run->C1);
		const AGFLabDirector* ClientD = ClientDirector(0);
		const FGFLabRun* ClientRun = ClientD ? ClientD->FindRun(Run->C1) : nullptr;
		const int32 Index = Run->AnomalyIndex;
		TestTrue(TEXT("Server contained it"), ServerRun && ServerRun->Anomalies.IsValidIndex(Index) && ServerRun->Anomalies[Index].bContained && !ServerRun->Anomalies[Index].bActive);
		TestTrue(TEXT("Client sees the same anomaly"), ClientRun && ServerRun && ClientRun->Anomalies.IsValidIndex(Index)
			&& ClientRun->Anomalies[Index].Kind == ServerRun->Anomalies[Index].Kind
			&& ClientRun->Anomalies[Index].bContained && !ClientRun->Anomalies[Index].bActive);
		Send(*Run, 0, TEXT("ExtractA"), TEXT("Extract"));
	});
	WaitUntil(Ctx, TEXT("anomaly shift extracted"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("ExtractA") }) && ServerDirector() && !ServerDirector()->FindRun(Run->C1);
	});

	// ---- Lab Bench ----
	Do(Ctx, [Run]()
	{
		GameState(ServerWorld())->ServerMint(FGuid::NewGuid(), Run->C1,
			{ FGFItemStack(TEXT("BioCulture"), 2), FGFItemStack(TEXT("GlowDust"), 2), FGFItemStack(TEXT("Fiber"), 2), FGFItemStack(TEXT("Fruit"), 1) }, TEXT("Test"));
		Send(*Run, 0, TEXT("Mist"), TEXT("Craft"), [](FGFModeRequest& R) { R.Names = { TEXT("CalmingMist") }; });
		Send(*Run, 0, TEXT("Treat"), TEXT("Craft"), [](FGFModeRequest& R) { R.Names = { TEXT("LabTreat") }; });
		Send(*Run, 0, TEXT("Chair"), TEXT("Craft"), [](FGFModeRequest& R) { R.Names = { TEXT("Chair") }; });
		Send(*Run, 0, TEXT("Nope"), TEXT("Craft"), [](FGFModeRequest& R) { R.Names = { TEXT("Nope") }; });
	});
	WaitUntil(Ctx, TEXT("bench answers and the mist in the player's bag"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("Mist"), TEXT("Treat"), TEXT("Chair"), TEXT("Nope") }) && Bag(0, TEXT("CalmingMist")) == 3;
	});
	Do(Ctx, [this, Run]()
	{
		const FGFModeResult* M = Result(*Run, TEXT("Mist"));
		TestTrue(TEXT("Brewed 3 Calming Mists"), M && M->bOk && FGFRewards::CountOf(M->Stacks, TEXT("CalmingMist")) == 3);
		TestEqual(TEXT("Lab Treat needs 2 fruit"), Code(*Run, TEXT("Treat")), FString(TEXT("MissingInputs")));
		TestEqual(TEXT("Chairs are made at the Bench, not the Lab Bench"), Code(*Run, TEXT("Chair")), FString(TEXT("WrongStation")));
		TestEqual(TEXT("Unknown"), Code(*Run, TEXT("Nope")), FString(TEXT("UnknownRecipe")));
		TestEqual(TEXT("Inputs burned once"), Bag(0, TEXT("BioCulture")), 1);
		TestEqual(TEXT("Failed craft burned nothing"), Bag(0, TEXT("Fruit")), 1);

		// ---- Calming Mist ----
		AGFDino* Near = SpawnRaptorNear(Run->C1, 400.f);
		AGFDino* Far = SpawnRaptorNear(Run->C1, 5000.f);
		Run->Near = Near;
		Run->Far = Far;
		if (TestNotNull(TEXT("Raptors"), Near) && Far)
		{
			Near->Taming->ServerForceHostile(Run->C1);
			TestTrue(TEXT("Hostile at the player"), Near->Taming->GetTameState() == EGFTameState::Hostile);
		}
	});
	WaitUntil(Ctx, TEXT("the player's client sees the raptors, one hostile"), 10.f, [Run]()
	{
		const AActor* A = OnClient(0, Run->Near.Get());
		const AActor* B = OnClient(0, Run->Far.Get());
		const UGFTamingComponent* T = A ? A->FindComponentByClass<UGFTamingComponent>() : nullptr;
		return B && T && T->GetTameState() == EGFTameState::Hostile;
	});
	Do(Ctx, [Run]()
	{
		Client(0)->RequestUseCalmingMist(OnClient(0, Run->Far.Get()));
		Client(0)->RequestUseCalmingMist(OnClient(0, Run->Near.Get()));
	});
	WaitUntil(Ctx, TEXT("mist results"), 10.f, []() { return Client(0)->MistResults.Num() == 2 && Bag(0, TEXT("CalmingMist")) == 2; });
	Do(Ctx, [this, Run]()
	{
		const TArray<bool>& R = Client(0)->MistResults;
		TestFalse(TEXT("Too far: no mist used"), R[0]);
		TestTrue(TEXT("Sprayed"), R[1]);
		UGFTamingComponent* T = Run->Near.IsValid() ? Run->Near->Taming.Get() : nullptr;
		if (TestNotNull(TEXT("Near raptor"), T))
		{
			TestTrue(TEXT("Settled"), T->GetTameState() != EGFTameState::Hostile && T->GetTameState() != EGFTameState::Threat && T->GetTargetKey().IsEmpty());
			T->ServerForceHostile(Run->C1); // a pack call can't pull it back while the mist lasts
			TestTrue(TEXT("Pack call ignored while calm"), T->GetTameState() != EGFTameState::Hostile);
		}
	});
	WaitUntil(Ctx, TEXT("the player's client sees it calm"), 10.f, [Run]()
	{
		const AActor* A = OnClient(0, Run->Near.Get());
		const UGFTamingComponent* T = A ? A->FindComponentByClass<UGFTamingComponent>() : nullptr;
		return T && T->CalmedUntil > 0.f && T->GetTameState() != EGFTameState::Hostile;
	});
	Do(Ctx, [Run]()
	{
		if (Run->Near.IsValid()) { Run->Near->Destroy(); }
		if (Run->Far.IsValid()) { Run->Far->Destroy(); }
	});

	// ---- a drop mid-shift keeps 75 % ----
	Do(Ctx, [this, Run]()
	{
		FGFLabRun* R = ServerDirector()->EditRunForTests(Run->C2);
		if (!TestNotNull(TEXT("Friend's shift still running"), R))
		{
			return;
		}
		R->Clock = 10.f; // the friend's shift has been idling at speed: keep it short of lockdown
		R->Unbanked = { FGFItemStack(TEXT("AnomalyShard"), 4), FGFItemStack(TEXT("BentPipe"), 1) };
		AGFPlayerState* P2 = OnServer(Run->C2);
		APlayerController* PC = P2 ? P2->GetPlayerController() : nullptr;
		AGameModeBase* GM = ServerWorld()->GetAuthGameMode();
		if (TestNotNull(TEXT("Client 2 controller"), PC) && GM && GM->GameSession)
		{
			GM->GameSession->KickPlayer(PC, FText::FromString(TEXT("GF test: drop mid-shift")));
		}
	});
	WaitUntil(Ctx, TEXT("dropped friend in the guest records"), 15.f, [Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		const FGFGuestRecord* Rec = GS ? GS->GuestRecords.Find(Run->C2) : nullptr;
		return Rec && Rec->bHasCharacter && !PlayerStateByKey(ServerWorld(), Run->C2);
	});
	Do(Ctx, [this, Run]()
	{
		const FGFGuestRecord* Rec = GameState(ServerWorld())->GuestRecords.Find(Run->C2);
		TestEqual(TEXT("Dropped: 4 -> 3 shards"), Rec ? Rec->LastCharacter.Inventory.GetCount(TEXT("AnomalyShard")) : -1, 3);
		TestEqual(TEXT("Dropped: 1 pipe -> 0"), Rec ? Rec->LastCharacter.Inventory.GetCount(TEXT("BentPipe")) : -1, 0);
		TestTrue(TEXT("Evacuated"), ServerDirector()->LastEnd.FindRef(Run->C2) == EGFLabEnd::Evacuate && !ServerDirector()->FindRun(Run->C2));
	});
	Stop(Ctx);
	return true;
}

#endif
