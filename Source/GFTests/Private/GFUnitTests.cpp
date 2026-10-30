// GFGame automation tests. Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFItemTypes.h"
#include "GFRecipeTypes.h"
#include "GFVoxelTypes.h"
#include "GFCraftingSubsystem.h"
#include "GFGardenTypes.h"
#include "GFDinoTypes.h"
#include "GFSaveGame.h"
#include "GFSaveSubsystem.h"
#include "GFRhythmSettings.h"
#include "GFWasapiLoopback.h"
#include "GFBeatTracker.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

template <typename T> static int32 GFE(T V) { return static_cast<int32>(V); }

static constexpr EAutomationTestFlags GFTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVoxelSetGetTest, "GFGame.Voxel.SetGet", GFTestFlags)
bool FGFVoxelSetGetTest::RunTest(const FString&)
{
	FGFVoxelGrid Grid(FIntVector(64, 64, 32));
	FRandomStream Rng(1234);
	const FName Ids[] = { TEXT("Grass"), TEXT("WoodPlank"), TEXT("Stone") };
	TMap<FIntVector, FName> Expected;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FIntVector C(Rng.RandRange(0, 63), Rng.RandRange(0, 63), Rng.RandRange(0, 31));
		const FName Id = Ids[Rng.RandRange(0, 2)];
		TestTrue(TEXT("Set in bounds"), Grid.Set(C, Id));
		Expected.Add(C, Id);
	}
	int32 Mismatches = 0;
	for (const TPair<FIntVector, FName>& P : Expected)
	{
		Mismatches += Grid.Get(P.Key) != P.Value;
	}
	TestEqual(TEXT("All cells read back"), Mismatches, 0);
	TestEqual(TEXT("Solid count"), Grid.CountSolid(), Expected.Num());
	TestFalse(TEXT("Out of bounds rejected (-1)"), Grid.Set(FIntVector(-1, 0, 0), Ids[0]));
	TestFalse(TEXT("Out of bounds rejected (Z=32)"), Grid.Set(FIntVector(0, 0, 32), Ids[0]));
	TestTrue(TEXT("Out of bounds reads air"), Grid.Get(FIntVector(64, 0, 0)).IsNone());
	const FIntVector Some = Expected.CreateConstIterator()->Key;
	Grid.Set(Some, NAME_None);
	TestTrue(TEXT("Remove makes air"), Grid.Get(Some).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVoxelRoundTripTest, "GFGame.Save.VoxelRoundTrip", GFTestFlags)
