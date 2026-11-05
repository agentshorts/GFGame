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
#include "GFAccelHopRoad.h"
#include "GFAccelHopTile.h"
#include "GFAccelHopTutorial.h"
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
#include "GFHotbarRules.h"
#include "GFInventoryScreen.h"
#include "GFInventoryView.h"
#include "GFInventoryComponent.h"
#include "GFEconomy.h"
#include "GFItemTypes.h"
#include "GFMagicComponent.h"
#include "GFMagicRules.h"
#include "Misc/PackageName.h"
#include "Engine/Canvas.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
	// A pawn its controller let go of, as at teardown, has no input to give back.
	APawn* Held = HeldPawn.Get();
	if (Held && Held->GetController() == GetOwningPlayerController())
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

void AGFHUD::CatchStrayInventoryRelease()
{
	if (Modal != EGFHudModal::Inventory || !InvCursor.IsButtonDown())
	{
		return;
	}
	// Hit box releases were dispatched with this frame's input, before the draw. A button that is up while the cursor
	// still thinks it is down was let go over no bag box (the world behind the panel, its border, off the window).
	const APlayerController* PC = GetOwningPlayerController();
	if (PC && !PC->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		SubmitHotbarMove(InvCursor.Release(FGFInvTarget::Nothing()));
	}
}

void AGFHUD::DrawHUD()
{
	CatchStrayInventoryRelease();
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
	// The lit diamond lies on the ground, so it goes down first and the landing circle is drawn over it (#347).
	DrawAccelHopLitMarker();
	DrawLandingMarker();
	DrawAccelHopTutorial();
	DrawZoneLabel();
	DrawChat();
	DrawToasts();
	DrawHotbar();
	DrawHint();
	DrawPathsAndVillains();
	DrawFieldHud();
	DrawPauseMenu();
	DrawElderMenu();
	DrawInventoryScreen();
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
		APawn* Old = HeldPawn.Get();
		if (Old && Old->GetController() == PC)
		{
			Old->EnableInput(PC);
		}
		Pawn->DisableInput(PC);
		HeldPawn = Pawn;
	}
	else if (!bHold && HeldPawn.IsValid())
	{
		if (HeldPawn->GetController() == PC)
		{
			HeldPawn->EnableInput(PC);
		}
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
		return;
	}
	if (Modal != EGFHudModal::Inventory || !BoxName.ToString().StartsWith(TEXT("GFInv_")))
	{
		return;
	}
	FString Box = BoxName.ToString();
	if (Box == TEXT("GFInv_Close"))
	{
		CloseInventory();
		return;
	}
	if (Box == TEXT("GFInv_Prev") || Box == TEXT("GFInv_Next"))
	{
		InvPage = FMath::Clamp(InvPage + (Box == TEXT("GFInv_Next") ? 1 : -1), 0, FMath::Max(0, InvPageCount - 1));
		return;
	}
	if (Box.RemoveFromStart(TEXT("GFInv_Tab_")) && Box.IsNumeric())
	{
		const int32 Tab = FCString::Atoi(*Box);
		if (Tab >= 0 && Tab < FGFInventoryView::TabCount)
		{
			SetInventoryTab(static_cast<EGFInventoryTab>(Tab));
		}
		return;
	}
	SubmitHotbarMove(InvCursor.Press(InventoryTargetFor(BoxName)));
}

void AGFHUD::NotifyHitBoxRelease(FName BoxName)
{
	Super::NotifyHitBoxRelease(BoxName);
	if (Modal == EGFHudModal::Inventory && BoxName.ToString().StartsWith(TEXT("GFInv_")))
	{
		SubmitHotbarMove(InvCursor.Release(InventoryTargetFor(BoxName)));
	}
}

FGFInvTarget AGFHUD::InventoryTargetFor(FName BoxName) const
{
	FString Box = BoxName.ToString();
	if (Box.RemoveFromStart(TEXT("GFInv_Cell_")) && Box.IsNumeric())
	{
		const int32 Index = FCString::Atoi(*Box);
		return InvPageEntries.IsValidIndex(Index) ? FGFInvTarget::Grid(InvPageEntries[Index].Id) : FGFInvTarget::Nothing();
	}
	Box = BoxName.ToString();
	if (Box.RemoveFromStart(TEXT("GFInv_Slot_")) && Box.IsNumeric())
	{
		const int32 Slot = FCString::Atoi(*Box);
		const APlayerController* PC = GetOwningPlayerController();
		const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
		const TArray<FName> Ids = PS ? PS->GetHotbarSlots() : TArray<FName>();
		return FGFInvTarget::BarSlot(Slot, Ids.IsValidIndex(Slot) ? Ids[Slot] : FName());
	}
	return FGFInvTarget::Nothing();
}

