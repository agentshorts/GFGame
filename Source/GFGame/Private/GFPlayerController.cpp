#include "GFPlayerController.h"
#include "GFDefaultInputKeys.h"
#include "GFElderShop.h"
#include "GFInventoryComponent.h"
#include "GFQuestSubsystem.h"
#include "GFDino.h"
#include "GFReturnToHub.h"
#include "GFSocialDirector.h"
#include "GFSocialComponent.h"
#include "GFGameState.h"
#include "GFHUD.h"
#include "GFHotbarRules.h"
#include "GFCharacter.h"
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
		InventoryAction = Make(TEXT("IA_GFInventory"));
		SlotActions.Reset();
		for (int32 Slot = 0; Slot < FGFHotbarRules::SlotCount; ++Slot)
		{
			UInputAction* Action = Make(*FString::Printf(TEXT("IA_GFSlot%d"), Slot + 1));
			SlotActions.Add(Action);
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
			if (Name == TEXT("Inventory")) return InventoryAction;
			FString Text = Name.ToString();
			if (Text.RemoveFromStart(TEXT("Slot")))
			{
				const int32 Index = FCString::Atoi(*Text) - 1;
				if (SlotActions.IsValidIndex(Index))
				{
					return SlotActions[Index];
				}
			}
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
			EIC->BindAction(InventoryAction, ETriggerEvent::Started, this, &AGFPlayerController::HandleInventoryKey);
			if (SlotActions.Num() == FGFHotbarRules::SlotCount)
			{
				EIC->BindAction(SlotActions[0], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot1);
				EIC->BindAction(SlotActions[1], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot2);
				EIC->BindAction(SlotActions[2], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot3);
				EIC->BindAction(SlotActions[3], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot4);
				EIC->BindAction(SlotActions[4], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot5);
				EIC->BindAction(SlotActions[5], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot6);
				EIC->BindAction(SlotActions[6], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot7);
				EIC->BindAction(SlotActions[7], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot8);
				EIC->BindAction(SlotActions[8], ETriggerEvent::Started, this, &AGFPlayerController::HandleSlot9);
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
		HUD->HandleEscape();
	}
}

void AGFPlayerController::HandleReturnKey()
{
	if (const AGFHUD* HUD = Cast<AGFHUD>(GetHUD()); HUD && HUD->IsChatOpen())
	{
		return;
	}
	RequestReturnToHub(IsReturnConfirmOpen());
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
		ClearReturnPrompt();
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
	ClearReturnPrompt();
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->ClosePauseMenu();
	}
}

void AGFPlayerController::ShowReturnConfirm(const FString& Prompt)
{
	ReturnPrompt = Prompt;
	if (IsLocalController())
	{
		if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
		{
			HUD->SetHudModal(EGFHudModalEvent::OpenConfirm);
		}
	}
	else
	{
		ClientPromptReturnConfirm(Prompt);
	}
}

void AGFPlayerController::ClientPromptReturnConfirm_Implementation(const FString& Prompt)
{
	ReturnPrompt = Prompt;
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->SetHudModal(EGFHudModalEvent::OpenConfirm);
	}
}

void AGFPlayerController::CancelReturnConfirm()
{
	ClearReturnPrompt();
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->SetHudModal(EGFHudModalEvent::CancelConfirm);
	}
}

void AGFPlayerController::ClearReturnPrompt()
{
	ReturnPrompt.Reset();
}

bool AGFPlayerController::IsReturnConfirmOpen() const
{
	const AGFHUD* HUD = Cast<AGFHUD>(GetHUD());
	return HUD && FGFHudModalRules::ConfirmsReturn(HUD->GetHudModal());
}

void AGFPlayerController::ApplyShownHeat(EGFTheftHeat State, float HuntLeft)
{
	ShownHeat = State;
	ShownHuntLeft = FMath::Max(0.f, HuntLeft);
}

