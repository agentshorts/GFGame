#include "GFMineable.h"
#include "GFLog.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshResources.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

bool FGFMineable::AllowsSlot(const UGFMineableAssetUserData* Data, int32 SlotIndex)
{
	if (!Data)
	{
		return false;
	}
	switch (Data->Mode)
	{
	case EGFMineableMode::WholeMesh:
		return true;
	case EGFMineableMode::Slots:
		return SlotIndex != INDEX_NONE && Data->Slots.Contains(SlotIndex);
	default:
		return false;
	}
}

const UGFMineableAssetUserData* FGFMineable::Find(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
	// GetAssetUserDataOfClass is not const on the interface; it only reads.
	return Cast<UGFMineableAssetUserData>(const_cast<UStaticMesh*>(Mesh)->GetAssetUserDataOfClass(UGFMineableAssetUserData::StaticClass()));
}

const UStaticMesh* FGFMineable::MeshOf(const UPrimitiveComponent* Component)
{
	const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Component);
	return SMC ? SMC->GetStaticMesh() : nullptr;
}

int32 FGFMineable::SlotFromHit(const FHitResult& Hit)
{
	const UStaticMesh* Mesh = MeshOf(Hit.GetComponent());
	if (!Mesh || Hit.FaceIndex < 0)
	{
		return INDEX_NONE;
	}
	// Same walk as UStaticMeshComponent::GetMaterialFromCollisionFaceIndex (collision faces are the LODForCollision
	// sections with collision on, in order), but a face past the end gives INDEX_NONE instead of section 0.
	const FStaticMeshRenderData* RD = Mesh->GetRenderData();
	if (!RD || !RD->LODResources.IsValidIndex(Mesh->LODForCollision))
	{
		return INDEX_NONE;
	}
	int32 Faces = 0;
	for (const FStaticMeshSection& Section : RD->LODResources[Mesh->LODForCollision].Sections)
	{
		if (!Section.bEnableCollision)
		{
			continue;
		}
		Faces += Section.NumTriangles;
		if (Hit.FaceIndex < Faces)
		{
			return Section.MaterialIndex;
		}
	}
	return INDEX_NONE;
}

bool FGFMineable::CanMineHit(const FHitResult& Hit, FString& OutReason)
{
	const UStaticMesh* Mesh = MeshOf(Hit.GetComponent());
	if (!Mesh)
	{
		OutReason = TEXT("not a mineable mesh");
		return false;
	}
	const UGFMineableAssetUserData* Data = Find(Mesh);
	if (!Data || Data->Mode == EGFMineableMode::None)
	{
		OutReason = TEXT("not mineable");
		return false;
	}
	if (Data->Mode == EGFMineableMode::WholeMesh)
	{
		return true;
	}
	const int32 Slot = SlotFromHit(Hit);
	if (Slot == INDEX_NONE)
	{
		OutReason = TEXT("face not resolved");
		return false;
	}
	if (!AllowsSlot(Data, Slot))
	{
		OutReason = TEXT("not the crystal part");
		return false;
	}
	return true;
}

FCollisionQueryParams FGFMineable::QueryParams(FName Tag)
{
	FCollisionQueryParams Params(Tag, /*bTraceComplex*/ true);
	Params.bReturnFaceIndex = true;
	return Params;
}

EGFMineableMode FGFMineable::ModeFromString(const FString& Mode)
{
	if (Mode == TEXT("whole_mesh"))
	{
		return EGFMineableMode::WholeMesh;
	}
	if (Mode == TEXT("slots"))
	{
		return EGFMineableMode::Slots;
	}
	return EGFMineableMode::None;
}

bool FGFMineable::ParseWhitelist(const FString& Json, TArray<FGFMineableEntry>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutError = TEXT("bad json");
		return false;
	}
	if (Root->GetStringField(TEXT("schema")) != TEXT("gf.mineable/v1"))
	{
		OutError = TEXT("unexpected schema");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Meshes = nullptr;
	if (!Root->TryGetArrayField(TEXT("meshes"), Meshes))
	{
		OutError = TEXT("no meshes");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : *Meshes)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid())
		{
			continue;
		}
		FGFMineableEntry E;
		E.Asset = FName(*O->GetStringField(TEXT("asset")));
		E.Path = O->GetStringField(TEXT("path"));
		const FString Mode = O->GetStringField(TEXT("mode"));
		E.Mode = ModeFromString(Mode);
		if (E.Mode == EGFMineableMode::None && Mode != TEXT("none"))
		{
			OutError = FString::Printf(TEXT("unknown mode %s on %s"), *Mode, *E.Asset.ToString());
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O->TryGetArrayField(TEXT("slots"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& S : *Arr)
			{
				E.Slots.Add(static_cast<int32>(S->AsNumber()));
			}
		}
		if (O->TryGetArrayField(TEXT("material_slots"), Arr))
		{
			for (const TSharedPtr<FJsonValue>& S : *Arr)
			{
				E.MaterialSlots.Add(FName(*S->AsString()));
			}
		}
		O->TryGetNumberField(TEXT("tri_count"), E.TriCount);
		O->TryGetStringField(TEXT("reason"), E.Reason);
		Out.Add(MoveTemp(E));
	}
	return Out.Num() > 0;
}

