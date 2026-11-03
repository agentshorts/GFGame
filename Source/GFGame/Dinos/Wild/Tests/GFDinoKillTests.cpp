// Wild dinos die, pay Hide or Scales once, and free their spawn slot.
// Tamed and rented bodies do not. Skin rolls are a seeded 30 percent.

#include "Misc/AutomationTest.h"
#include "GFDino.h"
#include "GFDinoKill.h"
#include "GFDataTables.h"
#include "GFEconomy.h"
#include "GFRewards.h"
#include "GFWildDinoDirector.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFDinoKillTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static int32 CountOf(const TArray<FGFItemStack>& Stacks, FName ItemId)
	{
		return FGFRewards::CountOf(Stacks, ItemId);
	}

	struct FKillListen
	{
		int32 Count = 0;
		FDelegateHandle Handle;

		FKillListen()
		{
			Handle = FGFDinoKill::OnDinoKilled().AddLambda([this](AGFDino*, const FString&, FName)
			{
				++Count;
			});
		}

		~FKillListen()
		{
			FGFDinoKill::OnDinoKilled().Remove(Handle);
		}
	};

	struct FKillWorld
	{
		UWorld* World = nullptr;

		~FKillWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		bool Open(FAutomationTestBase& T, const TCHAR* Name)
		{
			if (!T.TestNotNull(TEXT("Engine"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!T.TestNotNull(TEXT("World"), World))
			{
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			return true;
		}
	};

	static AGFDino* SpawnQuiet(UWorld* World, const FVector& At)
	{
		FActorSpawnParameters Deferred;
		Deferred.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Deferred.bDeferConstruction = true;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), At, FRotator::ZeroRotator, Deferred);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->AutoPossessAI = EAutoPossessAI::Disabled;
		Dino->FinishSpawning(FTransform(FRotator::ZeroRotator, At));
		if (UCharacterMovementComponent* Move = Dino->GetCharacterMovement())
		{
			Move->DisableMovement();
			Move->GravityScale = 0.f;
		}
		return Dino;
	}

	static int32 CountAlive(UWorld* World, AGFDino** OutFirst)
	{
		int32 Alive = 0;
		if (OutFirst)
		{
			*OutFirst = nullptr;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->IsActorBeingDestroyed() || It->GetLife() != EGFDinoLife::Alive)
			{
				continue;
			}
			if (OutFirst && *OutFirst == nullptr)
			{
				*OutFirst = *It;
			}
			++Alive;
		}
		return Alive;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDinoKillDropsTest, "GFGame.Dino.Kill.Drops", GFDinoKillTests::Flags)
