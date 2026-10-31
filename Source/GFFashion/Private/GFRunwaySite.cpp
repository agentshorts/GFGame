#include "GFRunwaySite.h"

#include "GFFashionTypes.h"
#include "GFLog.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerState.h"
#include "GFRunwayJudge.h"
#include "GFRunwayStage.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	static const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const TCHAR* FontPath = TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField");
	static const FName ColorParam(TEXT("Color"));

	static const FName VerbTheme(TEXT("Theme"));
	static const FName VerbLook(TEXT("Look"));
	static const FName VerbStart(TEXT("Start"));
	static const FName VerbFinish(TEXT("Finish"));
	static const FName VerbReturn(TEXT("Return"));

	static const FName LookGarden(TEXT("Garden"));
	static const FName LookExplorer(TEXT("Explorer"));
	static const FName ThemeGarden(TEXT("GardenParty"));

	static const TCHAR* GardenPieces[] = { TEXT("FernBraid"), TEXT("PetalBlouse"), TEXT("MeadowSkirt"), TEXT("BloomSandals"), TEXT("DaisyCrown") };
	static const TCHAR* ExplorerPieces[] = { TEXT("MossMop"), TEXT("ExplorerVest"), TEXT("CargoShorts"), TEXT("TrailBoots"), TEXT("FossilCompass") };

	bool IsLook(FName Id)
	{
		return Id == LookGarden || Id == LookExplorer;
	}

	FString ResultName(EGFRunwayRequestResult Result)
	{
		if (const UEnum* E = StaticEnum<EGFRunwayRequestResult>())
		{
			return E->GetNameStringByValue(static_cast<int64>(Result));
		}
		return TEXT("?");
	}

	FString PhaseName(EGFRunwayPhase Phase)
	{
		if (const UEnum* E = StaticEnum<EGFRunwayPhase>())
		{
			return E->GetNameStringByValue(static_cast<int64>(Phase));
		}
		return TEXT("?");
	}

	FString TierName(EGFRunwayTier Tier)
	{
		if (const UEnum* E = StaticEnum<EGFRunwayTier>())
		{
			return E->GetNameStringByValue(static_cast<int64>(Tier));
		}
		return TEXT("?");
	}

	FString ThemeTitle(FName Id)
	{
		const FGFFashionTheme* Theme = FGFFashionCatalog::FindTheme(Id);
		return Theme ? Theme->DisplayName : Id.ToString();
	}

	FString LookTitle(FName Id)
	{
		if (Id == LookExplorer)
		{
			return TEXT("Explorer");
		}
		return TEXT("Garden");
	}

	FGFOutfit MakeLook(FName Id)
	{
		FGFOutfit Outfit;
		const TCHAR* const* Pieces = Id == LookExplorer ? ExplorerPieces : GardenPieces;
		const int32 Count = Id == LookExplorer ? UE_ARRAY_COUNT(ExplorerPieces) : UE_ARRAY_COUNT(GardenPieces);
		for (int32 i = 0; i < Count; ++i)
		{
			Outfit.Equip(Pieces[i]);
		}
		return Outfit;
	}

	FString LookPieces(FName Id)
	{
		FString Names;
		const FGFOutfit Outfit = MakeLook(Id);
		for (const FName PieceId : Outfit.GetPieceIds())
		{
			if (!Names.IsEmpty())
			{
				Names += TEXT(", ");
			}
			if (const FGFFashionPiece* Piece = FGFFashionCatalog::FindPiece(PieceId))
			{
				Names += Piece->DisplayName;
			}
			else
			{
				Names += PieceId.ToString();
			}
		}
		return Names;
	}

	bool Usable(const APawn* Pawn, const AGFPlayerState* PS)
	{
		return Pawn && (Pawn->IsPlayerControlled() || PS);
	}
}

AGFRunwaySite::AGFRunwaySite()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	// Suggested greybox picks only. Standing on a spot is what wears a look.
	SelectedTheme = ThemeGarden;
	SelectedLook = LookGarden;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void AGFRunwaySite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFRunwaySite, bReady);
	DOREPLIFETIME(AGFRunwaySite, LastPrompt);
	DOREPLIFETIME(AGFRunwaySite, SelectedTheme);
	DOREPLIFETIME(AGFRunwaySite, SelectedLook);
	DOREPLIFETIME(AGFRunwaySite, bThemeChosen);
	DOREPLIFETIME(AGFRunwaySite, bLookChosen);
}

