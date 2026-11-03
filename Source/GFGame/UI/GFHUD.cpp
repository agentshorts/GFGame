#include "GFHUD.h"
#include "GFPlayerController.h"
#include "GFElder.h"
#include "GFElderBook.h"
#include "GFElderTalk.h"
#include "GFElderTrade.h"
#include "GFQuestSubsystem.h"
#include "GFCrosshairPanel.h"
#include "Aim/GFAimSim.h"
#include "GFBeatHopCourse.h"
#include "GFMineSite.h"
#include "GFRunwaySite.h"
#include "GFRunwayStage.h"
#include "GFArenaSite.h"
#include "GFZoneCatalog.h"
#include "GFZoneLayout.h"
#include "GFZoneSubsystem.h"
#include "GFHUDFormat.h"
#include "GFDefaultInputKeys.h"
#include "GFInteractable.h"
#include "GFQuestLine.h"
#include "GFVillainRules.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFMovementTuning.h"
#include "GFStaminaSim.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFTerritorySubsystem.h"
#include "GFVoxelWorld.h"
#include "GFMiningRange.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "GFGrumbleDigSite.h"
#include "GFDepthsSite.h"
#include "GFFerryStand.h"

namespace
{
	constexpr float ChatLineH = 16.f;
	constexpr float ChatBottom = 108.f;
	constexpr float LabelX = 16.f;

	/** Leaf text field so Escape is ours. SEditableTextBox would focus an inner widget and miss this. */
	class SGFChatText : public SEditableText
	{
	public:
		SLATE_BEGIN_ARGS(SGFChatText) {}
			SLATE_EVENT(FOnTextCommitted, OnTextCommitted)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Committed = InArgs._OnTextCommitted;
			SEditableText::Construct(
				SEditableText::FArguments()
				.OnTextCommitted(Committed)
				.ClearKeyboardFocusOnCommit(false)
				.RevertTextOnEscape(false)
				.SelectAllTextWhenFocused(false)
				.HintText(FText::FromString(TEXT("Chat")))
				.ColorAndOpacity(FLinearColor::White)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14)));
		}

		virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override
		{
			if (KeyEvent.GetKey() == EKeys::Escape)
			{
				Committed.ExecuteIfBound(FText::GetEmpty(), ETextCommit::OnCleared);
				return FReply::Handled();
			}
			return SEditableText::OnKeyDown(Geometry, KeyEvent);
		}

	private:
		FOnTextCommitted Committed;
	};
}

struct FGHUDChat
{
	TSharedPtr<SWidget> Root;
	TWeakPtr<SEditableText> Text;
};

AGFHUD::AGFHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

AGFHUD::~AGFHUD()
{
	RemoveAimSettings();
	RemoveChatWidget();
}

void AGFHUD::BeginPlay()
{
	Super::BeginPlay();
	if (const UWorld* World = GetWorld())
	{
		ShownAt = World->GetTimeSeconds();
		bShownAtSet = true;
	}
}

void AGFHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetHudModal(EGFHudModalEvent::Reset);
	if (APawn* Held = HeldPawn.Get())
	{
		Held->EnableInput(GetOwningPlayerController());
	}
	HeldPawn.Reset();
	RemoveAimSettings();
	RemoveChatWidget();
	ChatBlockTicks = 2;
	Super::EndPlay(EndPlayReason);
}

void AGFHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ChatBlockTicks > 0)
	{
		--ChatBlockTicks;
	}
	PollToasts();
}

bool AGFHUD::ConsumeChatBlock()
{
	if (ChatBlockTicks <= 0)
	{
		return false;
	}
	ChatBlockTicks = 0;
	return true;
}

void AGFHUD::ToggleControlsHint()
{
	if (IsChatOpen())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const float Elapsed = (World && bShownAtSet) ? static_cast<float>(World->GetTimeSeconds() - ShownAt) : 0.f;
	if (!bHintUserOverride)
	{
		bHintUserVisible = !GFHintVisible(Elapsed, false, true);
		bHintUserOverride = true;
		return;
	}
	bHintUserVisible = !bHintUserVisible;
}

UFont* AGFHUD::HudFont() const
{
	return GEngine ? GEngine->GetSmallFont() : nullptr;
}

void AGFHUD::DrawPathsAndVillains()
{
	if (!Canvas)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	if (!World || !PC)
	{
		return;
	}
	AGFPlayerState* Me = PC->GetPlayerState<AGFPlayerState>();
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const double Now = World->GetTimeSeconds();
	if (Me && !Me->Progress.bPathChosen)
	{
		float Y = 12.f * Scale;
		for (const FString& Line : GFFormatPathCards(FGFPathRules::Cards()))
		{
			DrawText(Line, FLinearColor(0.95f, 0.90f, 0.70f), 16.f, Y, Font, 0.65f * Scale, false);
			Y += 14.f * Scale;
		}
	}
	if (Me && Me->IsVillainMode())
	{
		const float LockLeft = FMath::Max(0.f, Me->VillainLockUntil - static_cast<float>(Now));
		DrawText(GFFormatVillainBadge(true, LockLeft), FLinearColor(0.72f, 0.28f, 0.85f), Canvas->ClipX - 180.f, 16.f, Font, Scale, false);
	}
	const APawn* MyPawn = GetOwningPawn();
	const AGameStateBase* GS = World->GetGameState();
	if (!MyPawn || !GS)
	{
		return;
	}
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AGFPlayerState* Other = Cast<AGFPlayerState>(PS);
		const APawn* Pawn = Other ? Other->GetPawn() : nullptr;
		if (!Other || !Pawn || Pawn == MyPawn)
		{
			continue;
		}
		const float Distance = FVector::Dist(MyPawn->GetActorLocation(), Pawn->GetActorLocation());
		const FGFNameplateText Plate = GFFormatNameplate(Other->GetPlayerName(), Other->ChosenPath, Other->IsVillainMode(),
			FGFVillainRules::TitleAt(Other->VillainTitle), Distance <= GFHUD::VillainSightCm);
		const FString Line = GFFormatNameplateWithRank(Plate.Line, Other->FactionRank);
		FVector2D Screen;
		if (!PC->ProjectWorldLocationToScreen(Pawn->GetActorLocation() + FVector(0.f, 0.f, 120.f), Screen, true))
		{
			continue;
		}
		const FLinearColor Tint = Plate.bShowVillain ? FLinearColor(0.78f, 0.45f, 0.95f) : FLinearColor::White;
		DrawText(Line, Tint, Screen.X, Screen.Y, Font, 0.7f * Scale, true);
	}
}

void AGFHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!bShowHUD || !Canvas)
	{
		return;
	}
	if (!bShownAtSet)
	{
		ShownAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		bShownAtSet = true;
	}
	PollToasts();
	DrawBeatHopPops();
	DrawBeatHopSession();
	DrawMineSession();
	DrawRunwaySession();
	DrawArenaSession();
	DrawGrumbleSession();
	DrawDepthsSession();
	DrawFerryStand();
	DrawMiningRange();
	DrawCrosshair();
	DrawAimOverlay();
	DrawCompass();
	DrawStaminaArc();
	DrawLandingMarker();
	DrawZoneLabel();
	DrawChat();
	DrawToasts();
	DrawHotbar();
	DrawHint();
	DrawPathsAndVillains();
	DrawFieldHud();
	DrawPauseMenu();
	DrawElderMenu();
}

void AGFHUD::DrawMiningRange()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	AGFMiningRange* Range = nullptr;
	for (TActorIterator<AGFMiningRange> It(World); It; ++It)
	{
		Range = *It;
		break;
	}
	if (!Range || !Range->IsHudOn())
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = Canvas->ClipY / 1080.f;
	float Y = 72.f * Scale;
	for (const FString& Line : Range->HudLines())
	{
		DrawText(Line, FLinearColor(0.93f, 0.90f, 0.82f), 28.f * Scale, Y, Font, 0.85f * Scale, false);
		Y += 18.f * Scale;
	}
}

