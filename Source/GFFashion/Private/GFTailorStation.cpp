#include "GFTailorStation.h"

#include "GFGameState.h"
#include "GFPlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Font.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

const FName AGFTailorStation::StationId(TEXT("Tailor"));

AGFTailorStation::AGFTailorStation()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	BenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bench"));
	BenchMesh->SetupAttachment(Root);
	BenchMesh->SetMobility(EComponentMobility::Movable);
	BenchMesh->SetCollisionProfileName(TEXT("BlockAll"));

	Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
	Sign->SetupAttachment(Root);
	Sign->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	Sign->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Sign->SetHorizontalAlignment(EHTA_Center);
	Sign->SetText(FText::FromString(TEXT("TAILOR")));
	Sign->SetWorldSize(42.f);
	Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGFTailorStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFTailorStation, LastPrompt);
}

void AGFTailorStation::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		BenchMesh->SetStaticMesh(Cube);
		BenchMesh->SetRelativeScale3D(FVector(1.6f, 0.7f, 0.9f));
		BenchMesh->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
	}
	if (UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField")))
	{
		Sign->SetFont(Font);
	}
}

FString AGFTailorStation::GetSignText() const
{
	return Sign ? Sign->Text.ToString() : FString();
}

EGFWardrobeResult AGFTailorStation::TryCraft(APawn* Pawn, FName PieceId)
{
	if (!HasAuthority())
	{
		LastResult = EGFWardrobeResult::Refused;
		LastPrompt = TEXT("Tailor: not here");
		return LastResult;
	}
	if (!Pawn || FVector::Dist2D(Pawn->GetActorLocation(), GetActorLocation()) > CraftReachCm)
	{
		LastResult = EGFWardrobeResult::Refused;
		LastPrompt = TEXT("Tailor: step closer");
		return LastResult;
	}
	AGFPlayerState* PS = Pawn->GetPlayerState<AGFPlayerState>();
	AGFGameState* GS = GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
	if (!PS || PS->PlayerKey.IsEmpty() || !GS)
	{
		LastResult = EGFWardrobeResult::NoAccount;
		LastPrompt = TEXT("Tailor: still joining");
		return LastResult;
	}
	LastResult = FGFWardrobe::TryCraft(GS->Ledger, GS->Accounts(), PS->PlayerKey, PieceId);
	if (LastResult == EGFWardrobeResult::Granted)
	{
		PS->NotifyLand(FGFWardrobe::ToastFor(PieceId));
		LastPrompt = FGFWardrobe::ToastFor(PieceId);
	}
	else if (LastResult == EGFWardrobeResult::AlreadyOwned)
	{
		LastPrompt = TEXT("Tailor: you already have that");
	}
	else if (LastResult == EGFWardrobeResult::MissingMaterials)
	{
		const FGFFashionPiece* Piece = FGFFashionCatalog::FindPiece(PieceId);
		LastPrompt = Piece ? Piece->SourceLine : FString(TEXT("Tailor: missing materials"));
	}
	else
	{
		LastPrompt = TEXT("Tailor: that piece is not sewn here");
	}
	return LastResult;
}
