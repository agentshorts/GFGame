// Issue 335: the world map spawns a player with no home at the rest area (PlayerStart_RestArea, tagged RestArea), and a
// player with a home at the home (AGFPlayerState::SetHome stub). Runs the world map in PIE as a listen server with one
// player, so the real AGFGameMode::ChoosePlayerStart and the placed start are used.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.RestAreaSpawn;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetRestAreaSpawnDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	// Rest_Area_Layout_v1 player start (24000, -2400) on the world map; co-op pawns land within a few metres of it.
	const FVector RestStart(24000.f, -2400.f, 120.f);
	// A home on open grass west of the rest area, away from the road (a stand-in for the home-setting quest).
	const FVector HomeAt(20000.f, -3000.f, 120.f);
	constexpr float NearCm = 600.f;

	static APlayerController* LocalPC(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return PC;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetRestAreaSpawnTest, "GFGame.Net.RestAreaSpawn", GFNetRestAreaSpawnDetail::Flags)
bool FGFNetRestAreaSpawnTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetRestAreaSpawnDetail;
	FCtxRef Ctx = StartOnMap(this, TEXT("/Game/Maps/L_World_Factions_A"), 1, 120.f);
	WaitUntil(Ctx, TEXT("local controller with a pawn"), 20.f, []()
	{
		const APlayerController* PC = LocalPC(ServerWorld());
		return PC && PC->GetPawn() && PC->GetPlayerState<AGFPlayerState>();
	});
	Do(Ctx, [Ctx]()
	{
		APlayerController* PC = LocalPC(ServerWorld());
		AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
		AGameModeBase* GM = ServerWorld() ? ServerWorld()->GetAuthGameMode() : nullptr;
		if (!PC || !PS || !GM || !PC->GetPawn())
		{
			Ctx->Test->AddError(TEXT("No controller, player state, pawn or game mode"));
			Ctx->bFailed = true;
			return;
		}
		const FVector At = PC->GetPawn()->GetActorLocation();
		Ctx->Test->AddInfo(FString::Printf(TEXT("Default spawn at %s"), *At.ToCompactString()));
		Ctx->Test->TestFalse(TEXT("A new player has no home"), PS->HasHome());
		Ctx->Test->TestTrue(TEXT("Default spawn is at the rest area"), FVector::Dist2D(At, RestStart) < NearCm);

		// Set a home, then respawn the same player.
		PS->SetHome(FTransform(FRotator(0.f, 90.f, 0.f), HomeAt));
		Ctx->Test->TestTrue(TEXT("SetHome sets HasHome"), PS->HasHome());
		Ctx->Test->TestTrue(TEXT("SetHome keeps the transform"), PS->GetHomeTransform().GetLocation().Equals(HomeAt, 0.1f));
		APawn* Old = PC->GetPawn();
		PC->UnPossess();
		Old->Destroy();
		GM->RestartPlayer(PC);
	});
	WaitUntil(Ctx, TEXT("the player respawns"), 10.f, []()
	{
		const APlayerController* PC = LocalPC(ServerWorld());
		return PC && PC->GetPawn();
	});
	Do(Ctx, [Ctx]()
	{
		const APlayerController* PC = LocalPC(ServerWorld());
		const FVector At = PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		Ctx->Test->AddInfo(FString::Printf(TEXT("Respawn with a home at %s"), *At.ToCompactString()));
		Ctx->Test->TestTrue(TEXT("With a home the player respawns at home"), FVector::Dist2D(At, HomeAt) < NearCm);
		Ctx->Test->TestTrue(TEXT("Not at the rest area any more"), FVector::Dist2D(At, RestStart) > NearCm);
	});
	Stop(Ctx);
	return true;
}

#endif