void AGFHUD::DrawCrosshair()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	const FGFCrosshairSettings Settings = Character ? Character->GetCrosshairSettings() : FGFCrosshairSettings();
	const FGFAimState IdleAim;
	const FGFAimState& Aim = Character ? Character->GetAimState() : IdleAim;
	if (Character && !GFShouldDrawCrosshair(Settings, Aim))
	{
		return;
	}
	const float Extra = Character
		? GFCrosshairGap(Settings, Character->IsCrosshairSpreadActive(), false) - Settings.Gap
		: 0.f;
	const FGFCrosshairDraw Draw = GFBuildCrosshair(Settings, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f, Extra);
	for (const FGFCrosshairMark& Mark : Draw.Marks)
	{
		if (Mark.Kind == EGFCrosshairMarkKind::Dot)
		{
			DrawRect(Mark.Color, Mark.X - Mark.Radius, Mark.Y - Mark.Radius, Mark.Radius * 2.f, Mark.Radius * 2.f);
		}
		else
		{
			DrawLine(Mark.X, Mark.Y, Mark.X2, Mark.Y2, Mark.Color, Mark.Thickness);
		}
	}
}

void AGFHUD::DrawAimOverlay()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	if (!Character || Canvas->ClipX < 2.f || Canvas->ClipY < 2.f)
	{
		return;
	}
	const bool bScope = Character->WantsScopeOverlay();
	const bool bFists = Character->WantsFistVignette();
	if (!bScope && !bFists)
	{
		return;
	}
	const TArray<FGFScreenRect> Bands = bScope ? GFScopeVignette(Canvas->ClipX, Canvas->ClipY) : GFFistVignette(Canvas->ClipX, Canvas->ClipY);
	for (const FGFScreenRect& Band : Bands)
	{
		DrawRect(Band.Color, Band.X, Band.Y, Band.W, Band.H);
	}
	if (bScope)
	{
		FGFCrosshairSettings Reticle;
		Reticle.Shape = EGFCrosshairShape::Circle;
		Reticle.Size = 16.f;
		Reticle.Thickness = 1.5f;
		Reticle.Opacity = 0.95f;
		Reticle.bCenterDot = true;
		const FGFCrosshairDraw Draw = GFBuildCrosshair(Reticle, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f, 0.f);
		for (const FGFCrosshairMark& Mark : Draw.Marks)
		{
			if (Mark.Kind == EGFCrosshairMarkKind::Dot)
			{
				DrawRect(Mark.Color, Mark.X - Mark.Radius, Mark.Y - Mark.Radius, Mark.Radius * 2.f, Mark.Radius * 2.f);
			}
			else
			{
				DrawLine(Mark.X, Mark.Y, Mark.X2, Mark.Y2, Mark.Color, Mark.Thickness);
			}
		}
	}
}

const TCHAR* AGFHUD::PauseEntryLabel(int32 Index)
{
	switch (Index)
	{
	case 0: return TEXT("Return to hub");
	case 1: return TEXT("Settings");
	case 2: return TEXT("Close");
	case 3: return TEXT("Controls / How to get items");
	default: return TEXT("Close");
	}
}

FString AGFHUD::GetPauseEntry(int32 Index) const
{
	if (const AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController()))
	{
		if (Modal == EGFHudModal::PauseConfirm)
		{
			if (Index == 0)
			{
				return TEXT("Confirm");
			}
			if (Index == 1)
			{
				return TEXT("Cancel");
			}
			return FString();
		}
	}
	return PauseEntryLabel(Index);
}

AGFHUD::FPauseInput AGFHUD::PauseInputFor(bool bMenuOpen)
{
	FPauseInput Out;
	Out.bGameAndUI = bMenuOpen;
	Out.bShowCursor = bMenuOpen;
	Out.bClickEvents = bMenuOpen;
	Out.bPawnInput = !bMenuOpen;
	return Out;
}

void AGFHUD::OpenPauseMenu()
{
	SetHudModal(EGFHudModalEvent::OpenPause);
}

void AGFHUD::ClosePauseMenu()
{
	SetHudModal(EGFHudModalEvent::ClosePause);
}

void AGFHUD::TogglePauseMenu()
{
	SetHudModal(EGFHudModalEvent::TogglePause);
}

void AGFHUD::HandleEscape()
{
	SetHudModal(EGFHudModalEvent::Escape);
}

void AGFHUD::ApplyMenuInput()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	const bool bHold = FGFHudModalRules::HoldsPawn(Modal, Restore);
	APawn* Pawn = PC->GetPawn();
	if (bHold && Pawn && HeldPawn.Get() != Pawn)
	{
		if (APawn* Old = HeldPawn.Get())
		{
			Old->EnableInput(PC);
		}
		Pawn->DisableInput(PC);
		HeldPawn = Pawn;
	}
	else if (!bHold && HeldPawn.IsValid())
	{
		HeldPawn->EnableInput(PC);
		HeldPawn.Reset();
	}
	if (Modal == EGFHudModal::Chat || Modal == EGFHudModal::Aim)
	{
		return;
	}
	const FPauseInput Want = PauseInputFor(FGFHudModalRules::FreesMouse(Modal));
	if (Want.bGameAndUI)
	{
		// Frees the mouse: the viewport only captures while a button is down, and the cursor stays visible then too.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
	PC->bShowMouseCursor = Want.bShowCursor;
	PC->bEnableClickEvents = Want.bClickEvents;
}

void AGFHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	FString Name = BoxName.ToString();
	if (IsPauseMenuOpen() && Name.RemoveFromStart(TEXT("GFPause_")) && Name.IsNumeric())
	{
		ActivatePauseEntry(FCString::Atoi(*Name));
		return;
	}
	if (Modal == EGFHudModal::Elder && BoxName.ToString().StartsWith(TEXT("GFElder_")))
	{
		ActivateElderBox(BoxName);
	}
}

void AGFHUD::ActivatePauseEntry(int32 Index)
{
	AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController());
	if (Modal == EGFHudModal::PauseConfirm)
	{
		if (!PC)
		{
			return;
		}
		if (Index == 0)
		{
			PC->RequestReturnFromMenu(true);
		}
		else
		{
			PC->CancelReturnConfirm();
			ClosePauseMenu();
		}
		return;
	}
	if (Index == 0)
	{
		if (PC)
		{
			PC->RequestReturnFromMenu(false);
		}
		return;
	}
	if (Index == 1)
	{
		ClosePauseMenu();
		ToggleAimSettings();
		return;
	}
	if (Index == 3)
	{
		ShowItemHowTo();
		ClosePauseMenu();
		return;
	}
	ClosePauseMenu();
}

void AGFHUD::ShowItemHowTo()
{
	bShowItemHowTo = true;
	bItemHowToPinned = true;
}

