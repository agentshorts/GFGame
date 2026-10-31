#include "GFArenaSite.h"

#include "GFArenaDirector.h"
#include "GFArenaTypes.h"
#include "GFCompanionRoster.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	static const FName ColorParam(TEXT("Color"));

	static const FName VerbStart(TEXT("Start"));
	static const FName VerbFinish(TEXT("Finish"));
	static const FName VerbResults(TEXT("Results"));
	static const FName VerbReturn(TEXT("Return"));

	static const FName VerbAutoPlace(TEXT("AutoPlace"));
	static const FName VerbNoWager(TEXT("NoWager"));
	static const FName VerbFight(TEXT("Fight"));

	static const FName SpeciesNipper(TEXT("Nipper"));
	static const FString LoanerName(TEXT("Sparring"));

	FString OutcomeWord(EGFArenaOutcome Outcome)
	{
		switch (Outcome)
		{
		case EGFArenaOutcome::Team0Won: return TEXT("Win");
		case EGFArenaOutcome::Team1Won: return TEXT("Loss");
		case EGFArenaOutcome::Draw: return TEXT("Draw");
		default: return TEXT("None");
		}
	}

	FString RewardList(const TArray<FGFItemStack>& Rewards)
	{
		FString Text;
		for (const FGFItemStack& Stack : Rewards)
		{
			if (!Text.IsEmpty())
			{
				Text += TEXT(", ");
			}
			Text += FString::Printf(TEXT("%s x%d"), *Stack.ItemId.ToString(), Stack.Count);
		}
		return Text.IsEmpty() ? FString(TEXT("none")) : Text;
	}
}

AGFArenaSite::AGFArenaSite()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AGFArenaSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFArenaSite, bReady);
	DOREPLIFETIME(AGFArenaSite, LastPrompt);
	DOREPLIFETIME(AGFArenaSite, BalconyCamera);
}

FVector AGFArenaSite::BalconyCameraLocal()
{
	return FVector(0.f, -1760.f, 760.f);
}

FVector AGFArenaSite::BalconyLookLocal()
{
	return FVector(0.f, 0.f, 40.f);
}

FRotator AGFArenaSite::BalconyCameraRotation()
{
	return (BalconyLookLocal() - BalconyCameraLocal()).Rotation();
}

FVector AGFArenaSite::BalconyStandLocal()
{
	return FVector(0.f, -1500.f, 530.f);
}

void AGFArenaSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() || bReady)
	{
		BuildIfNeeded();
	}
}

void AGFArenaSite::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BalconyCamera)
	{
		BalconyCamera->Destroy();
		BalconyCamera = nullptr;
	}
	Super::EndPlay(Reason);
}

void AGFArenaSite::OnRep_Ready()
{
	if (bReady)
	{
		BuildIfNeeded();
	}
}

void AGFArenaSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		PollSpots();
	}
}

AGFArenaSite* AGFArenaSite::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFArenaSite> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFArenaSite::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

FVector AGFArenaSite::GetHubReturnLocation() const
{
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(TEXT("Arena"));
	const FVector Rime = (Entry && Entry->bHasPad) ? GetActorLocation() - Entry->PadOffset : FVector::ZeroVector;
	return Rime + FVector(0.f, 0.f, HubStandZCm);
}

bool AGFArenaSite::HasVerb(FName Verb) const
{
	return TriggerVerbs.Contains(Verb);
}

UBoxComponent* AGFArenaSite::GetVerbTrigger(FName Verb) const
{
	for (int32 i = 0; i < Triggers.Num() && i < TriggerVerbs.Num(); ++i)
	{
		if (TriggerVerbs[i] == Verb)
		{
			return Triggers[i];
		}
	}
	return nullptr;
}

void AGFArenaSite::BuildIfNeeded()
{
	if (bVisualsBuilt)
	{
		return;
	}
	BuildVisuals();
	bVisualsBuilt = true;
	if (HasAuthority())
	{
		bReady = true;
	}
}

void AGFArenaSite::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
{
	if (!CubeMesh)
	{
		return;
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
	Mesh->SetupAttachment(Root);
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetRelativeLocation(LocalCenter);
	Mesh->SetRelativeScale3D(SizeCm / 100.f);
	Mesh->SetCanEverAffectNavigation(false);
	if (bBlock)
	{
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (BaseMaterial)
	{
		if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, Mesh))
		{
			Tint->SetVectorParameterValue(ColorParam, Color);
			Mesh->SetMaterial(0, Tint);
		}
	}
	Mesh->RegisterComponent();
}

