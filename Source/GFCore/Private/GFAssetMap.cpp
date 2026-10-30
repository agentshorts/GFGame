#include "GFAssetMap.h"
#include "GFDataTables.h"
#include "GFLog.h"
#include "GFSimpleAnimComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	TSet<FName>& MissingSkeletalLogs()
	{
		static TSet<FName> Keys;
		return Keys;
	}

	void LogMissingSkeletal(FName Key, const FString& Detail)
	{
		if (MissingSkeletalLogs().Contains(Key))
		{
			return;
		}
		MissingSkeletalLogs().Add(Key);
		UE_LOG(LogGF, Warning, TEXT("GFAssetMap: skeletal mesh for %s failed to load (%s); using the placeholder"), *Key.ToString(), *Detail);
	}

	void ApplyPlaceholderMesh(UStaticMeshComponent* Comp, const FGFAssetMapRow* Row, const FLinearColor* TintOverride)
	{
		const EGFPlaceholderShape Shape = Row ? Row->Placeholder : EGFPlaceholderShape::Cube;
		Comp->SetStaticMesh(FGFAssetMap::PlaceholderMesh(Shape));
		Comp->SetRelativeScale3D(Row ? Row->PlaceholderScale : FVector::OneVector);
		Comp->SetRelativeLocation(Row ? Row->PlaceholderOffset : FVector(0, 0, 50));
		const FLinearColor Color = TintOverride ? *TintOverride : (Row ? Row->Color : FLinearColor(1.f, 0.f, 1.f));
		Comp->SetMaterial(0, FGFAssetMap::MakeTint(Comp, Color));
	}

	void HideSkeletal(USkeletalMeshComponent* Art)
	{
		if (!Art)
		{
			return;
		}
		Art->SetSkeletalMesh(nullptr);
		Art->SetVisibility(false);
		Art->SetHiddenInGame(true);
	}

	void ShowPlaceholder(UStaticMeshComponent* Fallback, const FGFAssetMapRow* Row, const FLinearColor* Tint)
	{
		if (!Fallback)
		{
			return;
		}
		Fallback->SetVisibility(true);
		Fallback->SetHiddenInGame(false);
		ApplyPlaceholderMesh(Fallback, Row, Tint);
	}

	UGFSimpleAnimComponent* FindSimpleAnim(const UActorComponent* Comp)
	{
		const AActor* Owner = Comp ? Comp->GetOwner() : nullptr;
		if (!Owner && Comp)
		{
			Owner = Comp->GetTypedOuter<AActor>();
		}
		return Owner ? Owner->FindComponentByClass<UGFSimpleAnimComponent>() : nullptr;
	}
}

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
	ApplyPlaceholderMesh(Comp, Row, TintOverride);
	return false;
}

bool FGFAssetMap::ApplySkeletal(USkeletalMeshComponent* Art, UStaticMeshComponent* Fallback, FName Key, const FLinearColor* Tint)
{
	return ApplySkeletalFromRow(Art, Fallback, Key, Find(Key), Tint);
}

bool FGFAssetMap::ApplySkeletalFromRow(USkeletalMeshComponent* Art, UStaticMeshComponent* Fallback, FName Key, const FGFAssetMapRow* Row, const FLinearColor* Tint)
{
	auto UsePlaceholder = [&]()
	{
		HideSkeletal(Art);
		ShowPlaceholder(Fallback, Row, Tint);
		UActorComponent* Source = Art ? static_cast<UActorComponent*>(Art) : static_cast<UActorComponent*>(Fallback);
		if (UGFSimpleAnimComponent* Anim = FindSimpleAnim(Source))
		{
			Anim->SetAnims(NAME_None, NAME_None);
		}
		return false;
	};

	if (!Row || Row->SkeletalMesh.IsNull())
	{
		return UsePlaceholder();
	}
	USkeletalMesh* Mesh = nullptr;
	if (Art)
	{
		Mesh = Row->SkeletalMesh.LoadSynchronous();
	}
	if (!Art || !Mesh)
	{
		LogMissingSkeletal(Key, Row->SkeletalMesh.ToString());
		return UsePlaceholder();
	}

	Art->SetSkeletalMesh(Mesh);
	Art->SetVisibility(true);
	Art->SetHiddenInGame(false);
	Art->SetRelativeScale3D(Row->ArtScale);
	Art->SetRelativeLocation(Row->ArtOffset);
	Art->SetRelativeRotation(Row->ArtRotation);
	if (Fallback)
	{
		Fallback->SetVisibility(false);
		Fallback->SetHiddenInGame(true);
	}
	if (UGFSimpleAnimComponent* Anim = FindSimpleAnim(Art))
	{
		Anim->SetAnims(Row->IdleAnim, Row->MoveAnim);
	}
	return true;
}