void AGFHUD::SubmitHotbarMove(const TOptional<FGFHotbarMove>& Move)
{
	if (!Move.IsSet())
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayerController();
	AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	if (PS)
	{
		LastHotbarRequest = PS->RequestHotbarMove(Move.GetValue());
		bLastHotbarReported = false;
	}
}

void AGFHUD::ToggleInventory()
{
	SetHudModal(EGFHudModalEvent::ToggleInventory);
}

void AGFHUD::OpenInventory()
{
	SetHudModal(EGFHudModalEvent::OpenInventory);
}

void AGFHUD::CloseInventory()
{
	SetHudModal(EGFHudModalEvent::CloseInventory);
}

void AGFHUD::SetInventoryTab(EGFInventoryTab Tab)
{
	if (Tab != InvTab)
	{
		InvTab = Tab;
		InvPage = 0;
	}
}

void AGFHUD::InventoryNumberKey(int32 Slot)
{
	if (Modal == EGFHudModal::Inventory)
	{
		SubmitHotbarMove(InvCursor.NumberKey(Slot, InvHover));
	}
}

TArray<FGFInventoryEntry> AGFHUD::GetInventoryEntries() const
{
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	if (!PS || !PS->Inventory)
	{
		return {};
	}
	TArray<FGFHotbarBlock> Placeable;
	if (const AGFVoxelWorld* Voxel = PS->GetVoxelWorld())
	{
		for (const FName BlockId : Voxel->GetPlaceableBlockIds())
		{
			Placeable.Add({ BlockId, PS->ItemForBlock(BlockId) });
		}
	}
	const AGFGameState* GS = PS->GetGFGameState();
	return FGFInventoryView::Build(PS->Inventory->GetInventory(), Placeable, GS ? GS->GetItemTable() : nullptr, GS && GS->bFreeBuild);
}

void AGFHUD::RefreshInventoryPage(const FGFInventoryLayout& Layout)
{
	const TArray<FGFInventoryEntry> Tab = FGFInventoryView::Filter(GetInventoryEntries(), InvTab);
	const int32 PerPage = Layout.Cells.Num();
	InvPageCount = FGFInventoryView::PageCount(Tab.Num(), PerPage);
	InvPage = FGFInventoryView::ClampPage(InvPage, Tab.Num(), PerPage);
	InvPageEntries.Reset();
	for (int32 Index = InvPage * PerPage; Index < Tab.Num() && InvPageEntries.Num() < PerPage; ++Index)
	{
		InvPageEntries.Add(Tab[Index]);
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
	// Recomputed every frame from the live flight (#342), so the ring follows steering all the way down.
	const FGFLandingTarget Target = Move->GetLandingTarget();
	if (!Target.bValid)
	{
		return;
	}
	const FVector WorldPoint = Target.Point;
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
	float Radius = FMath::Clamp(900.f / FMath::Max(Distance, 150.f) * 18.f, 6.f, 28.f);
	// Green for floor, gold and a second ring when the flight comes down on the lit target tile.
	const FLinearColor Color = Target.bLit ? FLinearColor(1.f, 0.82f, 0.25f, 0.95f) : FLinearColor(0.45f, 0.92f, 0.55f, 0.9f);
	const int32 Steps = 16;
	const float Turn = 6.2831855f;
	const int32 Rings = Target.bLit ? 2 : 1;
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
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
		Radius *= 1.45f;
	}
}

void AGFHUD::DrawAccelHopTutorial()
{
	// #348: a plain panel, top centre: step and title, one line of body copy, the live goal counter.
	// VISUAL PASS: the colours and the box are placeholders (the lit magenta for the title, Beat Hop's dark violet for
	// the box); swap them for the tutorial's own panel art here.
	UWorld* World = GetWorld();
	const AGFAccelHopTutorial* Tutorial = AGFAccelHopTutorial::Find(World);
	if (!World || !Canvas || !Tutorial || !Tutorial->bShowPrompts)
	{
		return;
	}
	const FGFAccelHopTutorialPrompt Prompt = Tutorial->GetPrompt();
	const bool bFlash = World->GetTimeSeconds() - Tutorial->GetLastAdvanceTime() < FGFAccelHopTutorialRules::FlashSeconds;
	const TArray<FString> Lines = FGFAccelHopTutorialRules::PromptLines(Prompt, bFlash);
	UFont* Font = HudFont();
	const float Scale = FMath::Clamp(Canvas->ClipY / 720.f, 1.f, 3.f);
	const float TitleScale = Scale * 1.35f;
	float W = 0.f;
	float H = 0.f;
	TArray<float> Heights;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		float TW = 0.f, TH = 0.f;
		GetTextSize(Lines[Index], TW, TH, Font, Index == 0 ? TitleScale : Scale);
		W = FMath::Max(W, TW);
		Heights.Add(TH + 4.f * Scale);
		H += Heights.Last();
	}
	const float Pad = 12.f * Scale;
	const float X = Canvas->ClipX * 0.5f - W * 0.5f - Pad;
	const float Top = 12.f * Scale;
	DrawRect(FLinearColor(0.08f, 0.05f, 0.14f, 0.80f), X, Top, W + Pad * 2.f, H + Pad * 2.f);
	const FLinearColor TitleTint = bFlash ? FLinearColor(0.55f, 1.f, 0.55f) : FLinearColor(1.f, 0.35f, 0.90f);
	const FLinearColor BodyTint(0.92f, 0.90f, 0.98f);
	const FLinearColor GoalTint(1.f, 0.86f, 0.45f);
	float Y = Top + Pad;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FLinearColor Tint = Index == 0 ? TitleTint : (Index == Lines.Num() - 1 ? GoalTint : BodyTint);
		DrawText(Lines[Index], Tint, X + Pad, Y, Font, Index == 0 ? TitleScale : Scale, false);
		Y += Heights[Index];
	}
}

