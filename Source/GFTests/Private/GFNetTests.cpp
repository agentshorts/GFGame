// Multi-client tests (PLAN.md section 4.1): one-process PIE, listen server + clients, runs under -nullrhi.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFLocalProfileSubsystem.h"
#include "GFInventoryComponent.h"
#include "GFVoxelWorld.h"
#include "GFSaveGame.h"
#include "GFSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

static constexpr EAutomationTestFlags GFNetTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

static UGFLocalProfileSubsystem* ProfileOf(UWorld* World)
{
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UGFLocalProfileSubsystem>() : nullptr;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetSmokeTest, "GFGame.Net.Smoke", GFNetTestFlags)
bool FGFNetSmokeTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	Do(Ctx, [this]()
	{
		UWorld* Server = ServerWorld();
		const AGFGameState* SGS = GameState(Server);
		TSet<FString> Keys;
		TSet<int32> Colors;
		int32 Hosts = 0;
		for (APlayerState* PS : SGS->PlayerArray)
		{
			const AGFPlayerState* GPS = CastChecked<AGFPlayerState>(PS);
			Keys.Add(GPS->PlayerKey);
			Colors.Add(GPS->ColorIndex);
			Hosts += GPS->bIsHost ? 1 : 0;
		}
		TestEqual(TEXT("Unique player keys"), Keys.Num(), 3);
		TestEqual(TEXT("Unique colours"), Colors.Num(), 3);
		TestEqual(TEXT("Exactly one host"), Hosts, 1);
		TestTrue(TEXT("Server's local player is the host"), LocalPlayerState(Server)->bIsHost);
		for (UWorld* C : ClientWorlds())
		{
			TestFalse(TEXT("Client is not host"), LocalPlayerState(C)->bIsHost);
			int32 Pawns = 0;
			for (TActorIterator<APawn> It(C); It; ++It)
			{
				Pawns += It->GetPlayerState() ? 1 : 0; // ignore local spectator pawns
			}
			TestEqual(TEXT("Each client sees all three characters"), Pawns, 3);
			TestEqual(TEXT("Host world id replicated"), GameState(C)->HostWorldId, SGS->HostWorldId);
		}
		// Move client 1's character on the server; client 2 must see it move.
		AGFPlayerState* C1 = PlayerStateByKey(Server, LocalPlayerState(ClientWorlds()[0])->PlayerKey);
		C1->GetPawn()->SetActorLocation(FVector(800.f, 300.f, 150.f), false, nullptr, ETeleportType::TeleportPhysics);
	});
	WaitUntil(Ctx, TEXT("client 2 sees client 1 move"), 10.f, []()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		const AGFPlayerState* Seen = PlayerStateByKey(Clients[1], LocalPlayerState(Clients[0])->PlayerKey);
		return Seen && Seen->GetPawn() && FVector::Dist2D(Seen->GetPawn()->GetActorLocation(), FVector(800.f, 300.f, 0.f)) < 50.f;
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetInventoryTest, "GFGame.Net.Inventory", GFNetTestFlags)
bool FGFNetInventoryTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> ClientKey = MakeShared<FString>();
	Do(Ctx, [this, ClientKey]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		AGFPlayerState* Mine = LocalPlayerState(Client);
		*ClientKey = Mine->PlayerKey;
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *ClientKey);
		GameState(Server)->MaxEditReach = 0.f;
		GameState(Server)->bEnforcePlotPermissions = false; // edits outside any plot (plots are tested in Net.Permissions)
		TestEqual(TEXT("Starter kit on the server"), OnServer->Inventory->GetCount(TEXT("Dirt")), 64);
		TestEqual(TEXT("Owner sees own inventory"), Mine->Inventory->GetCount(TEXT("Dirt")), 64);
		const AGFPlayerState* HostSeenByClient = PlayerStateByKey(Client, LocalPlayerState(Server)->PlayerKey);
		TestEqual(TEXT("Others' inventories are not replicated"), HostSeenByClient->Inventory->GetCount(TEXT("Dirt")), 0);
		TestFalse(TEXT("Client can't add items locally"), Mine->Inventory->AddItem(TEXT("Wood"), 99));
		TestEqual(TEXT("...and nothing changed"), Mine->Inventory->GetCount(TEXT("Wood")), 0);
		OnServer->Inventory->AddItem(TEXT("Wood"), 7);
		OnServer->Inventory->RemoveItem(TEXT("Stone"), 64);
	});
	WaitUntil(Ctx, TEXT("server grant reaches the owner and is written back"), 10.f, [ClientKey]()
	{
		UWorld* Client = ClientWorlds()[0];
		const UGFLocalProfileSubsystem* Profile = ProfileOf(Client);
		return LocalPlayerState(Client)->Inventory->GetCount(TEXT("Wood")) == 7 && Profile && Profile->GetCharacter().Inventory.GetCount(TEXT("Wood")) == 7
			&& Profile->GetCharacter().Inventory.GetCount(TEXT("Stone")) == 0;
	});
	TSharedRef<int32> RejectedBefore = MakeShared<int32>(0);
	Do(Ctx, [ClientKey, RejectedBefore]()
	{
		AGFPlayerState* Mine = LocalPlayerState(ClientWorlds()[0]);
		*RejectedBefore = PlayerStateByKey(ServerWorld(), *ClientKey)->GetEditsRejected();
		Mine->RequestEdit(FIntVector(10, 10, 1), TEXT("WoodPlank")); // costs one plank
		Mine->RequestEdit(FIntVector(12, 10, 1), TEXT("Stone"));     // no stone left: rejected, rolled back
	});
	WaitUntil(Ctx, TEXT("paid edit applied, unpaid edit rejected and rolled back"), 10.f, [ClientKey, RejectedBefore]()
	{
		UWorld* Server = ServerWorld();
		UWorld* Client = ClientWorlds()[0];
		const AGFPlayerState* OnServer = PlayerStateByKey(Server, *ClientKey);
		return Voxel(Server)->GetBlock(FIntVector(10, 10, 1)) == FName(TEXT("WoodPlank"))
			&& Voxel(Server)->GetBlock(FIntVector(12, 10, 1)).IsNone()
			&& OnServer->Inventory->GetCount(TEXT("WoodPlank")) == 63
			&& LocalPlayerState(Client)->Inventory->GetCount(TEXT("WoodPlank")) == 63
			&& OnServer->GetEditsRejected() == *RejectedBefore + 1
			&& Voxel(Client)->GetBlock(FIntVector(12, 10, 1)).IsNone()
			&& VoxelsConverged();
	});
	Do(Ctx, []() { LocalPlayerState(ClientWorlds()[0])->RequestEdit(FIntVector(10, 10, 1), NAME_None); });
	WaitUntil(Ctx, TEXT("removing gives the item back"), 10.f, [ClientKey]()
	{
		return PlayerStateByKey(ServerWorld(), *ClientKey)->Inventory->GetCount(TEXT("WoodPlank")) == 64 && VoxelsConverged();
	});
	Do(Ctx, []() { LocalPlayerState(ClientWorlds()[0])->RequestSetHeldItem(TEXT("Wood")); });
	WaitUntil(Ctx, TEXT("held item replicated to the host"), 10.f, [ClientKey]()
	{
		return PlayerStateByKey(ServerWorld(), *ClientKey)->HeldItem == FName(TEXT("Wood"))
			&& LocalPlayerState(ClientWorlds()[0])->HeldItem == FName(TEXT("Wood"));
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetVoxelEditsTest, "GFGame.Net.VoxelEdits", GFNetTestFlags)
bool FGFNetVoxelEditsTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	Do(Ctx, []()
	{
		AGFGameState* GS = GameState(ServerWorld());
		GS->MaxEditReach = 0.f;
		GS->bEnforcePlotPermissions = false; // edits outside any plot (plots are tested in Net.Permissions)
		GS->bFreeBuild = true;
		SetBadNetwork(true);
	});
	Wait(Ctx, 0.5f);
	// 3 players x 10 edits per frame x 30 frames, all random (including invalid ones), under the Bad profile.
	TSharedRef<FRandomStream> Rng = MakeShared<FRandomStream>(4242);
	Repeat(Ctx, 30, 0.05f, [Rng](int32)
	{
		TArray<UWorld*> Worlds = ClientWorlds();
		Worlds.Insert(ServerWorld(), 0);
		static const FName Blocks[] = { TEXT("Grass"), TEXT("Dirt"), TEXT("WoodPlank"), TEXT("Stone") };
		for (UWorld* W : Worlds)
		{
			AGFPlayerState* PS = LocalPlayerState(W);
			for (int32 i = 0; i < 10; ++i)
			{
				const FIntVector Cell(Rng->RandRange(0, 15), Rng->RandRange(0, 15), Rng->RandRange(0, 4));
				if (Rng->FRand() < 0.05f)
				{
					PS->RequestEdit(FIntVector(-5, 0, 0), TEXT("Stone")); // out of bounds: always rejected
				}
				else if (Rng->FRand() < 0.6f)
				{
					PS->RequestEdit(Cell, Blocks[Rng->RandRange(0, 3)]);
				}
				else
				{
					PS->RequestEdit(Cell, NAME_None);
				}
			}
		}
	});
	// Two clients race for the same cell in the same frame.
	Do(Ctx, []()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		LocalPlayerState(Clients[0])->RequestEdit(FIntVector(30, 30, 3), TEXT("Stone"));
		LocalPlayerState(Clients[1])->RequestEdit(FIntVector(30, 30, 3), TEXT("WoodPlank"));
	});
	TSharedRef<FString> Why = MakeShared<FString>();
	WaitUntil(Ctx, TEXT("all clients converge to the server's grid (Bad network)"), 45.f, [Why]() { return VoxelsConverged(&*Why); });
	Do(Ctx, [this, Why]()
	{
		const AGFVoxelWorld* SV = Voxel(ServerWorld());
		TestFalse(TEXT("Raced cell holds exactly one winner"), SV->GetBlock(FIntVector(30, 30, 3)).IsNone());
		int32 Accepted = 0;
		int32 Rejected = 0;
		for (APlayerState* PS : GameState(ServerWorld())->PlayerArray)
		{
			Accepted += CastChecked<AGFPlayerState>(PS)->GetEditsAccepted();
			Rejected += CastChecked<AGFPlayerState>(PS)->GetEditsRejected();
		}
		TestTrue(TEXT("Some edits accepted"), Accepted > 100);
		TestTrue(TEXT("Invalid edits rejected"), Rejected > 10);
		AddInfo(FString::Printf(TEXT("Edits accepted %d, rejected %d, chunk revision total %lld"), Accepted, Rejected, SV->GetTotalRevision()));
		SetBadNetwork(false);
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetJoinInProgressTest, "GFGame.Net.JoinInProgress", GFNetTestFlags)
bool FGFNetJoinInProgressTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	Do(Ctx, []()
	{
		AGFGameState* GS = GameState(ServerWorld());
		GS->MaxEditReach = 0.f;
		GS->bEnforcePlotPermissions = false; // edits outside any plot (plots are tested in Net.Permissions)
		GS->bFreeBuild = true;
		// A built-up plot: ~4,000 blocks across many chunks (bases + deltas + compactions).
		AGFVoxelWorld* SV = Voxel(ServerWorld());
		FRandomStream Rng(99);
		static const FName Blocks[] = { TEXT("Dirt"), TEXT("WoodPlank"), TEXT("Stone") };
		for (int32 i = 0; i < 4000; ++i)
		{
			SV->ServerApplyEdit(FIntVector(Rng.RandRange(0, 63), Rng.RandRange(0, 63), Rng.RandRange(1, 12)), Blocks[Rng.RandRange(0, 2)]);
		}
	});
	WaitUntil(Ctx, TEXT("first client caught up"), 30.f, []() { return VoxelsConverged(); });
	LateJoin(Ctx);
	Do(Ctx, [this]()
	{
		const TArray<UWorld*> Clients = ClientWorlds();
		TestEqual(TEXT("Late joiner present"), Clients.Num(), 2);
		const AGFVoxelWorld* Late = Voxel(Clients.Last());
		TestEqual(TEXT("Late joiner snapshot matches the server"), static_cast<int64>(Late->GetAuthoritativeHash()), static_cast<int64>(Voxel(ServerWorld())->GetAuthoritativeHash()));
		TestEqual(TEXT("Late joiner sees 3 players"), GameState(Clients.Last())->PlayerArray.Num(), 3);
		LocalPlayerState(Clients.Last())->RequestEdit(FIntVector(1, 1, 20), TEXT("Stone"));
		LocalPlayerState(Clients[0])->RequestEdit(FIntVector(2, 1, 20), TEXT("Dirt"));
	});
	WaitUntil(Ctx, TEXT("edits after the late join converge"), 15.f, []()
	{
		return VoxelsConverged() && Voxel(ServerWorld())->GetBlock(FIntVector(1, 1, 20)) == FName(TEXT("Stone"));
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFSaveHostGuestTest, "GFGame.Save.HostGuest", GFNetTestFlags)
bool FGFSaveHostGuestTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FString> GuestKey = MakeShared<FString>();
	Do(Ctx, [GuestKey]()
	{
		UWorld* Server = ServerWorld();
		GameState(Server)->MaxEditReach = 0.f;
		GameState(Server)->bEnforcePlotPermissions = false; // edits outside any plot (plots are tested in Net.Permissions)
		*GuestKey = LocalPlayerState(ClientWorlds()[0])->PlayerKey;
		PlayerStateByKey(Server, *GuestKey)->Inventory->AddItem(TEXT("Wood"), 5);
		LocalPlayerState(ClientWorlds()[0])->RequestEdit(FIntVector(20, 20, 1), TEXT("Stone"));
		LocalPlayerState(Server)->RequestEdit(FIntVector(21, 20, 1), TEXT("WoodPlank"));
	});
	WaitUntil(Ctx, TEXT("guest edit and write-back"), 10.f, []()
	{
		const UGFLocalProfileSubsystem* Profile = ProfileOf(ClientWorlds()[0]);
		return VoxelsConverged() && Voxel(ServerWorld())->GetBlock(FIntVector(20, 20, 1)) == FName(TEXT("Stone"))
			&& Profile->GetCharacter().Inventory.GetCount(TEXT("Wood")) == 5 && Profile->GetCharacter().Inventory.GetCount(TEXT("Stone")) == 63;
	});
	Do(Ctx, [this, GuestKey]()
	{
		UWorld* Server = ServerWorld();
		AGFVoxelWorld* SV = Voxel(Server);
		const uint32 WorldHash = SV->GetAuthoritativeHash();

		// World save (host): blocks + who visited, with the server's copy of the guest's character.
		UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(Server);
		TArray<uint8> Bytes;
		TestTrue(TEXT("World serialises"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
		UGFSaveGame* Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		const FGFGuestRecord* Rec = Back ? Back->Guests.FindByPredicate([&](const FGFGuestRecord& R) { return R.PlayerKey == *GuestKey; }) : nullptr;
		TestTrue(TEXT("Guest record saved with the world"), Rec && Rec->bHasCharacter && Rec->LastCharacter.Inventory.GetCount(TEXT("Wood")) == 5);
		TestEqual(TEXT("Both players recorded"), Back ? Back->Guests.Num() : 0, 2);
		TestEqual(TEXT("World id saved"), Back ? Back->HostWorldId : FString(), GameState(Server)->HostWorldId);

		// Character saves: the guest's from its own machine; the host's from the live player state.
		const UGFCharacterSave* GuestChar = UGFSaveSubsystem::CaptureCharacter(ClientWorlds()[0]->GetGameInstance());
		TestEqual(TEXT("Guest's own character save has their items"), GuestChar->Character.Inventory.GetCount(TEXT("Wood")), 5);
		TestTrue(TEXT("Guest save matches the server's copy"), GuestChar->Character.Inventory == PlayerStateByKey(Server, *GuestKey)->Inventory->GetInventory());
		const UGFCharacterSave* HostChar = UGFSaveSubsystem::CaptureCharacter(Server->GetGameInstance());
		TestEqual(TEXT("Host's character paid for their plank"), HostChar->Character.Inventory.GetCount(TEXT("WoodPlank")), 63);

		// Wipe and restore the world from the save.
		SV->ClearAll();
		GameState(Server)->GuestRecords.Reset();
		TestTrue(TEXT("Apply world save"), UGFSaveSubsystem::ApplyToWorld(Server, Back));
		TestEqual(TEXT("Blocks restored"), static_cast<int64>(SV->GetAuthoritativeHash()), static_cast<int64>(WorldHash));
		TestTrue(TEXT("Guest records restored"), GameState(Server)->GuestRecords.Contains(*GuestKey));

		// Rejoin rule: the host's newer copy beats a stale upload, and a second upload can't add items.
		AGFPlayerState* OnServer = PlayerStateByKey(Server, *GuestKey);
		FGFGuestRecord& Live = GameState(Server)->GuestRecords.FindChecked(*GuestKey);
		Live.LastCharacter.Revision = 1000;
		Live.LastCharacter.Inventory = FGFInventory();
		Live.LastCharacter.Inventory.Add(TEXT("Wood"), 42);
		OnServer->bCharacterReady = false;
		FGFCharacterData Stale;
		Stale.bInitialized = true;
		Stale.Revision = 1;
		Stale.Inventory.Add(TEXT("Wood"), 500);
		OnServer->ServerAcceptCharacter(Stale);
		TestEqual(TEXT("Host copy wins over a stale upload"), OnServer->Inventory->GetCount(TEXT("Wood")), 42);
		FGFCharacterData Greedy = Stale;
		Greedy.Revision = 5000;
		OnServer->ServerAcceptCharacter(Greedy);
		TestEqual(TEXT("Second upload in a session is ignored"), OnServer->Inventory->GetCount(TEXT("Wood")), 42);
	});
	WaitUntil(Ctx, TEXT("restored world reaches the client"), 15.f, []() { return VoxelsConverged(); });
	Stop(Ctx);
	return true;
}

#endif