void AGFArenaSite::AddLabel(const FString& Text, const FVector& Local)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(42.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(FColor::White);
	if (!SignFont)
	{
		SignFont = LoadObject<UFont>(nullptr, FontPath);
	}
	if (SignFont)
	{
		Label->SetFont(SignFont);
	}
	// Default text faces +X. Players arrive from -X (the stand), so turn the sign around.
	Label->SetRelativeLocationAndRotation(Local, FRotator(0.f, 180.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
}

UBoxComponent* AGFArenaSite::AddTrigger(FName Verb, const FVector& Local, const FVector& Extent)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(this);
	Box->SetupAttachment(Root);
	Box->SetRelativeLocation(Local);
	Box->SetBoxExtent(Extent);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	Box->RegisterComponent();
	Triggers.Add(Box);
	TriggerVerbs.Add(Verb);
	return Box;
}

void AGFArenaSite::AddArch(const FString& Title, FName Verb, const FVector& At, const FLinearColor& Color)
{
	AddBlock(At + FVector(0.f, 0.f, 2.f), FVector(280.f, 360.f, 4.f), Color, false);
	AddBlock(At + FVector(0.f, -140.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(At + FVector(0.f, 140.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(At + FVector(0.f, 0.f, 260.f), FVector(40.f, 320.f, 30.f), Color, true);
	AddLabel(Title, At + FVector(0.f, 0.f, 320.f));
	AddTrigger(Verb, At + FVector(0.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));
}

void AGFArenaSite::BuildVisuals()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	// Slab, top at local Z = 1, inside the 40 m pad.
	// Top sits 1 cm above the pad slab so the two floors don't fight for the same surface.
	AddBlock(FVector(0.f, 0.f, -19.f), FVector(3600.f, 3600.f, 40.f), FLinearColor(0.72f, 0.58f, 0.32f), true);

	// Arrival from the hub gate is local (400, 0, 120). Keep that patch empty.
	// A painted lane joins Start and Finish. It does not block the stand.
	AddBlock(FVector(1200.f, 0.f, 2.f), FVector(80.f, 1600.f, 4.f), FLinearColor(0.85f, 0.70f, 0.38f), false);

	AddArch(TEXT("Start"), VerbStart, FVector(1200.f, -700.f, 0.f), FLinearColor(0.45f, 0.72f, 0.40f));
	AddArch(TEXT("Finish"), VerbFinish, FVector(1200.f, 700.f, 0.f), FLinearColor(0.78f, 0.32f, 0.24f));

	const FVector Board(200.f, 1100.f, 0.f);
	AddBlock(Board + FVector(0.f, 0.f, 110.f), FVector(20.f, 240.f, 180.f), FLinearColor(0.85f, 0.72f, 0.28f), true);
	AddLabel(TEXT("Results"), Board + FVector(-30.f, 0.f, 220.f));
	AddTrigger(VerbResults, Board + FVector(0.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));

	// West side of the pad, toward the hub.
	AddArch(TEXT("Hub"), VerbReturn, FVector(-1100.f, 0.f, 0.f), FLinearColor(0.90f, 0.90f, 0.85f));
	AddLabel(TEXT("Arena"), FVector(400.f, 0.f, 280.f));

	// South balcony. The stand at (400, 0) stays clear. The rail looks down the lane.
	const FVector Deck(0.f, -1500.f, 400.f);
	AddBlock(Deck, FVector(900.f, 520.f, 40.f), FLinearColor(0.45f, 0.38f, 0.32f), true);
	AddBlock(Deck + FVector(0.f, 220.f, 80.f), FVector(860.f, 30.f, 70.f), FLinearColor(0.62f, 0.48f, 0.28f), true);
	AddLabel(TEXT("Balcony"), Deck + FVector(0.f, 0.f, 160.f));

	if (HasAuthority() && GetWorld() && !BalconyCamera)
	{
		const FVector WorldPos = GetActorTransform().TransformPosition(BalconyCameraLocal());
		const FRotator WorldRot = BalconyCameraRotation();
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BalconyCamera = GetWorld()->SpawnActor<ACameraActor>(WorldPos, WorldRot, Params);
		if (BalconyCamera)
		{
			BalconyCamera->SetReplicates(true);
			BalconyCamera->bAlwaysRelevant = true;
			BalconyCamera->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			if (UCameraComponent* Lens = BalconyCamera->GetCameraComponent())
			{
				Lens->SetWorldLocationAndRotation(WorldPos, WorldRot);
				Lens->SetFieldOfView(70.f);
			}
		}
	}
}

bool AGFArenaSite::ApplyBalconyView(APawn* Pawn)
{
	if (!Pawn || !BalconyCamera)
	{
		return false;
	}
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC)
	{
		return false;
	}
	PC->SetViewTarget(BalconyCamera);
	return true;
}

void AGFArenaSite::ReleaseBalconyView(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
	{
		PC->SetViewTarget(Pawn);
	}
}

AGFPlayerState* AGFArenaSite::PlayerOf(APawn* Pawn) const
{
	return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
}

AGFArenaDirector* AGFArenaSite::Director() const
{
	return AGFArenaDirector::Get(GetWorld(), false);
}

FName AGFArenaSite::LoanerSpeciesId() const
{
	const FGFArenaData& Data = FGFArenaData::Get();
	if (Data.Species.Contains(SpeciesNipper))
	{
		return SpeciesNipper;
	}
	const TArray<FName> Ids = Data.SpeciesIds();
	return Ids.Num() > 0 ? Ids[0] : NAME_None;
}

bool AGFArenaSite::EnsureLoaner(AGFPlayerState* PS)
{
	if (!PS || PS->Companions.Num() > 0)
	{
		return false;
	}
	const FName Species = LoanerSpeciesId();
	if (Species.IsNone())
	{
		return false;
	}
	FGFCompanions::AddTamed(PS->Companions, Species, LoanerName);
	PS->MarkCharacterChanged();
	return true;
}

FGFModeResult AGFArenaSite::Send(AGFPlayerState* PS, FName Verb)
{
	if (!PS)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	const int32 RequestId = PS->RequestMode(FGFModeRequest::Make(AGFArenaDirector::ModeName, Verb));
	if (const FGFModeResult* Found = PS->ModeResults.Find(RequestId))
	{
		LastPrompt = FString::Printf(TEXT("%s: %s"), *Verb.ToString(), Found->bOk ? TEXT("Ok") : *Found->Code.ToString());
		return *Found;
	}
	LastPrompt = FString::Printf(TEXT("%s: no answer"), *Verb.ToString());
	return FGFModeResult::Fail(TEXT("NoAnswer"));
}

FGFModeResult AGFArenaSite::TryStart(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	if (!PS)
	{
		LastPrompt = TEXT("Start: NoPlayer");
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	AGFArenaDirector* D = Director();
	if (D && D->Phase == EGFArenaPhase::Fighting)
	{
		LastPrompt = TEXT("Start: Fighting");
		return FGFModeResult::Fail(TEXT("Fighting"));
	}
	EnsureLoaner(PS);
	const FGFModeResult Placed = Send(PS, VerbAutoPlace);
	if (!Placed.bOk)
	{
		return Placed;
	}
	const FGFModeResult Cleared = Send(PS, VerbNoWager);
	if (!Cleared.bOk)
	{
		return Cleared;
	}
	const FGFModeResult Began = Send(PS, VerbFight);
	if (Began.bOk)
	{
		const FVector Stand = GetActorTransform().TransformPosition(BalconyStandLocal());
		Pawn->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		ApplyBalconyView(Pawn);
		LastPrompt = TEXT("Start: bout began. Watching from the balcony.");
	}
	return Began;
}

FGFModeResult AGFArenaSite::TryFinish(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	if (!PS)
	{
		LastPrompt = TEXT("Finish: NoPlayer");
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	AGFArenaDirector* D = Director();
	if (!D || D->Phase == EGFArenaPhase::Idle)
	{
		LastPrompt = TEXT("Finish: NotFighting");
		return FGFModeResult::Fail(TEXT("NotFighting"));
	}
	if (D->Phase == EGFArenaPhase::Finished)
	{
		LastPrompt = TEXT("Finish: already over");
		return FGFModeResult::Ok();
	}
	if (!D->ResolvePlayableBout())
	{
		LastPrompt = TEXT("Finish: NotFighting");
		return FGFModeResult::Fail(TEXT("NotFighting"));
	}
	ReleaseBalconyView(Pawn);
	const FGFArenaEntry* Entry = D->FindEntry(PS->PlayerKey);
	const int32 Stars = Entry ? Entry->Stars : 0;
	LastPrompt = FString::Printf(TEXT("Finish: %s, %d star(s)"), *OutcomeWord(D->Outcome), Stars);
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Ints = { Stars };
	return Res;
}

FGFModeResult AGFArenaSite::TryResults(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	if (!PS)
	{
		LastPrompt = TEXT("Results: NoPlayer");
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	AGFArenaDirector* D = Director();
	if (!D || D->Phase != EGFArenaPhase::Finished)
	{
		LastPrompt = TEXT("Results: NotFinished");
		return FGFModeResult::Fail(TEXT("NotFinished"));
	}
	const FGFArenaEntry* Entry = D->FindEntry(PS->PlayerKey);
	const int32 Stars = Entry ? Entry->Stars : 0;
	const FString Rewards = Entry ? RewardList(Entry->Rewards) : FString(TEXT("none"));
	LastPrompt = FString::Printf(TEXT("Results: %s, %d star(s), rewards %s"), *OutcomeWord(D->Outcome), Stars, *Rewards);
	FGFModeResult Res = FGFModeResult::Ok();
	Res.Ints = { Stars, static_cast<int32>(D->Outcome) };
	return Res;
}

bool AGFArenaSite::TryReturn(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn)
	{
		return false;
	}
	if (!Pawn->IsPlayerControlled() && !PlayerOf(Pawn))
	{
		return false;
	}
	ReleaseBalconyView(Pawn);
	const FVector Where = GetHubReturnLocation();
	Pawn->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
	LastPrompt = TEXT("Back on the hub stand.");
	return true;
}

void AGFArenaSite::GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	OutLines.Add(TEXT("ARENA"));
	OutLines.Add(TEXT("Spots: Start, Finish, Results. Return: the white arch. Watch from the Balcony."));
	OutLines.Add(TEXT("Start places your dinos, clears any wager, and begins the bout."));
	OutLines.Add(TEXT("An empty roster gets one sparring Nipper. A roster you already have is left as it is."));
	OutLines.Add(TEXT("Finish runs the bout to its real result. No stake is taken on this pad."));
	const AGFArenaDirector* D = Director();
	if (D && D->Phase == EGFArenaPhase::Fighting)
	{
		OutLines.Add(TEXT("Bout in progress. Walk to Finish."));
	}
	else if (D && D->Phase == EGFArenaPhase::Finished && PS)
	{
		const FGFArenaEntry* Entry = D->FindEntry(PS->PlayerKey);
		const int32 Stars = Entry ? Entry->Stars : 0;
		const FString Rewards = Entry ? RewardList(Entry->Rewards) : FString(TEXT("none"));
		OutLines.Add(FString::Printf(TEXT("Results: %s, %d star(s), rewards %s"), *OutcomeWord(D->Outcome), Stars, *Rewards));
	}
	else
	{
		OutLines.Add(TEXT("Results waits until the bout is over."));
	}
	if (!LastPrompt.IsEmpty())
	{
		OutLines.Add(LastPrompt);
	}
}

void AGFArenaSite::PollSpots()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SpotInside.SetNum(Triggers.Num());
	for (int32 i = 0; i < Triggers.Num(); ++i)
	{
		UBoxComponent* Box = Triggers[i];
		if (!Box || !TriggerVerbs.IsValidIndex(i))
		{
			continue;
		}
		const FName Verb = TriggerVerbs[i];
		TSet<TWeakObjectPtr<APawn>> Inside;
		TArray<AActor*> Over;
		Box->GetOverlappingActors(Over, APawn::StaticClass());
		for (AActor* Actor : Over)
		{
			APawn* Pawn = Cast<APawn>(Actor);
			if (!Pawn || (!Pawn->IsPlayerControlled() && !PlayerOf(Pawn)))
			{
				continue;
			}
			const TWeakObjectPtr<APawn> Key(Pawn);
			Inside.Add(Key);
			if (SpotInside[i].Contains(Key))
			{
				continue;
			}
			SpotInside[i].Add(Key);
			if (Verb == VerbStart)
			{
				TryStart(Pawn);
			}
			else if (Verb == VerbFinish)
			{
				TryFinish(Pawn);
			}
			else if (Verb == VerbResults)
			{
				TryResults(Pawn);
			}
			else if (Verb == VerbReturn)
			{
				TryReturn(Pawn);
			}
		}
		for (auto It = SpotInside[i].CreateIterator(); It; ++It)
		{
			if (!Inside.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}
	}
}