void AGFHUD::DrawAccelHopLitMarker()
{
	// v3 (#347): a magenta diamond lying flat on the lit tile's top face, black outline and white core, plus a beacon
	// chevron over it. The v2 marker was a thin gold diamond floating 60 cm up, the same gold as the lit landing
	// ring, so the two read as one thing. Shape (diamond vs circle) and color (magenta vs green or gold) now differ.
	const AGFAccelHopRoad* Road = AGFAccelHopRoad::FindLocalRoad(GetWorld());
	const AGFAccelHopTile* Tile = Road ? Road->GetLitTile() : nullptr;
	APlayerController* Controller = GetOwningPlayerController();
	if (!Tile || !Controller || !Canvas)
	{
		return;
	}
	const FGFAccelHopLitTuning& Tuning = Road->GetLitTuning();
	const float Pulse = Tile->GetLitPulse();

	// The tile's own footprint (its axes, not the world box) and its top face. Before the art import there is no mesh,
	// so it falls back to the actor location and MarkerMinHalfCm.
	FVector Top = Tile->GetActorLocation();
	float HalfX = 0.f;
	float HalfY = 0.f;
	if (const UStaticMeshComponent* Mesh = Tile->GetMesh(); Mesh && Mesh->GetStaticMesh())
	{
		const FVector Local = Mesh->GetStaticMesh()->GetBounds().BoxExtent * Mesh->GetComponentScale().GetAbs();
		HalfX = static_cast<float>(Local.X);
		HalfY = static_cast<float>(Local.Y);
		Top = Mesh->Bounds.Origin;
		Top.Z = Mesh->Bounds.Origin.Z + Mesh->Bounds.BoxExtent.Z;
	}
	Top.Z += static_cast<double>(Tuning.MarkerLiftCm);
	const float HalfCm = FGFAccelHopLitRules::LitMarkerHalfCm(HalfX, HalfY, Pulse, Tuning);
	FVector Corners[4];
	FGFAccelHopLitRules::LitMarkerCorners(Top, Tile->GetActorForwardVector(), HalfCm, Corners);

	auto OnScreen = [this](const FVector2D& P)
	{
		return P.X >= 0.f && P.Y >= 0.f && P.X <= Canvas->ClipX && P.Y <= Canvas->ClipY;
	};
	FVector2D Center = FVector2D::ZeroVector;
	const bool bCenter = Controller->ProjectWorldLocationToScreen(Top, Center, true);
	FVector2D Screen[4];
	bool bAll = bCenter;
	for (int32 Index = 0; Index < 4 && bAll; ++Index)
	{
		bAll = Controller->ProjectWorldLocationToScreen(Corners[Index], Screen[Index], true);
	}
	// Far away the ground diamond shrinks to a sliver. Keep it at least MarkerMinScreenPx so it reads at a glance.
	const float MinPx = Tuning.MarkerMinScreenPx * (1.f + Tuning.MarkerPulseGrow * Pulse);
	const float SpanPx = bAll ? 0.5f * static_cast<float>(FMath::Max(FVector2D::Distance(Screen[0], Screen[2]), FVector2D::Distance(Screen[1], Screen[3]))) : 0.f;
	if (bCenter && (!bAll || SpanPx < MinPx))
	{
		Screen[0] = Center + FVector2D(0.f, -MinPx * 0.6f);
		Screen[1] = Center + FVector2D(MinPx, 0.f);
		Screen[2] = Center + FVector2D(0.f, MinPx * 0.6f);
		Screen[3] = Center + FVector2D(-MinPx, 0.f);
		bAll = true;
	}
	if (bAll && (OnScreen(Center) || OnScreen(Screen[0]) || OnScreen(Screen[1]) || OnScreen(Screen[2]) || OnScreen(Screen[3])))
	{
		auto Ring = [this, &Screen](const FLinearColor& Color, float Width, float Shrink, const FVector2D& Mid)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const FVector2D A = Mid + (Screen[Index] - Mid) * Shrink;
				const FVector2D B = Mid + (Screen[(Index + 1) % 4] - Mid) * Shrink;
				DrawLine(A.X, A.Y, B.X, B.Y, Color, Width);
			}
		};
		const FVector2D Mid = (Screen[0] + Screen[1] + Screen[2] + Screen[3]) * 0.25f;
		const float Grow = 1.f + 0.4f * Pulse;
		Ring(Tuning.MarkerOutlineColor, Tuning.MarkerOutlineWidthPx * Grow, 1.f, Mid);
		Ring(Tuning.MarkerColor, Tuning.MarkerLineWidthPx * Grow, 1.f, Mid);
		Ring(Tuning.MarkerOutlineColor, Tuning.MarkerOutlineWidthPx * 0.5f, 0.55f, Mid);
		Ring(FLinearColor(1.f, 1.f, 1.f, 0.95f), FMath::Max(2.f, Tuning.MarkerLineWidthPx * 0.5f), 0.55f, Mid);
	}

	// Beacon: a down chevron over the tile, found from hop height and over a crest where the face itself is hidden.
	FVector2D Beacon = FVector2D::ZeroVector;
	if (Controller->ProjectWorldLocationToScreen(Top + FVector(0.0, 0.0, Tuning.MarkerBeaconHeightCm), Beacon, true) && OnScreen(Beacon))
	{
		const float Size = Tuning.MarkerBeaconSizePx * (1.f + 0.3f * Pulse);
		const FVector2D Left = Beacon + FVector2D(-Size, -Size * 0.7f);
		const FVector2D Right = Beacon + FVector2D(Size, -Size * 0.7f);
		const FVector2D Tip = Beacon + FVector2D(0.f, Size * 0.5f);
		DrawLine(Left.X, Left.Y, Tip.X, Tip.Y, Tuning.MarkerOutlineColor, Tuning.MarkerOutlineWidthPx);
		DrawLine(Tip.X, Tip.Y, Right.X, Right.Y, Tuning.MarkerOutlineColor, Tuning.MarkerOutlineWidthPx);
		DrawLine(Left.X, Left.Y, Tip.X, Tip.Y, Tuning.MarkerColor, Tuning.MarkerLineWidthPx);
		DrawLine(Tip.X, Tip.Y, Right.X, Right.Y, Tuning.MarkerColor, Tuning.MarkerLineWidthPx);
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

UTexture2D* AGFHUD::SlotIcon(FName SlotId)
{
	if (SlotId.IsNone() || SlotId == FGFHotbarRules::FistsId())
	{
		return nullptr;
	}
	if (const TObjectPtr<UTexture2D>* Hit = IconCache.Find(SlotId))
	{
		return Hit->Get();
	}
	UTexture2D* Found = nullptr;
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	const FName ItemId = PS ? PS->ItemForBlock(SlotId) : SlotId;
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	// 1. The item row's own icon. 2. The T_Icon_<Id> convention for the slot id, then for the item it spends.
	// A miss is cached as null, so a slot without art costs one lookup per session, not one per frame.
	for (const FName Id : { SlotId, ItemId })
	{
		if (Found)
		{
			break;
		}
		if (const FGFItemRow* Row = GS ? FGFEconomy::FindItem(GS->GetItemTable(), Id) : nullptr)
		{
			if (!Row->Icon.IsNull())
			{
				Found = Row->Icon.LoadSynchronous();
			}
		}
		if (!Found)
		{
			const FString Path = FGFHotbarRules::ConventionIconPath(Id);
			if (!Path.IsEmpty() && FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path)))
			{
				Found = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
			}
		}
	}
	IconCache.Add(SlotId, Found);
	return Found;
}