void AGFHUD::DrawFieldHud()
{
	if (!Canvas || !FGFHudModalRules::ShowsFieldHud(Modal, Restore))
	{
		return;
	}
	static const FGFQuestCatalog Catalog = FGFQuestCatalog::Load();
	EGFQuestStep Step = EGFQuestStep::TakeCrystal;
	int32 Count = 0;
	EGFFactionRank Rank = EGFFactionRank::Outsider;
	EGFTheftHeat Heat = EGFTheftHeat::Clear;
	float SecondsLeft = 0.f;
	const APlayerController* Owning = GetOwningPlayerController();
	if (const AGFPlayerState* PS = Owning ? Owning->GetPlayerState<AGFPlayerState>() : nullptr)
	{
		Step = PS->QuestStep;
		Count = PS->QuestCount;
		Rank = PS->FactionRank;
	}
	if (const AGFPlayerController* GPC = Cast<AGFPlayerController>(Owning))
	{
		Heat = GPC->GetShownHeat();
		SecondsLeft = GPC->GetShownHuntLeft();
	}

	FString Verb;
	bool bReach = false;
	if (AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn()))
	{
		if (AActor* Target = Character->FindNearestInteractable())
		{
			if (IGFInteractable* Interactable = Cast<IGFInteractable>(Target))
			{
				Verb = Interactable->GetInteractPrompt().ToString();
				bReach = !Verb.IsEmpty();
			}
		}
	}

	const FString Key = FGFDefaultInputKeys::InteractKey().GetFName().ToString();
	const FGFQuestTrackerView Tracker = GFFormatQuestTracker(Catalog, Step, Count, Rank, true);
	const FGFSpottedAlert Spotted = GFFormatSpottedAlert(Heat, SecondsLeft, true);
	const FGFInteractPrompt Prompt = GFFormatInteractPrompt(bReach, Key, Verb, true);
	const FGFFieldHudLayout Layout = GFLayoutFieldHud(Canvas->ClipX, Canvas->ClipY, Tracker.Lines.Num(), Spotted.bVisible, Prompt.bVisible);
	UFont* Font = HudFont();

	if (!Layout.Tracker.IsEmpty())
	{
		DrawRect(FLinearColor(0.05f, 0.07f, 0.10f, 0.78f), Layout.Tracker.X, Layout.Tracker.Y, Layout.Tracker.W, Layout.Tracker.H);
		float Y = Layout.Tracker.Y + GFHUD::TrackerPad;
		for (int32 Index = 0; Index < Tracker.Lines.Num(); ++Index)
		{
			const bool bRank = Index == 0;
			const FLinearColor Color = bRank ? FLinearColor(0.98f, 0.86f, 0.45f) : FLinearColor(0.93f, 0.96f, 1.f);
			DrawText(Tracker.Lines[Index], Color, Layout.Tracker.X + GFHUD::TrackerPad, Y, Font, 1.f, false);
			Y += GFHUD::TrackerLineH;
		}
	}
	if (!Layout.Spotted.IsEmpty())
	{
		DrawRect(FLinearColor(0.55f, 0.08f, 0.08f, 0.92f), Layout.Spotted.X, Layout.Spotted.Y, Layout.Spotted.W, Layout.Spotted.H);
		DrawText(Spotted.Headline, FLinearColor::White, Layout.Spotted.X + GFHUD::TrackerPad, Layout.Spotted.Y + GFHUD::TrackerPad, Font, 1.f, false);
		if (!Spotted.Detail.IsEmpty())
		{
			DrawText(Spotted.Detail, FLinearColor(1.f, 0.85f, 0.85f), Layout.Spotted.X + GFHUD::TrackerPad, Layout.Spotted.Y + GFHUD::TrackerPad + GFHUD::TrackerLineH, Font, 1.f, false);
		}
	}
	if (!Layout.Interact.IsEmpty())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Layout.Interact.X, Layout.Interact.Y, Layout.Interact.W, Layout.Interact.H);
		DrawText(Prompt.Line, FLinearColor(0.98f, 0.86f, 0.45f), Layout.Interact.X + GFHUD::TrackerPad, Layout.Interact.Y + 4.f, Font, 1.f, false);
	}
}

void AGFHUD::DrawPauseMenu()
{
	if (!IsPauseMenuOpen() || !Canvas)
	{
		return;
	}
	UFont* Font = HudFont();
	const AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController());
	const bool bConfirm = Modal == EGFHudModal::PauseConfirm;
	const FString Title = bConfirm && PC ? PC->GetReturnConfirmPrompt() : FString(TEXT("Paused"));
	DrawRect(FLinearColor(0.05f, 0.08f, 0.12f, 0.88f), 24.f, 24.f, 420.f, bConfirm ? 148.f : 188.f);
	DrawText(Title, FLinearColor(0.93f, 0.96f, 1.f), 40.f, 36.f, Font, 1.f, false);
	const int32 Count = bConfirm ? 2 : 4;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		DrawText(GetPauseEntry(Index), FLinearColor(0.98f, 0.86f, 0.45f), 40.f, 68.f + Index * 28.f, Font, 1.f, false);
		AddHitBox(FVector2D(32.f, 64.f + Index * 28.f), FVector2D(400.f, 28.f), FName(*FString::Printf(TEXT("GFPause_%d"), Index)), true);
	}
}

void AGFHUD::ToggleAimSettings()
{
	SetHudModal(Modal == EGFHudModal::Aim ? EGFHudModalEvent::CloseAim : EGFHudModalEvent::OpenAim);
}

void AGFHUD::CloseAimSettings()
{
	SetHudModal(EGFHudModalEvent::CloseAim);
}

void AGFHUD::RemoveAimSettings()
{
	if (AimSettingsRoot.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(AimSettingsRoot.ToSharedRef());
	}
	AimSettingsRoot.Reset();
}

void AGFHUD::BuildAimWidget()
{
	AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	if (Character)
	{
		Character->SetAimPanelOpen(true);
	}
	AimSettingsRoot = GFMakeAimSettingsPanel(Character, [this]() { CloseAimSettings(); });
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(AimSettingsRoot.ToSharedRef());
	}
}

void AGFHUD::FocusAim()
{
	if (!AimSettingsRoot.IsValid())
	{
		return;
	}
	if (APlayerController* PC = GetOwningPlayerController())
	{
		// UI-only so Escape is handled by the panel and does not also toggle it from the game mapping.
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(AimSettingsRoot);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(AimSettingsRoot, EFocusCause::SetDirectly);
	}
}

void AGFHUD::DrawCompass()
{
	const APawn* Pawn = GetOwningPawn();
	const APlayerController* PC = GetOwningPlayerController();
	if (!Pawn || !PC || Canvas->ClipX < 32.f)
	{
		return;
	}
	const FGFZoneCatalog& Catalog = FGFZoneCatalog::Get();
	const AGFPlayerState* PS = PC->GetPlayerState<AGFPlayerState>();
	const TArray<FName> Unlocked = PS ? PS->Progress.UnlockedShardstones : TArray<FName>();
	const float ViewYaw = PC->GetControlRotation().Yaw;
	const FVector At = Pawn->GetActorLocation();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 8.f, 4.f, Canvas->ClipX - 16.f, 18.f);

	auto Mark = [this, At, ViewYaw](const FVector& Target, const FLinearColor& Color, const FString& Letter)
	{
		const FVector Delta = Target - At;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		const float Rel = FMath::FindDeltaAngleDegrees(ViewYaw, Yaw);
		const float X = GFCompassIconX(Rel, Canvas->ClipX);
		DrawRect(Color, X - 4.f, 8.f, 8.f, 8.f);
		DrawText(Letter, FLinearColor::White, X - 3.f, 6.f, HudFont(), 0.7f, false);
	};

	if (const FGFShardstoneRow* Kindle = Catalog.FindStone(FName(TEXT("Stone.Kindle"))))
	{
		Mark(Kindle->Location, FLinearColor(0.95f, 0.78f, 0.35f, 1.f), TEXT("K"));
	}
	for (const FGFRegionSlot& Slot : FGFZoneLayout::Regions())
	{
		const FGFShardstoneRow* Stone = Catalog.FindStone(Slot.StoneId);
		if (!Stone || !Catalog.IsUnlocked(*Stone, Unlocked))
		{
			continue;
		}
		const FString Letter = Slot.DisplayName.IsEmpty() ? FString(TEXT("*")) : Slot.DisplayName.Left(1);
		Mark(Stone->Location, Slot.Color, Letter);
	}
}

void AGFHUD::DrawStaminaArc()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	const UGFCharacterMovementComponent* Move = Character ? Character->GetGFMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	const UGFMovementTuning& Tuning = Move->GetTuning();
	const float MaxStamina = Move->GetMaxStamina();
	const float Stamina = Move->GetStamina();
	if (!GFStaminaSim::ShouldShowArc(Stamina, MaxStamina, Move->IsStaminaEnabled(), Tuning.HudHideEpsilon))
	{
		return;
	}

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Radius = 16.f;
	const float Thick = 2.f;
	const float FullTurn = 6.2831855f;
	const float StartAngle = -1.5707963f;
	const float Fraction = MaxStamina > 0.f ? FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f) : 0.f;
	const FLinearColor Track(1.f, 1.f, 1.f, 0.22f);
	const FLinearColor Fill(0.95f, 0.72f, 0.28f, 0.95f);
	const int32 TrackSteps = 28;

	auto DrawSweep = [this, CX, CY, Radius, Thick, StartAngle](float Sweep, const FLinearColor& Color, int32 Steps)
	{
		if (Sweep <= 0.f || Steps < 2)
		{
			return;
		}
		float PreviousX = CX + Radius * FMath::Cos(StartAngle);
		float PreviousY = CY + Radius * FMath::Sin(StartAngle);
		for (int32 Index = 1; Index <= Steps; ++Index)
		{
			const float Along = static_cast<float>(Index) / static_cast<float>(Steps);
			const float Angle = StartAngle + Sweep * Along;
			const float X = CX + Radius * FMath::Cos(Angle);
			const float Y = CY + Radius * FMath::Sin(Angle);
			DrawLine(PreviousX, PreviousY, X, Y, Color, Thick);
			PreviousX = X;
			PreviousY = Y;
		}
	};

	DrawSweep(FullTurn, Track, TrackSteps);
	const int32 FillSteps = FMath::Clamp(FMath::CeilToInt(static_cast<float>(TrackSteps) * Fraction), 2, TrackSteps);
	DrawSweep(FullTurn * Fraction, Fill, FillSteps);
}

