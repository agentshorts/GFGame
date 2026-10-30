#include "GFPlayerController.h"
#include "GFPlayerState.h"
#include "VoxelFinishPainterComponent.h"

AGFPlayerController::AGFPlayerController()
{
	Painter = CreateDefaultSubobject<UVoxelFinishPainterComponent>(TEXT("Painter"));
	Painter->MaxReachCm = 600.f;
	Painter->MaxPaintsPerSecond = 10.f;
}

void AGFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AGFPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (AGFPlayerState* PS = GetPlayerState<AGFPlayerState>())
	{
		PS->UploadLocalCharacterIfNeeded();
	}
}
