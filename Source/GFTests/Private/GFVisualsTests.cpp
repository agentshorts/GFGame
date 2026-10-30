// Skeletal meshes and the idle/move picker.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Visuals;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "GFAssetMap.h"
#include "GFAnimMap.h"
#include "GFDataTables.h"
#include "GFSimpleAnimComponent.h"
#include "GFCharacter.h"
#include "GFDino.h"
#include "GFGiantDino.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFVisualsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

static TArray<FString> GFVisualsCsvFields(const FString& Line)
{
	TArray<FString> Out;
	FString Cur;
	bool bQuoted = false;
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		const TCHAR C = Line[i];
		if (bQuoted)
		{
			if (C == TEXT('"') && i + 1 < Line.Len() && Line[i + 1] == TEXT('"')) { Cur.AppendChar(C); ++i; }
			else if (C == TEXT('"')) { bQuoted = false; }
			else { Cur.AppendChar(C); }
		}
		else if (C == TEXT('"')) { bQuoted = true; }
		else if (C == TEXT(',')) { Out.Add(Cur); Cur.Reset(); }
		else if (C != TEXT('\r')) { Cur.AppendChar(C); }
	}
	Out.Add(Cur);
	return Out;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVisualsColumnsTest, "GFGame.Visuals.AssetMapColumns", GFVisualsTestFlags)
bool FGFVisualsColumnsTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	const FString Path = FGFDataTables::CsvPath(TEXT("DT_AssetMap"));
	if (!TestTrue(TEXT("DT_AssetMap.csv loads"), FFileHelper::LoadFileToStringArray(Lines, *Path)) || Lines.Num() < 2)
	{
		return false;
	}
	const TArray<FString> Header = GFVisualsCsvFields(Lines[0]);
	const TArray<FString> Expect = {
		TEXT("---"), TEXT("Mesh"), TEXT("Material"), TEXT("Placeholder"), TEXT("Color"),
		TEXT("PlaceholderScale"), TEXT("PlaceholderOffset"),
		TEXT("SkeletalMesh"), TEXT("IdleAnim"), TEXT("MoveAnim"), TEXT("ArtScale"), TEXT("ArtOffset"), TEXT("ArtRotation")
	};
	if (!TestEqual(TEXT("DT_AssetMap column count"), Header.Num(), Expect.Num()))
	{
		return false;
	}
	for (int32 i = 0; i < Expect.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("column %d"), i), Header[i], Expect[i]);
	}
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		if (Lines[i].TrimStartAndEnd().IsEmpty())
		{
			continue;
		}
		const TArray<FString> Fields = GFVisualsCsvFields(Lines[i]);
		TestEqual(FString::Printf(TEXT("row %d has every column"), i + 1), Fields.Num(), Expect.Num());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVisualsRowsTest, "GFGame.Visuals.Rows", GFVisualsTestFlags)
