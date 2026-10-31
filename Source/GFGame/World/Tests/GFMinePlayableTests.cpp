// Pebble Mine, playable from the hub pad (#216): the gate drops you on the pad, the site has rooms and the
// crew verbs, a haul lands in the bag, one upgrade can be bought, and the arch puts you back on the hub stand.

#include "Misc/AutomationTest.h"
#include "GFGameState.h"
#include "GFHubGateDirector.h"
#include "GFInventoryComponent.h"
#include "GFMineDirector.h"
#include "GFMineSite.h"
#include "GFMineTypes.h"
#include "GFMiniGameCatalog.h"
#include "GFMiniGameGate.h"
#include "GFPlayerState.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFMinePlayableTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FMineWorld
	{
		UWorld* World = nullptr;

		explicit FMineWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FMineWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static int32 CountSites(UWorld* World)
	{
		int32 N = 0;
		for (TActorIterator<AGFMineSite> It(World); It; ++It)
		{
			++N;
		}
		return N;
	}

	static AGFMiniGameGate* MinePortal(UWorld* World)
	{
		for (TActorIterator<AGFMiniGameGate> It(World); It; ++It)
		{
			if (It->EntryId == FName(TEXT("Mine")) && It->GateRole == EGFMiniGameGateRole::Portal)
			{
				return *It;
			}
		}
		return nullptr;
	}

	static int32 BagTotal(const AGFPlayerState* PS)
	{
		int32 N = 0;
		if (!PS || !PS->Inventory)
		{
			return 0;
		}
		for (const FGFItemStack& S : PS->Inventory->GetInventory().Items)
		{
			N += S.Count;
		}
		return N;
	}

	static bool BagOnlyShallows(const AGFPlayerState* PS)
	{
		const FGFMineRoomRow* Room = FGFMineData::Get().Room(TEXT("Shallows"));
		if (!PS || !PS->Inventory || !Room)
		{
			return false;
		}
		for (const FGFItemStack& S : PS->Inventory->GetInventory().Items)
		{
			const bool bFromRoom = Room->Yields.ContainsByPredicate([&](const FGFRewardEntry& E) { return E.ItemId == S.ItemId; });
			if (!bFromRoom)
			{
				return false;
			}
		}
		return true;
	}

	static FString JoinLines(const TArray<FString>& Lines)
	{
		FString Joined;
		for (const FString& Line : Lines)
		{
			Joined += Line;
			Joined += TEXT("\n");
		}
		return Joined;
	}

	static bool SameTint(const FLinearColor& A, const FLinearColor& B)
	{
		return A.Equals(B, 0.02f);
	}

	static void MaxUpgrade(FGFMineCrew& Crew, FName Id)
	{
		Crew.SetLevel(Id, FGFMineData::Get().MaxLevel(Id));
	}

	static bool Digging(const AGFPlayerState* PS)
	{
		if (!PS || PS->Progress.Crew.Pebblings.Num() == 0)
		{
			return false;
		}
		const FGFPebbling& P = PS->Progress.Crew.Pebblings[0];
		int32 Carry = 0;
		for (const FGFItemStack& S : P.Carry)
		{
			Carry += S.Count;
		}
		return Carry > 0 && (P.State == EGFPebblingState::Work || P.State == EGFPebblingState::Full);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineSiteOnPadTest, "GFGame.Mine.Playable.SiteOnPad", GFMinePlayableTests::Flags)
bool FGFMineSiteOnPadTest::RunTest(const FString&)
{
	using namespace GFMinePlayableTests;
	FMineWorld W(TEXT("GFMineSiteWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFHubGateDirector* Director = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), Director))
	{
		return false;
	}
	Director->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);

	AGFMineSite* Site = AGFMineSite::Get(W.World);
	if (!TestNotNull(TEXT("Hub place builds a mine site"), Site))
	{
		return false;
	}
	TestEqual(TEXT("One site"), CountSites(W.World), 1);
	TestEqual(TEXT("Three rooms"), Site->NumRooms(), 3);
	const FName Verbs[] = { TEXT("Capture"), TEXT("Assign"), TEXT("Haul"), TEXT("Recall"), TEXT("Buy"), TEXT("Status"), TEXT("Return") };
	for (const FName Verb : Verbs)
	{
		UBoxComponent* Box = Site->GetVerbTrigger(Verb);
		if (TestNotNull(*FString::Printf(TEXT("%s spot"), *Verb.ToString()), Box))
		{
			TestTrue(*FString::Printf(TEXT("%s overlaps"), *Verb.ToString()), Box->GetGenerateOverlapEvents());
			TestTrue(*FString::Printf(TEXT("%s hits pawns"), *Verb.ToString()), Box->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Overlap);
		}
	}
	TestNotNull(TEXT("Shallows spot"), Site->GetRoomTrigger(TEXT("Shallows")));
	TestNotNull(TEXT("Crystal Gallery spot"), Site->GetRoomTrigger(TEXT("CrystalGallery")));
	TestNotNull(TEXT("Glow Grotto spot"), Site->GetRoomTrigger(TEXT("GlowGrotto")));

	TArray<FString> Lines;
	Site->GetPromptLines(nullptr, Lines);
	const FString Joined = JoinLines(Lines);
	const TCHAR* Words[] = { TEXT("Capture"), TEXT("Assign"), TEXT("Haul"), TEXT("Recall"), TEXT("Buy"), TEXT("Status") };
	for (const TCHAR* Word : Words)
	{
		TestTrue(*FString::Printf(TEXT("Prompt names %s"), Word), Joined.Contains(Word));
	}

	const FGFMineRoomRow* Shallows = FGFMineData::Get().Room(TEXT("Shallows"));
	if (TestNotNull(TEXT("Shallows row"), Shallows) && TestTrue(TEXT("Shallows has a yield"), Shallows->Yields.Num() > 0))
	{
		TestEqual(TEXT("Shallows still drops Stone"), Shallows->Yields[0].ItemId.ToString(), FString(TEXT("Stone")));
		TestEqual(TEXT("Stone min stays 2"), Shallows->Yields[0].Min, 2);
		TestEqual(TEXT("Stone max stays 3"), Shallows->Yields[0].Max, 3);
		TestTrue(TEXT("Stone always drops"), FMath::IsNearlyEqual(Shallows->Yields[0].Chance, 1.f));
	}

	const FGFMiniGameEntry* Mine = FGFMiniGameCatalog::Find(TEXT("Mine"));
	AGFMiniGameGate* Portal = MinePortal(W.World);
	if (!TestNotNull(TEXT("Pebble Mine portal"), Portal) || !TestNotNull(TEXT("Mine catalog row"), Mine))
	{
		return false;
	}
	TestEqual(TEXT("Portal says Pebble Mine"), Portal->GetLabel(), FString(TEXT("Pebble Mine")));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Walker = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(0.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Walker"), Walker))
	{
		return false;
	}
	TestFalse(TEXT("Hub stand is not on the mine pad"), Site->IsPlayerNear(Walker));
	TestTrue(TEXT("Walking into the portal enters the mine"), Portal->EnterFrom(Walker));
	const FVector Stand = FGFMiniGameCatalog::SimStand(*Mine, FVector::ZeroVector);
	TestTrue(TEXT("Stands on the mine pad"), Walker->GetActorLocation().Equals(Stand, 5.f));
	TestTrue(TEXT("The stand is inside the mine"), Site->IsPlayerNear(Walker));

	Director->Place(TEXT("/Game/Maps/UEDPIE_0_L_Hub"), FVector::ZeroVector);
	TestEqual(TEXT("Placing again keeps a single site"), CountSites(W.World), 1);
	TestEqual(TEXT("Rooms not doubled"), Site->NumRooms(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMinePlayableLoopTest, "GFGame.Mine.Playable.Loop", GFMinePlayableTests::Flags)
bool FGFMinePlayableLoopTest::RunTest(const FString&)
{
	using namespace GFMinePlayableTests;
	FMineWorld W(TEXT("GFMineLoopWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = W.World->SpawnActor<AGFGameState>();
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}
	W.World->SetGameState(GS);

	AGFHubGateDirector* Hub = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), Hub))
	{
		return false;
	}
	Hub->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);

	AGFMineDirector* Mine = AGFMineDirector::Get(W.World, false);
	AGFMineSite* Site = AGFMineSite::Get(W.World);
	if (!TestNotNull(TEXT("Mine director"), Mine) || !TestNotNull(TEXT("Mine site"), Site))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(400.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Miner");
	PS->SetPlayerId(7);
	PS->bCharacterReady = true;
	PS->Progress.bHasTamed = false;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	TArray<FString> Untamed;
	Site->GetPromptLines(PS, Untamed);
	const FString UntamedText = JoinLines(Untamed);
	TestTrue(TEXT("The mine says the crew is shy"), UntamedText.Contains(TEXT("shy until your first tame")));
	TestTrue(TEXT("The mine points at the Valley tame"), UntamedText.Contains(TEXT("Valley")));
	TestTrue(TEXT("The Valley line names the hub gate"), UntamedText.Contains(TEXT("Valley gate is on the hub")));

	const FGFModeResult Locked = Site->TryCapture(Pawn);
	TestFalse(TEXT("Capture refused before a tame"), Locked.bOk);
	TestEqual(TEXT("Locked"), Locked.Code.ToString(), FString(TEXT("Locked")));
	TestEqual(TEXT("No pebbling yet"), PS->Progress.Crew.Pebblings.Num(), 0);
	TestTrue(TEXT("The capture spot says Valley"), Site->LastPrompt.Contains(TEXT("Valley")));
	TestTrue(TEXT("The capture spot still says Locked"), Site->LastPrompt.Contains(TEXT("Locked")));

	PS->Progress.bHasTamed = true;
	TArray<FString> Tamed;
	Site->GetPromptLines(PS, Tamed);
	const FString TamedText = JoinLines(Tamed);
	TestFalse(TEXT("A tamed character is not sent back to the Valley"), TamedText.Contains(TEXT("Valley gate is on the hub")));
	TestFalse(TEXT("A tamed character is not called shy"), TamedText.Contains(TEXT("shy until your first tame")));
	Mine->HideSeconds = 0.f;
	if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
	{
		Move->Velocity = FVector(450.f, 0.f, 0.f);
	}
	const FGFModeResult Rushed = Site->TryCapture(Pawn);
	TestFalse(TEXT("A rush does not befriend it"), Rushed.bOk);
	TestEqual(TEXT("Startled"), Rushed.Code.ToString(), FString(TEXT("Startled")));
	TestEqual(TEXT("Still no crew"), PS->Progress.Crew.Pebblings.Num(), 0);

	if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
	{
		Move->Velocity = FVector::ZeroVector;
	}
	const FGFModeResult Caught = Site->TryCapture(Pawn);
	if (!TestTrue(TEXT("Standing still captures"), Caught.bOk))
	{
		return false;
	}
	TestEqual(TEXT("One pebbling"), PS->Progress.Crew.Pebblings.Num(), 1);
	TestTrue(TEXT("It has a name"), !PS->Progress.Crew.Pebblings[0].Name.IsNone());

	const FGFModeResult Gallery = Site->TryAssign(Pawn, TEXT("CrystalGallery"));
	TestFalse(TEXT("Crystal Gallery stays locked"), Gallery.bOk);
	TestEqual(TEXT("RoomLocked"), Gallery.Code.ToString(), FString(TEXT("RoomLocked")));

	const FGFModeResult Assigned = Site->TryAssign(Pawn, TEXT("Shallows"));
	if (!TestTrue(TEXT("Assign to the Shallows"), Assigned.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Walking to the Shallows"), PS->Progress.Crew.Pebblings[0].Room.ToString(), FString(TEXT("Shallows")));
	TestTrue(TEXT("On the way"), PS->Progress.Crew.Pebblings[0].State == EGFPebblingState::Travel);

	Mine->SimSpeed = 60.f;
	bool bDug = false;
	for (int32 i = 0; i < 8 && !bDug; ++i)
	{
		Mine->Tick(1.f);
		bDug = Digging(PS);
	}
	if (!TestTrue(TEXT("One load in the satchel"), bDug))
	{
		return false;
	}
	TestEqual(TEXT("Bag empty until the haul"), BagTotal(PS), 0);

	const FGFModeResult Hauled = Site->TryHaul(Pawn);
	if (!TestTrue(TEXT("Haul order"), Hauled.bOk))
	{
		return false;
	}
	bool bHome = false;
	for (int32 i = 0; i < 4 && !bHome; ++i)
	{
		Mine->Tick(1.f);
		bHome = BagTotal(PS) > 0;
	}
	const int32 Brought = BagTotal(PS);
	TestTrue(TEXT("Pebbles land in the bag"), Brought >= 2 && Brought <= 5);
	TestTrue(TEXT("Only Shallows drops"), BagOnlyShallows(PS));
	TestEqual(TEXT("No Wood in a Shallows haul"), PS->Inventory->GetCount(TEXT("Wood")), 0);

	Mine->SimSpeed = 0.f;
	const FGFModeResult Bought = Site->TryBuy(Pawn);
	TestTrue(TEXT("One upgrade bought"), Bought.bOk && Bought.Ints.Num() == 1 && Bought.Ints[0] == 1);
	TestEqual(TEXT("Picks 1"), PS->Progress.Crew.Level(GFMineUpgrades::Picks), 1);
	TestEqual(TEXT("The advance was spent"), PS->Inventory->GetCount(TEXT("Wood")), 0);

	const FGFModeResult Status = Site->TryStatus(Pawn);
	TestTrue(TEXT("Status"), Status.bOk);

	const FGFModeResult Recalled = Site->TryRecall(Pawn);
	TestTrue(TEXT("Recall"), Recalled.bOk);
	TestTrue(TEXT("Back toward the mouth"), PS->Progress.Crew.Pebblings[0].Room.IsNone());

	TestTrue(TEXT("Return arch"), Site->TryReturn(Pawn));
	TestTrue(TEXT("Back on the hub stand"), Pawn->GetActorLocation().Equals(Site->GetHubReturnLocation(), 5.f));
	TestFalse(TEXT("The hub stand is outside the mine HUD"), Site->IsPlayerNear(Pawn));
	TestTrue(TEXT("Hub stand is the Rime anchor plus 120"), Site->GetHubReturnLocation().Equals(FVector(0.f, 0.f, AGFMineSite::HubStandZCm), 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineRoomTintTest, "GFGame.Mine.Playable.RoomTint", GFMinePlayableTests::Flags)
bool FGFMineRoomTintTest::RunTest(const FString&)
{
	using namespace GFMinePlayableTests;
	FMineWorld W(TEXT("GFMineTintWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	// The game state comes first, as in a real map, so the mine director registers its mode handler on BeginPlay.
	AGFGameState* GS = W.World->SpawnActor<AGFGameState>();
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}
	W.World->SetGameState(GS);
	AGFHubGateDirector* Hub = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Hub director"), Hub))
	{
		return false;
	}
	Hub->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);
	AGFMineSite* Site = AGFMineSite::Get(W.World);
	if (!TestNotNull(TEXT("Mine site"), Site))
	{
		return false;
	}

	const FLinearColor ShallowsOpen = Site->GetRoomOpenColor(TEXT("Shallows"));
	const FLinearColor GalleryOpen = Site->GetRoomOpenColor(TEXT("CrystalGallery"));
	const FLinearColor GrottoOpen = Site->GetRoomOpenColor(TEXT("GlowGrotto"));
	TestTrue(TEXT("Shallows is open at build"), SameTint(Site->GetRoomTint(TEXT("Shallows")), ShallowsOpen));
	TestTrue(TEXT("Crystal Gallery starts dim"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen * AGFMineSite::LockedRoomBrightness));
	TestTrue(TEXT("Glow Grotto starts dim"), SameTint(Site->GetRoomTint(TEXT("GlowGrotto")), GrottoOpen * AGFMineSite::LockedRoomBrightness));
	TestTrue(TEXT("Gallery sign starts locked"), Site->GetRoomSign(TEXT("CrystalGallery")).Contains(TEXT("(locked)")));
	TestTrue(TEXT("Grotto sign starts locked"), Site->GetRoomSign(TEXT("GlowGrotto")).Contains(TEXT("(locked)")));
	TestFalse(TEXT("Shallows sign is not locked"), Site->GetRoomSign(TEXT("Shallows")).Contains(TEXT("(locked)")));

	FGFMineCrew WalkIn;
	FGFMineRules::AddPebbling(WalkIn);
	Site->SyncRoomTints(WalkIn);
	TestTrue(TEXT("A crew with no Tunnels leaves the gallery dim"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen * AGFMineSite::LockedRoomBrightness));
	WalkIn.SetLevel(GFMineUpgrades::Tunnels, 1);
	Site->SyncRoomTints(WalkIn);
	TestTrue(TEXT("Tunnels lights the Crystal Gallery"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen));
	TestTrue(TEXT("Glow Grotto stays dim without Lanterns"), SameTint(Site->GetRoomTint(TEXT("GlowGrotto")), GrottoOpen * AGFMineSite::LockedRoomBrightness));
	TestFalse(TEXT("Gallery sign drops the lock"), Site->GetRoomSign(TEXT("CrystalGallery")).Contains(TEXT("(locked)")));
	TestTrue(TEXT("Grotto sign stays locked"), Site->GetRoomSign(TEXT("GlowGrotto")).Contains(TEXT("(locked)")));
	WalkIn.SetLevel(GFMineUpgrades::Tunnels, 0);
	Site->SyncRoomTints(WalkIn);
	TestTrue(TEXT("Losing Tunnels dims the gallery again"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen * AGFMineSite::LockedRoomBrightness));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(400.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Inventory"), PS->Inventory.Get()))
	{
		return false;
	}
	PS->PlayerKey = TEXT("Miner");
	PS->SetPlayerId(7);
	PS->bCharacterReady = true;
	PS->Progress.bHasTamed = true;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);

	TestEqual(TEXT("Picks still has 3 levels"), FGFMineData::Get().MaxLevel(GFMineUpgrades::Picks), 3);
	MaxUpgrade(PS->Progress.Crew, GFMineUpgrades::Picks);
	MaxUpgrade(PS->Progress.Crew, GFMineUpgrades::Satchels);
	MaxUpgrade(PS->Progress.Crew, GFMineUpgrades::Rails);
	MaxUpgrade(PS->Progress.Crew, GFMineUpgrades::CrewQuarters);
	TestTrue(TEXT("Stone for Tunnels"), PS->Inventory->AddItem(TEXT("Stone"), 30));
	TestTrue(TEXT("Coalnut for Tunnels"), PS->Inventory->AddItem(TEXT("Coalnut"), 6));
	const FGFModeResult Tunnels = Site->TryBuy(Pawn);
	if (!TestTrue(TEXT("Buying Tunnels"), Tunnels.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Tunnels 1"), PS->Progress.Crew.Level(GFMineUpgrades::Tunnels), 1);
	TestTrue(TEXT("The buy retints the Crystal Gallery"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen));
	TestTrue(TEXT("Glow Grotto is still dim after Tunnels"), SameTint(Site->GetRoomTint(TEXT("GlowGrotto")), GrottoOpen * AGFMineSite::LockedRoomBrightness));
	TestTrue(TEXT("Shallows stays bright"), SameTint(Site->GetRoomTint(TEXT("Shallows")), ShallowsOpen));

	TestTrue(TEXT("Coalnut for Lanterns"), PS->Inventory->AddItem(TEXT("Coalnut"), 10));
	TestTrue(TEXT("Glass for Lanterns"), PS->Inventory->AddItem(TEXT("Glass"), 4));
	const FGFModeResult Lanterns = Site->TryBuy(Pawn);
	if (!TestTrue(TEXT("Buying Lanterns"), Lanterns.bOk))
	{
		return false;
	}
	TestEqual(TEXT("Lanterns 1"), PS->Progress.Crew.Level(GFMineUpgrades::Lanterns), 1);
	TestTrue(TEXT("The buy retints the Glow Grotto"), SameTint(Site->GetRoomTint(TEXT("GlowGrotto")), GrottoOpen));
	TestTrue(TEXT("Crystal Gallery stays bright"), SameTint(Site->GetRoomTint(TEXT("CrystalGallery")), GalleryOpen));
	TestFalse(TEXT("Grotto sign is unlocked"), Site->GetRoomSign(TEXT("GlowGrotto")).Contains(TEXT("(locked)")));

	const FGFMineRoomRow* Shallows = FGFMineData::Get().Room(TEXT("Shallows"));
	const FGFMineRoomRow* Gallery = FGFMineData::Get().Room(TEXT("CrystalGallery"));
	const FGFMineRoomRow* Grotto = FGFMineData::Get().Room(TEXT("GlowGrotto"));
	if (TestNotNull(TEXT("Shallows row"), Shallows) && TestNotNull(TEXT("Gallery row"), Gallery) && TestNotNull(TEXT("Grotto row"), Grotto))
	{
		TestTrue(TEXT("Shallows still needs nothing"), Shallows->NeedsUpgrade.IsNone());
		TestTrue(TEXT("Gallery still needs Tunnels"), Gallery->NeedsUpgrade == GFMineUpgrades::Tunnels);
		TestTrue(TEXT("Grotto still needs Lanterns"), Grotto->NeedsUpgrade == GFMineUpgrades::Lanterns);
		if (TestTrue(TEXT("Shallows still has a yield"), Shallows->Yields.Num() > 0))
		{
			TestEqual(TEXT("Shallows still drops Stone"), Shallows->Yields[0].ItemId.ToString(), FString(TEXT("Stone")));
			TestEqual(TEXT("Shallows Stone min unchanged"), Shallows->Yields[0].Min, 2);
			TestEqual(TEXT("Shallows Stone max unchanged"), Shallows->Yields[0].Max, 3);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFMineTestKitTameTest, "GFGame.Mine.Playable.TestKit", GFMinePlayableTests::Flags)
bool FGFMineTestKitTameTest::RunTest(const FString&)
{
	using namespace GFMinePlayableTests;
	TestFalse(TEXT("A comment does not turn the kit on"), AGFGameState::TestKitEnabledInIniText(TEXT("; bAutoGrantTestKit=True\n")));
	TestFalse(TEXT("False stays off"), AGFGameState::TestKitEnabledInIniText(TEXT("[/Script/GFGame.GFGameMode]\nbAutoGrantTestKit=False\n")));
	TestFalse(TEXT("A missing key stays off"), AGFGameState::TestKitEnabledInIniText(TEXT("[/Script/GFGame.GFGameMode]\nbFreeBuild=True\n")));
	TestTrue(TEXT("Game mode section"), AGFGameState::TestKitEnabledInIniText(TEXT("[/Script/GFGame.GFGameMode]\nbAutoGrantTestKit=True\n")));
	TestTrue(TEXT("Game state section"), AGFGameState::TestKitEnabledInIniText(TEXT("[/Script/GFNet.GFGameState]\nbAutoGrantTestKit=True\n")));
	TestTrue(TEXT("Any other section"), AGFGameState::TestKitEnabledInIniText(TEXT("[/Script/Some.OtherKit]\nbAutoGrantTestKit=True\n")));
	TestTrue(TEXT("One true section wins"), AGFGameState::TestKitEnabledInIniText(
		TEXT("[/Script/GFGame.GFGameMode]\nbAutoGrantTestKit=False\n[/Script/Packaging.Local]\nbAutoGrantTestKit=True\n")));

	FMineWorld W(TEXT("GFMineKitWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFGameState* GS = W.World->SpawnActor<AGFGameState>();
	AGFHubGateDirector* Hub = W.World->SpawnActor<AGFHubGateDirector>();
	if (!TestNotNull(TEXT("Game state"), GS) || !TestNotNull(TEXT("Hub director"), Hub))
	{
		return false;
	}
	W.World->SetGameState(GS);
	Hub->Place(TEXT("/Game/Maps/L_Hub"), FVector::ZeroVector);
	AGFMineDirector* Mine = AGFMineDirector::Get(W.World, false);
	AGFMineSite* Site = AGFMineSite::Get(W.World);
	if (!TestNotNull(TEXT("Mine director"), Mine) || !TestNotNull(TEXT("Mine site"), Site))
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	ACharacter* Pawn = W.World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(FVector(400.f, 0.f, 200.f)), Params);
	if (!TestNotNull(TEXT("Player"), PS) || !TestNotNull(TEXT("Pawn"), Pawn))
	{
		return false;
	}
	PS->PlayerKey = TEXT("KitMiner");
	PS->SetPlayerId(8);
	PS->bCharacterReady = true;
	PS->Progress.bHasTamed = false;
	GS->AddPlayerState(PS);
	Pawn->SetPlayerState(PS);
	Mine->HideSeconds = 0.f;

	const FGFModeResult Locked = Site->TryCapture(Pawn);
	TestFalse(TEXT("Without the kit, capture is locked"), Locked.bOk);
	TestEqual(TEXT("Locked"), Locked.Code.ToString(), FString(TEXT("Locked")));
	TestTrue(TEXT("CheckCapture agrees"), FGFMineRules::CheckCapture(PS->Progress, 0.f) == EGFMineOrder::Locked);

	GS->bAutoGrantTestKit = true;
	GS->ApplyTestKitTame(PS);
	TestTrue(TEXT("The test kit marks the first tame"), PS->Progress.bHasTamed);
	TestEqual(TEXT("The kit does not add a companion"), PS->Companions.Num(), 0);
	TestEqual(TEXT("The kit does not add a pebbling"), PS->Progress.Crew.Pebblings.Num(), 0);
	TestTrue(TEXT("CheckCapture is no longer Locked"), FGFMineRules::CheckCapture(PS->Progress, 0.f) == EGFMineOrder::Ok);
	TestTrue(TEXT("Rushing is still a startle"), FGFMineRules::CheckCapture(PS->Progress, 400.f) == EGFMineOrder::Startled);

	if (UCharacterMovementComponent* Move = Pawn->GetCharacterMovement())
	{
		Move->Velocity = FVector::ZeroVector;
	}
	const FGFModeResult Caught = Site->TryCapture(Pawn);
	TestTrue(TEXT("A kit playtest can capture"), Caught.bOk);
	TestEqual(TEXT("One pebbling, from capture, not from the kit"), PS->Progress.Crew.Pebblings.Num(), 1);

	AGFPlayerState* Already = W.World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (TestNotNull(TEXT("Second player"), Already))
	{
		Already->Progress.bHasTamed = true;
		const int32 Before = Already->Companions.Num();
		GS->ApplyTestKitTame(Already);
		TestTrue(TEXT("An existing tame stays"), Already->Progress.bHasTamed);
		TestEqual(TEXT("Still no companion added"), Already->Companions.Num(), Before);
	}
	return true;
}

#endif