void AGFPlayerController::NotifyTheftHeat(EGFTheftHeat State, float HuntLeft)
{
	if (!HasAuthority())
	{
		return;
	}
	const float Seconds = FMath::Max(0.f, HuntLeft);
	const int32 NextSecond = FMath::CeilToInt(Seconds);
	const int32 HaveSecond = FMath::CeilToInt(FMath::Max(0.f, ShownHuntLeft));
	if (ShownHeat == State && NextSecond == HaveSecond)
	{
		return;
	}
	ApplyShownHeat(State, Seconds);
	if (!IsLocalController())
	{
		ClientSetTheftHeat(static_cast<uint8>(State), Seconds);
	}
}

void AGFPlayerController::ClientSetTheftHeat_Implementation(uint8 State, float HuntLeft)
{
	const uint8 Max = static_cast<uint8>(EGFTheftHeat::Hunted);
	const EGFTheftHeat Decoded = State <= Max ? static_cast<EGFTheftHeat>(State) : EGFTheftHeat::Clear;
	ApplyShownHeat(Decoded, HuntLeft);
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

void AGFPlayerController::HandleInventoryKey()
{
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()); HUD && !HUD->IsChatOpen())
	{
		HUD->ToggleInventory();
	}
}

void AGFPlayerController::SendSlotKey(int32 KeyIndex)
{
	AGFHUD* HUD = Cast<AGFHUD>(GetHUD());
	if (HUD && HUD->IsInventoryOpen())
	{
		// In the bag a number key puts the hovered or held item on that slot.
		HUD->InventoryNumberKey(KeyIndex);
		return;
	}
	if (HUD && (HUD->IsChatOpen() || FGFHudModalRules::HoldsPawn(HUD->GetHudModal(), HUD->GetHudRestore())))
	{
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	const bool bPathPending = PS && !PS->Progress.bPathChosen;
	switch (FGFHotbarRules::RouteNumberKey(KeyIndex, bPathPending))
	{
	case EGFNumberKeyUse::Path:
		PS->ServerChoosePath(static_cast<uint8>(KeyIndex));
		break;
	case EGFNumberKeyUse::Hotbar:
		if (AGFCharacter* GFChar = Cast<AGFCharacter>(GetPawn()))
		{
			GFChar->SelectHotbarSlot(KeyIndex);
		}
		break;
	default:
		break;
	}
}

void AGFPlayerController::HandleSlot1() { SendSlotKey(0); }
void AGFPlayerController::HandleSlot2() { SendSlotKey(1); }
void AGFPlayerController::HandleSlot3() { SendSlotKey(2); }
void AGFPlayerController::HandleSlot4() { SendSlotKey(3); }
void AGFPlayerController::HandleSlot5() { SendSlotKey(4); }
void AGFPlayerController::HandleSlot6() { SendSlotKey(5); }
void AGFPlayerController::HandleSlot7() { SendSlotKey(6); }
void AGFPlayerController::HandleSlot8() { SendSlotKey(7); }
void AGFPlayerController::HandleSlot9() { SendSlotKey(8); }

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

void AGFPlayerController::ShowElderToast(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	if (IsLocalController())
	{
		if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
		{
			HUD->PushToast(Text);
		}
	}
	else
	{
		ClientElderToast(Text);
	}
}

void AGFPlayerController::ClientElderToast_Implementation(const FString& Text)
{
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->PushToast(Text);
	}
}

void AGFPlayerController::OpenElderMenu(AGFElder* Elder)
{
	if (!HasAuthority() || !Elder)
	{
		return;
	}
	ElderSession = Elder;
	ElderSessionId = FGuid::NewGuid();
	ElderPhase = FGFElderSessionRules::Transition(ElderPhase, EGFElderSessionEvent::Opened);
	if (IsLocalController())
	{
		if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
		{
			HUD->OpenElderMenu(Elder, Elder->TownId);
		}
	}
	else
	{
		ClientOpenElderMenu(Elder, Elder->TownId);
	}
}

void AGFPlayerController::ClientOpenElderMenu_Implementation(AGFElder* Elder, FName TownId)
{
	if (AGFHUD* HUD = Cast<AGFHUD>(GetHUD()))
	{
		HUD->OpenElderMenu(Elder, TownId);
	}
}

void AGFPlayerController::RequestCloseElderMenu()
{
	if (HasAuthority())
	{
		ServerCloseElderMenu_Implementation();
	}
	else
	{
		ServerCloseElderMenu();
	}
}

