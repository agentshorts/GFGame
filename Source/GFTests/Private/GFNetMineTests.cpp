// Pebble Mine over the network (PLAN.md M8 Net.Mine): pebblings stay locked until the player's first tame, rushing one
// makes it hide, the crew digs only its own rooms, nothing reaches the player's bag until they order the haul, upgrades
// burn materials, a returning player gets their capped offline haul, and leaving saves the crew with the clock.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFMineDirector.h"
#include "GFMineTypes.h"
#include "GFModeTypes.h"
#include "GFRewards.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetMineTest
{
	struct FMineRun
	{
		FString C1;
		TMap<FName, int32> Want;
		FGuid P0, P1;
		int64 Now = 0;
		double PeekAt = 0.0;
	};

	static const FName Mine(TEXT("Mine"));

	AGFPlayerState* Client(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	AGFPlayerState* OnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	AGFMineDirector* ServerDirector() { return AGFMineDirector::Get(ServerWorld(), false); }

	void Send(FMineRun& Run, const TCHAR* Label, FName Verb, TFunction<void(FGFModeRequest&)> Fill = nullptr)
	{
		FGFModeRequest R = FGFModeRequest::Make(Mine, Verb);
		if (Fill)
		{
			Fill(R);
		}
		Run.Want.Add(FName(Label), Client(0)->RequestMode(R));
	}

	const FGFModeResult* Result(const FMineRun& Run, const TCHAR* Label)
	{
		const int32* Id = Run.Want.Find(FName(Label));
		return Id && Client(0) ? Client(0)->ModeResults.Find(*Id) : nullptr;
	}

	bool HaveAll(const FMineRun& Run, std::initializer_list<const TCHAR*> Labels)
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

	FString Code(const FMineRun& Run, const TCHAR* Label)
	{
		const FGFModeResult* R = Result(Run, Label);
		return !R ? FString(TEXT("<none>")) : (R->bOk ? FString(TEXT("Ok")) : R->Code.ToString());
	}

	int32 BagTotal(const AGFPlayerState* PS)
	{
		int32 N = 0;
		for (const FGFItemStack& S : PS->Inventory->GetInventory().Items)
		{
			N += S.Count;
		}
		return N;
	}

	bool BagOnlyFrom(const AGFPlayerState* PS, std::initializer_list<const TCHAR*> Rooms)
	{
		for (const FGFItemStack& S : PS->Inventory->GetInventory().Items)
		{
			bool bFound = false;
			for (const TCHAR* R : Rooms)
			{
				const FGFMineRoomRow* Row = FGFMineData::Get().Room(R);
				bFound |= Row && Row->Yields.ContainsByPredicate([&](const FGFRewardEntry& E) { return E.ItemId == S.ItemId; });
			}
			if (!bFound)
			{
				return false;
			}
		}
		return true;
	}

	const FGFPebbling* ClientPebbling(const FGuid& Id)
	{
		const AGFPlayerState* PS = Client(0);
		return PS ? PS->Progress.Crew.Pebblings.FindByPredicate([&](const FGFPebbling& P) { return P.Id == Id; }) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetMineTest, "GFGame.Net.Mine", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetMineTest::RunTest(const FString&)
{
	using namespace GFNetMineTest;
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FMineRun> Run = MakeShared<FMineRun>();

	Do(Ctx, [this, Run]()
	{
		Run->C1 = Client(0)->PlayerKey;
		AGFPlayerState* P = OnServer(Run->C1);
		if (!TestNotNull(TEXT("Client player on the server"), P))
		{
			return;
		}
		const TArray<FGFItemStack> All = P->Inventory->GetInventory().Items;
		if (All.Num() > 0)
		{
			GameState(ServerWorld())->ServerBurn(FGuid::NewGuid(), Run->C1, All, TEXT("Test"));
		}
		AGFMineDirector* D = AGFMineDirector::Get(ServerWorld());
		if (TestNotNull(TEXT("Mine director"), D))
		{
			D->HideSeconds = 0.5f;
			Run->Now = FDateTime(2026, 10, 29, 12, 0, 0).GetTicks();
			D->UtcNowTicksOverride = Run->Now;
		}
		P->Progress.bHasTamed = false; // a fresh character (no tame yet)
	});
	WaitUntil(Ctx, TEXT("empty bag"), 15.f, []() { return Client(0) && Client(0)->Inventory->GetInventory().Items.Num() == 0; });

	// ---- unlock and capture ----
	Do(Ctx, [Run]() { Send(*Run, TEXT("Early"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 50.f }; }); });
	WaitUntil(Ctx, TEXT("early answer"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Early") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Locked until the player's first tame"), Code(*Run, TEXT("Early")), FString(TEXT("Locked")));
		OnServer(Run->C1)->Progress.bHasTamed = true; // as ServerAddCompanion does
		Send(*Run, TEXT("Rush"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 450.f }; });
	});
	WaitUntil(Ctx, TEXT("rush answer"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Rush") }); });
	Do(Ctx, [Run]() { Send(*Run, TEXT("Hidden"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 50.f }; }); });
	WaitUntil(Ctx, TEXT("hidden answer"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Hidden") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Rushing startles it"), Code(*Run, TEXT("Rush")), FString(TEXT("Startled")));
		TestEqual(TEXT("It hides for a moment"), Code(*Run, TEXT("Hidden")), FString(TEXT("Hiding")));
		Run->PeekAt = ServerWorld()->GetTimeSeconds() + 0.6; // HideSeconds is 0.5
	});
	WaitUntil(Ctx, TEXT("it peeks out again"), 5.f, [Run]() { return ServerWorld()->GetTimeSeconds() > Run->PeekAt; });
	Do(Ctx, [Run]()
	{
		Send(*Run, TEXT("Sneak1"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 120.f }; });
	});
	WaitUntil(Ctx, TEXT("first capture"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Sneak1") }); });
	Do(Ctx, [Run]() { Send(*Run, TEXT("Sneak2"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 120.f }; }); });
	WaitUntil(Ctx, TEXT("second capture"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Sneak2") }); });
	Do(Ctx, [Run]() { Send(*Run, TEXT("Third"), TEXT("Capture"), [](FGFModeRequest& R) { R.Floats = { 120.f }; }); });
	WaitUntil(Ctx, TEXT("crew on the player's client"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("Third") }) && Client(0)->Progress.Crew.Pebblings.Num() == 2;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Sneaking up works"), Code(*Run, TEXT("Sneak1")), FString(TEXT("Ok")));
		TestEqual(TEXT("A second one"), Code(*Run, TEXT("Sneak2")), FString(TEXT("Ok")));
		TestEqual(TEXT("Crew of 2 to start"), Code(*Run, TEXT("Third")), FString(TEXT("CrewFull")));
		const FGFMineCrew& Crew = Client(0)->Progress.Crew;
		Run->P0 = Crew.Pebblings[0].Id;
		Run->P1 = Crew.Pebblings[1].Id;
		TestTrue(TEXT("Player sees the pebblings' names"), !Crew.Pebblings[0].Name.IsNone() && Crew.Pebblings[0].Name != Crew.Pebblings[1].Name);
		const FGuid P0 = Run->P0, P1 = Run->P1;
		Send(*Run, TEXT("ToShallows"), TEXT("Assign"), [P0](FGFModeRequest& R) { R.Ids = { P0 }; R.Names = { TEXT("Shallows") }; });
		Send(*Run, TEXT("ToGallery"), TEXT("Assign"), [P1](FGFModeRequest& R) { R.Ids = { P1 }; R.Names = { TEXT("CrystalGallery") }; });
		Send(*Run, TEXT("Broke"), TEXT("Buy"), [](FGFModeRequest& R) { R.Names = { TEXT("Tunnels") }; });
	});
	WaitUntil(Ctx, TEXT("assign answers"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("ToShallows"), TEXT("ToGallery"), TEXT("Broke") }); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Shallows is open"), Code(*Run, TEXT("ToShallows")), FString(TEXT("Ok")));
		TestEqual(TEXT("The Gallery needs Tunnels"), Code(*Run, TEXT("ToGallery")), FString(TEXT("RoomLocked")));
		TestEqual(TEXT("Upgrades cost materials"), Code(*Run, TEXT("Broke")), FString(TEXT("CantAfford")));
		GameState(ServerWorld())->ServerMint(FGuid::NewGuid(), Run->C1, { FGFItemStack(TEXT("Stone"), 30), FGFItemStack(TEXT("Coalnut"), 6) }, TEXT("Test"));
		Send(*Run, TEXT("Tunnels"), TEXT("Buy"), [](FGFModeRequest& R) { R.Names = { TEXT("Tunnels") }; });
	});
	WaitUntil(Ctx, TEXT("tunnels bought"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Tunnels") }); });
	Do(Ctx, [this, Run]()
	{
		const FGFModeResult* R = Result(*Run, TEXT("Tunnels"));
		TestTrue(TEXT("Tunnels level 1"), R && R->bOk && R->Ints.Num() == 1 && R->Ints[0] == 1);
		const FGuid P1 = Run->P1;
		Send(*Run, TEXT("ToGallery2"), TEXT("Assign"), [P1](FGFModeRequest& R) { R.Ids = { P1 }; R.Names = { TEXT("CrystalGallery") }; });
		Send(*Run, TEXT("Again"), TEXT("Buy"), [](FGFModeRequest& R) { R.Names = { TEXT("Tunnels") }; });
	});
	WaitUntil(Ctx, TEXT("gallery assign, bag spent"), 10.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("ToGallery2"), TEXT("Again") }) && Client(0)->Inventory->GetInventory().Items.Num() == 0;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Gallery open now"), Code(*Run, TEXT("ToGallery2")), FString(TEXT("Ok")));
		TestEqual(TEXT("Tunnels has one level"), Code(*Run, TEXT("Again")), FString(TEXT("MaxLevel")));
		ServerDirector()->SimSpeed = 300.f; // a minute's dig in 0.2 s
	});

	// ---- digging: satchels fill, nothing comes home without the player's order ----
	WaitUntil(Ctx, TEXT("both satchels full on the player's client"), 20.f, [Run]()
	{
		const FGFPebbling* A = ClientPebbling(Run->P0);
		const FGFPebbling* B = ClientPebbling(Run->P1);
		return A && B && A->State == EGFPebblingState::Full && B->State == EGFPebblingState::Full;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Nothing in the player's bag before the haul order"), BagTotal(Client(0)), 0);
		Send(*Run, TEXT("HaulAll"), TEXT("Haul"));
	});
	WaitUntil(Ctx, TEXT("the haul reaches the player's bag"), 20.f, [Run]()
	{
		return HaveAll(*Run, { TEXT("HaulAll") }) && BagTotal(Client(0)) >= 24;
	});
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Haul order"), Code(*Run, TEXT("HaulAll")), FString(TEXT("Ok")));
		TestTrue(TEXT("Only what their rooms give"), BagOnlyFrom(Client(0), { TEXT("Shallows"), TEXT("CrystalGallery") }));
		TestTrue(TEXT("Metal or Crystal came from the Gallery"),
			Client(0)->Inventory->GetInventory().GetCount(TEXT("Metal")) + Client(0)->Inventory->GetInventory().GetCount(TEXT("Crystal")) > 0);
		ServerDirector()->SimSpeed = 0.f; // stop online digging for the offline checks
		const TArray<FGFItemStack> All = Client(0)->Inventory->GetInventory().Items;
		GameState(ServerWorld())->ServerBurn(FGuid::NewGuid(), Run->C1, All, TEXT("Test"));
	});
	WaitUntil(Ctx, TEXT("bag cleared"), 10.f, []() { return BagTotal(Client(0)) == 0; });

	// ---- offline: two hours away, then a month (capped at 4 h) ----
	Do(Ctx, [this, Run]()
	{
		AGFPlayerState* P = OnServer(Run->C1);
		AGFMineDirector* D = ServerDirector();
		P->Progress.Crew.LastSeenUtcTicks = Run->Now - FTimespan::FromHours(2).GetTicks();
		const FGFMineOffline Two = D->CollectOffline(P);
		TestTrue(TEXT("Two hours away"), FMath::IsNearlyEqual(Two.Hours, 2.f, 0.01f) && Two.Value > 0 && Two.Value <= 300);
		TestEqual(TEXT("Clock restarted"), P->Progress.Crew.LastSeenUtcTicks, Run->Now);
		Send(*Run, TEXT("Status2h"), TEXT("Status"));
	});
	WaitUntil(Ctx, TEXT("offline haul on the player's client"), 10.f, [Run]() { return HaveAll(*Run, { TEXT("Status2h") }) && BagTotal(Client(0)) > 0; });
	Do(Ctx, [this, Run]()
	{
		const FGFModeResult* R = Result(*Run, TEXT("Status2h"));
		TestTrue(TEXT("Status: 120 min away"), R && R->bOk && R->Ints.Num() == 1 && R->Ints[0] == 120);
		int32 N = 0;
		for (const FGFItemStack& S : R ? R->Stacks : TArray<FGFItemStack>())
		{
			N += S.Count;
		}
		TestEqual(TEXT("Status lists what came in"), N, BagTotal(Client(0)));
		AGFPlayerState* P = OnServer(Run->C1);
		P->Progress.Crew.LastSeenUtcTicks = Run->Now - FTimespan::FromDays(30).GetTicks();
		const FGFMineOffline Month = ServerDirector()->CollectOffline(P);
		TestTrue(TEXT("A month away is capped at 4 h"), FMath::IsNearlyEqual(Month.Hours, 4.f, 0.01f) && Month.Value <= 600);
		P->Progress.Crew.LastSeenUtcTicks = Run->Now + FTimespan::FromHours(5).GetTicks();
		TestEqual(TEXT("Clock moved back: nothing"), ServerDirector()->CollectOffline(P).Value, 0);
	});

	// ---- leaving saves the crew and starts the offline clock ----
	Do(Ctx, [this, Run]()
	{
		ServerDirector()->UtcNowTicksOverride = Run->Now + FTimespan::FromMinutes(7).GetTicks();
		AGFPlayerState* P = OnServer(Run->C1);
		APlayerController* PC = P ? P->GetPlayerController() : nullptr;
		AGameModeBase* GM = ServerWorld()->GetAuthGameMode();
		if (TestNotNull(TEXT("Client controller"), PC) && GM && GM->GameSession)
		{
			GM->GameSession->KickPlayer(PC, FText::FromString(TEXT("GF test: leave the mine")));
		}
	});
	WaitUntil(Ctx, TEXT("the player is in the guest records"), 15.f, [Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		const FGFGuestRecord* Rec = GS ? GS->GuestRecords.Find(Run->C1) : nullptr;
		return Rec && Rec->bHasCharacter && !PlayerStateByKey(ServerWorld(), Run->C1);
	});
	Do(Ctx, [this, Run]()
	{
		const FGFGuestRecord* Rec = GameState(ServerWorld())->GuestRecords.Find(Run->C1);
		const FGFMineCrew* Crew = Rec ? &Rec->LastCharacter.Progress.Crew : nullptr;
		TestTrue(TEXT("Crew saved"), Crew && Crew->Pebblings.Num() == 2 && Crew->Level(GFMineUpgrades::Tunnels) == 1);
		TestEqual(TEXT("Offline clock starts when the player leaves"), Crew ? Crew->LastSeenUtcTicks : 0, Run->Now + FTimespan::FromMinutes(7).GetTicks());
	});
	Stop(Ctx);
	return true;
}

#endif
