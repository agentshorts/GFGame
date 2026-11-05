// Issue 335 spawn rule: a player with no home starts at the rest area (PlayerStartTag RestArea); once AGFPlayerState::SetHome
// has run, the world map spawns them at home. Maps without a rest area start keep the old start rules.

#include "Misc/AutomationTest.h"
#include "GFRestAreaSpawn.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFRestAreaTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	static FGFMapPlayerStart Row(const TCHAR* Tag, const FVector& At)
	{
		FGFMapPlayerStart R;
		R.Tag = Tag ? FName(Tag) : NAME_None;
		R.Location = At;
		return R;
	}

	static TArray<FGFMapPlayerStart> WorldStarts()
	{
		// An untagged start first, so the rest area has to be picked by its tag and not by order.
		return {Row(nullptr, FVector(0.f, 0.f, 120.f)), Row(TEXT("RestArea"), FVector(24000.f, -2400.f, 120.f))};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRestAreaRulesTest, "GFGame.RestArea.SpawnRules", GFRestAreaTests::Flags)
bool FGFRestAreaRulesTest::RunTest(const FString&)
{
	using namespace GFRestAreaTests;
	const FName Rime(TEXT("Rime"));
	TestEqual(TEXT("Start tag"), FGFRestAreaSpawn::StartTag, FName(TEXT("RestArea")));
	TestEqual(TEXT("Rest area index found by tag"), FGFRestAreaSpawn::RestAreaIndex(WorldStarts()), 1);

	const FGFRestAreaSpawnChoice NoHome = FGFRestAreaSpawn::Choose(WorldStarts(), false, Rime);
	TestTrue(TEXT("No home: rest area"), NoHome.Source == EGFSpawnSource::RestArea);
	TestEqual(TEXT("No home: the rest area start"), NoHome.Index, 1);

	const FGFRestAreaSpawnChoice WithHome = FGFRestAreaSpawn::Choose(WorldStarts(), true, Rime);
	TestTrue(TEXT("Home set: home"), WithHome.Source == EGFSpawnSource::Home);
	TestEqual(TEXT("Home set: no map start index"), WithHome.Index, static_cast<int32>(INDEX_NONE));

	// A map with no rest area (hub, towns) keeps the old rules, home or not.
	const TArray<FGFMapPlayerStart> Town = {Row(nullptr, FVector(100.f, 0.f, 120.f)), Row(TEXT("Rime"), FVector(0.f, 500.f, 120.f))};
	for (const bool bHome : {false, true})
	{
		const FGFRestAreaSpawnChoice C = FGFRestAreaSpawn::Choose(Town, bHome, Rime);
		TestTrue(TEXT("No rest area: map rules"), C.Source == EGFSpawnSource::Map);
		TestEqual(TEXT("No rest area: anchor-tagged start"), C.Index, FGFPlayerStartRules::ChooseStartIndex(Town, Rime));
	}
	const FGFRestAreaSpawnChoice Empty = FGFRestAreaSpawn::Choose({}, true, Rime);
	TestTrue(TEXT("No starts: map rules (fallback)"), Empty.Source == EGFSpawnSource::Map && Empty.Index == INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFRestAreaHomeFlagTest, "GFGame.RestArea.HomeFlag", GFRestAreaTests::Flags)
bool FGFRestAreaHomeFlagTest::RunTest(const FString&)
{
	using namespace GFRestAreaTests;
	if (!GEngine)
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFRestAreaWorld"));
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	if (!World->HasBegunPlay() && World->GetWorldSettings())
	{
		World->GetWorldSettings()->NotifyBeginPlay();
	}
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	AGFGameState* GS = World->SpawnActor<AGFGameState>();
	if (!TestNotNull(TEXT("Game state"), GS))
	{
		return false;
	}
	World->SetGameState(GS);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AGFPlayerState* PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Player state"), PS))
	{
		return false;
	}
	PS->PlayerKey = TEXT("RestAreaHome");
	PS->SetPlayerId(335);
	if (GS->PlayerArray.Find(PS) == INDEX_NONE)
	{
		GS->AddPlayerState(PS);
	}
	TestFalse(TEXT("A new player has no home"), PS->HasHome());
	TestTrue(TEXT("So the world map picks the rest area"),
		FGFRestAreaSpawn::Choose(WorldStarts(), PS->HasHome(), TEXT("Rime")).Source == EGFSpawnSource::RestArea);

	const FTransform Home(FRotator(0.f, 90.f, 0.f), FVector(20000.f, -3000.f, 120.f));
	PS->SetHome(Home);
	TestTrue(TEXT("SetHome sets the flag"), PS->HasHome());
	TestTrue(TEXT("SetHome keeps the place"), PS->GetHomeTransform().Equals(Home, 0.01f));
	TestTrue(TEXT("Then the world map picks home"),
		FGFRestAreaSpawn::Choose(WorldStarts(), PS->HasHome(), TEXT("Rime")).Source == EGFSpawnSource::Home);
	return true;
}

#endif