void AGFPlayerController::ServerCloseElderMenu_Implementation()
{
	ElderPhase = FGFElderSessionRules::Transition(ElderPhase, EGFElderSessionEvent::Closed);
	ElderSession.Reset();
	ElderSessionId.Invalidate();
}

bool AGFPlayerController::ElderMenuAllows(FString& OutWhy) const
{
	if (!HasAuthority())
	{
		OutWhy = TEXT("The elder did not hear that.");
		return false;
	}
	if (ElderPhase != EGFElderSession::Open || !ElderSession.IsValid())
	{
		OutWhy = TEXT("The elder is not listening.");
		return false;
	}
	AGFElder* Elder = ElderSession.Get();
	if (!Elder || !Elder->CanInteract(GetPawn()))
	{
		OutWhy = TEXT("Too far from the elder.");
		return false;
	}
	return true;
}

void AGFPlayerController::RequestElderAccept()
{
	if (HasAuthority())
	{
		ServerElderAccept_Implementation();
	}
	else
	{
		ServerElderAccept();
	}
}

void AGFPlayerController::RequestElderHandIn()
{
	if (HasAuthority())
	{
		ServerElderHandIn_Implementation();
	}
	else
	{
		ServerElderHandIn();
	}
}

void AGFPlayerController::RequestElderSideAccept()
{
	if (HasAuthority())
	{
		ServerElderSideAccept_Implementation();
	}
	else
	{
		ServerElderSideAccept();
	}
}

void AGFPlayerController::RequestElderSideHandIn()
{
	if (HasAuthority())
	{
		ServerElderSideHandIn_Implementation();
	}
	else
	{
		ServerElderSideHandIn();
	}
}

void AGFPlayerController::RequestElderTrade(FName RowId)
{
	const int32 RequestId = ++ElderRequestSerial;
	if (HasAuthority())
	{
		ServerElderTrade_Implementation(RowId, RequestId);
	}
	else
	{
		ServerElderTrade(RowId, RequestId);
	}
}