void AGFRunwaySite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() || bReady)
	{
		BuildIfNeeded();
	}
}

void AGFRunwaySite::OnRep_Ready()
{
	if (bReady)
	{
		BuildIfNeeded();
	}
}

void AGFRunwaySite::OnRep_Choices()
{
	RefreshCues();
}

void AGFRunwaySite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The arch and the wardrobe sign follow the phase. Clients do not run the spots.
	RefreshCues();
	if (!HasAuthority())
	{
		return;
	}
	PollSpots();
	SeatFarPawns();

	UWorld* World = GetWorld();
	AGFRunwayStage* Stage = AGFRunwayStage::Find(World);
	if (!World || !Stage || bTravelled)
	{
		return;
	}
	if (Stage->GetPhase() == EGFRunwayPhase::Results)
	{
		if (ResultsShownAt < 0.0)
		{
			ResultsShownAt = World->GetTimeSeconds();
		}
		if (World->GetTimeSeconds() >= ResultsShownAt + FMath::Max(0.f, ResultsSeconds))
		{
			TravelToHub();
		}
	}
	else
	{
		ResultsShownAt = -1.0;
	}
}

AGFRunwaySite* AGFRunwaySite::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFRunwaySite> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFRunwaySite::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

bool AGFRunwaySite::HasVerb(FName Verb) const
{
	return TriggerVerbs.Contains(Verb);
}

UBoxComponent* AGFRunwaySite::GetVerbTrigger(FName Verb) const
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

UBoxComponent* AGFRunwaySite::GetThemeTrigger(FName ThemeId) const
{
	for (int32 i = 0; i < Triggers.Num() && i < TriggerVerbs.Num() && i < TriggerNames.Num(); ++i)
	{
		if (TriggerVerbs[i] == VerbTheme && TriggerNames[i] == ThemeId)
		{
			return Triggers[i];
		}
	}
	return nullptr;
}

UBoxComponent* AGFRunwaySite::GetLookTrigger(FName LookId) const
{
	for (int32 i = 0; i < Triggers.Num() && i < TriggerVerbs.Num() && i < TriggerNames.Num(); ++i)
	{
		if (TriggerVerbs[i] == VerbLook && TriggerNames[i] == LookId)
		{
			return Triggers[i];
		}
	}
	return nullptr;
}

int32 AGFRunwaySite::NumThemeSpots() const
{
	int32 N = 0;
	for (const FName Verb : TriggerVerbs)
	{
		N += Verb == VerbTheme ? 1 : 0;
	}
	return N;
}

int32 AGFRunwaySite::NumLookSpots() const
{
	int32 N = 0;
	for (const FName Verb : TriggerVerbs)
	{
		N += Verb == VerbLook ? 1 : 0;
	}
	return N;
}

void AGFRunwaySite::BuildIfNeeded()
{
	if (bVisualsBuilt)
	{
		return;
	}
	bVisualsBuilt = true;
	BuildVisuals();
	if (HasAuthority())
	{
		bReady = true;
	}
}

void AGFRunwaySite::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
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

UTextRenderComponent* AGFRunwaySite::AddLabel(const FString& Text, const FVector& Local, float WorldSize)
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
	// Default text faces +X. Players stand on the dressing side (local -X) and look down the catwalk.
	Label->SetRelativeLocationAndRotation(Local, FRotator(0.f, 180.f, 0.f));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
	return Label;
}

UBoxComponent* AGFRunwaySite::AddTrigger(FName Verb, FName Name, const FVector& Local, const FVector& Extent)
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
	TriggerNames.Add(Name);
	return Box;
}

