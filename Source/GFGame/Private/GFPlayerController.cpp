#include "GFPlayerController.h"
#include "GFDefaultInputKeys.h"
#include "GFDino.h"
#include "GFReturnToHub.h"
#include "GFSocialDirector.h"
#include "GFSocialComponent.h"
#include "GFGameState.h"
#include "GFHUD.h"
#include "GFPlayerState.h"
#include "GFTamingComponent.h"
#include "GFShardhop.h"
#include "GFZoneCatalog.h"
#include "EngineUtils.h"
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
	: ReturnKey(DefaultReturnKey())
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
		ShardhopAction = Make(TEXT("IA_GFShardhop"));
		VillainAction = Make(TEXT("IA_GFVillain"));
		ReturnAction = Make(TEXT("IA_GFReturn"));
		PathActions.Reset();
		for (int32 Slot = 0; Slot < 5; ++Slot)
		{
			UInputAction* Action = Make(*FString::Printf(TEXT("IA_GFPath%d"), Slot + 1));
			PathActions.Add(Action);
		}
		HudMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_GFHUD"));
		TArray<FGFMappedKey> Binds;
		FGFDefaultInputKeys::HudKeys(Binds, GetReturnKey());
		auto HudAction = [this](FName Name) -> UInputAction*
		{
			if (Name == TEXT("Chat")) return ChatAction;
			if (Name == TEXT("Hint")) return HintAction;
			if (Name == TEXT("CloseChat")) return CloseChatAction;
			if (Name == TEXT("Shardhop")) return ShardhopAction;
			if (Name == TEXT("Villain")) return VillainAction;
			if (Name == TEXT("Return")) return ReturnAction;
			if (Name == TEXT("Path1") && PathActions.IsValidIndex(0)) return PathActions[0];
			if (Name == TEXT("Path2") && PathActions.IsValidIndex(1)) return PathActions[1];
			if (Name == TEXT("Path3") && PathActions.IsValidIndex(2)) return PathActions[2];
			if (Name == TEXT("Path4") && PathActions.IsValidIndex(3)) return PathActions[3];
			if (Name == TEXT("Path5") && PathActions.IsValidIndex(4)) return PathActions[4];
			return nullptr;
		};
		for (const FGFMappedKey& Bind : Binds)
		{
			if (UInputAction* Action = HudAction(Bind.Action))
			{
				HudMapping->MapKey(Action, Bind.Key);
			}
		}
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
			EIC->BindAction(ShardhopAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleShardhopKey);
			EIC->BindAction(VillainAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleVillainKey);
			EIC->BindAction(ReturnAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleReturnKey);
			if (PathActions.Num() == 5)
			{
				EIC->BindAction(PathActions[0], ETriggerEvent::Started, this, &AGFPlayerController::HandlePath1);
				EIC->BindAction(PathActions[1], ETriggerEvent::Started, this, &AGFPlayerController::HandlePath2);
				EIC->BindAction(PathActions[2], ETriggerEvent::Started, this, &AGFPlayerController::HandlePath3);
				EIC->BindAction(PathActions[3], ETriggerEvent::Started, this, &AGFPlayerController::HandlePath4);
				EIC->BindAction(PathActions[4], ETriggerEvent::Started, this, &AGFPlayerController::HandlePath5);
			}
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
		else if (HUD->IsAimSettingsOpen())
		{
			HUD->CloseAimSettings();
		}
		else
		{
			HUD->TogglePauseMenu();
		}
	}
}

void AGFPlayerController::HandleReturnKey()
{
	if (const AGFHUD* HUD = Cast<AGFHUD>(GetHUD()); HUD && HUD->IsChatOpen())
	{
		return;
	}
	RequestReturnToHub(bReturnConfirmOpen);
}

void AGFPlayerController::SetReturnKey(FKey Key)
{
	ReturnKey = Key.IsValid() ? Key : DefaultReturnKey();
	if (HudMapping && ReturnAction)
	{
		HudMapping->UnmapAllKeysFromAction(ReturnAction);
		HudMapping->MapKey(ReturnAction, ReturnKey);
	}
}

void AGFPlayerController::RequestReturnToHub(bool bConfirmed)
{
	SubmitReturn(bConfirmed, false);
}