FString AGFHUD::SlotDisplayName(FName SlotId) const
{
	if (SlotId.IsNone())
	{
		return TEXT("Empty");
	}
	if (SlotId == FGFHotbarRules::FistsId())
	{
		return TEXT("Hands");
	}
	const UWorld* World = GetWorld();
	const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
	if (const FGFItemRow* Row = GS ? FGFEconomy::FindItem(GS->GetItemTable(), SlotId) : nullptr)
	{
		if (!Row->DisplayName.IsEmpty())
		{
			return Row->DisplayName.ToString();
		}
	}
	return FName::NameToDisplayString(SlotId.ToString(), false);
}

FLinearColor AGFHUD::SlotSwatch(FName SlotId) const
{
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		if (const FGFBlockRow* Row = It->FindBlockRow(SlotId))
		{
			return Row->DebugColor;
		}
		break;
	}
	// Not a block: a stable colour per id so two different items never look alike side by side.
	const uint32 Hash = GetTypeHash(SlotId.ToString());
	return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hash & 0xFF), 120, 200);
}

void AGFHUD::DrawSlotArt(FName Id, float X, float Y, float W, float H, bool bSpent)
{
	UFont* Font = HudFont();
	const float Pad = FMath::Max(2.f, W * 0.14f);
	const float IconSize = FMath::Max(0.f, FMath::Min(W, H) - 2.f * Pad);
	const float IconX = X + (W - IconSize) * 0.5f;
	const float IconY = Y + (H - IconSize) * 0.5f;
	const FLinearColor Tint = bSpent ? FLinearColor(1.f, 1.f, 1.f, 0.35f) : FLinearColor::White;
	if (Id == FGFHotbarRules::FistsId())
	{
		DrawText(TEXT("Hands"), FLinearColor(0.85f, 0.85f, 0.85f), X + 6.f, Y + H * 0.5f - 7.f, Font, W < 48.f ? 0.5f : 0.7f, false);
	}
	else if (!Id.IsNone())
	{
		if (UTexture2D* Icon = SlotIcon(Id))
		{
			DrawTexture(Icon, IconX, IconY, IconSize, IconSize, 0.f, 0.f, 1.f, 1.f, Tint, BLEND_Translucent);
		}
		else
		{
			FLinearColor Swatch = SlotSwatch(Id);
			Swatch.A = bSpent ? 0.35f : 0.95f;
			DrawRect(Swatch, IconX, IconY, IconSize, IconSize);
			const FLinearColor Ink = Swatch.GetLuminance() > 0.5f ? FLinearColor::Black : FLinearColor::White;
			DrawText(FGFHotbarRules::ShortLabel(Id), Ink, IconX + 4.f, IconY + 3.f, Font, W < 48.f ? 0.55f : 0.8f, false);
		}
	}
}