void AGFRunwaySite::BuildVisuals()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	// Floor top at local Z = 0. The map sign sits at (-500, 0), behind this slab.
	AddBlock(FVector(1100.f, 0.f, -20.f), FVector(2400.f, 1000.f, 40.f), FLinearColor(0.55f, 0.42f, 0.48f), true);

	const TArray<FGFFashionTheme>& Themes = FGFFashionCatalog::Themes();
	for (int32 i = 0; i < Themes.Num(); ++i)
	{
		const float Y = (static_cast<float>(i) - (Themes.Num() - 1) * 0.5f) * 180.f;
		const FVector At(250.f, Y, 0.f);
		const FLinearColor Tint = FLinearColor::MakeFromHSV8(static_cast<uint8>(i * 48), 140, 220);
		AddBlock(At + FVector(0.f, 0.f, 4.f), FVector(140.f, 140.f, 8.f), Tint, false);
		AddLabel(Themes[i].DisplayName, At + FVector(0.f, 0.f, 160.f));
		AddTrigger(VerbTheme, Themes[i].Id, At + FVector(0.f, 0.f, 100.f), FVector(70.f, 70.f, 100.f));
	}

	// Wardrobe before the start arch, close enough to read from the dressing end.
	// Posts sit outside the look triggers so a player can stand on a look.
	const FVector GardenAt(460.f, -180.f, 0.f);
	const FVector ExplorerAt(460.f, 180.f, 0.f);
	const FVector RackAt(460.f, 0.f, 0.f);
	const FLinearColor Wood(0.45f, 0.28f, 0.18f);
	const FLinearColor Brass(0.72f, 0.58f, 0.28f);
	AddBlock(RackAt + FVector(-55.f, -300.f, 120.f), FVector(18.f, 18.f, 240.f), Wood, true);
	AddBlock(RackAt + FVector(-55.f, 300.f, 120.f), FVector(18.f, 18.f, 240.f), Wood, true);
	AddBlock(RackAt + FVector(-55.f, 0.f, 232.f), FVector(14.f, 620.f, 12.f), Brass, true);
	AddLabel(TEXT("WARDROBE"), RackAt + FVector(0.f, 0.f, 310.f), 52.f);
	WearingCue = AddLabel(GetWearingCue(), RackAt + FVector(10.f, 0.f, 255.f), 32.f);
	// Busts are not solid: the look spot has to be walked onto.
	AddBlock(GardenAt + FVector(0.f, 0.f, 4.f), FVector(160.f, 140.f, 8.f), FLinearColor(0.85f, 0.55f, 0.65f), false);
	AddBlock(ExplorerAt + FVector(0.f, 0.f, 4.f), FVector(160.f, 140.f, 8.f), FLinearColor(0.55f, 0.42f, 0.28f), false);
	AddBlock(GardenAt + FVector(0.f, 0.f, 78.f), FVector(36.f, 28.f, 72.f), FLinearColor(0.85f, 0.55f, 0.65f), false);
	AddBlock(GardenAt + FVector(0.f, 0.f, 128.f), FVector(22.f, 22.f, 22.f), FLinearColor(0.93f, 0.78f, 0.68f), false);
	AddBlock(ExplorerAt + FVector(0.f, 0.f, 78.f), FVector(36.f, 28.f, 72.f), FLinearColor(0.55f, 0.42f, 0.28f), false);
	AddBlock(ExplorerAt + FVector(0.f, 0.f, 128.f), FVector(22.f, 22.f, 22.f), FLinearColor(0.93f, 0.78f, 0.68f), false);
	AddLabel(TEXT("Garden"), GardenAt + FVector(0.f, 0.f, 190.f));
	AddLabel(TEXT("Explorer"), ExplorerAt + FVector(0.f, 0.f, 190.f));
	AddLabel(TEXT("Stand here"), GardenAt + FVector(0.f, 0.f, 155.f), 28.f);
	AddLabel(TEXT("Stand here"), ExplorerAt + FVector(0.f, 0.f, 155.f), 28.f);
	AddTrigger(VerbLook, LookGarden, GardenAt + FVector(0.f, 0.f, 100.f), FVector(70.f, 70.f, 100.f));
	AddTrigger(VerbLook, LookExplorer, ExplorerAt + FVector(0.f, 0.f, 100.f), FVector(70.f, 70.f, 100.f));

	AddLabel(TEXT("Theme, then wardrobe, then Start"), FVector(160.f, 0.f, 230.f), 32.f);

	const FVector StartAt(860.f, 0.f, 0.f);
	AddBlock(StartAt + FVector(0.f, -140.f, 140.f), FVector(30.f, 30.f, 280.f), FLinearColor(0.85f, 0.45f, 0.62f), true);
	AddBlock(StartAt + FVector(0.f, 140.f, 140.f), FVector(30.f, 30.f, 280.f), FLinearColor(0.85f, 0.45f, 0.62f), true);
	AddBlock(StartAt + FVector(0.f, 0.f, 290.f), FVector(30.f, 310.f, 24.f), FLinearColor(0.85f, 0.45f, 0.62f), true);
	AddLabel(TEXT("START"), StartAt + FVector(0.f, 0.f, 360.f), 64.f);
	StartCue = AddLabel(GetStartCue(), StartAt + FVector(30.f, 0.f, 180.f), 36.f);
	AddTrigger(VerbStart, NAME_None, StartAt + FVector(0.f, 0.f, 120.f), FVector(90.f, 90.f, 120.f));

	AddBlock(FVector(1200.f, -220.f, 40.f), FVector(700.f, 16.f, 80.f), FLinearColor(0.72f, 0.62f, 0.66f), true);
	AddBlock(FVector(1200.f, 220.f, 40.f), FVector(700.f, 16.f, 80.f), FLinearColor(0.72f, 0.62f, 0.66f), true);

	const FVector FinishAt(1600.f, 0.f, 0.f);
	AddBlock(FinishAt + FVector(0.f, -140.f, 140.f), FVector(30.f, 30.f, 280.f), FLinearColor(0.95f, 0.78f, 0.35f), true);
	AddBlock(FinishAt + FVector(0.f, 140.f, 140.f), FVector(30.f, 30.f, 280.f), FLinearColor(0.95f, 0.78f, 0.35f), true);
	AddBlock(FinishAt + FVector(0.f, 0.f, 290.f), FVector(30.f, 310.f, 24.f), FLinearColor(0.95f, 0.78f, 0.35f), true);
	AddLabel(TEXT("Finish"), FinishAt + FVector(0.f, 0.f, 340.f));
	AddTrigger(VerbFinish, NAME_None, FinishAt + FVector(0.f, 0.f, 120.f), FVector(90.f, 90.f, 120.f));

	const FVector HubAt(2050.f, 0.f, 0.f);
	AddBlock(HubAt + FVector(0.f, -140.f, 125.f), FVector(30.f, 30.f, 250.f), FLinearColor(0.92f, 0.92f, 0.88f), true);
	AddBlock(HubAt + FVector(0.f, 140.f, 125.f), FVector(30.f, 30.f, 250.f), FLinearColor(0.92f, 0.92f, 0.88f), true);
	AddBlock(HubAt + FVector(0.f, 0.f, 260.f), FVector(30.f, 310.f, 24.f), FLinearColor(0.92f, 0.92f, 0.88f), true);
	AddLabel(TEXT("Hub"), HubAt + FVector(0.f, 0.f, 310.f));
	AddTrigger(VerbReturn, NAME_None, HubAt + FVector(0.f, 0.f, 120.f), FVector(100.f, 100.f, 120.f));

	AddLabel(TEXT("RUNWAY"), FVector(860.f, 0.f, 430.f));
}

