#include "GFPaintVolume.h"

#include "EngineUtils.h"
#include "GFDyes.h"
#include "GFEconomy.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFVoxelWorld.h"
#include "VoxelFinish.h"

TArray<FLinearColor> FGFPaintCost::VisibleColors(const FFinish& Finish)
{
	auto Shown = [&Finish](const FLinearColor& C)
	{
		return FLinearColor(FMath::Clamp(C.R * Finish.Tint.R, 0.f, 1.f), FMath::Clamp(C.G * Finish.Tint.G, 0.f, 1.f), FMath::Clamp(C.B * Finish.Tint.B, 0.f, 1.f), 1.f);
	};
	TArray<FLinearColor> Out = { Shown(Finish.ColorA) };
	if (Finish.PatternId != VoxelFinish::SolidPatternId)
	{
		Out.Add(Shown(Finish.ColorB));
	}
	return Out;
}

TArray<FName> FGFPaintCost::DyesFor(const FFinish& Finish)
{
	TArray<FName> Out;
	for (const FLinearColor& C : VisibleColors(Finish))
	{
		Out.AddUnique(FGFDyes::DyeForColor(C));
	}
	return Out;
}

bool FGFPaintCredits::TrySpend(const FString& Account, const TArray<FLinearColor>& Colors, TFunctionRef<bool(const TArray<FGFItemStack>&)> Burn)
{
	if (Account.IsEmpty() || Colors.Num() == 0)
	{
		return false;
	}
	TMap<FName, int32>& Mine = Credits.FindOrAdd(Account);
	TArray<FName> Dyes;
	TArray<FGFItemStack> ToBurn;
	for (const FLinearColor& C : Colors)
	{
		const FName Dye = FGFDyes::DyeForColor(C);
		if (Dyes.Contains(Dye))
		{
			continue;
		}
		Dyes.Add(Dye);
		if (Mine.FindRef(Dye) <= 0)
		{
			ToBurn.Append(FGFDyes::CostForBlocks(C, FGFDyes::BlocksPerDye));
		}
	}
	if (ToBurn.Num() > 0)
	{
		if (!Burn(ToBurn))
		{
			return false;
		}
		for (const FGFItemStack& S : ToBurn)
		{
			Mine.FindOrAdd(S.ItemId) += S.Count * FGFDyes::BlocksPerDye;
		}
	}
	for (const FName Dye : Dyes)
	{
		--Mine.FindOrAdd(Dye);
	}
	return true;
}

int32 FGFPaintCredits::GetCredits(const FString& Account, FName DyeItem) const
{
	const TMap<FName, int32>* Mine = Credits.Find(Account);
	return Mine ? Mine->FindRef(DyeItem) : 0;
}

AGFPaintVolume* AGFPaintVolume::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFPaintVolume> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AGFPaintVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!HasAuthority())
	{
		return;
	}
	// The hooks only run inside this actor's own session, so capturing this is safe.
	SetCanAffordAndSpend([this](const FFinish& Finish, APlayerState* Payer) { return ServerSpend(Finish, Payer); });
	SetPaintTargetResolver([this](const FIntVector& Cell, FVector& OutCenter) { return ResolveTarget(Cell, OutCenter); });
}

bool AGFPaintVolume::ServerSpend(const FFinish& Finish, APlayerState* Payer)
{
	const AGFPlayerState* PS = Cast<AGFPlayerState>(Payer);
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	if (!HasAuthority() || !PS || PS->PlayerKey.IsEmpty() || !GS || !GS->HasAuthority())
	{
		return false;
	}
	if (GS->bFreeBuild)
	{
		return true;
	}
	const FString Key = PS->PlayerKey;
	return Credits.TrySpend(Key, FGFPaintCost::VisibleColors(Finish), [GS, &Key](const TArray<FGFItemStack>& Stacks)
	{
		return GS->ServerBurn(FGuid::NewGuid(), Key, Stacks, BurnReason()) == EGFTxResult::Applied;
	});
}

bool AGFPaintVolume::ResolveTarget(const FIntVector& Cell, FVector& OutCenter) const
{
	const AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	const AGFVoxelWorld* Voxel = GS ? GS->GetVoxelWorld() : nullptr;
	// FGFVoxelGrid::Get is a bounds check plus a chunk lookup: a missing chunk reads as air and is never created.
	if (!Voxel || !Voxel->GetAuthoritativeGrid().IsSolid(Cell))
	{
		return false;
	}
	OutCenter = Voxel->CellToWorld(Cell) + FVector(0.f, 0.f, GFBlockSize * 0.5f);
	return true;
}
