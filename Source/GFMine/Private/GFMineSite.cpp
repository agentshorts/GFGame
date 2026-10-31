#include "GFMineSite.h"

#include "GFGameState.h"
#include "GFInventoryComponent.h"
#include "GFMineDirector.h"
#include "GFMineTypes.h"
#include "GFMiniGameCatalog.h"
#include "GFPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
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

	static const FName VerbCapture(TEXT("Capture"));
	static const FName VerbAssign(TEXT("Assign"));
	static const FName VerbHaul(TEXT("Haul"));
	static const FName VerbRecall(TEXT("Recall"));
	static const FName VerbBuy(TEXT("Buy"));
	static const FName VerbStatus(TEXT("Status"));
	static const FName VerbReturn(TEXT("Return"));

	static const FName RoomShallows(TEXT("Shallows"));
	static const FName RoomCrystal(TEXT("CrystalGallery"));
	static const FName RoomGlow(TEXT("GlowGrotto"));

	int32 StackTotal(const TArray<FGFItemStack>& Stacks)
	{
		int32 N = 0;
		for (const FGFItemStack& S : Stacks)
		{
			N += S.Count;
		}
		return N;
	}

	FString PrettyRoom(FName Room)
	{
		if (Room == RoomShallows) return TEXT("Shallows");
		if (Room == RoomCrystal) return TEXT("Crystal Gallery");
		if (Room == RoomGlow) return TEXT("Glow Grotto");
		return Room.ToString();
	}

	FString StateName(EGFPebblingState State)
	{
		if (const UEnum* E = StaticEnum<EGFPebblingState>())
		{
			return E->GetNameStringByValue(static_cast<int64>(State));
		}
		return TEXT("?");
	}

	const TCHAR* UntamedCrewLine = TEXT("Crew: shy until your first tame. Capture stays locked.");
	const TCHAR* UntamedValleyLine = TEXT("Tame a dino in the Valley first. The Valley gate is on the hub, then come back.");
	const TCHAR* UntamedCapturePrompt = TEXT("Capture: Locked. Tame a dino in the Valley first (hub gate), then come back.");
}

AGFMineSite::AGFMineSite()
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

void AGFMineSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFMineSite, bReady);
	DOREPLIFETIME(AGFMineSite, LastPrompt);
}

void AGFMineSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() || bReady)
	{
		BuildIfNeeded();
	}
}

void AGFMineSite::OnRep_Ready()
{
	if (bReady)
	{
		BuildIfNeeded();
	}
}

void AGFMineSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		PollSpots();
	}
	// Clients build their own meshes. The local crew's unlocks have to catch up after replication.
	if (const AGFPlayerState* PS = LocalCrewPlayer())
	{
		const uint32 Key = CrewTintKey(PS->Progress.Crew);
		if (!bRoomTintKeyValid || Key != RoomTintKey)
		{
			SyncRoomTints(PS->Progress.Crew);
		}
	}
}

AGFMineSite* AGFMineSite::Get(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGFMineSite> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool AGFMineSite::IsPlayerNear(const APawn* Pawn) const
{
	return Pawn && FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) <= NearRadiusCm;
}

FVector AGFMineSite::GetHubReturnLocation() const
{
	const FGFMiniGameEntry* Entry = FGFMiniGameCatalog::Find(TEXT("Mine"));
	const FVector Rime = (Entry && Entry->bHasPad) ? GetActorLocation() - Entry->PadOffset : FVector::ZeroVector;
	return Rime + FVector(0.f, 0.f, HubStandZCm);
}

bool AGFMineSite::HasVerb(FName Verb) const
{
	return TriggerVerbs.Contains(Verb);
}

UBoxComponent* AGFMineSite::GetVerbTrigger(FName Verb) const
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

UBoxComponent* AGFMineSite::GetRoomTrigger(FName Room) const
{
	for (int32 i = 0; i < Triggers.Num() && i < TriggerRooms.Num(); ++i)
	{
		if (TriggerVerbs.IsValidIndex(i) && TriggerVerbs[i] == VerbAssign && TriggerRooms[i] == Room)
		{
			return Triggers[i];
		}
	}
	return nullptr;
}

