// Hub gates: each mini-game resolves to a map that exists or to a sim on its own pad.
// Beat Hop and Depths keep their own names. Pads stay off the Rime hub and off each other.

#include "Misc/AutomationTest.h"
#include "GFBeatHopGateSubsystem.h"
#include "GFFashionGateSubsystem.h"
#include "GFHubGateDirector.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFMobaGateSubsystem.h"
#include "GFTamingGateSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFHubGateTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorldScope
	{
		UWorld* World = nullptr;

		FWorldScope()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFGateWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static int32 CountRole(UWorld* World, EGFMiniGameGateRole Role)
	{
		int32 Count = 0;
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			Count += It->GateRole == Role ? 1 : 0;
		}
		return Count;
	}

	/** Bare worlds skip BeginPlay. ApplyVisuals is the same call Place() makes after FinishSpawning. */
	static void FinishSpawnedGate(AGFMiniGameGate* Gate, const FTransform& Xform)
	{
		if (!Gate)
		{
			return;
		}
		Gate->FinishSpawning(Xform);
		Gate->ApplyVisuals();
	}

	static AGFMiniGameGate* FindSpawnedGate(UWorld* World, FName Id, EGFMiniGameGateRole Role)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == Id && It->GateRole == Role)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static int32 CountClass(UWorld* World, const FString& ClassPath)
	{
		UClass* Class = FSoftClassPath(ClassPath).TryLoadClass<AActor>();
		if (!Class)
		{
			return 0;
		}
		int32 Count = 0;
		for (TActorIterator<AActor> It(World, Class); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	static bool Separated(const FVector& A, const FVector2D& HalfA, const FVector& B, const FVector2D& HalfB, float Gap)
	{
		const float GapX = FMath::Abs(A.X - B.X) - HalfA.X - HalfB.X;
		const float GapY = FMath::Abs(A.Y - B.Y) - HalfA.Y - HalfB.Y;
		return GapX >= Gap || GapY >= Gap;
	}

	static float DistanceToDisc(const FVector& Center, const FVector2D& Half, const FVector& DiscCenter)
	{
		const float Dx = FMath::Max(0.f, FMath::Abs(Center.X - DiscCenter.X) - Half.X);
		const float Dy = FMath::Max(0.f, FMath::Abs(Center.Y - DiscCenter.Y) - Half.Y);
		return FMath::Sqrt(Dx * Dx + Dy * Dy);
	}

	static bool FloorBlocks(const UStaticMeshComponent* Floor)
	{
		if (!Floor || Floor->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		{
			return false;
		}
		return Floor->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
			&& Floor->GetCollisionResponseToChannel(ECC_WorldStatic) == ECR_Block;
	}

	static bool IsPadArt(const UStaticMeshComponent* Floor)
	{
		return Floor && Floor->GetStaticMesh() && Floor->GetStaticMesh()->GetPathName() == FGFMiniGameCatalog::PadObject;
	}

	/**
	 * The slab covers the pad and its walk surface sits on the marker origin.
	 * Engine cube: 100 cm and centred, so the top of its box is the surface.
	 * SM_GatePad: the origin is the surface (its pylons rise above it), so the relative Z is the surface.
	 */
	static bool FloorCoversPad(const UStaticMeshComponent* Floor, const FGFMiniGameEntry& Entry)
	{
		if (!FloorBlocks(Floor) || !Floor->GetStaticMesh() || Floor->bHiddenInGame)
		{
			return false;
		}
		const FVector Scale = Floor->GetRelativeScale3D();
		const FBox Box = Floor->GetStaticMesh()->GetBoundingBox();
		const float RelZ = Floor->GetRelativeLocation().Z;
		const float HalfX = Box.GetExtent().X * Scale.X;
		const float HalfY = Box.GetExtent().Y * Scale.Y;
		const float TopZ = IsPadArt(Floor) ? RelZ : RelZ + Box.Max.Z * Scale.Z;
		const float Thickness = TopZ - (RelZ + Box.Min.Z * Scale.Z);
		return HalfX + 0.5f >= Entry.PadHalfExtent.X
			&& HalfY + 0.5f >= Entry.PadHalfExtent.Y
			&& Thickness + 0.5f >= FGFMiniGameCatalog::PadFloorThicknessCm
			&& FMath::IsNearlyEqual(TopZ, 0.f, 1.f)
			&& !Floor->CanEverAffectNavigation();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGateCatalogTest, "GFGame.Gates.Catalog", GFHubGateTests::Flags)
bool FGFHubGateCatalogTest::RunTest(const FString&)
{
	using namespace GFHubGateTests;
	const TArray<FGFMiniGameEntry>& Entries = FGFMiniGameCatalog::All();
	TestEqual(TEXT("Eight mini-games"), Entries.Num(), 8);

	const TCHAR* Ids[] = {
		TEXT("Taming"), TEXT("Fashion"), TEXT("Moba"), TEXT("BeatHop"),
		TEXT("Arena"), TEXT("Depths"), TEXT("Mine"), TEXT("Lab")
	};
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Id %d"), i), Entries[i].Id.ToString(), FString(Ids[i]));
		TestEqual(FString::Printf(TEXT("Ring %d"), i), Entries[i].RingIndex, i);
		TestTrue(TEXT("Map or sim, not both and not neither"), Entries[i].HasMap() != Entries[i].HasSim());
		TestFalse(TEXT("No leftover arena map path"), Entries[i].Map.ToString().Contains(TEXT("L_Arena")));
	}

	const FGFMiniGameEntry* Beat = FGFMiniGameCatalog::Find(TEXT("BeatHop"));
	const FGFMiniGameEntry* Depths = FGFMiniGameCatalog::Find(TEXT("Depths"));
	const FGFMiniGameEntry* Moba = FGFMiniGameCatalog::Find(TEXT("Moba"));
	if (!TestNotNull(TEXT("Beat Hop row"), Beat) || !TestNotNull(TEXT("Depths row"), Depths) || !TestNotNull(TEXT("MOBA row"), Moba))
	{
		return false;
	}
	TestEqual(TEXT("Beat Hop sign"), Beat->Label, FString(TEXT("Beat Hop")));
	TestEqual(TEXT("Depths sign"), Depths->Label, FString(TEXT("Depths")));
	TestNotEqual(TEXT("Beat Hop is not Depths"), Beat->Label, Depths->Label);
	TestFalse(TEXT("Beat Hop is not the platformer label"), Beat->Label.Equals(TEXT("Platformer")) || Beat->Label.Equals(TEXT("Depths")));
	TestFalse(TEXT("Depths is not Beat Hop"), Depths->Label.Equals(TEXT("Beat Hop")));
	TestEqual(TEXT("Beat Hop map"), Beat->Map.ToString(), FString(TEXT("/Game/Maps/L_Beat")));
	TestTrue(TEXT("Depths is a sim"), Depths->HasSim() && !Depths->HasMap());
	TestEqual(TEXT("MOBA map"), Moba->Map.ToString(), FString(TEXT("/Game/Maps/L_Moba")));
	TestEqual(TEXT("Crystal Clash sign"), Moba->Label, FString(TEXT("Crystal Clash")));
	TestEqual(TEXT("Crystal Clash keeps the Moba id"), Moba->Id, FName(TEXT("Moba")));
	TestFalse(TEXT("Sign does not say MOBA"), Moba->Label.Equals(TEXT("MOBA")));

	TestEqual(TEXT("Taming gate label"), GetDefault<UGFTamingGateSubsystem>()->GetDisplayName().ToString(), FGFMiniGameCatalog::Find(TEXT("Taming"))->Label);
	TestEqual(TEXT("Runway gate label"), GetDefault<UGFFashionGateSubsystem>()->GetDisplayName().ToString(), FGFMiniGameCatalog::Find(TEXT("Fashion"))->Label);
	TestEqual(TEXT("MOBA gate label"), GetDefault<UGFMobaGateSubsystem>()->GetDisplayName().ToString(), Moba->Label);
	TestEqual(TEXT("Beat Hop gate label"), GetDefault<UGFBeatHopGateSubsystem>()->GetDisplayName().ToString(), Beat->Label);
	TestEqual(TEXT("MOBA subsystem map"), GetDefault<UGFMobaGateSubsystem>()->GetMapName().ToString(), Moba->Map.ToString());
	TestEqual(TEXT("Beat Hop subsystem map"), GetDefault<UGFBeatHopGateSubsystem>()->GetMapName().ToString(), Beat->Map.ToString());

	FString Script;
	const FString ScriptPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/create_maps.py"));
	TestTrue(TEXT("create_maps.py loads"), FFileHelper::LoadFileToString(Script, *ScriptPath));
	TestFalse(TEXT("create_maps.py does not build L_Arena"), Script.Contains(TEXT("/Game/Maps/L_Arena")));

	for (const FGFMiniGameEntry& Entry : Entries)
	{
		if (Entry.HasMap())
		{
			TestTrue(TEXT("Map package is on disk"), FPackageName::DoesPackageExist(Entry.Map.ToString()));
			TestTrue(TEXT("create_maps.py lists the map"), Script.Contains(Entry.Map.ToString()));
			TestFalse(TEXT("Map modes have no pad"), Entry.bHasPad);
			TestTrue(TEXT("Map modes have no director class"), Entry.DirectorClass.IsEmpty());
		}
		else
		{
			TestFalse(TEXT("Sim class path"), Entry.DirectorClass.IsEmpty());
			TestNotNull(TEXT("Sim director class loads"), FSoftClassPath(Entry.DirectorClass).TryLoadClass<AActor>());
			TestTrue(TEXT("Sim has a pad"), Entry.bHasPad && Entry.PadHalfExtent.X > 0.f && Entry.PadHalfExtent.Y > 0.f);
		}
		if (Entry.bExpectArt)
		{
			TestTrue(*FString::Printf(TEXT("%s art package"), *Entry.Id.ToString()), FPackageName::DoesPackageExist(FGFMiniGameCatalog::ArtPackage(Entry)));
			TestTrue(TEXT("Expected art lives under Content/Art/Gates"), FGFMiniGameCatalog::ArtPackage(Entry).Contains(TEXT("/Game/Art/Gates/")));
		}
	}

	const FName Hub = FGFMiniGameCatalog::HubMap();
	TestTrue(TEXT("Hub package"), FGFMiniGameCatalog::WorldIs(TEXT("/Game/Maps/L_Hub"), Hub));
	TestTrue(TEXT("PIE hub package"), FGFMiniGameCatalog::WorldIs(TEXT("/Game/Maps/UEDPIE_0_L_Hub"), Hub));
	TestFalse(TEXT("Beat map is not the hub"), FGFMiniGameCatalog::WorldIs(TEXT("/Game/Maps/L_Beat"), Hub));
	TestFalse(TEXT("PIE beat map is not the hub"), FGFMiniGameCatalog::WorldIs(TEXT("/Game/Maps/UEDPIE_0_L_Beat"), Hub));
	TestTrue(TEXT("PIE MOBA map"), FGFMiniGameCatalog::WorldIs(TEXT("/Game/Maps/UEDPIE_1_L_Moba"), Moba->Map));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGatePadTest, "GFGame.Gates.Pads", GFHubGateTests::Flags)
bool FGFHubGatePadTest::RunTest(const FString&)
{
	const FVector Rime = FVector::ZeroVector;
	TArray<const FGFMiniGameEntry*> Pads;
	for (const FGFMiniGameEntry& Entry : FGFMiniGameCatalog::All())
	{
		if (!Entry.bHasPad)
		{
			continue;
		}
		Pads.Add(&Entry);
		const FVector Origin = FGFMiniGameCatalog::PadOrigin(Entry, Rime);
		TestFalse(*FString::Printf(TEXT("%s pad is not the hub origin"), *Entry.Id.ToString()), Origin.IsNearlyZero(1.f));
		const float Clear = GFHubGateTests::DistanceToDisc(Origin, Entry.PadHalfExtent, Rime);
		TestTrue(*FString::Printf(TEXT("%s pad clears the hub"), *Entry.Id.ToString()), Clear > FGFMiniGameCatalog::HubClearanceRadiusCm);
	}
	TestEqual(TEXT("Four mapless sims"), Pads.Num(), 4);
	for (int32 i = 0; i < Pads.Num(); ++i)
	{
		const FVector A = FGFMiniGameCatalog::PadOrigin(*Pads[i], Rime);
		for (int32 j = i + 1; j < Pads.Num(); ++j)
		{
			const FVector B = FGFMiniGameCatalog::PadOrigin(*Pads[j], Rime);
			TestTrue(
				*FString::Printf(TEXT("%s and %s pads do not overlap"), *Pads[i]->Id.ToString(), *Pads[j]->Id.ToString()),
				GFHubGateTests::Separated(A, Pads[i]->PadHalfExtent, B, Pads[j]->PadHalfExtent, 100.f));
		}
	}

	// Locked stand-in until the Decision issue is overruled. These Y values sit outside the
	// voxel floor (about ±5400), so each pad marker carries its own slab. A shifted anchor moves the pad with it.
	const FGFMiniGameEntry* Arena = FGFMiniGameCatalog::Find(TEXT("Arena"));
	const FGFMiniGameEntry* Depths = FGFMiniGameCatalog::Find(TEXT("Depths"));
	const FGFMiniGameEntry* Mine = FGFMiniGameCatalog::Find(TEXT("Mine"));
	const FGFMiniGameEntry* Lab = FGFMiniGameCatalog::Find(TEXT("Lab"));
	if (!TestNotNull(TEXT("Arena row"), Arena) || !TestNotNull(TEXT("Depths row"), Depths) || !TestNotNull(TEXT("Mine row"), Mine) || !TestNotNull(TEXT("Lab row"), Lab))
	{
		return false;
	}
	TestTrue(TEXT("Arena pad"), FGFMiniGameCatalog::PadOrigin(*Arena, Rime).Equals(FVector(0.f, 9000.f, 0.f), 1.f));
	TestTrue(TEXT("Depths pad"), FGFMiniGameCatalog::PadOrigin(*Depths, Rime).Equals(FVector(0.f, 15000.f, 0.f), 1.f));
	TestTrue(TEXT("Mine pad"), FGFMiniGameCatalog::PadOrigin(*Mine, Rime).Equals(FVector(0.f, 21000.f, 0.f), 1.f));
	TestTrue(TEXT("Lab pad"), FGFMiniGameCatalog::PadOrigin(*Lab, Rime).Equals(FVector(0.f, 27000.f, 0.f), 1.f));
	const FVector Shifted(1000.f, -250.f, 0.f);
	TestTrue(TEXT("Pads follow the Rime anchor"), FGFMiniGameCatalog::PadOrigin(*Depths, Shifted).Equals(FVector(1000.f, 14750.f, 0.f), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGateRingTest, "GFGame.Gates.Ring", GFHubGateTests::Flags)
bool FGFHubGateRingTest::RunTest(const FString&)
{
	const FVector Rime(0.f, 0.f, 0.f);
	const int32 Count = FGFMiniGameCatalog::All().Num();
	const FVector East = FGFMiniGameCatalog::GateLocation(Rime, 0, Count);
	TestTrue(TEXT("First gate is east of Rime"), East.Equals(FVector(FGFMiniGameCatalog::GateRingRadiusCm, 0.f, 0.f), 0.5f));
	const FRotator Facing = FGFMiniGameCatalog::GateFacing(East, Rime);
	TestTrue(TEXT("East gate faces the hub"), FMath::Abs(FMath::FindDeltaAngleDegrees(Facing.Yaw, 180.f)) < 0.5f);

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector A = FGFMiniGameCatalog::GateLocation(Rime, i, Count);
		TestTrue(TEXT("On the ring"), FMath::IsNearlyEqual(FVector::Dist2D(A, Rime), FGFMiniGameCatalog::GateRingRadiusCm, 0.5f));
		for (int32 j = i + 1; j < Count; ++j)
		{
			const FVector B = FGFMiniGameCatalog::GateLocation(Rime, j, Count);
			TestTrue(TEXT("Gates are not stacked"), FVector::Dist2D(A, B) > 400.f);
		}
	}
	const FVector Moved = FGFMiniGameCatalog::GateLocation(FVector(25.f, -40.f, 0.f), 0, Count);
	TestTrue(TEXT("Ring follows Rime"), Moved.Equals(FVector(25.f + FGFMiniGameCatalog::GateRingRadiusCm, -40.f, 0.f), 0.5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGateActorTest, "GFGame.Gates.Actors", GFHubGateTests::Flags)
bool FGFHubGateActorTest::RunTest(const FString&)
{
	using namespace GFHubGateTests;
	FWorldScope Scope;
	if (!TestNotNull(TEXT("World"), Scope.World))
	{
		return false;
	}

	for (const FGFMiniGameEntry& Entry : FGFMiniGameCatalog::All())
	{
		const FTransform Xform(FRotator::ZeroRotator, FVector(Entry.RingIndex * 100.f, 0.f, 0.f));
		AGFMiniGameGate* Gate = Scope.World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), Xform);
		if (!TestNotNull(*Entry.Id.ToString(), Gate))
		{
			continue;
		}
		Gate->EntryId = Entry.Id;
		Gate->GateRole = EGFMiniGameGateRole::Portal;
		Gate->RimeCenter = FVector::ZeroVector;
		FinishSpawnedGate(Gate, Xform);
		TestEqual(*FString::Printf(TEXT("%s label"), *Entry.Id.ToString()), Gate->GetLabel(), Entry.Label);
		TestEqual(*FString::Printf(TEXT("%s sign text"), *Entry.Id.ToString()), Gate->GetSignText().ToString(), Entry.Label);
		const bool bArt = !Entry.ArtObject.IsEmpty() && FPackageName::DoesPackageExist(FGFMiniGameCatalog::ArtPackage(Entry));
		TestEqual(*FString::Printf(TEXT("%s placeholder matches missing art"), *Entry.Id.ToString()), Gate->IsUsingPlaceholder(), !bArt);
		// GetStaticMesh returns TObjectPtr. TestNotNull only accepts a raw pointer.
		UStaticMesh* MeshAsset = nullptr;
		if (UStaticMeshComponent* GateMesh = Gate->GetGateMesh())
		{
			MeshAsset = GateMesh->GetStaticMesh().Get();
		}
		TestNotNull(*FString::Printf(TEXT("%s mesh"), *Entry.Id.ToString()), MeshAsset);
		TestFalse(*FString::Printf(TEXT("%s portal has no solid floor"), *Entry.Id.ToString()), FloorBlocks(Gate->GetPadFloor()));
		if (Entry.bExpectArt)
		{
			TestFalse(*FString::Printf(TEXT("%s uses its gate mesh"), *Entry.Id.ToString()), Gate->IsUsingPlaceholder());
		}
	}

	const FVector Rime(40.f, -15.f, 0.f);
	const FTransform DepthsXform(FRotator::ZeroRotator, FVector(0.f, 0.f, 0.f));
	AGFMiniGameGate* DepthsGate = Scope.World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), DepthsXform);
	ADefaultPawn* Pawn = Scope.World->SpawnActor<ADefaultPawn>(FVector(1.f, 2.f, 3.f), FRotator::ZeroRotator);
	const FGFMiniGameEntry* DepthsRow = FGFMiniGameCatalog::Find(TEXT("Depths"));
	if (!TestNotNull(TEXT("Depths gate"), DepthsGate) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Depths row"), DepthsRow))
	{
		return false;
	}
	DepthsGate->EntryId = TEXT("Depths");
	DepthsGate->GateRole = EGFMiniGameGateRole::Portal;
	DepthsGate->RimeCenter = Rime;
	FinishSpawnedGate(DepthsGate, DepthsXform);
	TestTrue(TEXT("Depths portal moves the pawn onto its pad"), DepthsGate->EnterFrom(Pawn));
	TestTrue(TEXT("Stand follows the anchor"), Pawn->GetActorLocation().Equals(FGFMiniGameCatalog::SimStand(*DepthsRow, Rime), 1.f));

	const FTransform BeatXform(FRotator::ZeroRotator, FVector(500.f, 0.f, 0.f));
	AGFMiniGameGate* BeatGate = Scope.World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), BeatXform);
	if (!TestNotNull(TEXT("Beat Hop gate"), BeatGate))
	{
		return false;
	}
	BeatGate->EntryId = TEXT("BeatHop");
	BeatGate->GateRole = EGFMiniGameGateRole::Portal;
	FinishSpawnedGate(BeatGate, BeatXform);
	const FVector Before = Pawn->GetActorLocation();
	TestFalse(TEXT("Beat Hop does not drag the pawn onto a pad"), BeatGate->EnterFrom(Pawn));
	TestTrue(TEXT("Pawn stays put when the map cannot be opened here"), Pawn->GetActorLocation().Equals(Before, 1.f));

	AGFMiniGameGate* Marker = Scope.World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), BeatXform);
	if (!TestNotNull(TEXT("Depths marker"), Marker))
	{
		return false;
	}
	Marker->EntryId = TEXT("Depths");
	Marker->GateRole = EGFMiniGameGateRole::Marker;
	FinishSpawnedGate(Marker, BeatXform);
	TestEqual(TEXT("Pad marker says Depths"), Marker->GetLabel(), FString(TEXT("Depths")));
	TestEqual(TEXT("Pad marker sign says Depths"), Marker->GetSignText().ToString(), FString(TEXT("Depths")));
	TestFalse(TEXT("A marker is not a door"), Marker->EnterFrom(Pawn));
	TestTrue(TEXT("Depths pad has a solid floor"), FloorCoversPad(Marker->GetPadFloor(), *DepthsRow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGatePlaceTest, "GFGame.Gates.Place", GFHubGateTests::Flags)
bool FGFHubGatePlaceTest::RunTest(const FString&)
{
	using namespace GFHubGateTests;
	FWorldScope Scope;
	if (!TestNotNull(TEXT("World"), Scope.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = Scope.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Director"), Director))
	{
		return false;
	}
	const FVector Rime = FVector::ZeroVector;
	Director->Place(TEXT("/Game/Maps/L_Hub"), Rime);
	Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_Hub"), Rime);
	TestEqual(TEXT("One portal per mini-game"), CountRole(Scope.World, EGFMiniGameGateRole::Portal), FGFMiniGameCatalog::All().Num());
	TestEqual(TEXT("One marker per mapless sim"), CountRole(Scope.World, EGFMiniGameGateRole::Marker), 4);

	for (const FGFMiniGameEntry& Entry : FGFMiniGameCatalog::All())
	{
		AGFMiniGameGate* Portal = FindSpawnedGate(Scope.World, Entry.Id, EGFMiniGameGateRole::Portal);
		if (!TestNotNull(*FString::Printf(TEXT("%s portal"), *Entry.Id.ToString()), Portal))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s portal name"), *Entry.Id.ToString()), Portal->GetLabel(), Entry.Label);
		const FVector Want = FGFMiniGameCatalog::GateLocation(Rime, Entry.RingIndex, FGFMiniGameCatalog::All().Num());
		TestTrue(*FString::Printf(TEXT("%s on the ring"), *Entry.Id.ToString()), Portal->GetActorLocation().Equals(Want, 1.f));
		TestFalse(*FString::Printf(TEXT("%s portal floor stays open"), *Entry.Id.ToString()), FloorBlocks(Portal->GetPadFloor()));
		if (Entry.bHasPad)
		{
			AGFMiniGameGate* Marker = FindSpawnedGate(Scope.World, Entry.Id, EGFMiniGameGateRole::Marker);
			TestNotNull(*FString::Printf(TEXT("%s pad marker"), *Entry.Id.ToString()), Marker);
			if (Marker)
			{
				TestEqual(*FString::Printf(TEXT("%s pad name"), *Entry.Id.ToString()), Marker->GetLabel(), Entry.Label);
				TestTrue(*FString::Printf(TEXT("%s marker on the pad"), *Entry.Id.ToString()), Marker->GetActorLocation().Equals(FGFMiniGameCatalog::PadOrigin(Entry, Rime), 1.f));
				TestTrue(*FString::Printf(TEXT("%s pad floor"), *Entry.Id.ToString()), FloorCoversPad(Marker->GetPadFloor(), Entry));
			}
			TestEqual(*FString::Printf(TEXT("%s director"), *Entry.Id.ToString()), CountClass(Scope.World, Entry.DirectorClass), 1);
		}
	}

	const FGFMiniGameEntry* Beat = FGFMiniGameCatalog::Find(TEXT("BeatHop"));
	const FGFMiniGameEntry* Fashion = FGFMiniGameCatalog::Find(TEXT("Fashion"));
	if (!TestNotNull(TEXT("Beat Hop row"), Beat) || !TestNotNull(TEXT("Runway row"), Fashion))
	{
		return false;
	}

	{
		FWorldScope BeatWorld;
		if (!TestNotNull(TEXT("Beat world"), BeatWorld.World))
		{
			return false;
		}
		AGFHubGateDirector* BeatDirector = BeatWorld.World->SpawnActor<AGFHubGateDirector>();
		if (!TestNotNull(TEXT("Beat director"), BeatDirector))
		{
			return false;
		}
		BeatDirector->Place(TEXT("/Game/Maps/L_Beat"), Rime);
		AGFMiniGameGate* Sign = FindSpawnedGate(BeatWorld.World, TEXT("BeatHop"), EGFMiniGameGateRole::Marker);
		if (TestNotNull(TEXT("Beat Hop map sign"), Sign))
		{
			TestEqual(TEXT("Beat Hop map shows Beat Hop"), Sign->GetLabel(), FString(TEXT("Beat Hop")));
			TestEqual(TEXT("Beat Hop map sign text"), Sign->GetSignText().ToString(), FString(TEXT("Beat Hop")));
			TestFalse(TEXT("Beat Hop map marker has no pad floor"), FloorBlocks(Sign->GetPadFloor()));
		}
		TestEqual(TEXT("Course is on the Beat Hop map"), CountClass(BeatWorld.World, Beat->PlacedClass), 1);
		TestNull(TEXT("Depths is not written on the Beat Hop map"), FindSpawnedGate(BeatWorld.World, TEXT("Depths"), EGFMiniGameGateRole::Marker));
	}

	{
		FWorldScope RunwayWorld;
		if (!TestNotNull(TEXT("Runway world"), RunwayWorld.World))
		{
			return false;
		}
		AGFHubGateDirector* RunwayDirector = RunwayWorld.World->SpawnActor<AGFHubGateDirector>();
		if (!TestNotNull(TEXT("Runway director"), RunwayDirector))
		{
			return false;
		}
		RunwayDirector->Place(TEXT("/Game/Maps/L_Runway"), Rime);
		TestEqual(TEXT("Runway stage is placed"), CountClass(RunwayWorld.World, Fashion->PlacedClass), 1);
		AGFMiniGameGate* Sign = FindSpawnedGate(RunwayWorld.World, TEXT("Fashion"), EGFMiniGameGateRole::Marker);
		if (TestNotNull(TEXT("Runway sign actor"), Sign))
		{
			TestEqual(TEXT("Runway sign"), Sign->GetLabel(), FString(TEXT("Runway")));
			TestFalse(TEXT("Runway map marker has no pad floor"), FloorBlocks(Sign->GetPadFloor()));
		}
	}

	{
		FWorldScope MobaWorld;
		if (!TestNotNull(TEXT("MOBA world"), MobaWorld.World))
		{
			return false;
		}
		AGFHubGateDirector* MobaDirector = MobaWorld.World->SpawnActor<AGFHubGateDirector>();
		if (!TestNotNull(TEXT("MOBA director"), MobaDirector))
		{
			return false;
		}
		MobaDirector->Place(TEXT("/Game/Maps/L_Moba"), Rime);
		AGFMiniGameGate* Sign = FindSpawnedGate(MobaWorld.World, TEXT("Moba"), EGFMiniGameGateRole::Marker);
		if (TestNotNull(TEXT("MOBA sign actor"), Sign))
		{
			TestEqual(TEXT("Crystal Clash map label"), Sign->GetLabel(), FString(TEXT("Crystal Clash")));
			TestEqual(TEXT("Crystal Clash map sign text"), Sign->GetSignText().ToString(), FString(TEXT("Crystal Clash")));
			TestFalse(TEXT("MOBA map marker has no pad floor"), FloorBlocks(Sign->GetPadFloor()));
		}
		TestNull(TEXT("MOBA map has no Depths sign"), FindSpawnedGate(MobaWorld.World, TEXT("Depths"), EGFMiniGameGateRole::Marker));
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHubGateArtTest, "GFGame.Gates.Art", GFHubGateTests::Flags)
bool FGFHubGateArtTest::RunTest(const FString&)
{
	using namespace GFHubGateTests;
	// World map v1: the shared sim portal and pad slab carry the entry colour, and map portals get their sign.
	FWorldScope Scope;
	if (!TestNotNull(TEXT("World"), Scope.World))
	{
		return false;
	}
	const bool bSimArt = FPackageName::DoesPackageExist(TEXT("/Game/Art/Gates/SM_Gate_Sim"));
	const bool bPadArt = FPackageName::DoesPackageExist(TEXT("/Game/Art/Gates/SM_GatePad"));
	TestTrue(TEXT("SM_Gate_Sim imported"), bSimArt);
	TestTrue(TEXT("SM_GatePad imported"), bPadArt);

	auto DyeOf = [](const UStaticMeshComponent* Comp, FLinearColor& Out)
	{
		const UMaterialInstanceDynamic* Dyed = Comp ? Cast<UMaterialInstanceDynamic>(Comp->GetMaterial(0)) : nullptr;
		return Dyed && Dyed->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Dye_R")), Out);
	};

	int32 Signs = 0;
	for (const FGFMiniGameEntry& Entry : FGFMiniGameCatalog::All())
	{
		const FString Id = Entry.Id.ToString();
		const FRotator Yaw(0.f, 30.f + Entry.RingIndex * 45.f, 0.f);
		const FTransform Xform(Yaw, FVector(Entry.RingIndex * 1000.f, 0.f, 0.f));
		for (EGFMiniGameGateRole Role : { EGFMiniGameGateRole::Portal, EGFMiniGameGateRole::Marker })
		{
			AGFMiniGameGate* Gate = Scope.World->SpawnActorDeferred<AGFMiniGameGate>(AGFMiniGameGate::StaticClass(), Xform);
			if (!TestNotNull(*Id, Gate))
			{
				continue;
			}
			Gate->EntryId = Entry.Id;
			Gate->GateRole = Role;
			FinishSpawnedGate(Gate, Xform);
			const bool bPortal = Role == EGFMiniGameGateRole::Portal;
			const FString Tag = Id + (bPortal ? TEXT(" portal") : TEXT(" marker"));

			if (Entry.HasSim())
			{
				TestEqual(*(Tag + TEXT(" art")), Entry.ArtObject, FString(FGFMiniGameCatalog::SimGateObject));
				if (bSimArt)
				{
					UStaticMeshComponent* GateMesh = Gate->GetGateMesh();
					TestFalse(*(Tag + TEXT(" uses SM_Gate_Sim")), Gate->IsUsingPlaceholder());
					TestTrue(*(Tag + TEXT(" at scale 1 on the ground")), GateMesh && GateMesh->GetRelativeScale3D().Equals(FVector::OneVector) && GateMesh->GetRelativeLocation().IsNearlyZero());
					FLinearColor Dye;
					TestTrue(*(Tag + TEXT(" Dye_R is the entry colour")), DyeOf(GateMesh, Dye) && Dye.Equals(Entry.PlaceholderColor, 0.001f));
				}
			}

			UStaticMeshComponent* Floor = Gate->GetPadFloor();
			if (!bPortal && Entry.bHasPad && bPadArt)
			{
				TestTrue(*(Tag + TEXT(" floor is SM_GatePad")), IsPadArt(Floor));
				TestTrue(*(Tag + TEXT(" floor at scale 1, Z 0")), Floor && Floor->GetRelativeScale3D().Equals(FVector::OneVector) && Floor->GetRelativeLocation().IsNearlyZero());
				TestTrue(*(Tag + TEXT(" floor blocks")), FloorBlocks(Floor));
				TestTrue(*(Tag + TEXT(" floor covers the pad")), FloorCoversPad(Floor, Entry));
				FLinearColor Dye;
				TestTrue(*(Tag + TEXT(" floor Dye_R is the entry colour")), DyeOf(Floor, Dye) && Dye.Equals(Entry.PlaceholderColor, 0.001f));
				Gate->ApplyVisuals();
				TestTrue(*(Tag + TEXT(" re-apply keeps one dye")), DyeOf(Floor, Dye) && Dye.Equals(Entry.PlaceholderColor, 0.001f));
			}

			UStaticMeshComponent* Sign = Gate->GetModeSign();
			if (!TestNotNull(*(Tag + TEXT(" sign component")), Sign))
			{
				continue;
			}
			const bool bWantSign = bPortal && !Entry.SignObject.IsEmpty();
			if (bWantSign)
			{
				++Signs;
				TestTrue(*(Tag + TEXT(" sign asset exists")), FPackageName::DoesPackageExist(FSoftObjectPath(Entry.SignObject).GetLongPackageName()));
				TestTrue(*(Tag + TEXT(" sign mesh")), Sign->GetStaticMesh() && Sign->GetStaticMesh()->GetPathName() == Entry.SignObject);
				TestFalse(*(Tag + TEXT(" sign shows")), Sign->bHiddenInGame);
				TestTrue(*(Tag + TEXT(" sign blocks")), Sign->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
				const FVector Want = Xform.GetLocation() + Yaw.RotateVector(FVector(0.f, FGFMiniGameCatalog::SignRightCm, 0.f));
				TestTrue(*(Tag + TEXT(" sign 300 cm to the gate's right")), Sign->GetComponentLocation().Equals(Want, 1.f));
				TestTrue(*(Tag + TEXT(" sign keeps the gate yaw")), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Sign->GetComponentRotation().Yaw - Yaw.Yaw), 0.f, 0.1f));
			}
			else
			{
				TestTrue(*(Tag + TEXT(" no sign")), Sign->bHiddenInGame && Sign->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
			}
		}
	}
	TestEqual(TEXT("Beat Hop, Runway and MOBA portals have signs"), Signs, 3);
	for (const TCHAR* Id : { TEXT("BeatHop"), TEXT("Fashion"), TEXT("Moba") })
	{
		const FGFMiniGameEntry* Row = FGFMiniGameCatalog::Find(Id);
		TestTrue(*FString::Printf(TEXT("%s sign under Gates/Signs"), Id), Row && Row->SignObject.StartsWith(TEXT("/Game/Art/Gates/Signs/SM_Sign_")));
	}
	const FGFMiniGameEntry* Beat = FGFMiniGameCatalog::Find(TEXT("BeatHop"));
	TestTrue(TEXT("Beat Hop expects SM_Gate_Beat now"), Beat && Beat->bExpectArt);
	return true;
}

#endif
