// Two clients. One holds the field, the other world watches the voxel fall over, then a claw clears a short line.
#include "GFNetTestUtils.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMagicActors.h"
#include "GFMagicComponent.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetMagicTest
{
	static int32 Ribbons(UWorld* World)
	{
		int32 Count = 0;
		if (!World)
		{
			return 0;
		}
		for (TActorIterator<AGFMagicDropRibbon> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	// CellToWorld sits on the bottom face of a cell. Gravity and a client move drop the capsule off that plane, and the claw tunnel then starts a layer below the aimed line.
	static void HoldStill(APawn* Pawn)
	{
		if (!Pawn)
		{
			return;
		}
		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
				Move->Velocity = FVector::ZeroVector;
				Move->GravityScale = 0.f;
				Move->DisableMovement();
				Move->SetComponentTickEnabled(false);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetMagicMiningTest, "GFGame.Net.MagicMining", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetMagicMiningTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetMagicTest;
	AddExpectedMessage(TEXT("Your connection to the host has been lost"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 0, false);
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> Key = MakeShared<FString>();
	TSharedRef<int32> DirtBefore = MakeShared<int32>(0);
	TSharedRef<bool> SawRibbon = MakeShared<bool>(false);
	TSharedRef<bool> SawClawRibbon = MakeShared<bool>(false);

	Do(Ctx, [this, Key, DirtBefore]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		AGFGameState* GS = GameState(Server);
		AGFPlayerState* Mine = LocalPlayerState(Client);
		AGFPlayerState* OnServer = Mine ? PlayerStateByKey(Server, Mine->PlayerKey) : nullptr;
		AGFVoxelWorld* Grid = Voxel(Server);
		if (!TestNotNull(TEXT("Server digger"), OnServer) || !TestNotNull(TEXT("Voxel"), Grid) || !TestNotNull(TEXT("Client magic owner"), Mine) || !GS)
		{
			return;
		}
		*Key = Mine->PlayerKey;
		GS->bFreeBuild = true;
		GS->bEnforcePlotPermissions = false;
		GS->MaxEditReach = 0.f;
		const FIntVector Dirt(8, 8, 6);
		const FIntVector Fabric(9, 8, 6);
		const FIntVector Stone(8, 9, 6);
		TestTrue(TEXT("Dirt placed"), Grid->ServerApplyEdit(Dirt, TEXT("Dirt")));
		TestTrue(TEXT("Fabric placed"), Grid->ServerApplyEdit(Fabric, TEXT("Fabric")));
		TestTrue(TEXT("Stone placed by hand"), OnServer->ServerHandleEdit(Stone, TEXT("Stone")));
		if (UGFBlockIntegritySubsystem* Magic = Server->GetSubsystem<UGFBlockIntegritySubsystem>())
		{
			TestTrue(TEXT("A player place is spell-proof"), Magic->IsPlaced(Stone));
		}
		APawn* Pawn = OnServer->GetPawn();
		if (TestNotNull(TEXT("Pawn"), Pawn))
		{
			HoldStill(Pawn);
			HoldStill(Mine->GetPawn());
			Pawn->SetActorLocation(Grid->CellToWorld(Dirt), false, nullptr, ETeleportType::TeleportPhysics);
			HoldStill(Pawn);
		}
		*DirtBefore = OnServer->Inventory->GetCount(TEXT("Dirt"));
		if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
		{
			Magic->SetAim(Grid->CellToWorld(Dirt), Grid->CellToWorld(Dirt));
		}
	});
	WaitUntil(Ctx, TEXT("the server is aiming at the dirt"), 5.f, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		AGFVoxelWorld* Grid = Voxel(ServerWorld());
		UGFMagicComponent* Magic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		return Magic && Grid && FVector::Dist(Magic->GetAimPoint(), Grid->CellToWorld(FIntVector(8, 8, 6))) < 50.f;
	});
	Do(Ctx, [Key]()
	{
		if (AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]))
		{
			if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
			{
				Magic->SetFieldHeld(true);
			}
		}
		(void)Key;
	});
	WaitUntil(Ctx, TEXT("the dirt reaches the owner and the neighbours stay"), 8.f, [Key, DirtBefore, SawRibbon]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		AGFVoxelWorld* ServerGrid = Voxel(Server);
		AGFVoxelWorld* ClientGrid = Voxel(Client);
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *Key);
		AGFPlayerState* Mine = LocalPlayerState(Client);
		if (Ribbons(Server) > 0 || Ribbons(Client) > 0)
		{
			*SawRibbon = true;
		}
		return ServerGrid && ClientGrid && OnServer && Mine
			&& ServerGrid->GetBlock(FIntVector(8, 8, 6)).IsNone()
			&& ClientGrid->GetBlock(FIntVector(8, 8, 6)).IsNone()
			&& ServerGrid->GetBlock(FIntVector(9, 8, 6)) == FName(TEXT("Fabric"))
			&& ServerGrid->GetBlock(FIntVector(8, 9, 6)) == FName(TEXT("Stone"))
			&& OnServer->Inventory->GetCount(TEXT("Dirt")) == *DirtBefore + 1
			&& Mine->Inventory->GetCount(TEXT("Dirt")) == *DirtBefore + 1
			&& *SawRibbon
			&& VoxelsConverged();
	});
	Do(Ctx, []()
	{
		if (AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]))
		{
			if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
			{
				Magic->SetFieldHeld(false);
			}
		}
	});
	WaitUntil(Ctx, TEXT("the field let go"), 5.f, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		UGFMagicComponent* Magic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		return Magic && !Magic->IsFieldHeld();
	});
	Do(Ctx, [this, Key]()
	{
		UWorld* Server = ServerWorld();
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *Key);
		AGFVoxelWorld* Grid = Voxel(Server);
		AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]);
		UGFMagicComponent* Magic = Mine ? Mine->FindComponentByClass<UGFMagicComponent>() : nullptr;
		if (!OnServer || !Grid || !Magic || !TestNotNull(TEXT("Pawn for the claw"), OnServer->GetPawn()))
		{
			return;
		}
		for (int32 Step = 0; Step < 6; ++Step)
		{
			Grid->ServerApplyEdit(FIntVector(20 + Step, 8, 12), TEXT("Dirt"));
		}
		const FVector At = Grid->CellToWorld(FIntVector(19, 8, 12));
		HoldStill(OnServer->GetPawn());
		HoldStill(Mine->GetPawn());
		OnServer->GetPawn()->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		HoldStill(OnServer->GetPawn());
		Magic->SetAim(At, Grid->CellToWorld(FIntVector(25, 8, 12)));
	});
	WaitUntil(Ctx, TEXT("the claw aim arrived with the line still standing"), 5.f, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		AGFVoxelWorld* Grid = Voxel(ServerWorld());
		UGFMagicComponent* Magic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		if (!Magic || !Grid || Magic->IsFieldHeld())
		{
			return false;
		}
		if (FVector::Dist(Magic->GetAimPoint(), Grid->CellToWorld(FIntVector(25, 8, 12))) >= 80.f)
		{
			return false;
		}
		for (int32 Step = 0; Step < 6; ++Step)
		{
			if (Grid->GetBlock(FIntVector(20 + Step, 8, 12)) != FName(TEXT("Dirt")))
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]);
		AGFVoxelWorld* Grid = Voxel(ServerWorld());
		if (!OnServer || !Mine || !Grid)
		{
			return;
		}
		const FVector At = Grid->CellToWorld(FIntVector(19, 8, 12));
		HoldStill(OnServer->GetPawn());
		HoldStill(Mine->GetPawn());
		if (APawn* Pawn = OnServer->GetPawn())
		{
			Pawn->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
			HoldStill(Pawn);
		}
		if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
		{
			Magic->RequestClaw();
		}
	});
	WaitUntil(Ctx, TEXT("the claw cleared the line and the owner saw it"), 8.f, [Key, SawClawRibbon]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		AGFVoxelWorld* Grid = Voxel(Server);
		AGFPlayerState* Mine = LocalPlayerState(Client);
		UGFMagicComponent* Magic = Mine ? Mine->FindComponentByClass<UGFMagicComponent>() : nullptr;
		if (Ribbons(Client) > 0)
		{
			*SawClawRibbon = true;
		}
		if (!Grid || !Magic)
		{
			return false;
		}
		for (int32 Step = 0; Step < 6; ++Step)
		{
			if (!Grid->GetBlock(FIntVector(20 + Step, 8, 12)).IsNone())
			{
				return false;
			}
		}
		return Magic->GetClawCharges() == 1 && *SawClawRibbon && VoxelsConverged();
	});
	Stop(Ctx);
	return true;
}

#endif
