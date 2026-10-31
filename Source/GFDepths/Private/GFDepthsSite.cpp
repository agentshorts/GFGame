#include "GFDepthsSite.h"

#include "GFDepthsDirector.h"
#include "GFDepthsTypes.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerState.h"
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

	static const FName SpotStart(TEXT("Start"));
	static const FName SpotFinish(TEXT("Finish"));
	static const FName SpotResults(TEXT("Results"));
	static const FName SpotReturn(TEXT("Return"));

	static const FName ModeEnter(TEXT("Enter"));
	static const FName ModeLeave(TEXT("Leave"));
	static const FName ModeLift(TEXT("LiftHome"));

	// The arrival stand is local (400, 0). Keep that patch empty. The lane runs north of it.
	static constexpr float LaneX = 1100.f;
	static constexpr float StartY = -900.f;
	static constexpr float FinishY = 250.f;
	static constexpr float ResultsY = 1000.f;
	static constexpr float HubX = -1300.f;

	static const FLinearColor Night(0.28f, 0.22f, 0.38f);
	static const FLinearColor StartTint(0.72f, 0.62f, 0.95f);
	static const FLinearColor FinishTint(0.95f, 0.78f, 0.35f);
	static const FLinearColor ResultsTint(0.62f, 0.48f, 0.85f);
	static const FLinearColor HubTint(0.90f, 0.90f, 0.85f);
}

AGFDepthsSite::AGFDepthsSite()
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

void AGFDepthsSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFDepthsSite, bReady);
	DOREPLIFETIME(AGFDepthsSite, LastPrompt);
	DOREPLIFETIME(AGFDepthsSite, bResultsReady);
	DOREPLIFETIME(AGFDepthsSite, LastDepthFinished);
	DOREPLIFETIME(AGFDepthsSite, LastDepthsReached);
	DOREPLIFETIME(AGFDepthsSite, LastBanked);
}

void AGFDepthsSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() || bReady)
	{
		BuildIfNeeded();
	}
}

void AGFDepthsSite::OnRep_Ready()
{
	if (bReady)
	{
		BuildIfNeeded();
	}
}

void AGFDepthsSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		PollSpots();
	}
	UpdateRunner();
	UpdateBoard();
}

AGFDepthsSite* AGFDepthsSite::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFDepthsSite> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFDepthsSite::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

FVector AGFDepthsSite::GetHubReturnLocation() const
{
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(TEXT("Depths"));
	const FVector Rime = (Entry && Entry->bHasPad) ? GetActorLocation() - Entry->PadOffset : FVector::ZeroVector;
	return Rime + FVector(0.f, 0.f, HubStandZCm);
}

bool AGFDepthsSite::HasVerb(FName Verb) const
{
	return TriggerVerbs.Contains(Verb);
}

UBoxComponent* AGFDepthsSite::GetVerbTrigger(FName Verb) const
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

void AGFDepthsSite::BuildIfNeeded()
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

void AGFDepthsSite::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
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