void AGFMineSite::BuildIfNeeded()
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

void AGFMineSite::ApplyMeshTint(UStaticMeshComponent* Mesh, const FLinearColor& Color) const
{
	if (!Mesh)
	{
		return;
	}
	if (UMaterialInstanceDynamic* Tint = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)))
	{
		Tint->SetVectorParameterValue(ColorParam, Color);
		return;
	}
	if (BaseMaterial)
	{
		if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(BaseMaterial, Mesh))
		{
			Tint->SetVectorParameterValue(ColorParam, Color);
			Mesh->SetMaterial(0, Tint);
		}
	}
}

UStaticMeshComponent* AGFMineSite::AddBlock(const FVector& LocalCenter, const FVector& SizeCm, const FLinearColor& Color, bool bBlock)
{
	if (!CubeMesh)
	{
		return nullptr;
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
	ApplyMeshTint(Mesh, Color);
	Mesh->RegisterComponent();
	return Mesh;
}

UTextRenderComponent* AGFMineSite::AddLabel(const FString& Text, const FVector& Local)
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
	return Label;
}

UBoxComponent* AGFMineSite::AddTrigger(FName Verb, FName Room, const FVector& Local, const FVector& Extent)
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
	TriggerRooms.Add(Room);
	return Box;
}

void AGFMineSite::AddRoom(FName RoomId, const FString& Title, const FVector& Center, const FLinearColor& Color)
{
	RoomIds.Add(RoomId);
	RoomTitles.Add(Title);
	RoomOpenColors.Add(Color);
	RoomShownColors.Add(Color);
	const int32 MeshStart = RoomMeshParts.Num();
	// Floor is paint. The pad slab under the whole site is what holds you up.
	// Tint is corrected in SyncRoomTints once every room exists. Build starts from an empty crew.
	const FVector Sizes[] = {
		FVector(360.f, 360.f, 4.f), FVector(20.f, 400.f, 250.f), FVector(360.f, 20.f, 250.f), FVector(360.f, 20.f, 250.f)
	};
	const FVector Offsets[] = {
		FVector(0.f, 0.f, 2.f), FVector(180.f, 0.f, 125.f), FVector(0.f, 190.f, 125.f), FVector(0.f, -190.f, 125.f)
	};
	const bool Blocks[] = { false, true, true, true };
	for (int32 Piece = 0; Piece < 4; ++Piece)
	{
		if (UStaticMeshComponent* Mesh = AddBlock(Center + Offsets[Piece], Sizes[Piece], Color, Blocks[Piece]))
		{
			RoomMeshParts.Add(Mesh);
		}
	}
	RoomMeshStarts.Add(MeshStart);
	RoomMeshCounts.Add(RoomMeshParts.Num() - MeshStart);
	RoomLabels.Add(AddLabel(Title, Center + FVector(0.f, 0.f, 220.f)));
	RoomSignText.Add(Title);
	AddTrigger(VerbAssign, RoomId, Center + FVector(0.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));
}