bool FGFVisualsRowsTest::RunTest(const FString&)
{
	FGFDataTables::ResetCache();
	const FGFAssetMapRow* Player = FGFAssetMap::Find(TEXT("Player.Base"));
	if (!TestNotNull(TEXT("Player.Base row"), Player))
	{
		return false;
	}
	TestTrue(TEXT("Player skeletal path"), Player->SkeletalMesh.ToString().Contains(TEXT("SK_Player_Base")));
	TestEqual(TEXT("Player idle"), Player->IdleAnim, FName(TEXT("Player.Idle")));
	TestEqual(TEXT("Player move"), Player->MoveAnim, FName(TEXT("Player.Run")));
	TestTrue(TEXT("Player feet"), Player->ArtOffset.Equals(FVector(0.f, 0.f, -88.f), 0.01f));
	TestTrue(TEXT("Player faces +X"), Player->ArtRotation.Equals(FRotator::ZeroRotator, 0.01f));
	TestTrue(TEXT("Player art scale"), Player->ArtScale.Equals(FVector::OneVector, 0.01f));
	TestTrue(TEXT("Player static mesh still empty"), Player->Mesh.IsNull());

	const TCHAR* ArtKeys[] = {
		TEXT("Dino.Raptor"), TEXT("Dino.Tusksaw"), TEXT("Dino.Lanternmaw"),
		TEXT("Dino.Scythepaw"), TEXT("Dino.Ripwing"), TEXT("Dino.BigGrumble")
	};
	for (const TCHAR* Key : ArtKeys)
	{
		const FGFAssetMapRow* Row = FGFAssetMap::Find(Key);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), Key), Row))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s skeletal path set"), Key), Row->SkeletalMesh.IsNull());
		TestTrue(FString::Printf(TEXT("%s static mesh still empty"), Key), Row->Mesh.IsNull());
		TestTrue(FString::Printf(TEXT("%s art scale"), Key), Row->ArtScale.Equals(FVector::OneVector, 0.01f));
		if (!Row->IdleAnim.IsNone())
		{
			TestNotNull(FString::Printf(TEXT("%s idle clip %s"), Key, *Row->IdleAnim.ToString()), FGFAnimMap::Find(Row->IdleAnim));
		}
		if (!Row->MoveAnim.IsNone())
		{
			TestNotNull(FString::Printf(TEXT("%s move clip %s"), Key, *Row->MoveAnim.ToString()), FGFAnimMap::Find(Row->MoveAnim));
		}
	}

	const FGFAssetMapRow* Raptor = FGFAssetMap::Find(TEXT("Dino.Raptor"));
	if (TestNotNull(TEXT("Raptor row"), Raptor))
	{
		TestTrue(TEXT("Raptor colour survived the new columns"), Raptor->Color.Equals(FLinearColor(0.85f, 0.55f, 0.25f), 0.01f));
		TestTrue(TEXT("Raptor placeholder scale survived"), Raptor->PlaceholderScale.Equals(FVector(0.9f, 0.6f, 1.2f), 0.01f));
		TestTrue(TEXT("Raptor placeholder offset survived"), Raptor->PlaceholderOffset.Equals(FVector(0.f, 0.f, 60.f), 0.01f));
		TestTrue(TEXT("Raptor placeholder shape"), Raptor->Placeholder == EGFPlaceholderShape::Sphere);
		TestTrue(TEXT("Raptor feet"), Raptor->ArtOffset.Equals(FVector(0.f, 0.f, -96.f), 0.01f));
		TestEqual(TEXT("Raptor idle key"), Raptor->IdleAnim, FName(TEXT("Raptor.Idle")));
		TestEqual(TEXT("Raptor move key"), Raptor->MoveAnim, FName(TEXT("Raptor.Run")));
	}

	const FGFAssetMapRow* Grumble = FGFAssetMap::Find(TEXT("Dino.BigGrumble"));
	if (TestNotNull(TEXT("Big Grumble row"), Grumble))
	{
		TestTrue(TEXT("Big Grumble path"), Grumble->SkeletalMesh.ToString().Contains(TEXT("SK_Dino_BigGrumble")));
		TestEqual(TEXT("Big Grumble idle"), Grumble->IdleAnim, FName(TEXT("BigGrumble.Taunt_Idle")));
		TestEqual(TEXT("Big Grumble move"), Grumble->MoveAnim, FName(TEXT("BigGrumble.Prowl_Circle")));
		TestTrue(TEXT("Big Grumble offset is ScaleRoot-local"), Grumble->ArtOffset.Equals(FVector::ZeroVector, 0.01f));
	}

	for (const TCHAR* Key : { TEXT("Dino.Nipper"), TEXT("Dino.Sailback"), TEXT("Dino.Rex"), TEXT("Dino.Hornback") })
	{
		const FGFAssetMapRow* Row = FGFAssetMap::Find(Key);
		if (TestNotNull(FString::Printf(TEXT("%s row"), Key), Row))
		{
			TestTrue(FString::Printf(TEXT("%s stays a sphere"), Key), Row->SkeletalMesh.IsNull() && Row->Mesh.IsNull());
			TestTrue(FString::Printf(TEXT("%s shape"), Key), Row->Placeholder == EGFPlaceholderShape::Sphere);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVisualsFallbackTest, "GFGame.Visuals.MissingSkeletalFallsBack", GFVisualsTestFlags)
bool FGFVisualsFallbackTest::RunTest(const FString&)
{
	const FString Csv = FString::Printf(TEXT("%s\n%s\n"),
		TEXT("---,Mesh,Material,Placeholder,Color,PlaceholderScale,PlaceholderOffset,SkeletalMesh,IdleAnim,MoveAnim,ArtScale,ArtOffset,ArtRotation"),
		TEXT("Bad.Skel,None,None,Sphere,\"(R=0.20,G=0.30,B=0.40,A=1.0)\",\"(X=2.0,Y=3.0,Z=4.0)\",\"(X=5.0,Y=6.0,Z=7.0)\",/Game/Art/DoesNotExist/SK_Missing.SK_Missing,Missing.Idle,Missing.Move,\"(X=1.0,Y=1.0,Z=1.0)\",\"(X=0.0,Y=0.0,Z=-10.0)\",\"(Pitch=0.0,Yaw=15.0,Roll=0.0)\""));
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = FGFAssetMapRow::StaticStruct();
	const TArray<FString> Problems = Table->CreateTableFromCSVString(Csv);
	if (!TestEqual(TEXT("synthetic CSV parsed"), Problems.Num(), 0))
	{
		for (const FString& Problem : Problems)
		{
			AddError(Problem);
		}
		return false;
	}
	const FGFAssetMapRow* Row = Table->FindRow<FGFAssetMapRow>(TEXT("Bad.Skel"), TEXT("VisualsTest"), false);
	if (!TestNotNull(TEXT("Bad.Skel row"), Row))
	{
		return false;
	}
	TestFalse(TEXT("missing path is still set"), Row->SkeletalMesh.IsNull());
	TestNull(TEXT("missing path does not load"), Row->SkeletalMesh.LoadSynchronous());
	TestEqual(TEXT("idle key parsed"), Row->IdleAnim, FName(TEXT("Missing.Idle")));
	TestTrue(TEXT("art offset parsed"), Row->ArtOffset.Equals(FVector(0.f, 0.f, -10.f), 0.01f));
	TestTrue(TEXT("art yaw parsed"), FMath::IsNearlyEqual(Row->ArtRotation.Yaw, 15.f, 0.01f));

	USkeletalMeshComponent* Art = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(GetTransientPackage());
	const bool bArt = FGFAssetMap::ApplySkeletalFromRow(Art, Body, TEXT("Bad.Skel"), Row);
	TestFalse(TEXT("falls back"), bArt);
	TestFalse(TEXT("art hidden"), Art->GetVisibleFlag());
	TestTrue(TEXT("art hidden in game"), Art->bHiddenInGame);
	TestNull(TEXT("no skeletal mesh left on art"), Art->GetSkeletalMeshAsset());
	TestTrue(TEXT("placeholder visible"), Body->GetVisibleFlag());
	TestFalse(TEXT("placeholder not hidden"), Body->bHiddenInGame);
	TestTrue(TEXT("placeholder sphere"), Body->GetStaticMesh() == FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape::Sphere));
	TestTrue(TEXT("placeholder scale from the row"), Body->GetRelativeScale3D().Equals(FVector(2.f, 3.f, 4.f), 0.01f));
	TestTrue(TEXT("placeholder offset from the row"), Body->GetRelativeLocation().Equals(FVector(5.f, 6.f, 7.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVisualsChooseAnimTest, "GFGame.Visuals.ChooseAnim", GFVisualsTestFlags)
bool FGFVisualsChooseAnimTest::RunTest(const FString&)
{
	const FName Idle(TEXT("Idle"));
	const FName Move(TEXT("Move"));
	const float Threshold = UGFSimpleAnimComponent::DefaultMoveSpeed;
	TestEqual(TEXT("still"), UGFSimpleAnimComponent::ChooseAnim(0.f, Idle, Move, Threshold), Idle);
	TestEqual(TEXT("exactly the threshold stays idle"), UGFSimpleAnimComponent::ChooseAnim(Threshold, Idle, Move, Threshold), Idle);
	TestEqual(TEXT("just above walks"), UGFSimpleAnimComponent::ChooseAnim(Threshold + 0.01f, Idle, Move, Threshold), Move);
	TestEqual(TEXT("running"), UGFSimpleAnimComponent::ChooseAnim(500.f, Idle, Move, Threshold), Move);
	TestEqual(TEXT("no move clip stays idle"), UGFSimpleAnimComponent::ChooseAnim(500.f, Idle, NAME_None, Threshold), Idle);
	TestEqual(TEXT("negative speed stays idle"), UGFSimpleAnimComponent::ChooseAnim(-5.f, Idle, Move, Threshold), Idle);
	TestEqual(TEXT("default threshold is 20 cm/s"), Threshold, 20.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFVisualsWiringTest, "GFGame.Visuals.Wiring", GFVisualsTestFlags)
bool FGFVisualsWiringTest::RunTest(const FString&)
{
	FGFDataTables::ResetCache();
	const AGFCharacter* Player = GetDefault<AGFCharacter>();
	const FGFAssetMapRow* Row = FGFAssetMap::Find(TEXT("Player.Base"));
	if (!TestNotNull(TEXT("player CDO"), Player) || !TestNotNull(TEXT("player mesh"), Player ? Player->GetMesh() : nullptr) || !TestNotNull(TEXT("Player.Base"), Row))
	{
		return false;
	}
	TestNotNull(TEXT("player simple anim"), Player->SimpleAnim.Get());
	TestTrue(TEXT("player mesh uses the row offset"), Player->GetMesh()->GetRelativeLocation().Equals(Row->ArtOffset, 0.01f));
	TestTrue(TEXT("player mesh uses the row rotation"), Player->GetMesh()->GetRelativeRotation().Equals(Row->ArtRotation, 0.01f));
	TestTrue(TEXT("player mesh has no collision"), Player->GetMesh()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	if (Player->SimpleAnim)
	{
		TestEqual(TEXT("move speed"), Player->SimpleAnim->MoveSpeed, UGFSimpleAnimComponent::DefaultMoveSpeed);
	}

	USkeletalMeshComponent* Art = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(GetTransientPackage());
	const bool bShown = FGFAssetMap::ApplySkeletal(Art, Body, TEXT("Player.Base"));
	TestTrue(TEXT("Player.Base mesh loads"), bShown);
	TestNotNull(TEXT("art mesh set"), Art->GetSkeletalMeshAsset());
	TestTrue(TEXT("art visible"), Art->GetVisibleFlag() && !Art->bHiddenInGame);
	TestTrue(TEXT("sphere hidden"), Body->bHiddenInGame);
	TestTrue(TEXT("art feet"), Art->GetRelativeLocation().Equals(FVector(0.f, 0.f, -88.f), 0.01f));

	const AGFDino* Dino = GetDefault<AGFDino>();
	if (TestNotNull(TEXT("dino CDO"), Dino))
	{
		TestNotNull(TEXT("dino body"), Dino->Body.Get());
		TestNotNull(TEXT("dino art"), Dino->Art.Get());
		TestNotNull(TEXT("dino simple anim"), Dino->SimpleAnim.Get());
		TestTrue(TEXT("dino art starts hidden"), Dino->Art && Dino->Art->bHiddenInGame);
		TestTrue(TEXT("dino art is on the capsule"), Dino->Art && Dino->Art->GetAttachParent() == Dino->GetCapsuleComponent());
		TestTrue(TEXT("character mesh stays hidden"), Dino->GetMesh() && Dino->GetMesh()->bHiddenInGame);
	}

	const AGFGiantDino* Giant = GetDefault<AGFGiantDino>();
	if (TestNotNull(TEXT("giant CDO"), Giant))
	{
		TestNotNull(TEXT("giant body"), Giant->Body.Get());
		TestNotNull(TEXT("giant art"), Giant->Art.Get());
		TestNotNull(TEXT("giant simple anim"), Giant->SimpleAnim.Get());
		TestTrue(TEXT("giant art is under ScaleRoot"), Giant->Art && Giant->Art->GetAttachParent() == Giant->ScaleRoot);
		TestTrue(TEXT("giant art starts hidden"), Giant->Art && Giant->Art->bHiddenInGame);
	}
	return true;
}

#endif
