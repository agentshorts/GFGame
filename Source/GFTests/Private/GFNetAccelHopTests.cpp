// Acceleration Hop (issue 334): the server side of a client's hop. Listen server plus one client on L_NetTest.
// The server applies a client's reported hop only on a tile under that pawn, once per landing, and its own copy
// of the client's movement then runs faster.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.AccelHop;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFAccelHop.h"
#include "GFAccelHopTile.h"
#include "GFCharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetAccelHopDetail
{
	constexpr EAutomationTestFlags NetAccelHopFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** The remote client's pawn as the server sees it. */
	ACharacter* NetAccelHopRemoteBody(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && !PC->IsLocalController())
			{
				return Cast<ACharacter>(PC->GetPawn());
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetAccelHopServerTest, "GFGame.Net.AccelHop.ServerChecks", GFNetAccelHopDetail::NetAccelHopFlags)
bool FGFNetAccelHopServerTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	FCtxRef Ctx = Start(this, 2);
	WaitUntil(Ctx, TEXT("the client pawn is on the server"), 20.f, []()
	{
		return GFNetAccelHopDetail::NetAccelHopRemoteBody(ServerWorld()) != nullptr;
	});
	Do(Ctx, [Ctx]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = GFNetAccelHopDetail::NetAccelHopRemoteBody(World);
		UGFCharacterMovementComponent* Move = Body ? Cast<UGFCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			Ctx->Test->AddError(TEXT("No GF movement on the client pawn"));
			Ctx->bFailed = true;
			return;
		}
		const FVector Feet = Body->GetActorLocation();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFAccelHopTile* Under = World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(Feet - FVector(0.0, 0.0, 200.0)), Params);
		AGFAccelHopTile* Far = World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(Feet + FVector(5000.0, 0.0, -200.0)), Params);
		if (!Under || !Far)
		{
			Ctx->Test->AddError(TEXT("Could not spawn the test tiles"));
			Ctx->bFailed = true;
			return;
		}
		Under->SetKind(EGFHopTileKind::Boost);

		const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
		FGFAccelHopHit Hit = FGFAccelHopRules::JudgeLanding(9.0, 120.f, Windows, 4);
		const float Before = Move->GetAccelHopMultiplier();
		Ctx->Test->TestFalse(TEXT("A tile across the map is refused"), Move->ApplyServerAccelHop(Far, Hit, 0.5f));
		Ctx->Test->TestTrue(TEXT("A tile under the pawn is applied"), Move->ApplyServerAccelHop(Under, Hit, 0.5f));
		Ctx->Test->TestTrue(TEXT("The server's copy runs faster"), Move->GetAccelHopMultiplier() > Before);
		const float After = Move->GetAccelHopMultiplier();
		FGFAccelHopHit Again = FGFAccelHopRules::JudgeLanding(10.0, 120.f, Windows, 4);
		Ctx->Test->TestFalse(TEXT("A second hop in the same instant is rate limited"), Move->ApplyServerAccelHop(Under, Again, 0.5f));
		Ctx->Test->TestTrue(TEXT("Rate limit leaves the boost alone"), FMath::IsNearlyEqual(Move->GetAccelHopMultiplier(), After));
		Under->SetKind(EGFHopTileKind::Downbeat);
	});
	Wait(Ctx, 0.4f);
	Do(Ctx, [Ctx]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = GFNetAccelHopDetail::NetAccelHopRemoteBody(World);
		UGFCharacterMovementComponent* Move = Body ? Cast<UGFCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
		AGFAccelHopTile* Under = nullptr;
		for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
		{
			if (It->GetKind() == EGFHopTileKind::Downbeat)
			{
				Under = *It;
			}
		}
		if (!Move || !Under)
		{
			Ctx->Test->AddError(TEXT("Lost the pawn or the tile"));
			Ctx->bFailed = true;
			return;
		}
		// A client claims beat 13 is a bar line. The server works it out itself, so no Downbeat bonus.
		const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
		FGFAccelHopHit Lie = FGFAccelHopRules::JudgeLanding(13.0, 120.f, Windows, 4);
		Lie.bOnBarLine = true;
		const FGFAccelHopTuning& Hop = Move->GetTuning().AccelHop;
		const float Current = Move->GetAccelHopMultiplier();
		Ctx->Test->TestTrue(TEXT("Past the rate limit, the hop is applied"), Move->ApplyServerAccelHop(Under, Lie, 0.5f));
		Ctx->Test->TestFalse(TEXT("The server recomputed the bar line"), Move->GetLastAccelHopHit().bOnBarLine);
		const float Want = FMath::Min(Hop.MaxMultiplier, Current + Hop.PerfectBoost);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("No Downbeat bonus (%.3f, want %.3f)"), Move->GetAccelHopMultiplier(), Want), FMath::IsNearlyEqual(Move->GetAccelHopMultiplier(), Want, 0.01f));
		for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
		{
			It->Destroy();
		}
	});
	Stop(Ctx);
	return true;
}

#endif
