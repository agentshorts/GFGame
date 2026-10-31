// Placeable Atelier (#133): the server sews DT_Recipes rows whose station is Atelier.
// A client call and a pawn who is not at the bench leave the bag alone.

#include "Misc/AutomationTest.h"
#include "GFAtelierStation.h"
#include "GFDataTables.h"
#include "GFInventoryComponent.h"
#include "GFPlayerState.h"
#include "GFRecipeTypes.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFAtelierTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FWorldScope
	{
		UWorld* World = nullptr;

		explicit FWorldScope(const TCHAR* Name)
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

		~FWorldScope()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	struct FBench
	{
		AGFAtelierStation* Station = nullptr;
		AGFPlayerState* PS = nullptr;
		ACharacter* Pawn = nullptr;
	};

	static FBench Spawn(UWorld* World, const FVector& Where)
	{
		FBench Out;
		if (!World)
		{
			return Out;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Out.Station = World->SpawnActor<AGFAtelierStation>(AGFAtelierStation::StaticClass(), FTransform(Where), Params);
		Out.PS = World->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		Out.Pawn = World->SpawnActor<ACharacter>(ACharacter::StaticClass(), FTransform(Where), Params);
		if (Out.PS)
		{
			Out.PS->PlayerKey = TEXT("Seamster");
			Out.PS->SetPlayerId(4);
			Out.PS->bCharacterReady = true;
		}
		if (Out.Pawn && Out.PS)
		{
			Out.Pawn->SetPlayerState(Out.PS);
		}
		return Out;
	}

	static void Give(UGFInventoryComponent* Inventory, const FGFRecipeRow& Recipe)
	{
		for (const FGFItemStack& In : Recipe.Inputs)
		{
			Inventory->AddItem(In.ItemId, In.Count);
		}
	}

	static FName FirstAtelierRecipe(const UDataTable* Recipes)
	{
		FName Found = NAME_None;
		if (!Recipes)
		{
			return Found;
		}
		Recipes->ForeachRow<FGFRecipeRow>(TEXT("AtelierTest"), [&](const FName& Id, const FGFRecipeRow& Row)
		{
			if (Found.IsNone() && Row.Station == AGFAtelierStation::StationId && Id == Row.Output.ItemId && Row.Output.Count == 1)
			{
				Found = Id;
			}
		});
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAtelierCraftTest, "GFGame.Fashion.Atelier.Craft", GFAtelierTests::Flags)
bool FGFAtelierCraftTest::RunTest(const FString&)
{
	using namespace GFAtelierTests;
	FWorldScope Scope(TEXT("GFAtelierCraftWorld"));
	if (!TestNotNull(TEXT("World"), Scope.World))
	{
		return false;
	}
	TestFalse(TEXT("The Atelier can be dropped in a level"), AGFAtelierStation::StaticClass()->HasAnyClassFlags(CLASS_NotPlaceable));
	TestEqual(TEXT("Station id"), AGFAtelierStation::StationId.ToString(), FString(TEXT("Atelier")));

	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	if (!TestNotNull(TEXT("DT_Recipes"), Recipes))
	{
		return false;
	}
	const FName FashionId = FirstAtelierRecipe(Recipes);
	const FGFRecipeRow* Fashion = Recipes->FindRow<FGFRecipeRow>(FashionId, TEXT("AtelierTest"), false);
	const FGFRecipeRow* Coat = Recipes->FindRow<FGFRecipeRow>(TEXT("LabCoat"), TEXT("AtelierTest"), false);
	const FGFRecipeRow* Plank = Recipes->FindRow<FGFRecipeRow>(TEXT("WoodPlank"), TEXT("AtelierTest"), false);
	if (!TestNotNull(TEXT("An Atelier recipe"), Fashion) || !TestNotNull(TEXT("LabCoat recipe"), Coat) || !TestNotNull(TEXT("WoodPlank recipe"), Plank))
	{
		return false;
	}
	if (!TestTrue(TEXT("The Atelier recipe has inputs"), Fashion->Inputs.Num() > 0) || !TestTrue(TEXT("WoodPlank makes planks"), Plank->Output.Count == 4))
	{
		return false;
	}

	const FBench Bench = Spawn(Scope.World, FVector::ZeroVector);
	if (!TestNotNull(TEXT("Station"), Bench.Station) || !TestNotNull(TEXT("Pawn"), Bench.Pawn) || !TestNotNull(TEXT("Inventory"), Bench.PS ? Bench.PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	UGFInventoryComponent* Bag = Bench.PS->Inventory.Get();
	TestTrue(TEXT("The sign says Atelier"), Bench.Station->GetSignText().Contains(TEXT("Atelier")));
	TestNotNull(TEXT("Bench mesh"), Bench.Station->GetBenchMesh());
	if (UStaticMesh* Mesh = Bench.Station->GetBenchMesh()->GetStaticMesh())
	{
		TestEqual(TEXT("Greybox cube"), Mesh->GetName(), FString(TEXT("Cube")));
	}

	TestEqual(TEXT("No recipe"), Bench.Station->TryCraft(Bench.Pawn, NAME_None), EGFCraftResult::UnknownRecipe);
	TestEqual(TEXT("Unknown recipe"), Bench.Station->TryCraft(Bench.Pawn, TEXT("not_a_recipe")), EGFCraftResult::UnknownRecipe);
	TestEqual(TEXT("Unknown prompt"), Bench.Station->LastPrompt, FString(TEXT("Atelier: UnknownRecipe")));
	TestEqual(TEXT("Bag still empty"), Bag->GetCount(Fashion->Output.ItemId), 0);

	TestEqual(TEXT("Lab Coat is the Lab Bench, not the Atelier"), Bench.Station->TryCraft(Bench.Pawn, TEXT("LabCoat")), EGFCraftResult::WrongStation);
	TestEqual(TEXT("Wrong station prompt"), Bench.Station->LastPrompt, FString(TEXT("Atelier: WrongStation")));
	TestEqual(TEXT("Lab Coat was not sewn"), Bag->GetCount(TEXT("LabCoat")), 0);

	Give(Bag, *Fashion);
	Bench.Pawn->SetActorLocation(FVector(AGFAtelierStation::CraftReachCm + 80.f, 0.f, 0.f));
	TestEqual(TEXT("Too far from the bench"), Bench.Station->TryCraft(Bench.Pawn, FashionId), EGFCraftResult::WrongStation);
	TestEqual(TEXT("Distance prompt"), Bench.Station->LastPrompt, FString(TEXT("Atelier: stand at the bench.")));
	TestEqual(TEXT("Far attempt spent nothing"), Bag->GetCount(Fashion->Inputs[0].ItemId), Fashion->Inputs[0].Count);
	TestEqual(TEXT("Far attempt sewed nothing"), Bag->GetCount(FashionId), 0);

	Bench.Pawn->SetActorLocation(FVector::ZeroVector);
	Bench.Station->SetCraftAuthorityForTests(false);
	TestEqual(TEXT("A client does not sew"), Bench.Station->TryCraft(Bench.Pawn, FashionId), EGFCraftResult::NotAuthority);
	TestEqual(TEXT("Client prompt"), Bench.Station->LastPrompt, FString(TEXT("Atelier: NotAuthority")));
	TestEqual(TEXT("Client spent nothing"), Bag->GetCount(Fashion->Inputs[0].ItemId), Fashion->Inputs[0].Count);
	TestEqual(TEXT("Client sewed nothing"), Bag->GetCount(FashionId), 0);
	Bench.Station->SetCraftAuthorityForTests(true);

	TestEqual(TEXT("Sewn at the bench"), Bench.Station->TryCraft(Bench.Pawn, FashionId), EGFCraftResult::Crafted);
	TestEqual(TEXT("Crafted prompt"), Bench.Station->LastPrompt, FString(TEXT("Atelier: Crafted")));
	TestEqual(TEXT("One piece in the bag"), Bag->GetCount(FashionId), 1);
	for (const FGFItemStack& In : Fashion->Inputs)
	{
		TestEqual(*FString::Printf(TEXT("%s spent"), *In.ItemId.ToString()), Bag->GetCount(In.ItemId), 0);
	}
	TestEqual(TEXT("A second sew needs cloth"), Bench.Station->TryCraft(Bench.Pawn, FashionId), EGFCraftResult::MissingInputs);
	TestEqual(TEXT("Still one piece"), Bag->GetCount(FashionId), 1);

	// An empty station means craft anywhere. The Atelier does not refuse those rows.
	TestTrue(TEXT("Wood for a plank"), Bag->AddItem(TEXT("Wood"), 1));
	TestEqual(TEXT("A blank station still crafts here"), Bench.Station->TryCraft(Bench.Pawn, TEXT("WoodPlank")), EGFCraftResult::Crafted);
	TestEqual(TEXT("Four planks"), Bag->GetCount(TEXT("WoodPlank")), 4);
	TestEqual(TEXT("The wood was spent"), Bag->GetCount(TEXT("Wood")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFAtelierBenchTest, "GFGame.Fashion.Atelier.Bench", GFAtelierTests::Flags)
bool FGFAtelierBenchTest::RunTest(const FString&)
{
	using namespace GFAtelierTests;
	FWorldScope Scope(TEXT("GFAtelierBenchWorld"));
	if (!TestNotNull(TEXT("World"), Scope.World))
	{
		return false;
	}
	const UDataTable* Recipes = FGFDataTables::Get<FGFRecipeRow>(TEXT("DT_Recipes"));
	const FName FashionId = FirstAtelierRecipe(Recipes);
	const FGFRecipeRow* Fashion = Recipes ? Recipes->FindRow<FGFRecipeRow>(FashionId, TEXT("AtelierTest"), false) : nullptr;
	if (!TestNotNull(TEXT("An Atelier recipe"), Fashion) || !TestTrue(TEXT("The Atelier recipe has inputs"), Fashion->Inputs.Num() > 0))
	{
		return false;
	}

	const FBench Bench = Spawn(Scope.World, FVector(1000.f, 0.f, 0.f));
	if (!TestNotNull(TEXT("Station"), Bench.Station) || !TestNotNull(TEXT("Pawn"), Bench.Pawn) || !TestNotNull(TEXT("Inventory"), Bench.PS ? Bench.PS->Inventory.Get() : nullptr))
	{
		return false;
	}
	UGFInventoryComponent* Bag = Bench.PS->Inventory.Get();
	Bench.Station->BenchRecipeId = FashionId;
	Give(Bag, *Fashion);
	Give(Bag, *Fashion);

	Bench.Station->PollBench();
	TestEqual(TEXT("Standing at the bench sews once"), Bag->GetCount(FashionId), 1);
	TestEqual(TEXT("One visit spends one set of inputs"), Bag->GetCount(Fashion->Inputs[0].ItemId), Fashion->Inputs[0].Count);
	Bench.Station->PollBench();
	TestEqual(TEXT("The same visit does not sew again"), Bag->GetCount(FashionId), 1);

	Bench.Pawn->SetActorLocation(Bench.Station->GetActorLocation() + FVector(AGFAtelierStation::CraftReachCm + 80.f, 0.f, 0.f));
	Bench.Station->PollBench();
	Bench.Pawn->SetActorLocation(Bench.Station->GetActorLocation());
	Bench.Station->PollBench();
	TestEqual(TEXT("Coming back sews the next one"), Bag->GetCount(FashionId), 2);
	TestEqual(TEXT("Both sets of inputs are gone"), Bag->GetCount(Fashion->Inputs[0].ItemId), 0);

	Give(Bag, *Fashion);
	Bench.Station->SetCraftAuthorityForTests(false);
	Bench.Pawn->SetActorLocation(Bench.Station->GetActorLocation() + FVector(AGFAtelierStation::CraftReachCm + 80.f, 0.f, 0.f));
	Bench.Station->PollBench();
	Bench.Pawn->SetActorLocation(Bench.Station->GetActorLocation());
	Bench.Station->PollBench();
	TestEqual(TEXT("A client visit sews nothing"), Bag->GetCount(FashionId), 2);
	TestEqual(TEXT("A client visit spends nothing"), Bag->GetCount(Fashion->Inputs[0].ItemId), Fashion->Inputs[0].Count);
	return true;
}

#endif
