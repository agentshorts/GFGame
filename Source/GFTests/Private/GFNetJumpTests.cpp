// The player can jump where the game starts (L_World_Factions_A), in each town, and on the hub.
// Each map runs in PIE as a listen server with one player, so every startup director and placed actor is there.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Jump;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFCharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetJumpDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A jump must lift the feet at least this far. A normal hop clears well over a metre. */
	constexpr float MinRiseCm = 40.f;

	ACharacter* LocalBody(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				return Cast<ACharacter>(PC->GetPawn());
			}
		}
		return nullptr;
	}

	FString Describe(const ACharacter* Body)
	{
		if (!Body)
		{
			return TEXT("no pawn");
		}
		const UCharacterMovementComponent* Move = Body->GetCharacterMovement();
		const UGFCharacterMovementComponent* GFMove = Cast<UGFCharacterMovementComponent>(Move);
		FString Floor = TEXT("none");
		if (Move && Move->CurrentFloor.HitResult.GetActor())
		{
			Floor = FString::Printf(TEXT("%s/%s walkable=%d dist=%.1f"),
				*Move->CurrentFloor.HitResult.GetActor()->GetName(),
				Move->CurrentFloor.HitResult.GetComponent() ? *Move->CurrentFloor.HitResult.GetComponent()->GetName() : TEXT("?"),
				Move->CurrentFloor.bWalkableFloor ? 1 : 0,
				Move->CurrentFloor.FloorDist);
		}
		return FString::Printf(TEXT("at %s mode=%d grounded=%d stamina=%d (%.0f) jumpMax=%d jumpCount=%d zVel=%.0f floor=%s"),
			*Body->GetActorLocation().ToCompactString(),
			Move ? static_cast<int32>(Move->MovementMode.GetValue()) : -1,
			Move && Move->IsMovingOnGround() ? 1 : 0,
			GFMove && GFMove->IsStaminaEnabled() ? 1 : 0,
			GFMove ? GFMove->GetStamina() : 0.f,
			Body->JumpMaxCount,
			Body->JumpCurrentCount,
			Move ? Move->JumpZVelocity : 0.f,
			*Floor);
	}

	/** Loads Map, lands the player, presses jump once and expects the feet to leave the ground. */
	void RunJump(FAutomationTestBase* Test, const TCHAR* Map)
	{
		using namespace GFNetTest;
		FCtxRef Ctx = StartOnMap(Test, Map, 1, 120.f);
		TSharedRef<float> StartZ = MakeShared<float>(0.f);
		TSharedRef<float> PeakZ = MakeShared<float>(0.f);
		WaitUntil(Ctx, TEXT("player standing on the ground"), 20.f, []()
		{
			const ACharacter* Body = LocalBody(ServerWorld());
			return Body && Body->GetCharacterMovement() && Body->GetCharacterMovement()->IsMovingOnGround();
		});
		Wait(Ctx, 0.5f);
		Do(Ctx, [Ctx, StartZ, PeakZ]()
		{
			ACharacter* Body = LocalBody(ServerWorld());
			if (!Body)
			{
				Ctx->Test->AddError(TEXT("No local pawn"));
				Ctx->bFailed = true;
				return;
			}
			Ctx->Test->AddInfo(FString::Printf(TEXT("Before jump: %s"), *Describe(Body)));
			*StartZ = Body->GetActorLocation().Z;
			*PeakZ = *StartZ;
			Body->Jump();
		});
		WaitUntil(Ctx, TEXT("the jump lifts the player"), 2.f, [StartZ, PeakZ]()
		{
			ACharacter* Body = LocalBody(ServerWorld());
			if (!Body)
			{
				return false;
			}
			*PeakZ = FMath::Max(*PeakZ, static_cast<float>(Body->GetActorLocation().Z));
			return *PeakZ - *StartZ >= MinRiseCm;
		});
		Do(Ctx, [Ctx, StartZ, PeakZ]()
		{
			ACharacter* Body = LocalBody(ServerWorld());
			Ctx->Test->AddInfo(FString::Printf(TEXT("Rise %.0f cm. After jump: %s"), *PeakZ - *StartZ, *Describe(Body)));
			if (Body)
			{
				Body->StopJumping();
			}
		});
		Stop(Ctx);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpWorldTest, "GFGame.Net.Jump.World", GFNetJumpDetail::Flags)
bool FGFNetJumpWorldTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_World_Factions_A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpTownVerdantTest, "GFGame.Net.Jump.TownVerdant", GFNetJumpDetail::Flags)
bool FGFNetJumpTownVerdantTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_Town_Verdant_A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpTownRimeTest, "GFGame.Net.Jump.TownRime", GFNetJumpDetail::Flags)
bool FGFNetJumpTownRimeTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_Town_Rime_A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpTownCinderTest, "GFGame.Net.Jump.TownCinderwright", GFNetJumpDetail::Flags)
bool FGFNetJumpTownCinderTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_Town_Cinderwright_A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpHomesteadTest, "GFGame.Net.Jump.Homestead", GFNetJumpDetail::Flags)
bool FGFNetJumpHomesteadTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_Homestead_Verdant_A"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJumpHubTest, "GFGame.Net.Jump.Hub", GFNetJumpDetail::Flags)
bool FGFNetJumpHubTest::RunTest(const FString&)
{
	GFNetJumpDetail::RunJump(this, TEXT("/Game/Maps/L_Hub"));
	return true;
}

#endif