bool FGFDinoKillDropsTest::RunTest(const FString&)
{
	using namespace GFDinoKillTests;

	TestEqual(TEXT("Default hit points"), AGFDino::HitPointsFor(TEXT("Raptor")), 60.f);
	TestEqual(TEXT("Hornback uses the species default"), AGFDino::HitPointsFor(TEXT("Hornback")), 60.f);
	TestEqual(TEXT("Unknown species uses the default"), AGFDino::HitPointsFor(TEXT("NoSuchDino")), 60.f);
	TestEqual(TEXT("Skin chance"), FGFDinoKill::SkinChance, 0.30f);

	const TCHAR* HideSpecies[] = { TEXT("Tusksaw"), TEXT("Hornback"), TEXT("Lanternmaw") };
	const TCHAR* ScaleSpecies[] = { TEXT("Nipper"), TEXT("Raptor"), TEXT("Scythepaw") };
	for (const TCHAR* Species : HideSpecies)
	{
		TestEqual(*FString::Printf(TEXT("%s material"), Species), FGFDinoKill::MaterialItem(Species), FName(TEXT("Hide")));
		TestEqual(*FString::Printf(TEXT("%s skin id"), Species), FGFDinoKill::SkinItem(Species), FName(*FString::Printf(TEXT("Skin_%s"), Species)));
	}
	for (const TCHAR* Species : ScaleSpecies)
	{
		TestEqual(*FString::Printf(TEXT("%s material"), Species), FGFDinoKill::MaterialItem(Species), FName(TEXT("Scales")));
		TestEqual(*FString::Printf(TEXT("%s skin id"), Species), FGFDinoKill::SkinItem(Species), FName(*FString::Printf(TEXT("Skin_%s"), Species)));
	}
	TestTrue(TEXT("Unknown species has no table"), FGFDinoKill::MaterialItem(TEXT("Fernback")).IsNone());
	TestTrue(TEXT("Unknown species has no skin"), FGFDinoKill::SkinItem(TEXT("Fernback")).IsNone());

	int32 WithSkin = 0;
	int32 Below = 0;
	float First = -1.f;
	bool bFractionVaries = false;
	for (int32 Seed = 1; Seed <= 200; ++Seed)
	{
		const float Fraction = FGFDinoKill::SkinFraction(Seed);
		TestTrue(TEXT("Skin draw is a fraction"), Fraction >= 0.f && Fraction < 1.f);
		if (First < 0.f)
		{
			First = Fraction;
		}
		else if (!FMath::IsNearlyEqual(Fraction, First))
		{
			bFractionVaries = true;
		}
		TestEqual(TEXT("Same seed, same draw"), FGFDinoKill::SkinFraction(Seed), Fraction);
		const TArray<FGFItemStack> Stacks = FGFDinoKill::Roll(TEXT("Raptor"), Seed);
		const TArray<FGFItemStack> Again = FGFDinoKill::Roll(TEXT("Raptor"), Seed);
		TestEqual(TEXT("Scales are always two"), CountOf(Stacks, TEXT("Scales")), 2);
		TestEqual(TEXT("Hide is not on a Raptor"), CountOf(Stacks, TEXT("Hide")), 0);
		const bool bSkin = Fraction < FGFDinoKill::SkinChance;
		TestEqual(TEXT("Skin follows the seeded draw"), CountOf(Stacks, TEXT("Skin_Raptor")), bSkin ? 1 : 0);
		TestEqual(TEXT("A replay pays the same skin"), CountOf(Again, TEXT("Skin_Raptor")), CountOf(Stacks, TEXT("Skin_Raptor")));
		if (bSkin)
		{
			++Below;
			++WithSkin;
		}
	}
	TestTrue(TEXT("The draw is not a constant"), bFractionVaries);
	TestTrue(TEXT("Some seeds pay a skin"), WithSkin > 0);
	TestTrue(TEXT("Some seeds pay no skin"), WithSkin < 200);
	TestEqual(TEXT("Skin count matches the chance rule"), WithSkin, Below);

	for (const TCHAR* Species : HideSpecies)
	{
		const TArray<FGFItemStack> Stacks = FGFDinoKill::Roll(Species, 3);
		TestEqual(*FString::Printf(TEXT("%s pays two Hide"), Species), CountOf(Stacks, TEXT("Hide")), 2);
		TestEqual(*FString::Printf(TEXT("%s pays no Scales"), Species), CountOf(Stacks, TEXT("Scales")), 0);
	}

	FGFDinoKillMachine Machine;
	Machine.Reset(AGFDino::HitPointsFor(TEXT("Nipper")));
	const FGFDinoKillResult Partial = Machine.ApplyHit(TEXT("Nipper"), 59.f, TEXT("A"), 1, false, false);
	TestEqual(TEXT("Still alive one point short"), static_cast<int32>(Partial.Life), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("One hit point left"), Partial.HitPoints, 1.f);
	TestFalse(TEXT("No payment yet"), Partial.bKilled);

	const FGFDinoKillResult ByA = Machine.ApplyHit(TEXT("Nipper"), 1.f, TEXT("A"), 1, false, false);
	TestTrue(TEXT("The last point downs it"), ByA.bKilled);
	TestEqual(TEXT("Killer is the larger share"), ByA.KillerKey, FString(TEXT("A")));
	TestEqual(TEXT("Nipper pays Scales"), CountOf(ByA.Stacks, TEXT("Scales")), 2);

	FGFDinoKillMachine Shares;
	Shares.Reset(60.f);
	Shares.ApplyHit(TEXT("Tusksaw"), 40.f, TEXT("Scout"), 4, false, false);
	Shares.ApplyHit(TEXT("Tusksaw"), 10.f, TEXT("Scout"), 4, false, false);
	const FGFDinoKillResult Finish = Shares.ApplyHit(TEXT("Tusksaw"), 20.f, TEXT("Striker"), 4, false, false);
	TestEqual(TEXT("Largest share is the killer, not the last swing"), Finish.KillerKey, FString(TEXT("Scout")));
	TestEqual(TEXT("Tusksaw pays Hide"), CountOf(Finish.Stacks, TEXT("Hide")), 2);

	FGFDinoKillMachine Tie;
	Tie.Reset(60.f);
	Tie.ApplyHit(TEXT("Hornback"), 30.f, TEXT("A"), 2, false, false);
	const FGFDinoKillResult Tied = Tie.ApplyHit(TEXT("Hornback"), 30.f, TEXT("B"), 2, false, false);
	TestEqual(TEXT("A tied share goes to the killing blow"), Tied.KillerKey, FString(TEXT("B")));

	FGFDinoKillMachine Unknown;
	Unknown.Reset(60.f);
	const FGFDinoKillResult NoTable = Unknown.ApplyHit(TEXT("Fernback"), 60.f, TEXT("A"), 1, false, false);
	TestTrue(TEXT("A species with no table still dies"), NoTable.bKilled);
	TestEqual(TEXT("And pays nothing"), NoTable.Stacks.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDinoKillOnceTest, "GFGame.Dino.Kill.OncePerDeath", GFDinoKillTests::Flags)
bool FGFDinoKillOnceTest::RunTest(const FString&)
{
	using namespace GFDinoKillTests;

	TestEqual(TEXT("Alive with hit points stays alive"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Alive, 10.f, 0.f, true)), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("Alive at zero hit points is downed"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Alive, 0.f, 0.f, true)), static_cast<int32>(EGFDinoLife::Downed));
	TestEqual(TEXT("Downed waits out the delay"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Downed, 0.f, FGFDinoKill::DownedSeconds - 0.01f, true)), static_cast<int32>(EGFDinoLife::Downed));
	TestEqual(TEXT("Downed becomes dead when the delay ends"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Downed, 0.f, FGFDinoKill::DownedSeconds, true)), static_cast<int32>(EGFDinoLife::Dead));
	TestEqual(TEXT("Dead stays dead"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Dead, 0.f, 100.f, true)), static_cast<int32>(EGFDinoLife::Dead));
	TestEqual(TEXT("A dino that cannot die stays alive"), static_cast<int32>(FGFDinoKill::NextLife(EGFDinoLife::Alive, 0.f, 0.f, false)), static_cast<int32>(EGFDinoLife::Alive));

	FGFDinoKillMachine Machine;
	Machine.Reset(60.f);
	const FGFDinoKillResult First = Machine.ApplyHit(TEXT("Raptor"), 100.f, TEXT("A"), 9, false, false);
	const FGFDinoKillResult Second = Machine.ApplyHit(TEXT("Raptor"), 100.f, TEXT("B"), 9, false, false);
	const FGFDinoKillResult Wait = Machine.Advance(FGFDinoKill::DownedSeconds - 0.05f);
	const FGFDinoKillResult Dead = Machine.Advance(0.1f);
	const FGFDinoKillResult After = Machine.ApplyHit(TEXT("Raptor"), 100.f, TEXT("C"), 11, false, false);
	TestTrue(TEXT("The first killing hit pays"), First.bKilled && CountOf(First.Stacks, TEXT("Scales")) == 2);
	TestFalse(TEXT("A second hit on the body pays nothing"), Second.bKilled || Second.Stacks.Num() > 0);
	TestFalse(TEXT("Waiting out the delay pays nothing"), Wait.bKilled || Wait.bNowDead || Wait.Stacks.Num() > 0);
	TestTrue(TEXT("The delay ends in Dead"), Dead.bNowDead && !Dead.bKilled && Dead.Stacks.Num() == 0);
	TestEqual(TEXT("Dead stays dead after another hit"), static_cast<int32>(After.Life), static_cast<int32>(EGFDinoLife::Dead));
	TestFalse(TEXT("Dead does not pay again"), After.bKilled || After.Stacks.Num() > 0);
	TestTrue(TEXT("The machine remembers that it paid"), Machine.bPaid);

	FKillWorld Scope;
	if (!Scope.Open(*this, TEXT("GFDinoKillOnce")))
	{
		return false;
	}
	AGFDino* Dino = SpawnQuiet(Scope.World, FVector(0.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("Dino"), Dino))
	{
		return false;
	}
	Dino->SetSpecies(TEXT("Raptor"));
	Dino->SetDropSeed(9);
	FKillListen Listen;
	const float FirstHit = UGameplayStatics::ApplyDamage(Dino, 100.f, nullptr, nullptr, nullptr);
	const float SecondHit = UGameplayStatics::ApplyDamage(Dino, 100.f, nullptr, nullptr, nullptr);
	TestTrue(TEXT("The body accepted the killing hit"), FirstHit > 0.f);
	TestEqual(TEXT("A repeat hit removes nothing"), SecondHit, 0.f);
	TestEqual(TEXT("The body is downed"), static_cast<int32>(Dino->GetLife()), static_cast<int32>(EGFDinoLife::Downed));
	TestEqual(TEXT("Hit points are spent"), Dino->GetHitPoints(), 0.f);
	TestEqual(TEXT("OnDinoKilled fired once"), Listen.Count, 1);
	Dino->Tick(FGFDinoKill::DownedSeconds);
	TestEqual(TEXT("Destroying the body does not pay again"), Listen.Count, 1);
	TestTrue(TEXT("The body is gone after the delay"), !IsValid(Dino) || Dino->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDinoKillTamedTest, "GFGame.Dino.Kill.TamedNoDrop", GFDinoKillTests::Flags)