bool FGFVoxelRoundTripTest::RunTest(const FString&)
{
	FGFVoxelGrid Grid;
	FRandomStream Rng(42);
	for (int32 i = 0; i < 500; ++i)
	{
		Grid.Set(FIntVector(Rng.RandRange(0, 63), Rng.RandRange(0, 63), Rng.RandRange(0, 31)), Rng.RandBool() ? FName(TEXT("Stone")) : FName(TEXT("Grass")));
	}
	const uint32 Before = Grid.GetContentHash();
	const FGFVoxelSaveData Data = Grid.ToSaveData();
	TestEqual(TEXT("Palette = air + 2 ids"), Data.Palette.Num(), 3);

	FGFVoxelGrid Loaded;
	TestTrue(TEXT("Load ok"), Loaded.FromSaveData(Data));
	TestEqual(TEXT("Hash after round trip"), static_cast<int64>(Loaded.GetContentHash()), static_cast<int64>(Before));
	TestEqual(TEXT("Count after round trip"), Loaded.CountSolid(), Grid.CountSolid());

	// Through the real save objects and serializer: world (voxels) and character (inventory, journal).
	UGFSaveGame* Save = NewObject<UGFSaveGame>();
	Save->Voxels = Data;
	UGFCharacterSave* CharSave = NewObject<UGFCharacterSave>();
	CharSave->Character.Inventory.Add(TEXT("Wood"), 12);
	CharSave->Character.Inventory.Add(TEXT("Fruit"), 3);
	FGFDinoDiet::RecordDiscovery(CharSave->Character.Journal, TEXT("Raptor"), TEXT("JerkyBites"), EGFFeedResult::Favorite);
	TArray<uint8> Bytes, CharBytes;
	TestTrue(TEXT("SaveGameToMemory"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	TestTrue(TEXT("Character SaveGameToMemory"), UGameplayStatics::SaveGameToMemory(CharSave, CharBytes));
	const UGFSaveGame* Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	const UGFCharacterSave* CharBack = Cast<UGFCharacterSave>(UGameplayStatics::LoadGameFromMemory(CharBytes));
	if (TestNotNull(TEXT("Loaded save"), Back) && TestNotNull(TEXT("Loaded character"), CharBack))
	{
		FGFVoxelGrid FromDisk;
		FromDisk.FromSaveData(Back->Voxels);
		TestEqual(TEXT("Voxel hash via save object"), static_cast<int64>(FromDisk.GetContentHash()), static_cast<int64>(Before));
		TestTrue(TEXT("Inventory survives"), CharBack->Character.Inventory == CharSave->Character.Inventory);
		const FGFDinoPreferenceRecord* Rec = CharBack->Character.Journal.Find(TEXT("Raptor"));
		TestTrue(TEXT("Diet knowledge survives"), Rec && Rec->KnownFavorites.Contains(FName(TEXT("JerkyBites"))));
	}

	// v1 -> v2 migration: the old single-player inventory becomes the host's character.
	UGFSaveGame* V1 = NewObject<UGFSaveGame>();
	V1->SaveVersion = 1;
	V1->Inventory.Add(TEXT("Wood"), 5);
	TestTrue(TEXT("Migrate v1"), UGFSaveSubsystem::Migrate(V1));
	TestEqual(TEXT("Migrated to latest"), V1->SaveVersion, static_cast<int32>(EGFSaveVersion::Latest));
	TestTrue(TEXT("Legacy host character"), V1->bHasLegacyHostCharacter && V1->LegacyHostCharacter.Inventory.GetCount(TEXT("Wood")) == 5);
	TestEqual(TEXT("World save no longer holds an inventory"), V1->Inventory.GetCount(TEXT("Wood")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVoxelChunkCodecTest, "GFGame.Voxel.ChunkCodec", GFTestFlags)
bool FGFVoxelChunkCodecTest::RunTest(const FString&)
{
	// The replication model without a network: per-chunk bases + edit lists rebuild the same grid.
	FGFVoxelGrid Server(FIntVector(48, 48, 32));
	FRandomStream Rng(11);
	static const FName Ids[] = { TEXT("Grass"), TEXT("Stone"), TEXT("WoodPlank") };
	for (int32 i = 0; i < 3000; ++i)
	{
		Server.Set(FIntVector(Rng.RandRange(0, 47), Rng.RandRange(0, 47), Rng.RandRange(0, 31)), Ids[Rng.RandRange(0, 2)]);
	}
	// Snapshot every chunk (what a late joiner gets)...
	TMap<FIntVector, TPair<TArray<FName>, TArray<uint16>>> Bases;
	for (const FIntVector& C : Server.GetChunkCoords())
	{
		auto& B = Bases.Add(C);
		Server.EncodeChunk(C, B.Key, B.Value);
	}
	// ...then deltas recorded afterwards, including removals.
	TArray<TPair<FIntVector, FName>> Deltas;
	for (int32 i = 0; i < 500; ++i)
	{
		const FIntVector Cell(Rng.RandRange(0, 47), Rng.RandRange(0, 47), Rng.RandRange(0, 31));
		const FName Id = Rng.RandBool() ? FName() : Ids[Rng.RandRange(0, 2)];
		Server.Set(Cell, Id);
		Deltas.Emplace(Cell, Id);
	}
	FGFVoxelGrid Client(FIntVector(48, 48, 32));
	bool bDecoded = true;
	for (auto& Pair : Bases)
	{
		bDecoded &= Client.DecodeChunk(Pair.Key, Pair.Value.Key, Pair.Value.Value);
	}
	TestTrue(TEXT("All bases decode"), bDecoded);
	for (const auto& D : Deltas)
	{
		Client.Set(D.Key, D.Value);
	}
	TestEqual(TEXT("Base + deltas = server grid"), static_cast<int64>(Client.GetContentHash()), static_cast<int64>(Server.GetContentHash()));
	for (const FIntVector& C : Server.GetChunkCoords())
	{
		if (Client.GetChunkHash(C) != Server.GetChunkHash(C))
		{
			AddError(FString::Printf(TEXT("Chunk %s differs"), *C.ToString()));
			break;
		}
	}
	TArray<FName> Pal;
	TArray<uint16> Rle;
	Server.EncodeChunk(FIntVector(99, 99, 99), Pal, Rle);
	TestTrue(TEXT("Missing chunk encodes as all air"), Pal.Num() == 1 && Rle.Num() == 2 && Rle[0] == GFChunkVolume);
	TArray<uint16> Bad = { 5, 7 };
	TestFalse(TEXT("Malformed RLE is reported"), Client.DecodeChunk(FIntVector(0, 0, 0), Pal, Bad));

	// Regions (blueprints): extract from one spot, the hash is position-independent.
	const FIntVector Min(4, 4, 0), Max(19, 19, 15), Size = Max - Min + FIntVector(1, 1, 1);
	const FGFVoxelSaveData Region = Server.ExtractRegion(Min, Max);
	TestEqual(TEXT("Region hash via save data"), static_cast<int64>(FGFVoxelGrid::HashSaveData(Region, Size)), static_cast<int64>(Server.GetRegionHash(Min, Max)));
	FGFVoxelGrid Elsewhere(FIntVector(48, 48, 32));
	FGFVoxelGrid RegionGrid(Size);
	RegionGrid.FromSaveData(Region);
	RegionGrid.ForEachSolid([&](const FIntVector& C, FName Id) { Elsewhere.Set(C + FIntVector(20, 20, 4), Id); });
	TestEqual(TEXT("Same build, different spot, same hash"), static_cast<int64>(Elsewhere.GetRegionHash(FIntVector(20, 20, 4), FIntVector(20, 20, 4) + Size - FIntVector(1, 1, 1))), static_cast<int64>(Server.GetRegionHash(Min, Max)));
	TestEqual(TEXT("Region block count"), Elsewhere.CountSolid(), Server.CountSolidInRegion(Min, Max));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFCraftRecipeTest, "GFGame.Craft.Recipe", GFTestFlags)
bool FGFCraftRecipeTest::RunTest(const FString&)
{
	FGFRecipeRow Chair;
	Chair.Inputs = { FGFItemStack(TEXT("Wood"), 4), FGFItemStack(TEXT("Fiber"), 1) };
	Chair.Output = FGFItemStack(TEXT("Chair"), 1);
	Chair.Station = TEXT("Bench");
	const FGameplayTagContainer NoTags;

	FGFInventory Poor;
	Poor.Add(TEXT("Wood"), 3);
	Poor.Add(TEXT("Fiber"), 1);
	const uint32 PoorHash = Poor.GetContentHash();
	TestEqual(TEXT("Missing inputs"), GFE(UGFCraftingSubsystem::TryCraft(Chair, TEXT("Bench"), NoTags, Poor)), GFE(EGFCraftResult::MissingInputs));
	TestEqual(TEXT("Inventory unchanged on failure"), static_cast<int64>(Poor.GetContentHash()), static_cast<int64>(PoorHash));

	FGFInventory Rich;
	Rich.Add(TEXT("Wood"), 5);
	Rich.Add(TEXT("Fiber"), 1);
	TestEqual(TEXT("Wrong station"), GFE(UGFCraftingSubsystem::TryCraft(Chair, TEXT("Loom"), NoTags, Rich)), GFE(EGFCraftResult::WrongStation));
	TestEqual(TEXT("Crafted"), GFE(UGFCraftingSubsystem::TryCraft(Chair, TEXT("Bench"), NoTags, Rich)), GFE(EGFCraftResult::Crafted));
	TestEqual(TEXT("Wood consumed"), Rich.GetCount(TEXT("Wood")), 1);
	TestEqual(TEXT("Fiber consumed"), Rich.GetCount(TEXT("Fiber")), 0);
	TestEqual(TEXT("Chair added"), Rich.GetCount(TEXT("Chair")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFGardenGrowTest, "GFGame.Garden.Grow", GFTestFlags)
bool FGFGardenGrowTest::RunTest(const FString&)
{
	FGFPlantRow Row;
	Row.StageSeconds = { 60.f, 120.f, 120.f }; // 4 stages, 300 s total
	TestEqual(TEXT("Stages"), Row.GetNumStages(), 4);
	TestEqual(TEXT("Total"), Row.GetTotalGrowSeconds(), 300.f);
	TestEqual(TEXT("t=0"), Row.StageForElapsed(0.f), 0);
	TestEqual(TEXT("t=59"), Row.StageForElapsed(59.f), 0);
	TestEqual(TEXT("t=60"), Row.StageForElapsed(60.f), 1);
	TestEqual(TEXT("t=299"), Row.StageForElapsed(299.f), 2);
	TestEqual(TEXT("t=300 harvestable"), Row.StageForElapsed(300.f), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRhythmListenToTest, "GFGame.Rhythm.ListenTo", GFTestFlags)
bool FGFRhythmListenToTest::RunTest(const FString&)
{
	const UGFRhythmSettings* Defaults = GetDefault<UGFRhythmSettings>();
	TestTrue(TEXT("Listen to defaults to system default output"), Defaults->ListenDeviceId.IsEmpty());

	const TArray<FGFAudioOutputDevice> Devices = FGFWasapiLoopback::EnumerateOutputDevices();
	if (TestTrue(TEXT("At least the system default entry"), Devices.Num() >= 1))
	{
		TestTrue(TEXT("First entry is System default"), Devices[0].Id.IsEmpty() && Devices[0].bIsSystemDefault);
	}
	AddInfo(FString::Printf(TEXT("%d active output device(s) found"), Devices.Num() - 1));

	FGFBeatTracker Tracker(48000);
	TArray<float> Silence;
	Silence.SetNumZeroed(48000 * 2);
	Tracker.ProcessSamples(Silence.GetData(), Silence.Num(), 0.0);
	TestTrue(TEXT("2 s of silence detected"), Tracker.IsSilent());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
