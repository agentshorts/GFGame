#include "GFMiningFX.h"
#include "GFDataTables.h"
#include "GFLog.h"
#include "GFMagicTables.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
	TSet<FString>& MissingFX()
	{
		static TSet<FString> Logged;
		return Logged;
	}

	void LogMissingOnce(const FString& Key)
	{
		if (Key.IsEmpty() || MissingFX().Contains(Key))
		{
			return;
		}
		MissingFX().Add(Key);
		UE_LOG(LogGF, Warning, TEXT("Mining FX: %s did not load as a Niagara system"), *Key);
	}

	FVector SafeNormal(const FVector& Normal)
	{
		const FVector N = Normal.GetSafeNormal();
		return N.IsNearlyZero() ? FVector::UpVector : N;
	}
}

AGFMiningFXRelay::AGFMiningFXRelay()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void AGFMiningFXRelay::Dispatch(const FGFMiningFXEvent& Event)
{
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() != NM_DedicatedServer)
	{
		FGFMiningFX::SpawnCosmetic(GetWorld(), Event);
	}
	if (!World || World->GetNetMode() == NM_Standalone || World->GetNetDriver() == nullptr)
	{
		return;
	}
	MulticastCosmetic(Event);
}

void AGFMiningFXRelay::MulticastCosmetic_Implementation(FGFMiningFXEvent Event)
{
	if (HasAuthority())
	{
		return;
	}
	FGFMiningFX::SpawnCosmetic(GetWorld(), Event);
}

FGFOnMiningFX& FGFMiningFX::OnPlayed()
{
	static FGFOnMiningFX Delegate;
	return Delegate;
}

FGFOnMiningFX& FGFMiningFX::OnCosmetic()
{
	static FGFOnMiningFX Delegate;
	return Delegate;
}

FName FGFMiningFX::ElementName(EGFMagicElement Element)
{
	switch (Element)
	{
	case EGFMagicElement::Magma: return TEXT("Magma");
	case EGFMagicElement::Root: return TEXT("Root");
	case EGFMagicElement::Frost: return TEXT("Frost");
	default: return TEXT("Spark");
	}
}

int32 FGFMiningFX::CrackStage(float Remaining, float Full)
{
	if (Full <= KINDA_SMALL_NUMBER || Remaining <= 0.f)
	{
		return 0;
	}
	const float Ratio = Remaining / Full;
	if (Ratio <= 0.25f)
	{
		return 3;
	}
	if (Ratio <= 0.50f)
	{
		return 2;
	}
	if (Ratio <= 0.75f)
	{
		return 1;
	}
	return 0;
}

EGFMiningFXKit FGFMiningFX::KitForBlock(FName BlockId)
{
	if (BlockId == TEXT("CrystalVein") || BlockId == TEXT("Crystal"))
	{
		return EGFMiningFXKit::Crystal;
	}
	if (FGFMagicCatalog::AffinityOrDefault(BlockId).bTree)
	{
		return EGFMiningFXKit::Tree;
	}
	return EGFMiningFXKit::None;
}

void FGFMiningFX::Collect(float BeforeRemaining, float AfterRemaining, float Full, bool bBroke, EGFMagicElement Element, EGFMiningFXKit Kit, const FVector& Location, const FVector& Normal, TArray<FGFMiningFXEvent>& Out)
{
	FGFMiningFXEvent Event;
	Event.Location = Location;
	Event.Normal = SafeNormal(Normal);
	Event.Element = Element;
	Event.Kit = Kit;
	if (bBroke || AfterRemaining <= 0.f)
	{
		Event.Kind = EGFMiningFXKind::Break;
		Out.Add(Event);
		return;
	}
	Event.Kind = EGFMiningFXKind::Hit;
	Out.Add(Event);
	const int32 Was = CrackStage(BeforeRemaining, Full);
	const int32 Now = CrackStage(AfterRemaining, Full);
	if (Now > Was)
	{
		Event.Kind = EGFMiningFXKind::Crack;
		Event.CrackStage = Now;
		Out.Add(Event);
	}
}

const UDataTable* FGFMiningFX::Table()
{
	return FGFDataTables::Get<FGFMiningFXRow>(TEXT("DT_MiningFX"));
}

FString FGFMiningFX::ResolveFile(const FString& SystemPath)
{
	if (SystemPath.IsEmpty())
	{
		return FString();
	}
	if (SystemPath.StartsWith(TEXT("/")))
	{
		const FString Package = SoftPathFor(SystemPath).GetLongPackageName();
		FString PackageFile;
		if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package, &PackageFile))
		{
			return FPaths::ConvertRelativePathToFull(PackageFile);
		}
	}
	if (FPaths::FileExists(SystemPath))
	{
		return FPaths::ConvertRelativePathToFull(SystemPath);
	}
	const FString Full = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / SystemPath);
	if (FPaths::FileExists(Full))
	{
		return Full;
	}
	return FString();
}

