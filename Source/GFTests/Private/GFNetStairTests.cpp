// Host and client walk one hub flight. The local StairClimb test covers every flight and dinos.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.StairClimb;Quit" -unattended -nullrhi
#include "GFNetTestUtils.h"
#include "GFPlayerState.h"
#include "GFStairCatalog.h"
#include "GFStairFlight.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetStairDetail
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	AGFStairFlight* FindFlight(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AGFStairFlight> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	float FeetZ(const APawn* Pawn)
	{
		const ACharacter* Body = Cast<ACharacter>(Pawn);
		const float Half = Body && Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 88.f;
		return Pawn ? Pawn->GetActorLocation().Z - Half : 0.f;
	}

	float AlongX(const AGFStairFlight* Flight, const APawn* Pawn)
	{
		if (!Flight || !Pawn)
		{
			return 0.f;
		}
		return Flight->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()).X;
	}

	void Halt(APawn* Pawn)
	{
		if (!Pawn)
		{
			return;
		}
		Pawn->ConsumeMovementInputVector();
		if (ACharacter* Body = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
			}
		}
	}

	void Place(APawn* Pawn, const FVector& At)
	{
		if (!Pawn)
		{
			return;
		}
		Pawn->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		if (ACharacter* Body = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
			{
				Move->StopMovementImmediately();
				Move->MaxWalkSpeed = 300.f;
				Move->BrakingDecelerationWalking = 4096.f;
			}
		}
	}

	bool OnFlat(const AGFStairFlight* Flight, const APawn* Pawn, float AlongGoal, float FeetGoal, bool bAbove)
	{
		if (!Flight || !Pawn)
		{
			return false;
		}
		const float Along = AlongX(Flight, Pawn);
		const bool bAlong = bAbove ? Along >= AlongGoal : Along <= AlongGoal;
		return bAlong && FMath::Abs(FeetZ(Pawn) - FeetGoal) < 40.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetStairClimbTest, "GFGame.Net.StairClimb", GFNetStairDetail::Flags)
