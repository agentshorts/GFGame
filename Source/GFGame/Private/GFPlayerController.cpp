#include "GFPlayerController.h"
#include "GFGameState.h"
#include "GFHUD.h"
#include "GFPlayerState.h"
#include "VoxelFinishPainterComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "TimerManager.h"

AGFPlayerController::AGFPlayerController()
{
	Painter = CreateDefaultSubobject<UVoxelFinishPainterComponent>(TEXT("Painter"));
	Painter->MaxPaintsPerSecond = 10.f;
	// Paint reach is the build reach, read at paint time so a change on the game state applies at once.
	// <= 0 means no limit (as for building). Without a game state the painter's own MaxReachCm applies.
	Painter->SetMaxReachProvider([this]()
	{
		const UWorld* World = GetWorld();
		const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
		return GS ? GS->MaxEditReach : (Painter ? Painter->MaxReachCm : 0.f);
	});
}

void AGFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		EnsureHudInput();
	}
}

void AGFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	EnsureHudInput();
}

void AGFPlayerController::EnsureHudInput()
{
	if (!IsLocalController())
	{
		return;
	}
	if (!ChatAction)
	{
		auto Make = [this](const TCHAR* Name)
		{
			UInputAction* Action = NewObject<UInputAction>(this, Name);
			Action->ValueType = EInputActionValueType::Boolean;
			return Action;
		};
		ChatAction = Make(TEXT("IA_GFChat"));
		HintAction = Make(TEXT("IA_GFHint"));
		CloseChatAction = Make(TEXT("IA_GFCloseChat"));
		HudMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_GFHUD"));
		HudMapping->MapKey(ChatAction, EKeys::Enter);
		HudMapping->MapKey(HintAction, EKeys::H);
		HudMapping->MapKey(CloseChatAction, EKeys::Escape);
	}
	if (!bHudMappingAdded)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			// Above the pawn's context (priority 0). These keys are not on that map.
			Sub->AddMappingContext(HudMapping, 1);
			bHudMappingAdded = true;
		}
	}
	if (!bHudActionsBound)
	{
		if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
		{
			EIC->BindAction(ChatAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleChatKey);
			EIC->BindAction(HintAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleHintKey);
			EIC->BindAction(CloseChatAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleCloseChatKey);
			bHudActionsBound = true;
		}
	}
}

void AGFPlayerController::HandleChatKey()
{
	AGFHUD* HUD = Cast<AGFHUD>(GetHUD());
	if (!HUD)
	{
		return;
	}
	if (HUD->IsChatOpen())
	{
		HUD->SubmitChat();
		return;
	}
	// The Enter that just closed the line, or one already waiting to open it.
	if (HUD->ConsumeChatBlock() || bChatOpenQueued)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Next tick, so this Enter is finished before the text box exists and cannot submit an empty line.
	bChatOpenQueued = true;
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bChatOpenQueued = false;
		AGFHUD* Later = Cast<AGFHUD>(GetHUD());
		if (!Later || Later->IsChatOpen() || Later->ConsumeChatBlock())
		{
			return;
		}
		Later->OpenChat();
	}));
}

void AGFPlayerController::HandleHintKey()
{
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->ToggleControlsHint();
	}
}

void AGFPlayerController::HandleCloseChatKey()
{
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		if (HUD->IsChatOpen())
		{
			HUD->CloseChat();
		}
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
