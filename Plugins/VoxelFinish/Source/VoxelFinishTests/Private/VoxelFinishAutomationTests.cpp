#include "Misc/AutomationTest.h"

#include <limits>

#include "VoxelFinish.h"
#include "VoxelFinishEffectGraph.h"
#include "VoxelFinishLookup.h"
#include "VoxelFinishPainterComponent.h"
#include "VoxelFinishRateLimiter.h"
#include "VoxelFinishSession.h"
#include "VoxelFinishVolume.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags VoxelFinishTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

static FFinish MakeSampleFinish()
{
	FFinish Finish;
	Finish.Tint = FLinearColor::White;
	Finish.PatternId = 3;
	Finish.ColorA = FLinearColor(1.f, 0.f, 0.f, 1.f);
	Finish.ColorB = FLinearColor(0.f, 0.f, 1.f, 1.f);
	Finish.EffectId = 4;
	Finish.Intensity = 1.f;
	Finish.bGlow = true;
	return Finish;
}

/** The dye hook refuses when unset, so tests that are not about spending bind one that always pays. */
static void AllowAllDyes(FVoxelFinishSession& Session)
{
	Session.SetCanAffordAndSpend([](const FFinish&, APlayerState*)
	{
		return true;
	});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishDedupeHashTest, "VoxelFinish.DedupeHash", VoxelFinishTestFlags)