void AGFDepthsSite::AddLabel(const FString& Text, const FVector& Local, float WorldSize)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Root);
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(WorldSize);
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
	// Default text faces +X. Players arrive from the stand on -X, so turn the sign around.
	Label->SetRelativeLocationAndRotation(Local, FRotator(0.f, 180.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
}

UBoxComponent* AGFDepthsSite::AddTrigger(FName Verb, const FVector& Local, const FVector& Extent)
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

void AGFDepthsSite::AddArch(const FString& Title, FName Verb, const FVector& At, const FLinearColor& Color)
{
	AddBlock(At + FVector(0.f, -160.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(At + FVector(0.f, 160.f, 125.f), FVector(40.f, 40.f, 250.f), Color, true);
	AddBlock(At + FVector(0.f, 0.f, 260.f), FVector(40.f, 360.f, 30.f), Color, true);
	AddLabel(Title, At + FVector(0.f, 0.f, 320.f));
	AddTrigger(Verb, At + FVector(0.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));
}

void AGFDepthsSite::BuildVisuals()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	// Slab, top just above the pad so the two floors don't fight for the same surface.
	AddBlock(FVector(0.f, 0.f, -19.f), FVector(3600.f, 3600.f, 40.f), Night, true);

	AddLabel(TEXT("Hallow Night"), FVector(400.f, 0.f, 280.f));

	// A short picture of the course. Paint only: the pad is what you walk on.
	// The real 67 m lane would leave this 40 m pad, so it stays in the sim.
	AddBlock(FVector(LaneX, -600.f, 4.f), FVector(220.f, 120.f, 8.f), FLinearColor(0.22f, 0.16f, 0.32f), false);
	AddBlock(FVector(LaneX, -420.f, 28.f), FVector(160.f, 70.f, 20.f), FLinearColor(0.45f, 0.75f, 0.95f), false);
	AddLabel(TEXT("Z"), FVector(LaneX, -420.f, 70.f), 32.f);
	AddBlock(FVector(LaneX, -240.f, 4.f), FVector(220.f, 120.f, 8.f), FLinearColor(0.22f, 0.16f, 0.32f), false);
	AddBlock(FVector(LaneX, -80.f, 70.f), FVector(40.f, 40.f, 140.f), FLinearColor(0.95f, 0.55f, 0.45f), false);
	AddLabel(TEXT("X"), FVector(LaneX, -80.f, 160.f), 32.f);
	AddBlock(FVector(LaneX, 40.f, 24.f), FVector(180.f, 60.f, 16.f), FLinearColor(0.55f, 0.85f, 0.55f), false);
	AddLabel(TEXT("C"), FVector(LaneX, 40.f, 60.f), 32.f);
	// The drink under that bridge. Paint only: the sim owns the real channel.
	AddBlock(FVector(LaneX, 145.f, 2.f), FVector(220.f, 90.f, 12.f), FLinearColor(0.25f, 0.55f, 0.85f), false);
	AddLabel(TEXT("Water"), FVector(LaneX, 145.f, 48.f), 28.f);

	AddArch(TEXT("Start"), SpotStart, FVector(LaneX, StartY, 0.f), StartTint);
	AddArch(TEXT("Finish"), SpotFinish, FVector(LaneX, FinishY, 0.f), FinishTint);

	AddBlock(FVector(LaneX, ResultsY, 90.f), FVector(16.f, 240.f, 160.f), ResultsTint, true);
	AddLabel(TEXT("Results"), FVector(LaneX, ResultsY, 200.f));
	AddTrigger(SpotResults, FVector(LaneX, ResultsY, 120.f), FVector(140.f, 140.f, 120.f));

	ResultsBoard = NewObject<UTextRenderComponent>(this);
	ResultsBoard->SetupAttachment(Root);
	ResultsBoard->SetWorldSize(22.f);
	ResultsBoard->SetHorizontalAlignment(EHTA_Center);
	ResultsBoard->SetVerticalAlignment(EVRTA_TextCenter);
	ResultsBoard->SetTextRenderColor(FColor::White);
	if (SignFont)
	{
		ResultsBoard->SetFont(SignFont);
	}
	ResultsBoard->SetRelativeLocationAndRotation(FVector(LaneX - 24.f, ResultsY, 80.f), FRotator(0.f, 180.f, 0.f));
	ResultsBoard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ResultsBoard->SetCanEverAffectNavigation(false);
	ResultsBoard->SetText(FText::FromString(ResultsLine()));
	ResultsBoard->RegisterComponent();

	AddArch(TEXT("Hub"), SpotReturn, FVector(HubX, 0.f, 0.f), HubTint);

	if (CubeMesh)
	{
		Runner = NewObject<UStaticMeshComponent>(this);
		Runner->SetupAttachment(Root);
		Runner->SetStaticMesh(CubeMesh);
		Runner->SetRelativeScale3D(FVector(0.45f, 0.45f, 0.9f));
		Runner->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Runner->SetCanEverAffectNavigation(false);
		Runner->SetHiddenInGame(true);
		if (BaseMaterial)
		{
			if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, Runner))
			{
				Tint->SetVectorParameterValue(ColorParam, FLinearColor(0.95f, 0.85f, 0.55f));
				Runner->SetMaterial(0, Tint);
			}
		}
		Runner->RegisterComponent();
	}
}