void AGFMineSite::BuildVisuals()
{
	CubeMesh = LoadObject<UStaticMesh>(nullptr, CubePath);
	BaseMaterial = LoadObject<UMaterialInterface>(nullptr, MaterialPath);

	// Slab, top at local Z = 0, inside the 40 m pad.
	// Top sits 1 cm above the pad slab so the two floors don't fight for the same surface.
	AddBlock(FVector(0.f, 0.f, -19.f), FVector(3600.f, 3600.f, 40.f), FLinearColor(0.42f, 0.40f, 0.36f), true);

	// Arrival from the hub gate is local (400, 0, 120). Keep that patch empty.
	AddRoom(RoomShallows, TEXT("Shallows"), FVector(1300.f, -650.f, 0.f), FLinearColor(0.72f, 0.62f, 0.42f));
	AddRoom(RoomCrystal, TEXT("Crystal Gallery"), FVector(1300.f, 0.f, 0.f), FLinearColor(0.45f, 0.62f, 0.85f));
	AddRoom(RoomGlow, TEXT("Glow Grotto"), FVector(1300.f, 650.f, 0.f), FLinearColor(0.45f, 0.82f, 0.55f));

	const FVector CaptureAt(1300.f, -1200.f, 0.f);
	AddBlock(CaptureAt + FVector(0.f, 0.f, 2.f), FVector(360.f, 360.f, 4.f), FLinearColor(0.85f, 0.55f, 0.58f), false);
	AddBlock(CaptureAt + FVector(180.f, 0.f, 125.f), FVector(20.f, 400.f, 250.f), FLinearColor(0.85f, 0.55f, 0.58f), true);
	AddBlock(CaptureAt + FVector(0.f, 190.f, 125.f), FVector(360.f, 20.f, 250.f), FLinearColor(0.85f, 0.55f, 0.58f), true);
	AddBlock(CaptureAt + FVector(0.f, -190.f, 125.f), FVector(360.f, 20.f, 250.f), FLinearColor(0.85f, 0.55f, 0.58f), true);
	AddBlock(CaptureAt + FVector(0.f, 0.f, 30.f), FVector(50.f, 50.f, 40.f), FLinearColor(0.95f, 0.62f, 0.68f), false);
	AddLabel(TEXT("Capture"), CaptureAt + FVector(0.f, 0.f, 220.f));
	AddTrigger(VerbCapture, NAME_None, CaptureAt + FVector(0.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));

	AddLabel(TEXT("Haul"), FVector(200.f, 450.f, 160.f));
	AddBlock(FVector(200.f, 450.f, 40.f), FVector(60.f, 60.f, 80.f), FLinearColor(0.55f, 0.40f, 0.28f), true);
	AddTrigger(VerbHaul, NAME_None, FVector(200.f, 450.f, 120.f), FVector(120.f, 120.f, 120.f));

	AddLabel(TEXT("Recall"), FVector(200.f, -450.f, 160.f));
	AddBlock(FVector(200.f, -450.f, 40.f), FVector(60.f, 60.f, 80.f), FLinearColor(0.45f, 0.55f, 0.70f), true);
	AddTrigger(VerbRecall, NAME_None, FVector(200.f, -450.f, 120.f), FVector(120.f, 120.f, 120.f));

	AddLabel(TEXT("Buy"), FVector(550.f, 900.f, 160.f));
	AddBlock(FVector(550.f, 900.f, 40.f), FVector(80.f, 80.f, 80.f), FLinearColor(0.85f, 0.72f, 0.28f), true);
	AddTrigger(VerbBuy, NAME_None, FVector(550.f, 900.f, 120.f), FVector(120.f, 120.f, 120.f));

	AddLabel(TEXT("Status"), FVector(550.f, -900.f, 160.f));
	AddBlock(FVector(550.f, -900.f, 40.f), FVector(80.f, 80.f, 80.f), FLinearColor(0.70f, 0.70f, 0.72f), true);
	AddTrigger(VerbStatus, NAME_None, FVector(550.f, -900.f, 120.f), FVector(120.f, 120.f, 120.f));

	// West side of the pad, toward the hub. Two posts and a lintel.
	AddBlock(FVector(-1100.f, -140.f, 125.f), FVector(40.f, 40.f, 250.f), FLinearColor(0.90f, 0.90f, 0.85f), true);
	AddBlock(FVector(-1100.f, 140.f, 125.f), FVector(40.f, 40.f, 250.f), FLinearColor(0.90f, 0.90f, 0.85f), true);
	AddBlock(FVector(-1100.f, 0.f, 260.f), FVector(40.f, 320.f, 30.f), FLinearColor(0.90f, 0.90f, 0.85f), true);
	AddLabel(TEXT("Hub"), FVector(-1100.f, 0.f, 320.f));
	AddTrigger(VerbReturn, NAME_None, FVector(-1100.f, 0.f, 120.f), FVector(140.f, 140.f, 120.f));

	AddLabel(TEXT("Pebble Mine"), FVector(400.f, 0.f, 280.f));
	// Build has no player yet. Locked rooms stay dim until a crew with the unlock is synced.
	SyncRoomTints(FGFMineCrew());
}