AGFPlayerState* AGFRunwaySite::PlayerOf(APawn* Pawn) const
{
	return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
}

bool AGFRunwaySite::ApplyLook(const FString& PlayerKey)
{
	AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	if (!Stage || PlayerKey.IsEmpty())
	{
		return false;
	}
	return Stage->HandleSetOutfit(PlayerKey, MakeLook(SelectedLook)) == EGFRunwayRequestResult::Ok;
}

bool AGFRunwaySite::TrySelectTheme(APawn* Pawn, FName ThemeId)
{
	if (!HasAuthority() || !Usable(Pawn, PlayerOf(Pawn)))
	{
		return false;
	}
	if (!FGFFashionCatalog::FindTheme(ThemeId))
	{
		LastPrompt = TEXT("Theme: unknown");
		return false;
	}
	SelectedTheme = ThemeId;
	bThemeChosen = true;
	LastPrompt = FString::Printf(TEXT("Theme: %s"), *ThemeTitle(ThemeId));
	RefreshCues();
	return true;
}

bool AGFRunwaySite::TrySelectLook(APawn* Pawn, FName LookId)
{
	if (!HasAuthority() || !Usable(Pawn, PlayerOf(Pawn)))
	{
		return false;
	}
	if (!IsLook(LookId))
	{
		LastPrompt = TEXT("Look: unknown");
		return false;
	}
	SelectedLook = LookId;
	bLookChosen = true;
	AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	AGFPlayerState* PS = PlayerOf(Pawn);
	const bool bDuringShow = Stage && Stage->GetPhase() == EGFRunwayPhase::Dressing && PS;
	const bool bWorn = bDuringShow && ApplyLook(PS->PlayerKey);
	if (bDuringShow && !bWorn)
	{
		LastPrompt = FString::Printf(TEXT("Look: %s, outfit refused"), *LookTitle(LookId));
	}
	else
	{
		// The wardrobe lends the catalog look. Nothing is taken from the bag.
		const TCHAR* How = bWorn ? TEXT("Wearing") : TEXT("Granted");
		LastPrompt = FString::Printf(TEXT("Look: %s. %s %s."), *LookTitle(LookId), How, *LookPieces(LookId));
	}
	RefreshCues();
	return true;
}