AGFPlayerState* AGFDepthsSite::PlayerOf(APawn* Pawn) const
{
	return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
}

int32 AGFDepthsSite::NextDepth(const AGFPlayerState* PS) const
{
	const int32 Done = PS ? PS->Progress.DepthsReached : 0;
	return FMath::Clamp(Done + 1, 1, FGFDepthsLevel::MaxDepth);
}

FString AGFDepthsSite::ResultsLine() const
{
	if (!bResultsReady)
	{
		return TEXT("Results: finish the night first.");
	}
	FString Items;
	for (const FGFItemStack& S : LastBanked)
	{
		if (!S.IsValid())
		{
			continue;
		}
		if (!Items.IsEmpty())
		{
			Items += TEXT(", ");
		}
		Items += FString::Printf(TEXT("%s x%d"), *S.ItemId.ToString(), S.Count);
	}
	if (Items.IsEmpty())
	{
		Items = TEXT("nothing");
	}
	return FString::Printf(TEXT("Results: depth %d finished. Home keeps %s. Deepest open: %d."), LastDepthFinished, *Items, LastDepthsReached);
}

FGFModeResult AGFDepthsSite::Send(AGFPlayerState* PS, FName Verb, int32 Depth)
{
	if (!PS)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	FGFModeRequest R = FGFModeRequest::Make(AGFDepthsDirector::ModeName, Verb);
	if (Verb == ModeEnter)
	{
		R.Ints.Add(Depth);
	}
	const int32 RequestId = PS->RequestMode(R);
	if (const FGFModeResult* Found = PS->ModeResults.Find(RequestId))
	{
		LastPrompt = FString::Printf(TEXT("%s: %s"), *Verb.ToString(), Found->bOk ? TEXT("Ok") : *Found->Code.ToString());
		return *Found;
	}
	LastPrompt = FString::Printf(TEXT("%s: no answer"), *Verb.ToString());
	return FGFModeResult::Fail(TEXT("NoAnswer"));
}

FGFModeResult AGFDepthsSite::TryStart(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return FGFModeResult::Fail(TEXT("NotAuthority"));
	}
	AGFPlayerState* PS = PlayerOf(Pawn);
	const int32 Depth = NextDepth(PS);
	FGFModeResult R = Send(PS, ModeEnter, Depth);
	if (R.bOk)
	{
		bResultsReady = false;
		LastDepthFinished = 0;
		LastDepthsReached = 0;
		LastBanked.Reset();
		LastPrompt = FString::Printf(TEXT("Start: depth %d."), Depth);
	}
	return R;
}

FGFModeResult AGFDepthsSite::TryFinish(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return FGFModeResult::Fail(TEXT("NotAuthority"));
	}
	AGFPlayerState* PS = PlayerOf(Pawn);
	AGFDepthsDirector* Director = AGFDepthsDirector::Get(GetWorld(), false);
	if (!PS || !Director || !Director->FindPlayer(PS->PlayerKey))
	{
		return Send(PS, ModeLift, 0);
	}
	const int32 DepthNow = Director->Depth;
	if (!Director->ArriveAtExit(PS->PlayerKey))
	{
		LastPrompt = TEXT("Finish: the exit lift did not catch.");
		return FGFModeResult::Fail(TEXT("NotAtExit"));
	}
	FGFModeResult R = Send(PS, ModeLift, 0);
	if (R.bOk)
	{
		bResultsReady = true;
		LastDepthFinished = DepthNow;
		LastDepthsReached = R.Ints.Num() > 0 ? R.Ints[0] : PS->Progress.DepthsReached;
		LastBanked = R.Stacks;
		LastPrompt = ResultsLine();
	}
	return R;
}

FGFModeResult AGFDepthsSite::TryResults(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return FGFModeResult::Fail(TEXT("NotAuthority"));
	}
	if (!Pawn)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	LastPrompt = ResultsLine();
	return bResultsReady ? FGFModeResult::Ok() : FGFModeResult::Fail(TEXT("NoResults"));
}

