#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace GFNetTest
{
	class FStepCommand : public IAutomationLatentCommand
	{
	public:
		FStepCommand(FCtxRef InCtx, FString InWhat, float InTimeout, TFunction<bool()> InPred, bool bInAlwaysRun = false)
			: Ctx(InCtx), What(MoveTemp(InWhat)), Timeout(InTimeout), Pred(MoveTemp(InPred)), bAlwaysRun(bInAlwaysRun)
		{
		}

		virtual bool Update() override
		{
			if (Ctx->bFailed && !bAlwaysRun)
			{
				return true;
			}
			if (StartTime < 0.0)
			{
				StartTime = FPlatformTime::Seconds();
			}
			if (Pred())
			{
				return true;
			}
			if (Timeout > 0.f && FPlatformTime::Seconds() - StartTime > Timeout)
			{
				Ctx->Test->AddError(FString::Printf(TEXT("Timed out after %.0f s waiting for: %s"), Timeout, *What));
				Ctx->bFailed = true;
				return true;
			}
			return false;
		}

	private:
		FCtxRef Ctx;
		FString What;
		float Timeout;
		TFunction<bool()> Pred;
		bool bAlwaysRun;
		double StartTime = -1.0;
	};

	static void AddStep(const FCtxRef& Ctx, const FString& What, float Timeout, TFunction<bool()> Pred, bool bAlwaysRun = false)
	{
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShareable(new FStepCommand(Ctx, What, Timeout, MoveTemp(Pred), bAlwaysRun)));
	}

	FCtxRef Start(FAutomationTestBase* Test, int32 NumPlayers, float TimeoutSeconds)
	{
		FCtxRef Ctx = MakeShared<FCtx>();
		Ctx->Test = Test;
		Ctx->Players = NumPlayers;
		Do(Ctx, [Ctx, NumPlayers]()
		{
			FString MapFile;
			if (FPackageName::TryConvertLongPackageNameToFilename(NetTestMap, MapFile, FPackageName::GetMapPackageExtension()) && FPaths::FileExists(MapFile))
			{
				FEditorFileUtils::LoadMap(MapFile, /*bLoadAsTemplate*/ false, /*bShowProgress*/ false);
			}
			else
			{
				Ctx->Test->AddWarning(FString::Printf(TEXT("%s not found; playing the current editor world"), NetTestMap));
			}
			// CommonUI complains at Error verbosity about PIE's plain viewport client; that is not a test failure.
			GEngine->Exec(nullptr, TEXT("log LogUIActionRouter off"));
			ULevelEditorPlaySettings* Settings = GetMutableDefault<ULevelEditorPlaySettings>();
			Settings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
			Settings->SetPlayNumberOfClients(NumPlayers);
			Settings->SetRunUnderOneProcess(true);
			Settings->bLaunchSeparateServer = false;
			FRequestPlaySessionParams Params;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;
			Params.EditorPlaySettings = Settings;
			GEditor->RequestPlaySession(Params);
		});
		AddStep(Ctx, FString::Printf(TEXT("%d players in and ready"), NumPlayers), TimeoutSeconds, [NumPlayers]() { return AllReady(NumPlayers); });
		return Ctx;
	}

	void Do(const FCtxRef& Ctx, TFunction<void()> Fn)
	{
		AddStep(Ctx, TEXT("do"), 0.f, [Fn = MoveTemp(Fn)]() { Fn(); return true; });
	}

	void Repeat(const FCtxRef& Ctx, int32 Count, float IntervalSeconds, TFunction<void(int32)> Fn)
	{
		TSharedRef<int32> Counter = MakeShared<int32>(0);
		TSharedRef<double> Next = MakeShared<double>(0.0);
		AddStep(Ctx, TEXT("repeat"), 0.f, [Counter, Next, Count, IntervalSeconds, Fn = MoveTemp(Fn)]()
		{
			const double Now = FPlatformTime::Seconds();
			if (Now >= *Next)
			{
				*Next = Now + IntervalSeconds;
				Fn((*Counter)++);
			}
			return *Counter >= Count;
		});
	}

	void WaitUntil(const FCtxRef& Ctx, const FString& What, float TimeoutSeconds, TFunction<bool()> Pred)
	{
		AddStep(Ctx, What, TimeoutSeconds, MoveTemp(Pred));
	}

	void Wait(const FCtxRef& Ctx, float Seconds)
	{
		TSharedRef<double> End = MakeShared<double>(-1.0);
		AddStep(Ctx, TEXT("wait"), 0.f, [End, Seconds]()
		{
			if (*End < 0.0)
			{
				*End = FPlatformTime::Seconds() + Seconds;
			}
			return FPlatformTime::Seconds() >= *End;
		});
	}

	void LateJoin(const FCtxRef& Ctx, float TimeoutSeconds)
	{
		Do(Ctx, [Ctx]()
		{
			Ctx->Players += 1;
			GEditor->RequestLateJoin();
		});
		AddStep(Ctx, TEXT("late joiner in and ready"), TimeoutSeconds, [Ctx]() { return AllReady(Ctx->Players); });
	}

	void Stop(const FCtxRef& Ctx)
	{
		AddStep(Ctx, TEXT("end PIE"), 0.f, []()
		{
			SetBadNetwork(false);
			if (GEditor->PlayWorld)
			{
				GEditor->RequestEndPlayMap();
			}
			return true;
		}, true);
		AddStep(Ctx, TEXT("PIE shut down"), 30.f, []() { return GEditor->PlayWorld == nullptr && !GEditor->IsPlaySessionInProgress(); }, true);
	}

	TArray<UWorld*> AllWorlds()
	{
		TArray<TPair<int32, UWorld*>> Found;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE && Ctx.World())
			{
				Found.Emplace(Ctx.PIEInstance, Ctx.World());
			}
		}
		Found.Sort([](const TPair<int32, UWorld*>& A, const TPair<int32, UWorld*>& B) { return A.Key < B.Key; });
		TArray<UWorld*> Out;
		for (const TPair<int32, UWorld*>& P : Found)
		{
			Out.Add(P.Value);
		}
		return Out;
	}

	UWorld* ServerWorld()
	{
		for (UWorld* W : AllWorlds())
		{
			if (W->GetNetMode() == NM_ListenServer || W->GetNetMode() == NM_DedicatedServer)
			{
				return W;
			}
		}
		return nullptr;
	}

	TArray<UWorld*> ClientWorlds()
	{
		TArray<UWorld*> Out;
		for (UWorld* W : AllWorlds())
		{
			if (W->GetNetMode() == NM_Client)
			{
				Out.Add(W);
			}
		}
		return Out;
	}

	AGFGameState* GameState(UWorld* World)
	{
		return World ? World->GetGameState<AGFGameState>() : nullptr;
	}

	AGFPlayerState* LocalPlayerState(UWorld* World)
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
				return PC->GetPlayerState<AGFPlayerState>();
			}
		}
		return nullptr;
	}

	AGFPlayerState* PlayerStateByKey(UWorld* World, const FString& Key)
	{
		const AGFGameState* GS = GameState(World);
		return GS ? GS->FindPlayerByKey(Key) : nullptr;
	}

	AGFVoxelWorld* Voxel(UWorld* World)
	{
		const AGFGameState* GS = GameState(World);
		return GS ? GS->GetVoxelWorld() : nullptr;
	}

	bool AllReady(int32 NumPlayers)
	{
		UWorld* Server = ServerWorld();
		const TArray<UWorld*> Clients = ClientWorlds();
		if (!Server || Clients.Num() != NumPlayers - 1)
		{
			return false;
		}
		const AGFGameState* SGS = GameState(Server);
		if (!SGS || SGS->PlayerArray.Num() != NumPlayers || !Voxel(Server))
		{
			return false;
		}
		for (APlayerState* PS : SGS->PlayerArray)
		{
			const AGFPlayerState* GPS = Cast<AGFPlayerState>(PS);
			if (!GPS || !GPS->bCharacterReady || !GPS->GetPawn())
			{
				return false;
			}
		}
		for (UWorld* C : Clients)
		{
			const AGFGameState* GS = GameState(C);
			const AGFPlayerState* PS = LocalPlayerState(C);
			const AGFVoxelWorld* V = Voxel(C);
			if (!GS || !PS || !V || GS->PlayerArray.Num() != NumPlayers || !PS->bCharacterReady || PS->PlayerKey.IsEmpty() || !PS->GetPawn())
			{
				return false;
			}
			if (V->GetAuthoritativeHash() != Voxel(Server)->GetAuthoritativeHash())
			{
				return false;
			}
		}
		return true;
	}

	void SetBadNetwork(bool bBad, int32 LossPercent)
	{
#if DO_ENABLE_NET_TEST
		for (UWorld* W : AllWorlds())
		{
			if (UNetDriver* Driver = W->GetNetDriver())
			{
				FPacketSimulationSettings S;
				if (bBad)
				{
					S.PktLag = 150;
					S.PktLagVariance = 30;
					S.PktLoss = LossPercent;
					S.PktOrder = 1;
				}
				Driver->SetPacketSimulationSettings(S);
			}
		}
#endif
	}

	bool VoxelsConverged(FString* OutWhy)
	{
		const AGFVoxelWorld* SV = Voxel(ServerWorld());
		if (!SV)
		{
			return false;
		}
		const uint32 Want = SV->GetAuthoritativeHash();
		int32 Index = 0;
		for (UWorld* C : ClientWorlds())
		{
			const AGFVoxelWorld* CV = Voxel(C);
			if (!CV || CV->GetAuthoritativeHash() != Want || CV->GetGrid().GetContentHash() != Want || CV->NumPendingPredictions() != 0)
			{
				if (OutWhy)
				{
					*OutWhy = FString::Printf(TEXT("client %d: auth %u display %u pending %d, server %u"), Index,
						CV ? CV->GetAuthoritativeHash() : 0u, CV ? CV->GetGrid().GetContentHash() : 0u, CV ? CV->NumPendingPredictions() : -1, Want);
				}
				return false;
			}
			++Index;
		}
		return true;
	}
}