bool AGFRunwaySite::TryStart(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return false;
	}
	AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	AGFPlayerState* PS = PlayerOf(Pawn);
	if (!Stage || !PS || PS->PlayerKey.IsEmpty())
	{
		LastPrompt = TEXT("Start: no player");
		return false;
	}
	if (!PS->bCharacterReady)
	{
		LastPrompt = TEXT("Start: still joining");
		return false;
	}
	const EGFRunwayPhase Phase = Stage->GetPhase();
	if (Phase != EGFRunwayPhase::Idle && Phase != EGFRunwayPhase::Results)
	{
		LastPrompt = TEXT("Start: show already on");
		return false;
	}
	// A suggested name is not a choice. Say so on the arch instead of opening the show undressed.
	if (!bThemeChosen || !FGFFashionCatalog::FindTheme(SelectedTheme))
	{
		LastPrompt = TEXT("Pick a theme first");
		RefreshCues();
		return false;
	}
	if (!bLookChosen || !IsLook(SelectedLook))
	{
		LastPrompt = TEXT("Pick a look first");
		RefreshCues();
		return false;
	}

	// Short enough to walk in one visit. The stage's 75/8/4 defaults stay until a show starts here.
	Stage->DressingSeconds = PlayDressingSeconds;
	Stage->WalkSeconds = PlayWalkSeconds;
	Stage->JudgingSeconds = PlayJudgingSeconds;

	TArray<FString> Keys;
	if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		for (const APlayerState* Other : GS->PlayerArray)
		{
			const AGFPlayerState* GFPS = Cast<AGFPlayerState>(Other);
			if (GFPS && GFPS->bCharacterReady && !GFPS->PlayerKey.IsEmpty())
			{
				Keys.AddUnique(GFPS->PlayerKey);
			}
		}
	}
	Keys.AddUnique(PS->PlayerKey);

	const EGFRunwayRequestResult Started = Stage->StartShowFor(SelectedTheme, Keys, static_cast<int32>(FPlatformTime::Cycles()));
	if (Started != EGFRunwayRequestResult::Ok)
	{
		LastPrompt = FString::Printf(TEXT("Start: %s"), *ResultName(Started));
		return false;
	}
	bTravelled = false;
	ResultsShownAt = -1.0;
	const bool bDressed = ApplyLook(PS->PlayerKey);
	LastPrompt = bDressed
		? FString::Printf(TEXT("Start: %s in %s"), *LookTitle(SelectedLook), *ThemeTitle(SelectedTheme))
		: TEXT("Start: show on, outfit refused");
	RefreshCues();
	return true;
}