uint32 AGFMineSite::CrewTintKey(const FGFMineCrew& Crew)
{
	uint32 H = HashCombine(GetTypeHash(Crew.Pebblings.Num()), GetTypeHash(Crew.UpgradeIds.Num()));
	for (int32 i = 0; i < Crew.UpgradeIds.Num(); ++i)
	{
		H = HashCombine(H, GetTypeHash(Crew.UpgradeIds[i]));
		const int32 Level = Crew.UpgradeLevels.IsValidIndex(i) ? Crew.UpgradeLevels[i] : 0;
		H = HashCombine(H, GetTypeHash(Level));
	}
	for (const FGFPebbling& P : Crew.Pebblings)
	{
		H = HashCombine(H, GetTypeHash(P.Id));
		H = HashCombine(H, GetTypeHash(P.Room));
	}
	return H;
}

void AGFMineSite::SyncRoomTints(const FGFMineCrew& Crew)
{
	for (int32 i = 0; i < RoomIds.Num(); ++i)
	{
		if (!RoomOpenColors.IsValidIndex(i) || !RoomMeshStarts.IsValidIndex(i) || !RoomMeshCounts.IsValidIndex(i))
		{
			continue;
		}
		const bool bOpen = FGFMineRules::RoomOpen(Crew, RoomIds[i]);
		const FLinearColor Tint = bOpen ? RoomOpenColors[i] : RoomOpenColors[i] * LockedRoomBrightness;
		RoomShownColors[i] = Tint;
		const int32 Start = RoomMeshStarts[i];
		const int32 Count = RoomMeshCounts[i];
		for (int32 m = 0; m < Count; ++m)
		{
			if (RoomMeshParts.IsValidIndex(Start + m))
			{
				ApplyMeshTint(RoomMeshParts[Start + m], Tint);
			}
		}
		const FString Title = RoomTitles.IsValidIndex(i) ? RoomTitles[i] : RoomIds[i].ToString();
		const FString Note = bOpen ? Title : FString::Printf(TEXT("%s (locked)"), *Title);
		const FString Sign = FString::Printf(TEXT("Assign\n%s"), *Note);
		if (RoomSignText.IsValidIndex(i))
		{
			RoomSignText[i] = Sign;
		}
		if (RoomLabels.IsValidIndex(i) && RoomLabels[i])
		{
			RoomLabels[i]->SetText(FText::FromString(Sign));
		}
	}
	RoomTintKey = CrewTintKey(Crew);
	bRoomTintKeyValid = true;
}

FLinearColor AGFMineSite::GetRoomTint(FName RoomId) const
{
	const int32 i = RoomIds.IndexOfByKey(RoomId);
	if (i == INDEX_NONE)
	{
		return FLinearColor::Black;
	}
	if (RoomMeshStarts.IsValidIndex(i) && RoomMeshCounts.IsValidIndex(i))
	{
		const int32 Start = RoomMeshStarts[i];
		const int32 Count = RoomMeshCounts[i];
		for (int32 m = 0; m < Count; ++m)
		{
			const UStaticMeshComponent* Mesh = RoomMeshParts.IsValidIndex(Start + m) ? RoomMeshParts[Start + m].Get() : nullptr;
			const UMaterialInstanceDynamic* Mid = Mesh ? Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)) : nullptr;
			if (!Mid)
			{
				continue;
			}
			FLinearColor Read;
			if (Mid->GetVectorParameterValue(FHashedMaterialParameterInfo(ColorParam), Read))
			{
				return Read;
			}
		}
	}
	return RoomShownColors.IsValidIndex(i) ? RoomShownColors[i] : FLinearColor::Black;
}