FString FGFMineable::TownWhitelistPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Town/town_v1_mineable.json"));
}

bool FGFMineable::LoadWhitelistFile(const FString& Path, TArray<FGFMineableEntry>& Out, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = TEXT("missing ") + Path;
		return false;
	}
	return ParseWhitelist(Json, Out, OutError);
}

bool FGFMineable::LoadTownWhitelist(TArray<FGFMineableEntry>& Out, FString& OutError)
{
	return LoadWhitelistFile(TownWhitelistPath(), Out, OutError);
}

FString FGFMineable::TownsV2WhitelistPath()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Town/towns_v2_mineable.json"));
}

bool FGFMineable::LoadTownsV2Whitelist(TArray<FGFMineableEntry>& Out, FString& OutError)
{
	return LoadWhitelistFile(TownsV2WhitelistPath(), Out, OutError);
}

namespace
{
	struct FKitList
	{
		TSet<FName> Names;
		bool bReady = false;

		void Ensure()
		{
			if (bReady)
			{
				return;
			}
			bReady = true;
			auto Ingest = [this](bool (*Load)(TArray<FGFMineableEntry>&, FString&))
			{
				TArray<FGFMineableEntry> Rows;
				FString Err;
				if (!Load(Rows, Err))
				{
					return;
				}
				for (const FGFMineableEntry& Row : Rows)
				{
					Names.Add(Row.Asset);
				}
			};
			Ingest(&FGFMineable::LoadTownWhitelist);
			Ingest(&FGFMineable::LoadTownsV2Whitelist);
		}
	};

	FKitList& KitList()
	{
		static FKitList List;
		return List;
	}

	TSet<FName>& TagWarnings()
	{
		static TSet<FName> Warned;
		return Warned;
	}

	FDelegateHandle& AssetLoadedHandle()
	{
		static FDelegateHandle Handle;
		return Handle;
	}

	void AuditMesh(UObject* Object)
	{
		const UStaticMesh* Mesh = Cast<UStaticMesh>(Object);
		if (!Mesh || Mesh->HasAnyFlags(RF_ClassDefaultObject | RF_NeedPostLoad))
		{
			return;
		}
		FGFMineable::WarnIfListedWithoutTag(Mesh);
	}
}

bool FGFMineable::ListedWithoutTag(const UStaticMesh* Mesh)
{
	if (!Mesh || Find(Mesh))
	{
		return false;
	}
	KitList().Ensure();
	return KitList().Names.Contains(Mesh->GetFName());
}

bool FGFMineable::WarnIfListedWithoutTag(const UStaticMesh* Mesh)
{
	if (!ListedWithoutTag(Mesh))
	{
		return false;
	}
	const FName Name = Mesh->GetFName();
	if (!TagWarnings().Contains(Name))
	{
		TagWarnings().Add(Name);
		UE_LOG(LogGF, Warning, TEXT("Mineable kit mesh %s is listed in Data/Town/*_mineable.json but has no UGFMineableAssetUserData"), *Name.ToString());
	}
	return true;
}

void FGFMineable::StartKitAudit()
{
	if (!AssetLoadedHandle().IsValid())
	{
#if WITH_EDITOR
		AssetLoadedHandle() = FCoreUObjectDelegates::OnAssetLoaded.AddStatic(&AuditMesh);
#endif
	}
	for (TObjectIterator<UStaticMesh> It; It; ++It)
	{
		AuditMesh(*It);
	}
}

void FGFMineable::StopKitAudit()
{
	if (AssetLoadedHandle().IsValid())
	{
#if WITH_EDITOR
		FCoreUObjectDelegates::OnAssetLoaded.Remove(AssetLoadedHandle());
#endif
		AssetLoadedHandle().Reset();
	}
}

bool UGFMineableLibrary::SetMeshMineable(UStaticMesh* Mesh, EGFMineableMode Mode, const TArray<int32>& Slots, FName Kit, const FString& Reason)
{
	if (!Mesh)
	{
		return false;
	}
	Mesh->RemoveUserDataOfClass(UGFMineableAssetUserData::StaticClass());
	UGFMineableAssetUserData* Data = NewObject<UGFMineableAssetUserData>(Mesh, NAME_None, RF_Public | RF_Transactional);
	Data->Mode = Mode;
	Data->Slots = Mode == EGFMineableMode::Slots ? Slots : TArray<int32>();
	Data->Kit = Kit;
	Data->Reason = Reason;
	Mesh->AddAssetUserData(Data);
	Mesh->MarkPackageDirty();
	return true;
}

EGFMineableMode UGFMineableLibrary::GetMeshMineableMode(const UStaticMesh* Mesh, TArray<int32>& OutSlots)
{
	const UGFMineableAssetUserData* Data = FGFMineable::Find(Mesh);
	OutSlots = Data ? Data->Slots : TArray<int32>();
	return Data ? Data->Mode : EGFMineableMode::None;
}