bool AGFRunwaySite::TryFinish(APawn* Pawn)
{
	if (!HasAuthority())
	{
		return false;
	}
	AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	AGFPlayerState* PS = PlayerOf(Pawn);
	if (!Stage || !PS || PS->PlayerKey.IsEmpty())
	{
		LastPrompt = TEXT("Finish: no player");
		return false;
	}
	if (Stage->GetPhase() == EGFRunwayPhase::Idle)
	{
		LastPrompt = TEXT("Finish: walk the start arch first");
		return false;
	}
	if (Stage->GetPhase() == EGFRunwayPhase::Dressing)
	{
		const FGFRunwayContestant* C = Stage->FindContestant(PS->PlayerKey);
		if (!C || C->Outfit.IsEmpty())
		{
			ApplyLook(PS->PlayerKey);
		}
		Stage->HandleSetReady(PS->PlayerKey, true);
	}
	if (Stage->GetPhase() != EGFRunwayPhase::Results)
	{
		// One step crosses the remaining walks and the judging. The finish arch ends the show.
		Stage->Step(Stage->WalkSeconds * 8.f + Stage->JudgingSeconds + 1.f);
	}
	const bool bDone = Stage->GetPhase() == EGFRunwayPhase::Results;
	LastPrompt = bDone ? TEXT("Finish: scores are up") : TEXT("Finish: the show is still going");
	RefreshCues();
	return bDone;
}

bool AGFRunwaySite::TryReturn(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn)
	{
		return false;
	}
	if (!Usable(Pawn, PlayerOf(Pawn)))
	{
		return false;
	}
	TravelToHub();
	LastPrompt = TEXT("Back to the hub.");
	return true;
}

void AGFRunwaySite::TravelToHub()
{
	if (bTravelled)
	{
		return;
	}
	bTravelled = true;
	const FString Map = FGFMiniGameCatalog::HubMap().ToString();
	LastTravelMap = Map;
	UE_LOG(LogGF, Log, TEXT("Runway: back to the hub (%s)"), *Map);
	if (TravelOverride)
	{
		TravelOverride(Map);
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	const ENetMode Mode = GetNetMode();
	if (Mode == NM_ListenServer)
	{
		World->ServerTravel(Map + TEXT("?listen"));
	}
	else if (Mode == NM_DedicatedServer)
	{
		World->ServerTravel(Map);
	}
	else
	{
		UGameplayStatics::OpenLevel(this, FName(*Map));
	}
}

void AGFRunwaySite::SeatFarPawns()
{
	UWorld* World = GetWorld();
	AGFRunwayStage* Stage = AGFRunwayStage::Find(World);
	if (!World || !Stage || Stage->GetPhase() != EGFRunwayPhase::Idle)
	{
		return;
	}
	const FVector Stand = GetActorTransform().TransformPosition(FVector(80.f, 0.f, 140.f));
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Pawn = *It;
		if (!Usable(Pawn, PlayerOf(Pawn)))
		{
			continue;
		}
		const TWeakObjectPtr<APawn> Key(Pawn);
		if (Seated.Contains(Key))
		{
			continue;
		}
		if (FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= 3000.f && Pawn->GetActorLocation().Z > -50.f && Pawn->GetActorLocation().Z < 2000.f)
		{
			Seated.Add(Key);
			continue;
		}
		Pawn->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
		// Face down the catwalk, toward the wardrobe and the start arch.
		Pawn->SetActorRotation(GetActorRotation());
		Seated.Add(Key);
	}
}

FString AGFRunwaySite::GetStartCue() const
{
	const AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	const EGFRunwayPhase Phase = Stage ? Stage->GetPhase() : EGFRunwayPhase::Idle;
	if (Phase != EGFRunwayPhase::Idle && Phase != EGFRunwayPhase::Results)
	{
		return TEXT("Show is on. Walk to Finish.");
	}
	if (!bThemeChosen)
	{
		return TEXT("Pick a theme first");
	}
	if (!bLookChosen)
	{
		return TEXT("Pick a look first");
	}
	return TEXT("Walk through to start the show");
}

FString AGFRunwaySite::GetWearingCue() const
{
	if (!bLookChosen)
	{
		return TEXT("Stand on Garden or Explorer");
	}
	return FString::Printf(TEXT("Wearing %s"), *LookTitle(SelectedLook));
}

