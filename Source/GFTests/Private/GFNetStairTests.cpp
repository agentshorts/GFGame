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

	struct FMotion
	{
		int32 Mode = -1;
		float Speed = 0.f;
		float Accel = 0.f;
		float MaxSpeed = 0.f;
		float MaxAccel = 0.f;
		float Along = 0.f;
		float Feet = 0.f;
	};

	FMotion Sample(const AGFStairFlight* Flight, const APawn* Pawn)
	{
		FMotion Out;
		Out.Along = AlongX(Flight, Pawn);
		Out.Feet = FeetZ(Pawn);
		const ACharacter* Body = Cast<ACharacter>(Pawn);
		const UCharacterMovementComponent* Move = Body ? Body->GetCharacterMovement() : nullptr;
		if (Move)
		{
			Out.Mode = static_cast<int32>(Move->MovementMode);
			Out.Speed = Move->Velocity.Size2D();
			Out.Accel = Move->GetCurrentAcceleration().Size();
			Out.MaxSpeed = Move->GetMaxSpeed();
			Out.MaxAccel = Move->GetMaxAcceleration();
		}
		return Out;
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
				Move->bIgnoreClientMovementErrorChecksAndCorrection = false;
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
				Move->BrakingDecelerationWalking = 2048.f;
				if (Move->MaxAcceleration <= KINDA_SMALL_NUMBER)
				{
					Move->MaxAcceleration = 2048.f;
				}
				// A teleport can leave the mode at None. None does not walk.
				if (Move->MovementMode == MOVE_None)
				{
					Move->SetMovementMode(MOVE_Walking);
				}
				Move->bJustTeleported = true;
				// The place and the client's last move from the hub race on a cold net driver.
				Move->bIgnoreClientMovementErrorChecksAndCorrection = true;
			}
		}
	}

	void Drive(APawn* Pawn, float Dir)
	{
		if (!Pawn)
		{
			return;
		}
		if (ACharacter* Body = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
			{
				if (Move->MovementMode == MOVE_None)
				{
					Move->SetMovementMode(MOVE_Walking);
				}
				if (Move->MaxWalkSpeed < 100.f)
				{
					Move->MaxWalkSpeed = 300.f;
				}
				if (Move->MaxAcceleration <= KINDA_SMALL_NUMBER)
				{
					Move->MaxAcceleration = 2048.f;
				}
			}
		}
		Pawn->AddMovementInput(FVector(Dir, 0.f, 0.f), 1.f);
	}

	void PlaceParty()
	{
		using namespace GFNetTest;
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds().Num() > 0 ? ClientWorlds()[0] : nullptr;
		AGFStairFlight* ServerFlight = FindFlight(Server);
		AGFStairFlight* ClientFlight = FindFlight(Client);
		APawn* Host = Server && LocalPlayerState(Server) ? LocalPlayerState(Server)->GetPawn() : nullptr;
		APawn* ClientLocal = Client && LocalPlayerState(Client) ? LocalPlayerState(Client)->GetPawn() : nullptr;
		const AGFPlayerState* ClientState = Client ? LocalPlayerState(Client) : nullptr;
		const AGFPlayerState* ClientOnServerState = ClientState && Server ? PlayerStateByKey(Server, ClientState->PlayerKey) : nullptr;
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
	Do(Ctx, []() { PlaceParty(); });
	// Let the teleport win over the client's last hub move before anyone walks. A cold net driver loses that race inside 20 s.
	Wait(Ctx, 1.f);
	Do(Ctx, []() { PlaceParty(); });
	WaitUntil(Ctx, TEXT("host and client stand on the lower pad"), 15.f, []()
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
		if (!ServerFlight)
		{
			return false;
		}
		const float Pad = ServerFlight->GetActorLocation().Z;
		// Along 40 is still the bottom of the flight. The landing is the run plus a margin.
		const bool bSettled = OnFlat(ServerFlight, Host, 40.f, Pad, false)
			&& OnFlat(ServerFlight, ClientOnServer, 40.f, Pad, false)
			&& OnFlat(ClientFlight, ClientLocal, 40.f, Pad, false);
		if (!bSettled)
		{
			PlaceParty();
		}
		return bSettled;
	});
	TSharedRef<int32> UpSamples = MakeShared<int32>(0);
	WaitUntil(Ctx, TEXT("host and client reach the landing"), 40.f, [Dir, UpSamples]()
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
		const float Signed = static_cast<float>(*Dir);
		Drive(Host, Signed);
		Drive(ClientLocal, Signed);
		if (!ServerFlight)
		{
			return false;
		}
		if (*UpSamples < 6)
		{
			const FMotion HostMotion = Sample(ServerFlight, Host);
			const FMotion ClientMotion = Sample(ClientFlight, ClientLocal);
			const FMotion ServerClientMotion = Sample(ServerFlight, ClientOnServer);
			UE_LOG(LogTemp, Display, TEXT("NetStair up %d host mode %d speed %.0f accel %.0f max %.0f maxAccel %.0f along %.0f feet %.0f | client mode %d speed %.0f accel %.0f max %.0f maxAccel %.0f along %.0f feet %.0f | serverClient mode %d speed %.0f along %.0f feet %.0f"),
				*UpSamples,
				HostMotion.Mode, HostMotion.Speed, HostMotion.Accel, HostMotion.MaxSpeed, HostMotion.MaxAccel, HostMotion.Along, HostMotion.Feet,
				ClientMotion.Mode, ClientMotion.Speed, ClientMotion.Accel, ClientMotion.MaxSpeed, ClientMotion.MaxAccel, ClientMotion.Along, ClientMotion.Feet,
				ServerClientMotion.Mode, ServerClientMotion.Speed, ServerClientMotion.Along, ServerClientMotion.Feet);
		}
		++*UpSamples;
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
	TSharedRef<int32> DownSamples = MakeShared<int32>(0);
	WaitUntil(Ctx, TEXT("host and client walk back down"), 40.f, [Dir, DownSamples]()
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
		const float Signed = static_cast<float>(*Dir);
		Drive(Host, Signed);
		Drive(ClientLocal, Signed);
		if (!ServerFlight)
		{
			return false;
		}
		if (*DownSamples < 4)
		{
			const FMotion HostMotion = Sample(ServerFlight, Host);
			const FMotion ClientMotion = Sample(ClientFlight, ClientLocal);
			UE_LOG(LogTemp, Display, TEXT("NetStair down %d host mode %d speed %.0f accel %.0f max %.0f maxAccel %.0f along %.0f feet %.0f | client mode %d speed %.0f accel %.0f max %.0f along %.0f feet %.0f"),
				*DownSamples,
				HostMotion.Mode, HostMotion.Speed, HostMotion.Accel, HostMotion.MaxSpeed, HostMotion.MaxAccel, HostMotion.Along, HostMotion.Feet,
				ClientMotion.Mode, ClientMotion.Speed, ClientMotion.Accel, ClientMotion.MaxSpeed, ClientMotion.Along, ClientMotion.Feet);
		}
		++*DownSamples;
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