bool FGFMiningFX::RowResolves(const FGFMiningFXRow& Row)
{
	return !ResolveFile(Row.SystemPath).IsEmpty();
}

FSoftObjectPath FGFMiningFX::SoftPathFor(const FString& SystemPath)
{
	FString Path = SystemPath;
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (Path.IsEmpty())
	{
		return FSoftObjectPath();
	}
	if (Path.StartsWith(TEXT("/")) && Path.Contains(TEXT(".")))
	{
		return FSoftObjectPath(Path);
	}
	FString Mount = Path;
	if (Mount.StartsWith(TEXT("Plugins/")))
	{
		FString After = Mount.Mid(8);
		int32 Slash = INDEX_NONE;
		After.FindChar(TEXT('/'), Slash);
		const FString Plugin = Slash == INDEX_NONE ? After : After.Left(Slash);
		FString Rest = Slash == INDEX_NONE ? FString() : After.Mid(Slash + 1);
		if (Rest.StartsWith(TEXT("Content/")))
		{
			Rest = Rest.Mid(8);
		}
		Mount = FString::Printf(TEXT("/%s/%s"), *Plugin, *Rest);
	}
	else if (!Mount.StartsWith(TEXT("/")))
	{
		Mount = TEXT("/Game/") + Mount;
	}
	const FString Base = FPaths::GetBaseFilename(Mount);
	const FString Dir = FPaths::GetPath(Mount);
	if (Base.IsEmpty() || Dir.IsEmpty())
	{
		return FSoftObjectPath();
	}
	return FSoftObjectPath((Dir / Base) + TEXT(".") + Base);
}

void FGFMiningFX::Gather(EGFMiningFXKind Kind, EGFMagicElement Element, EGFMiningFXKit Kit, TArray<const FGFMiningFXRow*>& Out)
{
	const UDataTable* Rows = Table();
	if (!Rows)
	{
		return;
	}
	const FName ElementId = ElementName(Element);
	for (const TPair<FName, uint8*>& Pair : Rows->GetRowMap())
	{
		const FGFMiningFXRow* Row = reinterpret_cast<const FGFMiningFXRow*>(Pair.Value);
		if (!Row || Row->Kind != Kind)
		{
			continue;
		}
		const bool bElement = Row->Element == ElementId || Row->Element == TEXT("Any");
		const bool bKit = Row->Kit == Kit || Row->Kit == EGFMiningFXKit::Any;
		if (bElement && bKit)
		{
			Out.Add(Row);
		}
	}
}

UNiagaraSystem* FGFMiningFX::LoadSystem(const FString& SystemPath)
{
	const FSoftObjectPath Soft = SoftPathFor(SystemPath);
	if (Soft.IsNull())
	{
		LogMissingOnce(SystemPath);
		return nullptr;
	}
	const FString Package = Soft.GetLongPackageName();
	if (Package.IsEmpty() || !FPackageName::DoesPackageExist(Package))
	{
		LogMissingOnce(SystemPath);
		return nullptr;
	}
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Soft.TryLoad());
	if (!System)
	{
		LogMissingOnce(SystemPath);
	}
	return System;
}

UNiagaraSystem* FGFMiningFX::LoadFor(EGFMiningFXKind Kind, EGFMagicElement Element, EGFMiningFXKit Kit)
{
	TArray<const FGFMiningFXRow*> Rows;
	Gather(Kind, Element, Kit, Rows);
	const FGFMiningFXRow* Best = nullptr;
	const FName ElementId = ElementName(Element);
	for (const FGFMiningFXRow* Row : Rows)
	{
		if (!Row)
		{
			continue;
		}
		if (Row->Element == ElementId && Row->Kit == Kit)
		{
			Best = Row;
			break;
		}
		if (!Best)
		{
			Best = Row;
		}
	}
	if (!Best)
	{
		LogMissingOnce(FString::Printf(TEXT("kind %d %s"), static_cast<int32>(Kind), *ElementId.ToString()));
		return nullptr;
	}
	return LoadSystem(Best->SystemPath);
}

void FGFMiningFX::SpawnCosmetic(UWorld* World, const FGFMiningFXEvent& Event)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	OnCosmetic().Broadcast(Event);
	TArray<const FGFMiningFXRow*> Rows;
	Gather(Event.Kind, Event.Element, Event.Kit, Rows);
	TSet<FString> Seen;
	const FRotator Rotation = Event.Normal.ToOrientationRotator();
	for (const FGFMiningFXRow* Row : Rows)
	{
		if (!Row || Seen.Contains(Row->SystemPath))
		{
			continue;
		}
		Seen.Add(Row->SystemPath);
		if (UNiagaraSystem* System = LoadSystem(Row->SystemPath))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Event.Location, Rotation);
		}
	}
}
