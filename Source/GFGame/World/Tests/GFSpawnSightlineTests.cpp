// The Rime stand looks toward the ferry. A solid cell above the surface, or the Kindle column
// (spire plus shardstone), fails this test. L_Hub has no PlayerStart: the stand is the Rime
// anchor plus 120 cm, yaw 0. The ferry perch at (0, -300), yaw 36, is guarded as well.

#include "Misc/AutomationTest.h"
#include "GFCharacter.h"
#include "GFFerryDirector.h"
#include "GFFerrySpawn.h"
#include "GFHubDressing.h"
#include "GFOverworldBuild.h"
#include "GFOverworldGround.h"
#include "GFTerritorySubsystem.h"
#include "GFZoneCatalog.h"
#include "GFZoneTypes.h"
#include "GameFramework/SpringArmComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFSpawnSightline
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FSight
	{
		FVector2D From = FVector2D::ZeroVector;
		FVector2D To = FVector2D::ZeroVector;
		const TCHAR* Label = TEXT("");
	};

	static FVector2D Flat(const FVector& World)
	{
		return FVector2D(World.X, World.Y);
	}

	static FVector2D Along(const FVector2D& From, float YawDeg, float Distance)
	{
		const float YawRad = FMath::DegreesToRadians(YawDeg);
		return From + FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad)) * Distance;
	}

	static FVector2D CameraBack(const FVector2D& Feet, float YawDeg, float ArmBack)
	{
		const float YawRad = FMath::DegreesToRadians(YawDeg);
		const FVector2D Forward(FMath::Cos(YawRad), FMath::Sin(YawRad));
		return Feet - Forward * ArmBack;
	}

	static float DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Delta = B - A;
		const double LenSq = Delta.SizeSquared();
		double AlongSegment = 0.0;
		if (LenSq > KINDA_SMALL_NUMBER)
		{
			AlongSegment = FMath::Clamp(FVector2D::DotProduct(Point - A, Delta) / LenSq, 0.0, 1.0);
		}
		return static_cast<float>(FVector2D::Distance(Point, A + Delta * AlongSegment));
	}

	static void AddView(TArray<FSight>& Sights, const FVector2D& From, const FVector2D& To, const TCHAR* Label)
	{
		FSight Sight;
		Sight.From = From;
		Sight.To = To;
		Sight.Label = Label;
		Sights.Add(Sight);
	}

	static bool AirAlong(FAutomationTestBase& Test, const FGFOverworldLayout& Layout, const FSight& Sight)
	{
		const FVector2D Delta = Sight.To - Sight.From;
		const float Length = Delta.Size();
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / FGFOverworldLayout::BlockCm));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float T = static_cast<float>(Step) / static_cast<float>(Steps);
			const FVector2D At = Sight.From + Delta * T;
			const FIntVector Surface = Layout.SurfaceCell(FVector(At.X, At.Y, 0.f));
			for (int32 Above = Surface.Z + 1; Above < Layout.GridSize.Z; ++Above)
			{
				const FIntVector Cell(Surface.X, Surface.Y, Above);
				const FName Block = Layout.BlockAt(Cell);
				if (!Block.IsNone())
				{
					Test.AddError(FString::Printf(TEXT("%s has solid %s at cell (%d, %d, %d)"),
						Sight.Label, *Block.ToString(), Cell.X, Cell.Y, Cell.Z));
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSpawnSightlineTest, "GFGame.World.SpawnSightline", GFSpawnSightline::Flags)
bool FGFSpawnSightlineTest::RunTest(const FString&)
{
	using namespace GFSpawnSightline;

	const UGFTerritorySubsystem* Territory = GetDefault<UGFTerritorySubsystem>();
	const AGFOverworldGround* Ground = GetDefault<AGFOverworldGround>();
	if (!TestNotNull(TEXT("Territory defaults"), Territory) || !TestNotNull(TEXT("Ground defaults"), Ground))
	{
		return false;
	}

	TArray<FGFTerritoryFaction> Factions = Territory->FactionLayout;
	const FVector Spawn = FGFPlayerStartRules::FallbackLocation(FName(TEXT("Rime")), Factions);
	const FVector Perch = FGFPlayerStartRules::DefaultPerchLocation();
	const float PerchYaw = FGFPlayerStartRules::DefaultPerchRotation().Yaw;
	const FVector2D SpawnFlat = Flat(Spawn);
	const FVector2D PerchFlat = Flat(Perch);

	FVector Ferry = FVector(1200.f, 600.f, 0.f);
	if (const AGFFerryDirector* FerryDefaults = GetDefault<AGFFerryDirector>())
	{
		for (const FGFFerryStop& Stop : FerryDefaults->Stops)
		{
			if (Stop.Id == FName(TEXT("RimeLanding")))
			{
				Ferry = Stop.Location;
			}
		}
	}
	const FVector2D FerryFlat = Flat(Ferry);

	float ArmLength = 450.f;
	float ArmPitch = -20.f;
	if (const AGFCharacter* Body = GetDefault<AGFCharacter>())
	{
		if (Body->SpringArm)
		{
			ArmLength = Body->SpringArm->TargetArmLength;
			ArmPitch = Body->SpringArm->GetRelativeRotation().Pitch;
		}
	}
	const float FlatArm = ArmLength;
	const float PitchedArm = ArmLength * FMath::Cos(FMath::DegreesToRadians(ArmPitch));

	TArray<FSight> Sights;
	AddView(Sights, SpawnFlat, FerryFlat, TEXT("Stand to ferry"));
	AddView(Sights, SpawnFlat, Along(SpawnFlat, 0.f, 1800.f), TEXT("Stand facing +X"));
	AddView(Sights, CameraBack(SpawnFlat, 0.f, FlatArm), FerryFlat, TEXT("Stand camera to ferry"));
	AddView(Sights, CameraBack(SpawnFlat, 0.f, FlatArm), Along(SpawnFlat, 0.f, 1800.f), TEXT("Stand camera facing +X"));
	AddView(Sights, CameraBack(SpawnFlat, 0.f, PitchedArm), FerryFlat, TEXT("Pitched stand camera to ferry"));
	AddView(Sights, PerchFlat, FerryFlat, TEXT("Perch to ferry"));
	AddView(Sights, PerchFlat, Along(PerchFlat, PerchYaw, 2200.f), TEXT("Perch yaw toward ferry"));
	AddView(Sights, CameraBack(PerchFlat, PerchYaw, FlatArm), FerryFlat, TEXT("Perch camera to ferry"));
	AddView(Sights, CameraBack(PerchFlat, PerchYaw, PitchedArm), Along(PerchFlat, PerchYaw, 2200.f), TEXT("Pitched perch camera"));
	AddView(Sights, SpawnFlat, Along(SpawnFlat, 90.f, 1600.f), TEXT("Stand toward the gate ring"));

	FGFZoneCatalog Catalog = FGFZoneCatalog::Load();
	const FGFShardstoneRow* Kindle = Catalog.FindStone(TEXT("Stone.Kindle"));
	if (!TestNotNull(TEXT("Kindle stone"), Kindle))
	{
		return false;
	}
	const FVector2D Column = Flat(GFHubDressing::ColumnOrigin(Kindle->Location));
	const FVector2D Fallback = Flat(GFHubDressing::FallbackOrigin());
	const float Radius = GFHubDressing::ColumnRadiusCm();
	const TArray<FVector2D> Columns = { Column, Fallback };

	for (const FVector2D& Centre : Columns)
	{
		for (const FSight& Sight : Sights)
		{
			const float Gap = DistanceToSegment(Centre, Sight.From, Sight.To);
			if (!TestTrue(*FString::Printf(TEXT("%s misses the column (%.0f cm, radius %.0f)"), Sight.Label, Gap, Radius), Gap > Radius))
			{
				return false;
			}
		}
	}

	if (!TestTrue(TEXT("Stand is outside the column"), FVector2D::Distance(SpawnFlat, Column) > Radius)
		|| !TestTrue(TEXT("Perch is outside the column"), FVector2D::Distance(PerchFlat, Column) > Radius)
		|| !TestTrue(TEXT("Stand can still reach the Kindle stone"), FVector::Dist2D(Spawn, FVector(Column.X, Column.Y, 0.f)) <= FGFZoneConstants::HopRadiusCm))
	{
		return false;
	}

	const FGFOverworldLayout Layout = FGFOverworldBuild::MakeKindleRing(
		Territory->Curve, Territory->FactionLayout, Territory->Hazards, Ground->Margin);
	for (const FSight& Sight : Sights)
	{
		if (!AirAlong(*this, Layout, Sight))
		{
			return false;
		}
	}

	const FIntVector SpawnCell = Layout.SurfaceCell(Spawn);
	TestEqual(TEXT("Rime surface under the stand"), Layout.BlockAt(SpawnCell).ToString(), GFOverworldBlock::Ice().ToString());
	for (int32 Above = SpawnCell.Z + 1; Above < Layout.GridSize.Z; ++Above)
	{
		const FIntVector AirCell(SpawnCell.X, SpawnCell.Y, Above);
		TestTrue(*FString::Printf(TEXT("Air above the stand at Z %d"), Above), Layout.BlockAt(AirCell).IsNone());
	}
	return true;
}

#endif