void AGFHUD::DrawLandingMarker()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	UGFCharacterMovementComponent* Move = Character ? Character->GetGFMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	FVector WorldPoint = FVector::ZeroVector;
	if (!Move->GetLandingMarker(WorldPoint))
	{
		return;
	}
	APlayerController* Controller = GetOwningPlayerController();
	FVector2D Screen = FVector2D::ZeroVector;
	if (!Controller || !Controller->ProjectWorldLocationToScreen(WorldPoint, Screen, true))
	{
		return;
	}
	if (Screen.X < 0.f || Screen.Y < 0.f || Screen.X > Canvas->ClipX || Screen.Y > Canvas->ClipY)
	{
		return;
	}
	const float Distance = static_cast<float>(FVector::Dist(Character->GetActorLocation(), WorldPoint));
	const float Radius = FMath::Clamp(900.f / FMath::Max(Distance, 150.f) * 18.f, 6.f, 28.f);
	const FLinearColor Color(0.45f, 0.92f, 0.55f, 0.9f);
	const int32 Steps = 16;
	const float Turn = 6.2831855f;
	float PreviousX = Screen.X + Radius;
	float PreviousY = Screen.Y;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float Angle = Turn * static_cast<float>(Index) / static_cast<float>(Steps);
		const float X = Screen.X + Radius * FMath::Cos(Angle);
		const float Y = Screen.Y + Radius * FMath::Sin(Angle);
		DrawLine(PreviousX, PreviousY, X, Y, Color, 2.f);
		PreviousX = X;
		PreviousY = Y;
	}
}

void AGFHUD::DrawZoneLabel()
{
	const APawn* Pawn = GetOwningPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		return;
	}
	UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(World);
	if (!Territory)
	{
		return;
	}
	const FVector At = Pawn->GetActorLocation();
	const FGFTerritoryZoneHit Zone = Territory->GetZoneAt(At);
	const FGFTerritoryHazardHit Hazard = Territory->GetHazardAt(At);
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	const FName HazardName = !Hazard.Id.IsNone() ? Hazard.Id : Hazard.Kind;
	const bool bLegacy = Zone.Zone == EGFTerritoryZone::Faction
		|| Zone.Zone == EGFTerritoryZone::Neutral
		|| (Hazard.bHazard && Zone.Zone == EGFTerritoryZone::Wild);
	FString Label;
	if (bLegacy)
	{
		Label = GFFormatZoneLabel(Zone.Zone, Zone.FactionId, Territory->IsSafeFor(PS, At), Hazard.bHazard, HazardName);
	}
	else
	{
		const FGFZoneCatalog& Catalog = FGFZoneCatalog::Get();
		FName ZoneId = PS ? PS->GetCurrentZoneId() : NAME_None;
		FGFZoneHit Hit = ZoneId.IsNone() ? Catalog.ResolveAt(At, nullptr) : Catalog.Describe(ZoneId, nullptr);
		if (const UGFZoneSubsystem* Zones = World->GetSubsystem<UGFZoneSubsystem>(); ZoneId.IsNone() && Zones)
		{
			Hit = Zones->ResolveAt(At);
		}
		Label = GFFormatWorldZoneLabel(Hit.DisplayName, Hit.DangerTier);
		if (Label.IsEmpty())
		{
			Label = GFFormatZoneLabel(Zone.Zone, Zone.FactionId, Territory->IsSafeFor(PS, At), Hazard.bHazard, HazardName);
		}
	}
	if (Label.IsEmpty())
	{
		return;
	}
	UFont* Font = HudFont();
	float XL = 0.f;
	float YL = 0.f;
	constexpr float LabelY = 28.f;
	if (Font)
	{
		Canvas->TextSize(Font, Label, XL, YL);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), LabelX - 6.f, LabelY - 2.f, XL + 12.f, FMath::Max(YL, 14.f) + 4.f);
	}
	DrawText(Label, FLinearColor::White, LabelX, LabelY, Font, 1.f, false);
}

void AGFHUD::NoteChatLine(int32 Seq, double Now)
{
	if (!ChatSeenAt.Contains(Seq))
	{
		ChatSeenAt.Add(Seq, Now);
	}
}

void AGFHUD::DrawChat()
{
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	if (!World || !GS || GS->ChatLog.Num() == 0)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const TArray<FGFChatMessage>& Log = GS->ChatLog;
	const int32 Start = FMath::Max(0, Log.Num() - GFHUD::VisibleChatLines);

	struct FShown
	{
		int32 Index = 0;
		float Alpha = 1.f;
	};
	TArray<FShown> Shown;
	TSet<int32> Live;
	for (int32 Index = Start; Index < Log.Num(); ++Index)
	{
		const FGFChatMessage& Message = Log[Index];
		Live.Add(Message.Seq);
		NoteChatLine(Message.Seq, Now);
		const float Age = static_cast<float>(Now - ChatSeenAt.FindChecked(Message.Seq));
		const float Alpha = GFChatLineAlpha(Age);
		if (Alpha > 0.02f)
		{
			Shown.Add({ Index, Alpha });
		}
	}
	for (auto It = ChatSeenAt.CreateIterator(); It; ++It)
	{
		if (!Live.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	if (Shown.Num() == 0)
	{
		return;
	}

	UFont* Font = HudFont();
	// Sit above the chat entry (bottom padding ChatBottom, plus the box height).
	const float Bottom = Canvas->ClipY - (ChatBottom + 32.f) - Shown.Num() * ChatLineH;
	for (int32 Row = 0; Row < Shown.Num(); ++Row)
	{
		const FGFChatMessage& Message = Log[Shown[Row].Index];
		const float Alpha = Shown[Row].Alpha;
		const float Y = Bottom + Row * ChatLineH;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.4f * Alpha), 8.f, Y - 1.f, 520.f, ChatLineH);

		int32 ColorIndex = INDEX_NONE;
		if (!Message.SenderKey.IsEmpty())
		{
			if (const AGFPlayerState* Sender = GS->FindPlayerByKey(Message.SenderKey))
			{
				ColorIndex = Sender->ColorIndex;
			}
		}
		const bool bNamed = !Message.SenderName.IsEmpty();
		if (!bNamed || !Font)
		{
			FLinearColor Body(1.f, 1.f, 1.f, Alpha);
			DrawText(bNamed ? (Message.SenderName + TEXT(": ") + Message.Text) : Message.Text, Body, LabelX, Y, Font, 1.f, false);
			continue;
		}
		FLinearColor NameColor = GFPlayerColor(ColorIndex);
		NameColor.A = Alpha;
		const FLinearColor Body(1.f, 1.f, 1.f, Alpha);
		DrawText(Message.SenderName, NameColor, LabelX, Y, Font, 1.f, false);
		float XL = 0.f;
		float YL = 0.f;
		Canvas->TextSize(Font, Message.SenderName, XL, YL);
		DrawText(FString(TEXT(": ")) + Message.Text, Body, LabelX + XL, Y, Font, 1.f, false);
	}
}

void AGFHUD::PollToasts()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	if (!World || !PS)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	for (int32 Index = AppliedNotices; Index < PS->LandNotices.Num(); ++Index)
	{
		FGFHUDToast& Toast = Toasts.AddDefaulted_GetRef();
		Toast.Text = PS->LandNotices[Index];
		Toast.ShownAt = Now;
	}
	AppliedNotices = PS->LandNotices.Num();
	for (int32 Index = AppliedResults; Index < PS->ChatResults.Num(); ++Index)
	{
		FGFHUDToast& Toast = Toasts.AddDefaulted_GetRef();
		Toast.Text = PS->ChatResults[Index].ToString();
		Toast.ShownAt = Now;
	}
	AppliedResults = PS->ChatResults.Num();
	Toasts.RemoveAll([Now](const FGFHUDToast& Toast)
	{
		return Now - Toast.ShownAt > static_cast<double>(GFHUD::ToastSeconds);
	});
}

