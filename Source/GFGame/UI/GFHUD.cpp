#include "GFHUD.h"
#include "GFZoneCatalog.h"
#include "GFZoneLayout.h"
#include "GFZoneSubsystem.h"
#include "GFHUDFormat.h"
#include "GFCharacter.h"
#include "GFCharacterMovementComponent.h"
#include "GFMovementTuning.h"
#include "GFStaminaSim.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFTerritorySubsystem.h"
#include "GFVoxelWorld.h"
#include "Engine/Canvas.h"
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
	if (bChatOpen)
	{
		if (APlayerController* PC = GetOwningPlayerController())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = false;
		}
	}
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
	if (bChatOpen)
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
	DrawCrosshair();
	DrawCompass();
	DrawStaminaArc();
	DrawZoneLabel();
	DrawChat();
	DrawToasts();
	DrawHotbar();
	DrawHint();
}

void AGFHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const FLinearColor Color(1.f, 1.f, 1.f, 0.9f);
	const float Arm = 10.f;
	const float Gap = 4.f;
	const float Thick = 1.5f;
	DrawLine(CX - Arm, CY, CX - Gap, CY, Color, Thick);
	DrawLine(CX + Gap, CY, CX + Arm, CY, Color, Thick);
	DrawLine(CX, CY - Arm, CX, CY - Gap, Color, Thick);
	DrawLine(CX, CY + Gap, CX, CY + Arm, Color, Thick);
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
	TArray<FName> Ids;
	for (TActorIterator<AGFVoxelWorld> It(GetWorld()); It; ++It)
	{
		Ids = It->GetPlaceableBlockIds();
		break;
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
	if (bChatOpen || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	TSharedRef<SGFChatText> Text = SNew(SGFChatText)
		.OnTextCommitted(FOnTextCommitted::CreateLambda([this](const FText& Committed, ETextCommit::Type Method)
		{
			if (!bChatOpen)
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
	GEngine->GameViewport->AddViewportWidgetContent(Chat->Root.ToSharedRef());
	bChatOpen = true;

	if (APlayerController* PC = GetOwningPlayerController())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Text);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = false;
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(Text, EFocusCause::SetDirectly);
	}
}

void AGFHUD::CloseChat()
{
	DismissChat();
}

void AGFHUD::SubmitChat()
{
	if (!bChatOpen)
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
	if (!bChatOpen && !Chat)
	{
		return;
	}
	bChatOpen = false;
	ChatBlockTicks = 2;
	if (APlayerController* PC = GetOwningPlayerController())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	RemoveChatWidget();
}

void AGFHUD::RemoveChatWidget()
{
	// Drop the flag before removing the widget. Focus loss commits OnUserMovedFocus, and that must not re-enter.
	bChatOpen = false;
	FGHUDChat* Dying = Chat;
	Chat = nullptr;
	if (Dying && Dying->Root.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Dying->Root.ToSharedRef());
	}
	delete Dying;
}