bool AGFDepthsSite::TryReturn(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn)
	{
		return false;
	}
	if (!Pawn->IsPlayerControlled() && !PlayerOf(Pawn))
	{
		return false;
	}
	if (AGFPlayerState* PS = PlayerOf(Pawn))
	{
		if (AGFDepthsDirector* Director = AGFDepthsDirector::Get(GetWorld(), false))
		{
			if (Director->FindPlayer(PS->PlayerKey))
			{
				Send(PS, ModeLeave, 0);
			}
		}
	}
	Pawn->SetActorLocation(GetHubReturnLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	LastPrompt = TEXT("Back on the hub stand.");
	return true;
}

void AGFDepthsSite::GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	OutLines.Add(TEXT("HALLOW NIGHT"));
	OutLines.Add(TEXT("Walk Start, then Finish, then Results. The Hub arch goes back."));
	OutLines.Add(FString::Printf(TEXT("Start opens depth %d. Finished: %d."), NextDepth(PS), PS ? PS->Progress.DepthsReached : 0));
	OutLines.Add(TEXT("Finish is the exit lift. It banks what the exit drops."));
	if (FGFDepthsLevel::Get(1).Waters.Num() > 0)
	{
		OutLines.Add(TEXT("Water sits under the last bridge. A rented flyer can catch you or fly you over. A rented swimmer can float it."));
	}
	const AGFDepthsDirector* Director = AGFDepthsDirector::Get(GetWorld(), false);
	if (PS && Director && Director->FindPlayer(PS->PlayerKey))
	{
		OutLines.Add(FString::Printf(TEXT("In the run at depth %d."), Director->Depth));
	}
	OutLines.Add(ResultsLine());
	if (!LastPrompt.IsEmpty() && LastPrompt != ResultsLine())
	{
		OutLines.Add(LastPrompt);
	}
}

void AGFDepthsSite::UpdateRunner()
{
	if (!Runner)
	{
		return;
	}
	const AGFDepthsDirector* Director = AGFDepthsDirector::Get(GetWorld(), false);
	const FGFDepthsPlayerView* View = nullptr;
	if (Director)
	{
		FString Key;
		if (UWorld* World = GetWorld())
		{
			if (const APlayerController* PC = World->GetFirstPlayerController())
			{
				if (const AGFPlayerState* PS = PC->GetPlayerState<AGFPlayerState>())
				{
					Key = PS->PlayerKey;
				}
			}
		}
		View = Key.IsEmpty() ? nullptr : Director->FindPlayer(Key);
		if (!View && Director->Players.Num() > 0)
		{
			View = &Director->Players[0];
		}
	}
	const int32 Depth = Director && Director->Depth > 0 ? Director->Depth : 1;
	const float ExitX = FMath::Max(1.f, FGFDepthsLevel::Get(Depth).Exit.X);
	if (View)
	{
		const float T = FMath::Clamp(View->Body.Pos.X / ExitX, 0.f, 1.f);
		Runner->SetRelativeLocation(FVector(LaneX, FMath::Lerp(StartY, FinishY, T), 90.f));
		Runner->SetHiddenInGame(false);
	}
	else if (bResultsReady)
	{
		Runner->SetRelativeLocation(FVector(LaneX, FinishY, 90.f));
		Runner->SetHiddenInGame(false);
	}
	else
	{
		Runner->SetHiddenInGame(true);
	}
}

void AGFDepthsSite::UpdateBoard()
{
	if (ResultsBoard)
	{
		ResultsBoard->SetText(FText::FromString(ResultsLine()));
	}
}

void AGFDepthsSite::PollSpots()
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
			if (Verb == SpotStart)
			{
				TryStart(Pawn);
			}
			else if (Verb == SpotFinish)
			{
				TryFinish(Pawn);
			}
			else if (Verb == SpotResults)
			{
				TryResults(Pawn);
			}
			else if (Verb == SpotReturn)
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
