// The elder errand, played through the server. No HUD.
// Break a town crystal through the town node field, get witnessed, watch the villager warn and flee,
// let the elder hire, clear the heat, accept, hunt, turn in, craft, wear, and win a tier A show.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.Quest.EndToEnd+GFGame.Net.Quest.EndToEnd;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

#include "GFAtelierStation.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFCharacter.h"
#include "GFCraftingSubsystem.h"
#include "GFDino.h"
#include "GFDinoKill.h"
#include "GFElder.h"
#include "GFElderBook.h"
#include "GFFashionTypes.h"
#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMagicComponent.h"
#include "GFMagicPlayerState.h"
#include "GFMineable.h"
#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "GFQuestLine.h"
#include "GFQuestSubsystem.h"
#include "GFRunwayJudge.h"
#include "GFRunwaySite.h"
#include "GFRunwayStage.h"
#include "GFSaveGame.h"
#include "GFSaveSubsystem.h"
#include "GFTamingComponent.h"
#include "GFTerritorySubsystem.h"
#include "GFTheftDirector.h"
#include "GFTownNodes.h"
#include "GFTownsfolkRules.h"
#include "GFVillager.h"
#include "GFNetTestUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFQuestE2E
{
	static const FName Rime(TEXT("Rime"));
	static const FName Verdant(TEXT("Verdant"));
	static const FName Cinderwrights(TEXT("Cinderwrights"));
	static const FName ScalesId(TEXT("Scales"));
	static const FName HideId(TEXT("Hide"));
	static const FName ClayId(TEXT("Clay"));
	static const FName CrystalId(TEXT("Crystal"));
	static const FName SkinNipperId(TEXT("Skin_Nipper"));
	static const FName NipperId(TEXT("Nipper"));
	static const FName StrayId(TEXT("Stray"));
	static const FName PieceId(TEXT("dirtachi_01"));
	static const FName RecipeId(TEXT("dirtachi_01"));
	static const FName GardenTheme(TEXT("GardenParty"));
	static const TCHAR* PenAsset = TEXT("SM_Town_CaravanPen");

	struct FTri
	{
		FVector A;
		FVector B;
		FVector C;
		int32 Slot = INDEX_NONE;
	};

	struct FStand
	{
		FVector Impact = FVector::ZeroVector;
		FVector At = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
		bool bOk = false;
	};

	struct FTown
	{
		FName Id;
		FVector Center = FVector::ZeroVector;
		float Camp = 0.f;
	};

	struct FPlayer
	{
		AGFMagicPlayerState* PS = nullptr;
		AGFPlayerController* PC = nullptr;
		AGFCharacter* Pawn = nullptr;
		FString Key;
	};

	struct FHooks
	{
		UWorld* World = nullptr;
		AGFGameState* GS = nullptr;
		UGFQuestSubsystem* Quest = nullptr;
		UGFBlockIntegritySubsystem* Magic = nullptr;
		AGFTheftDirector* Theft = nullptr;

		bool Bind(FAutomationTestBase& T, UWorld* InWorld)
		{
			World = InWorld;
			GS = World ? World->GetGameState<AGFGameState>() : nullptr;
			Quest = World ? World->GetSubsystem<UGFQuestSubsystem>() : nullptr;
			Magic = World ? World->GetSubsystem<UGFBlockIntegritySubsystem>() : nullptr;
			if (Quest)
			{
				Quest->EnsureRegistered();
			}
			Theft = nullptr;
			if (World)
			{
				for (TActorIterator<AGFTheftDirector> It(World); It; ++It)
				{
					Theft = *It;
					break;
				}
				if (!Theft)
				{
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					Theft = World->SpawnActor<AGFTheftDirector>(AGFTheftDirector::StaticClass(), FTransform::Identity, Params);
				}
			}
			return T.TestNotNull(TEXT("World"), World)
				&& T.TestNotNull(TEXT("Game state"), GS)
				&& T.TestNotNull(TEXT("Quest subsystem"), Quest)
				&& T.TestNotNull(TEXT("Integrity subsystem"), Magic)
				&& T.TestNotNull(TEXT("Theft director"), Theft);
		}
	};

	struct FSession
	{
		UWorld* World = nullptr;

		bool Open(FAutomationTestBase& T, const TCHAR* Name, FHooks& Out)
		{
			if (!T.TestNotNull(TEXT("Engine"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			if (!World)
			{
				T.AddError(TEXT("CreateWorld failed"));
				return false;
			}
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
			AGFGameState* GS = World->SpawnActor<AGFGameState>();
			if (GS)
			{
				World->SetGameState(GS);
			}
			return Out.Bind(T, World);
		}

		~FSession()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};

	struct FScene
	{
		FTown Town;
		AGFElder* Elder = nullptr;
		AGFVillager* Witness = nullptr;
		AGFVillager* Bystander = nullptr;
		UStaticMeshComponent* Crystal = nullptr;
		TArray<AGFDino*> Quarry;
	};

	static TArray<FTri> Triangles(UStaticMesh* Mesh)
	{
		TArray<FTri> Out;
		const FMeshDescription* MD = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!MD)
		{
			return Out;
		}
		FStaticMeshConstAttributes Attr(*MD);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		const TPolygonGroupAttributesConstRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		for (const FTriangleID T : MD->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> V = MD->GetTriangleVertices(T);
			FTri Tri;
			Tri.A = FVector(Pos[V[0]]);
			Tri.B = FVector(Pos[V[1]]);
			Tri.C = FVector(Pos[V[2]]);
			Tri.Slot = Mesh->GetMaterialIndex(SlotNames[MD->GetTrianglePolygonGroup(T)]);
			Out.Add(Tri);
		}
		return Out;
	}

	static bool ConfirmStand(UWorld* World, UStaticMeshComponent* Comp, const FHitResult& Hit, float Distance, TFunctionRef<bool(const FHitResult&)> Accept, FStand& Out)
	{
		if (Hit.GetComponent() != Comp || !Accept(Hit))
		{
			return false;
		}
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
		if (Normal.IsNearlyZero())
		{
			return false;
		}
		const FVector Stand = FVector(Hit.ImpactPoint) + Normal * Distance;
		FHitResult Check;
		if (!World->LineTraceSingleByChannel(Check, Stand, FVector(Hit.ImpactPoint), ECC_Visibility, FGFMineable::QueryParams()))
		{
			return false;
		}
		if (Check.GetComponent() != Comp || !Accept(Check))
		{
			return false;
		}
		Out.Impact = Check.ImpactPoint;
		Out.At = Stand;
		Out.Normal = Check.ImpactNormal.GetSafeNormal();
		Out.bOk = true;
		return true;
	}

	static FStand StandOutside(UWorld* World, UStaticMeshComponent* Comp, float Distance, TFunctionRef<bool(const FHitResult&)> Accept)
	{
		FStand Out;
		if (!World || !Comp)
		{
			return Out;
		}
		Comp->UpdateBounds();
		const FVector Center = Comp->Bounds.Origin;
		const float Reach = Comp->Bounds.SphereRadius + Distance + 100.f;
		const FVector Dirs[] = {
			FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0), FVector(0, 0, 1), FVector(0, 0, -1)
		};
		for (const FVector& Dir : Dirs)
		{
			FHitResult Hit;
			const FVector Start = Center + Dir * Reach;
			if (!World->LineTraceSingleByChannel(Hit, Start, Center, ECC_Visibility, FGFMineable::QueryParams()))
			{
				continue;
			}
			if (ConfirmStand(World, Comp, Hit, Distance, Accept, Out))
			{
				return Out;
			}
		}
		const TArray<FTri> Tris = Triangles(Comp->GetStaticMesh());
		const FTransform Xform = Comp->GetComponentTransform();
		const int32 Step = FMath::Max(1, Tris.Num() / 96);
		for (int32 Pass = 0; Pass < 2 && !Out.bOk; ++Pass)
		{
			int32 Tried = 0;
			for (int32 Index = 0; Index < Tris.Num() && Tried < 96 && !Out.bOk; ++Index)
			{
				const FTri& Tri = Tris[Index];
				if (Pass == 0)
				{
					if (Tri.Slot == 0)
					{
						continue;
					}
				}
				else if ((Index % Step) != 0)
				{
					continue;
				}
				++Tried;
				const FVector A = Xform.TransformPosition(Tri.A);
				const FVector B = Xform.TransformPosition(Tri.B);
				const FVector C = Xform.TransformPosition(Tri.C);
				const FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
				if (N.IsNearlyZero())
				{
					continue;
				}
				const FVector Mid = (A + B + C) / 3.0;
				for (const float Side : { 1.f, -1.f })
				{
					FHitResult Hit;
					const FVector Start = Mid + N * (Side * (Distance + 4.f));
					if (!World->LineTraceSingleByChannel(Hit, Start, Mid - N * (Side * 2.f), ECC_Visibility, FGFMineable::QueryParams()))
					{
						continue;
					}
					if (ConfirmStand(World, Comp, Hit, Distance, Accept, Out))
					{
						return Out;
					}
				}
			}
		}
		return Out;
	}

	static bool CrystalFace(const FHitResult& Hit)
	{
		FString Why;
		return FGFMineable::CanMineHit(Hit, Why) && FGFMineable::SlotFromHit(Hit) == 1 && Hit.ImpactNormal.Z > -0.25f;
	}

	static bool ReadTowns(UWorld* World, TArray<FTown>& Out)
	{
		Out.Reset();
		const TArray<FGFTerritoryFaction>* Layout = nullptr;
		FGFTerritoryCurve Curve;
		if (const UGFTerritorySubsystem* Live = UGFTerritorySubsystem::Get(World))
		{
			if (Live->FactionLayout.Num() > 0)
			{
				Layout = &Live->FactionLayout;
				Curve = Live->Curve;
			}
		}
		if (!Layout)
		{
			const UGFTerritorySubsystem* Defaults = GetDefault<UGFTerritorySubsystem>();
			if (!Defaults || Defaults->FactionLayout.Num() == 0)
			{
				return false;
			}
			Layout = &Defaults->FactionLayout;
			Curve = Defaults->Curve;
		}
		for (const FName& Id : { Rime, Verdant, Cinderwrights })
		{
			const FGFTerritoryFaction* Found = Layout->FindByPredicate([Id](const FGFTerritoryFaction& Faction) { return Faction.Id == Id; });
			if (!Found)
			{
				return false;
			}
			FTown Town;
			Town.Id = Id;
			Town.Center = Found->Center;
			Town.Camp = FGFTownsfolkRules::CampRadius(*Found, Curve, 1);
			if (Town.Camp < 100.f)
			{
				return false;
			}
			Out.Add(Town);
		}
		return Out.Num() == 3;
	}

	static const FTown* FindTown(const TArray<FTown>& Towns, FName Id)
	{
		return Towns.FindByPredicate([Id](const FTown& Town) { return Town.Id == Id; });
	}

	static void Still(ACharacter* Body)
	{
		if (!Body)
		{
			return;
		}
		if (UCharacterMovementComponent* Move = Body->GetCharacterMovement())
		{
			Move->GravityScale = 0.f;
			Move->StopMovementImmediately();
			Move->DisableMovement();
		}
	}

	static void Plant(AGFCharacter* Body, const FVector& At)
	{
		if (!Body)
		{
			return;
		}
		Body->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Still(Body);
	}

	static void ParkBody(AGFCharacter* Body)
	{
		Plant(Body, FVector(0.f, 0.f, -500000.f));
	}

	static int32 BagCount(const AGFPlayerState* PS, FName ItemId)
	{
		return (PS && PS->Inventory) ? PS->Inventory->GetCount(ItemId) : 0;
	}

	static int32 StackCount(const TArray<FGFItemStack>& Stacks, FName ItemId)
	{
		int32 Count = 0;
		for (const FGFItemStack& Stack : Stacks)
		{
			if (Stack.ItemId == ItemId)
			{
				Count += Stack.Count;
			}
		}
		return Count;
	}

	static int32 PostsOf(UWorld* World, FName Town, EGFElderKind Kind, const FString& SubjectKey)
	{
		const UGFElderBookSubsystem* Books = UGFElderBookSubsystem::Get(World);
		if (!Books)
		{
			return 0;
		}
		int32 Count = 0;
		for (const FGFElderPost& Post : Books->GetBook().ChannelPosts(FGFElderBook::TownChannel(Town)))
		{
			if (Post.Kind == Kind && (SubjectKey.IsEmpty() || Post.SubjectKey == SubjectKey))
			{
				++Count;
			}
		}
		return Count;
	}

	static bool SpawnPlayer(FAutomationTestBase& T, FHooks& Hooks, const FString& Key, FPlayer& Out)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Out.PS = Hooks.World->SpawnActor<AGFMagicPlayerState>(AGFMagicPlayerState::StaticClass(), FTransform::Identity, Params);
		Out.PC = Hooks.World->SpawnActor<AGFPlayerController>(AGFPlayerController::StaticClass(), FTransform::Identity, Params);
		Out.Pawn = Hooks.World->SpawnActor<AGFCharacter>(AGFCharacter::StaticClass(), FTransform(FVector(0.f, 0.f, -500000.f)), Params);
		Out.Key = Key;
		if (!T.TestNotNull(TEXT("Magic player"), Out.PS) || !T.TestNotNull(TEXT("Controller"), Out.PC) || !T.TestNotNull(TEXT("Body"), Out.Pawn) || !T.TestNotNull(TEXT("Magic"), Out.PS ? Out.PS->Magic.Get() : nullptr))
		{
			return false;
		}
		Out.PS->PlayerKey = Key;
		Out.PS->SetPlayerId(4100 + Hooks.GS->PlayerArray.Num());
		Out.PS->SetPlayerName(Key);
		Out.PS->bCharacterReady = false;
		if (Hooks.GS->PlayerArray.Find(Out.PS) == INDEX_NONE)
		{
			Hooks.GS->AddPlayerState(Out.PS);
		}
		Still(Out.Pawn);
		return true;
	}

	static void Link(FPlayer& Player)
	{
		if (!Player.PC || !Player.PS || !Player.Pawn)
		{
			return;
		}
		Player.PC->PlayerState = Player.PS;
		Player.PS->SetOwner(Player.PC);
		Player.Pawn->SetPlayerState(Player.PS);
		Player.PC->Possess(Player.Pawn);
		if (Player.PS->Magic)
		{
			Player.PS->Magic->BindAvatar(Player.Pawn);
		}
		Still(Player.Pawn);
	}

	static bool BindExisting(FAutomationTestBase& T, AGFPlayerState* PS, FPlayer& Out)
	{
		Out.PS = Cast<AGFMagicPlayerState>(PS);
		Out.Pawn = Cast<AGFCharacter>(PS ? PS->GetPawn() : nullptr);
		Out.PC = Cast<AGFPlayerController>(Out.Pawn ? Out.Pawn->GetController() : nullptr);
		Out.Key = PS ? PS->PlayerKey : FString();
		if (!T.TestNotNull(TEXT("Live magic player"), Out.PS)
			|| !T.TestNotNull(TEXT("Live body"), Out.Pawn)
			|| !T.TestNotNull(TEXT("Live controller"), Out.PC)
			|| !T.TestNotNull(TEXT("Live magic"), Out.PS ? Out.PS->Magic.Get() : nullptr)
			|| !T.TestFalse(TEXT("Live key"), Out.Key.IsEmpty()))
		{
			return false;
		}
		Still(Out.Pawn);
		return true;
	}

	static void ParkStrays(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			AGFDino* Dino = *It;
			if (!Dino)
			{
				continue;
			}
			Dino->SetSpecies(StrayId);
			Dino->SetActorLocation(FVector(0.f, 0.f, -500000.f), false, nullptr, ETeleportType::TeleportPhysics);
			Still(Dino);
		}
	}

	static UStaticMesh* LoadPen()
	{
		TArray<FGFMineableEntry> All;
		FString Err;
		if (!FGFMineable::LoadTownWhitelist(All, Err))
		{
			return nullptr;
		}
		const FGFMineableEntry* Entry = All.FindByPredicate([](const FGFMineableEntry& E) { return E.Asset == FName(PenAsset); });
		if (!Entry)
		{
			return nullptr;
		}
		const FString Name = FPackageName::GetLongPackageAssetName(Entry->Path);
		return LoadObject<UStaticMesh>(nullptr, *(Entry->Path + TEXT(".") + Name));
	}

	static UStaticMeshComponent* PlaceCrystal(UWorld* World, const FTown& Town, FStand& OutStand)
	{
		UStaticMesh* Mesh = LoadPen();
		if (!World || !Mesh)
		{
			return nullptr;
		}
		const FVector Where = Town.Center + FVector(0.f, 0.f, 1600.f);
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Where, FRotator::ZeroRotator);
		if (!Actor)
		{
			return nullptr;
		}
		UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->RecreatePhysicsState();
		for (int32 Attempt = 0; Attempt < 5; ++Attempt)
		{
			Comp->UpdateBounds();
			const float Radius = Comp->Bounds.SphereRadius;
			const float Limit = Town.Camp * 0.12f;
			if (Radius > Limit && Radius > 1.f)
			{
				const float Scale = Limit / Radius;
				Comp->SetWorldScale3D(Comp->GetComponentScale() * Scale);
				Comp->RecreatePhysicsState();
			}
			OutStand = StandOutside(World, Comp, 120.f, CrystalFace);
			if (OutStand.bOk
				&& FVector::Dist2D(OutStand.Impact, Town.Center) < Town.Camp * 0.85f
				&& FVector::Dist2D(OutStand.At, Town.Center) < Town.Camp * 0.85f)
			{
				return Comp;
			}
			Comp->SetWorldScale3D(Comp->GetComponentScale() * 0.5f);
			Comp->RecreatePhysicsState();
		}
		return Comp;
	}

	static AGFVillager* SpawnFolk(UWorld* World, const FTown& Town, EGFTownsfolkRole Role, TSubclassOf<AGFVillager> Class, const FVector& At)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFVillager* Folk = World->SpawnActor<AGFVillager>(Class, At, FRotator::ZeroRotator, Params);
		if (!Folk)
		{
			return nullptr;
		}
		FGFTownsfolkSpawnParams Spawn;
		Spawn.TownId = Town.Id;
		Spawn.Role = Role;
		Spawn.TownCenter = FVector(Town.Center.X, Town.Center.Y, At.Z);
		Spawn.TownRadius = 8000.f;
		Spawn.WanderRadius = 0.f;
		Spawn.WalkSpeed = 220.f;
		Spawn.TalkSeconds = 4.f;
		Spawn.Reach = FGFTownsfolkRules::InteractReachCm;
		Spawn.IdleMin = 30.f;
		Spawn.IdleMax = 30.f;
		Spawn.StopDistance = 80.f;
		Folk->InitTownsfolk(Spawn);
		return Folk;
	}

	static bool CanSee(UWorld* World, const AActor* From, const FVector& To)
	{
		if (!World || !From)
		{
			return false;
		}
		const FVector Start = From->GetActorLocation() + FVector(0.f, 0.f, 80.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GFQuestE2ESight), false, From);
		if (!World->LineTraceSingleByChannel(Hit, Start, To, ECC_Visibility, Params))
		{
			return true;
		}
		return Hit.Distance + 30.f >= FVector::Dist(Start, To);
	}

	static bool PlaceWitness(UWorld* World, const FTown& Town, const FStand& Stand, AGFVillager*& Out)
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Stand.Normal).GetSafeNormal();
		const FVector Spots[] = {
			Stand.At,
			Stand.Impact + Stand.Normal * 220.f,
			Stand.Impact + Stand.Normal * 220.f + Right * 140.f,
			Stand.Impact + Stand.Normal * 180.f + FVector(0.f, 0.f, 160.f),
			Stand.Impact + Stand.Normal * 400.f
		};
		for (const FVector& At : Spots)
		{
			if (FVector::Dist2D(At, Town.Center) > Town.Camp * 0.9f || FVector::Dist(At, Stand.Impact) > 1400.f)
			{
				continue;
			}
			AGFVillager* Folk = SpawnFolk(World, Town, EGFTownsfolkRole::Villager, AGFVillager::StaticClass(), At);
			if (Folk && CanSee(World, Folk, Stand.Impact))
			{
				Out = Folk;
				return true;
			}
			if (Folk)
			{
				Folk->Destroy();
			}
		}
		return false;
	}

	static bool Mine(FHooks& Hooks, FPlayer& Player, UStaticMeshComponent* Crystal, const FStand& Stand)
	{
		if (!Player.PS || !Player.PS->Magic || !Hooks.Magic)
		{
			return false;
		}
		ParkBody(Player.Pawn);
		if (Player.PC)
		{
			Player.PC->UnPossess();
		}
		Player.PS->Magic->BindAvatar(nullptr);
		Player.PS->Magic->SetManualOrigin(Stand.At);
		Player.PS->bCharacterReady = true;
		for (int32 Step = 0; Step < 12 && !Hooks.Magic->IsTownNodeDepleted(Crystal); ++Step)
		{
			Player.PS->Magic->SetManualOrigin(Stand.At);
			Player.PS->Magic->SetAim(Stand.At, Stand.Impact);
			Player.PS->Magic->SetFieldHeld(true);
			Hooks.Magic->Advance(0.4f);
		}
		Player.PS->Magic->SetFieldHeld(false);
		Link(Player);
		return Hooks.Magic->IsTownNodeDepleted(Crystal);
	}

	static void PickSeeds(TArray<int32>& Skin, TArray<int32>& Bare)
	{
		Skin.Reset();
		Bare.Reset();
		for (int32 Seed = 1; Seed < 5000 && (Skin.Num() < 2 || Bare.Num() < 3); ++Seed)
		{
			const int32 Skins = StackCount(FGFDinoKill::Roll(NipperId, Seed), SkinNipperId);
			if (Skins > 0 && Skin.Num() < 2)
			{
				Skin.Add(Seed);
			}
			else if (Skins == 0 && Bare.Num() < 3)
			{
				Bare.Add(Seed);
			}
		}
	}

	static AGFDino* SpawnNipper(UWorld* World, const FVector& At, int32 Seed)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFDino* Dino = World->SpawnActor<AGFDino>(AGFDino::StaticClass(), At, FRotator::ZeroRotator, Params);
		if (!Dino)
		{
			return nullptr;
		}
		Dino->SetSpecies(NipperId);
		Dino->SetDropSeed(Seed);
		Dino->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
		Still(Dino);
		return Dino;
	}

	static int32 HostileAt(UWorld* World, const FString& Key)
	{
		int32 Count = 0;
		if (!World)
		{
			return 0;
		}
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			AGFDino* Dino = *It;
			if (!Dino || !Dino->Taming || Dino->GetLife() != EGFDinoLife::Alive)
			{
				continue;
			}
			if (Dino->Taming->GetTameState() == EGFTameState::Hostile && Dino->Taming->GetTargetKey() == Key)
			{
				++Count;
			}
		}
		return Count;
	}

	static void KillAll(const TArray<AGFDino*>& Dinos, FPlayer& Player)
	{
		for (AGFDino* Dino : Dinos)
		{
			if (Dino && Dino->GetLife() == EGFDinoLife::Alive)
			{
				UGameplayStatics::ApplyDamage(Dino, 1000.f, Player.PC, Player.Pawn, nullptr);
			}
		}
	}

	static void KillHostile(UWorld* World, const FString& Key, FPlayer& Player)
	{
		TArray<AGFDino*> Marked;
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			AGFDino* Dino = *It;
			if (Dino && Dino->Taming && Dino->GetLife() == EGFDinoLife::Alive && Dino->Taming->GetTameState() == EGFTameState::Hostile && Dino->Taming->GetTargetKey() == Key)
			{
				Marked.Add(Dino);
			}
		}
		KillAll(Marked, Player);
	}

	static bool OpenElder(FAutomationTestBase& T, FPlayer& Player, AGFElder* Elder)
	{
		if (!Player.Pawn || !Player.PC || !Elder)
		{
			T.AddError(TEXT("Elder talk is missing a body"));
			return false;
		}
		Plant(Player.Pawn, Elder->GetActorLocation() + FVector(120.f, 0.f, 0.f));
		Player.Pawn->RequestInteractWith(Elder);
		if (!Player.PC->IsElderMenuSessionOpen())
		{
			T.AddError(TEXT("The elder menu did not open"));
			return false;
		}
		return true;
	}

	static AGFElder* SpawnElder(UWorld* World, const FTown& Town)
	{
		const FVector At = Town.Center + FVector(FMath::Min(900.f, Town.Camp * 0.55f), 0.f, 1600.f);
		return Cast<AGFElder>(SpawnFolk(World, Town, EGFTownsfolkRole::Elder, AGFElder::StaticClass(), At));
	}

	static int32 ShowSeed(const FString& Key)
	{
		const FGFFashionTheme* Theme = FGFFashionCatalog::FindTheme(GardenTheme);
		if (!Theme)
		{
			return INDEX_NONE;
		}
		FGFOutfit Look;
		for (const TCHAR* Id : { TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown") })
		{
			if (!Look.Equip(Id))
			{
				return INDEX_NONE;
			}
		}
		for (int32 Seed = 1; Seed <= 64; ++Seed)
		{
			const FGFRunwayScore Score = FGFRunwayJudge::Judge(Look, *Theme, {}, Seed, Key);
			if (Score.Tier == EGFRunwayTier::A || Score.Tier == EGFRunwayTier::S)
			{
				return Seed;
			}
		}
		return INDEX_NONE;
	}

	static FGFOutfit GardenLook()
	{
		FGFOutfit Look;
		Look.Equip(TEXT("FernBraid"));
		Look.Equip(TEXT("PetalBlouse"));
		Look.Equip(TEXT("MeadowSkirt"));
		Look.Equip(TEXT("BloomSandals"));
		Look.Equip(TEXT("DaisyCrown"));
		return Look;
	}

	static bool BreakTown(FAutomationTestBase& T, FHooks& Hooks, FPlayer& Player, const FTown& Town, bool bSeedNippers, FScene& Scene)
	{
		Scene = FScene();
		Scene.Town = Town;
		FStand Stand;
		Scene.Crystal = PlaceCrystal(Hooks.World, Town, Stand);
		if (!T.TestNotNull(TEXT("Crystal mesh"), Scene.Crystal) || !T.TestTrue(TEXT("Crystal face inside the camp"), Stand.bOk))
		{
			return false;
		}
		ParkBody(Player.Pawn);
		if (!T.TestTrue(TEXT("A villager can see the crystal"), PlaceWitness(Hooks.World, Town, Stand, Scene.Witness)))
		{
			return false;
		}
		const FVector Rim = FVector(Town.Center.X, Town.Center.Y, Stand.At.Z) + FVector(10000.f, 0.f, 0.f);
		Scene.Bystander = SpawnFolk(Hooks.World, Town, EGFTownsfolkRole::Villager, AGFVillager::StaticClass(), Rim);
		Scene.Elder = SpawnElder(Hooks.World, Town);
		if (!T.TestNotNull(TEXT("Witness"), Scene.Witness) || !T.TestNotNull(TEXT("Bystander"), Scene.Bystander) || !T.TestNotNull(TEXT("Elder"), Scene.Elder))
		{
			return false;
		}
		const int32 CrystalBefore = BagCount(Player.PS, CrystalId);
		Player.PS->bCharacterReady = true;
		if (!T.TestTrue(TEXT("Sparkcone spends the crystal"), Mine(Hooks, Player, Scene.Crystal, Stand)))
		{
			T.AddError(FString::Printf(TEXT("Crystal remaining %.1f"), Hooks.Magic->GetTownNodeRemaining(Scene.Crystal)));
			return false;
		}
		if (!T.TestTrue(TEXT("The break was witnessed"), Player.PS->QuestStep == EGFQuestStep::Escape))
		{
			T.AddError(FString::Printf(TEXT("Step after the break is %d"), static_cast<int32>(Player.PS->QuestStep)));
			return false;
		}
		if (!T.TestTrue(TEXT("The witness warns"), Scene.Witness->Behavior == EGFTownsfolkBehavior::Warn))
		{
			return false;
		}
		if (!T.TestEqual(TEXT("Warn shout"), Scene.Witness->GetShout(), FString(FGFTownsfolkRules::WarnShout())))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("The far villager did not see it"), Scene.Bystander->Behavior == EGFTownsfolkBehavior::Idle))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Crystal landed in the bag"), BagCount(Player.PS, CrystalId) >= CrystalBefore + 1))
		{
			return false;
		}

		if (bSeedNippers)
		{
			TArray<int32> SkinSeeds;
			TArray<int32> BareSeeds;
			PickSeeds(SkinSeeds, BareSeeds);
			if (!T.TestEqual(TEXT("Two skin seeds"), SkinSeeds.Num(), 2) || !T.TestEqual(TEXT("Three bare seeds"), BareSeeds.Num(), 3))
			{
				return false;
			}
			TArray<int32> Seeds = SkinSeeds;
			Seeds.Append(BareSeeds);
			for (int32 Index = 0; Index < Seeds.Num(); ++Index)
			{
				const FVector At = FVector(Town.Center.X, Town.Center.Y, Stand.At.Z) + FVector(280.f, 70.f * Index, 0.f);
				AGFDino* Dino = SpawnNipper(Hooks.World, At, Seeds[Index]);
				if (!Dino)
				{
					T.AddError(TEXT("Nipper did not spawn"));
					return false;
				}
				Scene.Quarry.Add(Dino);
			}
		}

		bool bFled = false;
		bool bHired = false;
		for (int32 Tick = 0; Tick < 100; ++Tick)
		{
			Scene.Witness->Tick(0.25f);
			if (Scene.Witness->Behavior == EGFTownsfolkBehavior::Flee)
			{
				bFled = true;
			}
			const int32 Hostile = HostileAt(Hooks.World, Player.Key);
			if (Hostile > 0)
			{
				bHired = true;
				if (!bSeedNippers || Hostile == 3)
				{
					break;
				}
			}
		}
		if (!T.TestTrue(TEXT("The villager flees"), bFled) || !T.TestTrue(TEXT("The elder hires"), bHired))
		{
			return false;
		}
		if (bSeedNippers && !T.TestEqual(TEXT("Three hired nippers"), HostileAt(Hooks.World, Player.Key), 3))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Theft post"), PostsOf(Hooks.World, Town.Id, EGFElderKind::Theft, Player.Key) >= 1))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Hire post"), PostsOf(Hooks.World, Town.Id, EGFElderKind::Hire, Player.Key) >= 1))
		{
			return false;
		}

		const int32 ScalesBefore = BagCount(Player.PS, ScalesId);
		const int32 SkinBefore = BagCount(Player.PS, SkinNipperId);
		if (!T.TestTrue(TEXT("Pawn is the tamer"), Player.PS->GetPawn() == Player.Pawn))
		{
			return false;
		}
		if (bSeedNippers)
		{
			KillAll(Scene.Quarry, Player);
			if (!T.TestTrue(TEXT("Five kills paid 10 Scales"), BagCount(Player.PS, ScalesId) >= ScalesBefore + 10))
			{
				return false;
			}
			if (!T.TestTrue(TEXT("Two town skins dropped"), BagCount(Player.PS, SkinNipperId) >= SkinBefore + 2))
			{
				return false;
			}
		}
		else
		{
			KillHostile(Hooks.World, Player.Key, Player);
		}
		Hooks.Theft->Tick(0.5f);
		if (Player.PS->QuestStep != EGFQuestStep::TalkToElder)
		{
			Hooks.Theft->Tick(0.5f);
		}
		if (!T.TestTrue(TEXT("Heat clears to the elder"), Player.PS->QuestStep == EGFQuestStep::TalkToElder))
		{
			T.AddError(FString::Printf(TEXT("Step after the hunt is %d"), static_cast<int32>(Player.PS->QuestStep)));
			return false;
		}
		if (!OpenElder(T, Player, Scene.Elder))
		{
			return false;
		}
		Player.PC->RequestElderAccept();
		if (!T.TestTrue(TEXT("Errand accepted"), Player.PS->QuestStep == EGFQuestStep::HuntHide))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Initiate"), Player.PS->FactionRank == EGFFactionRank::Initiate))
		{
			return false;
		}
		if (!T.TestEqual(TEXT("Joined the town"), Player.PS->FactionId, Town.Id))
		{
			return false;
		}
		return true;
	}

	static bool HandIn(FAutomationTestBase& T, FPlayer& Player, AGFElder* Elder)
	{
		if (!Player.PC->IsElderMenuSessionOpen() && !OpenElder(T, Player, Elder))
		{
			return false;
		}
		Player.PC->RequestElderHandIn();
		return true;
	}

	static bool PlayToMember(FAutomationTestBase& T, FHooks& Hooks, FPlayer& Player, const FTown& Town, FScene& Scene)
	{
		if (!BreakTown(T, Hooks, Player, Town, true, Scene))
		{
			return false;
		}
		const int32 MatsBefore = BagCount(Player.PS, ScalesId) + BagCount(Player.PS, HideId);
		if (!HandIn(T, Player, Scene.Elder))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Hide turn in reaches skins"), Player.PS->QuestStep == EGFQuestStep::HuntSkins))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Member"), Player.PS->FactionRank == EGFFactionRank::Member))
		{
			return false;
		}
		const int32 MatsAfter = BagCount(Player.PS, ScalesId) + BagCount(Player.PS, HideId);
		if (!T.TestTrue(TEXT("Scales left for the atelier"), BagCount(Player.PS, ScalesId) >= 3) || !T.TestEqual(TEXT("Turn in took 6"), MatsAfter, MatsBefore - 6))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Skins still held"), BagCount(Player.PS, SkinNipperId) >= 2))
		{
			return false;
		}
		Player.PS->Inventory->AddItem(ClayId, 2);
		return true;
	}

	static bool Finish(FAutomationTestBase& T, FHooks& Hooks, FPlayer& Player, AGFElder* Elder)
	{
		if (Player.PS->QuestStep == EGFQuestStep::HuntSkins)
		{
			if (!Elder)
			{
				TArray<FTown> Towns;
				if (!ReadTowns(Hooks.World, Towns))
				{
					T.AddError(TEXT("No town anchors on the reloaded world"));
					return false;
				}
				const FTown* Town = FindTown(Towns, Player.PS->FactionId);
				Elder = Town ? SpawnElder(Hooks.World, *Town) : nullptr;
			}
			if (!T.TestNotNull(TEXT("Elder for the skins"), Elder) || !HandIn(T, Player, Elder))
			{
				return false;
			}
		}
		if (!T.TestTrue(TEXT("Skins turn in reaches the atelier"), Player.PS->QuestStep == EGFQuestStep::CraftLook))
		{
			T.AddError(FString::Printf(TEXT("Step before craft is %d"), static_cast<int32>(Player.PS->QuestStep)));
			return false;
		}
		if (!T.TestTrue(TEXT("Champion"), Player.PS->FactionRank == EGFFactionRank::Champion))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Scales for dirtachi"), BagCount(Player.PS, ScalesId) >= 3)
			|| !T.TestTrue(TEXT("Clay for dirtachi"), BagCount(Player.PS, ClayId) >= 2)
			|| !T.TestTrue(TEXT("Crystal for dirtachi"), BagCount(Player.PS, CrystalId) >= 1))
		{
			return false;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFAtelierStation* Atelier = Hooks.World->SpawnActor<AGFAtelierStation>(AGFAtelierStation::StaticClass(), Player.Pawn->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!T.TestNotNull(TEXT("Atelier"), Atelier))
		{
			return false;
		}
		Plant(Player.Pawn, Atelier->GetActorLocation());
		const EGFCraftResult Crafted = Atelier->TryCraft(Player.Pawn, RecipeId);
		if (!T.TestTrue(TEXT("Atelier sewed dirtachi"), Crafted == EGFCraftResult::Crafted))
		{
			T.AddError(Atelier->LastPrompt.IsEmpty() ? FString(TEXT("Craft failed")) : Atelier->LastPrompt);
			return false;
		}
		if (!T.TestEqual(TEXT("Craft counted"), Player.PS->QuestCount, 1) || !T.TestTrue(TEXT("dirtachi is in the bag"), BagCount(Player.PS, PieceId) >= 1))
		{
			return false;
		}

		AGFRunwayStage* Stage = Hooks.World->SpawnActor<AGFRunwayStage>(AGFRunwayStage::StaticClass(), FTransform::Identity, Params);
		AGFRunwaySite* Site = Hooks.World->SpawnActor<AGFRunwaySite>(AGFRunwaySite::StaticClass(), FTransform::Identity, Params);
		if (!T.TestNotNull(TEXT("Stage"), Stage) || !T.TestNotNull(TEXT("Runway"), Site))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Wore the atelier piece"), Site->TryEquipPiece(Player.Pawn, PieceId)))
		{
			T.AddError(Site->LastPrompt.IsEmpty() ? FString(TEXT("Wear failed")) : Site->LastPrompt);
			return false;
		}
		if (!T.TestTrue(TEXT("Wear opens the show step"), Player.PS->QuestStep == EGFQuestStep::WinShow))
		{
			return false;
		}

		const int32 Seed = ShowSeed(Player.Key);
		if (!T.TestTrue(TEXT("A garden seed scores tier A or S"), Seed != INDEX_NONE))
		{
			return false;
		}
		Stage->WalkSeconds = 0.05f;
		Stage->JudgingSeconds = 0.05f;
		const EGFRunwayRequestResult Started = Stage->StartShowFor(GardenTheme, { Player.Key }, Seed);
		if (!T.TestTrue(TEXT("Show started"), Started == EGFRunwayRequestResult::Ok))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Entered"), Player.PS->QuestStep == EGFQuestStep::WinShow && Player.PS->QuestCount == 1))
		{
			return false;
		}
		const EGFRunwayRequestResult Dressed = Stage->HandleSetOutfit(Player.Key, GardenLook());
		const EGFRunwayRequestResult Ready = Stage->HandleSetReady(Player.Key, true);
		if (!T.TestTrue(TEXT("Garden look accepted"), Dressed == EGFRunwayRequestResult::Ok) || !T.TestTrue(TEXT("Ready"), Ready == EGFRunwayRequestResult::Ok))
		{
			return false;
		}
		Stage->Step(1.f);
		const FGFRunwayContestant* Contestant = Stage->FindContestant(Player.Key);
		if (!T.TestTrue(TEXT("Results"), Stage->GetPhase() == EGFRunwayPhase::Results) || !T.TestNotNull(TEXT("Contestant"), Contestant))
		{
			return false;
		}
		if (!T.TestEqual(TEXT("First place"), Contestant->Rank, 1))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Tier A or better"), Contestant->Score.Tier == EGFRunwayTier::A || Contestant->Score.Tier == EGFRunwayTier::S))
		{
			T.AddError(FString::Printf(TEXT("Tier %d"), static_cast<int32>(Contestant->Score.Tier)));
			return false;
		}
		if (!T.TestTrue(TEXT("Errand done"), Player.PS->QuestStep == EGFQuestStep::Done))
		{
			T.AddError(FString::Printf(TEXT("Step after the show is %d"), static_cast<int32>(Player.PS->QuestStep)));
			return false;
		}
		if (!T.TestTrue(TEXT("Leader rank"), Player.PS->FactionRank == EGFFactionRank::Leader))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Holds the seat"), Hooks.GS->IsFactionLeader(Player.PS)))
		{
			return false;
		}
		if (!T.TestTrue(TEXT("Show win post"), PostsOf(Hooks.World, Player.PS->FactionId, EGFElderKind::ShowWin, Player.Key) >= 1))
		{
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestLineE2ETest, "GFGame.Quest.EndToEnd.FirstQuestLine", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestLineE2ETest::RunTest(const FString&)
{
	using namespace GFQuestE2E;
	FSession Session;
	FHooks Hooks;
	if (!Session.Open(*this, TEXT("GFQuestLineE2E"), Hooks))
	{
		return false;
	}
	TArray<FTown> Towns;
	if (!TestTrue(TEXT("Rime, Verdant, and Cinderwrights"), ReadTowns(Hooks.World, Towns)))
	{
		return false;
	}
	const FTown* Town = FindTown(Towns, Rime);
	if (!TestNotNull(TEXT("Rime anchor"), Town))
	{
		return false;
	}
	FPlayer Player;
	if (!SpawnPlayer(*this, Hooks, TEXT("RimePlayer"), Player))
	{
		return false;
	}
	Link(Player);
	FScene Scene;
	if (!PlayToMember(*this, Hooks, Player, *Town, Scene))
	{
		return false;
	}
	return Finish(*this, Hooks, Player, Scene.Elder);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestLineSaveE2ETest, "GFGame.Quest.EndToEnd.SaveMidway", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestLineSaveE2ETest::RunTest(const FString&)
{
	using namespace GFQuestE2E;
	FSession First;
	FHooks Live;
	if (!First.Open(*this, TEXT("GFQuestLineSaveA"), Live))
	{
		return false;
	}
	TArray<FTown> Towns;
	if (!TestTrue(TEXT("Anchors before the save"), ReadTowns(Live.World, Towns)))
	{
		return false;
	}
	const FTown* Town = FindTown(Towns, Rime);
	if (!TestNotNull(TEXT("Rime anchor"), Town))
	{
		return false;
	}
	FPlayer Player;
	if (!SpawnPlayer(*this, Live, TEXT("RimePlayer"), Player))
	{
		return false;
	}
	Link(Player);
	FScene Scene;
	if (!PlayToMember(*this, Live, Player, *Town, Scene))
	{
		return false;
	}
	Player.PS->bCharacterReady = true;
	UGFSaveGame* Save = UGFSaveSubsystem::CaptureWorld(Live.World);
	if (!TestNotNull(TEXT("Save"), Save))
	{
		return false;
	}
	Save->AddToRoot();
	if (!HandIn(*this, Player, Scene.Elder) || !TestTrue(TEXT("The live world moved on after the save"), Player.PS->QuestStep == EGFQuestStep::CraftLook))
	{
		Save->RemoveFromRoot();
		return false;
	}

	FSession Second;
	FHooks Reloaded;
	if (!Second.Open(*this, TEXT("GFQuestLineSaveB"), Reloaded) || !TestTrue(TEXT("Save applies"), UGFSaveSubsystem::ApplyToWorld(Reloaded.World, Save)))
	{
		Save->RemoveFromRoot();
		return false;
	}
	Save->RemoveFromRoot();
	FPlayer Again;
	if (!SpawnPlayer(*this, Reloaded, TEXT("RimePlayer"), Again))
	{
		return false;
	}
	Again.PS->ServerAcceptCharacter(FGFCharacterData());
	if (!TestTrue(TEXT("Reload is still on skins"), Again.PS->QuestStep == EGFQuestStep::HuntSkins))
	{
		return false;
	}
	if (!TestTrue(TEXT("Reload is still a member"), Again.PS->FactionRank == EGFFactionRank::Member))
	{
		return false;
	}
	if (!TestEqual(TEXT("Reload kept Rime"), Again.PS->FactionId, Rime))
	{
		return false;
	}
	if (!TestTrue(TEXT("Reload kept Scales"), BagCount(Again.PS, ScalesId) >= 3)
		|| !TestTrue(TEXT("Reload kept skins"), BagCount(Again.PS, SkinNipperId) >= 2)
		|| !TestTrue(TEXT("Reload kept Clay"), BagCount(Again.PS, ClayId) >= 2)
		|| !TestTrue(TEXT("Reload kept Crystal"), BagCount(Again.PS, CrystalId) >= 1))
	{
		return false;
	}
	Link(Again);
	return Finish(*this, Reloaded, Again, nullptr);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFQuestLineNetE2ETest, "GFGame.Net.Quest.EndToEnd.TwoPlayers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGFQuestLineNetE2ETest::RunTest(const FString&)
{
	using namespace GFNetTest;
	using namespace GFQuestE2E;
	FCtxRef Ctx = Start(this, 2, 45.f);
	TSharedRef<FString> HostKey = MakeShared<FString>();
	TSharedRef<FString> GuestKey = MakeShared<FString>();

	Do(Ctx, [this, Ctx, HostKey, GuestKey]()
	{
		UWorld* Server = ServerWorld();
		FHooks Hooks;
		if (!Server || !Hooks.Bind(*this, Server))
		{
			Ctx->bFailed = true;
			return;
		}
		ParkStrays(Server);
		TArray<FTown> Towns;
		if (!TestTrue(TEXT("Net anchors"), ReadTowns(Server, Towns)))
		{
			Ctx->bFailed = true;
			return;
		}
		const FTown* RimeTown = FindTown(Towns, Rime);
		const FTown* VerdantTown = FindTown(Towns, Verdant);
		AGFPlayerState* HostPS = LocalPlayerState(Server);
		AGFPlayerState* GuestPS = nullptr;
		if (ClientWorlds().Num() > 0)
		{
			const AGFPlayerState* ClientPS = LocalPlayerState(ClientWorlds()[0]);
			GuestPS = ClientPS ? PlayerStateByKey(Server, ClientPS->PlayerKey) : nullptr;
		}
		FPlayer Host;
		FPlayer Guest;
		if (!RimeTown || !VerdantTown || !BindExisting(*this, HostPS, Host) || !BindExisting(*this, GuestPS, Guest))
		{
			Ctx->bFailed = true;
			return;
		}
		*HostKey = Host.Key;
		*GuestKey = Guest.Key;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AGFPlayerState* Founder = Server->SpawnActor<AGFPlayerState>(AGFPlayerState::StaticClass(), FTransform::Identity, Params);
		if (!TestNotNull(TEXT("Verdant founder"), Founder))
		{
			Ctx->bFailed = true;
			return;
		}
		Founder->PlayerKey = TEXT("VerdantFounder");
		Founder->SetPlayerName(TEXT("VerdantFounder"));
		Founder->SetPlayerId(9100);
		if (Hooks.GS->PlayerArray.Find(Founder) == INDEX_NONE)
		{
			Hooks.GS->AddPlayerState(Founder);
		}
		if (!TestEqual(TEXT("Founder takes the empty Verdant seat"), Hooks.GS->ServerClaimFaction(Founder, Verdant), FName(TEXT("Ok"))))
		{
			Ctx->bFailed = true;
			return;
		}

		FScene HostScene;
		if (!PlayToMember(*this, Hooks, Host, *RimeTown, HostScene) || !Finish(*this, Hooks, Host, HostScene.Elder))
		{
			Ctx->bFailed = true;
			return;
		}
		FScene GuestScene;
		if (!BreakTown(*this, Hooks, Guest, *VerdantTown, false, GuestScene))
		{
			Ctx->bFailed = true;
			return;
		}
		if (!TestTrue(TEXT("Host finished"), Host.PS->QuestStep == EGFQuestStep::Done)
			|| !TestTrue(TEXT("Host leads"), Host.PS->FactionRank == EGFFactionRank::Leader)
			|| !TestTrue(TEXT("Host holds Rime"), Hooks.GS->IsFactionLeader(Host.PS))
			|| !TestEqual(TEXT("Rime seat"), Hooks.GS->FactionLeaderOf(Rime), *HostKey)
			|| !TestEqual(TEXT("Verdant seat stays with the founder"), Hooks.GS->FactionLeaderOf(Verdant), FString(TEXT("VerdantFounder")))
			|| !TestTrue(TEXT("Guest is on the errand"), Guest.PS->QuestStep == EGFQuestStep::HuntHide)
			|| !TestTrue(TEXT("Guest is an initiate"), Guest.PS->FactionRank == EGFFactionRank::Initiate)
			|| !TestEqual(TEXT("Guest joined Verdant"), Guest.PS->FactionId, Verdant)
			|| !TestFalse(TEXT("Guest does not lead Rime"), Hooks.GS->FactionLeaderOf(Rime) == *GuestKey)
			|| !TestFalse(TEXT("Guest does not lead Verdant"), Hooks.GS->IsFactionLeader(Guest.PS)))
		{
			Ctx->bFailed = true;
		}
	});

	WaitUntil(Ctx, TEXT("client sees both towns"), 15.f, [HostKey, GuestKey]()
	{
		if (ClientWorlds().Num() == 0)
		{
			return false;
		}
		UWorld* Client = ClientWorlds()[0];
		AGFGameState* GS = GameState(Client);
		AGFPlayerState* Host = PlayerStateByKey(Client, *HostKey);
		AGFPlayerState* Guest = LocalPlayerState(Client);
		if (!GS || !Host || !Guest)
		{
			return false;
		}
		return Host->QuestStep == EGFQuestStep::Done
			&& Host->FactionRank == EGFFactionRank::Leader
			&& Host->FactionId == Rime
			&& Guest->QuestStep == EGFQuestStep::HuntHide
			&& Guest->FactionRank == EGFFactionRank::Initiate
			&& Guest->FactionId == Verdant
			&& GS->FactionLeaderOf(Rime) == *HostKey
			&& GS->FactionLeaderOf(Verdant) == FString(TEXT("VerdantFounder"));
	});
	Stop(Ctx);
	return true;
}

#endif