void AGFRunwaySite::RefreshCues()
{
	auto Set = [](UTextRenderComponent* Label, const FString& Text)
	{
		if (!Label || Label->Text.ToString() == Text)
		{
			return;
		}
		Label->SetText(FText::FromString(Text));
	};
	Set(StartCue, GetStartCue());
	Set(WearingCue, GetWearingCue());
}

void AGFRunwaySite::GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	OutLines.Add(TEXT("RUNWAY"));

	const AGFRunwayStage* Stage = AGFRunwayStage::Find(GetWorld());
	const EGFRunwayPhase Phase = Stage ? Stage->GetPhase() : EGFRunwayPhase::Idle;
	if (Phase == EGFRunwayPhase::Results && Stage)
	{
		OutLines.Add(TEXT("Scores are up."));
		for (const FGFRunwayContestant& C : Stage->GetState().Contestants)
		{
			FString Pay;
			for (const FGFItemStack& S : C.Rewards)
			{
				if (!Pay.IsEmpty())
				{
					Pay += TEXT(", ");
				}
				Pay += FString::Printf(TEXT("%s x%d"), *S.ItemId.ToString(), S.Count);
			}
			if (Pay.IsEmpty())
			{
				Pay = TEXT("no purse");
			}
			OutLines.Add(FString::Printf(TEXT("%s  rank %d  %s  %.1f  %s"), *C.PlayerKey, C.Rank, *TierName(C.Score.Tier), C.Score.Total, *Pay));
		}
		OutLines.Add(TEXT("The Hub arch takes you back."));
	}
	else
	{
		// 0 theme, 1 look, 2 start arch, 3 finish arch. One of them is the step to do next.
		const int32 Step = !bThemeChosen ? 0 : (!bLookChosen ? 1 : (Phase == EGFRunwayPhase::Idle ? 2 : 3));
		const TCHAR* Steps[] = {
			TEXT("Pick a theme."),
			TEXT("Pick a look on the wardrobe."),
			TEXT("Walk through the Start arch."),
			TEXT("Walk to the Finish arch."),
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
		{
			OutLines.Add(FString::Printf(TEXT("%s%s"), i == Step ? TEXT("> ") : TEXT("  "), Steps[i]));
		}
		if (!bLookChosen)
		{
			OutLines.Add(TEXT("Look: none. The wardrobe lends Garden or Explorer. Pick a look first."));
		}
		else
		{
			OutLines.Add(FString::Printf(TEXT("Look: %s. Wearing %s."), *LookTitle(SelectedLook), *LookPieces(SelectedLook)));
		}
		if (bThemeChosen)
		{
			OutLines.Add(FString::Printf(TEXT("Theme: %s"), *ThemeTitle(SelectedTheme)));
		}
		else
		{
			OutLines.Add(TEXT("Theme: not chosen."));
		}
		OutLines.Add(TEXT("The Hub arch leaves."));
		const int32 Left = Stage ? Stage->GetState().PhaseSecondsLeft : 0;
		OutLines.Add(FString::Printf(TEXT("Phase: %s (%ds)"), *PhaseName(Phase), Left));
	}
	if (PS && !PS->PlayerKey.IsEmpty())
	{
		OutLines.Add(FString::Printf(TEXT("You: %s"), *PS->PlayerKey));
	}
	if (!LastPrompt.IsEmpty())
	{
		OutLines.Add(LastPrompt);
	}
}

void AGFRunwaySite::PollSpots()
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
		const FName Name = TriggerNames.IsValidIndex(i) ? TriggerNames[i] : NAME_None;
		TSet<TWeakObjectPtr<APawn>> Inside;
		TArray<AActor*> Over;
		Box->GetOverlappingActors(Over, APawn::StaticClass());
		for (AActor* Actor : Over)
		{
			APawn* Pawn = Cast<APawn>(Actor);
			if (!Pawn || !Usable(Pawn, PlayerOf(Pawn)))
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
			if (Verb == VerbTheme)
			{
				TrySelectTheme(Pawn, Name);
			}
			else if (Verb == VerbLook)
			{
				TrySelectLook(Pawn, Name);
			}
			else if (Verb == VerbStart)
			{
				TryStart(Pawn);
			}
			else if (Verb == VerbFinish)
			{
				TryFinish(Pawn);
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