void AGFHUD::DrawToasts()
{
	if (Toasts.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	float Y = Canvas->ClipY * 0.22f;
	for (const FGFHUDToast& Toast : Toasts)
	{
		if (Toast.Text.IsEmpty())
		{
			continue;
		}
		float XL = 80.f;
		float YL = 14.f;
		if (Font)
		{
			Canvas->TextSize(Font, Toast.Text, XL, YL);
		}
		const float X = FMath::Max(8.f, (Canvas->ClipX - XL) * 0.5f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 8.f, Y - 2.f, XL + 16.f, YL + 4.f);
		DrawText(Toast.Text, FLinearColor::White, X, Y, Font, 1.f, false);
		Y += YL + 6.f;
	}
}

void AGFHUD::DrawHotbar()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	const FName Selected = Character ? Character->SelectedBlock : NAME_None;
	TArray<FName> Ids = Character ? Character->GetHotbarIds() : TArray<FName>();
	if (!Character)
	{
		for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
		{
			Ids = It->GetPlaceableBlockIds();
			break;
		}
	}
	const int32 SelectedIndex = Ids.IndexOfByKey(Selected);
	const TArray<FGFHUDSlotRect> Slots = GFLayoutHotbar(Ids.Num(), SelectedIndex, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = HudFont();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FGFHUDSlotRect& Slot = Slots[Index];
		const FLinearColor Border = Slot.bSelected ? FLinearColor(1.f, 0.92f, 0.45f, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.28f);
		const FLinearColor Fill = Slot.bSelected ? FLinearColor(0.95f, 0.78f, 0.22f, 0.92f) : FLinearColor(0.f, 0.f, 0.f, 0.5f);
		DrawRect(Border, Slot.X - 2.f, Slot.Y - 2.f, Slot.W + 4.f, Slot.H + 4.f);
		DrawRect(Fill, Slot.X, Slot.Y, Slot.W, Slot.H);
		const FLinearColor TextColor = Slot.bSelected ? FLinearColor::Black : FLinearColor::White;
		DrawText(Ids[Index].ToString(), TextColor, Slot.X + 4.f, Slot.Y + 6.f, Font, Slot.W < 48.f ? 0.55f : 0.75f, false);
	}
}

void AGFHUD::DrawHint()
{
	const UWorld* World = GetWorld();
	const float Elapsed = (World && bShownAtSet) ? static_cast<float>(World->GetTimeSeconds() - ShownAt) : 0.f;
	if (!GFHintVisible(Elapsed, bHintUserOverride, bHintUserVisible))
	{
		return;
	}
	const FString Hint = GFHUD::ControlsHint();
	UFont* Font = HudFont();
	float XL = 0.f;
	float YL = 14.f;
	if (Font)
	{
		Canvas->TextSize(Font, Hint, XL, YL);
	}
	const float HotbarTop = Canvas->ClipY - GFHUD::HotbarBottomMargin - GFHUD::HotbarSlotH;
	const float X = FMath::Max(8.f, (Canvas->ClipX - XL) * 0.5f);
	const float Y = HotbarTop - YL - 6.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 6.f, Y - 2.f, XL + 12.f, YL + 4.f);
	DrawText(Hint, FLinearColor(0.92f, 0.92f, 0.92f), X, Y, Font, 1.f, false);
}

void AGFHUD::OpenChat()
{
	SetHudModal(EGFHudModalEvent::OpenChat);
}

void AGFHUD::CloseChat()
{
	DismissChat();
}

void AGFHUD::SubmitChat()
{
	if (!IsChatOpen())
	{
		return;
	}
	FString Line;
	if (Chat)
	{
		if (TSharedPtr<SEditableText> Text = Chat->Text.Pin())
		{
			Line = Text->GetText().ToString().TrimStartAndEnd();
		}
	}
	DismissChat();
	if (!Line.IsEmpty())
	{
		SendChat(Line);
	}
}

void AGFHUD::SendChat(const FString& Line)
{
	APlayerController* PC = GetOwningPlayerController();
	AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	if (PS)
	{
		PS->RequestChat(Line);
	}
}

void AGFHUD::DismissChat()
{
	if (!IsChatOpen() && !Chat)
	{
		return;
	}
	if (IsChatOpen())
	{
		SetHudModal(EGFHudModalEvent::CloseChat);
		return;
	}
	RemoveChatWidget();
}

void AGFHUD::RemoveChatWidget()
{
	// Modal is already off Chat. Focus loss commits OnUserMovedFocus, and that must not re-enter.
	FGHUDChat* Dying = Chat;
	Chat = nullptr;
	if (Dying && Dying->Root.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Dying->Root.ToSharedRef());
	}
	delete Dying;
}

void AGFHUD::BuildChatWidget()
{
	TSharedRef<SGFChatText> Text = SNew(SGFChatText)
		.OnTextCommitted(FOnTextCommitted::CreateLambda([this](const FText& Committed, ETextCommit::Type Method)
		{
			if (!IsChatOpen())
			{
				return;
			}
			if (Method == ETextCommit::OnEnter)
			{
				const FString Line = Committed.ToString().TrimStartAndEnd();
				DismissChat();
				if (!Line.IsEmpty())
				{
					SendChat(Line);
				}
				return;
			}
			DismissChat();
		}));

	Chat = new FGHUDChat();
	Chat->Text = Text;
	Chat->Root = SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(12.f, 0.f, 12.f, ChatBottom))
		[
			SNew(SBox)
			.WidthOverride(420.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.82f))
				.Padding(FMargin(8.f, 4.f))
				[
					Text
				]
			]
		];
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Chat->Root.ToSharedRef());
	}
}

void AGFHUD::FocusChat()
{
	if (!Chat)
	{
		return;
	}
	TSharedPtr<SEditableText> Text = Chat->Text.Pin();
	if (APlayerController* PC = GetOwningPlayerController())
	{
		FInputModeUIOnly Mode;
		if (Text.IsValid())
		{
			Mode.SetWidgetToFocus(Text);
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = false;
	}
	if (Text.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Text, EFocusCause::SetDirectly);
	}
}

void AGFHUD::SetHudModal(EGFHudModalEvent Event)
{
	const FGFHudModalRules::FState Next = FGFHudModalRules::Transition(Modal, Restore, Event);
	if (Next.Modal == Modal && Next.Restore == Restore)
	{
		return;
	}
	const bool bEnterChat = Next.Modal == EGFHudModal::Chat && Modal != EGFHudModal::Chat;
	const bool bEnterAim = Next.Modal == EGFHudModal::Aim && Modal != EGFHudModal::Aim;
	if ((bEnterChat || bEnterAim) && (!GEngine || !GEngine->GameViewport))
	{
		return;
	}

	const EGFHudModal Prev = Modal;
	Modal = Next.Modal;
	Restore = Next.Restore;

	if (Prev == EGFHudModal::Chat && Modal != EGFHudModal::Chat)
	{
		ChatBlockTicks = 2;
		RemoveChatWidget();
	}
	if (Prev == EGFHudModal::Aim && Modal != EGFHudModal::Aim)
	{
		if (AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn()))
		{
			Character->SetAimPanelOpen(false);
		}
		RemoveAimSettings();
	}
	if (Prev == EGFHudModal::PauseConfirm && Modal != EGFHudModal::PauseConfirm)
	{
		if (AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController()))
		{
			PC->ClearReturnPrompt();
		}
	}
	if (Prev == EGFHudModal::Elder && Modal != EGFHudModal::Elder && Modal != EGFHudModal::Chat)
	{
		if (AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController()))
		{
			PC->RequestCloseElderMenu();
		}
	}
	if (bEnterChat)
	{
		BuildChatWidget();
	}
	if (bEnterAim)
	{
		BuildAimWidget();
	}
	ApplyMenuInput();
	if (bEnterChat && Modal == EGFHudModal::Chat)
	{
		FocusChat();
	}
	if (bEnterAim && Modal == EGFHudModal::Aim)
	{
		FocusAim();
	}
}

void AGFHUD::PushToast(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	FGFHUDToast& Toast = Toasts.AddDefaulted_GetRef();
	Toast.Text = Text;
	const UWorld* World = GetWorld();
	Toast.ShownAt = World ? World->GetTimeSeconds() : 0.0;
}

void AGFHUD::SetElderTab(EGFElderTab Tab)
{
	ElderTab = Tab;
}