bool FVoxelFinishDedupeHashTest::RunTest(const FString&)
{
	const FFinish A = MakeSampleFinish();
	FFinish B = A;
	TestEqual(TEXT("Canonical hash is stable"), static_cast<int64>(A.GetCanonicalHash()), static_cast<int64>(0xFD7B5E66u));
	TestEqual(TEXT("Identical finishes hash equal"), static_cast<int64>(A.GetCanonicalHash()), static_cast<int64>(B.GetCanonicalHash()));
	TestTrue(TEXT("Identical finishes compare canonical"), A.CanonicallyEquals(B));

	FVoxelFinishSession Session(true);
	const uint16 First = Session.GetPalette().FindOrAdd(A);
	const uint16 Second = Session.GetPalette().FindOrAdd(B);
	TestEqual(TEXT("Dedupe returns the same id"), First, Second);
	TestEqual(TEXT("One palette slot"), Session.GetPalette().NumOccupied(), 1);
	TestTrue(TEXT("Id is not unpainted"), First != VoxelFinish::UnpaintedId);

	FFinish Noisy = A;
	Noisy.ColorA.G = 0.001f;
	TestEqual(TEXT("Sub-quantum green still matches"), static_cast<int64>(A.GetCanonicalHash()), static_cast<int64>(Noisy.GetCanonicalHash()));
	TestEqual(TEXT("Noisy finish reuses the id"), Session.GetPalette().FindOrAdd(Noisy), First);

	FFinish Different = A;
	Different.ColorA.G = 1.f / 255.f;
	TestNotEqual(TEXT("One 8-bit step changes the hash"), A.GetCanonicalHash(), Different.GetCanonicalHash());
	TestNotEqual(TEXT("Different finish gets a new id"), Session.GetPalette().FindOrAdd(Different), First);
	TestEqual(TEXT("Two occupied slots"), Session.GetPalette().NumOccupied(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishPaletteCapTest, "VoxelFinish.PaletteCap", VoxelFinishTestFlags)
bool FVoxelFinishPaletteCapTest::RunTest(const FString&)
{
	FVoxelFinishPalette Palette;
	uint16 First = 0;
	for (int32 Index = 0; Index < VoxelFinish::MaxPaletteEntries; ++Index)
	{
		FFinish Finish;
		Finish.EffectId = Index;
		Finish.Intensity = 1.f;
		const uint16 Id = Palette.FindOrAdd(Finish);
		if (Index == 0)
		{
			First = Id;
		}
		if (Id == VoxelFinish::UnpaintedId)
		{
			AddError(FString::Printf(TEXT("Palette rejected entry %d"), Index));
			return false;
		}
	}
	TestEqual(TEXT("Palette is full"), Palette.NumOccupied(), VoxelFinish::MaxPaletteEntries);
	FFinish Extra;
	Extra.EffectId = 100000;
	Extra.Intensity = 1.f;
	TestEqual(TEXT("Finish past the cap is rejected"), Palette.FindOrAdd(Extra), static_cast<uint16>(VoxelFinish::UnpaintedId));
	TestEqual(TEXT("Cap did not grow"), Palette.NumOccupied(), VoxelFinish::MaxPaletteEntries);
	TestNotNull(TEXT("First finish is still stored"), Palette.Find(First));

	FFinish Again;
	Again.EffectId = 0;
	Again.Intensity = 1.f;
	TestEqual(TEXT("Existing finish still resolves when full"), Palette.FindOrAdd(Again), First);

	// Every id the palette can hand out must have a lookup column.
	FFinish Last;
	Last.EffectId = VoxelFinish::MaxPaletteEntries - 1;
	Last.Intensity = 1.f;
	const uint16 LastId = Palette.FindOrAdd(Last);
	TestEqual(TEXT("Highest id is MaxPaletteEntries"), static_cast<int32>(LastId), VoxelFinish::MaxPaletteEntries);
	TArray<FColor> Pixels;
	FVoxelFinishLookup::InitPixels(Pixels);
	FFinish Red;
	Red.ColorA = FLinearColor::Red;
	FVoxelFinishLookup::WriteFinish(Pixels, LastId, Red);
	TestEqual(TEXT("Highest id is baked into the lookup"), static_cast<int32>(FVoxelFinishLookup::Read(Pixels, LastId, FVoxelFinishLookup::RowColorA).R), 255);
	TestTrue(TEXT("Lookup has a column for every id"), VoxelFinish::MaxPaletteEntries < VoxelFinish::LookupTextureWidth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishGCBlueprintTest, "VoxelFinish.GCBlueprintRefs", VoxelFinishTestFlags)
bool FVoxelFinishGCBlueprintTest::RunTest(const FString&)
{
	FVoxelFinishSession Session(true);
	AllowAllDyes(Session);
	FFinish Painted;
	Painted.PatternId = 1;
	Painted.ColorA = FLinearColor::Red;
	FFinish Replaced;
	Replaced.PatternId = 2;
	Replaced.ColorA = FLinearColor::Green;
	FFinish BlueprintOnly;
	BlueprintOnly.PatternId = 4;
	BlueprintOnly.ColorA = FLinearColor::Blue;

	const FIntVector Cell(1, 2, 3);
	TestEqual(TEXT("Paint applied"), static_cast<int32>(Session.ServerPaint(Cell, Painted, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const uint16 PaintedId = Session.GetGrid().GetFinishId(Cell);
	TestEqual(TEXT("Replace applied"), static_cast<int32>(Session.ServerPaint(FIntVector(4, 2, 3), Replaced, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const uint16 ReplacedId = Session.GetGrid().GetFinishId(FIntVector(4, 2, 3));
	const uint16 BlueprintId = Session.RestoreBlueprintFinish(BlueprintOnly);
	TestTrue(TEXT("Blueprint id assigned"), BlueprintId != VoxelFinish::UnpaintedId);

	TestEqual(TEXT("Overwrite the first cell"), static_cast<int32>(Session.ServerPaint(Cell, Replaced, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const int32 Freed = Session.CollectGarbage();
	TestEqual(TEXT("Only the unreferenced finish is collected"), Freed, 1);
	TestNull(TEXT("Paint with no refs is gone"), Session.GetPalette().Find(PaintedId));
	TestNotNull(TEXT("Finish still on a block stays"), Session.GetPalette().Find(ReplacedId));
	TestNotNull(TEXT("Blueprint ref keeps an unused finish"), Session.GetPalette().Find(BlueprintId));
	TestTrue(TEXT("Blueprint ref count is positive"), Session.GetPalette().GetBlueprintRefs(BlueprintId) > 0);

	const uint16 Resolved = Session.GetPalette().FindOrAdd(BlueprintOnly);
	TestEqual(TEXT("Blueprint value re-resolves to the same id"), Resolved, BlueprintId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishClearCellsTest, "VoxelFinish.ClearCells", VoxelFinishTestFlags)
bool FVoxelFinishClearCellsTest::RunTest(const FString&)
{
	FVoxelFinishSession Session(true);
	AllowAllDyes(Session);
	auto Make = [](int32 Pattern, const FLinearColor& Color)
	{
		FFinish Finish;
		Finish.PatternId = Pattern;
		Finish.ColorA = Color;
		return Finish;
	};
	const FFinish Red = Make(1, FLinearColor::Red);
	const FFinish Green = Make(2, FLinearColor::Green);
	const FFinish Leftover = Make(5, FLinearColor::Yellow);
	const FFinish Blueprint = Make(4, FLinearColor::Blue);
	const FIntVector A(1, 2, 3);
	const FIntVector B(2, 2, 3);
	const FIntVector C(40, 2, 3);
	const int32 Applied = static_cast<int32>(EVoxelFinishPaintResult::Applied);
	TestEqual(TEXT("A red"), static_cast<int32>(Session.ServerPaint(A, Red, nullptr)), Applied);
	TestEqual(TEXT("B red"), static_cast<int32>(Session.ServerPaint(B, Red, nullptr)), Applied);
	TestEqual(TEXT("C leftover"), static_cast<int32>(Session.ServerPaint(C, Leftover, nullptr)), Applied);
	TestEqual(TEXT("C green (leftover now unused)"), static_cast<int32>(Session.ServerPaint(C, Green, nullptr)), Applied);
	const uint16 BlueprintId = Session.RestoreBlueprintFinish(Blueprint);
	TestEqual(TEXT("Blueprint used on a block"), static_cast<int32>(Session.ServerPaint(FIntVector(3, 2, 3), Blueprint, nullptr)), Applied);
	Session.GetGrid().ClearDirty();
	TArray<uint16> Ignored;
	TArray<uint16> IgnoredRemoved;
	Session.GetPalette().ConsumeChanges(Ignored, IgnoredRemoved);
	const uint16 RedId = Session.GetPalette().FindExisting(Red);
	const uint16 LeftoverId = Session.GetPalette().FindExisting(Leftover);

	TestEqual(TEXT("Clearing A (and an unpainted cell) unpaints one cell"), Session.ServerClearCells({ A, FIntVector(9, 9, 9) }), 1);
	TestEqual(TEXT("A unpainted"), static_cast<int32>(Session.GetGrid().GetFinishId(A)), 0);
	TestNotNull(TEXT("Red stays while B uses it"), Session.GetPalette().Find(RedId));
	TArray<FIntVector> Dirty = Session.GetGrid().ConsumeDirtyChunks();
	TestTrue(TEXT("Only A's chunk is dirty"), Dirty.Num() == 1 && Dirty[0] == VoxelFinish::CellToChunk(A));

	TestEqual(TEXT("Clearing B and the blueprint's block"), Session.ServerClearCells({ B, FIntVector(3, 2, 3) }), 2);
	TestNull(TEXT("Red is freed with its last block"), Session.GetPalette().Find(RedId));
	TestNotNull(TEXT("The blueprint's finish stays (blueprint ref)"), Session.GetPalette().Find(BlueprintId));
	TestNotNull(TEXT("An unrelated unused finish is left for the game's own GC"), Session.GetPalette().Find(LeftoverId));
	TArray<uint16> Upserts;
	TArray<uint16> Removed;
	Session.GetPalette().ConsumeChanges(Upserts, Removed);
	TestTrue(TEXT("Red is reported as removed"), Removed.Num() == 1 && Removed[0] == RedId);
	TestEqual(TEXT("Only C stays painted"), Session.GetGrid().NumPainted(), 1);

	FVoxelFinishSession Client(false);
	Client.GetGrid().WriteReplicated(A, 1);
	TestEqual(TEXT("A client cannot clear"), Client.ServerClearCells({ A }), 0);
	TestEqual(TEXT("...and its cell is untouched"), static_cast<int32>(Client.GetGrid().GetFinishId(A)), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishVertexTest, "VoxelFinish.VertexEncoding", VoxelFinishTestFlags)
bool FVoxelFinishVertexTest::RunTest(const FString&)
{
	FVector2f UV2;
	VoxelFinish::WriteFinishToVertex(42, FVector2f(0.5f, 0.25f), UV2);
	TestEqual(TEXT("Finish id is UV2.X"), FMath::RoundToInt(UV2.X), 42);
	TestEqual(TEXT("Face texel is packed in UV2.Y"), FMath::RoundToInt(UV2.Y), 8 + 4 * 16);

	uint16 Id = 0;
	FVector2f Face;
	VoxelFinish::DecodeFinishVertex(UV2, Id, Face);
	TestEqual(TEXT("Decoded id"), static_cast<int32>(Id), 42);
	TestEqual(TEXT("Decoded face U"), Face.X, 8.5f / 16.f);
	TestEqual(TEXT("Decoded face V"), Face.Y, 4.5f / 16.f);

	VoxelFinish::WriteFinishToVertex(0, FVector2f(0.f, 0.f), UV2);
	TestEqual(TEXT("Unpainted id stays 0"), FMath::RoundToInt(UV2.X), 0);
	TestEqual(TEXT("Origin texel packs to 0"), FMath::RoundToInt(UV2.Y), 0);

	VoxelFinish::WriteFinishToVertex(7, FVector2f(1.f, 1.f), UV2);
	TestEqual(TEXT("UV 1 clamps to the last texel"), FMath::RoundToInt(UV2.Y), 15 + 15 * 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishCapsTest, "VoxelFinish.Caps", VoxelFinishTestFlags)
bool FVoxelFinishCapsTest::RunTest(const FString&)
{
	FVoxelFinishSession Session(true);
	AllowAllDyes(Session);
	// The caps are about distinct effect finishes, so this test registers arbitrary effect ids.
	for (int32 Index = 0; Index < VoxelFinish::MaxEffectFinishesPerChunk; ++Index)
	{
		Session.AllowEffectId(Index);
	}
	Session.AllowEffectId(1000);
	Session.AllowEffectId(90000);
	for (int32 Index = 2000; Index < 2000 + 9 * 32; ++Index)
	{
		Session.AllowEffectId(Index);
	}
	for (int32 Index = 0; Index < VoxelFinish::MaxEffectFinishesPerChunk; ++Index)
	{
		FFinish Finish;
		Finish.EffectId = Index;
		Finish.Intensity = 1.f;
		const FIntVector Cell(Index % VoxelFinish::ChunkSize, Index / VoxelFinish::ChunkSize, 0);
		if (Session.ServerPaint(Cell, Finish, nullptr) != EVoxelFinishPaintResult::Applied)
		{
			AddError(FString::Printf(TEXT("Chunk effect %d rejected"), Index));
			return false;
		}
	}
	FFinish Overflow;
	Overflow.EffectId = 1000;
	Overflow.Intensity = 1.f;
	const FIntVector OverflowCell(0, 2, 0);
	TestEqual(TEXT("33rd effect finish in the chunk is rejected"), static_cast<int32>(Session.ServerPaint(OverflowCell, Overflow, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::RejectedEffectCap));
	TestEqual(TEXT("Rejected paint does not write"), static_cast<int32>(Session.GetGrid().GetFinishId(OverflowCell)), 0);

	FFinish Plain;
	Plain.EffectId = INDEX_NONE;
	Plain.PatternId = 2;
	TestEqual(TEXT("Plain paint still fits in a full effect chunk"), static_cast<int32>(Session.ServerPaint(OverflowCell, Plain, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));

	for (int32 Chunk = 1; Chunk < VoxelFinish::MaxEffectFinishesGlobal / VoxelFinish::MaxEffectFinishesPerChunk; ++Chunk)
	{
		for (int32 Index = 0; Index < VoxelFinish::MaxEffectFinishesPerChunk; ++Index)
		{
			FFinish Finish;
			Finish.EffectId = 2000 + Chunk * 32 + Index;
			Finish.Intensity = 1.f;
			const FIntVector Cell(Index % VoxelFinish::ChunkSize, Index / VoxelFinish::ChunkSize, Chunk * VoxelFinish::ChunkSize);
			if (Session.ServerPaint(Cell, Finish, nullptr) != EVoxelFinishPaintResult::Applied)
			{
				AddError(FString::Printf(TEXT("Global fill chunk %d index %d rejected"), Chunk, Index));
				return false;
			}
		}
	}
	FFinish GlobalOverflow;
	GlobalOverflow.EffectId = 90000;
	GlobalOverflow.Intensity = 1.f;
	const FIntVector FarCell(0, 0, 8 * VoxelFinish::ChunkSize);
	TestEqual(TEXT("Global effect cap rejects a new effect finish"), static_cast<int32>(Session.ServerPaint(FarCell, GlobalOverflow, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::RejectedEffectCap));

	TestTrue(TEXT("Effect visible at the cutoff"), VoxelFinish::IsEffectVisibleAtDistance(VoxelFinish::EffectDistanceCutoffCm));
	TestFalse(TEXT("Effect drops past the cutoff"), VoxelFinish::IsEffectVisibleAtDistance(VoxelFinish::EffectDistanceCutoffCm + 1.f));
	TestEqual(TEXT("Paint id remains past the cutoff"), static_cast<int32>(Session.GetGrid().GetFinishId(OverflowCell)), static_cast<int32>(Session.GetPalette().FindOrAdd(Plain)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishRoundTripTest, "VoxelFinish.SerializationRoundTrip", VoxelFinishTestFlags)
bool FVoxelFinishRoundTripTest::RunTest(const FString&)
{
	FVoxelFinishSession Server(true);
	AllowAllDyes(Server);
	FFinish Painted = MakeSampleFinish();
	Painted.EffectId = INDEX_NONE;
	Painted.bGlow = false;
	FFinish BlueprintFinish;
	BlueprintFinish.PatternId = 8;
	BlueprintFinish.ColorA = FLinearColor(0.2f, 0.4f, 0.6f, 1.f);
	BlueprintFinish.ColorB = FLinearColor(0.9f, 0.1f, 0.1f, 1.f);
	BlueprintFinish.EffectId = 2;
	BlueprintFinish.Intensity = 2.5f;
	BlueprintFinish.bGlow = true;

	const FIntVector Cell(3, 4, 5);
	TestEqual(TEXT("Paint"), static_cast<int32>(Server.ServerPaint(Cell, Painted, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const uint16 BlueprintId = Server.RestoreBlueprintFinish(BlueprintFinish);
	TestTrue(TEXT("Blueprint stored"), BlueprintId != VoxelFinish::UnpaintedId);

	FVoxelFinishSaveData Data = Server.ToSaveData();
	TestEqual(TEXT("Save is sparse"), Data.Version, FVoxelFinishSaveData::SparseVersion);
	TestEqual(TEXT("Only occupied slots are saved"), Data.SlotIds.Num(), 2);
	TestEqual(TEXT("No dense palette in a new save"), Data.Palette.Num(), 0);
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	FVoxelFinishSaveData::StaticStruct()->SerializeItem(Writer, &Data, nullptr);

	FVoxelFinishSaveData Loaded;
	FMemoryReader Reader(Bytes, true);
	FVoxelFinishSaveData::StaticStruct()->SerializeItem(Reader, &Loaded, nullptr);

	FVoxelFinishSession Restored(true);
	Restored.RestoreFromSave(Loaded);
	TestEqual(TEXT("Cell id restored"), static_cast<int32>(Restored.GetGrid().GetFinishId(Cell)), static_cast<int32>(Server.GetGrid().GetFinishId(Cell)));
	const FFinish* RestoredPaint = Restored.GetPalette().Find(Restored.GetGrid().GetFinishId(Cell));
	TestTrue(TEXT("Painted finish matches"), RestoredPaint && RestoredPaint->CanonicallyEquals(Painted));

	const uint16 Resolved = Restored.GetPalette().FindOrAdd(BlueprintFinish);
	TestEqual(TEXT("Blueprint re-resolves after restore"), Resolved, BlueprintId);
	TestTrue(TEXT("Re-resolved finish matches the stored value"), Restored.GetPalette().Find(Resolved) && Restored.GetPalette().Find(Resolved)->CanonicallyEquals(BlueprintFinish));

	FVoxelFinishSaveData Legacy;
	Legacy.Palette.SetNum(VoxelFinish::MaxPaletteEntries + 2);
	Legacy.Occupied.SetNumZeroed(VoxelFinish::MaxPaletteEntries + 2);
	Legacy.BlueprintRefs.SetNumZeroed(VoxelFinish::MaxPaletteEntries + 2);
	Legacy.Palette[9] = Painted;
	Legacy.Occupied[9] = 1;
	Legacy.Palette[VoxelFinish::MaxPaletteEntries + 1] = BlueprintFinish;
	Legacy.Occupied[VoxelFinish::MaxPaletteEntries + 1] = 1;
	Legacy.BlueprintRefs[VoxelFinish::MaxPaletteEntries + 1] = 1;
	const uint16 LegacyOverflowId = static_cast<uint16>(VoxelFinish::MaxPaletteEntries + 1);
	const FIntVector OverflowCell(6, 4, 5);
	const FIntVector UnknownCell(7, 4, 5);
	Legacy.Cells.Add(Cell);
	Legacy.CellFinishIds.Add(9);
	Legacy.Cells.Add(OverflowCell);
	Legacy.CellFinishIds.Add(LegacyOverflowId);
	Legacy.Cells.Add(UnknownCell);
	Legacy.CellFinishIds.Add(77);
	FVoxelFinishSession FromLegacy(true);
	FromLegacy.RestoreFromSave(Legacy);
	TestEqual(TEXT("Dense legacy save still loads"), static_cast<int32>(FromLegacy.GetGrid().GetFinishId(Cell)), 9);
	TestTrue(TEXT("Legacy slot keeps its finish"), FromLegacy.GetPalette().Find(9) && FromLegacy.GetPalette().Find(9)->CanonicallyEquals(Painted));
	TestEqual(TEXT("Legacy id 4096 is re-added under a free id"), FromLegacy.GetPalette().NumOccupied(), 2);
	const uint16 RemappedId = FromLegacy.GetGrid().GetFinishId(OverflowCell);
	TestTrue(TEXT("The id-4096 cell is remapped into the palette range"), RemappedId != VoxelFinish::UnpaintedId && RemappedId <= VoxelFinish::MaxPaletteEntries);
	TestTrue(TEXT("The remapped id holds the id-4096 finish"), FromLegacy.GetPalette().Find(RemappedId) && FromLegacy.GetPalette().Find(RemappedId)->CanonicallyEquals(BlueprintFinish));
	TestEqual(TEXT("The remapped slot keeps its blueprint refs"), FromLegacy.GetPalette().GetBlueprintRefs(RemappedId), 1);
	TestEqual(TEXT("A cell whose id is not in the palette loads unpainted"), static_cast<int32>(FromLegacy.GetGrid().GetFinishId(UnknownCell)), 0);
	TestEqual(TEXT("Remapped cell is counted under its new id"), FromLegacy.GetGrid().GetUseCount(RemappedId), 1);
	TestEqual(TEXT("Nothing is counted under id 4096"), FromLegacy.GetGrid().GetUseCount(LegacyOverflowId), 0);
	TestEqual(TEXT("Legacy restore leaves no orphaned ids"), FromLegacy.CollectGarbage(), 0);

	// Same legacy layout with every in-range slot taken: the id-4096 cell has nowhere to go and loads unpainted.
	FVoxelFinishSaveData FullLegacy = Legacy;
	for (int32 Id = 1; Id <= VoxelFinish::MaxPaletteEntries; ++Id)
	{
		if (Id == 9)
		{
			continue;
		}
		FFinish Filler = Painted;
		Filler.ColorB = FLinearColor((Id & 0xFF) / 255.f, ((Id >> 8) & 0xFF) / 255.f, 0.5f, 1.f);
		FullLegacy.Palette[Id] = Filler;
		FullLegacy.Occupied[Id] = 1;
	}
	FVoxelFinishSession FromFullLegacy(true);
	FromFullLegacy.RestoreFromSave(FullLegacy);
	TestEqual(TEXT("Full legacy palette keeps 4095 slots"), FromFullLegacy.GetPalette().NumOccupied(), VoxelFinish::MaxPaletteEntries);
	TestEqual(TEXT("Full palette: the id-4096 cell loads unpainted"), static_cast<int32>(FromFullLegacy.GetGrid().GetFinishId(OverflowCell)), 0);
	TestEqual(TEXT("Full palette: in-range cells still load"), static_cast<int32>(FromFullLegacy.GetGrid().GetFinishId(Cell)), 9);

	FVoxelFinishSession FromValue(true);
	const uint16 Fresh = FromValue.RestoreBlueprintFinish(BlueprintFinish);
	TestTrue(TEXT("Value-only restore assigns an id"), Fresh != VoxelFinish::UnpaintedId);
	TestTrue(TEXT("Value-only restore keeps the finish"), FromValue.GetPalette().Find(Fresh) && FromValue.GetPalette().Find(Fresh)->CanonicallyEquals(BlueprintFinish));
	TestEqual(TEXT("Garbage collection keeps the blueprint finish"), FromValue.CollectGarbage(), 0);
	TestNotNull(TEXT("Blueprint finish survives GC"), FromValue.GetPalette().Find(Fresh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishAuthorityTest, "VoxelFinish.Paint.Authority", VoxelFinishTestFlags)
bool FVoxelFinishAuthorityTest::RunTest(const FString&)
{
	FVoxelFinishSession Client(false);
	FFinish Finish = MakeSampleFinish();
	Finish.EffectId = INDEX_NONE;
	const FIntVector Cell(1, 0, 0);
	TestEqual(TEXT("Client paint is rejected"), static_cast<int32>(Client.ServerPaint(Cell, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::RejectedNotAuthority));
	TestEqual(TEXT("Client grid unchanged"), static_cast<int32>(Client.GetGrid().GetFinishId(Cell)), 0);
	Client.GetGrid().SetFinishId(Cell, 4);
	TestEqual(TEXT("SetFinishId is server only"), static_cast<int32>(Client.GetGrid().GetFinishId(Cell)), 0);

	int32 Spent = 0;
	FVoxelFinishSession Server(true);
	TestEqual(TEXT("An unset dye hook refuses"), static_cast<int32>(Server.ServerPaint(Cell, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::RejectedCannotAfford));
	TestEqual(TEXT("Refused paint takes no palette slot"), Server.GetPalette().NumOccupied(), 0);
	Server.SetCanAffordAndSpend([&Spent](const FFinish&, APlayerState*)
	{
		return false;
	});
	TestEqual(TEXT("Dye check can reject"), static_cast<int32>(Server.ServerPaint(Cell, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::RejectedCannotAfford));
	TestEqual(TEXT("Rejected dye does not spend a slot"), Server.GetPalette().NumOccupied(), 0);

	Server.SetCanAffordAndSpend([&Spent](const FFinish&, APlayerState*)
	{
		++Spent;
		return true;
	});
	TestEqual(TEXT("Server paint applies"), static_cast<int32>(Server.ServerPaint(Cell, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	TestEqual(TEXT("Materials hook ran once"), Spent, 1);
	TestEqual(TEXT("Same finish on the same cell is a no-op"), static_cast<int32>(Server.ServerPaint(Cell, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::AlreadyPainted));
	FFinish Noisy = Finish;
	Noisy.ColorA.G += 0.0001f;
	TestEqual(TEXT("A canonically equal finish is also a no-op"), static_cast<int32>(Server.ServerPaint(Cell, Noisy, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::AlreadyPainted));
	TestEqual(TEXT("Same-finish repaint is not charged"), Spent, 1);
	const TArray<FIntVector> Dirty = Server.GetGrid().ConsumeDirtyChunks();
	TestEqual(TEXT("Only one chunk is dirty"), Dirty.Num(), 1);
	TestTrue(TEXT("Dirty chunk is the painted cell's chunk"), Dirty[0] == VoxelFinish::CellToChunk(Cell));

	const FIntVector Other(20, 0, 0);
	TestEqual(TEXT("Second paint"), static_cast<int32>(Server.ServerPaint(Other, Finish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const TArray<FIntVector> DirtyAgain = Server.GetGrid().ConsumeDirtyChunks();
	TestEqual(TEXT("Repaint dirties only the new chunk"), DirtyAgain.Num(), 1);
	TestTrue(TEXT("Second dirty chunk"), DirtyAgain[0] == VoxelFinish::CellToChunk(Other));
	TestFalse(TEXT("First chunk was not dirtied again"), DirtyAgain.Contains(VoxelFinish::CellToChunk(Cell)));

	TArray<TPair<uint16, uint16>> DeltaA;
	TArray<TPair<uint16, uint16>> DeltaB;
	Server.ExportChunkDelta(VoxelFinish::CellToChunk(Cell), DeltaA);
	Server.ExportChunkDelta(VoxelFinish::CellToChunk(Other), DeltaB);
	const uint16 Id = Server.GetGrid().GetFinishId(Cell);
	const FFinish* Stored = Server.GetPalette().Find(Id);

	FVoxelFinishSession Replica(false);
	Replica.ApplyReplicatedPaletteSlot(Id, *Stored, 0);
	Replica.ApplyReplicatedChunk(VoxelFinish::CellToChunk(Cell), DeltaA);
	TestEqual(TEXT("Replica received chunk A"), static_cast<int32>(Replica.GetGrid().GetFinishId(Cell)), static_cast<int32>(Id));
	Replica.ApplyReplicatedChunk(VoxelFinish::CellToChunk(Other), DeltaB);
	TestEqual(TEXT("Replica received chunk B"), static_cast<int32>(Replica.GetGrid().GetFinishId(Other)), static_cast<int32>(Id));

	FFinish OtherFinish = Finish;
	OtherFinish.PatternId = 9;
	TestEqual(TEXT("Repaint B"), static_cast<int32>(Server.ServerPaint(Other, OtherFinish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	const TArray<FIntVector> OnlyB = Server.GetGrid().ConsumeDirtyChunks();
	TestEqual(TEXT("Only B is dirty after repaint"), OnlyB.Num(), 1);
	TArray<TPair<uint16, uint16>> DeltaB2;
	Server.ExportChunkDelta(OnlyB[0], DeltaB2);
	const uint16 IdB = Server.GetGrid().GetFinishId(Other);
	Replica.ApplyReplicatedPaletteSlot(IdB, *Server.GetPalette().Find(IdB), 0);
	Replica.ApplyReplicatedChunk(OnlyB[0], DeltaB2);
	TestEqual(TEXT("Chunk A paint stayed"), static_cast<int32>(Replica.GetGrid().GetFinishId(Cell)), static_cast<int32>(Id));
	TestEqual(TEXT("Chunk B paint updated"), static_cast<int32>(Replica.GetGrid().GetFinishId(Other)), static_cast<int32>(IdB));

	// Repaint A too, so the first finish has no blocks left; GC frees it and the client drops the slot.
	TestEqual(TEXT("Repaint A"), static_cast<int32>(Server.ServerPaint(Cell, OtherFinish, nullptr)), static_cast<int32>(EVoxelFinishPaintResult::Applied));
	TestEqual(TEXT("GC frees the unused finish"), Server.CollectGarbage(), 1);
	TArray<uint16> Upserts;
	TArray<uint16> Removed;
	Server.GetPalette().ConsumeChanges(Upserts, Removed);
	TestTrue(TEXT("The freed id is reported as removed"), Removed.Contains(Id));
	for (uint16 RemovedId : Removed)
	{
		Replica.ApplyReplicatedPaletteRemoval(RemovedId);
	}
	TestNull(TEXT("Replica drops the removed slot"), Replica.GetPalette().Find(Id));
	TestNotNull(TEXT("Replica keeps the live slot"), Replica.GetPalette().Find(IdB));

	int32 Visited = 0;
	Replica.GetMeshSource().ForEachPaintedBlock([&Visited](FIntVector, uint16) { ++Visited; });
	TestEqual(TEXT("Mesh source walks both painted blocks"), Visited, 2);
	TestNotNull(TEXT("Volume class is registered"), AVoxelFinishVolume::StaticClass());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishPlayerOwnedPathTest, "VoxelFinish.Paint.PlayerOwnedPath", VoxelFinishTestFlags)
bool FVoxelFinishPlayerOwnedPathTest::RunTest(const FString&)
{
	TestNull(TEXT("The volume has no client-callable paint RPC"), AVoxelFinishVolume::StaticClass()->FindFunctionByName(TEXT("ServerPaint")));

	UFunction* Rpc = UVoxelFinishPainterComponent::StaticClass()->FindFunctionByName(TEXT("ServerRequestPaint"));
	TestNotNull(TEXT("Painter component has the server RPC"), Rpc);
	if (Rpc)
	{
		TestTrue(TEXT("ServerRequestPaint is a server RPC"), Rpc->HasAnyFunctionFlags(FUNC_NetServer));
		bool bTakesPlayer = false;
		for (TFieldIterator<FProperty> It(Rpc); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(*It))
			{
				bTakesPlayer = bTakesPlayer || (Object->PropertyClass && Object->PropertyClass->IsChildOf(APlayerState::StaticClass()));
			}
		}
		TestFalse(TEXT("The client cannot name the payer"), bTakesPlayer);
	}

	UVoxelFinishPainterComponent* Painter = NewObject<UVoxelFinishPainterComponent>(GetTransientPackage());
	TestNull(TEXT("No owner means no payer"), Painter->ResolvePayer());
	Painter->RequestPaint(FIntVector(1, 1, 1), MakeSampleFinish());
	TestTrue(TEXT("Painter replicates by default"), Painter->GetIsReplicated());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishValidationTest, "VoxelFinish.Paint.Validation", VoxelFinishTestFlags)
bool FVoxelFinishValidationTest::RunTest(const FString&)
{
	auto Paint = [](FVoxelFinishSession& Session, const FFinish& Finish)
	{
		return static_cast<int32>(Session.ServerPaint(FIntVector(0, 0, 0), Finish, nullptr));
	};
	const int32 Invalid = static_cast<int32>(EVoxelFinishPaintResult::RejectedInvalidFinish);
	const int32 Applied = static_cast<int32>(EVoxelFinishPaintResult::Applied);

	int32 Spent = 0;
	FVoxelFinishSession Session(true);
	Session.SetCanAffordAndSpend([&Spent](const FFinish&, APlayerState*)
	{
		++Spent;
		return true;
	});

	FFinish Unknown = MakeSampleFinish();
	Unknown.EffectId = 77;
	TestEqual(TEXT("Unregistered effect id is rejected"), Paint(Session, Unknown), Invalid);
	TestEqual(TEXT("Sparkle is not allowed until registered"), Paint(Session, MakeSampleFinish()), Invalid);
	Session.AllowEffectId(VoxelFinish::SparkleEffectId);
	TestTrue(TEXT("No effect is always allowed"), Session.IsEffectIdAllowed(INDEX_NONE));

	FFinish BadPattern = MakeSampleFinish();
	BadPattern.PatternId = 16;
	TestEqual(TEXT("Pattern 16 is rejected"), Paint(Session, BadPattern), Invalid);
	BadPattern.PatternId = -1;
	TestEqual(TEXT("Pattern -1 is rejected"), Paint(Session, BadPattern), Invalid);
	BadPattern.PatternId = 254;
	TestEqual(TEXT("Pattern 254 is rejected"), Paint(Session, BadPattern), Invalid);

	FFinish NotFinite = MakeSampleFinish();
	NotFinite.ColorA.R = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("NaN colour is rejected"), Paint(Session, NotFinite), Invalid);
	NotFinite = MakeSampleFinish();
	NotFinite.Intensity = std::numeric_limits<float>::infinity();
	TestEqual(TEXT("Infinite intensity is rejected"), Paint(Session, NotFinite), Invalid);
	TestEqual(TEXT("Rejected finishes do not spend"), Spent, 0);
	TestEqual(TEXT("Rejected finishes take no palette slot"), Session.GetPalette().NumOccupied(), 0);

	FFinish Solid = MakeSampleFinish();
	Solid.PatternId = VoxelFinish::SolidPatternId;
	TestEqual(TEXT("Solid pattern 255 is accepted"), Paint(Session, Solid), Applied);

	FFinish Loud = MakeSampleFinish();
	Loud.ColorA = FLinearColor(4.f, -2.f, 0.5f, 1.f);
	Loud.Intensity = 50.f;
	TestEqual(TEXT("Out-of-range values are clamped, not rejected"), static_cast<int32>(Session.ServerPaint(FIntVector(1, 0, 0), Loud, nullptr)), Applied);
	const FFinish* Stored = Session.GetPalette().Find(Session.GetGrid().GetFinishId(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Stored colour is clamped"), Stored && Stored->ColorA.R == 1.f && Stored->ColorA.G == 0.f);
	TestTrue(TEXT("Stored intensity is clamped"), Stored && Stored->Intensity == 8.f);

	FEffectGraph Parsed;
	FString Error;
	TestTrue(TEXT("Graph with effect_index parses"), FEffectGraphRegistry::ParseGraphJson(TEXT("{\"id\":\"fx_idx\",\"effect_index\":7,\"nodes\":[{\"id\":\"c\",\"type\":\"const\"}]}"), Parsed, Error));
	TestEqual(TEXT("effect_index is read"), Parsed.EffectIndex, 7);
	FEffectGraphRegistry Registry;
	Registry.Add(Parsed);
	TSet<int32> Indexes;
	Registry.CollectAcceptedEffectIndexes(Indexes);
	TestTrue(TEXT("Accepted graph contributes its effect index"), Indexes.Contains(7));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishGridCountsTest, "VoxelFinish.Grid.Counts", VoxelFinishTestFlags)
bool FVoxelFinishGridCountsTest::RunTest(const FString&)
{
	FVoxelFinishGrid Grid(true);
	const FIntVector ChunkA(0, 0, 0);
	const FIntVector ChunkB(1, 0, 0);
	Grid.SetFinishId(FIntVector(1, 1, 1), 5);
	Grid.SetFinishId(FIntVector(2, 1, 1), 5);
	Grid.SetFinishId(FIntVector(17, 0, 0), 5);
	Grid.SetFinishId(FIntVector(18, 0, 0), 6);
	TestEqual(TEXT("World uses of id 5"), Grid.GetUseCount(5), 3);
	TestEqual(TEXT("Chunk A uses of id 5"), Grid.GetChunkUseCount(ChunkA, 5), 2);
	TestEqual(TEXT("Chunk B uses of id 5"), Grid.GetChunkUseCount(ChunkB, 5), 1);
	TestEqual(TEXT("Painted cells"), Grid.NumPainted(), 4);

	Grid.SetFinishId(FIntVector(1, 1, 1), 6);
	TestEqual(TEXT("Overwrite moves the count"), Grid.GetUseCount(5), 2);
	TestEqual(TEXT("Overwrite adds to the new id"), Grid.GetChunkUseCount(ChunkA, 6), 1);
	Grid.SetFinishId(FIntVector(2, 1, 1), VoxelFinish::UnpaintedId);
	TestEqual(TEXT("Unpaint drops the count"), Grid.GetChunkUseCount(ChunkA, 5), 0);
	TestEqual(TEXT("Unpaint drops a cell"), Grid.NumPainted(), 3);

	TArray<TPair<uint16, uint16>> Delta;
	Grid.ExportChunk(ChunkB, Delta);
	TestEqual(TEXT("Chunk export holds only that chunk"), Delta.Num(), 2);

	Grid.ClearChunk(ChunkB);
	TestEqual(TEXT("ClearChunk removes its uses"), Grid.GetUseCount(5), 0);
	TestEqual(TEXT("ClearChunk keeps other chunks"), Grid.GetUseCount(6), 1);
	TestNull(TEXT("Cleared chunk has no counts"), Grid.GetChunkUseCounts(ChunkB));
	TestEqual(TEXT("ClearChunk drops its cells"), Grid.NumPainted(), 1);

	TMap<uint16, int32> Uses;
	Grid.CountBlockUses(Uses);
	TestEqual(TEXT("Block uses match the cache"), Uses.FindRef(6), 1);
	TestEqual(TEXT("Cell lookup still works"), static_cast<int32>(Grid.GetFinishId(FIntVector(1, 1, 1))), 6);
	TestEqual(TEXT("Negative cells map into their own chunk"), static_cast<int32>(Grid.GetFinishId(FIntVector(-15, 1, 1))), 0);
	return true;
}

static FEffectGraphNode MakeNode(const FString& Id, const FString& Type)
{
	FEffectGraphNode Node;
	Node.Id = Id;
	Node.Type = Type;
	return Node;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishEffectLimitTest, "VoxelFinish.Effect.Limits", VoxelFinishTestFlags)
bool FVoxelFinishEffectLimitTest::RunTest(const FString&)
{
	FEffectGraphRegistry Registry;

	FEffectGraph Leaf;
	Leaf.EffectId = TEXT("leaf");
	Leaf.Revision = 1;
	Leaf.Nodes.Add(MakeNode(TEXT("c"), TEXT("const")));
	TestTrue(TEXT("Register leaf"), Registry.Add(Leaf));
	TestTrue(TEXT("Leaf accepted"), Registry.Validate(TEXT("leaf")).bAccepted);

	FEffectGraph Self;
	Self.EffectId = TEXT("self");
	Self.Revision = 1;
	FEffectGraphNode Loop = MakeNode(TEXT("loop"), TEXT("subeffect"));
	Loop.SubEffectId = TEXT("self");
	Loop.SubEffectRev = 1;
	Self.Nodes.Add(Loop);
	TestTrue(TEXT("Register self cycle"), Registry.Add(Self));
	const FEffectValidation SelfResult = Registry.Validate(TEXT("self"));
	TestFalse(TEXT("Self cycle rejected"), SelfResult.bAccepted);

	FEffectGraph A;
	A.EffectId = TEXT("cycle_a");
	A.Revision = 1;
	FEffectGraphNode ToB = MakeNode(TEXT("to_b"), TEXT("subeffect"));
	ToB.SubEffectId = TEXT("cycle_b");
	ToB.SubEffectRev = 1;
	A.Nodes.Add(ToB);
	FEffectGraph B;
	B.EffectId = TEXT("cycle_b");
	B.Revision = 1;
	FEffectGraphNode ToA = MakeNode(TEXT("to_a"), TEXT("subeffect"));
	ToA.SubEffectId = TEXT("cycle_a");
	ToA.SubEffectRev = 1;
	B.Nodes.Add(ToA);
	TestTrue(TEXT("Register cycle pair"), Registry.Add(A) && Registry.Add(B));
	TestFalse(TEXT("Mutual cycle rejected"), Registry.Validate(TEXT("cycle_a")).bAccepted);

	auto MakeChain = [](const FString& Id, const FString& Child)
	{
		FEffectGraph Graph;
		Graph.EffectId = Id;
		Graph.Revision = 1;
		FEffectGraphNode Node = MakeNode(TEXT("child"), TEXT("subeffect"));
		Node.SubEffectId = Child;
		Node.SubEffectRev = 1;
		Graph.Nodes.Add(Node);
		return Graph;
	};
	TestTrue(TEXT("Depth chain"), Registry.Add(MakeChain(TEXT("d1"), TEXT("leaf"))) && Registry.Add(MakeChain(TEXT("d2"), TEXT("d1"))) && Registry.Add(MakeChain(TEXT("d3"), TEXT("d2"))) && Registry.Add(MakeChain(TEXT("d4"), TEXT("d3"))));
	TestTrue(TEXT("Depth 3 accepted"), Registry.Validate(TEXT("d3")).bAccepted);
	TestEqual(TEXT("Depth 3 records the nest"), Registry.Validate(TEXT("d3")).Depth, 3);
	TestFalse(TEXT("Depth 4 rejected"), Registry.Validate(TEXT("d4")).bAccepted);

	FEffectGraph Wide;
	Wide.EffectId = TEXT("wide");
	Wide.Revision = 1;
	for (int32 Index = 0; Index < 33; ++Index)
	{
		Wide.Nodes.Add(MakeNode(FString::Printf(TEXT("n%d"), Index), TEXT("const")));
	}
	TestTrue(TEXT("Register wide"), Registry.Add(Wide));
	TestFalse(TEXT("33 authored nodes rejected"), Registry.Validate(TEXT("wide")).bAccepted);

	FEffectGraph Left;
	Left.EffectId = TEXT("flat_l");
	Left.Revision = 1;
	FEffectGraph Right = Left;
	Right.EffectId = TEXT("flat_r");
	for (int32 Index = 0; Index < 22; ++Index)
	{
		Left.Nodes.Add(MakeNode(FString::Printf(TEXT("l%d"), Index), TEXT("add")));
		Right.Nodes.Add(MakeNode(FString::Printf(TEXT("r%d"), Index), TEXT("hue_shift")));
	}
	FEffectGraph Combined;
	Combined.EffectId = TEXT("combined");
	Combined.Revision = 1;
	FEffectGraphNode SubL = MakeNode(TEXT("sl"), TEXT("subeffect"));
	SubL.SubEffectId = TEXT("flat_l");
	SubL.SubEffectRev = 1;
	FEffectGraphNode SubR = MakeNode(TEXT("sr"), TEXT("subeffect"));
	SubR.SubEffectId = TEXT("flat_r");
	SubR.SubEffectRev = 1;
	FEffectGraphNode SubL2 = MakeNode(TEXT("sl2"), TEXT("subeffect"));
	SubL2.SubEffectId = TEXT("flat_l");
	SubL2.SubEffectRev = 1;
	Combined.Nodes.Add(SubL);
	Combined.Nodes.Add(SubR);
	Combined.Nodes.Add(SubL2);
	TestTrue(TEXT("Register flatten case"), Registry.Add(Left) && Registry.Add(Right) && Registry.Add(Combined));
	const FEffectValidation Flat = Registry.Validate(TEXT("combined"));
	TestFalse(TEXT("66 flattened nodes rejected"), Flat.bAccepted);
	TestEqual(TEXT("Flatten counted 66 nodes"), Flat.FlattenedNodes, 66);

	FEffectGraph Ops;
	Ops.EffectId = TEXT("costly");
	Ops.Revision = 1;
	for (int32 Index = 0; Index < 32; ++Index)
	{
		Ops.Nodes.Add(MakeNode(FString::Printf(TEXT("h%d"), Index), TEXT("hue_shift")));
	}
	TestTrue(TEXT("Register costly"), Registry.Add(Ops));
	const FEffectValidation Cost = Registry.Validate(TEXT("costly"));
	TestTrue(TEXT("32 hue shifts cost 128 and are accepted"), Cost.bAccepted);
	TestEqual(TEXT("Op cost is 128"), Cost.FlattenedOps, 128);

	FEffectGraph CostChild;
	CostChild.EffectId = TEXT("cost_child");
	CostChild.Revision = 1;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		CostChild.Nodes.Add(MakeNode(FString::Printf(TEXT("c%d"), Index), TEXT("hue_shift")));
	}
	FEffectGraph CostParent;
	CostParent.EffectId = TEXT("cost_parent");
	CostParent.Revision = 1;
	FEffectGraphNode CostA = MakeNode(TEXT("a"), TEXT("subeffect"));
	CostA.SubEffectId = TEXT("cost_child");
	CostA.SubEffectRev = 1;
	FEffectGraphNode CostB = MakeNode(TEXT("b"), TEXT("subeffect"));
	CostB.SubEffectId = TEXT("cost_child");
	CostB.SubEffectRev = 1;
	CostParent.Nodes.Add(CostA);
	CostParent.Nodes.Add(CostB);
	TestTrue(TEXT("Register over-cost graph"), Registry.Add(CostChild) && Registry.Add(CostParent));
	const FEffectValidation OverCost = Registry.Validate(TEXT("cost_parent"));
	TestFalse(TEXT("160 flattened ops are rejected"), OverCost.bAccepted);
	TestEqual(TEXT("Over-cost op total"), OverCost.FlattenedOps, 160);

	FEffectGraph NodeCycle;
	NodeCycle.EffectId = TEXT("node_cycle");
	NodeCycle.Revision = 1;
	FEffectGraphNode CycleNode = MakeNode(TEXT("a"), TEXT("add"));
	CycleNode.Inputs.Add(TEXT("a"));
	NodeCycle.Nodes.Add(CycleNode);
	TestTrue(TEXT("Register node cycle"), Registry.Add(NodeCycle));
	TestFalse(TEXT("Node cycle rejected"), Registry.Validate(TEXT("node_cycle")).bAccepted);

	FString JsonError;
	FEffectGraph Parsed;
	const FString Json = TEXT("{\"effect_id\":\"parsed\",\"rev\":{\"number\":2},\"nodes\":[{\"id\":\"c\",\"type\":\"const\"}]}");
	TestTrue(TEXT("JSON parses"), FEffectGraphRegistry::ParseGraphJson(Json, Parsed, JsonError));
	TestEqual(TEXT("Parsed revision"), Parsed.Revision, 2);
	TestTrue(TEXT("Parsed graph registers"), Registry.Add(Parsed));
	TestTrue(TEXT("Parsed graph accepted"), Registry.Validate(TEXT("parsed"), 2).bAccepted);

	FString LoadError;
	const int32 Loaded = Registry.LoadStarterGraphs(&LoadError);
	TestTrue(TEXT("Starter graphs loaded"), Loaded > 0);
	if (Loaded > 0)
	{
		const FEffectValidation Starter = Registry.Validate(TEXT("fx_shimmer_over_pulse"), 1);
		TestTrue(FString::Printf(TEXT("Starter composite accepted (%s)"), *Starter.Reason), Starter.bAccepted);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishLookupTest, "VoxelFinish.LookupBake", VoxelFinishTestFlags)
bool FVoxelFinishLookupTest::RunTest(const FString&)
{
	TArray<FColor> Pixels;
	FVoxelFinishLookup::InitPixels(Pixels);
	FFinish Finish = MakeSampleFinish();
	Finish.bGlow = true;
	FVoxelFinishLookup::WriteFinish(Pixels, 7, Finish);
	const FColor ColorA = FVoxelFinishLookup::Read(Pixels, 7, FVoxelFinishLookup::RowColorA);
	TestEqual(TEXT("Baked color A red"), static_cast<int32>(ColorA.R), 255);
	TestEqual(TEXT("Baked color A green empty"), static_cast<int32>(ColorA.G), 0);
	const FColor ColorB = FVoxelFinishLookup::Read(Pixels, 7, FVoxelFinishLookup::RowColorB);
	TestEqual(TEXT("Glow flag baked in color B alpha"), static_cast<int32>(ColorB.A), 255);
	const FColor Meta = FVoxelFinishLookup::Read(Pixels, 7, FVoxelFinishLookup::RowMeta);
	TestEqual(TEXT("Sparkle effect baked"), static_cast<int32>(Meta.R), 255);
	TestEqual(TEXT("Pattern id baked"), static_cast<int32>(Meta.G), 3);
	TestEqual(TEXT("Unpainted column stays empty"), static_cast<int32>(FVoxelFinishLookup::Read(Pixels, 0, 0).R), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishContentTest, "VoxelFinish.Content.MasterMaterials", VoxelFinishTestFlags)
bool FVoxelFinishContentTest::RunTest(const FString&)
{
	// Read-only: this test never imports or saves assets. Build missing content with the VoxelFinish.BuildContent console command.
	UTexture2D* Lookup = LoadObject<UTexture2D>(nullptr, TEXT("/VoxelFinish/Textures/Atlas/T_FinishLookup.T_FinishLookup"));
	UTexture2D* Pattern = LoadObject<UTexture2D>(nullptr, TEXT("/VoxelFinish/Textures/Atlas/T_PatternAtlas.T_PatternAtlas"));
	UTexture2D* Sparkle = LoadObject<UTexture2D>(nullptr, TEXT("/VoxelFinish/Textures/Flipbooks/T_SparkleFlipbook.T_SparkleFlipbook"));
	UMaterial* Opaque = LoadObject<UMaterial>(nullptr, TEXT("/VoxelFinish/Materials/M_FinishOpaque.M_FinishOpaque"));
	UMaterial* Emissive = LoadObject<UMaterial>(nullptr, TEXT("/VoxelFinish/Materials/M_FinishEmissive.M_FinishEmissive"));
	TestNotNull(TEXT("Lookup texture"), Lookup);
	TestNotNull(TEXT("Pattern atlas"), Pattern);
	TestNotNull(TEXT("Sparkle flipbook"), Sparkle);
	TestNotNull(TEXT("Opaque master"), Opaque);
	TestNotNull(TEXT("Emissive master"), Emissive);
	if (!Lookup || !Pattern || !Sparkle || !Opaque || !Emissive)
	{
		AddError(TEXT("VoxelFinish content is missing. Run the VoxelFinish.BuildContent editor console command, then re-run the test."));
		return false;
	}
	if (Lookup)
	{
		TestEqual(TEXT("Lookup width"), static_cast<int64>(Lookup->Source.GetSizeX()), static_cast<int64>(VoxelFinish::LookupTextureWidth));
		TestEqual(TEXT("Lookup nearest"), static_cast<int32>(Lookup->Filter), static_cast<int32>(TF_Nearest));
	}
	if (Pattern)
	{
		TestEqual(TEXT("Pattern nearest"), static_cast<int32>(Pattern->Filter), static_cast<int32>(TF_Nearest));
	}
	if (Opaque && Opaque->GetEditorOnlyData())
	{
		TestTrue(TEXT("Opaque base color connected"), Opaque->GetEditorOnlyData()->BaseColor.IsConnected());
	}
	if (Emissive && Emissive->GetEditorOnlyData())
	{
		TestTrue(TEXT("Emissive color connected"), Emissive->GetEditorOnlyData()->EmissiveColor.IsConnected());
	}

	// Each master reads the lookup through the FinishLookup texture parameter, so a volume can bind its own
	// transient lookup (AVoxelFinishVolume::ApplyLookupTo). No plain sample may read T_FinishLookup directly.
	for (UMaterial* Master : { Opaque, Emissive })
	{
		int32 LookupParameters = 0;
		int32 DirectLookupSamples = 0;
		for (const TObjectPtr<UMaterialExpression>& Expression : Master->GetExpressions())
		{
			const UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(Expression.Get());
			if (!Sample)
			{
				continue;
			}
			const UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Sample);
			if (Parameter && Parameter->ParameterName == FVoxelFinishLookup::ParameterName)
			{
				++LookupParameters;
			}
			else if (Sample->Texture == Lookup)
			{
				++DirectLookupSamples;
			}
		}
		TestTrue(FString::Printf(TEXT("%s samples the FinishLookup parameter"), *Master->GetName()), LookupParameters > 0);
		TestEqual(FString::Printf(TEXT("%s has no direct T_FinishLookup sample"), *Master->GetName()), DirectLookupSamples, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishRateLimitTest, "VoxelFinish.Paint.RateLimit", VoxelFinishTestFlags)
bool FVoxelFinishRateLimitTest::RunTest(const FString&)
{
	FVoxelFinishRateLimiter Limiter;
	int32 Allowed = 0;
	for (int32 i = 0; i < 15; ++i)
	{
		Allowed += Limiter.TryConsume(100.0, 10.f) ? 1 : 0;
	}
	TestEqual(TEXT("A full bucket lets 10 requests through at once"), Allowed, 10);
	TestFalse(TEXT("Half a token after 50 ms is not enough"), Limiter.TryConsume(100.05, 10.f));
	TestTrue(TEXT("One token after 100 ms"), Limiter.TryConsume(100.1, 10.f));
	TestFalse(TEXT("And only one"), Limiter.TryConsume(100.1, 10.f));
	TestFalse(TEXT("Time going backwards adds nothing"), Limiter.TryConsume(50.0, 10.f));
	Allowed = 0;
	for (int32 i = 0; i < 30; ++i)
	{
		Allowed += Limiter.TryConsume(1000.0, 10.f) ? 1 : 0;
	}
	TestEqual(TEXT("A long pause refills to the cap, not past it"), Allowed, 10);

	FVoxelFinishRateLimiter Slow;
	TestTrue(TEXT("Below 1 per second the bucket still holds one request"), Slow.TryConsume(0.0, 0.5f));
	TestFalse(TEXT("Then it waits"), Slow.TryConsume(1.0, 0.5f));
	TestTrue(TEXT("Two seconds at 0.5 per second"), Slow.TryConsume(2.0, 0.5f));

	FVoxelFinishRateLimiter Off;
	Allowed = 0;
	for (int32 i = 0; i < 100; ++i)
	{
		Allowed += Off.TryConsume(0.0, 0.f) ? 1 : 0;
	}
	TestEqual(TEXT("A rate of 0 means no limit"), Allowed, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFinishPainterGuardsTest, "VoxelFinish.Paint.ServerGuards", VoxelFinishTestFlags)
bool FVoxelFinishPainterGuardsTest::RunTest(const FString&)
{
	// A standalone game world, so the painter's owner has authority and requests take the server path directly.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	AVoxelFinishVolume* Volume = World->SpawnActor<AVoxelFinishVolume>();
	APlayerState* State = World->SpawnActor<APlayerState>();
	APawn* Pawn = World->SpawnActor<APawn>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Volume"), Volume) || !TestNotNull(TEXT("PlayerState"), State) || !TestNotNull(TEXT("Pawn"), Pawn))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Pawn->SetPlayerState(State);
	int32 Spent = 0;
	Volume->SetCanAffordAndSpend([&Spent](const FFinish&, APlayerState* Payer)
	{
		++Spent;
		return Payer != nullptr;
	});

	auto MakePainter = [&](float Rate)
	{
		UVoxelFinishPainterComponent* P = NewObject<UVoxelFinishPainterComponent>(State);
		P->RegisterComponent();
		P->SetVolume(Volume);
		P->MaxPaintsPerSecond = Rate;
		return P;
	};
	TArray<TPair<FIntVector, uint8>> Rejected;
	auto Listen = [&Rejected](UVoxelFinishPainterComponent* P)
	{
		P->OnPaintRejected.AddLambda([&Rejected](FIntVector Cell, uint8 Result) { Rejected.Add({ Cell, Result }); });
	};
	auto LastRejected = [&Rejected]() { return Rejected.Num() > 0 ? static_cast<int32>(Rejected.Last().Value) : -1; };
	const FFinish Finish = [] { FFinish F; F.ColorA = FLinearColor(0.2f, 0.6f, 0.3f); return F; }();
	const FVoxelFinishGrid& Grid = Volume->GetSession().GetGrid();

	UVoxelFinishPainterComponent* Painter = MakePainter(0.f);
	Listen(Painter);
	TestEqual(TEXT("Default reach is 600 cm"), GetDefault<UVoxelFinishPainterComponent>()->MaxReachCm, 600.f);
	TestEqual(TEXT("Default rate is 10 per second"), GetDefault<UVoxelFinishPainterComponent>()->MaxPaintsPerSecond, 10.f);
	TestTrue(TEXT("Reach is measured from the PlayerState's pawn"), Painter->ResolvePawn() == Pawn);

	FVector Center;
	TestFalse(TEXT("No resolver: no cell is a block"), Volume->ResolvePaintTarget(FIntVector(1, 1, 1), Center));
	Painter->RequestPaint(FIntVector(1, 1, 1), Finish);
	TestEqual(TEXT("No resolver: refused as no block"), LastRejected(), static_cast<int32>(EVoxelFinishPaintResult::RejectedNoBlock));

	// Solid blocks: (1,1,1) next to the pawn, (9,0,0) 900 cm away, (20,0,0) 2000 cm away. Everything else is air.
	Volume->SetPaintTargetResolver([](const FIntVector& Cell, FVector& OutCenter)
	{
		if (Cell == FIntVector(1, 1, 1) || Cell == FIntVector(9, 0, 0) || Cell == FIntVector(2, 1, 1) || Cell == FIntVector(3, 1, 1) || Cell == FIntVector(20, 0, 0))
		{
			OutCenter = (FVector(Cell) + FVector(0.5)) * 100.0;
			return true;
		}
		return false;
	});
	Painter->RequestPaint(FIntVector(5, 5, 5), Finish);
	TestEqual(TEXT("Air is refused"), LastRejected(), static_cast<int32>(EVoxelFinishPaintResult::RejectedNoBlock));
	Painter->RequestPaint(FIntVector(-4000, 7, 7), Finish);
	TestEqual(TEXT("A far-away empty cell is refused"), LastRejected(), static_cast<int32>(EVoxelFinishPaintResult::RejectedNoBlock));
	Painter->RequestPaint(FIntVector(9, 0, 0), Finish);
	TestEqual(TEXT("A block 900 cm away is out of reach"), LastRejected(), static_cast<int32>(EVoxelFinishPaintResult::RejectedOutOfReach));
	TestEqual(TEXT("Refused requests spend nothing"), Spent, 0);
	TestEqual(TEXT("Refused requests paint nothing"), Grid.NumPainted(), 0);
	TestNull(TEXT("Painting air never creates a paint chunk"), Grid.GetChunkUseCounts(VoxelFinish::CellToChunk(FIntVector(-4000, 7, 7))));
	TestEqual(TEXT("No paint chunk is dirty"), Grid.GetDirtyChunks().Num(), 0);

	const int32 RejectedBefore = Rejected.Num();
	Painter->RequestPaint(FIntVector(1, 1, 1), Finish);
	TestEqual(TEXT("A block in reach is painted"), Rejected.Num(), RejectedBefore);
	TestTrue(TEXT("Cell holds the finish"), Grid.GetFinishId(FIntVector(1, 1, 1)) != VoxelFinish::UnpaintedId);
	TestEqual(TEXT("Spent once"), Spent, 1);
	Painter->MaxReachCm = 1000.f;
	Painter->RequestPaint(FIntVector(9, 0, 0), Finish);
	TestTrue(TEXT("Reach is tunable"), Grid.GetFinishId(FIntVector(9, 0, 0)) != VoxelFinish::UnpaintedId);
	const int32 OutOfReach = static_cast<int32>(EVoxelFinishPaintResult::RejectedOutOfReach);
	Painter->RequestPaint(FIntVector(20, 0, 0), Finish);
	TestEqual(TEXT("A block 2000 cm away is past 1000 cm"), LastRejected(), OutOfReach);

	// A bound provider is read at paint time and wins over MaxReachCm; <= 0 means no limit.
	float ProvidedReach = 500.f;
	Painter->SetMaxReachProvider([&ProvidedReach]() { return ProvidedReach; });
	TestEqual(TEXT("Effective reach comes from the provider"), Painter->GetEffectiveMaxReachCm(), 500.f);
	Rejected.Reset();
	Painter->RequestPaint(FIntVector(9, 0, 0), Finish);
	TestEqual(TEXT("Provider reach of 500 cm refuses a block 900 cm away"), LastRejected(), OutOfReach);
	ProvidedReach = 0.f;
	Painter->RequestPaint(FIntVector(20, 0, 0), Finish);
	TestTrue(TEXT("Provider reach of 0 means no limit"), Grid.GetFinishId(FIntVector(20, 0, 0)) != VoxelFinish::UnpaintedId);
	TestEqual(TEXT("Only the 500 cm request was refused"), Rejected.Num(), 1);
	Painter->SetMaxReachProvider(nullptr);
	Painter->MaxReachCm = 0.f;
	TestEqual(TEXT("Unbound: MaxReachCm again"), Painter->GetEffectiveMaxReachCm(), 0.f);
	Painter->RequestPaint(FIntVector(20, 0, 0), Finish);
	TestEqual(TEXT("MaxReachCm of 0 means no limit (already painted, not refused)"), Rejected.Num(), 1);
	TestEqual(TEXT("Spent for the two applied paints"), Spent, 3);

	// A second player-owned painter with 3 requests per second: the 4th and 5th in the same instant are dropped quietly.
	UVoxelFinishPainterComponent* Limited = MakePainter(3.f);
	Listen(Limited);
	Rejected.Reset();
	Limited->RequestPaint(FIntVector(2, 1, 1), Finish);
	Limited->RequestPaint(FIntVector(3, 1, 1), Finish);
	Limited->RequestPaint(FIntVector(5, 5, 5), Finish); // refused (air), still counts against the limit
	Limited->RequestPaint(FIntVector(1, 1, 1), Finish);
	Limited->RequestPaint(FIntVector(1, 1, 1), Finish);
	TestEqual(TEXT("Only the air request is reported back"), Rejected.Num(), 1);
	TestEqual(TEXT("...as no block"), LastRejected(), static_cast<int32>(EVoxelFinishPaintResult::RejectedNoBlock));
	TestEqual(TEXT("The 4th and 5th requests were rate limited"), Limited->GetNumRateLimited(), 2);
	TestEqual(TEXT("The unlimited painter never was"), Painter->GetNumRateLimited(), 0);
	TestTrue(TEXT("Requests inside the limit were painted"), Grid.GetFinishId(FIntVector(2, 1, 1)) != VoxelFinish::UnpaintedId && Grid.GetFinishId(FIntVector(3, 1, 1)) != VoxelFinish::UnpaintedId);
	TestEqual(TEXT("Rate-limited requests spend nothing"), Spent, 5);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
