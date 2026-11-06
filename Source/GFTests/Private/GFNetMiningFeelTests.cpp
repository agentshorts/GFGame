// The aimed cell's integrity fraction replicates to the owner. The wound map does not.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFMagicComponent.h"
#include "GFMiningFeel.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void HoldStill(APawn* Pawn)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetMiningFeelFractionTest, "GFGame.Net.MiningFeel.TargetedFraction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetMiningFeelFractionTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> Key = MakeShared<FString>();
	const FIntVector Stone(8, 8, 6);
	const FIntVector Neighbour(9, 8, 6);

	Do(Ctx, [this, Key]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFGameState* GS = GameState(Server);
		AGFPlayerState* Mine = Client ? LocalPlayerState(Client) : nullptr;
		AGFPlayerState* OnServer = Mine ? PlayerStateByKey(Server, Mine->PlayerKey) : nullptr;
		AGFVoxelWorld* Grid = Voxel(Server);
		if (!TestNotNull(TEXT("Server digger"), OnServer) || !TestNotNull(TEXT("Client digger"), Mine) || !GS || !TestNotNull(TEXT("Voxel"), Grid))
		{
			return;
		}
		*Key = Mine->PlayerKey;
		GS->bFreeBuild = true;
		GS->bEnforcePlotPermissions = false;
		GS->MaxEditReach = 0.f;
		TestTrue(TEXT("Stone placed"), Grid->ServerApplyEdit(FIntVector(8, 8, 6), TEXT("Stone")));
		TestTrue(TEXT("Neighbour placed"), Grid->ServerApplyEdit(FIntVector(9, 8, 6), TEXT("Fabric")));
		if (APawn* Pawn = OnServer->GetPawn())
		{
			HoldStill(Pawn);
			Pawn->SetActorLocation(Grid->CellToWorld(FIntVector(8, 8, 6)), false, nullptr, ETeleportType::TeleportPhysics);
			HoldStill(Pawn);
		}
		HoldStill(Mine->GetPawn());
		if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
		{
			const FVector At = Grid->CellToWorld(FIntVector(8, 8, 6));
			Magic->SetAim(At, At);
		}
	});
	WaitUntil(Ctx, TEXT("the server is aiming at the stone"), 5.f, [Key]()
	{
		AGFPlayerState* OnServer = PlayerStateByKey(ServerWorld(), *Key);
		AGFVoxelWorld* Grid = Voxel(ServerWorld());
		UGFMagicComponent* Magic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		return Magic && Grid && FVector::Dist(Magic->GetAimPoint(), Grid->CellToWorld(FIntVector(8, 8, 6))) < 50.f;
	});
	Do(Ctx, []()
	{
		if (AGFPlayerState* Mine = LocalPlayerState(ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr))
		{
			if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
			{
				Magic->SetFieldHeld(true);
			}
		}
	});
	WaitUntil(Ctx, TEXT("the owner receives the aimed cell fraction"), 8.f, [Key, Stone, Neighbour]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *Key);
		AGFPlayerState* Mine = Client ? LocalPlayerState(Client) : nullptr;
		UGFMagicComponent* ServerMagic = OnServer ? OnServer->FindComponentByClass<UGFMagicComponent>() : nullptr;
		UGFMagicComponent* ClientMagic = Mine ? Mine->FindComponentByClass<UGFMagicComponent>() : nullptr;
		if (!ServerMagic || !ClientMagic)
		{
			return false;
		}
		const FGFTargetedCellFraction& ServerRep = ServerMagic->GetTargetedFraction();
		const FGFTargetedCellFraction& ClientRep = ClientMagic->GetTargetedFraction();
		return ServerRep.bActive && ClientRep.bActive
			&& !ServerRep.bTown && !ClientRep.bTown
			&& ServerRep.Cell == Stone && ClientRep.Cell == Stone
			&& ClientRep.Cell != Neighbour
			&& ServerRep.Fraction < 0.97f && ServerRep.Fraction > 0.2f
			&& ClientRep.Fraction < 0.97f && ClientRep.Fraction > 0.2f
			&& FMath::Abs(ClientRep.Fraction - ServerRep.Fraction) < 0.15f
			&& ClientRep.Serial > 0;
	});
	Do(Ctx, []()
	{
		if (AGFPlayerState* Mine = LocalPlayerState(ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr))
		{
			if (UGFMagicComponent* Magic = Mine->FindComponentByClass<UGFMagicComponent>())
			{
				Magic->SetFieldHeld(false);
			}
		}
	});
	Stop(Ctx);
	return true;
}

#endif
