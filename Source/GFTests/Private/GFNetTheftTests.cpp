// A witnessed crystal break hires a wild dino onto the thief, and the client sees that target.
// A hire is let go when the director goes away, and a hire that goes down leaves its case on the next tick.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Net.Theft;Quit" -unattended -nullrhi

#include "GFNetTestUtils.h"
#include "GFDino.h"
#include "GFDinoKill.h"
#include "GFElderBook.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "GFTheftDirector.h"
#include "GFTheftRules.h"
#include "GFTownNodes.h"
#include "GFTownsfolkRules.h"
#include "GFVillager.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFNetTheft
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	void Plant(AActor* Body, const FVector& At)
	{
		if (!Body)
		{
			return;
		}
		Body->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = Body->FindComponentByClass<UCharacterMovementComponent>())
		{
			Move->GravityScale = 0.f;
			Move->StopMovementImmediately();
		}
	}

	AGFVillager* SpawnFolk(UWorld* World, const FVector& At, FName TownId, EGFTownsfolkRole Role)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFVillager* Folk = World->SpawnActor<AGFVillager>(AGFVillager::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (!Folk)
		{
			return nullptr;
		}
		Plant(Folk, At);
		FGFTownsfolkSpawnParams Spawn;
		Spawn.TownId = TownId;
		Spawn.Role = Role;
		Spawn.TownCenter = FVector::ZeroVector;
		Spawn.TownRadius = 5000.f;
		Spawn.WalkSpeed = 400.f;
		Spawn.StopDistance = 80.f;
		Spawn.IdleMin = 30.f;
		Spawn.IdleMax = 30.f;
		Folk->InitTownsfolk(Spawn);
		Plant(Folk, At);
		return Folk;
	}

	/** Every dino the elder hired onto Key: hostile at Key and holding a hire. */
	TArray<AGFDino*> HiresOn(UWorld* World, const FString& Key)
	{
		TArray<AGFDino*> Out;
		if (!World)
		{
			return Out;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			if (It->Taming && It->Taming->IsHired() && It->Taming->GetTargetKey() == Key && !It->IsActorBeingDestroyed())
			{
				Out.Add(*It);
			}
		}
		return Out;
	}

	bool HireBookPosted(UWorld* World)
	{
		if (const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(World))
		{
			for (const FGFElderPost& Post : Book->GetBook().ChannelPosts(EGFElderChannel::RimeBook))
			{
				if (Post.Kind == EGFElderKind::Hire)
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Server side: a theft director, a Rime villager and elder, one wild Nipper in the ring, and a crystal break by
	 * the host. OutThief is the host's key. False when the stage could not be built.
	 */
	bool StageTheft(FAutomationTestBase& T, FString& OutThief)
	{
		UWorld* World = GFNetTest::ServerWorld();
		const AGFPlayerState* Host = World ? GFNetTest::LocalPlayerState(World) : nullptr;
		if (!T.TestNotNull(TEXT("Server"), World) || !T.TestNotNull(TEXT("Host"), Host))
		{
			return false;
		}
		OutThief = Host->PlayerKey;

		bool bHasDirector = false;
		for (TActorIterator<AGFTheftDirector> It(World); It; ++It)
		{
			bHasDirector = true;
		}
		if (!bHasDirector)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params);
		}

		const FVector Crystal(0.f, 0.f, 500.f);
		AGFVillager* Villager = SpawnFolk(World, FVector(200.f, 0.f, 500.f), TEXT("Rime"), EGFTownsfolkRole::Villager);
		AGFVillager* Elder = SpawnFolk(World, FVector(260.f, 0.f, 500.f), TEXT("Rime"), EGFTownsfolkRole::Elder);
		if (!T.TestNotNull(TEXT("Villager"), Villager) || !T.TestNotNull(TEXT("Elder"), Elder))
		{
			return false;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), FVector(900.f, 0.f, 500.f), FRotator::ZeroRotator, Params);
		if (!T.TestNotNull(TEXT("Dino"), Dino))
		{
			return false;
		}
		Dino->bAlwaysRelevant = true;
		Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, FVector(900.f, 0.f, 500.f)));
		Dino->SetSpecies(TEXT("Nipper"));
		Plant(Dino, FVector(900.f, 0.f, 500.f));

		TArray<FString> Keys;
		Keys.Add(OutThief);
		FGFTownNodeEvents::OnCrystalTaken().Broadcast(World, TEXT("RimeCrystal"), Crystal, Keys);
		return true;
	}

	bool AnyHostileAt(UWorld* World, const FString& Key)
	{
		if (!World)
		{
			return false;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			if (It->Taming && It->Taming->GetTameState() == EGFTameState::Hostile && It->Taming->GetTargetKey() == Key)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTheftHostileOnThiefTest, "GFGame.Net.Theft.HostileOnThief", GFNetTheft::Flags)
bool FGFNetTheftHostileOnThiefTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetTheft;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ThiefKey = MakeShared<FString>();

	Do(Ctx, [this, ThiefKey]()
	{
		StageTheft(*this, *ThiefKey);
	});

	// A wild dino can turn on the host by itself, so also wait for the elder's hire post.
	WaitUntil(Ctx, TEXT("the elder hired and a dino is hostile at the thief on the server and the client"), 20.f, [ThiefKey]()
	{
		return HireBookPosted(ServerWorld())
			&& AnyHostileAt(ServerWorld(), *ThiefKey)
			&& ClientWorlds().Num() > 0
			&& AnyHostileAt(ClientWorlds()[0], *ThiefKey);
	});

	Do(Ctx, [this, ThiefKey]()
	{
		TestTrue(TEXT("Server hire is on the thief"), AnyHostileAt(ServerWorld(), *ThiefKey));
		if (ClientWorlds().Num() > 0)
		{
			TestTrue(TEXT("Client sees the hire"), AnyHostileAt(ClientWorlds()[0], *ThiefKey));
		}
		bool bTheft = false;
		bool bHire = false;
		if (const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(ServerWorld()))
		{
			for (const FGFElderPost& Post : Book->GetBook().ChannelPosts(EGFElderChannel::RimeBook))
			{
				bTheft |= Post.Kind == EGFElderKind::Theft;
				bHire |= Post.Kind == EGFElderKind::Hire;
			}
		}
		TestTrue(TEXT("RimeBook has the theft"), bTheft);
		TestTrue(TEXT("RimeBook has the hire"), bHire);
	});

	Stop(Ctx);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTheftDirectorGoneTest, "GFGame.Net.Theft.DirectorGoneReleasesHire", GFNetTheft::Flags)
bool FGFNetTheftDirectorGoneTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetTheft;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ThiefKey = MakeShared<FString>();
	TSharedRef<TArray<TWeakObjectPtr<AGFDino>>> Hired = MakeShared<TArray<TWeakObjectPtr<AGFDino>>>();

	Do(Ctx, [this, ThiefKey]()
	{
		StageTheft(*this, *ThiefKey);
	});

	WaitUntil(Ctx, TEXT("the elder hired at least one dino onto the thief"), 20.f, [ThiefKey]()
	{
		return HireBookPosted(ServerWorld()) && HiresOn(ServerWorld(), *ThiefKey).Num() > 0;
	});

	// The director goes away mid-hunt while the world keeps running.
	Do(Ctx, [this, ThiefKey, Hired]()
	{
		UWorld* World = ServerWorld();
		for (AGFDino* Dino : HiresOn(World, *ThiefKey))
		{
			Hired->Add(Dino);
		}
		TestTrue(TEXT("A hire is held before the director goes"), Hired->Num() > 0);
		int32 Directors = 0;
		for (TActorIterator<AGFTheftDirector> It(World); It; ++It)
		{
			It->Destroy();
			++Directors;
		}
		TestTrue(TEXT("A director was there to remove"), Directors > 0);
	});

	Wait(Ctx, 0.5f);

	Do(Ctx, [this, ThiefKey, Hired]()
	{
		TestEqual(TEXT("No dino is still hired onto the thief"), HiresOn(ServerWorld(), *ThiefKey).Num(), 0);
		for (const TWeakObjectPtr<AGFDino>& Weak : *Hired)
		{
			AGFDino* Dino = Weak.Get();
			if (!Dino || Dino->IsActorBeingDestroyed() || !Dino->Taming)
			{
				// A hire spawn is despawned with its director. That is the expected end for it.
				continue;
			}
			TestFalse(TEXT("A borrowed dino drops the hire"), Dino->Taming->IsHired());
			Dino->Taming->ServerForceHostile(TEXT("PackCaller"));
			TestEqual(TEXT("A pack call reaches it again"), Dino->Taming->GetTargetKey(), FString(TEXT("PackCaller")));
		}
	});

	Stop(Ctx);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetTheftDownedHireTest, "GFGame.Net.Theft.DownedHireLetGo", GFNetTheft::Flags)
