// Economy over the network (PLAN.md M1 "Net.Inventory" acceptance): 1,000 transfers under packet loss,
// every client request sent twice, and a client dropped mid-run. Nothing may be duplicated or lost.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace
{
	struct FTransferRun
	{
		FRandomStream Rng = FRandomStream(4242);
		TArray<FString> Keys; // host, client 1, client 2
		TWeakObjectPtr<AGFPlayerState> Client1;
		TWeakObjectPtr<AGFPlayerState> Client2;
		TArray<FGuid> Client1Sent;
		TArray<FGuid> ServerTx;
		TMap<FName, int64> Start;
		int32 ServerApplied = 0;
		int32 ServerAttempts = 0;
		int32 ClientAttempts = 0;
		bool bKicked = false;
	};

	const FName TransferItems[] = { TEXT("Dirt"), TEXT("WoodPlank"), TEXT("Stone") };

	TArray<FGFItemStack> SmallStacks(FRandomStream& Rng)
	{
		TArray<FGFItemStack> S;
		const int32 N = Rng.RandRange(1, 2);
		for (int32 i = 0; i < N; ++i)
		{
			S.Emplace(TransferItems[Rng.RandRange(0, 2)], Rng.RandRange(1, 6));
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTransfersTest, "GFGame.Net.Transfers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetTransfersTest::RunTest(const FString&)
{
	// The kicked client logs its own "connection lost" error; that's the point of the test, not a failure.
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FTransferRun> Run = MakeShared<FTransferRun>();
	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		const TArray<UWorld*> Clients = ClientWorlds();
		Run->Keys.Add(LocalPlayerState(Server)->PlayerKey);
		Run->Client1 = LocalPlayerState(Clients[0]);
		Run->Client2 = LocalPlayerState(Clients[1]);
		Run->Keys.Add(Run->Client1->PlayerKey);
		Run->Keys.Add(Run->Client2->PlayerKey);
		const AGFGameState* GS = GameState(Server);
		for (FName It : TransferItems)
		{
			Run->Start.Add(It, GS->CountEverywhere(It));
		}
		SetBadNetwork(true, 5);
	});
	// 200 rounds x (1 client request sent twice + 4 server-side transfers + 1 server replay) = 1,000 transfers.
	Repeat(Ctx, 200, 0.02f, [this, Run](int32 Round)
	{
		UWorld* Server = ServerWorld();
		AGFGameState* GS = GameState(Server);
		FRandomStream& Rng = Run->Rng;

		// Client request, resent with the same TxId (as a client would after a timeout).
		const bool bUseClient2 = !Run->bKicked && Rng.RandBool();
		AGFPlayerState* Sender = bUseClient2 ? Run->Client2.Get() : Run->Client1.Get();
		if (Sender)
		{
			const int32 ToIndex = Rng.RandBool() ? 0 : (bUseClient2 ? 1 : 2);
			const FGuid Tx = FGuid::NewGuid();
			const TArray<FGFItemStack> Stacks = SmallStacks(Rng);
			Sender->SendTransfer(Tx, Run->Keys[ToIndex], Stacks);
			Sender->SendTransfer(Tx, Run->Keys[ToIndex], Stacks);
			++Run->ClientAttempts;
			if (!bUseClient2)
			{
				Run->Client1Sent.Add(Tx);
			}
		}

		// Server-side transfers (rent, gifts, trades); the dropped client's account is its guest record.
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 From = Rng.RandRange(0, 2);
			const int32 To = (From + Rng.RandRange(1, 2)) % 3;
			const FGuid Tx = FGuid::NewGuid();
			Run->ServerApplied += GS->ServerTransfer(Tx, Run->Keys[From], Run->Keys[To], SmallStacks(Rng), TEXT("Test")) == EGFTxResult::Applied ? 1 : 0;
			Run->ServerTx.Add(Tx);
			++Run->ServerAttempts;
		}
		const FGuid Old = Run->ServerTx[Rng.RandRange(0, Run->ServerTx.Num() - 1)];
		const bool bWasApplied = GS->Ledger.WasApplied(Old);
		const EGFTxResult Replay = GS->ServerTransfer(Old, Run->Keys[0], Run->Keys[1], { FGFItemStack(TEXT("Dirt"), 1) }, TEXT("Replay"));
		if (bWasApplied && Replay != EGFTxResult::AlreadyApplied)
		{
			AddError(TEXT("A replayed TxId was applied again"));
		}

		if (Round == 100)
		{
			// Drop client 2 mid-run (kicked by the host).
			AGFPlayerState* OnServer = PlayerStateByKey(Server, Run->Keys[2]);
			APlayerController* PC = OnServer ? OnServer->GetPlayerController() : nullptr;
			AGameModeBase* GM = Server->GetAuthGameMode();
			if (TestNotNull(TEXT("Client 2 controller on the server"), PC) && GM && GM->GameSession)
			{
				GM->GameSession->KickPlayer(PC, FText::FromString(TEXT("GF test: drop mid-transfer")));
			}
			Run->bKicked = true;
		}
	});
	WaitUntil(Ctx, TEXT("dropped client is in the guest records"), 15.f, [Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		const FGFGuestRecord* Rec = GS ? GS->GuestRecords.Find(Run->Keys[2]) : nullptr;
		return Rec && Rec->bHasCharacter && !PlayerStateByKey(ServerWorld(), Run->Keys[2]);
	});
	WaitUntil(Ctx, TEXT("client 1 has a result for every request"), 30.f, [Run]()
	{
		const AGFPlayerState* C1 = Run->Client1.Get();
		if (!C1)
		{
			return false;
		}
		for (const FGuid& Tx : Run->Client1Sent)
		{
			if (!C1->TransferResults.Contains(Tx))
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, []() { SetBadNetwork(false); });
	WaitUntil(Ctx, TEXT("client 1's inventory matches the server's"), 10.f, [Run]()
	{
		const AGFPlayerState* C1 = Run->Client1.Get();
		const AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), Run->Keys[1]);
		return C1 && OnServer && C1->Inventory->GetRevision() == OnServer->Inventory->GetRevision()
			&& C1->Inventory->GetInventory().GetContentHash() == OnServer->Inventory->GetInventory().GetContentHash();
	});
	Do(Ctx, [this, Run]()
	{
		const AGFGameState* GS = GameState(ServerWorld());
		for (FName It : TransferItems)
		{
			TestEqual(FString::Printf(TEXT("%s: nothing duplicated or lost"), *It.ToString()), GS->CountEverywhere(It), Run->Start[It]);
		}
		TSet<FGuid> Unique(GS->Ledger.AppliedOrder);
		TestEqual(TEXT("No TxId applied twice"), Unique.Num(), GS->Ledger.AppliedOrder.Num());
		const int32 Total = Run->ServerAttempts + Run->ClientAttempts;
		AddInfo(FString::Printf(TEXT("Transfers: %d attempted (%d client, %d server), %d server-side applied"), Total, Run->ClientAttempts, Run->ServerAttempts, Run->ServerApplied));
		TestEqual(TEXT("1,000 transfers attempted"), Total, 1000);
		TestTrue(TEXT("Most server-side transfers applied"), Run->ServerApplied > 500);
		int32 C1Applied = 0;
		for (const TPair<FGuid, EGFTxResult>& P : Run->Client1->TransferResults)
		{
			C1Applied += (P.Value == EGFTxResult::Applied || P.Value == EGFTxResult::AlreadyApplied) && GS->Ledger.WasApplied(P.Key) ? 1 : 0;
		}
		TestTrue(TEXT("Client requests went through"), C1Applied > 30);
	});
	Stop(Ctx);
	return true;
}

#endif