void AGFHUD::OpenElderMenu(AGFElder* Elder, FName TownId)
{
	if (Elder)
	{
		ElderActor = Elder;
		if (!Elder->TownId.IsNone())
		{
			TownId = Elder->TownId;
		}
	}
	ElderTownId = TownId;
	SetElderTab(EGFElderTab::Talk);
	SetHudModal(EGFHudModalEvent::OpenElder);
	// Aim refuses the panel. Drop the server session so a hidden menu cannot still trade.
	if (Modal != EGFHudModal::Elder && Restore != EGFHudModal::Elder)
	{
		if (AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController()))
		{
			PC->RequestCloseElderMenu();
		}
	}
}

namespace
{
	FString FormatTradeStacks(const TArray<FGFItemStack>& Stacks)
	{
		TArray<FString> Parts;
		for (const FGFItemStack& Stack : Stacks)
		{
			Parts.Add(FString::Printf(TEXT("%s x%d"), *Stack.ItemId.ToString(), Stack.Count));
		}
		return Parts.Num() > 0 ? FString::Join(Parts, TEXT(", ")) : FString(TEXT("nothing"));
	}

	const FGFElderTradeCatalog& ElderTradeRows()
	{
		static const FGFElderTradeCatalog Catalog = FGFElderTradeCatalog::Load();
		return Catalog;
	}

	const FGFQuestCatalog& ElderQuestRows()
	{
		static const FGFQuestCatalog Catalog = FGFQuestCatalog::Load();
		return Catalog;
	}
}

void AGFHUD::ActivateElderBox(FName BoxName)
{
	const FString Name = BoxName.ToString();
	AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController());
	if (Name == TEXT("GFElder_Close"))
	{
		SetHudModal(EGFHudModalEvent::CloseElder);
		return;
	}
	if (Name == TEXT("GFElder_Tab_0"))
	{
		SetElderTab(EGFElderTab::Talk);
		return;
	}
	if (Name == TEXT("GFElder_Tab_1"))
	{
		SetElderTab(EGFElderTab::Trade);
		return;
	}
	if (Name == TEXT("GFElder_Tab_2"))
	{
		SetElderTab(EGFElderTab::Book);
		return;
	}
	if (!PC)
	{
		return;
	}
	if (Name == TEXT("GFElder_Accept"))
	{
		PC->RequestElderAccept();
		return;
	}
	if (Name == TEXT("GFElder_HandIn"))
	{
		PC->RequestElderHandIn();
		return;
	}
	FString Trade = Name;
	if (Trade.RemoveFromStart(TEXT("GFElder_Trade_")) && Trade.IsNumeric())
	{
		const int32 Index = FCString::Atoi(*Trade);
		const TArray<const FGFElderTradeListing*> Rows = ElderTradeRows().ForTown(ElderTownId);
		if (Rows.IsValidIndex(Index) && Rows[Index])
		{
			PC->RequestElderTrade(Rows[Index]->RowId);
		}
	}
}

void AGFHUD::DrawElderMenu()
{
	if (Modal != EGFHudModal::Elder || !Canvas)
	{
		return;
	}
	UFont* Font = HudFont();
	DrawRect(FLinearColor(0.06f, 0.07f, 0.10f, 0.92f), 24.f, 24.f, 560.f, 400.f);

	FString Title = TEXT("Elder");
	if (const AGFElder* Elder = ElderActor.Get())
	{
		Title = Elder->DisplayTitle();
	}
	else if (!ElderTownId.IsNone())
	{
		Title = ElderTownId.ToString() + TEXT(" Elder");
	}
	DrawText(Title, FLinearColor(0.93f, 0.96f, 1.f), 40.f, 36.f, Font, 1.f, false);

	const TCHAR* TabLabels[] = { TEXT("Talk"), TEXT("Trade"), TEXT("Elder Book") };
	const EGFElderTab Tabs[] = { EGFElderTab::Talk, EGFElderTab::Trade, EGFElderTab::Book };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const bool bOn = ElderTab == Tabs[Index];
		const FLinearColor Color = bOn ? FLinearColor(0.98f, 0.86f, 0.45f) : FLinearColor(0.75f, 0.78f, 0.82f);
		DrawText(TabLabels[Index], Color, 40.f + Index * 150.f, 68.f, Font, 1.f, false);
		AddHitBox(FVector2D(32.f + Index * 150.f, 64.f), FVector2D(140.f, 26.f), FName(*FString::Printf(TEXT("GFElder_Tab_%d"), Index)), true);
	}

	const AGFPlayerController* PC = Cast<AGFPlayerController>(GetOwningPlayerController());
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	float Y = 108.f;
	if (ElderTab == EGFElderTab::Talk)
	{
		const EGFQuestStep Step = PS ? PS->QuestStep : EGFQuestStep::TakeCrystal;
		const FGFQuestCatalog& Quests = ElderQuestRows();
		const FString QuestTitle = Quests.TitleFor(Step);
		const FString Hint = Quests.HintFor(Step);
		DrawText(QuestTitle.IsEmpty() ? FString(TEXT("The Elder's Errand")) : QuestTitle, FLinearColor::White, 40.f, Y, Font, 1.f, false);
		Y += 28.f;
		DrawText(Hint, FLinearColor(0.88f, 0.90f, 0.84f), 40.f, Y, Font, 0.9f, false);
		Y += 36.f;
		const FGFElderTalk::EAction Action = FGFElderTalk::ActionFor(Step);
		if (Action == FGFElderTalk::EAction::Accept)
		{
			DrawText(TEXT("Accept"), FLinearColor(0.98f, 0.86f, 0.45f), 40.f, Y, Font, 1.f, false);
			AddHitBox(FVector2D(32.f, Y - 4.f), FVector2D(200.f, 26.f), TEXT("GFElder_Accept"), true);
		}
		else if (Action == FGFElderTalk::EAction::HandIn)
		{
			DrawText(TEXT("Hand in"), FLinearColor(0.98f, 0.86f, 0.45f), 40.f, Y, Font, 1.f, false);
			AddHitBox(FVector2D(32.f, Y - 4.f), FVector2D(200.f, 26.f), TEXT("GFElder_HandIn"), true);
		}
	}
	else if (ElderTab == EGFElderTab::Trade)
	{
		const TArray<const FGFElderTradeListing*> Rows = ElderTradeRows().ForTown(ElderTownId);
		if (Rows.Num() == 0)
		{
			DrawText(TEXT("This elder has nothing on the table."), FLinearColor(0.88f, 0.90f, 0.84f), 40.f, Y, Font, 0.9f, false);
		}
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const FGFElderTradeListing* Row = Rows[Index];
			if (!Row)
			{
				continue;
			}
			const FString Line = FString::Printf(TEXT("Give %s. Get %s."), *FormatTradeStacks(Row->Give), *FormatTradeStacks(Row->Get));
			DrawText(Line, FLinearColor(0.98f, 0.86f, 0.45f), 40.f, Y, Font, 0.85f, false);
			AddHitBox(FVector2D(32.f, Y - 4.f), FVector2D(520.f, 24.f), FName(*FString::Printf(TEXT("GFElder_Trade_%d"), Index)), true);
			Y += 28.f;
		}
	}
	else
	{
		TArray<FGFElderPost> Posts;
		if (const UGFElderBookSubsystem* Book = UGFElderBookSubsystem::Get(GetWorld()))
		{
			Posts = Book->GetBook().Feed(FGFElderBook::ElderViewId());
		}
		int32 Shown = 0;
		for (int32 Index = Posts.Num() - 1; Index >= 0 && Shown < 8; --Index)
		{
			if (Posts[Index].Line.IsEmpty())
			{
				continue;
			}
			DrawText(Posts[Index].Line, FLinearColor(0.90f, 0.92f, 0.86f), 40.f, Y, Font, 0.8f, false);
			Y += 22.f;
			++Shown;
		}
		if (Shown == 0)
		{
			DrawText(TEXT("The Elder Book is empty."), FLinearColor(0.88f, 0.90f, 0.84f), 40.f, Y, Font, 0.9f, false);
		}
	}

	DrawText(TEXT("Close"), FLinearColor(0.98f, 0.86f, 0.45f), 40.f, 384.f, Font, 1.f, false);
	AddHitBox(FVector2D(32.f, 378.f), FVector2D(160.f, 28.f), TEXT("GFElder_Close"), true);
}