void AGFPlayerController::RequestReturnFromMenu(bool bConfirmed)
{
	SubmitReturn(bConfirmed, true);
}

void AGFPlayerController::SubmitReturn(bool bConfirmed, bool bFromMenu)
{
	if (!GetPawn())
	{
		return;
	}
	if (HasAuthority())
	{
		ServerRequestReturnToHub_Implementation(bConfirmed, bFromMenu);
	}
	else
	{
		ServerRequestReturnToHub(bConfirmed, bFromMenu);
	}
}

void AGFPlayerController::ServerRequestReturnToHub_Implementation(bool bConfirmed, bool bFromMenu)
{
	if (!GetPawn())
	{
		return;
	}
	const FGFReturnResult Result = bFromMenu
		? FGFReturnToHub::FromMenu(GetWorld(), GetPawn(), bConfirmed)
		: FGFReturnToHub::FromKey(GetWorld(), GetPawn(), bConfirmed);
	if (Result.bNeedsConfirm)
	{
		ShowReturnConfirm(Result.Prompt);
		return;
	}
	if (Result.bMoved)
	{
		bReturnConfirmOpen = false;
		ReturnPrompt.Reset();
		if (IsLocalController())
		{
			if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
			{
				HUD->ClosePauseMenu();
			}
		}
		else
		{
			ClientReturnFinished();
		}
	}
}

void AGFPlayerController::ClientReturnFinished_Implementation()
{
	bReturnConfirmOpen = false;
	ReturnPrompt.Reset();
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->ClosePauseMenu();
	}
}

void AGFPlayerController::ShowReturnConfirm(const FString& Prompt)
{
	bReturnConfirmOpen = true;
	ReturnPrompt = Prompt;
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->OpenPauseMenu();
	}
	if (!IsLocalController())
	{
		ClientPromptReturnConfirm(Prompt);
	}
}

void AGFPlayerController::ClientPromptReturnConfirm_Implementation(const FString& Prompt)
{
	bReturnConfirmOpen = true;
	ReturnPrompt = Prompt;
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->OpenPauseMenu();
	}
}

void AGFPlayerController::CancelReturnConfirm()
{
	bReturnConfirmOpen = false;
	ReturnPrompt.Reset();
}

void AGFPlayerController::HandleShardhopKey()
{
	if (const AGFHUD* HUD = Cast<AGFHUD>(GetHUD()); HUD && HUD->IsChatOpen())
	{
		return;
	}
	const AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	const APawn* MyPawn = GetPawn();
	if (!PS || !MyPawn)
	{
		return;
	}
	const FName Dest = FGFShardhop::PickDestination(FGFZoneCatalog::Get(), PS->Progress.UnlockedShardstones, MyPawn->GetActorLocation(), GetControlRotation().Yaw);
	if (!Dest.IsNone())
	{
		RequestShardhop(Dest);
	}
}

void AGFPlayerController::SendPathSlot(int32 Slot)
{
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	if (PS && !PS->Progress.bPathChosen)
	{
		PS->ServerChoosePath(static_cast<uint8>(Slot));
	}
}

void AGFPlayerController::HandlePath1() { SendPathSlot(0); }
void AGFPlayerController::HandlePath2() { SendPathSlot(1); }
void AGFPlayerController::HandlePath3() { SendPathSlot(2); }
void AGFPlayerController::HandlePath4() { SendPathSlot(3); }
void AGFPlayerController::HandlePath5() { SendPathSlot(4); }

void AGFPlayerController::HandleVillainKey()
{
	if (const AGFHUD* HUD = Cast<AGFHUD>(GetHUD()); HUD && HUD->IsChatOpen())
	{
		return;
	}
	ServerToggleVillain();
}

void AGFPlayerController::ServerToggleVillain_Implementation()
{
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	if (!PS)
	{
		return;
	}
	if (UGFSocialComponent* Social = AGFSocialDirector::GetSocial(GetWorld(), false))
	{
		Social->ServerToggleVillain(PS);
	}
}

void AGFPlayerController::RequestShardhop(FName DestId)
{
	if (HasAuthority())
	{
		ServerRequestShardhop_Implementation(DestId);
	}
	else
	{
		ServerRequestShardhop(DestId);
	}
}