bool FGFDinoKillTamedTest::RunTest(const FString&)
{
	using namespace GFDinoKillTests;

	TestFalse(TEXT("Tamed cannot die"), FGFDinoKill::CanDie(true, false));
	TestFalse(TEXT("Rented cannot die"), FGFDinoKill::CanDie(false, true));
	TestTrue(TEXT("A wild dino can die"), FGFDinoKill::CanDie(false, false));

	FGFDinoKillMachine Tamed;
	Tamed.Reset(60.f);
	const FGFDinoKillResult TameHit = Tamed.ApplyHit(TEXT("Raptor"), 1000.f, TEXT("A"), 1, true, false);
	const FGFDinoKillResult TameAgain = Tamed.ApplyHit(TEXT("Raptor"), 1000.f, TEXT("A"), 1, true, false);
	TestEqual(TEXT("Tamed stays alive"), static_cast<int32>(TameHit.Life), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("Tamed keeps its hit points"), TameHit.HitPoints, 60.f);
	TestEqual(TEXT("Tamed hit removes nothing"), TameHit.Applied, 0.f);
	TestFalse(TEXT("Tamed pays nothing"), TameHit.bKilled || TameHit.Stacks.Num() > 0 || TameAgain.bKilled);

	FGFDinoKillMachine Rented;
	Rented.Reset(60.f);
	const FGFDinoKillResult RentHit = Rented.ApplyHit(TEXT("Hornback"), 1000.f, TEXT("A"), 2, false, true);
	TestEqual(TEXT("Rented stays alive"), static_cast<int32>(RentHit.Life), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("Rented keeps its hit points"), RentHit.HitPoints, 60.f);
	TestFalse(TEXT("Rented pays nothing"), RentHit.bKilled || RentHit.Stacks.Num() > 0);
	TestFalse(TEXT("Rented did not record a payment"), Rented.bPaid);

	FKillWorld Scope;
	if (!Scope.Open(*this, TEXT("GFDinoKillTamed")))
	{
		return false;
	}
	AGFDino* Dino = SpawnQuiet(Scope.World, FVector(40.f, 0.f, 100.f));
	if (!TestNotNull(TEXT("Rental body"), Dino))
	{
		return false;
	}
	Dino->SetSpecies(TEXT("Raptor"));
	Dino->SetRental(true);
	FKillListen Listen;
	const float Dealt = UGameplayStatics::ApplyDamage(Dino, 1000.f, nullptr, nullptr, nullptr);
	TestEqual(TEXT("A rented body shrugs the hit off"), Dealt, 0.f);
	TestEqual(TEXT("Rental life stays alive"), static_cast<int32>(Dino->GetLife()), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("Rental hit points stay full"), Dino->GetHitPoints(), AGFDino::HitPointsFor(TEXT("Raptor")));
	TestEqual(TEXT("Rental does not raise OnDinoKilled"), Listen.Count, 0);
	TestTrue(TEXT("The rental is still in the world"), IsValid(Dino) && !Dino->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDinoKillRespawnTest, "GFGame.Dino.Kill.Respawn", GFDinoKillTests::Flags)
bool FGFDinoKillRespawnTest::RunTest(const FString&)
{
	using namespace GFDinoKillTests;

	FKillWorld Scope;
	if (!Scope.Open(*this, TEXT("GFDinoKillRespawn")))
	{
		return false;
	}

	FActorSpawnParameters Deferred;
	Deferred.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Deferred.bDeferConstruction = true;
	AGFWildDinoDirector* Director = Scope.World->SpawnActor<AGFWildDinoDirector>(AGFWildDinoDirector::StaticClass(), FTransform::Identity, Deferred);
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	FGFWildDinoSpawn One;
	One.AnchorId = TEXT("NotAFaction");
	One.Species = TEXT("Raptor");
	One.Count = 1;
	One.Radius = 0.f;
	One.MinRadius = 0.f;
	One.Location = FVector(2500.f, -800.f, 0.f);
	Director->Spawns.Reset();
	Director->Spawns.Add(One);
	Director->MaxAlive = 1;
	Director->RespawnSeconds = 1000.f;
	Director->bSpawnHostile = false;
	Director->FinishSpawning(FTransform::Identity);

	AGFDino* First = nullptr;
	if (!TestEqual(TEXT("One wild dino is up"), CountAlive(Scope.World, &First), 1) || !TestNotNull(TEXT("First body"), First))
	{
		return false;
	}
	const int32 FirstId = First->GetUniqueID();
	TestEqual(TEXT("It is a Raptor"), First->Taming->Species, FName(TEXT("Raptor")));
	TestEqual(TEXT("It starts alive"), static_cast<int32>(First->GetLife()), static_cast<int32>(EGFDinoLife::Alive));
	if (UCharacterMovementComponent* Move = First->GetCharacterMovement())
	{
		Move->DisableMovement();
		Move->GravityScale = 0.f;
	}

	UGameplayStatics::ApplyDamage(First, 1000.f, nullptr, nullptr, nullptr);
	TestEqual(TEXT("The kill downs it"), static_cast<int32>(First->GetLife()), static_cast<int32>(EGFDinoLife::Downed));
	First->Tick(FGFDinoKill::DownedSeconds);
	TestTrue(TEXT("The delay destroys the body"), !IsValid(First) || First->IsActorBeingDestroyed());

	Director->Tick(0.1f);
	AGFDino* Waiting = nullptr;
	TestEqual(TEXT("The slot waits out RespawnSeconds"), CountAlive(Scope.World, &Waiting), 0);

	Director->RespawnSeconds = 0.f;
	Director->Tick(0.1f);
	AGFDino* Next = nullptr;
	TestEqual(TEXT("The slot spawns again once the wait is over"), CountAlive(Scope.World, &Next), 1);
	if (!TestNotNull(TEXT("Replacement"), Next))
	{
		return false;
	}
	TestNotEqual(TEXT("The replacement is a new body"), Next->GetUniqueID(), FirstId);
	TestEqual(TEXT("The replacement is the same species"), Next->Taming->Species, FName(TEXT("Raptor")));
	TestEqual(TEXT("The replacement is alive"), static_cast<int32>(Next->GetLife()), static_cast<int32>(EGFDinoLife::Alive));
	TestEqual(TEXT("The replacement has a full pool"), Next->GetHitPoints(), AGFDino::HitPointsFor(TEXT("Raptor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFDinoSkinItemsTest, "GFGame.Data.Items.Skins", GFDinoKillTests::Flags)
bool FGFDinoSkinItemsTest::RunTest(const FString&)
{
	const UDataTable* Items = FGFDataTables::Get<FGFItemRow>(TEXT("DT_Items"));
	if (!TestNotNull(TEXT("DT_Items"), Items))
	{
		return false;
	}
	for (const FString& Problem : FGFEconomy::ValidateItemTable(Items))
	{
		AddError(Problem);
	}
	const int32 TierValue = FGFEconomy::DefaultValueForTier(3);
	const TCHAR* Skins[] = {
		TEXT("Skin_Nipper"), TEXT("Skin_Raptor"), TEXT("Skin_Tusksaw"),
		TEXT("Skin_Lanternmaw"), TEXT("Skin_Scythepaw"), TEXT("Skin_Hornback")
	};
	for (const TCHAR* Id : Skins)
	{
		const FGFItemRow* Row = FGFEconomy::FindItem(Items, Id);
		if (!TestNotNull(*FString::Printf(TEXT("%s exists"), Id), Row))
		{
			continue;
		}
		const FString What(Id);
		TestEqual(*(What + TEXT(" category")), Row->Category, EGFItemCategory::Material);
		TestEqual(*(What + TEXT(" tier")), Row->Tier, 3);
		TestEqual(*(What + TEXT(" value")), Row->Value, TierValue);
		TestEqual(*(What + TEXT(" material category")), Row->MaterialCategory, EGFMaterialCategory::Trophy);
		TestEqual(*(What + TEXT(" path")), Row->PathTag, EGFPath::Wrangler);
		TestTrue(*(What + TEXT(" is wagerable")), Row->bWagerable);
		TestFalse(*(What + TEXT(" is not rentable")), Row->bRentable);
		TestFalse(*(What + TEXT(" is not account-bound")), Row->bAccountBound);
		TestEqual(*(What + TEXT(" exclusive")), Row->Exclusive, EGFExclusive::None);
		TestTrue(*(What + TEXT(" has no outfit slot")), Row->OutfitSlot.IsNone());
		TestTrue(*(What + TEXT(" has a name")), !Row->DisplayName.IsEmpty());
		TestTrue(*(What + TEXT(" icon matches the rest of the table")), Row->Icon.IsNull());
	}
	return true;
}

#endif
