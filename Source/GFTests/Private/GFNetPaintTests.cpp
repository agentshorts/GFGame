// Block paint over the network (VoxelFinish + AGFPaintVolume): dye cost through the ledger, server-side reach and
// solid-block checks, and the per-player rate limit. One-process PIE, listen server + client, runs under -nullrhi.
#include "GFNetTestUtils.h"
#include "GFDyes.h"
#include "GFGameState.h"
#include "GFPaintVolume.h"
#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "GFInventoryComponent.h"
#include "GFVoxelWorld.h"
#include "VoxelFinish.h"
#include "VoxelFinishPainterComponent.h"
#include "VoxelFinishSession.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

static constexpr EAutomationTestFlags GFNetPaintTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

namespace GFNetPaint
{
	static const FName Rose(TEXT("RoseDye"));
	static const FName Sky(TEXT("SkyDye"));

	struct FState
	{
		FString Key;
		TArray<FIntVector> Near;  // solid, well inside reach
		FIntVector Far = FIntVector::ZeroValue;   // solid, out of reach
		FIntVector Air = FIntVector::ZeroValue;   // in bounds, in reach, empty
		FIntVector Outside = FIntVector(-50000, -50000, -50000);
		bool bAirChunkBefore = false;
		int32 SolidChunksBefore = 0;
		TArray<uint8> Rejected;
		int32 RejectedBefore = 0;
		int64 RoseBurnedBefore = 0;
		bool Ready() const { return Near.Num() >= 17; } // the setup found its cells; later steps are skipped otherwise
	};

	static FFinish Solid(const FLinearColor& C)
	{
		FFinish F;
		F.PatternId = VoxelFinish::SolidPatternId;
		F.ColorA = C;
		F.ColorB = FLinearColor::White;
		return F;
	}

	static UVoxelFinishPainterComponent* ClientPainter()
	{
		AGFPlayerController* PC = Cast<AGFPlayerController>(ClientWorlds()[0]->GetFirstPlayerController());
		return PC ? PC->GetPainter() : nullptr;
	}

	static void SetCount(AGFGameState* GS, AGFPlayerState* PS, FName Item, int32 Want)
	{
		const int32 Have = PS->Inventory->GetCount(Item);
		if (Have < Want)
		{
			GS->ServerMint(FGuid::NewGuid(), PS->PlayerKey, { FGFItemStack(Item, Want - Have) }, TEXT("Test"));
		}
		else if (Have > Want)
		{
			GS->ServerBurn(FGuid::NewGuid(), PS->PlayerKey, { FGFItemStack(Item, Have - Want) }, TEXT("Test"));
		}
	}

	static uint16 ServerFinishAt(const FIntVector& Cell)
	{
		const AGFPaintVolume* Vol = AGFPaintVolume::Find(ServerWorld());
		return Vol ? Vol->GetSession().GetGrid().GetFinishId(Cell) : 0;
	}