void AGFHUD::DrawBeatHopPops()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFBeatHopCourse* Course = nullptr;
	for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
	{
		Course = *It;
		break;
	}
	if (!Course || Course->GetPops().Num() == 0)
	{
		return;
	}
	if (!bBeatTexLoaded)
	{
		bBeatTexLoaded = true;
		BeatJudgementTex = LoadObject<UTexture2D>(nullptr, FGFBeatHopVfx::JudgementTexturePath, nullptr, LOAD_Quiet | LOAD_NoWarn);
		BeatSparkleTex = LoadObject<UTexture2D>(nullptr, FGFBeatHopVfx::SparkleTexturePath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}
	if (!BeatJudgementTex)
	{
		return;
	}
	FString MyKey;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const AGFPlayerState* PS = PC->GetPlayerState<AGFPlayerState>())
		{
			MyKey = PS->PlayerKey;
		}
	}
	const double Now = World->GetTimeSeconds();
	// Sheet pixels to screen pixels: 4x at 1080p, kept whole-ish so the pixel font stays crisp.
	const float Px = FMath::Clamp(FMath::RoundToFloat(Canvas->ClipY / 270.f), 2.f, 8.f);
	const float Sheet = static_cast<float>(FGFBeatHopVfx::SheetSize);
	auto DrawCell = [this, Px, Sheet](const FIntRect& R, float X, float Y, const FLinearColor& Color)
	{
		DrawTexture(BeatJudgementTex, X, Y, R.Width() * Px, R.Height() * Px,
			R.Min.X / Sheet, R.Min.Y / Sheet, R.Width() / Sheet, R.Height() / Sheet, Color, BLEND_Translucent);
	};
	for (const FGFBeatHopPop& P : Course->GetPops())
	{
		const double Age = Now - P.ShownAt;
		if (Age < 0.0 || Age > FGFBeatHopVfx::PopSeconds)
		{
			continue;
		}
		const FVector Screen = Project(P.WorldLocation, false);
		if (Screen.Z <= 0.f)
		{
			continue; // behind the camera
		}
		const float T = static_cast<float>(Age / FGFBeatHopVfx::PopSeconds);
		const float Alpha = 1.f - T * T;

		// Sparkle on the brick top: 4 frames of 16x16 at 12 fps, lane-tinted.
		const int32 Frame = FGFBeatHopVfx::SparkleFrame(Age);
		if (BeatSparkleTex && P.Judgement != EGFJudgement::Miss && Frame != INDEX_NONE)
		{
			const float Size = 16.f * Px;
			const float U = 1.f / FGFBeatHopVfx::SparkleFrames;
			FLinearColor C = P.LaneColor;
			C.A = 1.f;
			DrawTexture(BeatSparkleTex, Screen.X - Size * 0.5f, Screen.Y - Size * 0.5f, Size, Size, Frame * U, 0.f, U, 1.f, C, BLEND_Translucent);
		}

		FIntRect Word;
		if (!FGFBeatHopVfx::WordRect(P.Judgement, Word))
		{
			continue;
		}
		const float Rise = 10.f * Px * T;
		// Stack other players' words so two hops on one brick don't draw on the same pixels.
		const float Stack = static_cast<float>(GetTypeHash(P.PlayerKey) % 3) * (10.f * Px);
		const float WordY = Screen.Y - 18.f * Px - Rise - (P.PlayerKey == MyKey ? 0.f : Stack);
		FLinearColor Tint = FGFBeatHopVfx::Tint(P.Judgement);
		Tint.A = Alpha;
		DrawCell(Word, Screen.X - Word.Width() * Px * 0.5f, WordY, Tint);

		if (P.Judgement != EGFJudgement::Miss && P.Combo >= 2)
		{
			// COMBO x12: digits advance 6 px (5 px glyph + 1 px gap).
			const FString Digits = FString::FromInt(P.Combo);
			const FIntRect Combo = FGFBeatHopVfx::ComboRect();
			const FIntRect Times = FGFBeatHopVfx::TimesRect();
			const float Width = (Combo.Width() + 2 + Times.Width() + 1 + Digits.Len() * FGFBeatHopVfx::DigitAdvancePx - 1) * Px;
			float X = Screen.X - Width * 0.5f;
			const float Y = WordY + (Word.Height() + 3) * Px;
			FLinearColor Lav = FGFBeatHopVfx::ComboTint();
			Lav.A = Alpha;
			DrawCell(Combo, X, Y, Lav);
			X += (Combo.Width() + 2) * Px;
			DrawCell(Times, X, Y, Lav);
			X += (Times.Width() + 1) * Px;
			for (const TCHAR Ch : Digits)
			{
				DrawCell(FGFBeatHopVfx::DigitRect(Ch - TEXT('0')), X, Y, Lav);
				X += FGFBeatHopVfx::DigitAdvancePx * Px;
			}
		}
	}
}

void AGFHUD::DrawBeatHopSession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFBeatHopCourse* Course = nullptr;
	for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
	{
		if (It->IsSession())
		{
			Course = *It;
			break;
		}
	}
	if (!Course)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	auto Panel = [&](const TArray<TPair<FString, FLinearColor>>& Lines, float CenterX, float TopY)
	{
		float W = 0.f;
		for (const TPair<FString, FLinearColor>& L : Lines)
		{
			float TW = 0.f, TH = 0.f;
			GetTextSize(L.Key, TW, TH, Font, Scale);
			W = FMath::Max(W, TW);
		}
		const float Pad = 10.f * Scale;
		const float H = Lines.Num() * LineH + Pad * 2.f;
		const float X = CenterX - W * 0.5f - Pad;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, TopY, W + Pad * 2.f, H);
		float Y = TopY + Pad;
		for (const TPair<FString, FLinearColor>& L : Lines)
		{
			DrawText(L.Key, L.Value, X + Pad, Y, Font, Scale, false);
			Y += LineH;
		}
	};
	const FLinearColor Gold(1.f, 0.82f, 0.25f);
	const FLinearColor Soft(0.85f, 0.85f, 0.9f);
	TArray<TPair<FString, FLinearColor>> Lines;

	if (Course->IsShowingResults())
	{
		Lines.Emplace(TEXT("FINISH!"), Gold);
		for (const FGFBeatHopResultRow& Row : Course->GetResults())
		{
			const FString Grade = StaticEnum<EGFBeatHopGrade>()->GetNameStringByValue(static_cast<int64>(Row.Grade));
			Lines.Emplace(FString::Printf(TEXT("%s   Grade %s   Score %d   Best combo %d   (Perfect %d / Good %d / Miss %d)"),
				*Row.PlayerName, *Grade, Row.Score, Row.BestCombo, Row.Perfects, Row.Goods, Row.Misses), FLinearColor::White);
		}
		if (Course->GetResults().Num() == 0)
		{
			Lines.Emplace(TEXT("(no hops landed)"), Soft);
		}
		Lines.Emplace(FString::Printf(TEXT("Back to the hub in %d"), FMath::CeilToInt(Course->GetResultsSecondsLeft())), Soft);
		Panel(Lines, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.28f);
		return;
	}

	const FString Title = AGFBeatHopCourse::SongTitle(Course->IsRunning() ? Course->GetRunSongId() : Course->GetSelectedSong());
	const FString Diff = AGFBeatHopCourse::DifficultyName(Course->IsRunning() ? Course->GetRunDifficulty() : Course->GetSelectedDifficulty());
	if (Course->IsRunning())
	{
		FString MyKey;
		if (const APlayerController* PC = GetOwningPlayerController())
		{
			if (const AGFPlayerState* PS = PC->GetPlayerState<AGFPlayerState>())
			{
				MyKey = PS->PlayerKey;
			}
		}
		Lines.Emplace(FString::Printf(TEXT("BEAT HOP  %s (%s)    Time %.0f / %.0f s"), *Title, *Diff,
			FMath::Max(0.0, Course->GetRunTime()), Course->GetRunEndTime()), Gold);
		TArray<FGFBeatHopScoreRep> Board;
		Course->GetScoreboard(Board);
		if (Board.Num() == 0)
		{
			const FGFRhythmScore* Score = MyKey.IsEmpty() ? nullptr : Course->GetScore(MyKey);
			Lines.Emplace(FString::Printf(TEXT("Score %d   Combo %d"), Score ? Score->Score : 0, Score ? Score->Combo : 0), FLinearColor::White);
		}
		else
		{
			Board.Sort([&MyKey](const FGFBeatHopScoreRep& A, const FGFBeatHopScoreRep& B)
			{
				if (A.PlayerKey == MyKey) { return true; }
				if (B.PlayerKey == MyKey) { return false; }
				return A.Score > B.Score;
			});
			for (const FGFBeatHopScoreRep& Row : Board)
			{
				FString Name = Row.PlayerKey;
				if (const AGameStateBase* GS = World->GetGameState())
				{
					for (const APlayerState* PS : GS->PlayerArray)
					{
						const AGFPlayerState* GFPS = Cast<AGFPlayerState>(PS);
						if (GFPS && GFPS->PlayerKey == Row.PlayerKey)
						{
							Name = GFPS->GetPlayerName();
							break;
						}
					}
				}
				const FLinearColor Tint = Row.PlayerKey == MyKey ? FLinearColor::White : Soft;
				Lines.Emplace(FString::Printf(TEXT("%s   Score %d   Combo %d   (Perfect %d / Good %d / Miss %d)"),
					*Name, Row.Score, Row.Combo, Row.Perfects, Row.Goods, Row.Misses), Tint);
			}
		}
		Panel(Lines, Canvas->ClipX * 0.5f, 12.f * Scale);
		return;
	}

	Lines.Emplace(TEXT("BEAT HOP"), Gold);
	Lines.Emplace(FString::Printf(TEXT("Song: %s    Difficulty: %s"), *Title, *Diff), FLinearColor::White);
	Lines.Emplace(TEXT("Step on a tile to pick a song or difficulty, then run through the gate ahead."), Soft);
	Lines.Emplace(TEXT("You run on your own: Space to hop on the beat, A / D to switch lanes."), Soft);
	Panel(Lines, Canvas->ClipX * 0.5f, 12.f * Scale);
}

