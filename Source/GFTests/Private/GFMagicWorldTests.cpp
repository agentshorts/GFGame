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
#include "GFMagicShapes.h"
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

		bool Create(FAutomationTestBase& Test, const TCHAR* Name, FIntVector Plot = FIntVector(16, 16, 8))
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
				Voxel->PlotSizeInBlocks = Plot;
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
	PS->Magic->TickMagic(0.1f);
	TestEqual(TEXT("Cinderwrights take magma"), PS->Magic->GetElement(), EGFMagicElement::Magma);
	TestEqual(TEXT("Magma spray reaches 10 m"), PS->Magic->FieldRange(), 1000.f);
	PS->ChosenPath = EGFPath::Delver;
	PS->Inventory->AddItem(TEXT("SpellPage"), 1);
	PS->Inventory->AddItem(TEXT("Crystal"), 1);
	TestEqual(TEXT("Sticky Magma sells"), PS->Magic->AuthorityBuy(TEXT("StickyMagma")), FName(TEXT("Ok")));
	TestEqual(TEXT("The page is gone"), PS->Inventory->GetCount(TEXT("SpellPage")), 0);
	TestEqual(TEXT("The crystal is gone"), PS->Inventory->GetCount(TEXT("Crystal")), 0);
	TestTrue(TEXT("Sticky Magma sticks"), PS->Magic->Modifiers().bSticky);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMagicWorldShapesTest, "GFGame.MagicMining.WorldShapes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFMagicWorldShapesTest::RunTest(const FString&)
{
	using namespace GFMagicWorldTest;
	FWorld Harness;
	// The shared harness is 16 cells across. This pass places rows out to x = 40.
	if (!Harness.Create(*this, TEXT("GFMagicShapes"), FIntVector(48, 16, 8)))
	{
		return false;
	}

	auto Center = [&Harness](const FIntVector& Cell)
	{
		return Harness.Voxel->CellToWorld(Cell) + FVector(0.f, 0.f, 50.f);
	};

	Harness.Voxel->ServerApplyEdit(FIntVector(3, 0, 2), TEXT("Dirt"));
	Harness.Voxel->ServerApplyEdit(FIntVector(4, 0, 2), TEXT("Dirt"));
	Harness.Voxel->ServerApplyEdit(FIntVector(5, 0, 2), TEXT("Dirt"));
	Harness.Voxel->ServerApplyEdit(FIntVector(3, 2, 2), TEXT("Dirt"));
	AGFMagicPlayerState* Cone = Harness.SpawnDigger(TEXT("Cone"), Center(FIntVector(0, 0, 2)));
	if (!TestNotNull(TEXT("Cone caster"), Cone))
	{
		return false;
	}
	Cone->Magic->SetAim(Center(FIntVector(0, 0, 2)), Center(FIntVector(0, 0, 2)) + FVector(600.f, 0.f, 0.f));
	Cone->Magic->SetFieldHeld(true);
	Harness.Magic->Advance(0.6f);
	TestTrue(TEXT("The cone clears the first solid"), Harness.Voxel->GetBlock(FIntVector(3, 0, 2)).IsNone());
	TestTrue(TEXT("Penetration clears the cell behind it"), Harness.Voxel->GetBlock(FIntVector(4, 0, 2)).IsNone());
	TestEqual(TEXT("The cell past penetration stays"), Harness.Voxel->GetBlock(FIntVector(5, 0, 2)), FName(TEXT("Dirt")));
	TestEqual(TEXT("A cell outside the cone stays"), Harness.Voxel->GetBlock(FIntVector(3, 2, 2)), FName(TEXT("Dirt")));
	Cone->Magic->SetFieldHeld(false);

	const FVector Feet = Harness.Voxel->CellToWorld(FIntVector(20, 2, 2));
	const FVector SwingAim = Feet + FVector(250.f, 0.f, 0.f);
	AGFMagicPlayerState* Maul = Harness.SpawnDigger(TEXT("Maul"), Feet);
	if (!TestNotNull(TEXT("Maul"), Maul))
	{
		return false;
	}
	Maul->FactionId = TEXT("Verdant");
	TestTrue(TEXT("Verdant takes the club"), Maul->Magic->AuthorityToggle());
	const FIntVector HitFeet = Harness.Voxel->WorldToCell(Feet + FVector(50.f, 0.f, 0.f));
	const TArray<FIntVector> Swing = FGFMagicShapes::MeleeCells(HitFeet, FVector::ForwardVector, 3, 2, 1);
	if (!TestTrue(TEXT("The swing has a cell"), Swing.Num() > 0))
	{
		return false;
	}
	Harness.Voxel->ServerApplyEdit(Swing[0], TEXT("Dirt"));
	Harness.Voxel->ServerApplyEdit(HitFeet, TEXT("Dirt"));
	Maul->Magic->SetAim(Feet, SwingAim);
	Maul->Magic->SetFieldHeld(true);
	Harness.Magic->Advance(0.2f);
	Maul->Magic->SetFieldHeld(false);
	Harness.Magic->Advance(0.1f);
	TestTrue(TEXT("The swing breaks the cell in front"), Harness.Voxel->GetBlock(Swing[0]).IsNone());
	if (!Swing.Contains(HitFeet))
	{
		TestEqual(TEXT("The feet cell was not in the swing"), Harness.Voxel->GetBlock(HitFeet), FName(TEXT("Dirt")));
	}

	for (int32 Z = 1; Z <= 3; ++Z)
	{
		Harness.Voxel->ServerApplyEdit(FIntVector(10, 0, Z), TEXT("Dirt"));
	}
	Harness.Voxel->ServerApplyEdit(FIntVector(12, 0, 3), TEXT("Dirt"));
	const FVector SprayAt = Center(FIntVector(10, 0, 4));
	AGFMagicPlayerState* Spray = Harness.SpawnDigger(TEXT("Spray"), SprayAt);
	if (!TestNotNull(TEXT("Spray"), Spray))
	{
		return false;
	}
	Spray->FactionId = TEXT("Cinderwrights");
	TestTrue(TEXT("Cinderwrights take magma"), Spray->Magic->AuthorityToggle());
	TestEqual(TEXT("The spray is magma"), Spray->Magic->GetElement(), EGFMagicElement::Magma);
	Spray->Magic->SetAim(SprayAt, SprayAt + FVector(0.f, 0.f, -400.f));
	Spray->Magic->SetFieldHeld(true);
	Harness.Magic->Advance(2.8f);
	TestTrue(TEXT("The top of the stack dissolved"), Harness.Voxel->GetBlock(FIntVector(10, 0, 3)).IsNone());
	TestTrue(TEXT("The puddle dissolved the cell under it"), Harness.Voxel->GetBlock(FIntVector(10, 0, 2)).IsNone());
	TestTrue(TEXT("The puddle kept sinking"), Harness.Voxel->GetBlock(FIntVector(10, 0, 1)).IsNone());
	TestEqual(TEXT("A cell off the stream stays"), Harness.Voxel->GetBlock(FIntVector(12, 0, 3)), FName(TEXT("Dirt")));
	TestNotEqual(TEXT("The spray never writes lava"), Harness.Voxel->GetBlock(FIntVector(10, 0, 2)), FName(TEXT("Lava")));
	Spray->Magic->SetFieldHeld(false);

	for (int32 Step = 0; Step < 7; ++Step)
	{
		Harness.Voxel->ServerApplyEdit(FIntVector(30 + Step, 2, 2), TEXT("Ice"));
	}
	// Lava is not placeable, so ServerApplyEdit refuses it and the cell stays empty.
	TestTrue(TEXT("Lava is authored into the cell"), Harness.Voxel->ServerAuthorCell(FIntVector(40, 2, 2), TEXT("Lava")));
	const FVector FrostAt = Harness.Voxel->CellToWorld(FIntVector(30, 2, 2));
	AGFMagicPlayerState* Frost = Harness.SpawnDigger(TEXT("Frost"), FrostAt);
	if (!TestNotNull(TEXT("Frost"), Frost))
	{
		return false;
	}
	Frost->FactionId = TEXT("Rime");
	TestTrue(TEXT("Rime takes frost"), Frost->Magic->AuthorityToggle());
	TestEqual(TEXT("Rime takes frostbind"), Frost->Magic->GetElement(), EGFMagicElement::Frost);
	const FVector LavaAt = Harness.Voxel->CellToWorld(FIntVector(40, 2, 2));
	Frost->Magic->SetManualOrigin(LavaAt);
	Frost->Magic->SetAim(LavaAt, LavaAt);
	Frost->Magic->SetFieldHeld(true);
	const int32 DirtBefore = Frost->Inventory->GetCount(TEXT("Dirt"));
	Harness.Magic->Advance(0.2f);
	TestEqual(TEXT("The beam quenches lava at once"), Harness.Voxel->GetBlock(FIntVector(40, 2, 2)), FName(TEXT("Basalt")));
	TestEqual(TEXT("The quench still pays nothing"), Frost->Inventory->GetCount(TEXT("Dirt")), DirtBefore);
	Frost->Magic->SetManualOrigin(FrostAt);
	Frost->Magic->SetAim(FrostAt, FrostAt);
	Harness.Magic->Advance(7.f);
	TestTrue(TEXT("The aimed ice shattered"), Harness.Voxel->GetBlock(FIntVector(30, 2, 2)).IsNone());
	TestTrue(TEXT("The chain reached four cells out"), Harness.Voxel->GetBlock(FIntVector(34, 2, 2)).IsNone());
	TestEqual(TEXT("The cell past the generation cap stays"), Harness.Voxel->GetBlock(FIntVector(36, 2, 2)), FName(TEXT("Ice")));
	return true;
}

#endif