	static int32 CountRejected(const FState& S, EVoxelFinishPaintResult R, int32 From = 0)
	{
		int32 N = 0;
		for (int32 i = From; i < S.Rejected.Num(); ++i)
		{
			N += S.Rejected[i] == static_cast<uint8>(R) ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetPaintTest, "GFGame.Net.Paint", GFNetPaintTestFlags)
bool FGFNetPaintTest::RunTest(const FString&)
{
	using namespace GFNetPaint;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FState> S = MakeShared<FState>();
	const FLinearColor Red(0.9f, 0.1f, 0.1f);
	const FLinearColor Blue(0.1f, 0.3f, 0.9f);

	Do(Ctx, [this, S]()
	{
		UWorld* Server = ServerWorld();
		AGFGameState* GS = GameState(Server);
		GS->bFreeBuild = false;
		AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]);
		S->Key = Mine->PlayerKey;
		AGFPlayerState* OnServer = PlayerStateByKey(Server, S->Key);
		AGFPlayerController* ServerPC = Cast<AGFPlayerController>(OnServer->GetOwningController());
		TestNotNull(TEXT("The paint volume is spawned with the game state"), AGFPaintVolume::Find(Server));
		TestNotNull(TEXT("Every player controller has a painter"), ServerPC ? ServerPC->GetPainter() : nullptr);
		TestNotNull(TEXT("...on the client too"), ClientPainter());
		if (!ServerPC || !ServerPC->GetPainter() || !ClientPainter() || !OnServer->GetPawn())
		{
			return;
		}
		TestEqual(TEXT("Reach is 600 cm"), ServerPC->GetPainter()->MaxReachCm, 600.f);
		TestEqual(TEXT("Rate limit is 10 per second"), ServerPC->GetPainter()->MaxPaintsPerSecond, 10.f);

		// Cells around the client's pawn, measured the way the server measures them.
		const AGFVoxelWorld* V = Voxel(Server);
		const FGFVoxelGrid& Grid = V->GetAuthoritativeGrid();
		const FVector PawnAt = OnServer->GetPawn()->GetActorLocation();
		const FIntVector P = V->WorldToCell(PawnAt);
		auto Center = [V](const FIntVector& C) { return V->CellToWorld(C) + FVector(0.f, 0.f, GFBlockSize * 0.5f); };
		auto TopSolid = [&Grid, &P](int32 X, int32 Y, FIntVector& Out)
		{
			for (int32 Z = P.Z; Z >= P.Z - 8; --Z)
			{
				if (Grid.IsSolid(FIntVector(X, Y, Z)))
				{
					Out = FIntVector(X, Y, Z);
					return true;
				}
			}
			return false;
		};
		for (int32 DX = -2; DX <= 2; ++DX)
		{
			for (int32 DY = -2; DY <= 2; ++DY)
			{
				FIntVector C;
				if (TopSolid(P.X + DX, P.Y + DY, C) && FVector::Dist(PawnAt, Center(C)) < 450.f)
				{
					S->Near.Add(C);
				}
			}
		}
		bool bFar = false;
		for (const FIntVector& Dir : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
		{
			for (int32 Dist = 9; Dist <= 12 && !bFar; ++Dist)
			{
				FIntVector C;
				if (TopSolid(P.X + Dir.X * Dist, P.Y + Dir.Y * Dist, C) && FVector::Dist(PawnAt, Center(C)) > 750.f)
				{
					S->Far = C;
					bFar = true;
				}
			}
		}
		S->Air = P + FIntVector(0, 0, 2);
		TestTrue(TEXT("Found 17 solid blocks in reach"), S->Near.Num() >= 17);
		TestTrue(TEXT("Found a solid block out of reach"), bFar);
		TestTrue(TEXT("The air cell is in the world"), Grid.IsInBounds(S->Air));
		TestFalse(TEXT("...and empty"), Grid.IsSolid(S->Air));
		TestTrue(TEXT("...and in reach"), FVector::Dist(PawnAt, Center(S->Air)) < 450.f);
		TestFalse(TEXT("The outside cell is out of the world"), Grid.IsInBounds(S->Outside));
		S->bAirChunkBefore = Grid.HasChunk(FGFVoxelGrid::CellToChunk(S->Air));
		S->SolidChunksBefore = Grid.GetChunkCoords().Num();
		SetCount(GS, OnServer, Rose, 1);
		SetCount(GS, OnServer, Sky, 0);
		S->RoseBurnedBefore = GS->Ledger.GetBurned(Rose);
		ClientPainter()->OnPaintRejected.AddLambda([S](FIntVector, uint8 Result) { S->Rejected.Add(Result); });
	});
	WaitUntil(Ctx, TEXT("client sees its dyes"), 10.f, [S]()
	{
		const AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]);
		return Mine->Inventory->GetCount(Rose) == 1 && Mine->Inventory->GetCount(Sky) == 0;
	});
	// Cost, repaint, reach and solid-block checks.
	Do(Ctx, [S, Red, Blue]()
	{
		UVoxelFinishPainterComponent* Painter = ClientPainter();
		if (!S->Ready() || !Painter)
		{
			return;
		}
		FFinish Striped = Solid(Red);
		Striped.PatternId = 3;
		Striped.ColorB = Blue;
		Painter->RequestPaint(S->Near[0], Solid(Red));   // applied: burns the RoseDye, 7 credits left
		Painter->RequestPaint(S->Near[0], Solid(Red));   // same finish: free, no rejection
		Painter->RequestPaint(S->Near[1], Striped);      // needs SkyDye: CannotAfford
		Painter->RequestPaint(S->Far, Solid(Red));       // OutOfReach
		Painter->RequestPaint(S->Air, Solid(Red));       // NoBlock
		Painter->RequestPaint(S->Outside, Solid(Red));   // NoBlock
	});
	WaitUntil(Ctx, TEXT("paint applied and four rejections back on the client"), 10.f, [S]()
	{
		const AGFPaintVolume* ClientVol = AGFPaintVolume::Find(ClientWorlds()[0]);
		return !S->Ready() || S->Rejected.Num() >= 4 && ServerFinishAt(S->Near[0]) != 0 && ClientVol && ClientVol->GetSession().GetGrid().GetFinishId(S->Near[0]) != 0;
	});
	Do(Ctx, [this, S]()
	{
		if (!S->Ready())
		{
			return;
		}
		UWorld* Server = ServerWorld();
		AGFGameState* GS = GameState(Server);
		AGFPlayerState* OnServer = PlayerStateByKey(Server, S->Key);
		const AGFPaintVolume* Vol = AGFPaintVolume::Find(Server);
		const FGFVoxelGrid& Grid = Voxel(Server)->GetAuthoritativeGrid();
		TestEqual(TEXT("Exactly four rejections"), S->Rejected.Num(), 4);
		TestEqual(TEXT("Two-colour finish without SkyDye: CannotAfford"), CountRejected(*S, EVoxelFinishPaintResult::RejectedCannotAfford), 1);
		TestEqual(TEXT("Far block: OutOfReach"), CountRejected(*S, EVoxelFinishPaintResult::RejectedOutOfReach), 1);
		TestEqual(TEXT("Air and outside the world: NoBlock"), CountRejected(*S, EVoxelFinishPaintResult::RejectedNoBlock), 2);
		TestEqual(TEXT("One RoseDye burned for the block and none for the repaint"), GS->Ledger.GetBurned(Rose) - S->RoseBurnedBefore, static_cast<int64>(1));
		TestEqual(TEXT("RoseDye spent"), OnServer->Inventory->GetCount(Rose), 0);
		TestEqual(TEXT("7 block credits left"), Vol->GetCredits().GetCredits(S->Key, Rose), 7);
		TestEqual(TEXT("No SkyDye credit from the refused stripe"), Vol->GetCredits().GetCredits(S->Key, Sky), 0);
		TestEqual(TEXT("Only one block painted"), Vol->GetSession().GetGrid().NumPainted(), 1);
		TestEqual(TEXT("Refused stripe left its block bare"), ServerFinishAt(S->Near[1]), static_cast<uint16>(0));
		TestEqual(TEXT("Far block bare"), ServerFinishAt(S->Far), static_cast<uint16>(0));
		TestEqual(TEXT("Air not painted"), ServerFinishAt(S->Air), static_cast<uint16>(0));
		TestFalse(TEXT("Air cell is still air"), Grid.IsSolid(S->Air));
		TestEqual(TEXT("Painting created no voxel chunk"), Grid.HasChunk(FGFVoxelGrid::CellToChunk(S->Air)), S->bAirChunkBefore);
		TestEqual(TEXT("Voxel chunk count unchanged"), Grid.GetChunkCoords().Num(), S->SolidChunksBefore);
		TestNull(TEXT("Painting outside the world created no paint chunk"), Vol->GetSession().GetGrid().GetChunkUseCounts(VoxelFinish::CellToChunk(S->Outside)));
		SetCount(GS, OnServer, Rose, 2); // 7 credits + 16 blocks: plenty for the burst below
		S->RejectedBefore = S->Rejected.Num();
	});
	Wait(Ctx, 1.2f); // the bucket refills (10 tokens, 10 per second)

	// Rate limit: 15 paints in one frame, at most 10 (+1 for frame spread) get through.
	Do(Ctx, [S, Red]()
	{
		UVoxelFinishPainterComponent* Painter = ClientPainter();
		if (!S->Ready() || !Painter)
		{
			return;
		}
		for (int32 i = 2; i < 17; ++i)
		{
			Painter->RequestPaint(S->Near[i], Solid(Red));
		}
	});
	WaitUntil(Ctx, TEXT("the burst is answered"), 10.f, [S]()
	{
		if (!S->Ready())
		{
			return true;
		}
		int32 Painted = 0;
		for (int32 i = 2; i < 17; ++i)
		{
			Painted += ServerFinishAt(S->Near[i]) != 0 ? 1 : 0;
		}
		return Painted + (S->Rejected.Num() - S->RejectedBefore) >= 15;
	});
	Do(Ctx, [this, S]()
	{
		if (!S->Ready())
		{
			return;
		}
		int32 Painted = 0;
		for (int32 i = 2; i < 17; ++i)
		{
			Painted += ServerFinishAt(S->Near[i]) != 0 ? 1 : 0;
		}
		const int32 Limited = CountRejected(*S, EVoxelFinishPaintResult::RejectedRateLimited, S->RejectedBefore);
		TestTrue(*FString::Printf(TEXT("10 (or 11) of the 15 painted, got %d"), Painted), Painted >= 10 && Painted <= 11);
		TestEqual(TEXT("The rest were rate limited"), Limited, 15 - Painted);
		TestEqual(TEXT("Nothing else was rejected"), S->Rejected.Num() - S->RejectedBefore, Limited);
		const AGFPaintVolume* Vol = AGFPaintVolume::Find(ServerWorld());
		TestEqual(TEXT("Rate-limited paints cost nothing"), Vol->GetCredits().GetCredits(S->Key, Rose)
			+ PlayerStateByKey(ServerWorld(), S->Key)->Inventory->GetCount(Rose) * FGFDyes::BlocksPerDye, 7 + 2 * FGFDyes::BlocksPerDye - Painted);
	});
	Stop(Ctx);
	return true;
}

#endif
