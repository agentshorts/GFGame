// Magic mining against a bare voxel world: the field, the haul, the claw, and Stairwell Call.
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFInventoryComponent.h"
#include "GFMagicActors.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFMagicTables.h"
#include "GFVoxelWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFMagicWorldTest
{
	struct FWorld
	{
		UWorld* World = nullptr;
		AGFVoxelWorld* Voxel = nullptr;
		UGFBlockIntegritySubsystem* Magic = nullptr;

		bool Create(FAutomationTestBase& Test, const TCHAR* Name)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World || !GEngine)
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
			FActorSpawnParameters Params;
			Params.bDeferConstruction = true;
			Voxel = World->SpawnActor<AGFVoxelWorld>(AGFVoxelWorld::StaticClass(), FTransform::Identity, Params);
			if (Voxel)
			{
				Voxel->PlotSizeInBlocks = FIntVector(16, 16, 8);
				Voxel->StarterGroundLayers = 0;
				Voxel->FinishSpawning(FTransform::Identity);
			}
			Magic = World->GetSubsystem<UGFBlockIntegritySubsystem>();
			return Test.TestNotNull(TEXT("Voxel world"), Voxel) && Test.TestNotNull(TEXT("Integrity subsystem"), Magic);
		}

		AGFMagicPlayerState* SpawnDigger(const TCHAR* Key, const FVector& At) const
		{
			AGFMagicPlayerState* PS = World->SpawnActor<AGFMagicPlayerState>();
			if (!PS || !PS->Magic)
			{
				return nullptr;
			}
			PS->PlayerKey = Key;
			PS->Magic->BindAvatar(nullptr);
			PS->Magic->SetManualOrigin(At);
			return PS;
		}

		void Destroy()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}

		~FWorld() { Destroy(); }
	};

	static int32 CountRibbons(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AGFMagicDropRibbon> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	static int32 CountAbilities(const UAbilitySystemComponent* ASC)
	{
		if (!ASC)
		{
			return 0;
		}
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability)
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicWorldFieldTest, "GFGame.MagicMining.World", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMagicWorldFieldTest::RunTest(const FString&)
{
	using namespace GFMagicWorldTest;
	FWorld Harness;
	if (!Harness.Create(*this, TEXT("GFMagicWorld")))
	{
		return false;
	}
	const FIntVector Dirt(2, 2, 2);
	const FIntVector Fabric(3, 2, 2);
	const FIntVector Stone(2, 3, 2);
	const FIntVector Lava(8, 2, 2);
	Harness.Voxel->ServerStamp([&](const FIntVector& Cell) -> FName
	{
		if (Cell == Dirt) return FName(TEXT("Dirt"));
		if (Cell == Fabric) return FName(TEXT("Fabric"));
		if (Cell == Stone) return FName(TEXT("Stone"));
		if (Cell == Lava) return FName(TEXT("Lava"));
		return NAME_None;
	});
	Harness.Magic->NotePlaced(Stone);

	const FVector At = Harness.Voxel->CellToWorld(Dirt);
	AGFMagicPlayerState* PS = Harness.SpawnDigger(TEXT("Digger"), At);
	if (!TestNotNull(TEXT("Digger"), PS))
	{
		return false;
	}
	TestEqual(TEXT("Four mining abilities"), CountAbilities(PS->AbilitySystem), 4);
	PS->FactionId = TEXT("Nope");
	TestFalse(TEXT("An unknown faction cannot toggle"), PS->Magic->AuthorityToggle());
	TestEqual(TEXT("The field stays Spark"), PS->Magic->GetElement(), EGFMagicElement::Spark);

	PS->Magic->SetAim(At, At);
	PS->Magic->SetFieldHeld(true);
	Harness.Magic->Advance(0.5f);
	TestTrue(TEXT("Dirt is gone"), Harness.Voxel->GetBlock(Dirt).IsNone());
	TestEqual(TEXT("The bag took the dirt"), PS->Inventory->GetCount(TEXT("Dirt")), 1);
	TestEqual(TEXT("Fabric stayed"), Harness.Voxel->GetBlock(Fabric), FName(TEXT("Fabric")));
	TestEqual(TEXT("The placed stone stayed"), Harness.Voxel->GetBlock(Stone), FName(TEXT("Stone")));
	TestTrue(TEXT("A ribbon flew home"), CountRibbons(Harness.World) >= 1);
	PS->Magic->SetFieldHeld(false);

	PS->FactionId = TEXT("Rime");
	TestTrue(TEXT("Rime can swap to frost"), PS->Magic->AuthorityToggle());
	TestEqual(TEXT("The field is frost"), PS->Magic->GetElement(), EGFMagicElement::Frost);
	TestEqual(TEXT("The summon is a raptor"), PS->Magic->GetClawForm(), FName(TEXT("Raptor")));
	const FVector LavaAt = Harness.Voxel->CellToWorld(Lava);
	PS->Magic->SetManualOrigin(LavaAt);
	PS->Magic->SetAim(LavaAt, LavaAt);
	PS->Magic->SetFieldHeld(true);
	const int32 DirtBefore = PS->Inventory->GetCount(TEXT("Dirt"));
	Harness.Magic->Advance(0.8f);
	TestEqual(TEXT("Lava quenched to basalt"), Harness.Voxel->GetBlock(Lava), FName(TEXT("Basalt")));
	TestEqual(TEXT("The quench did not pay a drop"), PS->Inventory->GetCount(TEXT("Dirt")), DirtBefore);
	PS->Magic->SetFieldHeld(false);

	PS->FactionId = TEXT("Cinderwrights");
	PS->ChosenPath = EGFPath::Delver;
	PS->Inventory->AddItem(TEXT("SpellPage"), 1);
	PS->Inventory->AddItem(TEXT("Crystal"), 1);
	TestEqual(TEXT("Lava Lance sells"), PS->Magic->AuthorityBuy(TEXT("LavaLance")), FName(TEXT("Ok")));
	TestEqual(TEXT("The page is gone"), PS->Inventory->GetCount(TEXT("SpellPage")), 0);
	TestEqual(TEXT("The crystal is gone"), PS->Inventory->GetCount(TEXT("Crystal")), 0);
	TestEqual(TEXT("Reach grew by 2 m"), PS->Magic->FieldRange(), 1000.f);
	TestTrue(TEXT("Overcharge spends 50"), PS->Magic->AuthorityOvercharge());
	TestTrue(TEXT("The field is overcharged"), PS->Magic->IsOvercharged());
	PS->Magic->TickMagic(8.f);
	TestFalse(TEXT("Eight seconds later it rests"), PS->Magic->IsOvercharged());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicWorldHaulTest, "GFGame.MagicMining.WorldHaul", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMagicWorldHaulTest::RunTest(const FString&)
{
	using namespace GFMagicWorldTest;
	FWorld Harness;
	if (!Harness.Create(*this, TEXT("GFMagicHaul")))
	{
		return false;
	}
	const FIntVector Cell(2, 2, 2);
	const FIntVector Shared(6, 2, 2);
	Harness.Voxel->ServerApplyEdit(Cell, TEXT("Dirt"));
	Harness.Voxel->ServerApplyEdit(Shared, TEXT("Dirt"));
	const FVector At = Harness.Voxel->CellToWorld(Cell);
	AGFMagicPlayerState* PS = Harness.SpawnDigger(TEXT("Hauler"), At);
	if (!TestNotNull(TEXT("Hauler"), PS))
	{
		return false;
	}
	PS->Magic->PocketCap = 0;
	PS->Magic->SetAim(At, At);
	PS->Magic->SetFieldHeld(true);
	Harness.Magic->Advance(0.5f);
	TestTrue(TEXT("The cell broke"), Harness.Voxel->GetBlock(Cell).IsNone());
	TestEqual(TEXT("A full pocket does not take the drop"), PS->Inventory->GetCount(TEXT("Dirt")), 0);
	TestEqual(TEXT("The haul is holding it"), PS->Magic->HaulCount(TEXT("Dirt")), 1);
	TestTrue(TEXT("The ribbon still flew"), CountRibbons(Harness.World) >= 1);
	PS->Magic->SetFieldHeld(false);
	PS->Magic->PocketCap = 999999;
	PS->Magic->TickMagic(0.1f);
	TestEqual(TEXT("The haul flushes when there is room"), PS->Inventory->GetCount(TEXT("Dirt")), 1);
	TestEqual(TEXT("The haul is empty"), PS->Magic->HaulCount(TEXT("Dirt")), 0);

	const FVector ShareAt = Harness.Voxel->CellToWorld(Shared);
	AGFMagicPlayerState* Other = Harness.SpawnDigger(TEXT("Partner"), ShareAt);
	if (!TestNotNull(TEXT("Partner"), Other))
	{
		return false;
	}
	PS->Magic->SetManualOrigin(ShareAt);
	PS->Magic->SetAim(ShareAt, ShareAt);
	PS->Magic->SetFieldHeld(true);
	Other->Magic->SetAim(ShareAt, ShareAt);
	Other->Magic->SetFieldHeld(true);
	const int32 Mine = PS->Inventory->GetCount(TEXT("Dirt"));
	Harness.Magic->Advance(0.5f);
	TestTrue(TEXT("The shared cell broke"), Harness.Voxel->GetBlock(Shared).IsNone());
	TestEqual(TEXT("The first digger got a full drop"), PS->Inventory->GetCount(TEXT("Dirt")), Mine + 1);
	TestEqual(TEXT("The partner got a full drop"), Other->Inventory->GetCount(TEXT("Dirt")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicWorldClawTest, "GFGame.MagicMining.WorldClaw", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMagicWorldClawTest::RunTest(const FString&)
{
	using namespace GFMagicWorldTest;
	FWorld Harness;
	if (!Harness.Create(*this, TEXT("GFMagicClaw")))
	{
		return false;
	}
	for (int32 Step = 0; Step < 6; ++Step)
	{
		Harness.Voxel->ServerApplyEdit(FIntVector(4 + Step, 2, 2), TEXT("Dirt"));
	}
	Harness.Voxel->ServerApplyEdit(FIntVector(4, 3, 2), TEXT("Fabric"));
	const FVector At = Harness.Voxel->CellToWorld(FIntVector(3, 2, 2));
	const FVector Aim = Harness.Voxel->CellToWorld(FIntVector(9, 2, 2));
	AGFMagicPlayerState* PS = Harness.SpawnDigger(TEXT("Caller"), At);
	if (!TestNotNull(TEXT("Caller"), PS))
	{
		return false;
	}
	PS->FactionId = TEXT("Cinderwrights");
	PS->Magic->TickMagic(0.1f);
	TestEqual(TEXT("Cinderwrights call a scythepaw"), PS->Magic->GetClawForm(), FName(TEXT("Scythepaw")));
	TestEqual(TEXT("Two charges"), PS->Magic->GetClawCharges(), 2);
	PS->Magic->SetAim(At, Aim);
	TestTrue(TEXT("The first claw answers"), PS->Magic->AuthorityClaw());
	TestEqual(TEXT("One charge left"), PS->Magic->GetClawCharges(), 1);
	for (int32 Step = 0; Step < 6; ++Step)
	{
		TestTrue(FString::Printf(TEXT("Dirt %d is mined"), Step), Harness.Voxel->GetBlock(FIntVector(4 + Step, 2, 2)).IsNone());
	}
	TestEqual(TEXT("Fabric in the swipe stayed"), Harness.Voxel->GetBlock(FIntVector(4, 3, 2)), FName(TEXT("Fabric")));
	TestEqual(TEXT("Six dirt landed in the bag"), PS->Inventory->GetCount(TEXT("Dirt")), 6);
	TestTrue(TEXT("A second claw answers"), PS->Magic->AuthorityClaw());
	TestFalse(TEXT("The third claw is out of charges"), PS->Magic->AuthorityClaw());
	PS->Magic->TickMagic(6.f);
	TestEqual(TEXT("Six seconds brings one charge back"), PS->Magic->GetClawCharges(), 1);
	int32 Claws = 0;
	for (TActorIterator<AGFMagicClaw> It(Harness.World); It; ++It)
	{
		++Claws;
	}
	TestTrue(TEXT("The summon showed itself"), Claws >= 1);
	return true;
}

#endif
