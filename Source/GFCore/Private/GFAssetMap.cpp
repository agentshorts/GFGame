#include "GFAssetMap.h"
#include "GFDataTables.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

const FGFAssetMapRow* FGFAssetMap::Find(FName Key)
{
	UDataTable* Table = FGFDataTables::Get<FGFAssetMapRow>(TEXT("DT_AssetMap"));
	return Table ? Table->FindRow<FGFAssetMapRow>(Key, TEXT("GFAssetMap"), false) : nullptr;
}

UStaticMesh* FGFAssetMap::PlaceholderMesh(EGFPlaceholderShape Shape)
{
	const TCHAR* Path = TEXT("/Engine/BasicShapes/Cube.Cube");
	switch (Shape)
	{
	case EGFPlaceholderShape::Sphere: Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); break;
	case EGFPlaceholderShape::Cylinder: Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); break;
	case EGFPlaceholderShape::Cone: Path = TEXT("/Engine/BasicShapes/Cone.Cone"); break;
	default: break;
	}
	return LoadObject<UStaticMesh>(nullptr, Path);
}

UMaterialInterface* FGFAssetMap::MakeTint(UObject* Outer, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Outer);
	Mid->SetVectorParameterValue(TEXT("Color"), Color);
	return Mid;
}

bool FGFAssetMap::Apply(UStaticMeshComponent* Comp, FName Key, const FLinearColor* TintOverride)
{
	if (!Comp)
	{
		return false;
	}
	const FGFAssetMapRow* Row = Find(Key);
	if (Row && !Row->Mesh.IsNull())
	{
		if (UStaticMesh* Mesh = Row->Mesh.LoadSynchronous())
		{
			Comp->SetStaticMesh(Mesh);
			if (UMaterialInterface* Mat = Row->Material.LoadSynchronous())
			{
				Comp->SetMaterial(0, Mat);
			}
			return true;
		}
	}
	const EGFPlaceholderShape Shape = Row ? Row->Placeholder : EGFPlaceholderShape::Cube;
	Comp->SetStaticMesh(PlaceholderMesh(Shape));
	Comp->SetRelativeScale3D(Row ? Row->PlaceholderScale : FVector::OneVector);
	Comp->SetRelativeLocation(Row ? Row->PlaceholderOffset : FVector(0, 0, 50));
	const FLinearColor Color = TintOverride ? *TintOverride : (Row ? Row->Color : FLinearColor(1.f, 0.f, 1.f));
	Comp->SetMaterial(0, MakeTint(Comp, Color));
	return false;
}