bool FGFNetTheftDownedHireTest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFNetTheft;

	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ThiefKey = MakeShared<FString>();
	TSharedRef<TArray<TWeakObjectPtr<AGFDino>>> Downed = MakeShared<TArray<TWeakObjectPtr<AGFDino>>>();

	Do(Ctx, [this, ThiefKey]()
	{
		StageTheft(*this, *ThiefKey);
	});

	WaitUntil(Ctx, TEXT("the elder hired at least one dino onto the thief"), 20.f, [ThiefKey]()
	{
		return HireBookPosted(ServerWorld()) && HiresOn(ServerWorld(), *ThiefKey).Num() > 0;
	});

	// Knock every hire down in one blow each.
	Do(Ctx, [this, ThiefKey, Downed]()
	{
		for (AGFDino* Dino : HiresOn(ServerWorld(), *ThiefKey))
		{
			Dino->TakeDamage(100000.f, FDamageEvent(), nullptr, nullptr);
			TestEqual(TEXT("The hire is down"), static_cast<int32>(Dino->GetLife()), static_cast<int32>(EGFDinoLife::Downed));
			Downed->Add(Dino);
		}
		TestTrue(TEXT("At least one hire went down"), Downed->Num() > 0);
	});

	// Well inside the 2s down delay, so the body's own destroy has not happened yet.
	WaitUntil(Ctx, TEXT("each downed hire left its case: a borrowed one dropped the hire, a spawn has a lifespan"), 1.5f, [Downed]()
	{
		for (const TWeakObjectPtr<AGFDino>& Weak : *Downed)
		{
			const AGFDino* Dino = Weak.Get();
			if (!Dino || Dino->IsActorBeingDestroyed())
			{
				continue;
			}
			const bool bReleased = Dino->Taming && !Dino->Taming->IsHired();
			const bool bOnTimer = Dino->GetLifeSpan() > 0.f;
			if (!bReleased && !bOnTimer)
			{
				return false;
			}
		}
		return true;
	});

	Do(Ctx, [this, Downed]()
	{
		for (const TWeakObjectPtr<AGFDino>& Weak : *Downed)
		{
			const AGFDino* Dino = Weak.Get();
			if (!Dino || Dino->IsActorBeingDestroyed())
			{
				continue;
			}
			TestTrue(TEXT("A downed hire spawn removes itself within the down delay plus grace"),
				!Dino->Taming || !Dino->Taming->IsHired() || Dino->GetLifeSpan() <= FGFTheftRules::DespawnDelay(EGFDinoLife::Downed));
		}
	});

	Wait(Ctx, FGFTheftRules::DespawnDelay(EGFDinoLife::Downed) + 0.5f);

	Do(Ctx, [this, Downed]()
	{
		int32 Left = 0;
		for (const TWeakObjectPtr<AGFDino>& Weak : *Downed)
		{
			const AGFDino* Dino = Weak.Get();
			Left += (Dino && !Dino->IsActorBeingDestroyed()) ? 1 : 0;
		}
		TestEqual(TEXT("No downed hire body is left behind"), Left, 0);
	});

	Stop(Ctx);
	return true;
}

#endif
