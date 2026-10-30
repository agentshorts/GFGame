// Landlord loop over the network (PLAN.md section 2a, M1 accept tests): plot permissions, rent in materials
// (cosmetic consequences only), mailbox, and blueprint backups / pack-up / restore. Listen server + clients.
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
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace GFNetTest;

namespace
{
	constexpr EAutomationTestFlags GFLandNetFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	template <typename E>
	bool CheckEnum(FAutomationTestBase* Test, const FString& What, E Actual, E Expected)
	{
		return Test->TestEqual(*What, UEnum::GetValueAsString(Actual), UEnum::GetValueAsString(Expected));
	}

	AGFPlayerState* Host() { return LocalPlayerState(ServerWorld()); }
	AGFPlayerState* Guest(int32 i) { const TArray<UWorld*> C = ClientWorlds(); return C.IsValidIndex(i) ? LocalPlayerState(C[i]) : nullptr; }
	/** The server's player state for client i. */
	AGFPlayerState* GuestOnServer(int32 i) { AGFPlayerState* G = Guest(i); return G ? PlayerStateByKey(ServerWorld(), G->PlayerKey) : nullptr; }
	AGFGameState* SGS() { return GameState(ServerWorld()); }
	FGFLandPlot* SPlot(FName Id) { return SGS() ? SGS()->Land.Find(Id) : nullptr; }
	const FName PlotA = FGFLandRules::PlotAId();
	const FName PlotB = FGFLandRules::PlotBId();
	const FName HomePlot = FGFLandRules::HomePlotId();

	FGFLandAction Act(EGFLandOp Op, FName Plot = NAME_None)
	{
		return FGFLandAction::Make(Op, Plot);
	}
	FGFLandAction Rent(FName Plot, const TCHAR* Item, int32 Amount, bool bForRent = true)
	{
		FGFLandAction A = Act(EGFLandOp::SetPlotRent, Plot);
		A.ItemId = Item;
		A.Amount = Amount;
		A.bFlag = bForRent;
		return A;
	}
	FGFLandAction ToPlayer(EGFLandOp Op, const FString& Other, bool bFlag = false)
	{
		FGFLandAction A = Act(Op);
		A.OtherKey = Other;
		A.bFlag = bFlag;
		return A;
	}

	/** Latent: Who() sends Action, waits for the server's answer, and checks it. */
	void Land(const FCtxRef& Ctx, FAutomationTestBase* Test, TFunction<AGFPlayerState*()> Who, TFunction<FGFLandAction()> Action, EGFLandResult Expected, const FString& What)
	{
		TSharedRef<int32> Id = MakeShared<int32>(0);
		TSharedRef<TWeakObjectPtr<AGFPlayerState>> PS = MakeShared<TWeakObjectPtr<AGFPlayerState>>();
		Do(Ctx, [Who, Action, Id, PS]()
		{
			*PS = Who();
			*Id = PS->IsValid() ? (*PS)->RequestLandAction(Action()) : 0;
		});
		WaitUntil(Ctx, What + TEXT(" (answer)"), 15.f, [Id, PS]() { return PS->IsValid() && (*PS)->LandResults.Contains(*Id); });
		Do(Ctx, [Test, Id, PS, Expected, What]()
		{
			CheckEnum(Test, What, (*PS)->LandResults.FindRef(*Id), Expected);
		});
	}
	void LandA(const FCtxRef& Ctx, FAutomationTestBase* Test, TFunction<AGFPlayerState*()> Who, const FGFLandAction& Action, EGFLandResult Expected, const FString& What)
	{
		Land(Ctx, Test, MoveTemp(Who), [Action]() { return Action; }, Expected, What);
	}