void AGFHUD::DrawMineSession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFMineSite* Site = AGFMineSite::Get(World);
	const APawn* Pawn = GetOwningPawn();
	if (!Site || !Site->IsPlayerNear(Pawn))
	{
		return;
	}
	const AGFPlayerState* PS = nullptr;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		PS = PC->GetPlayerState<AGFPlayerState>();
	}
	TArray<FString> Prompt;
	Site->GetPromptLines(PS, Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.05f, 0.04f, 0.03f, 0.72f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(1.f, 0.82f, 0.25f);
	const FLinearColor Soft(0.9f, 0.86f, 0.78f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		const FLinearColor Tint = (i == 0) ? Gold : Soft;
		DrawText(Prompt[i], Tint, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
}

void AGFHUD::DrawRunwaySession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFRunwaySite* Site = AGFRunwaySite::Get(World);
	const APawn* Pawn = GetOwningPawn();
	if (!Site || !Site->IsPlayerNear(Pawn))
	{
		return;
	}
	const AGFPlayerState* PS = nullptr;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		PS = PC->GetPlayerState<AGFPlayerState>();
	}
	TArray<FString> Prompt;
	Site->GetPromptLines(PS, Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.18f, 0.08f, 0.12f, 0.72f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(1.f, 0.82f, 0.55f);
	const FLinearColor Step(1.f, 0.97f, 0.86f);
	const FLinearColor Soft(0.95f, 0.88f, 0.90f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		// "> " is the step to do next. The empty-closet line stays bright until a look is chosen.
		const bool bCurrent = Prompt[i].StartsWith(TEXT(">")) || Prompt[i].Contains(TEXT("Pick a look first"));
		const FLinearColor Tint = (i == 0) ? Gold : (bCurrent ? Step : Soft);
		DrawText(Prompt[i], Tint, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
	if (!bShowItemHowTo)
	{
		return;
	}
	const AGFRunwayStage* Stage = AGFRunwayStage::Find(World);
	const bool bResults = Stage && Stage->GetPhase() == EGFRunwayPhase::Results;
	if (bResults && !bItemHowToPinned)
	{
		return;
	}
	TArray<FString> Hint;
	Site->GetItemHintLines(Hint);
	if (Hint.Num() == 0)
	{
		return;
	}
	Hint.Insert(TEXT("HOW TO GET ITEMS"), 0);
	float HintW = 0.f;
	for (const FString& Line : Hint)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		HintW = FMath::Max(HintW, TW);
	}
	const float HintH = Hint.Num() * LineH + Pad * 2.f;
	const float HintX = 16.f * Scale;
	const float HintTop = Top;
	DrawRect(FLinearColor(0.10f, 0.08f, 0.06f, 0.78f), HintX, HintTop, HintW + Pad * 2.f, HintH);
	float HY = HintTop + Pad;
	for (int32 i = 0; i < Hint.Num(); ++i)
	{
		const bool bCurrent = Hint[i].StartsWith(TEXT(">"));
		const FLinearColor Tint = (i == 0) ? Gold : (bCurrent ? Step : Soft);
		DrawText(Hint[i], Tint, HintX + Pad, HY, Font, Scale, false);
		HY += LineH;
	}
}

void AGFHUD::DrawArenaSession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFArenaSite* Site = AGFArenaSite::Get(World);
	const APawn* Pawn = GetOwningPawn();
	if (!Site || !Site->IsPlayerNear(Pawn))
	{
		return;
	}
	const AGFPlayerState* PS = nullptr;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		PS = PC->GetPlayerState<AGFPlayerState>();
	}
	TArray<FString> Prompt;
	Site->GetPromptLines(PS, Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.08f, 0.05f, 0.02f, 0.72f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(1.f, 0.82f, 0.25f);
	const FLinearColor Soft(0.9f, 0.86f, 0.78f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		const FLinearColor Tint = (i == 0) ? Gold : Soft;
		DrawText(Prompt[i], Tint, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
}

void AGFHUD::DrawGrumbleSession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFGrumbleDigSite* Site = AGFGrumbleDigSite::Get(World);
	const APawn* Pawn = GetOwningPawn();
	if (!Site || !Site->IsPlayerNear(Pawn))
	{
		return;
	}
	TArray<FString> Prompt;
	Site->GetPromptLines(Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.08f, 0.05f, 0.03f, 0.72f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(1.f, 0.82f, 0.25f);
	const FLinearColor Soft(0.9f, 0.86f, 0.78f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		const FLinearColor Tint = (i == 0) ? Gold : Soft;
		DrawText(Prompt[i], Tint, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
}

void AGFHUD::DrawDepthsSession()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const AGFDepthsSite* Site = AGFDepthsSite::Get(World);
	const APawn* Pawn = GetOwningPawn();
	if (!Site || !Site->IsPlayerNear(Pawn))
	{
		return;
	}
	const AGFPlayerState* PS = nullptr;
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		PS = PC->GetPlayerState<AGFPlayerState>();
	}
	TArray<FString> Prompt;
	Site->GetPromptLines(PS, Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.08f, 0.05f, 0.14f, 0.78f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(0.86f, 0.72f, 1.f);
	const FLinearColor Soft(0.90f, 0.86f, 0.96f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		const FLinearColor Tint = (i == 0) ? Gold : Soft;
		DrawText(Prompt[i], Tint, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
}

void AGFHUD::DrawFerryStand()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}
	const APawn* Pawn = GetOwningPawn();
	const AGFFerryStand* Stand = nullptr;
	float Best = 0.f;
	for (TActorIterator<AGFFerryStand> It(World); It; ++It)
	{
		if (!It->IsPlayerNear(Pawn))
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Pawn->GetActorLocation(), It->GetActorLocation());
		if (!Stand || Dist < Best)
		{
			Stand = *It;
			Best = Dist;
		}
	}
	if (!Stand)
	{
		return;
	}
	TArray<FString> Prompt;
	Stand->GetPromptLines(Prompt);
	if (Prompt.Num() == 0)
	{
		return;
	}
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float LineH = 16.f * Scale;
	float W = 0.f;
	for (const FString& Line : Prompt)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Line, TW, TH, Font, Scale);
		W = FMath::Max(W, TW);
	}
	const float Pad = 10.f * Scale;
	const float H = Prompt.Num() * LineH + Pad * 2.f;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.05f, 0.06f, 0.08f, 0.72f), X, Top, W + Pad * 2.f, H);
	const FLinearColor Gold(1.f, 0.82f, 0.25f);
	const FLinearColor Soft(0.9f, 0.86f, 0.78f);
	float Y = Top + Pad;
	for (int32 i = 0; i < Prompt.Num(); ++i)
	{
		DrawText(Prompt[i], (i == 0) ? Gold : Soft, X + Pad, Y, Font, Scale, false);
		Y += LineH;
	}
}