FLinearColor AGFMineSite::GetRoomOpenColor(FName RoomId) const
{
	const int32 i = RoomIds.IndexOfByKey(RoomId);
	return RoomOpenColors.IsValidIndex(i) ? RoomOpenColors[i] : FLinearColor::Black;
}

FString AGFMineSite::GetRoomSign(FName RoomId) const
{
	const int32 i = RoomIds.IndexOfByKey(RoomId);
	if (i == INDEX_NONE)
	{
		return FString();
	}
	if (RoomLabels.IsValidIndex(i) && RoomLabels[i])
	{
		return RoomLabels[i]->GetText().ToString();
	}
	return RoomSignText.IsValidIndex(i) ? RoomSignText[i] : FString();
}

void AGFMineSite::NoteCrew(const APawn* Pawn)
{
	if (const AGFPlayerState* PS = PlayerOf(Pawn))
	{
		SyncRoomTints(PS->Progress.Crew);
	}
}

const AGFPlayerState* AGFMineSite::LocalCrewPlayer() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
}

AGFPlayerState* AGFMineSite::PlayerOf(const APawn* Pawn) const
{
	return Pawn ? Pawn->GetPlayerState<AGFPlayerState>() : nullptr;
}

FGFModeResult AGFMineSite::Send(AGFPlayerState* PS, FName Verb, FName Name, const FGuid& Id, float Speed)
{
	if (!PS)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	FGFModeRequest R = FGFModeRequest::Make(AGFMineDirector::ModeName, Verb);
	if (!Name.IsNone())
	{
		R.Names.Add(Name);
	}
	if (Id.IsValid())
	{
		R.Ids.Add(Id);
	}
	if (Verb == VerbCapture)
	{
		R.Floats.Add(Speed);
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

FGFModeResult AGFMineSite::TryCapture(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	const float Speed = Pawn ? Pawn->GetVelocity().Size2D() : 0.f;
	FGFModeResult R = Send(PS, VerbCapture, NAME_None, FGuid(), Speed);
	// Locked is only the first-tame gate. Say where that tame is, in the mine, not as a code name.
	if (!R.bOk && R.Code == FName(TEXT("Locked")))
	{
		LastPrompt = UntamedCapturePrompt;
	}
	NoteCrew(Pawn);
	return R;
}

FGFModeResult AGFMineSite::TryAssign(APawn* Pawn, FName Room)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	FGuid Id;
	if (PS)
	{
		for (const FGFPebbling& P : PS->Progress.Crew.Pebblings)
		{
			if (P.Room != Room)
			{
				Id = P.Id;
				break;
			}
		}
		if (!Id.IsValid() && PS->Progress.Crew.Pebblings.Num() > 0)
		{
			Id = PS->Progress.Crew.Pebblings[0].Id;
		}
	}
	FGFModeResult R = Send(PS, VerbAssign, Room, Id, 0.f);
	NoteCrew(Pawn);
	return R;
}

FGFModeResult AGFMineSite::TryHaul(APawn* Pawn)
{
	// Invalid id: every pebbling with a satchel.
	FGFModeResult R = Send(PlayerOf(Pawn), VerbHaul, NAME_None, FGuid(), 0.f);
	NoteCrew(Pawn);
	return R;
}

FGFModeResult AGFMineSite::TryRecall(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	FGuid Id;
	if (PS)
	{
		for (const FGFPebbling& P : PS->Progress.Crew.Pebblings)
		{
			if (!P.Room.IsNone())
			{
				Id = P.Id;
				break;
			}
		}
		if (!Id.IsValid() && PS->Progress.Crew.Pebblings.Num() > 0)
		{
			Id = PS->Progress.Crew.Pebblings[0].Id;
		}
	}
	FGFModeResult R = Send(PS, VerbRecall, NAME_None, Id, 0.f);
	NoteCrew(Pawn);
	return R;
}

FName AGFMineSite::NextBuyId(const AGFPlayerState* PS) const
{
	if (!PS)
	{
		return GFMineUpgrades::Picks;
	}
	const FName Order[] = {
		GFMineUpgrades::Picks, GFMineUpgrades::Satchels, GFMineUpgrades::Rails, GFMineUpgrades::CrewQuarters,
		GFMineUpgrades::Tunnels, GFMineUpgrades::Lanterns, GFMineUpgrades::AutoHaul, GFMineUpgrades::NightShift
	};
	for (const FName& Id : Order)
	{
		EGFMineOrder Why;
		if (FGFMineRules::NextUpgrade(PS->Progress.Crew, Id, Why))
		{
			return Id;
		}
	}
	return GFMineUpgrades::Picks;
}

bool AGFMineSite::GrantShortfall(AGFPlayerState* PS, FName Upgrade)
{
	AGFGameState* GS = PS ? PS->GetGFGameState() : nullptr;
	if (!PS || !GS || !PS->Inventory || PS->PlayerKey.IsEmpty() || ForemanAdvance.Contains(PS->PlayerKey))
	{
		return false;
	}
	EGFMineOrder Why;
	const FGFMineUpgradeRow* Row = FGFMineRules::NextUpgrade(PS->Progress.Crew, Upgrade, Why);
	if (!Row)
	{
		return false;
	}
	TArray<FGFItemStack> Missing;
	for (const FGFItemStack& Cost : Row->Cost)
	{
		const int32 Have = PS->Inventory->GetCount(Cost.ItemId);
		if (Have < Cost.Count)
		{
			Missing.Add(FGFItemStack(Cost.ItemId, Cost.Count - Have));
		}
	}
	if (Missing.Num() == 0)
	{
		return false;
	}
	if (GS->ServerMint(FGuid::NewGuid(), PS->PlayerKey, Missing, TEXT("Mine.ForemanAdvance")) != EGFTxResult::Applied)
	{
		return false;
	}
	ForemanAdvance.Add(PS->PlayerKey);
	return true;
}

FGFModeResult AGFMineSite::TryBuy(APawn* Pawn)
{
	AGFPlayerState* PS = PlayerOf(Pawn);
	const FName Upgrade = NextBuyId(PS);
	FGFModeResult R = Send(PS, VerbBuy, Upgrade, FGuid(), 0.f);
	// One shortfall, once, so a first upgrade is buyable. Room yields are not touched.
	if (!R.bOk && R.Code == FName(TEXT("CantAfford")) && GrantShortfall(PS, Upgrade))
	{
		R = Send(PS, VerbBuy, Upgrade, FGuid(), 0.f);
		if (R.bOk)
		{
			LastPrompt = FString::Printf(TEXT("Buy: Ok (foreman covered the shortfall for %s)"), *Upgrade.ToString());
		}
	}
	NoteCrew(Pawn);
	return R;
}

FGFModeResult AGFMineSite::TryStatus(APawn* Pawn)
{
	return Send(PlayerOf(Pawn), VerbStatus, NAME_None, FGuid(), 0.f);
}

bool AGFMineSite::TryReturn(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn)
	{
		return false;
	}
	if (!Pawn->IsPlayerControlled() && !PlayerOf(Pawn))
	{
		return false;
	}
	const FVector Where = GetHubReturnLocation();
	Pawn->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
	LastPrompt = TEXT("Back on the hub stand.");
	return true;
}