void AGFHUD::DrawHotbar()
{
	const AGFCharacter* Character = Cast<AGFCharacter>(GetOwningPawn());
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	TArray<FName> Ids;
	if (Character)
	{
		Ids = Character->GetHotbarIds();
	}
	else if (PS)
	{
		Ids = PS->GetHotbarSlots();
	}
	if (Ids.Num() == 0)
	{
		return;
	}
	const int32 SelectedIndex = Character ? Character->GetSelectedHotbarSlot() : INDEX_NONE;
	const TArray<FGFHUDSlotRect> Slots = GFLayoutHotbar(Ids.Num(), SelectedIndex, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = HudFont();
	const FLinearColor Gold(1.f, 0.86f, 0.32f, 1.f);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FGFHUDSlotRect& Slot = Slots[Index];
		const FName Id = Ids[Index];
		// Selected: a thick gold frame, a lifted fill, and the slot pops up 4 px.
		const float Lift = Slot.bSelected ? 4.f : 0.f;
		const float Y = Slot.Y - Lift;
		if (Slot.bSelected)
		{
			DrawRect(Gold, Slot.X - 3.f, Y - 3.f, Slot.W + 6.f, Slot.H + 6.f);
		}
		else
		{
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.25f), Slot.X - 1.f, Y - 1.f, Slot.W + 2.f, Slot.H + 2.f);
		}
		DrawRect(Slot.bSelected ? FLinearColor(0.16f, 0.13f, 0.06f, 0.92f) : FLinearColor(0.f, 0.f, 0.f, 0.55f), Slot.X, Y, Slot.W, Slot.H);

		const int64 Count = PS ? PS->GetHotbarCount(Id) : -1;
		const bool bSpent = FGFHotbarRules::IsSpent(Id, Count);
		DrawSlotArt(Id, Slot.X, Y, Slot.W, Slot.H, bSpent);
		// Key number top left, count bottom right.
		DrawText(FString::FromInt(Index + 1), Slot.bSelected ? Gold : FLinearColor(1.f, 1.f, 1.f, 0.75f), Slot.X + 3.f, Y + 1.f, Font, 0.55f, false);
		const FString CountText = FGFHotbarRules::FormatCount(Count);
		if (!CountText.IsEmpty())
		{
			float XL = 0.f;
			float YL = 10.f;
			if (Font)
			{
				Canvas->TextSize(Font, CountText, XL, YL, 0.65f, 0.65f);
			}
			const FLinearColor CountColor = bSpent ? FLinearColor(1.f, 0.45f, 0.4f) : FLinearColor::White;
			DrawText(CountText, FLinearColor(0.f, 0.f, 0.f, 0.8f), Slot.X + Slot.W - XL - 2.f, Y + Slot.H - YL - 1.f, Font, 0.65f, false);
			DrawText(CountText, CountColor, Slot.X + Slot.W - XL - 3.f, Y + Slot.H - YL - 2.f, Font, 0.65f, false);
		}
	}

	// Band above the slots: the selected item's name, then the element with its swap key.
	const FString ItemLine = Ids.IsValidIndex(SelectedIndex) ? SlotDisplayName(Ids[SelectedIndex]) : FString();
	const UGFMagicComponent* Magic = PS ? PS->FindComponentByClass<UGFMagicComponent>() : nullptr;
	const FGFElementChip Chip = Magic
		? GFFormatElementChip(Magic->GetElement(), FGFMagicRules::FactionElement(PS->FactionId))
		: FGFElementChip();
	const FString ElementLine = Magic ? FString::Printf(TEXT("%s   %s"), *Chip.Label, *Chip.Hint) : FString();
	auto Measure = [this, Font](const FString& Text)
	{
		float XL = 0.f;
		float YL = 0.f;
		if (Font && !Text.IsEmpty())
		{
			Canvas->TextSize(Font, Text, XL, YL, 0.8f, 0.8f);
		}
		return Text.IsEmpty() ? 0.f : XL + 20.f;
	};
	const FGFHotbarBandLayout Band = GFLayoutHotbarBand(Canvas->ClipX, Canvas->ClipY, Measure(ItemLine), Measure(ElementLine) + (Magic ? 14.f : 0.f));
	if (!Band.Item.IsEmpty())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Band.Item.X, Band.Item.Y, Band.Item.W, Band.Item.H);
		DrawText(ItemLine, FLinearColor::White, Band.Item.X + 10.f, Band.Item.Y + 3.f, Font, 0.8f, false);
	}
	if (!Band.Element.IsEmpty())
	{
		const FGFHudRect& E = Band.Element;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), E.X, E.Y, E.W, E.H);
		DrawRect(Chip.Color, E.X, E.Y, 4.f, E.H);
		DrawRect(Chip.Color, E.X + 10.f, E.Y + E.H * 0.5f - 4.f, 8.f, 8.f);
		DrawText(Chip.Label, Chip.Color, E.X + 24.f, E.Y + 3.f, Font, 0.8f, false);
		float LabelW = 0.f;
		float LabelH = 0.f;
		if (Font)
		{
			Canvas->TextSize(Font, Chip.Label + TEXT("   "), LabelW, LabelH, 0.8f, 0.8f);
		}
		DrawText(Chip.Hint, Chip.bCanSwap ? FLinearColor(0.92f, 0.92f, 0.92f) : FLinearColor(0.7f, 0.7f, 0.7f), E.X + 24.f + LabelW, E.Y + 3.f, Font, 0.8f, false);
	}
}

