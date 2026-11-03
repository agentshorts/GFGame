// A show win's faction seat replicates. A client cannot move it.
#include "GFNetTestUtils.h"
#include "GFFactionLead.h"
#include "GFGameState.h"
#include "GFPlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetFashionQuestLeaderTest, "GFGame.Net.FashionQuest.LeaderReplicates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetFashionQuestLeaderTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	const FName Verdant(TEXT("Verdant"));
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FString> HostKey = MakeShared<FString>();
	TSharedRef<FString> WinnerKey = MakeShared<FString>();
	TSharedRef<FString> OtherKey = MakeShared<FString>();

	Do(Ctx, [HostKey, WinnerKey, OtherKey, Verdant]()
	{
		*HostKey = LocalPlayerState(ServerWorld())->PlayerKey;
		*WinnerKey = LocalPlayerState(ClientWorlds()[0])->PlayerKey;
		*OtherKey = LocalPlayerState(ClientWorlds()[1])->PlayerKey;
		AGFGameState* GS = GameState(ServerWorld());
		GS->ServerClaimFaction(LocalPlayerState(ServerWorld()), Verdant);
		GS->ServerClaimFaction(PlayerStateByKey(ServerWorld(), *WinnerKey), Verdant);
		GS->ServerClaimFaction(PlayerStateByKey(ServerWorld(), *OtherKey), Verdant);
	});
	WaitUntil(Ctx, TEXT("both clients see the first claimer"), 15.f, [HostKey, Verdant]()
	{
		for (UWorld* Client : ClientWorlds())
		{
			AGFGameState* GS = GameState(Client);
			if (!GS || GS->FactionLeaderOf(Verdant) != *HostKey)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [this, HostKey, WinnerKey, Verdant]()
	{
		AGFGameState* ClientGS = GameState(ClientWorlds()[0]);
		if (!TestNotNull(TEXT("Client game state"), ClientGS))
		{
			return;
		}
		TestEqual(TEXT("A client cannot move the seat"), ClientGS->ServerSetFactionLeader(Verdant, *WinnerKey, AGFGameState::LeaderReasonShowWin), FName(TEXT("NotAuthority")));
		TestEqual(TEXT("Client copy unchanged"), ClientGS->FactionLeaderOf(Verdant), *HostKey);
		AGFPlayerState* Winner = PlayerStateByKey(ServerWorld(), *WinnerKey);
		if (!Winner)
		{
			return;
		}
		Winner->FactionRank = EGFFactionRank::Champion;
		AGFGameState* ServerGS = GameState(ServerWorld());
		TestEqual(TEXT("Champion takes the seat"), ServerGS->ServerSetFactionLeader(Verdant, *WinnerKey, AGFGameState::LeaderReasonShowWin), FName(TEXT("Ok")));
		TestEqual(TEXT("Server seat"), ServerGS->FactionLeaderOf(Verdant), *WinnerKey);
	});
	WaitUntil(Ctx, TEXT("both clients see the new leader"), 15.f, [WinnerKey, Verdant]()
	{
		for (UWorld* Client : ClientWorlds())
		{
			AGFGameState* GS = GameState(Client);
			if (!GS || GS->FactionLeaderOf(Verdant) != *WinnerKey)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [this, WinnerKey, OtherKey, Verdant]()
	{
		for (UWorld* Client : ClientWorlds())
		{
			AGFGameState* GS = GameState(Client);
			TestEqual(TEXT("Replicated seat"), GS ? GS->FactionLeaderOf(Verdant) : FString(), *WinnerKey);
		}
		TestEqual(TEXT("The other member did not take it"), GameState(ServerWorld())->FactionLeaderOf(Verdant), *WinnerKey);
		TestFalse(TEXT("Other key"), *OtherKey == *WinnerKey);
	});
	Stop(Ctx);
	return true;
}

#endif