bool FGFNetStairClimbTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetStairDetail;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<int32> Dir = MakeShared<int32>(1);
	Do(Ctx, [this]()
	{
		UWorld* Server = ServerWorld();
		if (!TestNotNull(TEXT("Server world"), Server))
		{
			return;
		}
		FGFStairFlightSpec Spec;
		Spec.Id = TEXT("Hub.Steep");
		Spec.Steps = 5;
		Spec.RiserCm = 25.f;
		Spec.TreadCm = 32.f;
		Spec.WidthCm = 180.f;
		Spec.YawDeg = 0.f;
		Spec.Origin = FVector(6000.f, 6000.f, 300.f);
		TestNotNull(TEXT("Server flight"), GFStairCatalog::SpawnFlight(Server, Spec));
	});
	WaitUntil(Ctx, TEXT("client has the stair ramp"), 15.f, []()
	{
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		const AGFStairFlight* Flight = FindFlight(Client);
		const AGFStairFlight* ServerFlight = FindFlight(ServerWorld());
		return Flight && Flight->GetRamp() && Flight->Spec.Id == FName(TEXT("Hub.Steep")) && Flight->Spec.Steps == 5
			&& ServerFlight && FVector::DistSquared(Flight->GetActorLocation(), ServerFlight->GetActorLocation()) < 4.f;
	});
	Do(Ctx, []()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		AGFStairFlight* ServerFlight = FindFlight(Server);
		AGFStairFlight* ClientFlight = FindFlight(Client);
		APawn* Host = LocalPlayerState(Server) ? LocalPlayerState(Server)->GetPawn() : nullptr;
		APawn* ClientLocal = LocalPlayerState(Client) ? LocalPlayerState(Client)->GetPawn() : nullptr;
		const AGFPlayerState* ClientState = LocalPlayerState(Client);
		const AGFPlayerState* ClientOnServerState = ClientState ? PlayerStateByKey(Server, ClientState->PlayerKey) : nullptr;
		APawn* ClientOnServer = ClientOnServerState ? ClientOnServerState->GetPawn() : nullptr;
		const auto HalfOf = [](const APawn* Pawn) -> float
		{
			const ACharacter* Body = Cast<ACharacter>(Pawn);
			return Body && Body->GetCapsuleComponent() ? Body->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() : 88.f;
		};
		if (ServerFlight && Host)
		{
			Place(Host, ServerFlight->ApproachLocation(HalfOf(Host), -60.f));
		}
		if (ServerFlight && ClientOnServer)
		{
			Place(ClientOnServer, ServerFlight->ApproachLocation(HalfOf(ClientOnServer), 60.f));
		}
		if (ClientFlight && ClientLocal)
		{
			Place(ClientLocal, ClientFlight->ApproachLocation(HalfOf(ClientLocal), 60.f));
		}
	});
	WaitUntil(Ctx, TEXT("host and client reach the landing"), 20.f, [Dir]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFStairFlight* ServerFlight = FindFlight(Server);
		AGFStairFlight* ClientFlight = FindFlight(Client);
		APawn* Host = Server && LocalPlayerState(Server) ? LocalPlayerState(Server)->GetPawn() : nullptr;
		APawn* ClientLocal = Client && LocalPlayerState(Client) ? LocalPlayerState(Client)->GetPawn() : nullptr;
		const AGFPlayerState* ClientState = Client ? LocalPlayerState(Client) : nullptr;
		const AGFPlayerState* ClientOnServerState = ClientState ? PlayerStateByKey(Server, ClientState->PlayerKey) : nullptr;
		APawn* ClientOnServer = ClientOnServerState ? ClientOnServerState->GetPawn() : nullptr;
		if (Host)
		{
			Host->AddMovementInput(FVector(static_cast<float>(*Dir), 0.f, 0.f), 1.f);
		}
		if (ClientLocal)
		{
			ClientLocal->AddMovementInput(FVector(static_cast<float>(*Dir), 0.f, 0.f), 1.f);
		}
		if (!ServerFlight)
		{
			return false;
		}
		const float Top = ServerFlight->GetActorLocation().Z + ServerFlight->Spec.RiseCm();
		const float Goal = ServerFlight->Spec.RunCm() + 40.f;
		const bool bHost = OnFlat(ServerFlight, Host, Goal, Top, true);
		const bool bServerClient = OnFlat(ServerFlight, ClientOnServer, Goal, Top, true);
		const bool bClient = OnFlat(ClientFlight, ClientLocal, Goal, Top, true);
		if (bHost && bServerClient && bClient)
		{
			Halt(Host);
			Halt(ClientLocal);
			Halt(ClientOnServer);
			return true;
		}
		return false;
	});
	Do(Ctx, [Dir]() { *Dir = -1; });
	WaitUntil(Ctx, TEXT("host and client walk back down"), 20.f, [Dir]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFStairFlight* ServerFlight = FindFlight(Server);
		AGFStairFlight* ClientFlight = FindFlight(Client);
		APawn* Host = Server && LocalPlayerState(Server) ? LocalPlayerState(Server)->GetPawn() : nullptr;
		APawn* ClientLocal = Client && LocalPlayerState(Client) ? LocalPlayerState(Client)->GetPawn() : nullptr;
		const AGFPlayerState* ClientState = Client ? LocalPlayerState(Client) : nullptr;
		const AGFPlayerState* ClientOnServerState = ClientState ? PlayerStateByKey(Server, ClientState->PlayerKey) : nullptr;
		APawn* ClientOnServer = ClientOnServerState ? ClientOnServerState->GetPawn() : nullptr;
		if (Host)
		{
			Host->AddMovementInput(FVector(static_cast<float>(*Dir), 0.f, 0.f), 1.f);
		}
		if (ClientLocal)
		{
			ClientLocal->AddMovementInput(FVector(static_cast<float>(*Dir), 0.f, 0.f), 1.f);
		}
		if (!ServerFlight)
		{
			return false;
		}
		const float Pad = ServerFlight->GetActorLocation().Z;
		const bool bHost = OnFlat(ServerFlight, Host, -20.f, Pad, false);
		const bool bServerClient = OnFlat(ServerFlight, ClientOnServer, -20.f, Pad, false);
		const bool bClient = OnFlat(ClientFlight, ClientLocal, -20.f, Pad, false);
		if (bHost && bServerClient && bClient)
		{
			Halt(Host);
			Halt(ClientLocal);
			Halt(ClientOnServer);
			return true;
		}
		return false;
	});
	Stop(Ctx);
	return true;
}

#endif