void AGFPlayerController::ServerElderAccept_Implementation()
{
	FString Why;
	if (!ElderMenuAllows(Why))
	{
		ShowElderToast(Why);
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(GetWorld());
	if (!PS || PS->PlayerKey.IsEmpty() || !Quest)
	{
		ShowElderToast(TEXT("The elder cannot take that yet."));
		return;
	}
	Quest->EnsureRegistered();
	const FGFQuestApplyResult Result = Quest->ApplyEvent(FGFQuestEvents::ElderAccepted(PS->PlayerKey, ElderSession->TownId));
	if (Result.Result == EGFQuestResult::Refused)
	{
		ShowElderToast(FGFElderTalk::RefusalText(Result.Reason, PS->QuestStep));
	}
}

void AGFPlayerController::ServerElderHandIn_Implementation()
{
	FString Why;
	if (!ElderMenuAllows(Why))
	{
		ShowElderToast(Why);
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(GetWorld());
	if (!PS || !PS->Inventory || PS->PlayerKey.IsEmpty() || !Quest)
	{
		ShowElderToast(TEXT("The elder cannot take that yet."));
		return;
	}
	Quest->EnsureRegistered();
	const FGFQuestProgress* Progress = Quest->FindProgress(PS->PlayerKey);
	const FGFQuestProgress Fallback;
	const FGFQuestProgress& View = Progress ? *Progress : Fallback;
	const TArray<FGFItemStack> Offer = FGFElderTalk::OfferFromInventory(View, Quest->GetCatalog(), PS->Inventory->GetInventory());
	const FGFQuestApplyResult Result = Quest->ApplyEvent(FGFQuestEvents::TurnIn(PS->PlayerKey, Offer));
	if (Result.Result == EGFQuestResult::Refused)
	{
		ShowElderToast(FGFElderTalk::RefusalText(Result.Reason, View.Step));
	}
	else if (Result.Result == EGFQuestResult::Ignored && FGFElderTalk::ActionFor(View.Step) == FGFElderTalk::EAction::HandIn && Offer.Num() == 0)
	{
		ShowElderToast(FGFElderTalk::RefusalText(FGFQuestCodes::Short, View.Step));
	}
}

void AGFPlayerController::ServerElderSideAccept_Implementation()
{
	FString Why;
	if (!ElderMenuAllows(Why))
	{
		ShowElderToast(Why);
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(GetWorld());
	if (!PS || PS->PlayerKey.IsEmpty() || !Quest)
	{
		ShowElderToast(TEXT("The elder cannot take that yet."));
		return;
	}
	Quest->EnsureRegistered();
	const FName TownId = ElderSession->TownId;
	const FGFQuestProgress* Errand = Quest->FindProgress(PS->PlayerKey);
	const FGFSideQuestBoard* Board = Quest->FindSideBoard(PS->PlayerKey);
	const FGFSideQuestBoard Empty;
	const FGFElderTalk::FSideOffer Offer = FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
		Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, TownId);
	if (Offer.Action != FGFElderTalk::ESideAction::Accept)
	{
		ShowElderToast(TEXT("This elder has no town task for you right now."));
		return;
	}
	const FGFSideQuestApplyResult Result = Quest->ApplySideEvent(FGFSideQuestEvents::TalkAccepted(PS->PlayerKey, Offer.LineId, TownId));
	if (Result.Result != EGFQuestResult::Ok)
	{
		ShowElderToast(FGFElderTalk::SideRefusalText(Result.Reason));
	}
}

void AGFPlayerController::ServerElderSideHandIn_Implementation()
{
	FString Why;
	if (!ElderMenuAllows(Why))
	{
		ShowElderToast(Why);
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	UGFQuestSubsystem* Quest = UGFQuestSubsystem::Get(GetWorld());
	if (!PS || !PS->Inventory || PS->PlayerKey.IsEmpty() || !Quest)
	{
		ShowElderToast(TEXT("The elder cannot take that yet."));
		return;
	}
	Quest->EnsureRegistered();
	const FName TownId = ElderSession->TownId;
	const FGFQuestProgress* Errand = Quest->FindProgress(PS->PlayerKey);
	const FGFSideQuestBoard* Board = Quest->FindSideBoard(PS->PlayerKey);
	const FGFSideQuestBoard Empty;
	const FGFElderTalk::FSideOffer Offer = FGFElderTalk::SideOfferFor(Quest->GetSideCatalog(), Board ? *Board : Empty,
		Errand ? Errand->FactionId : NAME_None, Errand ? Errand->Rank : EGFFactionRank::Outsider, TownId);
	FGFSideQuestEvent Event;
	if (!FGFElderTalk::SideHandInEvent(Offer, PS->PlayerKey, TownId, PS->HeldItem, PS->Inventory->GetInventory(), Event))
	{
		ShowElderToast(TEXT("There is nothing to hand in for this town task yet."));
		return;
	}
	const FGFSideQuestApplyResult Result = Quest->ApplySideEvent(Event);
	if (Result.Result != EGFQuestResult::Ok)
	{
		ShowElderToast(FGFElderTalk::SideRefusalText(Result.Reason));
	}
}

void AGFPlayerController::ServerElderTrade_Implementation(FName RowId, int32 RequestId)
{
	FString Why;
	if (!ElderMenuAllows(Why))
	{
		ShowElderToast(Why);
		return;
	}
	AGFPlayerState* PS = GetPlayerState<AGFPlayerState>();
	UGFElderShopSubsystem* Shop = UGFElderShopSubsystem::Get(GetWorld());
	AGFElder* Elder = ElderSession.Get();
	if (!PS || PS->PlayerKey.IsEmpty() || !Shop || !Elder)
	{
		ShowElderToast(TEXT("The shop is closed."));
		return;
	}
	FString TradeWhy;
	const EGFTxResult Result = Shop->Trade(ElderSessionId, PS->PlayerKey, Elder->LedgerKey(), Elder->TownId, RowId, RequestId, TradeWhy);
	if (Result != EGFTxResult::Applied && Result != EGFTxResult::AlreadyApplied)
	{
		ShowElderToast(TradeWhy.IsEmpty() ? FString(TEXT("The trade did not go through.")) : TradeWhy);
	}
}
