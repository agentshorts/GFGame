#include "GFTestKit.h"
#include "GFFerryDirector.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

TArray<FGFItemStack> FGFTestKit::Contents(FName PassItem)
{
	TArray<FGFItemStack> Out;
	// Placeable blocks (the item each DT_Blocks row places and drops).
	Out.Add(FGFItemStack(TEXT("Dirt"), 64));
	Out.Add(FGFItemStack(TEXT("WoodPlank"), 64));
	Out.Add(FGFItemStack(TEXT("Stone"), 32));
	Out.Add(FGFItemStack(TEXT("Glass"), 32));
	Out.Add(FGFItemStack(TEXT("Fabric"), 16));
	// Crafting materials: Wood makes planks at no station, Fiber goes into a chair at a bench.
	Out.Add(FGFItemStack(TEXT("Wood"), 20));
	Out.Add(FGFItemStack(TEXT("Fiber"), 10));
	// One outfit item.
	Out.Add(FGFItemStack(TEXT("LabCoat"), 1));
	if (!PassItem.IsNone())
	{
		Out.Add(FGFItemStack(PassItem, 1));
	}
	return Out;
}

FName FGFTestKit::DefaultPassItem()
{
	const AGFFerryDirector* Ferry = GetDefault<AGFFerryDirector>();
	const FName FromConfig = Ferry ? Ferry->Tuning.PassItem : NAME_None;
	return FromConfig.IsNone() ? FName(TEXT("FerryPass")) : FromConfig;
}

TArray<FGFItemStack> FGFTestKit::Missing(const FGFInventory& Inventory, FName PassItem)
{
	TArray<FGFItemStack> Out;
	for (const FGFItemStack& Want : Contents(PassItem))
	{
		const int32 Short = Want.Count - Inventory.GetCount(Want.ItemId);
		if (Short > 0)
		{
			Out.Add(FGFItemStack(Want.ItemId, Short));
		}
	}
	return Out;
}

int32 FGFTestKit::TopUp(FGFInventory& Inventory, FName PassItem)
{
	int32 Added = 0;
	for (const FGFItemStack& S : Missing(Inventory, PassItem))
	{
		Inventory.Add(S.ItemId, S.Count);
		Added += S.Count;
	}
	return Added;
}

int32 FGFTestKit::Grant(AGFPlayerState* PlayerState)
{
	UGFInventoryComponent* Inv = PlayerState ? PlayerState->Inventory.Get() : nullptr;
	if (!Inv || !Inv->CanMutate())
	{
		return -1;
	}
	const TArray<FGFItemStack> Need = Missing(Inv->GetInventory(), DefaultPassItem());
	int32 Added = 0;
	for (const FGFItemStack& S : Need)
	{
		Added += S.Count;
	}
	if (Need.Num() > 0 && !Inv->AddAll(Need))
	{
		return -1;
	}
	UE_LOG(LogGF, Log, TEXT("TestKit: granted %d items (%d stacks) to %s"), Added, Need.Num(), *PlayerState->PlayerKey);
	return Added;
}

int32 FGFTestKit::GrantAll(UWorld* World)
{
	int32 Players = 0;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return 0;
	}
	for (TActorIterator<AGFPlayerState> It(World); It; ++It)
	{
		if (It->bCharacterReady && Grant(*It) >= 0)
		{
			++Players;
		}
	}
	return Players;
}

bool FGFTestKit::IsRequestedOnCommandLine()
{
	return FParse::Param(FCommandLine::Get(), TEXT("GFTestKit"));
}

bool UGFTestKitSubsystem::IsAutoGrantOn() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bAutoGrantTestKit || FGFTestKit::IsRequestedOnCommandLine();
#endif
}

bool UGFTestKitSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGFTestKitSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!IsAutoGrantOn() || InWorld.GetNetMode() == NM_Client)
	{
		return;
	}
	UE_LOG(LogGF, Log, TEXT("TestKit: auto-grant on"));
	InWorld.GetTimerManager().SetTimer(PollHandle, FTimerDelegate::CreateUObject(this, &UGFTestKitSubsystem::Poll), 1.f, true, 0.5f);
}

void UGFTestKitSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollHandle);
	}
	Granted.Reset();
	Super::Deinitialize();
}

void UGFTestKitSubsystem::Poll()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AGFPlayerState> It(World); It; ++It)
	{
		// Wait for the character upload: it replaces the whole inventory.
		if (!It->bCharacterReady || Granted.Contains(*It))
		{
			continue;
		}
		if (FGFTestKit::Grant(*It) >= 0)
		{
			Granted.Add(*It);
		}
	}
}

#if !UE_BUILD_SHIPPING
static void GFRunTestKitCommand(const TArray<FString>& Args, UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogGF, Warning, TEXT("TestKit: gf.TestKit runs on the host only"));
		return;
	}
	const int32 Players = FGFTestKit::GrantAll(World);
	UE_LOG(LogGF, Log, TEXT("TestKit: gf.TestKit topped up %d player(s)"), Players);
}

static FAutoConsoleCommandWithWorldAndArgs GGFTestKitCommand(
	TEXT("gf.TestKit"),
	TEXT("Host only. Tops every player up to the dev test kit: blocks, Wood, Fiber, a Lab Coat and a ferry lifetime pass."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GFRunTestKitCommand));
#endif
