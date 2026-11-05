// Acceleration Hop (issue 334): the server side of a client's hop. Listen server plus one client on L_NetTest.
// The server applies a client's reported hop only on a tile under that pawn, once per landing, and its own copy
// of the client's movement then runs faster.
// v2 (issue 342): the road's lit target tile replicates, so the client sees the same tile lit, and the server pays a
// landing on it 1.5x, once.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.AccelHop;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFAccelHop.h"
#include "GFAccelHopRoad.h"
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

	AGFAccelHopRoad* NetAccelHopRoad(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFAccelHopRoad> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/** The test tile of Kind in World (each world spawns its own copy at the same spot, as a placed map would). */
	AGFAccelHopTile* NetAccelHopTileOf(UWorld* World, EGFHopTileKind Kind)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
		{
			if (It->GetKind() == Kind)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Every client road shows lit index Index with the matching tile drawn lit (or, for -1, nothing lit). */
	bool NetAccelHopClientsShow(int32 Index)
	{
		const TArray<UWorld*> Clients = GFNetTest::ClientWorlds();
		if (Clients.Num() == 0)
		{
			return false;
		}
		for (UWorld* World : Clients)
		{
			const AGFAccelHopRoad* Road = NetAccelHopRoad(World);
			if (!Road || Road->GetLitIndex() != Index)
			{
				return false;
			}
			for (TActorIterator<AGFAccelHopTile> It(World); It; ++It)
			{
				if (It->IsLitVisual() != (Index != INDEX_NONE && It->GetRoadIndex() == Index))
				{
					return false;
				}
			}
		}
		return true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetAccelHopLitTileTest, "GFGame.Net.AccelHop.LitTile", GFNetAccelHopDetail::NetAccelHopFlags)
bool FGFNetAccelHopLitTileTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<int32> LitIndex = MakeShared<int32>(INDEX_NONE);
	WaitUntil(Ctx, TEXT("the client pawn is on the server"), 20.f, []()
	{
		return GFNetAccelHopDetail::NetAccelHopRemoteBody(ServerWorld()) != nullptr && ClientWorlds().Num() > 0;
	});
	Do(Ctx, [Ctx]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = GFNetAccelHopDetail::NetAccelHopRemoteBody(World);
		if (!Body)
		{
			Ctx->Test->AddError(TEXT("No client pawn on the server"));
			Ctx->bFailed = true;
			return;
		}
		// Same tiles at the same spots on every machine (map-placed tiles do not replicate), under the remote pawn.
		const FVector Feet = Body->GetActorLocation() - FVector(0.0, 0.0, 200.0);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (UWorld* Each : AllWorlds())
		{
			AGFAccelHopTile* Target = Each->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(Feet), Params);
			AGFAccelHopTile* Plain = Each->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(Feet + FVector(150.0, 0.0, 0.0)), Params);
			if (!Target || !Plain)
			{
				Ctx->Test->AddError(TEXT("Could not spawn the test tiles"));
				Ctx->bFailed = true;
				return;
			}
			Target->SetKind(EGFHopTileKind::Boost);
			Plain->SetKind(EGFHopTileKind::Standard);
		}
		AGFAccelHopRoad* Road = World->SpawnActor<AGFAccelHopRoad>(AGFAccelHopRoad::StaticClass(), FTransform(Feet), Params);
		if (!Road)
		{
			Ctx->Test->AddError(TEXT("Could not spawn the road"));
			Ctx->bFailed = true;
			return;
		}
		Road->SetAutoPick(false);
		Ctx->Test->TestEqual(TEXT("The server road sees both tiles"), Road->GetTiles().Num(), 2);
	});
	WaitUntil(Ctx, TEXT("the road reaches the client"), 10.f, []()
	{
		for (UWorld* World : ClientWorlds())
		{
			const AGFAccelHopRoad* Road = GFNetAccelHopDetail::NetAccelHopRoad(World);
			if (!Road || Road->GetTiles().Num() != 2)
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [Ctx, LitIndex]()
	{
		AGFAccelHopRoad* Road = GFNetAccelHopDetail::NetAccelHopRoad(ServerWorld());
		AGFAccelHopTile* Target = GFNetAccelHopDetail::NetAccelHopTileOf(ServerWorld(), EGFHopTileKind::Boost);
		if (!Road || !Target)
		{
			Ctx->Test->AddError(TEXT("Lost the server road or tile"));
			Ctx->bFailed = true;
			return;
		}
		Road->ServerSetLit(Target->GetRoadIndex(), ServerWorld()->GetTimeSeconds());
		*LitIndex = Target->GetRoadIndex();
		Ctx->Test->TestTrue(TEXT("The server draws its own pick (listen host)"), Target->IsLitVisual());
	});
	WaitUntil(Ctx, TEXT("the client sees the same lit tile"), 10.f, [LitIndex]()
	{
		return *LitIndex != INDEX_NONE && GFNetAccelHopDetail::NetAccelHopClientsShow(*LitIndex);
	});
	Do(Ctx, [Ctx]()
	{
		for (UWorld* World : ClientWorlds())
		{
			const AGFAccelHopRoad* Road = GFNetAccelHopDetail::NetAccelHopRoad(World);
			const AGFAccelHopTile* Lit = Road ? Road->GetLitTile() : nullptr;
			Ctx->Test->TestTrue(TEXT("The client's lit tile is the Boost tile, same as the server's"), Lit && Lit->GetKind() == EGFHopTileKind::Boost);
		}
		UWorld* World = ServerWorld();
		ACharacter* Body = GFNetAccelHopDetail::NetAccelHopRemoteBody(World);
		UGFCharacterMovementComponent* Move = Body ? Cast<UGFCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
		AGFAccelHopRoad* Road = GFNetAccelHopDetail::NetAccelHopRoad(World);
		AGFAccelHopTile* Target = GFNetAccelHopDetail::NetAccelHopTileOf(World, EGFHopTileKind::Boost);
		if (!Move || !Road || !Target)
		{
			Ctx->Test->AddError(TEXT("Lost the pawn, road or tile"));
			Ctx->bFailed = true;
			return;
		}
		const FGFAccelHopTuning& Hop = Move->GetTuning().AccelHop;
		const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
		const FGFAccelHopHit Hit = FGFAccelHopRules::JudgeLanding(9.0, 120.f, Windows, 4);
		const float Before = Move->GetAccelHopMultiplier();
		Ctx->Test->TestTrue(TEXT("The client's hop on the lit tile is applied"), Move->ApplyServerAccelHop(Target, Hit, 0.5f));
		Ctx->Test->TestTrue(TEXT("The server counted it lit"), Move->WasLastAccelHopLit());
		const float Want = FMath::Min(Hop.MaxMultiplier, Before + (Hop.PerfectBoost + Hop.BoostTileBonus) * Hop.LitBoostScale);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("1.5x the gain (%.3f, want %.3f)"), Move->GetAccelHopMultiplier(), Want), FMath::IsNearlyEqual(Move->GetAccelHopMultiplier(), Want, 0.01f));
		Ctx->Test->TestEqual(TEXT("Paid out: the server's road has no lit tile"), Road->GetLitIndex(), static_cast<int32>(INDEX_NONE));
	});
	WaitUntil(Ctx, TEXT("the client sees the tile go dark"), 10.f, []()
	{
		return GFNetAccelHopDetail::NetAccelHopClientsShow(INDEX_NONE);
	});
	Wait(Ctx, 0.4f);
	Do(Ctx, [Ctx]()
	{
		UWorld* World = ServerWorld();
		ACharacter* Body = GFNetAccelHopDetail::NetAccelHopRemoteBody(World);
		UGFCharacterMovementComponent* Move = Body ? Cast<UGFCharacterMovementComponent>(Body->GetCharacterMovement()) : nullptr;
		AGFAccelHopTile* Plain = GFNetAccelHopDetail::NetAccelHopTileOf(World, EGFHopTileKind::Standard);
		if (!Move || !Plain)
		{
			Ctx->Test->AddError(TEXT("Lost the pawn or the plain tile"));
			Ctx->bFailed = true;
			return;
		}
		// A client cannot claim a tile is lit: the server only trusts its own pick, and nothing is lit now.
		const FGFAccelHopTuning& Hop = Move->GetTuning().AccelHop;
		const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, EGFBeatDifficulty::Normal);
		const FGFAccelHopHit Hit = FGFAccelHopRules::JudgeLanding(13.0, 120.f, Windows, 4);
		const float Before = Move->GetAccelHopMultiplier();
		Ctx->Test->TestTrue(TEXT("A plain hop past the rate limit is applied"), Move->ApplyServerAccelHop(Plain, Hit, 0.5f));
		Ctx->Test->TestFalse(TEXT("Not lit"), Move->WasLastAccelHopLit());
		const float Want = FMath::Min(Hop.MaxMultiplier, Before + Hop.PerfectBoost);
		Ctx->Test->TestTrue(*FString::Printf(TEXT("The normal gain (%.3f, want %.3f)"), Move->GetAccelHopMultiplier(), Want), FMath::IsNearlyEqual(Move->GetAccelHopMultiplier(), Want, 0.01f));
		for (UWorld* Each : AllWorlds())
		{
			for (TActorIterator<AGFAccelHopTile> It(Each); It; ++It)
			{
				It->Destroy();
			}
		}
		if (AGFAccelHopRoad* Road = GFNetAccelHopDetail::NetAccelHopRoad(World))
		{
			Road->Destroy();
		}
	});
	Stop(Ctx);
	return true;
}

#endif