void AGFMineSite::GetPromptLines(const AGFPlayerState* PS, TArray<FString>& OutLines) const
{
	OutLines.Reset();
	OutLines.Add(TEXT("PEBBLE MINE"));
	OutLines.Add(TEXT("Walk the spots: Capture, Assign, Haul, Recall, Buy, Status."));
	OutLines.Add(TEXT("Capture: stop on the pink spot. Faster than 200 cm/s startles it for 5 s."));
	OutLines.Add(TEXT("Assign: Shallows is open. Crystal Gallery needs Tunnels. Glow Grotto needs Lanterns."));
	OutLines.Add(TEXT("Haul sends satchels home. Recall calls one pebbling back to the mouth."));
	const FName BuyId = NextBuyId(PS);
	const int32 BuyLevel = (PS ? PS->Progress.Crew.Level(BuyId) : 0) + 1;
	EGFMineOrder Why = EGFMineOrder::Ok;
	const FGFMineUpgradeRow* Row = PS
		? FGFMineRules::NextUpgrade(PS->Progress.Crew, BuyId, Why)
		: FGFMineData::Get().Upgrade(BuyId, 1);
	if (Row)
	{
		FString Cost;
		for (const FGFItemStack& C : Row->Cost)
		{
			if (!Cost.IsEmpty())
			{
				Cost += TEXT(", ");
			}
			Cost += FString::Printf(TEXT("%s x%d"), *C.ItemId.ToString(), C.Count);
		}
		OutLines.Add(FString::Printf(TEXT("Buy %s %d: %s. One shortfall is covered once. Drop rates stay normal."), *BuyId.ToString(), BuyLevel, *Cost));
	}
	else
	{
		OutLines.Add(TEXT("Buy: that upgrade is maxed. Drop rates stay normal."));
	}
	OutLines.Add(TEXT("Status reads the last offline haul. The white arch returns to the hub."));
	if (!PS || !PS->Progress.bHasTamed)
	{
		OutLines.Add(UntamedCrewLine);
		OutLines.Add(UntamedValleyLine);
	}
	else if (PS->Progress.Crew.Pebblings.Num() == 0)
	{
		OutLines.Add(TEXT("Crew: none yet. Stop on Capture."));
	}
	else
	{
		for (const FGFPebbling& P : PS->Progress.Crew.Pebblings)
		{
			const FString Where = P.Room.IsNone() ? FString(TEXT("mouth")) : PrettyRoom(P.Room);
			OutLines.Add(FString::Printf(TEXT("%s  %s  %s  satchel %d  %.0fs"),
				*P.Name.ToString(), *Where, *StateName(P.State), StackTotal(P.Carry), P.StateLeft));
		}
	}
	if (!LastPrompt.IsEmpty())
	{
		OutLines.Add(LastPrompt);
	}
}

