#pragma once

// Multi-client PIE harness for headless automation (-nullrhi): one process, listen server + N-1 clients.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

class UWorld;
class AGFGameState;
class AGFPlayerState;
class AGFVoxelWorld;

namespace GFNetTest
{
	static const TCHAR* NetTestMap = TEXT("/Game/Maps/L_NetTest");

	struct FCtx
	{
		FAutomationTestBase* Test = nullptr;
		bool bFailed = false;
		int32 Players = 0;
	};
	using FCtxRef = TSharedRef<FCtx>;

	/** Loads the test map and starts PIE with NumPlayers (host + clients), then waits until everyone is in. */
	FCtxRef Start(FAutomationTestBase* Test, int32 NumPlayers, float TimeoutSeconds = 60.f);
	/** Latent: Fn runs once. */
	void Do(const FCtxRef& Ctx, TFunction<void()> Fn);
	/** Latent: Fn(i) for i in [0, Count), one call every IntervalSeconds (keeps reliable RPC queues sane). */
	void Repeat(const FCtxRef& Ctx, int32 Count, float IntervalSeconds, TFunction<void(int32)> Fn);
	/** Latent: waits for Pred; on timeout reports What and skips the remaining steps. */
	void WaitUntil(const FCtxRef& Ctx, const FString& What, float TimeoutSeconds, TFunction<bool()> Pred);
	void Wait(const FCtxRef& Ctx, float Seconds);
	/** Adds a client to the running session ("late join") and waits for it. */
	void LateJoin(const FCtxRef& Ctx, float TimeoutSeconds = 60.f);
	/** Ends PIE and waits until it has shut down. Always runs, even after a failure. */
	void Stop(const FCtxRef& Ctx);

	UWorld* ServerWorld();
	/** Client worlds, in join order. */
	TArray<UWorld*> ClientWorlds();
	TArray<UWorld*> AllWorlds();
	AGFGameState* GameState(UWorld* World);
	AGFPlayerState* LocalPlayerState(UWorld* World);
	/** The player state for Key as seen in World. */
	AGFPlayerState* PlayerStateByKey(UWorld* World, const FString& Key);
	AGFVoxelWorld* Voxel(UWorld* World);
	bool AllReady(int32 NumPlayers);
	/** "Bad" profile from PLAN.md section 4.1: 150 ms +-30 lag, 3% loss, reordering. On every PIE net driver. */
	void SetBadNetwork(bool bBad, int32 LossPercent = 3);
	/** Every client's confirmed and displayed grid equal the server's, with no predictions pending. */
	bool VoxelsConverged(FString* OutWhy = nullptr);
}