void AGFPlayerController::ServerRequestShardhop_Implementation(FName DestId)
{
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!PS || !MyPawn || !World)
	{
		return;
	}
	FGFShardhopView View;
	View.Location = MyPawn->GetActorLocation();
	View.bVillainMode = PS->IsVillainMode();
	View.NowSeconds = World->GetTimeSeconds();
	View.LastPvPDamageSeconds = PS->GetLastPvPDamageSeconds();
	View.DamageSerial = PS->GetDamageSerial();
	View.Unlocked = PS->Progress.UnlockedShardstones;
	FGFShardhopChannel Channel;
	Channel.bActive = bShardhopActive;
	Channel.DestId = ShardhopDest;
	Channel.StartedAt = ShardhopStartedAt;
	Channel.DamageSerialAtStart = ShardhopSerial;
	const EGFShardhopResult Result = FGFShardhop::TryStart(FGFZoneCatalog::Get(), View, DestId, Channel);
	bShardhopActive = Channel.bActive;
	ShardhopDest = Channel.DestId;
	ShardhopStartedAt = Channel.StartedAt;
	ShardhopSerial = Channel.DamageSerialAtStart;
	if (Result != EGFShardhopResult::Started)
	{
		const FString Why = FGFShardhop::DenyText(Result);
		if (!Why.IsEmpty())
		{
			PS->NotifyLand(Why);
		}
		return;
	}
	World->GetTimerManager().SetTimer(ShardhopTimer, this, &AGFPlayerController::TickShardhop, 0.1f, true);
}

void AGFPlayerController::TickShardhop()
{
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !bShardhopActive || !PS || !MyPawn || !World)
	{
		bShardhopActive = false;
		if (World)
		{
			World->GetTimerManager().ClearTimer(ShardhopTimer);
		}
		return;
	}
	FGFShardhopView View;
	View.Location = MyPawn->GetActorLocation();
	View.bVillainMode = PS->IsVillainMode();
	View.NowSeconds = World->GetTimeSeconds();
	View.LastPvPDamageSeconds = PS->GetLastPvPDamageSeconds();
	View.DamageSerial = PS->GetDamageSerial();
	View.Unlocked = PS->Progress.UnlockedShardstones;
	FGFShardhopChannel Channel;
	Channel.bActive = bShardhopActive;
	Channel.DestId = ShardhopDest;
	Channel.StartedAt = ShardhopStartedAt;
	Channel.DamageSerialAtStart = ShardhopSerial;
	const FName DestId = Channel.DestId;
	const EGFShardhopResult Result = FGFShardhop::Tick(FGFZoneCatalog::Get(), View, Channel);
	bShardhopActive = Channel.bActive;
	ShardhopDest = Channel.DestId;
	ShardhopStartedAt = Channel.StartedAt;
	ShardhopSerial = Channel.DamageSerialAtStart;
	if (Result == EGFShardhopResult::Running)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(ShardhopTimer);
	if (Result == EGFShardhopResult::Completed)
	{
		FinishShardhop(DestId);
	}
}

void AGFPlayerController::FinishShardhop(FName DestId)
{
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	const FGFShardstoneRow* Stone = FGFZoneCatalog::Get().FindStone(DestId);
	if (!PS || !MyPawn || !World || !Stone)
	{
		return;
	}
	const FVector From = MyPawn->GetActorLocation();
	const int32 Slot = FGFShardhop::SlotOf(World->GetGameState(), PS);
	const FVector At = FGFShardhop::ArrivalPoint(*Stone, Slot);
	MyPawn->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
	int32 Brought = 0;
	for (TActorIterator<AGFDino> It(World); It; ++It)
	{
		const UGFTamingComponent* Taming = It->Taming;
		if (!Taming)
		{
			continue;
		}
		const float Dist = FVector::Dist2D(It->GetActorLocation(), From);
		if (!FGFShardhop::ShouldBringCompanion(Taming->GetTameState() == EGFTameState::Tamed, Taming->GetOwnerKey(), PS->PlayerKey, Dist))
		{
			continue;
		}
		++Brought;
		const FVector Beside = At + FVector(80.f, static_cast<float>(Brought) * 40.f, 0.f);
		It->SetActorLocation(Beside, false, nullptr, ETeleportType::TeleportPhysics);
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
