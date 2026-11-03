// Rank and quest step replicate to the owning client. Two players do not share a step.
#include "GFNetTestUtils.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFGameState.h"
#include "GFPlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetQuestReplicatesTest, "GFGame.Net.Quest.Replicates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetQuestReplicatesTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ClientKey = MakeShared<FString>();
	TSharedRef<FString> HostKey = MakeShared<FString>();

	Do(Ctx, [ClientKey, HostKey]()
	{
		*HostKey = LocalPlayerState(ServerWorld())->PlayerKey;
		*ClientKey = LocalPlayerState(ClientWorlds()[0])->PlayerKey;
		UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(ServerWorld());
		if (Quest)
		{
			Quest->EnsureRegistered();
		}
		FGFQuestEvents::Raise(ServerWorld(), FGFQuestEvents::ElderAccepted(*ClientKey, TEXT("Verdant")));
		FGFQuestEvents::Raise(ServerWorld(), FGFQuestEvents::CrystalTaken(*HostKey, TEXT("Rime")));
	});
	WaitUntil(Ctx, TEXT("owning client sees step and rank, and the other player stays apart"), 15.f, [ClientKey, HostKey]()
	{
		const AGFPlayerState* Owner = LocalPlayerState(ClientWorlds()[0]);
		const AGFPlayerState* HostOnClient = PlayerStateByKey(ClientWorlds()[0], *HostKey);
		const AGFPlayerState* OwnerOnServer = PlayerStateByKey(ServerWorld(), *ClientKey);
		const AGFPlayerState* HostOnServer = PlayerStateByKey(ServerWorld(), *HostKey);
		return Owner && HostOnClient && OwnerOnServer && HostOnServer
			&& Owner->QuestStep == EGFQuestStep::HuntHide
			&& Owner->FactionRank == EGFFactionRank::Initiate
			&& Owner->QuestCount == 0
			&& Owner->FactionId == FName(TEXT("Verdant"))
			&& HostOnClient->QuestStep == EGFQuestStep::Escape
			&& HostOnClient->FactionRank == EGFFactionRank::Outsider
			&& OwnerOnServer->QuestStep == EGFQuestStep::HuntHide
			&& OwnerOnServer->FactionRank == EGFFactionRank::Initiate
			&& HostOnServer->QuestStep == EGFQuestStep::Escape
			&& HostOnServer->FactionRank == EGFFactionRank::Outsider;
	});
	Do(Ctx, [this, ClientKey, HostKey]()
	{
		const AGFPlayerState* Owner = LocalPlayerState(ClientWorlds()[0]);
		const UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(ServerWorld());
		if (!TestNotNull(TEXT("Owner"), Owner) || !TestNotNull(TEXT("Server quest"), Quest))
		{
			return;
		}
		TestTrue(TEXT("Owner step"), Owner->QuestStep == EGFQuestStep::HuntHide);
		TestTrue(TEXT("Owner rank"), Owner->FactionRank == EGFFactionRank::Initiate);
		const FGFQuestProgress* ClientLine = Quest->FindProgress(*ClientKey);
		const FGFQuestProgress* HostLine = Quest->FindProgress(*HostKey);
		TestTrue(TEXT("Lines differ"), ClientLine && HostLine && ClientLine->Step != HostLine->Step);
		TestTrue(TEXT("Client line"), ClientLine && ClientLine->Step == EGFQuestStep::HuntHide);
		TestTrue(TEXT("Host line"), HostLine && HostLine->Step == EGFQuestStep::Escape);
	});
	Stop(Ctx);
	return true;
}

#endif
