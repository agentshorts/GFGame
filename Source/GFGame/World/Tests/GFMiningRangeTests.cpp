// Mining range: one break-and-reset pass per row, dressing v2, the hub portal, and a chunk round-trip.

#include "Misc/AutomationTest.h"
#include "GFDataTables.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMiningRange.h"
#include "GFMiningRangeLayout.h"
#include "GFMiningRangePortal.h"
#include "GFMiniGameCatalog.h"
#include "GFReturnToHub.h"
#include "GFMiniGameGate.h"
#include "GFPlayerState.h"
#include "GFVoxelTypes.h"
#include "GFVoxelWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFMiningRangeTest
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FRangeWorld
	{
		UWorld* World = nullptr;

		explicit FRangeWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FRangeWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FFallbackGuard
	{
		FFallbackGuard() { AGFMiningRange::bTestForceDressingFallback = true; }
		~FFallbackGuard() { AGFMiningRange::bTestForceDressingFallback = false; }
	};

	static AGFGameState* AddGameState(UWorld* World)
	{
		AGFGameState* GS = World->SpawnActor<AGFGameState>();
		if (GS)
		{
			World->SetGameState(GS);
			GS->MaxEditReach = 0.f;
		}
		return GS;
	}

	static AGFPlayerState* AddMiner(UWorld* World, AGFGameState* GS, const TCHAR* Key, bool bHost)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		if (!PS || !PS->Inventory)
		{
			return nullptr;
		}
		PS->PlayerKey = Key;
		PS->SetPlayerId(7);
		PS->bCharacterReady = true;
		PS->bIsHost = bHost;
		GS->AddPlayerState(PS);
		return PS;
	}

	static int32 CountItem(const AGFPlayerState* PS, FName Item)
	{
		return (PS && PS->Inventory) ? PS->Inventory->GetCount(Item) : 0;
	}

	template <typename T>
	static int32 CountOf(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<T> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	bool RunRow(FAutomationTestBase& Test, const TCHAR* RowId)
	{
		const FString WorldName = FString::Printf(TEXT("GFMining_%s"), RowId);
		FRangeWorld W(*WorldName);
		if (!Test.TestNotNull(TEXT("World"), W.World))
		{
			return false;
		}
		AGFGameState* GS = AddGameState(W.World);
		AGFMiningRange* Range = W.World->SpawnActor<AGFMiningRange>();
		AGFPlayerState* PS = (GS && Range) ? AddMiner(W.World, GS, TEXT("Miner"), true) : nullptr;
		if (!Test.TestNotNull(TEXT("Game state"), GS) || !Test.TestNotNull(TEXT("Range"), Range) || !Test.TestNotNull(TEXT("Miner"), PS) || !Test.TestNotNull(TEXT("Voxel"), Range ? Range->GetVoxel() : nullptr))
		{
			return false;
		}
		Range->PollGains();

		const FGFMiningRangeLayout& Layout = Range->GetLayout();
		const int32 Index = Layout.RowIndexOf(RowId);
		if (!Test.TestTrue(TEXT("Row exists"), Index != INDEX_NONE))
		{
			return false;
		}
		AGFVoxelWorld* Voxel = Range->GetVoxel();
		TMap<FName, int32> Before;
		for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
		{
			Before.Add(Stack.ItemId, Stack.Count);
		}

		TMap<FName, int32> Expect;
		int32 Broke = 0;
		int32 Held = 0;
		for (const FGFMiningCell& Cell : Layout.Cells)
		{
			if (Cell.RowIndex != Index)
			{
				continue;
			}
			const FIntVector Grid = Range->ToGrid(Cell.Range);
			Test.TestEqual(*FString::Printf(TEXT("%s starts as %s"), *Cell.Range.ToString(), *Cell.Stamp.ToString()), Voxel->GetBlock(Grid).ToString(), Cell.Stamp.ToString());
			if (!Cell.bBreakable)
			{
				PS->RequestEdit(Grid, NAME_None);
				Test.TestEqual(TEXT("Protected cell stays"), Voxel->GetBlock(Grid).ToString(), Cell.Stamp.ToString());
				++Held;
				continue;
			}
			PS->RequestEdit(Grid, NAME_None);
			Test.TestTrue(*FString::Printf(TEXT("%s breaks"), *Cell.Range.ToString()), Voxel->GetBlock(Grid).IsNone());
			Expect.FindOrAdd(PS->ItemForBlock(Cell.Stamp)) += 1;
			++Broke;
		}
		Test.TestEqual(TEXT("Broke the breakable cells"), Broke, Layout.BreakableCount(Index));

		const FString Id(RowId);
		if (Id == TEXT("Verdant_TreePlot"))
		{
			Test.TestEqual(TEXT("Tree is 61 planks"), Expect.FindRef(FName(TEXT("WoodPlank"))), 61);
			Test.TestEqual(TEXT("Log plus leaves"), Layout.TreeLogs + Layout.TreeLeaves, 61);
			Test.TestEqual(TEXT("Tree sign keeps its line"), Layout.Rows[Index].Sign1, FString(TEXT("TreePlot")));
		}
		else if (Id == TEXT("Common_Fabric") || Id == TEXT("Common_PlayerWall"))
		{
			Test.TestEqual(TEXT("Spell-proof row has nothing to break"), Broke, 0);
			Test.TestTrue(TEXT("The wall is still there"), Held > 0);
		}
		else if (Id == TEXT("Cinderwrights_Lava"))
		{
			Test.TestEqual(TEXT("Lava pool is 30"), Broke, 30);
			Test.TestTrue(TEXT("The lip stays"), Held > 0);
			Test.TestEqual(TEXT("Lava drops stone"), Expect.FindRef(FName(TEXT("Stone"))), 30);
		}
		else if (Id == TEXT("Rime_RakeLane"))
		{
			Test.TestEqual(TEXT("Rake lane is 36"), Broke, 36);
		}
		else if (Id == TEXT("Cinderwrights_SparkStation"))
		{
			Test.TestEqual(TEXT("Spark station is 15"), Broke, 15);
		}
		else
		{
			Test.TestEqual(TEXT("A block row is 30"), Broke, 30);
		}
		if (Id == TEXT("Ore_ClayBed"))
		{
			Test.TestEqual(TEXT("Clay bed stamps as clay"), Layout.Rows[Index].Block.ToString(), FString(TEXT("ClayBed")));
			Test.TestTrue(TEXT("Sign says drops Clay"), Layout.Rows[Index].Sign3.Contains(TEXT("drops Clay")));
			Test.TestFalse(TEXT("The today note is gone"), Layout.Rows[Index].Sign3.Contains(TEXT("today")));
		}

		for (const TPair<FName, int32>& Pair : Expect)
		{
			const int32 Delta = CountItem(PS, Pair.Key) - Before.FindRef(Pair.Key);
			Test.TestEqual(*FString::Printf(TEXT("Bag gained %s"), *Pair.Key.ToString()), Delta, Pair.Value);
		}
		Range->PollGains();
		for (const TPair<FName, int32>& Pair : Expect)
		{
			Test.TestEqual(*FString::Printf(TEXT("HUD gained %s"), *Pair.Key.ToString()), Range->GainedCount(PS->PlayerKey, Pair.Key), Pair.Value);
		}
		if (Expect.Num() == 0)
		{
			Test.TestEqual(TEXT("Spell-proof adds no dirt"), CountItem(PS, TEXT("Dirt")), Before.FindRef(FName(TEXT("Dirt"))));
			Test.TestEqual(TEXT("Spell-proof adds no planks"), CountItem(PS, TEXT("WoodPlank")), Before.FindRef(FName(TEXT("WoodPlank"))));
			Test.TestEqual(TEXT("HUD stays quiet"), Range->GainedCount(PS->PlayerKey, TEXT("Dirt")) + Range->GainedCount(PS->PlayerKey, TEXT("WoodPlank")), 0);
		}

		TMap<FName, int32> AfterBreak;
		for (const FGFItemStack& Stack : PS->Inventory->GetInventory().Items)
		{
			AfterBreak.Add(Stack.ItemId, Stack.Count);
		}
		Range->ResetAll();
		for (const FGFMiningCell& Cell : Layout.Cells)
		{
			if (Cell.RowIndex == Index)
			{
				Test.TestEqual(*FString::Printf(TEXT("%s restored"), *Cell.Range.ToString()), Voxel->GetBlock(Range->ToGrid(Cell.Range)).ToString(), Cell.Stamp.ToString());
			}
		}
		for (const TPair<FName, int32>& Pair : Expect)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s gain cleared"), *Pair.Key.ToString()), Range->GainedCount(PS->PlayerKey, Pair.Key), 0);
			Test.TestEqual(*FString::Printf(TEXT("%s stays in the bag"), *Pair.Key.ToString()), CountItem(PS, Pair.Key), AfterBreak.FindRef(Pair.Key));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningDressingTest, "GFGame.MiningRange.Dressing", GFMiningRangeTest::Flags)
bool FGFMiningDressingTest::RunTest(const FString&)
{
	FGFMiningRangeLayout Layout;
	if (!TestTrue(TEXT("Rows and the tree load"), Layout.LoadProject()))
	{
		return false;
	}
	TestEqual(TEXT("21 rows"), Layout.Rows.Num(), 21);
	TestTrue(TEXT("Dressing v2 loaded"), Layout.Dressing.bLoaded);
	TestTrue(TEXT("Spawner reads v2"), FGFMiningRangeLayout::DressingPath().EndsWith(TEXT("MiningRange_Dressing_v2.json")));
	TestFalse(TEXT("Spawner does not read v1"), FGFMiningRangeLayout::DressingPath().Contains(TEXT("Dressing_v1")));
	TestEqual(TEXT("21 signs"), Layout.Dressing.Signs.Num(), 21);
	TestEqual(TEXT("2 badges"), Layout.Dressing.Badges.Num(), 2);
	TestTrue(TEXT("RESET lever"), Layout.Dressing.Lever.bPresent);
	TestEqual(TEXT("11 trims"), Layout.Dressing.Trims.Num(), 11);
	TestEqual(TEXT("72 tiles"), Layout.Dressing.Tiles.Num(), 72);

	int32 Yaw90 = 0;
	int32 Yaw180 = 0;
	for (const FGFDressingProp& Trim : Layout.Dressing.Trims)
	{
		Yaw90 += FMath::IsNearlyEqual(Trim.YawDeg, 90.f) ? 1 : 0;
		Yaw180 += FMath::IsNearlyEqual(Trim.YawDeg, 180.f) ? 1 : 0;
	}
	TestEqual(TEXT("Side trims yaw 90"), Yaw90, 7);
	TestEqual(TEXT("End caps yaw 180"), Yaw180, 4);
	for (const FGFDressingBadge& Badge : Layout.Dressing.Badges)
	{
		TestTrue(TEXT("Badge sits at (4, -44, 10)"), Badge.OffsetCm.Equals(FVector(4.f, -44.f, 10.f), 0.01f));
	}

	UDataTable* Table = FGFDataTables::FromCsv(TEXT("DT_Blocks"), FGFBlockRow::StaticStruct());
	if (!TestNotNull(TEXT("DT_Blocks csv"), Table))
	{
		return false;
	}
	Layout.ApplyBlockTable(Table);
	const int32 Clay = Layout.RowIndexOf(TEXT("Ore_ClayBed"));
	const int32 Tree = Layout.RowIndexOf(TEXT("Verdant_TreePlot"));
	if (!TestTrue(TEXT("Clay and tree rows"), Clay != INDEX_NONE && Tree != INDEX_NONE))
	{
		return false;
	}
	bool bClayStamped = false;
	for (const FGFMiningCell& Cell : Layout.Cells)
	{
		if (Cell.RowIndex == Clay && Cell.bBreakable)
		{
			bClayStamped = Cell.Stamp == FName(TEXT("ClayBed"));
			break;
		}
	}
	TestTrue(TEXT("Clay bed is a real block"), bClayStamped);
	TestTrue(TEXT("Clay sign"), Layout.Rows[Clay].Sign3.Contains(TEXT("drops Clay")));
	TestFalse(TEXT("No today on the clay sign"), Layout.Rows[Clay].Sign3.Contains(TEXT("today")));
	TestEqual(TEXT("Tree plot sign"), Layout.Rows[Tree].Sign1, FString(TEXT("TreePlot")));
	TestEqual(TEXT("61 tree cells"), Layout.TreeLogs + Layout.TreeLeaves, 61);

	FGFMiningDressing Missing;
	FString Error;
	TestFalse(TEXT("Missing dressing file"), FGFMiningRangeLayout::LoadDressingFile(TEXT("Data/MiningRange/no-such-dressing.json"), Missing, Error));
	TestFalse(TEXT("Missing file stays unloaded"), Missing.bLoaded);
	TestFalse(TEXT("Broken JSON"), FGFMiningRangeLayout::ParseDressingString(TEXT("{"), Missing, Error));
	TestFalse(TEXT("No signs"), FGFMiningRangeLayout::ParseDressingString(TEXT("{\"signs\":[]}"), Missing, Error));

	const TCHAR* Semantic = TEXT("{\"signs\":[{\"row_id\":\"Common_Fabric\"}],\"badges\":[{\"row_id\":\"Common_Fabric\",\"sign_local_offset_cm\":{\"forward\":4,\"right\":44,\"up\":10}}]}");
	FGFMiningDressing Sem;
	TestTrue(TEXT("Semantic offset parses"), FGFMiningRangeLayout::ParseDressingString(Semantic, Sem, Error));
	if (TestTrue(TEXT("Semantic badge"), Sem.Badges.Num() == 1))
	{
		TestTrue(TEXT("Viewer's right is local -Y"), Sem.Badges[0].OffsetCm.Equals(FVector(4.f, -44.f, 10.f), 0.01f));
	}
	const TCHAR* Both = TEXT("{\"signs\":[{\"row_id\":\"Common_Fabric\"}],\"badges\":[{\"row_id\":\"Common_Fabric\",\"sign_local_offset_cm\":{\"forward\":4,\"right\":44,\"up\":10},\"sign_local_offset_ue_cm\":[9,3,1]}]}");
	FGFMiningDressing Ue;
	TestTrue(TEXT("UE offset parses"), FGFMiningRangeLayout::ParseDressingString(Both, Ue, Error));
	if (TestTrue(TEXT("UE badge"), Ue.Badges.Num() == 1))
	{
		TestTrue(TEXT("Explicit UE offset wins"), Ue.Badges[0].OffsetCm.Equals(FVector(9.f, 3.f, 1.f), 0.01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningWiringTest, "GFGame.MiningRange.Wiring", GFMiningRangeTest::Flags)
bool FGFMiningWiringTest::RunTest(const FString&)
{
	TestEqual(TEXT("Catalog stays at eight"), FGFMiniGameCatalog::All().Num(), 8);
	TestNotNull(TEXT("gf.MineRange"), IConsoleManager::Get().FindConsoleObject(TEXT("gf.MineRange")));

	TArray<FString> Shots;
	AGFMiningRange::CaptureShotNames(Shots);
	TestEqual(TEXT("Seven captures"), Shots.Num(), 7);
	const TCHAR* Want[] = { TEXT("Common"), TEXT("Verdant"), TEXT("Rime"), TEXT("Cinderwrights"), TEXT("Ore"), TEXT("Wide"), TEXT("Wide_Hud") };
	for (int32 i = 0; i < Shots.Num() && i < UE_ARRAY_COUNT(Want); ++i)
	{
		TestEqual(*FString::Printf(TEXT("Shot %d"), i), Shots[i], FString(Want[i]));
	}

	FString Ini;
	TestTrue(TEXT("DefaultGame.ini"), FFileHelper::LoadFileToString(Ini, *(FPaths::ProjectDir() / TEXT("Config/DefaultGame.ini"))));
	TestTrue(TEXT("Map is cooked"), Ini.Contains(TEXT("L_MiningRange")));
	TestTrue(TEXT("Director is listed"), Ini.Contains(TEXT("GFMiningRangeDirector")));

	FString Script;
	TestTrue(TEXT("create_maps.py"), FFileHelper::LoadFileToString(Script, *(FPaths::ProjectDir() / TEXT("Tools/create_maps.py"))));
	TestTrue(TEXT("create_maps lists the range"), Script.Contains(TEXT("/Game/Maps/L_MiningRange")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningFallbackTest, "GFGame.MiningRange.DressingFallback", GFMiningRangeTest::Flags)
bool FGFMiningFallbackTest::RunTest(const FString&)
{
	using namespace GFMiningRangeTest;
	FFallbackGuard Guard;
	FRangeWorld W(TEXT("GFMiningFallback"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = AddGameState(W.World);
	AGFMiningRange* Range = GS ? W.World->SpawnActor<AGFMiningRange>() : nullptr;
	if (!TestNotNull(TEXT("Range"), Range))
	{
		return false;
	}
	TestFalse(TEXT("Art props skipped"), Range->UsedArtDressing());
	TestEqual(TEXT("Row signs still go up"), Range->SignCount(), 21);
	TestEqual(TEXT("No badges"), Range->BadgeCount(), 0);
	TestEqual(TEXT("No trims"), Range->TrimCount(), 0);
	TestEqual(TEXT("No tiles"), Range->TileCount(), 0);
	TestTrue(TEXT("Cube lever still overlaps"), Range->HasResetTrigger());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningClientTest, "GFGame.MiningRange.ClientBreak", GFMiningRangeTest::Flags)
bool FGFMiningClientTest::RunTest(const FString&)
{
	using namespace GFMiningRangeTest;
	FRangeWorld W(TEXT("GFMiningClient"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = AddGameState(W.World);
	AGFMiningRange* Range = GS ? W.World->SpawnActor<AGFMiningRange>() : nullptr;
	AGFPlayerState* PS = (GS && Range) ? AddMiner(W.World, GS, TEXT("Guest"), false) : nullptr;
	if (!TestNotNull(TEXT("Guest"), PS) || !TestNotNull(TEXT("Voxel"), Range ? Range->GetVoxel() : nullptr))
	{
		return false;
	}
	TestFalse(TEXT("Not the host"), PS->bIsHost);
	TestFalse(TEXT("Plot permissions are off"), GS->bEnforcePlotPermissions);
	AGFReturnGate* Home = AGFReturnGate::Find(W.World, AGFMiningRange::ModeName);
	if (TestNotNull(TEXT("Shared return gate"), Home))
	{
		TestEqual(TEXT("Signed HUB"), Home->GetSignText(), FString(TEXT("HUB")));
		TestTrue(TEXT("Gate stands on the rows-file spot"), Home->GetActorLocation().Equals(Range->GetLayout().ReturnCm, 1.f));
		TestTrue(TEXT("The gate travels back to the hub"), Home->IsMapTravel());
	}
	Range->PollGains();

	const FGFMiningCell* Dirt = nullptr;
	const FGFMiningCell* Fabric = nullptr;
	for (const FGFMiningCell& Cell : Range->GetLayout().Cells)
	{
		if (!Range->GetLayout().Rows.IsValidIndex(Cell.RowIndex))
		{
			continue;
		}
		const FString& Id = Range->GetLayout().Rows[Cell.RowIndex].Id;
		if (!Dirt && Id == TEXT("Common_Dirt") && Cell.bBreakable)
		{
			Dirt = &Cell;
		}
		if (!Fabric && Id == TEXT("Common_Fabric") && Cell.bSpellProof)
		{
			Fabric = &Cell;
		}
	}
	if (!TestNotNull(TEXT("Dirt cell"), Dirt) || !TestNotNull(TEXT("Fabric cell"), Fabric))
	{
		return false;
	}
	AGFVoxelWorld* Voxel = Range->GetVoxel();
	const FIntVector DirtGrid = Range->ToGrid(Dirt->Range);
	const FIntVector FabricGrid = Range->ToGrid(Fabric->Range);
	PS->RequestEdit(DirtGrid, NAME_None);
	TestTrue(TEXT("A guest breaks dirt"), Voxel->GetBlock(DirtGrid).IsNone());
	TestEqual(TEXT("Dirt landed"), CountItem(PS, TEXT("Dirt")), 1);
	PS->RequestEdit(FabricGrid, NAME_None);
	TestEqual(TEXT("Fabric is spell-proof"), Voxel->GetBlock(FabricGrid).ToString(), Fabric->Stamp.ToString());
	TestEqual(TEXT("No fabric drop"), CountItem(PS, TEXT("Fabric")), 0);
	Range->PollGains();
	TestEqual(TEXT("Guest HUD counts the dirt"), Range->GainedCount(TEXT("Guest"), TEXT("Dirt")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningRepTest, "GFGame.MiningRange.Replication", GFMiningRangeTest::Flags)
bool FGFMiningRepTest::RunTest(const FString&)
{
	using namespace GFMiningRangeTest;
	FRangeWorld W(TEXT("GFMiningRep"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = AddGameState(W.World);
	AGFMiningRange* Range = GS ? W.World->SpawnActor<AGFMiningRange>() : nullptr;
	AGFVoxelWorld* Voxel = Range ? Range->GetVoxel() : nullptr;
	if (!TestNotNull(TEXT("Voxel"), Voxel))
	{
		return false;
	}

	const FGFVoxelGrid& Auth = Voxel->GetGrid();
	FGFVoxelGrid Copy(Auth.GetSize());
	for (const FIntVector& Coord : Auth.GetChunkCoords())
	{
		TArray<FName> Palette;
		TArray<uint16> Rle;
		Auth.EncodeChunk(Coord, Palette, Rle);
		TestTrue(TEXT("Chunk decodes"), Copy.DecodeChunk(Coord, Palette, Rle));
	}
	const uint32 Original = Auth.GetContentHash();
	TestEqual(TEXT("Client hash matches"), Copy.GetContentHash(), Original);

	const FIntVector Air = Range->ToGrid(FIntVector(0, 0, 0));
	TestTrue(TEXT("(0,0,0) is air"), Voxel->GetBlock(Air).IsNone());
	TestFalse(TEXT("Lava is not placeable"), Voxel->PlaceBlock(Air, TEXT("Lava")));
	TestTrue(TEXT("Place left the cell empty"), Voxel->GetBlock(Air).IsNone());
	TestTrue(TEXT("Author can write lava"), Voxel->ServerAuthorCell(Air, TEXT("Lava")));
	TestEqual(TEXT("Cell is lava"), Voxel->GetBlock(Air).ToString(), FString(TEXT("Lava")));

	FGFVoxelGrid After(Auth.GetSize());
	for (const FIntVector& Coord : Voxel->GetGrid().GetChunkCoords())
	{
		TArray<FName> Palette;
		TArray<uint16> Rle;
		Voxel->GetGrid().EncodeChunk(Coord, Palette, Rle);
		After.DecodeChunk(Coord, Palette, Rle);
	}
	TestEqual(TEXT("Lava round-trip"), After.GetContentHash(), Voxel->GetAuthoritativeHash());

	Range->ResetAll();
	TestEqual(TEXT("Reset restores the hash"), Voxel->GetAuthoritativeHash(), Original);
	TestTrue(TEXT("Lava is gone"), Voxel->GetBlock(Air).IsNone());

	AGFPlayerState* PS = AddMiner(W.World, GS, TEXT("Miner"), true);
	if (TestNotNull(TEXT("Miner"), PS))
	{
		FGFModeRequest Missing = FGFModeRequest::Make(AGFMiningRange::ModeName, TEXT("row"));
		Missing.Names = { TEXT("NoSuchRow") };
		const FGFModeResult Nope = Range->HandleModeRequest(PS, Missing);
		TestFalse(TEXT("Unknown row"), Nope.bOk);
		TestEqual(TEXT("NoSuchRow"), Nope.Code.ToString(), FString(TEXT("NoSuchRow")));

		FGFModeRequest One = FGFModeRequest::Make(AGFMiningRange::ModeName, TEXT("row"));
		One.Names = { TEXT("Common_Dirt") };
		TestTrue(TEXT("Row reset"), Range->HandleModeRequest(PS, One).bOk);
		TestTrue(TEXT("HUD starts on"), Range->IsHudOn());
		TestTrue(TEXT("HUD toggle"), Range->HandleModeRequest(PS, FGFModeRequest::Make(AGFMiningRange::ModeName, TEXT("hud"))).bOk);
		TestFalse(TEXT("HUD is off"), Range->IsHudOn());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningPortalTest, "GFGame.MiningRange.HubPortal", GFMiningRangeTest::Flags)
bool FGFMiningPortalTest::RunTest(const FString&)
{
	using namespace GFMiningRangeTest;
	TestEqual(TEXT("Still eight mini-games"), FGFMiniGameCatalog::All().Num(), 8);

	{
		FRangeWorld Quiet(TEXT("GFMiningQuiet"));
		if (!TestNotNull(TEXT("Quiet world"), Quiet.World))
		{
			return false;
		}
		AGFMiningRangeDirector* Director = Quiet.World->SpawnActor<AGFMiningRangeDirector>();
		if (!TestNotNull(TEXT("Director"), Director))
		{
			return false;
		}
		TestEqual(TEXT("BeginPlay stays quiet"), CountOf<AGFMiningRangePortal>(Quiet.World), 0);
		TestEqual(TEXT("No range on a test world"), CountOf<AGFMiningRange>(Quiet.World), 0);
		TestEqual(TEXT("No catalog gates"), CountOf<AGFMiniGameGate>(Quiet.World), 0);
	}

	{
		FRangeWorld Other(TEXT("GFMiningOther"));
		if (!TestNotNull(TEXT("Other world"), Other.World))
		{
			return false;
		}
		AGFMiningRangeDirector* Director = Other.World->SpawnActor<AGFMiningRangeDirector>();
		if (!TestNotNull(TEXT("Director"), Director))
		{
			return false;
		}
		Director->Place(TEXT("/Game/Maps/L_NetTest"), FVector::ZeroVector);
		TestEqual(TEXT("L_NetTest gets nothing"), CountOf<AGFMiningRangePortal>(Other.World), 0);
		TestEqual(TEXT("L_NetTest is not the range"), CountOf<AGFMiningRange>(Other.World), 0);
	}

	FRangeWorld Hub(TEXT("GFMiningHub"));
	if (!TestNotNull(TEXT("Hub world"), Hub.World))
	{
		return false;
	}
	AddGameState(Hub.World);
	AGFMiningRangeDirector* Director = Hub.World->SpawnActor<AGFMiningRangeDirector>();
	if (!TestNotNull(TEXT("Hub director"), Director))
	{
		return false;
	}
	const int32 Gates = CountOf<AGFMiniGameGate>(Hub.World);
	const FVector Rime(10.f, 20.f, 30.f);
	Director->Place(TEXT("/Game/Maps/L_Hub"), Rime);
	TestEqual(TEXT("One mining portal"), CountOf<AGFMiningRangePortal>(Hub.World), 1);
	TestEqual(TEXT("Catalog gates unchanged"), CountOf<AGFMiniGameGate>(Hub.World), Gates);
	TestEqual(TEXT("Catalog still eight"), FGFMiniGameCatalog::All().Num(), 8);
	AGFMiningRangePortal* Portal = nullptr;
	for (TActorIterator<AGFMiningRangePortal> It(Hub.World); It; ++It)
	{
		Portal = *It;
	}
	if (!TestNotNull(TEXT("Portal"), Portal))
	{
		return false;
	}
	TestEqual(TEXT("Signed Mining Range"), Portal->GetSignText().ToString(), FString(TEXT("Mining Range")));
	TestTrue(TEXT("Off the ring"), Portal->GetActorLocation().Equals(Rime + FVector(3200.f, -1600.f, 0.f), 0.1f));
	Director->Place(TEXT("/Game/Maps/L_Hub"), Rime);
	TestEqual(TEXT("Second place does not duplicate"), CountOf<AGFMiningRangePortal>(Hub.World), 1);

	Director->Place(TEXT("/Game/Maps/L_MiningRange"), FVector::ZeroVector);
	TestEqual(TEXT("Range map spawns the range"), CountOf<AGFMiningRange>(Hub.World), 1);
	TestEqual(TEXT("Portal stays one"), CountOf<AGFMiningRangePortal>(Hub.World), 1);
	return true;
}

#define GF_RANGE_ROW_TEST(Id) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningRow_##Id, "GFGame.MiningRange.Row." #Id, GFMiningRangeTest::Flags) \
	bool FGFMiningRow_##Id::RunTest(const FString&) { return GFMiningRangeTest::RunRow(*this, TEXT(#Id)); }

GF_RANGE_ROW_TEST(Common_Dirt)
GF_RANGE_ROW_TEST(Common_Stone)
GF_RANGE_ROW_TEST(Common_Fabric)
GF_RANGE_ROW_TEST(Common_PlayerWall)
GF_RANGE_ROW_TEST(Verdant_Log)
GF_RANGE_ROW_TEST(Verdant_Leaves)
GF_RANGE_ROW_TEST(Verdant_WoodPlank)
GF_RANGE_ROW_TEST(Verdant_Grass)
GF_RANGE_ROW_TEST(Verdant_Farmland)
GF_RANGE_ROW_TEST(Verdant_TreePlot)
GF_RANGE_ROW_TEST(Rime_Ice)
GF_RANGE_ROW_TEST(Rime_Glass)
GF_RANGE_ROW_TEST(Rime_RakeLane)
GF_RANGE_ROW_TEST(Cinderwrights_Lava)
GF_RANGE_ROW_TEST(Cinderwrights_Basalt)
GF_RANGE_ROW_TEST(Cinderwrights_Stone)
GF_RANGE_ROW_TEST(Cinderwrights_SparkStation)
GF_RANGE_ROW_TEST(Ore_ClayBed)
GF_RANGE_ROW_TEST(Ore_OreMetal)
GF_RANGE_ROW_TEST(Ore_CoalnutSeam)
GF_RANGE_ROW_TEST(Ore_CrystalVein)

#undef GF_RANGE_ROW_TEST

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMiningMagicDemoTest, "GFGame.MiningRange.MagicDemo", GFMiningRangeTest::Flags)
bool FGFMiningMagicDemoTest::RunTest(const FString&)
{
	using namespace GFMiningRangeTest;
	FGFMiningRangeLayout Layout;
	if (!TestTrue(TEXT("Rows load"), Layout.LoadProject()))
	{
		return false;
	}
	TestEqual(TEXT("Twenty one rows"), Layout.Rows.Num(), 21);
	int32 Demos = 0;
	for (const FGFMiningRow& Row : Layout.Rows)
	{
		Demos += Row.DemoCommands.IsEmpty() ? 0 : 1;
	}
	TestEqual(TEXT("Every row has a demo"), Demos, Layout.Rows.Num());
	const int32 Spark = Layout.RowIndexOf(TEXT("Cinderwrights_SparkStation"));
	const int32 Rake = Layout.RowIndexOf(TEXT("Rime_RakeLane"));
	const int32 Ore = Layout.RowIndexOf(TEXT("Ore_OreMetal"));
	const int32 Vein = Layout.RowIndexOf(TEXT("Ore_CrystalVein"));
	const int32 Log = Layout.RowIndexOf(TEXT("Verdant_Log"));
	if (!TestTrue(TEXT("Demo rows exist"), Spark != INDEX_NONE && Rake != INDEX_NONE && Ore != INDEX_NONE && Vein != INDEX_NONE && Log != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("Spark station says Sparkcone"), Layout.Rows[Spark].Line2, FString(TEXT("Sparkcone 8 m")));
	TestEqual(TEXT("Spark station forces the cone"), Layout.Rows[Spark].DemoCommands, FString(TEXT("gf.Magic.Field Spark")));
	TestEqual(TEXT("Rake lane calls the claw"), Layout.Rows[Rake].DemoCommands, FString(TEXT("gf.Magic.Claw")));
	TestEqual(TEXT("Ore row buys Smelter Plus"), Layout.Rows[Ore].DemoCommands, FString(TEXT("gf.Magic.Buy SmelterPlus")));
	TestTrue(TEXT("Crystal vein overcharges"), Layout.Rows[Vein].DemoCommands.Contains(TEXT("gf.Magic.Overcharge")));
	TestTrue(TEXT("Verdant toggles then holds the field"), Layout.Rows[Log].DemoCommands.Contains(TEXT("gf.Magic.Toggle")) && Layout.Rows[Log].DemoCommands.Contains(TEXT("gf.Magic.Field")));

	FFallbackGuard Guard;
	FRangeWorld W(TEXT("GFMiningMagicDemo"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AddGameState(W.World);
	AGFMiningRange* Range = W.World->SpawnActor<AGFMiningRange>();
	if (!TestNotNull(TEXT("Range"), Range))
	{
		return false;
	}
	TestEqual(TEXT("One trigger per row"), Range->DemoTriggerCount(), Range->GetLayout().Rows.Num());

	AGFMagicPlayerState* PS = W.World->SpawnActor<AGFMagicPlayerState>();
	if (!TestNotNull(TEXT("Caster"), PS) || !TestNotNull(TEXT("Magic"), PS->Magic.Get()) || !TestNotNull(TEXT("Bag"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Demo");
	PS->FactionId = TEXT("Cinderwrights");
	PS->Inventory->AddItem(TEXT("SpellPage"), 1);
	PS->Inventory->AddItem(TEXT("Crystal"), 1);
	Range->RunRowDemo(PS, TEXT("Cinderwrights_SparkStation"));
	TestTrue(TEXT("The spark station holds the field"), PS->Magic->IsFieldHeld());
	TestEqual(TEXT("The spark station forces Spark"), PS->Magic->GetElement(), EGFMagicElement::Spark);
	Range->ReleaseRowDemo(PS, TEXT("Cinderwrights_SparkStation"));
	TestFalse(TEXT("Stepping off releases the field"), PS->Magic->IsFieldHeld());
	Range->RunRowDemo(PS, TEXT("Ore_OreMetal"));
	TestTrue(TEXT("The ore sign bought Smelter Plus"), PS->Magic->GetOwnedUpgrades().Contains(FName(TEXT("SmelterPlus"))));
	const int32 Charges = PS->Magic->GetClawCharges();
	Range->RunRowDemo(PS, TEXT("Rime_RakeLane"));
	TestEqual(TEXT("The rake lane spent a claw charge"), PS->Magic->GetClawCharges(), Charges - 1);
	Range->RunRowDemo(PS, TEXT("Ore_CrystalVein"));
	TestTrue(TEXT("The vein sign overcharges"), PS->Magic->IsOvercharged());
	return true;
}

#endif