void AGFHUD::DrawHint()
{
	const UWorld* World = GetWorld();
	const float Elapsed = (World && bShownAtSet) ? static_cast<float>(World->GetTimeSeconds() - ShownAt) : 0.f;
	if (!GFHintVisible(Elapsed, bHintUserOverride, bHintUserVisible) || Modal == EGFHudModal::Inventory)
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
	const float HotbarTop = GFHudHotbarTop(Canvas->ClipY);
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
	const FGFHudModalRules::FState Before{ Modal, Restore };
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
	if (Prev == EGFHudModal::Inventory && Modal != EGFHudModal::Inventory)
	{
		InvCursor.Cancel();
		InvHover = FGFInvTarget();
	}
	if (FGFHudModalRules::EndsElderSession(Before, Next))
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
	// Only Game (or chat over Game) takes the panel. Anything else refused it: drop the server session so a hidden
	// menu cannot still trade.
	if (!FGFHudModalRules::HasElder(Modal, Restore))
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

void AGFHUD::DrawInventoryScreen()
{
	if (Modal != EGFHudModal::Inventory || !Canvas)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayerController();
	const AGFPlayerState* PS = PC ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	UFont* Font = HudFont();
	const FGFInventoryLayout Layout = GFLayoutInventory(Canvas->ClipX, Canvas->ClipY);
	RefreshInventoryPage(Layout);
	const FLinearColor Gold(1.f, 0.86f, 0.32f, 1.f);

	// Everything that is not a cell, a slot or a button: a drop here takes a bar item back into the bag.
	AddHitBox(FVector2D(0.f, 0.f), FVector2D(Canvas->ClipX, Canvas->ClipY), TEXT("GFInv_Back"), true, 0);

	const FGFHudRect& P = Layout.Panel;
	DrawRect(FLinearColor(0.06f, 0.07f, 0.10f, 0.92f), P.X, P.Y, P.W, P.H);
	DrawText(TEXT("Bag"), FLinearColor(0.93f, 0.96f, 1.f), P.X + GFHUD::InvPad, P.Y + 6.f, Font, 1.f, false);
	DrawText(TEXT("Tab / I / Esc close"), FLinearColor(0.6f, 0.62f, 0.66f), P.X + GFHUD::InvPad + 48.f, P.Y + 9.f, Font, 0.7f, false);
	DrawRect(FLinearColor(0.3f, 0.1f, 0.1f, 0.9f), Layout.Close.X, Layout.Close.Y, Layout.Close.W, Layout.Close.H);
	DrawText(TEXT("X"), FLinearColor::White, Layout.Close.X + 7.f, Layout.Close.Y + 3.f, Font, 0.9f, false);
	AddHitBox(FVector2D(Layout.Close.X, Layout.Close.Y), FVector2D(Layout.Close.W, Layout.Close.H), TEXT("GFInv_Close"), true, 10);

	const TArray<FGFInventoryEntry> All = GetInventoryEntries();
	for (int32 Index = 0; Index < Layout.Tabs.Num(); ++Index)
	{
		const EGFInventoryTab Tab = static_cast<EGFInventoryTab>(Index);
		const FGFHudRect& T = Layout.Tabs[Index];
		const bool bOn = Tab == InvTab;
		DrawRect(bOn ? FLinearColor(0.22f, 0.19f, 0.09f, 0.95f) : FLinearColor(0.f, 0.f, 0.f, 0.45f), T.X, T.Y, T.W, T.H);
		const int32 InTab = FGFInventoryView::Filter(All, Tab).Num();
		DrawText(FString::Printf(TEXT("%s %d"), *FGFInventoryView::TabName(Tab), InTab), bOn ? Gold : FLinearColor(0.75f, 0.78f, 0.82f), T.X + 5.f, T.Y + 4.f, Font, 0.7f, false);
		AddHitBox(FVector2D(T.X, T.Y), FVector2D(T.W, T.H), FName(*FString::Printf(TEXT("GFInv_Tab_%d"), Index)), true, 10);
	}

	float MouseX = -1.f;
	float MouseY = -1.f;
	if (PC)
	{
		PC->GetMousePosition(MouseX, MouseY);
	}
	InvHover = FGFInvTarget();
	const FGFInventoryEntry* HoverEntry = nullptr;

	for (int32 Index = 0; Index < Layout.Cells.Num(); ++Index)
	{
		const FGFHudRect& C = Layout.Cells[Index];
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.12f), C.X - 1.f, C.Y - 1.f, C.W + 2.f, C.H + 2.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), C.X, C.Y, C.W, C.H);
		if (!InvPageEntries.IsValidIndex(Index))
		{
			continue;
		}
		const FGFInventoryEntry& Entry = InvPageEntries[Index];
		const bool bHeldHere = InvCursor.IsHolding() && InvCursor.GetFromSlot() == INDEX_NONE && InvCursor.GetHeld() == Entry.Id;
		if (GFHudContains(C, MouseX, MouseY))
		{
			InvHover = FGFInvTarget::Grid(Entry.Id);
			HoverEntry = &Entry;
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.6f), C.X - 2.f, C.Y - 2.f, C.W + 4.f, 2.f);
			DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.6f), C.X - 2.f, C.Bottom(), C.W + 4.f, 2.f);
		}
		DrawSlotArt(Entry.Id, C.X, C.Y, C.W, C.H, bHeldHere);
		const FString CountText = FGFHotbarRules::FormatCount(Entry.Count);
		if (!CountText.IsEmpty())
		{
			float XL = 0.f;
			float YL = 10.f;
			if (Font)
			{
				Canvas->TextSize(Font, CountText, XL, YL, 0.65f, 0.65f);
			}
			DrawText(CountText, FLinearColor::White, C.X + C.W - XL - 3.f, C.Y + C.H - YL - 2.f, Font, 0.65f, false);
		}
		AddHitBox(FVector2D(C.X, C.Y), FVector2D(C.W, C.H), FName(*FString::Printf(TEXT("GFInv_Cell_%d"), Index)), true, 10);
	}
	if (InvPageEntries.Num() == 0)
	{
		const TCHAR* Empty = InvTab == EGFInventoryTab::Blocks ? TEXT("No blocks yet. Dig some up with LMB.") : TEXT("Nothing here yet.");
		DrawText(Empty, FLinearColor(0.7f, 0.72f, 0.76f), Layout.Cells.Num() > 0 ? Layout.Cells[0].X : P.X + GFHUD::InvPad, (Layout.Cells.Num() > 0 ? Layout.Cells[0].Y : P.Y + 80.f) + 16.f, Font, 0.8f, false);
	}

	const FString PageText = GFFormatInventoryPage(InvPage, InvPageCount);
	if (!PageText.IsEmpty())
	{
		for (const FGFHudRect* Button : { &Layout.Prev, &Layout.Next })
		{
			const bool bPrev = Button == &Layout.Prev;
			const bool bLive = bPrev ? InvPage > 0 : InvPage + 1 < InvPageCount;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, bLive ? 0.6f : 0.25f), Button->X, Button->Y, Button->W, Button->H);
			DrawText(bPrev ? TEXT("< Prev") : TEXT("Next >"), FLinearColor(1.f, 1.f, 1.f, bLive ? 0.95f : 0.4f), Button->X + 8.f, Button->Y + 4.f, Font, 0.75f, false);
			AddHitBox(FVector2D(Button->X, Button->Y), FVector2D(Button->W, Button->H), bPrev ? TEXT("GFInv_Prev") : TEXT("GFInv_Next"), true, 10);
		}
		DrawText(PageText, FLinearColor(0.75f, 0.78f, 0.82f), P.X + P.W * 0.5f - 34.f, Layout.Prev.Y + 4.f, Font, 0.75f, false);
	}

	// The hotbar takes drops while the bag is open. The selected slot's 4 px lift is not part of its hit box.
	const TArray<FName> BarIds = PS ? PS->GetHotbarSlots() : TArray<FName>();
	const TArray<FGFHUDSlotRect> Bar = GFLayoutHotbar(FGFHotbarRules::SlotCount, INDEX_NONE, Canvas->ClipX, Canvas->ClipY);
	for (int32 Index = 0; Index < Bar.Num(); ++Index)
	{
		const FGFHUDSlotRect& S = Bar[Index];
		const FGFHudRect Rect{ S.X, S.Y, S.W, S.H };
		if (GFHudContains(Rect, MouseX, MouseY))
		{
			InvHover = FGFInvTarget::BarSlot(Index, BarIds.IsValidIndex(Index) ? BarIds[Index] : FName());
			if (InvCursor.IsHolding())
			{
				DrawRect(FLinearColor(0.4f, 0.9f, 0.5f, 0.35f), S.X, S.Y, S.W, S.H);
			}
		}
		if (InvCursor.GetFromSlot() == Index)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), S.X, S.Y, S.W, S.H);
		}
		AddHitBox(FVector2D(S.X, S.Y), FVector2D(S.W, S.H), FName(*FString::Printf(TEXT("GFInv_Slot_%d"), Index)), true, 10);
	}

	// What the cursor carries follows it; otherwise the tooltip for what it is over.
	if (InvCursor.IsHolding() && MouseX >= 0.f)
	{
		DrawSlotArt(InvCursor.GetHeld(), MouseX - 20.f, MouseY - 20.f, 40.f, 40.f, false);
	}
	else
	{
		FGFInventoryEntry BarEntry;
		if (!HoverEntry && InvHover.Kind == FGFInvTarget::EKind::Slot && !InvHover.Id.IsNone() && InvHover.Id != FGFHotbarRules::FistsId())
		{
			for (const FGFInventoryEntry& Entry : All)
			{
				if (Entry.Id == InvHover.Id)
				{
					BarEntry = Entry;
					HoverEntry = &BarEntry;
					break;
				}
			}
		}
		if (HoverEntry)
		{
			const UWorld* World = GetWorld();
			const AGFGameState* GS = World ? World->GetGameState<AGFGameState>() : nullptr;
			const FGFItemRow* Row = GS ? FGFEconomy::FindItem(GS->GetItemTable(), HoverEntry->ItemId) : nullptr;
			const TArray<FString> Lines = GFFormatInventoryTooltip(*HoverEntry, Row, SlotDisplayName(HoverEntry->Id));
			const FGFHudRect Tip = GFPlaceTooltip(MouseX, MouseY, Lines.Num(), Canvas->ClipX, Canvas->ClipY);
			DrawRect(FLinearColor(0.02f, 0.02f, 0.03f, 0.95f), Tip.X, Tip.Y, Tip.W, Tip.H);
			DrawRect(Gold, Tip.X, Tip.Y, Tip.W, 2.f);
			for (int32 Line = 0; Line < Lines.Num(); ++Line)
			{
				const bool bTitle = Line == 0;
				const bool bHowTo = Line == Lines.Num() - 1;
				const FLinearColor Color = bTitle ? Gold : (bHowTo ? FLinearColor(0.6f, 0.62f, 0.66f) : FLinearColor(0.9f, 0.9f, 0.9f));
				DrawText(Lines[Line], Color, Tip.X + GFHUD::TooltipPad, Tip.Y + GFHUD::TooltipPad + Line * GFHUD::TooltipLineH, Font, bTitle ? 0.9f : 0.75f, false);
			}
		}
	}

	// Say why the server refused the last move (owned elsewhere, spent meanwhile, too fast).
	if (!bLastHotbarReported && PS)
	{
		if (const EGFHotbarMoveResult* Result = PS->HotbarResults.Find(LastHotbarRequest))
		{
			if (*Result != EGFHotbarMoveResult::Pending)
			{
				bLastHotbarReported = true;
				if (*Result == EGFHotbarMoveResult::NotOwned)
				{
					PushToast(TEXT("You don't have any of that anymore."));
				}
				else if (*Result == EGFHotbarMoveResult::UnknownItem || *Result == EGFHotbarMoveResult::BadSlot)
				{
					PushToast(TEXT("That can't go on the hotbar."));
				}
				else if (*Result == EGFHotbarMoveResult::Busy)
				{
					PushToast(TEXT("Easy there. Try that again."));
				}
			}
		}
	}
}