	FName BlockAt(const FIntVector& Cell)
	{
		const AGFVoxelWorld* V = Voxel(ServerWorld());
		return V ? V->GetBlock(Cell) : NAME_None;
	}
	int64 PlotHashNow(FName Id)
	{
		const AGFVoxelWorld* V = Voxel(ServerWorld());
		const FGFLandPlot* P = SPlot(Id);
		return V && P ? FGFLandRules::PlotHash(V->GetGrid(), *P) : -1;
	}
	int32 PlotBlocksNow(FName Id)
	{
		const AGFVoxelWorld* V = Voxel(ServerWorld());
		const FGFLandPlot* P = SPlot(Id);
		return V && P ? V->GetGrid().CountSolidInRegion(P->Min, P->Max()) : -1;
	}
	const UGFLocalProfileSubsystem* ProfileOfGuest(int32 i)
	{
		const TArray<UWorld*> C = ClientWorlds();
		UGameInstance* GI = C.IsValidIndex(i) ? C[i]->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UGFLocalProfileSubsystem>() : nullptr;
	}
	int32 UnclaimedFor(const FString& Key, EGFLetterKind Kind)
	{
		int32 N = 0;
		for (const FGFMailLetter& L : SGS()->Mail)
		{
			N += (L.ToKey == Key && !L.bClaimed && L.Kind == Kind) ? 1 : 0;
		}
		return N;
	}
	const FGFMailLetter* NewestLetter(const FString& Key, EGFLetterKind Kind)
	{
		const TArray<FGFMailLetter>& Mail = SGS()->Mail;
		for (int32 i = Mail.Num() - 1; i >= 0; --i)
		{
			if (Mail[i].ToKey == Key && Mail[i].Kind == Kind)
			{
				return &Mail[i];
			}
		}
		return nullptr;
	}
	void SetDay(int32 Offset)
	{
		SGS()->DebugDayOffset = Offset;
		SGS()->ServerUpdateRent();
	}
}

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetPermissionsTest, "GFGame.Net.Permissions", GFLandNetFlags)
bool FGFNetPermissionsTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<TArray<FString>> Keys = MakeShared<TArray<FString>>();
	Do(Ctx, [Keys]()
	{
		SGS()->MaxEditReach = 0.f;
		Keys->Add(Host()->PlayerKey);
		Keys->Add(Guest(0)->PlayerKey);
		Keys->Add(Guest(1)->PlayerKey);
	});
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Host key is the landlord"), SGS()->Land.LandlordKey, Host()->PlayerKey);
		TestTrue(TEXT("Plot permissions are on by default"), SGS()->bEnforcePlotPermissions);
	});
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ClaimPlot, PlotA), EGFLandResult::Ok, TEXT("Guest 1 claims PlotA"));
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ClaimPlot, PlotB), EGFLandResult::AlreadyHasPlot, TEXT("One plot per guest"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimPlot, PlotA), EGFLandResult::PlotTaken, TEXT("PlotA is taken"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimPlot, HomePlot), EGFLandResult::NotAllowed, TEXT("Host's home can't be claimed"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimPlot, TEXT("Nowhere")), EGFLandResult::NoSuchPlot, TEXT("Unknown plot"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimPlot, PlotB), EGFLandResult::Ok, TEXT("Guest 2 claims PlotB"));
	LandA(Ctx, this, []() { return Guest(1); }, Rent(PlotB, TEXT("Wood"), 0), EGFLandResult::NotAllowed, TEXT("Guests can't set rent"));
	WaitUntil(Ctx, TEXT("claims replicate to every client"), 10.f, [Keys]()
	{
		for (UWorld* W : AllWorlds())
		{
			const AGFGameState* GS = GameState(W);
			const FGFLandPlot* A = GS ? GS->Land.Find(PlotA) : nullptr;
			const FGFLandPlot* B = GS ? GS->Land.Find(PlotB) : nullptr;
			if (!A || !B || A->TenantKey != (*Keys)[1] || B->TenantKey != (*Keys)[2])
			{
				return false;
			}
		}
		return true;
	});
	Do(Ctx, []()
	{
		Guest(0)->RequestEdit(FIntVector(40, 10, 1), TEXT("Stone"));     // own plot: ok
		Guest(0)->RequestEdit(FIntVector(12, 40, 1), TEXT("Stone"));     // PlotB: no
		Guest(0)->RequestEdit(FIntVector(10, 10, 1), TEXT("Stone"));     // the host's home: no
		Guest(0)->RequestEdit(FIntVector(50, 50, 1), TEXT("Stone"));     // commons: no
		Guest(1)->RequestEdit(FIntVector(10, 40, 1), TEXT("Stone"));     // own plot: ok
		Host()->RequestEdit(FIntVector(41, 10, 1), TEXT("WoodPlank"));   // the host can help anywhere
		Host()->RequestEdit(FIntVector(50, 51, 1), TEXT("WoodPlank"));
		Host()->RequestEdit(FIntVector(5, 5, 1), TEXT("WoodPlank"));
	});
	WaitUntil(Ctx, TEXT("first edits settle"), 15.f, []() { return VoxelsConverged() && BlockAt(FIntVector(40, 10, 1)) == FName(TEXT("Stone")) && BlockAt(FIntVector(10, 40, 1)) == FName(TEXT("Stone")); });
	Do(Ctx, []() { Guest(1)->RequestEdit(FIntVector(40, 10, 1), NAME_None); }); // can't remove someone else's block
	WaitUntil(Ctx, TEXT("removal attempt settles"), 15.f, []() { return VoxelsConverged() && GuestOnServer(1)->GetEditsRejected() >= 1; });
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Guest 1 built on PlotA"), BlockAt(FIntVector(40, 10, 1)), FName(TEXT("Stone")));
		TestEqual(TEXT("Guest 1 couldn't build on PlotB"), BlockAt(FIntVector(12, 40, 1)), FName());
		TestEqual(TEXT("Guest 1 couldn't build in the host's home"), BlockAt(FIntVector(10, 10, 1)), FName());
		TestEqual(TEXT("Guest 1 couldn't build in the commons"), BlockAt(FIntVector(50, 50, 1)), FName());
		TestEqual(TEXT("Guest 2 built on PlotB"), BlockAt(FIntVector(10, 40, 1)), FName(TEXT("Stone")));
		TestEqual(TEXT("Host built on PlotA"), BlockAt(FIntVector(41, 10, 1)), FName(TEXT("WoodPlank")));
		TestEqual(TEXT("Host built in the commons"), BlockAt(FIntVector(50, 51, 1)), FName(TEXT("WoodPlank")));
		TestEqual(TEXT("Host built at home"), BlockAt(FIntVector(5, 5, 1)), FName(TEXT("WoodPlank")));
		TestEqual(TEXT("Guest 1: 1 accepted"), GuestOnServer(0)->GetEditsAccepted(), 1);
		TestEqual(TEXT("Guest 1: 3 rejected"), GuestOnServer(0)->GetEditsRejected(), 3);
		TestEqual(TEXT("Guest 1 only paid for the accepted block"), GuestOnServer(0)->Inventory->GetCount(TEXT("Stone")), 63);
	});
	// Builder rights on the host's common areas.
	Land(Ctx, this, []() { return Guest(0); }, [Keys]() { return ToPlayer(EGFLandOp::GrantBuilder, (*Keys)[2], true); }, EGFLandResult::NotAllowed, TEXT("Guests can't grant builder rights"));
	Land(Ctx, this, []() { return Host(); }, [Keys]() { return ToPlayer(EGFLandOp::GrantBuilder, (*Keys)[2], true); }, EGFLandResult::Ok, TEXT("Host makes guest 2 a builder"));
	Do(Ctx, []() { Guest(1)->RequestEdit(FIntVector(52, 52, 1), TEXT("Stone")); Guest(1)->RequestEdit(FIntVector(5, 6, 1), TEXT("Stone")); });
	WaitUntil(Ctx, TEXT("builder edits settle"), 15.f, []() { return VoxelsConverged() && GuestOnServer(1)->GetEditsRejected() >= 2; });
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Builder builds in the commons"), BlockAt(FIntVector(52, 52, 1)), FName(TEXT("Stone")));
		TestEqual(TEXT("Builder still can't build in the host's home"), BlockAt(FIntVector(5, 6, 1)), FName());
	});
	Land(Ctx, this, []() { return Host(); }, [Keys]() { return ToPlayer(EGFLandOp::GrantBuilder, (*Keys)[2], false); }, EGFLandResult::Ok, TEXT("Host revokes builder"));
	Do(Ctx, []() { Guest(1)->RequestEdit(FIntVector(52, 52, 1), NAME_None); });
	WaitUntil(Ctx, TEXT("revoked edit settles"), 15.f, []() { return VoxelsConverged() && GuestOnServer(1)->GetEditsRejected() >= 3; });
	Do(Ctx, [this]() { TestEqual(TEXT("Revoked builder can't edit the commons"), BlockAt(FIntVector(52, 52, 1)), FName(TEXT("Stone"))); });
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
namespace
{
	struct FRentRun
	{
		FString HostKey;
		FString GuestKey;
		int64 WoodStart = 0;
		int64 WoodMinted = 0;
		int64 PlotHash = 0;
		int32 PlotBlocks = 0;
		FGuid FirstLetter;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetRentTest, "GFGame.Net.Rent", GFLandNetFlags)
bool FGFNetRentTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 2);
	TSharedRef<FRentRun> Run = MakeShared<FRentRun>();
	Do(Ctx, [Run]()
	{
		SGS()->MaxEditReach = 0.f;
		Run->HostKey = Host()->PlayerKey;
		Run->GuestKey = Guest(0)->PlayerKey;
	});
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ClaimPlot, PlotA), EGFLandResult::Ok, TEXT("Guest claims PlotA"));
	LandA(Ctx, this, []() { return Host(); }, Rent(PlotA, TEXT("Wood"), 31), EGFLandResult::RentTooHigh, TEXT("Rent over 30 value is refused"));
	LandA(Ctx, this, []() { return Host(); }, Rent(PlotA, TEXT("Moonstone"), 1), EGFLandResult::NotRentable, TEXT("Moonstone can't be rent"));
	LandA(Ctx, this, []() { return Host(); }, Rent(PlotA, TEXT("Wood"), 5), EGFLandResult::Ok, TEXT("Rent: 5 wood a day"));
	Do(Ctx, [this, Run]()
	{
		GuestOnServer(0)->Inventory->AddItem(TEXT("Wood"), 12);
		Run->WoodMinted += 12;
		Run->WoodStart = SGS()->CountEverywhere(TEXT("Wood")) - Run->WoodMinted;
		Guest(0)->RequestEdit(FIntVector(40, 10, 1), TEXT("Stone"));
		Guest(0)->RequestEdit(FIntVector(41, 10, 1), TEXT("Stone"));
		Guest(0)->RequestEdit(FIntVector(42, 10, 1), TEXT("Stone"));
		CheckEnum(this, TEXT("New tenant: Paid"), SPlot(PlotA)->Status, EGFRentStatus::Paid);
	});
	WaitUntil(Ctx, TEXT("guest's house is up"), 15.f, []() { return VoxelsConverged() && BlockAt(FIntVector(42, 10, 1)) == FName(TEXT("Stone")); });

	// Day 1: auto-pay (on by default) pays, and a rent letter lands in the host's mailbox.
	Do(Ctx, [this, Run]()
	{
		SetDay(1);
		const FGFLandPlot* P = SPlot(PlotA);
		TestEqual(TEXT("Auto-paid: nothing owed"), P->OwedPeriods, 0);
		CheckEnum(this, TEXT("Auto-paid: Paid"), P->Status, EGFRentStatus::Paid);
		TestEqual(TEXT("Guest paid 5 wood"), GuestOnServer(0)->Inventory->GetCount(TEXT("Wood")), 7);
		TestEqual(TEXT("One rent letter for the host"), UnclaimedFor(Run->HostKey, EGFLetterKind::Rent), 1);
		const FGFMailLetter* L = NewestLetter(Run->HostKey, EGFLetterKind::Rent);
		TestTrue(TEXT("Letter holds the 5 wood"), L && L->Stacks.Num() == 1 && L->Stacks[0].ItemId == FName(TEXT("Wood")) && L->Stacks[0].Count == 5);
		Run->FirstLetter = L ? L->Id : FGuid();
		TestEqual(TEXT("Wood conserved (in the letter)"), SGS()->CountEverywhere(TEXT("Wood")), Run->WoodStart + Run->WoodMinted);
		TestEqual(TEXT("Host's mailbox shows it"), Host()->Mailbox.Num(), 1);
	});
	WaitUntil(Ctx, TEXT("guest sees the 'Rent paid' toast and Paid status"), 10.f, []()
	{
		const AGFPlayerState* G = Guest(0);
		const AGFGameState* CGS = GameState(ClientWorlds()[0]);
		return G && CGS && G->LandNotices.ContainsByPredicate([](const FString& N) { return N.StartsWith(TEXT("Rent paid")); })
			&& CGS->Land.Find(PlotA)->Status == EGFRentStatus::Paid && CGS->Land.Find(PlotA)->LastPaidAmount == 5;
	});
	Land(Ctx, this, []() { return Guest(0); }, [Run]() { FGFLandAction A = Act(EGFLandOp::ClaimLetter); A.LetterId = Run->FirstLetter; return A; }, EGFLandResult::NoSuchLetter, TEXT("Guest can't claim the host's letter"));
	Land(Ctx, this, []() { return Host(); }, [Run]() { FGFLandAction A = Act(EGFLandOp::ClaimLetter); A.LetterId = Run->FirstLetter; return A; }, EGFLandResult::Ok, TEXT("Host claims the rent"));
	Land(Ctx, this, []() { return Host(); }, [Run]() { FGFLandAction A = Act(EGFLandOp::ClaimLetter); A.LetterId = Run->FirstLetter; return A; }, EGFLandResult::AlreadyClaimed, TEXT("Claiming twice does nothing"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Host has the 5 wood"), Host()->Inventory->GetCount(TEXT("Wood")), 5);
		TestEqual(TEXT("Wood conserved after claim"), SGS()->CountEverywhere(TEXT("Wood")), Run->WoodStart + Run->WoodMinted);
	});

	// Auto-pay off: missed days make the plot look worse, step by step.
	Land(Ctx, this, []() { return Guest(0); }, [] { FGFLandAction A = Act(EGFLandOp::SetAutoPay); A.bFlag = false; return A; }, EGFLandResult::Ok, TEXT("Guest turns auto-pay off"));
	Do(Ctx, [this]()
	{
		SetDay(2);
		CheckEnum(this, TEXT("1 missed: Due"), SPlot(PlotA)->Status, EGFRentStatus::Due);
		TestEqual(TEXT("No auto-pay"), GuestOnServer(0)->Inventory->GetCount(TEXT("Wood")), 7);
		Guest(0)->RequestEdit(FIntVector(43, 10, 1), TEXT("Stone")); // grace day: still fine
	});
	WaitUntil(Ctx, TEXT("Due: placing still works"), 15.f, []() { return VoxelsConverged() && BlockAt(FIntVector(43, 10, 1)) == FName(TEXT("Stone")); });
	Do(Ctx, [this]()
	{
		SetDay(3);
		CheckEnum(this, TEXT("2 missed: Overgrown"), SPlot(PlotA)->Status, EGFRentStatus::Overgrown);
		Guest(0)->RequestEdit(FIntVector(44, 10, 1), TEXT("Stone")); // no new blocks while overgrown
		Guest(0)->RequestEdit(FIntVector(43, 10, 1), NAME_None);     // removing is fine
	});
	WaitUntil(Ctx, TEXT("Overgrown edits settle"), 15.f, []() { return VoxelsConverged() && BlockAt(FIntVector(43, 10, 1)).IsNone(); });
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Overgrown: placing refused"), BlockAt(FIntVector(44, 10, 1)), FName());
		Run->PlotHash = PlotHashNow(PlotA);
		Run->PlotBlocks = PlotBlocksNow(PlotA);
		SetDay(9);
		const FGFLandPlot* P = SPlot(PlotA);
		TestEqual(TEXT("Owed is capped at 3"), P->OwedPeriods, 3);
		CheckEnum(this, TEXT("3 missed: Faded"), P->Status, EGFRentStatus::Faded);
		TestEqual(TEXT("Faded: no block changed"), PlotHashNow(PlotA), Run->PlotHash);
		TestEqual(TEXT("Faded: block count unchanged"), PlotBlocksNow(PlotA), Run->PlotBlocks);
		TestEqual(TEXT("Tenant keeps the plot"), P->TenantKey, Run->GuestKey);
	});
	WaitUntil(Ctx, TEXT("client sees Faded"), 10.f, []() { return GameState(ClientWorlds()[0])->Land.Find(PlotA)->Status == EGFRentStatus::Faded; });

	// Paying restores at once (as much as they can afford).
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::PayRent, PlotA), EGFLandResult::Ok, TEXT("Pays what they can (1 day)"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("2 still owed"), SPlot(PlotA)->OwedPeriods, 2);
		TestEqual(TEXT("Wood left: 2"), GuestOnServer(0)->Inventory->GetCount(TEXT("Wood")), 2);
		GuestOnServer(0)->Inventory->AddItem(TEXT("Wood"), 20);
		Run->WoodMinted += 20;
	});
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::PayRent, PlotA), EGFLandResult::Ok, TEXT("Pays the rest"));
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::PayRent, PlotA), EGFLandResult::NothingOwed, TEXT("Nothing more to pay"));
	LandA(Ctx, this, []() { return Host(); }, Act(EGFLandOp::PayRent, PlotA), EGFLandResult::NotAllowed, TEXT("Only the tenant pays"));
	Do(Ctx, [this, Run]()
	{
		CheckEnum(this, TEXT("Paid again"), SPlot(PlotA)->Status, EGFRentStatus::Paid);
		TestEqual(TEXT("Wood left: 12"), GuestOnServer(0)->Inventory->GetCount(TEXT("Wood")), 12);
		TestEqual(TEXT("Paid: no block changed"), PlotHashNow(PlotA), Run->PlotHash);
		TestEqual(TEXT("Wood conserved through rent"), SGS()->CountEverywhere(TEXT("Wood")), Run->WoodStart + Run->WoodMinted);
		TestEqual(TEXT("2 more rent letters waiting"), UnclaimedFor(Run->HostKey, EGFLetterKind::Rent), 2);
		Guest(0)->RequestEdit(FIntVector(44, 10, 1), TEXT("Stone"));
	});
	WaitUntil(Ctx, TEXT("paying lets them build again"), 15.f, []() { return VoxelsConverged() && BlockAt(FIntVector(44, 10, 1)) == FName(TEXT("Stone")); });

	// Forgive and pause.
	Do(Ctx, [this]() { SetDay(11); TestEqual(TEXT("2 owed"), SPlot(PlotA)->OwedPeriods, 2); });
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ForgiveRent, PlotA), EGFLandResult::NotAllowed, TEXT("Guests can't forgive themselves"));
	LandA(Ctx, this, []() { return Host(); }, Act(EGFLandOp::ForgiveRent, PlotA), EGFLandResult::Ok, TEXT("Host forgives the rent"));
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Forgiven: nothing owed"), SPlot(PlotA)->OwedPeriods, 0);
		CheckEnum(this, TEXT("Forgiven: Paid"), SPlot(PlotA)->Status, EGFRentStatus::Paid);
	});
	Land(Ctx, this, []() { return Host(); }, [] { FGFLandAction A = Act(EGFLandOp::PauseRent, PlotA); A.bFlag = true; return A; }, EGFLandResult::Ok, TEXT("Host pauses rent"));
	Do(Ctx, [this]() { SetDay(15); TestEqual(TEXT("Paused: nothing accrues"), SPlot(PlotA)->OwedPeriods, 0); });
	Land(Ctx, this, []() { return Host(); }, [] { FGFLandAction A = Act(EGFLandOp::PauseRent, PlotA); A.bFlag = false; return A; }, EGFLandResult::Ok, TEXT("Host resumes rent"));
	Do(Ctx, [this]() { SetDay(16); TestEqual(TEXT("Resumed: one day owed"), SPlot(PlotA)->OwedPeriods, 1); });

	// Login: everything missed while away is collected, capped at 3 periods.
	Land(Ctx, this, []() { return Guest(0); }, [] { FGFLandAction A = Act(EGFLandOp::SetAutoPay); A.bFlag = true; return A; }, EGFLandResult::Ok, TEXT("Auto-pay back on"));
	Do(Ctx, [this, Run]()
	{
		GuestOnServer(0)->Inventory->AddItem(TEXT("Wood"), 100);
		Run->WoodMinted += 100;
		const int32 Before = GuestOnServer(0)->Inventory->GetCount(TEXT("Wood"));
		SGS()->DebugDayOffset = 40;
		SGS()->ServerOnPlayerReady(GuestOnServer(0));
		TestEqual(TEXT("Login collected everything"), SPlot(PlotA)->OwedPeriods, 0);
		TestEqual(TEXT("Login charged at most 3 days"), Before - GuestOnServer(0)->Inventory->GetCount(TEXT("Wood")), 15);
		const FGFMailLetter* L = NewestLetter(Run->HostKey, EGFLetterKind::Rent);
		TestTrue(TEXT("One letter for the 3 days"), L && L->Stacks.Num() == 1 && L->Stacks[0].Count == 15);
		TestEqual(TEXT("Wood conserved through login rent"), SGS()->CountEverywhere(TEXT("Wood")), Run->WoodStart + Run->WoodMinted);
	});

	// Save round-trip: land, rent state and unclaimed letters (with their items) come back.
	Do(Ctx, [this, Run]()
	{
		AGFGameState* GS = SGS();
		const int32 Letters = GS->Mail.Num();
		const int32 Unclaimed = UnclaimedFor(Run->HostKey, EGFLetterKind::Rent);
		const int64 Wood = GS->CountEverywhere(TEXT("Wood"));
		UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(ServerWorld());
		TArray<uint8> Bytes;
		TestTrue(TEXT("World serialises"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
		UGFSaveGame* Back = Cast<UGFSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
		if (!TestNotNull(TEXT("Save loads"), Back))
		{
			return;
		}
		TestTrue(TEXT("Save version is current"), Back->SaveVersion >= static_cast<int32>(EGFSaveVersion::LandAndMail) && Back->SaveVersion == static_cast<int32>(EGFSaveVersion::Latest));
		GS->Land = FGFLandState();
		GS->Mail.Reset();
		TestTrue(TEXT("Apply"), UGFSaveSubsystem::ApplyToWorld(ServerWorld(), Back));
		const FGFLandPlot* P = SPlot(PlotA);
		TestTrue(TEXT("Tenant and rent restored"), P && P->TenantKey == Run->GuestKey && P->RentItem == FName(TEXT("Wood")) && P->RentAmount == 5 && P->bAutoPay);
		TestEqual(TEXT("Landlord restored"), GS->Land.LandlordKey, Run->HostKey);
		TestEqual(TEXT("Letters restored"), GS->Mail.Num(), Letters);
		TestEqual(TEXT("Unclaimed letters restored"), UnclaimedFor(Run->HostKey, EGFLetterKind::Rent), Unclaimed);
		TestEqual(TEXT("Wood conserved through save/load"), GS->CountEverywhere(TEXT("Wood")), Wood);
	});
	LandA(Ctx, this, []() { return Host(); }, Act(EGFLandOp::ClaimAllLetters), EGFLandResult::Ok, TEXT("Host claims all rent after load"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("All rent claimed"), UnclaimedFor(Run->HostKey, EGFLetterKind::Rent), 0);
		TestTrue(TEXT("Host got at least every payment we made"), Host()->Inventory->GetCount(TEXT("Wood")) >= 5 + 5 + 10 + 15);
		TestEqual(TEXT("Every wood is the host's or the guest's"), static_cast<int64>(Host()->Inventory->GetCount(TEXT("Wood")) + GuestOnServer(0)->Inventory->GetCount(TEXT("Wood"))), Run->WoodStart + Run->WoodMinted);
		TestEqual(TEXT("Nothing left in escrow"), SGS()->Ledger.HeldInEscrow(TEXT("Wood")), static_cast<int64>(0));
		TestEqual(TEXT("Wood conserved overall"), SGS()->CountEverywhere(TEXT("Wood")), Run->WoodStart + Run->WoodMinted);
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
namespace
{
	struct FMailRun
	{
		TArray<FString> Keys;
		int64 Stone = 0;
		int64 Dirt = 0;
		FGuid Gift;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetMailboxTest, "GFGame.Net.Mailbox", GFLandNetFlags)
bool FGFNetMailboxTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FMailRun> Run = MakeShared<FMailRun>();
	Do(Ctx, [Run]()
	{
		Run->Keys = { Host()->PlayerKey, Guest(0)->PlayerKey, Guest(1)->PlayerKey };
		Run->Stone = SGS()->CountEverywhere(TEXT("Stone"));
		Run->Dirt = SGS()->CountEverywhere(TEXT("Dirt"));
	});
	auto Gift = [Run](int32 To, const TCHAR* Item, int32 Count, const TCHAR* Text)
	{
		return [Run, To, Item, Count, Text]()
		{
			FGFLandAction A = ToPlayer(EGFLandOp::SendGift, To < 0 ? FString(TEXT("nobody")) : Run->Keys[To]);
			A.Stacks = { FGFItemStack(Item, Count) };
			A.Text = Text;
			return A;
		};
	};
	Land(Ctx, this, []() { return Guest(0); }, Gift(2, TEXT("Stone"), 10, TEXT("for your roof")), EGFLandResult::Ok, TEXT("Guest 1 gifts guest 2 ten stone"));
	Land(Ctx, this, []() { return Guest(0); }, Gift(1, TEXT("Stone"), 1, TEXT("me")), EGFLandResult::NotAllowed, TEXT("No gifts to yourself"));
	Land(Ctx, this, []() { return Guest(0); }, Gift(2, TEXT("Stone"), 1000, TEXT("too much")), EGFLandResult::CantAfford, TEXT("Can't gift what you don't have"));
	Land(Ctx, this, []() { return Guest(0); }, Gift(-1, TEXT("Stone"), 1, TEXT("?")), EGFLandResult::NoSuchPlayer, TEXT("Unknown recipient"));
	Land(Ctx, this, []() { return Guest(0); }, Gift(2, TEXT("Stone"), 0, TEXT("zero")), EGFLandResult::InvalidStacks, TEXT("Zero items"));
	Land(Ctx, this, []() { return Host(); }, [Run]() { FGFLandAction A = ToPlayer(EGFLandOp::SendNote, Run->Keys[2]); A.Text = TEXT("welcome to the island!"); return A; }, EGFLandResult::Ok, TEXT("Host sends a note"));
	Land(Ctx, this, []() { return Host(); }, Gift(2, TEXT("Dirt"), 5, TEXT("for the garden")), EGFLandResult::Ok, TEXT("Host sends dirt"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Guest 1 gave 10 stone"), GuestOnServer(0)->Inventory->GetCount(TEXT("Stone")), 54);
		TestEqual(TEXT("Stone conserved (10 in the letter)"), SGS()->CountEverywhere(TEXT("Stone")), Run->Stone);
		TestEqual(TEXT("Escrow holds 10 stone"), SGS()->Ledger.HeldInEscrow(TEXT("Stone")), static_cast<int64>(10));
		const FGFMailLetter* L = NewestLetter(Run->Keys[2], EGFLetterKind::Gift);
		const FGFMailLetter* First = nullptr;
		for (const FGFMailLetter& M : SGS()->Mail)
		{
			if (M.ToKey == Run->Keys[2] && M.Kind == EGFLetterKind::Gift && !First)
			{
				First = &M;
			}
		}
		Run->Gift = First ? First->Id : FGuid();
		TestTrue(TEXT("Gift letter from guest 1"), First && First->FromKey == Run->Keys[1] && First->Text == TEXT("for your roof"));
		TestTrue(TEXT("Newest gift is the dirt"), L && L->Stacks.Num() == 1 && L->Stacks[0].ItemId == FName(TEXT("Dirt")));
	});
	WaitUntil(Ctx, TEXT("guest 2's mailbox replicates (3 letters)"), 10.f, []()
	{
		const AGFPlayerState* G = Guest(1);
		return G && G->Mailbox.Num() == 3 && G->LandNotices.Num() >= 3;
	});
	Do(Ctx, [this]()
	{
		TestEqual(TEXT("Guest 1 doesn't see guest 2's mail"), Guest(0)->Mailbox.Num(), 0);
		const AGFPlayerState* G2OnG1 = PlayerStateByKey(ClientWorlds()[0], Guest(1)->PlayerKey);
		TestTrue(TEXT("Mailbox is owner-only"), G2OnG1 && G2OnG1->Mailbox.Num() == 0);
	});
	Land(Ctx, this, []() { return Guest(0); }, [Run]() { FGFLandAction A = Act(EGFLandOp::ClaimLetter); A.LetterId = Run->Gift; return A; }, EGFLandResult::NoSuchLetter, TEXT("Guest 1 can't claim guest 2's letter"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimAllLetters), EGFLandResult::Ok, TEXT("Guest 2 claims all"));
	Land(Ctx, this, []() { return Guest(1); }, [Run]() { FGFLandAction A = Act(EGFLandOp::ClaimLetter); A.LetterId = Run->Gift; return A; }, EGFLandResult::AlreadyClaimed, TEXT("No double claim"));
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ClaimAllLetters), EGFLandResult::NoSuchLetter, TEXT("Nothing left to claim"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Guest 2 got the stone"), GuestOnServer(1)->Inventory->GetCount(TEXT("Stone")), 74);
		TestEqual(TEXT("Guest 2 got the dirt"), GuestOnServer(1)->Inventory->GetCount(TEXT("Dirt")), 69);
		TestEqual(TEXT("Stone conserved"), SGS()->CountEverywhere(TEXT("Stone")), Run->Stone);
		TestEqual(TEXT("Dirt conserved"), SGS()->CountEverywhere(TEXT("Dirt")), Run->Dirt);
		TestEqual(TEXT("Escrow empty"), SGS()->Ledger.HeldInEscrow(TEXT("Stone")) + SGS()->Ledger.HeldInEscrow(TEXT("Dirt")), static_cast<int64>(0));
	});
	WaitUntil(Ctx, TEXT("claimed letters replicate"), 10.f, []()
	{
		const AGFPlayerState* G = Guest(1);
		return G && G->Mailbox.Num() == 3 && !G->Mailbox.ContainsByPredicate([](const FGFMailLetter& L) { return !L.bClaimed; });
	});
	Stop(Ctx);
	return true;
}

// ------------------------------------------------------------------------------------------------
namespace
{
	struct FBlueprintRun
	{
		FString WorldId;
		int64 Hash = 0;
		int32 Blocks = 0;
		int32 StoneBefore = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFNetBlueprintTest, "GFGame.Net.Blueprint", GFLandNetFlags)
bool FGFNetBlueprintTest::RunTest(const FString&)
{
	FCtxRef Ctx = Start(this, 3);
	TSharedRef<FBlueprintRun> Run = MakeShared<FBlueprintRun>();
	Do(Ctx, [Run]()
	{
		SGS()->MaxEditReach = 0.f;
		Run->WorldId = SGS()->HostWorldId;
	});
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ClaimPlot, PlotA), EGFLandResult::Ok, TEXT("Guest claims PlotA"));
	Do(Ctx, []()
	{
		const FIntVector M = SPlot(PlotA)->Min;
		for (int32 i = 0; i < 5; ++i)
		{
			Guest(0)->RequestEdit(M + FIntVector(i + 2, 3, 1), TEXT("Stone"));
		}
		Guest(0)->RequestEdit(M + FIntVector(4, 3, 2), TEXT("Stone"));
	});
	WaitUntil(Ctx, TEXT("guest's build is up"), 15.f, []() { return VoxelsConverged() && GuestOnServer(0)->GetEditsAccepted() == 6; });

	// Autosave backup: the blueprint matches the plot and reaches the guest's own save.
	Do(Ctx, [this, Run]()
	{
		SGS()->ServerBackupPlots();
		Run->Hash = PlotHashNow(PlotA);
		Run->Blocks = PlotBlocksNow(PlotA);
		const FGFPlotBlueprint* BP = FGFLandRules::FindBlueprint(GuestOnServer(0)->Blueprints, Run->WorldId, PlotA);
		TestTrue(TEXT("Backup hash = plot hash"), BP && BP->Hash == Run->Hash && BP->BlockCount == Run->Blocks);
	});
	WaitUntil(Ctx, TEXT("backup written to the guest's machine"), 10.f, [Run]()
	{
		const UGFLocalProfileSubsystem* P = ProfileOfGuest(0);
		const FGFPlotBlueprint* BP = P ? FGFLandRules::FindBlueprint(P->GetCharacter().Blueprints, Run->WorldId, PlotA) : nullptr;
		return BP && BP->Hash == Run->Hash;
	});
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::PackUpPlot, PlotA), EGFLandResult::NotAllowed, TEXT("Guests can't pack up (host tool)"));

	// Pack up under a bad network: nothing is cleared until the guest's machine confirms it has the blueprint.
	Do(Ctx, [this, Run]()
	{
		SetBadNetwork(true, 5);
		FGFLandAction A = Act(EGFLandOp::PackUpPlot, PlotA);
		const int32 Id = Host()->RequestLandAction(A);
		CheckEnum(this, TEXT("Pack-up waits for the guest"), Host()->LandResults.FindRef(Id), EGFLandResult::Pending);
		TestEqual(TEXT("Still rented while waiting"), SPlot(PlotA)->TenantKey, Guest(0)->PlayerKey);
		TestEqual(TEXT("Nothing cleared before the ack"), PlotBlocksNow(PlotA), Run->Blocks);
	});
	WaitUntil(Ctx, TEXT("pack-up completes after the ack"), 30.f, []() { return !SPlot(PlotA)->HasTenant(); });
	Do(Ctx, [this, Run]()
	{
		SetBadNetwork(false);
		const AGFVoxelWorld* V = Voxel(ServerWorld());
		TestFalse(TEXT("Plot cleared above ground"), FGFLandRules::IsBuiltOn(V->GetGrid(), *SPlot(PlotA)));
		TestEqual(TEXT("Ground kept"), PlotBlocksNow(PlotA), 24 * 24);
		const UGFLocalProfileSubsystem* P = ProfileOfGuest(0);
		const FGFPlotBlueprint* BP = P ? FGFLandRules::FindBlueprint(P->GetCharacter().Blueprints, Run->WorldId, PlotA) : nullptr;
		TestTrue(TEXT("Guest's machine has the blueprint before the clear"), BP && BP->Hash == Run->Hash);
		TestTrue(TEXT("Guest acked at least the pack-up revision"), GuestOnServer(0)->GetAckedRevision() > 0);
		TestNotNull(TEXT("Host's world keeps a copy too"), FGFLandRules::FindBlueprint(SGS()->PackedBlueprints, Run->WorldId, PlotA));
	});
	WaitUntil(Ctx, TEXT("cleared plot reaches clients"), 15.f, []() { return VoxelsConverged(); });

	// Free restore on the same plot: needs the host's OK.
	LandA(Ctx, this, []() { return Guest(0); }, Act(EGFLandOp::ClaimPlot, PlotA), EGFLandResult::Ok, TEXT("Guest re-claims PlotA"));
	Land(Ctx, this, []() { return Guest(0); }, [Run]() { FGFLandAction A = Act(EGFLandOp::RestoreBlueprint, PlotA); A.SourceWorldId = Run->WorldId; A.SourcePlotId = PlotA; return A; }, EGFLandResult::Pending, TEXT("Restore asks the host first"));
	Do(Ctx, [this]() { TestFalse(TEXT("Not restored before the host's OK"), FGFLandRules::IsBuiltOn(Voxel(ServerWorld())->GetGrid(), *SPlot(PlotA))); });
	LandA(Ctx, this, []() { return Guest(1); }, Act(EGFLandOp::ApproveRestore, PlotA), EGFLandResult::NotAllowed, TEXT("Only the host approves"));
	LandA(Ctx, this, []() { return Host(); }, Act(EGFLandOp::ApproveRestore, PlotA), EGFLandResult::Ok, TEXT("Host approves"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Restored build = blueprint"), PlotHashNow(PlotA), Run->Hash);
		TestEqual(TEXT("Restored block count"), PlotBlocksNow(PlotA), Run->Blocks);
		Run->StoneBefore = GuestOnServer(0)->Inventory->GetCount(TEXT("Stone"));
	});
	Land(Ctx, this, []() { return Guest(1); }, [Run]() { FGFLandAction A = Act(EGFLandOp::RestoreBlueprint, PlotA); A.SourceWorldId = Run->WorldId; A.SourcePlotId = PlotA; return A; }, EGFLandResult::NotAllowed, TEXT("Not your plot"));
	Land(Ctx, this, []() { return Host(); }, [Run]() { FGFLandAction A = Act(EGFLandOp::RestoreBlueprint, PlotA); A.SourceWorldId = Run->WorldId; A.SourcePlotId = PlotA; return A; }, EGFLandResult::NoBlueprint, TEXT("Host has no blueprint of their own"));

	// Paid restore onto another plot: the materials again.
	Land(Ctx, this, []() { return Host(); }, []() { FGFLandAction A = ToPlayer(EGFLandOp::AssignPlot, Guest(0)->PlayerKey); A.PlotId = PlotB; return A; }, EGFLandResult::Ok, TEXT("Host assigns a second plot"));
	Land(Ctx, this, []() { return Guest(0); }, [Run]() { FGFLandAction A = Act(EGFLandOp::RestoreBlueprint, PlotB); A.SourceWorldId = Run->WorldId; A.SourcePlotId = PlotA; return A; }, EGFLandResult::Ok, TEXT("Paid restore onto PlotB"));
	Do(Ctx, [this, Run]()
	{
		TestEqual(TEXT("Copy on PlotB = blueprint"), PlotHashNow(PlotB), Run->Hash);
		TestEqual(TEXT("Paid 6 stone"), Run->StoneBefore - GuestOnServer(0)->Inventory->GetCount(TEXT("Stone")), 6);
		TestEqual(TEXT("Original still on PlotA"), PlotHashNow(PlotA), Run->Hash);
	});
	Land(Ctx, this, []() { return Guest(0); }, [Run]() { FGFLandAction A = Act(EGFLandOp::RestoreBlueprint, PlotB); A.SourceWorldId = Run->WorldId; A.SourcePlotId = PlotA; return A; }, EGFLandResult::PlotNotEmpty, TEXT("Never restores over a build"));
	Land(Ctx, this, []() { return Host(); }, []() { FGFLandAction A = ToPlayer(EGFLandOp::AssignPlot, FString()); A.PlotId = PlotB; return A; }, EGFLandResult::PlotNotEmpty, TEXT("Can't unassign a built plot (pack up instead)"));
	WaitUntil(Ctx, TEXT("restores reach clients"), 15.f, []() { return VoxelsConverged(); });
	Stop(Ctx);
	return true;
}

#endif
