// Faction protection over the network (listen server + 2 clients, ids ProtPA / ProtPB). Land is two discs installed as the
// safe-zone override on every machine, answering IsSafeFor from the replicated social Access the same way the
// territory map does (the territory map itself is GFGame-private and covered by GFGame.Territory.Protection).
// C1 founds ProtPA (leader), the host joins ProtPA (member), C0 founds ProtPB and visits ProtPA land. Checks: visitors start
// unprotected and can't build, break or use jail power there but can still paint; the leader toggles protection per
// player over the net and it replicates (client territory answers IsSafeFor too); turning it off records the Dragger
// and timestamp on the player state and posts the config chat line; only the leader grants the per-member grant (which
// covers Protect / Unprotect and Guest / Unguest), checked on the server; members and the host can't be toggled; guests get full rights plus protection.
#include "GFNetTestUtils.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFPlayerController.h"
#include "GFInventoryComponent.h"
#include "GFModeTypes.h"
#include "GFPaintVolume.h"
#include "GFSafeZone.h"
#include "GFSocialComponent.h"
#include "GFSocialDirector.h"
#include "GFVoxelWorld.h"
#include "VoxelFinish.h"
#include "VoxelFinishPainterComponent.h"
#include "VoxelFinishSession.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace GFNetProtectionTest
{
	static const FName ProtPA(TEXT("PA"));
	static const FName ProtPB(TEXT("PB"));
	static const FName ProtRose(TEXT("RoseDye"));

	struct FProtRun
	{
		FString Host, C0, C1;
		FVector Origin = FVector::ZeroVector;
		FIntVector Ground = FIntVector::ZeroValue; // solid, in reach of C0, on ProtPA land
		FIntVector Air = FIntVector::ZeroValue;    // empty, in reach of C0, on ProtPA land
		FName GroundBlock;
		bool bCells = false;
		int32 Id = 0;
		int32 Id2 = 0;
		FString Expected;
		int32 ChatBefore = 0;
	};

	static AGFPlayerState* ProtClient(int32 i) { return LocalPlayerState(ClientWorlds()[i]); }
	static AGFPlayerState* ProtOnServer(const FString& Key) { return PlayerStateByKey(ServerWorld(), Key); }
	static UGFSocialComponent* ProtServerSocial() { return AGFSocialDirector::GetSocial(ServerWorld(), true); }
	static UGFSocialComponent* ProtClientSocial(int32 i) { return AGFSocialDirector::GetSocial(ClientWorlds()[i], false); }

	static void ProtPlace(const FString& Key, const FVector& At)
	{
		AGFPlayerState* PS = ProtOnServer(Key);
		if (APawn* Pawn = PS ? PS->GetPawn() : nullptr)
		{
			Pawn->TeleportTo(FVector(At.X, At.Y, Pawn->GetActorLocation().Z), FRotator::ZeroRotator, false, true);
		}
	}

	static int32 ProtAsk(int32 i, FName Verb, const FString& TargetKey)
	{
		FGFModeRequest R = FGFModeRequest::Make(UGFSocialComponent::ModeName, Verb);
		const AGFPlayerState* T = PlayerStateByKey(ClientWorlds()[i], TargetKey);
		R.Ints.Add(T ? T->GetPlayerId() : -1);
		return ProtClient(i)->RequestMode(R);
	}

	static const FGFModeResult* ProtResult(int32 i, int32 Id) { return ProtClient(i)->ModeResults.Find(Id); }

	/** ProtPA disc at Origin, ProtPB disc 3500 cm east of it, 1500 cm each; protection read live from this world's social Access. */
	struct FProtLands : public IGFSafeZoneQuery
	{
		TWeakObjectPtr<UWorld> World;
		FVector Origin = FVector::ZeroVector;
		static constexpr float Radius = 1500.f;
		virtual FName OwnerAt(const FVector& L) const override
		{
			if (FVector::Dist2D(L, Origin) <= Radius)
			{
				return ProtPA;
			}
			return FVector::Dist2D(L, Origin + FVector(3500.f, 0.f, 0.f)) <= Radius ? ProtPB : NAME_None;
		}
		virtual bool IsInsideSafe(const FVector& L) const override { return !OwnerAt(L).IsNone(); }
		virtual FVector ClosestSafePoint(const FVector& L) const override { return IsInsideSafe(L) ? L : Origin; }
		virtual bool ClosestEdge(const FVector& L, FVector& OutPoint, FVector& OutOutward) const override { return false; }
		virtual bool HasAnySafe() const override { return true; }
		virtual bool IsSafeFor(const FGFSafeSubject& Who, const FVector& L) const override
		{
			const UGFSocialComponent* S = World.IsValid() ? AGFSocialDirector::GetSocial(World.Get(), false) : nullptr;
			static const FGFFactionAccess NoAccess;
			return FGFProtectionRules::IsProtectedOn(Who.Key, Who.GroupId, Who.bHost, OwnerAt(L), S ? S->Access : NoAccess);
		}
	};

	static void ProtSetLand(UWorld* World, const FVector& Origin)
	{
		TSharedPtr<FProtLands> Lands = MakeShared<FProtLands>();
		Lands->World = World;
		Lands->Origin = Origin;
		UGFSafeZoneSubsystem::Get(World)->SetOverride(Lands);
	}

	static bool ProtSafeOn(UWorld* World, const FString& Key)
	{
		const AGFPlayerState* PS = PlayerStateByKey(World, Key);
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		const UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(World);
		return Pawn && Zones && Zones->IsSafeFor(FGFSafeSubject(PS->PlayerKey, PS->FactionId, PS->bIsHost), Pawn->GetActorLocation());
	}

	static bool ProtTryEdit(const FString& Key, const FIntVector& Cell, FName Block, FString& OutWhy)
	{
		AGFPlayerState* PS = ProtOnServer(Key);
		return PS && PS->ServerHandleEdit(Cell, Block, &OutWhy);
	}

	static FFinish ProtRed()
	{
		FFinish F;
		F.PatternId = VoxelFinish::SolidPatternId;
		F.ColorA = FLinearColor(0.9f, 0.1f, 0.1f);
		F.ColorB = FLinearColor::White;
		return F;
	}

	static uint16 ProtFinishAt(const FIntVector& Cell)
	{
		const AGFPaintVolume* Vol = AGFPaintVolume::Find(ServerWorld());
		return Vol ? Vol->GetSession().GetGrid().GetFinishId(Cell) : 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetProtectionTest, "GFGame.Protection.Net", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFNetProtectionTest::RunTest(const FString&)
{
	using namespace GFNetProtectionTest;
	using C = FGFSocialCodes;
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FProtRun> Run = MakeShared<FProtRun>();

	Do(Ctx, [this, Run]()
	{
		UWorld* Server = ServerWorld();
		Run->Host = LocalPlayerState(Server)->PlayerKey;
		Run->C0 = ProtClient(0)->PlayerKey;
		Run->C1 = ProtClient(1)->PlayerKey;
		AGFPlayerState* Visitor = ProtOnServer(Run->C0);
		if (!TestNotNull(TEXT("Social director"), ProtServerSocial()) || !Visitor || !Visitor->GetPawn())
		{
			return;
		}
		Run->Origin = Visitor->GetPawn()->GetActorLocation();
		ProtSetLand(Server, Run->Origin);
		for (UWorld* W : ClientWorlds())
		{
			ProtSetLand(W, Run->Origin);
		}
		AGFGameState* GS = GameState(Server);
		GS->bFreeBuild = true;                 // no block items needed: the test is about land rights
		GS->bEnforcePlotPermissions = false;   // plot rules stay on top in play; off here so the visitor rule is what refuses
		TestEqual(TEXT("C1 founds PA (leader)"), GS->ServerClaimFaction(ProtOnServer(Run->C1), ProtPA), FName(TEXT("Ok")));
		TestEqual(TEXT("Host joins PA"), GS->ServerClaimFaction(LocalPlayerState(Server), ProtPA), FName(TEXT("Ok")));
		TestEqual(TEXT("C0 founds PB"), GS->ServerClaimFaction(Visitor, ProtPB), FName(TEXT("Ok")));
		ProtPlace(Run->Host, Run->Origin + FVector(300.f, 0.f, 0.f));
		ProtPlace(Run->C1, Run->Origin + FVector(-300.f, 0.f, 0.f));

		// Cells next to the visitor, measured the way the server measures them.
		const AGFVoxelWorld* V = Voxel(Server);
		const FGFVoxelGrid& Grid = V->GetAuthoritativeGrid();
		const FIntVector P = V->WorldToCell(Run->Origin);
		for (int32 Z = P.Z; Z >= P.Z - 8 && !Run->bCells; --Z)
		{
			for (const FIntVector& D : { FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(-1, 0, 0), FIntVector(0, -1, 0) })
			{
				const FIntVector Cell(P.X + D.X, P.Y + D.Y, Z);
				if (Grid.IsSolid(Cell) && !Grid.IsSolid(Cell + FIntVector(0, 0, 1)) && FVector::Dist(Run->Origin, V->CellToWorld(Cell)) < 450.f)
				{
					Run->Ground = Cell;
					Run->GroundBlock = Grid.Get(Cell);
					Run->bCells = true;
					break;
				}
			}
		}
		Run->Air = P + FIntVector(0, 0, 2);
		TestTrue(TEXT("Found a solid block next to the visitor"), Run->bCells);
		TestTrue(TEXT("Found an empty cell above the visitor"), Grid.IsInBounds(Run->Air) && !Grid.IsSolid(Run->Air));
		const int32 Have = Visitor->Inventory->GetCount(ProtRose);
		if (Have < 1)
		{
			GS->ServerMint(FGuid::NewGuid(), Visitor->PlayerKey, { FGFItemStack(ProtRose, 1 - Have) }, TEXT("Test"));
		}
	});
	WaitUntil(Ctx, TEXT("factions and land on every machine"), 15.f, [Run]()
	{
		TArray<UWorld*> Worlds = ClientWorlds();
		Worlds.Add(ServerWorld());
		for (UWorld* W : Worlds)
		{
			const AGFPlayerState* H = PlayerStateByKey(W, Run->Host);
			const AGFPlayerState* A = PlayerStateByKey(W, Run->C0);
			const AGFPlayerState* B = PlayerStateByKey(W, Run->C1);
			const UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(W);
			if (!H || !A || !B || H->FactionId != ProtPA || A->FactionId != ProtPB || B->FactionId != ProtPA || !Zones || Zones->OwnerAt(Run->Origin) != ProtPA)
			{
				return false;
			}
		}
		return ProtClientSocial(0) && ProtClientSocial(1) && ProtClient(0)->Inventory->GetCount(ProtRose) >= 1;
	});

	// --- Visitors start unprotected, can't build, break or jail; can still paint ---
	Do(Ctx, [this, Run]()
	{
		UGFSocialComponent* S = ProtServerSocial();
		AGFPlayerState* Visitor = ProtOnServer(Run->C0);
		TestFalse(TEXT("Visitor starts unprotected (server)"), S->IsPlayerSafe(Visitor));
		TestFalse(TEXT("...and on the visitor's own client"), ProtSafeOn(ClientWorlds()[0], Run->C0));
		TestTrue(TEXT("Leader is safe at home"), S->IsPlayerSafe(ProtOnServer(Run->C1)));
		TestTrue(TEXT("Host member is safe at home"), S->IsPlayerSafe(ProtOnServer(Run->Host)));
		const FGFSocialPlayer D = S->Describe(Visitor);
		TestEqual(TEXT("Visitor stands on PA land"), D.LandOwner, ProtPA);
		TestTrue(TEXT("...without rights"), D.bVisitorNoRights);
		FString Why;
		TestFalse(TEXT("Visitor can't build on PA land"), ProtTryEdit(Run->C0, Run->Air, Run->GroundBlock, Why));
		TestEqual(TEXT("...because of the visitor rule"), Why, C::VisitorNoRights.ToString());
		TestFalse(TEXT("Visitor can't break on PA land"), ProtTryEdit(Run->C0, Run->Ground, NAME_None, Why));
		TestEqual(TEXT("...same reason"), Why, C::VisitorNoRights.ToString());
		TestEqual(TEXT("Visitor (PB leader) has no jail power on PA land"), S->ServerJail(Visitor, ProtOnServer(Run->C1)), C::VisitorNoRights);
		AGFPlayerController* PC = Cast<AGFPlayerController>(ClientWorlds()[0]->GetFirstPlayerController());
		if (TestNotNull(TEXT("Visitor's painter"), PC ? PC->GetPainter() : nullptr) && Run->bCells)
		{
			PC->GetPainter()->RequestPaint(Run->Ground, ProtRed());
		}
	});
	WaitUntil(Ctx, TEXT("the visitor's paint lands on PA land"), 10.f, [Run]() { return Run->bCells && ProtFinishAt(Run->Ground) != 0; });

	// --- The leader protects the visitor over the net; a member without the grant can't ---
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Host member without the grant can't protect"), ProtServerSocial()->ServerSetProtected(LocalPlayerState(ServerWorld()), ProtOnServer(Run->C0), true), C::NotAllowed);
		Run->Id = ProtAsk(1, TEXT("Protect"), Run->C0);
	});
	WaitUntil(Ctx, TEXT("protection replicates"), 10.f, [Run]()
	{
		const UGFSocialComponent* CS = ProtClientSocial(0);
		return ProtResult(1, Run->Id) && CS && CS->Access.IsProtected(ProtPA, Run->C0) && ProtSafeOn(ClientWorlds()[0], Run->C0);
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader's Protect: ok"), ProtResult(1, Run->Id)->bOk);
		TestTrue(TEXT("Protected visitor is safe (server)"), ProtServerSocial()->IsPlayerSafe(ProtOnServer(Run->C0)));
		FString Why;
		TestFalse(TEXT("Protection alone gives no build rights"), ProtTryEdit(Run->C0, Run->Air, Run->GroundBlock, Why));
		UGFSocialComponent* S = ProtServerSocial();
		S->bBroadcastProtectionRevoked = true;
		Run->Expected = S->FormatProtectionRevoked(ProtOnServer(Run->C1)->GetPlayerName(), ProtOnServer(Run->C0)->GetPlayerName(), ProtPA);
		Run->ChatBefore = GameState(ServerWorld())->ChatLog.Num();
		Run->Id = ProtAsk(1, TEXT("Unprotect"), Run->C0);
	});
	// --- Unprotect: Dragger + timestamp replicate, the config line goes to all chat ---
	WaitUntil(Ctx, TEXT("dragger and chat line replicate"), 10.f, [Run]()
	{
		const AGFPlayerState* Self = ProtClient(0);
		const AGFGameState* GS1 = GameState(ClientWorlds()[1]);
		return ProtResult(1, Run->Id) && Self->LastDrag.IsSet() && GS1 && GS1->ChatLog.Num() > 0 && GS1->ChatLog.Last().Text == Run->Expected;
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader's Unprotect: ok"), ProtResult(1, Run->Id)->bOk);
		const FGFDragRecord& Rec = ProtClient(0)->LastDrag;
		TestEqual(TEXT("Dragger key"), Rec.DraggerKey, Run->C1);
		TestEqual(TEXT("Dragger name"), Rec.DraggerName, ProtOnServer(Run->C1)->GetPlayerName());
		TestEqual(TEXT("Land"), Rec.LandFaction, ProtPA);
		TestTrue(TEXT("Timestamp is recent"), FMath::Abs(FDateTime::UtcNow().GetTicks() - Rec.AtUtcTicks) < ETimespan::TicksPerMinute);
		TestFalse(TEXT("Expected line is filled"), Run->Expected.IsEmpty() || Run->Expected.Contains(TEXT("{")));
		TestEqual(TEXT("One line posted"), GameState(ServerWorld())->ChatLog.Num(), Run->ChatBefore + 1);
		TestFalse(TEXT("Unsafe again"), ProtServerSocial()->IsPlayerSafe(ProtOnServer(Run->C0)));
		ProtServerSocial()->bBroadcastProtectionRevoked = false;
		Run->Id = ProtAsk(1, TEXT("GrantToggle"), Run->Host);
		Run->Id2 = ProtAsk(0, TEXT("GrantToggle"), Run->C1); // C0 leads ProtPB, C1 isn't theirs
	});
	// --- Per-member grant: only the leader grants it, server-checked ---
	WaitUntil(Ctx, TEXT("grant answered"), 10.f, [Run]() { return ProtResult(1, Run->Id) && ProtResult(0, Run->Id2); });
	Do(Ctx, [this, Run]()
	{
		UGFSocialComponent* S = ProtServerSocial();
		AGFPlayerState* HostPS = LocalPlayerState(ServerWorld());
		TestTrue(TEXT("Leader grants the host member"), ProtResult(1, Run->Id)->bOk);
		TestEqual(TEXT("A leader can't grant someone else's member"), ProtResult(0, Run->Id2)->Code, C::NotMember);
		TestEqual(TEXT("A non-leader can't grant"), S->ServerSetToggleGrant(HostPS, ProtOnServer(Run->C1), true), C::NotAllowed);
		TestEqual(TEXT("Granted member protects"), S->ServerSetProtected(HostPS, ProtOnServer(Run->C0), true), C::Ok);
		TestFalse(TEXT("Live drag record cleared"), ProtOnServer(Run->C0)->LastDrag.IsSet());
		TestEqual(TEXT("Members can't be unprotected"), S->ServerSetProtected(ProtOnServer(Run->C1), HostPS, false), C::SameFaction);
		TestTrue(TEXT("Host member still safe"), S->IsPlayerSafe(HostPS));
		Run->Id = ProtAsk(1, TEXT("RevokeToggle"), Run->Host);
	});
	WaitUntil(Ctx, TEXT("revoke answered"), 10.f, [Run]() { return ProtResult(1, Run->Id) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader revokes"), ProtResult(1, Run->Id)->bOk);
		TestEqual(TEXT("Revoked member can't toggle"), ProtServerSocial()->ServerSetProtected(LocalPlayerState(ServerWorld()), ProtOnServer(Run->C0), false), C::NotAllowed);
		TestFalse(TEXT("Grant gone from the replicated book"), ProtServerSocial()->Access.HasToggleGrant(ProtPA, Run->Host));
		// The host is always protected, also on ProtPB land.
		ProtPlace(Run->Host, Run->Origin + FVector(3500.f, 0.f, 0.f));
		Run->Id = ProtAsk(0, TEXT("Unprotect"), Run->Host);
	});
	WaitUntil(Ctx, TEXT("host toggle answered"), 10.f, [Run]() { return ProtResult(0, Run->Id) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("PB's leader can't unprotect the host"), ProtResult(0, Run->Id)->Code, C::TargetIsHost);
		TestEqual(TEXT("Host stands on PB land"), ProtServerSocial()->Describe(LocalPlayerState(ServerWorld())).LandOwner, ProtPB);
		TestTrue(TEXT("Host is protected there"), ProtServerSocial()->IsPlayerSafe(LocalPlayerState(ServerWorld())));
		ProtPlace(Run->Host, Run->Origin + FVector(300.f, 0.f, 0.f));
		Run->Id = ProtAsk(1, TEXT("Guest"), Run->C0);
	});
	// --- Guest: full rights plus protection ---
	WaitUntil(Ctx, TEXT("guest replicates"), 10.f, [Run]()
	{
		const UGFSocialComponent* CS = ProtClientSocial(0);
		return ProtResult(1, Run->Id) && CS && CS->Access.IsGuest(ProtPA, Run->C0);
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader's Guest: ok"), ProtResult(1, Run->Id)->bOk);
		TestTrue(TEXT("Guest is safe"), ProtServerSocial()->IsPlayerSafe(ProtOnServer(Run->C0)));
		TestFalse(TEXT("Guest has rights"), ProtServerSocial()->Describe(ProtOnServer(Run->C0)).bVisitorNoRights);
		FString Why;
		TestTrue(TEXT("Guest builds on PA land"), ProtTryEdit(Run->C0, Run->Air, Run->GroundBlock, Why));
		TestTrue(TEXT("Guest breaks on PA land"), ProtTryEdit(Run->C0, Run->Air, NAME_None, Why));
		Run->Id = ProtAsk(1, TEXT("Unprotect"), Run->C0);
	});
	WaitUntil(Ctx, TEXT("guest unprotect answered"), 10.f, [Run]() { return ProtResult(1, Run->Id) != nullptr; });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("A guest can't be unprotected"), ProtResult(1, Run->Id)->Code, C::TargetIsGuest);
		Run->Id = ProtAsk(1, TEXT("Unguest"), Run->C0);
	});
	WaitUntil(Ctx, TEXT("unguest replicates"), 10.f, [Run]()
	{
		const UGFSocialComponent* CS = ProtClientSocial(0);
		return ProtResult(1, Run->Id) && CS && !CS->Access.IsGuest(ProtPA, Run->C0);
	});
	Do(Ctx, [this, Run]()
	{
		TestTrue(TEXT("Leader's Unguest: ok"), ProtResult(1, Run->Id)->bOk);
		TestTrue(TEXT("Still protected (the member's toggle stays)"), ProtServerSocial()->IsPlayerSafe(ProtOnServer(Run->C0)));
		FString Why;
		TestFalse(TEXT("No rights after unguest"), ProtTryEdit(Run->C0, Run->Air, Run->GroundBlock, Why));
		TestEqual(TEXT("...visitor rule"), Why, C::VisitorNoRights.ToString());
	});

	// --- Powers and holds: an unprotected visitor can be grabbed and grown; protection changes don't drop either ---
	Do(Ctx, [this, Run]()
	{
		UGFSocialComponent* S = ProtServerSocial();
		AGFPlayerState* HostPS = LocalPlayerState(ServerWorld());
		AGFPlayerState* Visitor = ProtOnServer(Run->C0);
		AGFPlayerState* Leader = ProtOnServer(Run->C1);
		TestEqual(TEXT("Protected visitor is immune to a grab"), S->ServerPower(HostPS, Visitor, EGFSocialPower::Grab), C::TargetProtected);
		TestEqual(TEXT("Visitor can't grab a member at home"), S->ServerPower(Visitor, Leader, EGFSocialPower::Grab), C::TargetProtected);
		TestEqual(TEXT("Leader unprotects the visitor"), S->ServerSetProtected(Leader, Visitor, false), C::Ok);
		TestEqual(TEXT("Host member grabs the unprotected visitor"), S->ServerPower(HostPS, Visitor, EGFSocialPower::Grab), C::Ok);
		TestEqual(TEXT("Leader grows the unprotected visitor"), S->ServerPower(Leader, Visitor, EGFSocialPower::Grow), C::Ok);
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [this, Run]()
	{
		UGFSocialComponent* S = ProtServerSocial();
		TestEqual(TEXT("Grab holds"), S->Tethers.Num(), 1);
		TestEqual(TEXT("Leader protects the held visitor"), S->ServerSetProtected(ProtOnServer(Run->C1), ProtOnServer(Run->C0), true), C::Ok);
	});
	Wait(Ctx, 1.f);
	Do(Ctx, [this, Run]()
	{
		UGFSocialComponent* S = ProtServerSocial();
		TestEqual(TEXT("Protecting doesn't drop the grab"), S->Tethers.Num(), 1);
		TestTrue(TEXT("...or undo the grow"), FMath::IsNearlyEqual(S->ScaleOf(Run->C0), S->Tuning.ScaleStep));
		TestEqual(TEXT("Leader unprotects again"), S->ServerSetProtected(ProtOnServer(Run->C1), ProtOnServer(Run->C0), false), C::Ok);
	});
	Wait(Ctx, 0.5f);
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Unprotecting doesn't drop the grab either"), ProtServerSocial()->Tethers.Num(), 1);
		ProtPlace(Run->Host, Run->Origin + FVector(-4500.f, 0.f, 0.f)); // the caster walks into the wild
	});
	WaitUntil(Ctx, TEXT("leaving safe land drops the grab"), 10.f, [Run]() { return ProtServerSocial()->Tethers.Num() == 0; });
	Do(Ctx, [Run]() { ProtPlace(Run->C0, Run->Origin + FVector(-4500.f, 600.f, 0.f)); });
	WaitUntil(Ctx, TEXT("leaving safe land undoes the grow"), 10.f, [Run]() { return ProtServerSocial()->ScaleOf(Run->C0) == 1.f; });
	Stop(Ctx);
	return true;
}

#endif