void AGFMineSite::PollSpots()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SpotInside.SetNum(Triggers.Num());
	const double Now = World->GetTimeSeconds();
	TSet<TWeakObjectPtr<APawn>> Capturing;
	for (int32 i = 0; i < Triggers.Num(); ++i)
	{
		UBoxComponent* Box = Triggers[i];
		if (!Box || !TriggerVerbs.IsValidIndex(i))
		{
			continue;
		}
		const FName Verb = TriggerVerbs[i];
		const FName Room = TriggerRooms.IsValidIndex(i) ? TriggerRooms[i] : NAME_None;
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
			if (Verb == VerbCapture)
			{
				Capturing.Add(Key);
				FStay& Stay = CaptureStays.FindOrAdd(Key);
				if (Stay.bDone)
				{
					continue;
				}
				const float Speed = Pawn->GetVelocity().Size2D();
				if (Speed > FGFMineRules::SneakSpeed)
				{
					if (!Stay.bSentFast)
					{
						TryCapture(Pawn);
						Stay.bSentFast = true;
					}
				}
				else if (Now >= Stay.NextSlow)
				{
					Stay.NextSlow = Now + 0.5;
					const FGFModeResult R = TryCapture(Pawn);
					if (R.bOk || R.Code == FName(TEXT("CrewFull")))
					{
						Stay.bDone = true;
					}
				}
				continue;
			}
			if (SpotInside[i].Contains(Key))
			{
				continue;
			}
			SpotInside[i].Add(Key);
			if (Verb == VerbAssign)
			{
				TryAssign(Pawn, Room);
			}
			else if (Verb == VerbHaul)
			{
				TryHaul(Pawn);
			}
			else if (Verb == VerbRecall)
			{
				TryRecall(Pawn);
			}
			else if (Verb == VerbBuy)
			{
				TryBuy(Pawn);
			}
			else if (Verb == VerbStatus)
			{
				TryStatus(Pawn);
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
	for (auto It = CaptureStays.CreateIterator(); It; ++It)
	{
		if (!Capturing.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}
