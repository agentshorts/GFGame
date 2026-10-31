#include "GFCrosshairPanel.h"
#include "Aim/GFAimSim.h"
#include "GFCharacter.h"
#include "GFCrosshairTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	class SGFCrosshairPreview : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGFCrosshairPreview) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		void SetSettings(const FGFCrosshairSettings& In)
		{
			Settings = In;
			Invalidate(EInvalidateWidget::Paint);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(360.f, 150.f);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
			auto Box = [&](float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements, Layer,
					AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
					White, ESlateDrawEffect::None, Color);
			};
			Box(0.f, 0.f, Size.X, Size.Y * 0.62f, FLinearColor(0.45f, 0.62f, 0.78f, 1.f), LayerId);
			Box(0.f, Size.Y * 0.62f, Size.X, Size.Y * 0.38f, FLinearColor(0.36f, 0.48f, 0.28f, 1.f), LayerId);
			// A chunky body on the left, so the sample matches the over-shoulder frame.
			Box(46.f, 48.f, 28.f, 36.f, FLinearColor(0.93f, 0.78f, 0.55f, 1.f), LayerId + 1);
			Box(50.f, 28.f, 20.f, 22.f, FLinearColor(0.95f, 0.82f, 0.62f, 1.f), LayerId + 1);
			const FGFCrosshairDraw Draw = GFBuildCrosshair(Settings, Size.X * 0.5f, Size.Y * 0.5f, 0.f);
			for (const FGFCrosshairMark& Mark : Draw.Marks)
			{
				if (Mark.Kind == EGFCrosshairMarkKind::Dot)
				{
					const float D = Mark.Radius * 2.f;
					Box(Mark.X - Mark.Radius, Mark.Y - Mark.Radius, D, D, Mark.Color, LayerId + 2);
					continue;
				}
				TArray<FVector2f> Points;
				Points.Add(FVector2f(Mark.X, Mark.Y));
				Points.Add(FVector2f(Mark.X2, Mark.Y2));
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Mark.Color, true, Mark.Thickness);
			}
			return LayerId + 3;
		}

		FGFCrosshairSettings Settings;
	};

	class SGFAimSettingsPanel : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGFAimSettingsPanel) {}
			SLATE_ARGUMENT(TWeakObjectPtr<AGFCharacter>, Character)
			SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Character = InArgs._Character;
			OnClose = InArgs._OnClose;
			if (AGFCharacter* Body = Character.Get())
			{
				Crosshair = Body->GetCrosshairSettings();
				AimUser = Body->GetAimUserSettings();
			}
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.07f, 0.94f))
				.Padding(12.f)
				[
					SNew(SBox)
					.WidthOverride(400.f)
					.MaxDesiredHeight(640.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(Column, SVerticalBox)
						]
					]
				]
			];
			Rebuild();
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual FReply OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override
		{
			if (KeyEvent.GetKey() == EKeys::Escape)
			{
				OnClose.ExecuteIfBound();
				return FReply::Handled();
			}
			return SCompoundWidget::OnPreviewKeyDown(Geometry, KeyEvent);
		}

	private:
		void PushCrosshair()
		{
			Crosshair.Clamp();
			if (AGFCharacter* Body = Character.Get())
			{
				Body->SetCrosshairSettings(Crosshair);
			}
			if (Preview.IsValid())
			{
				Preview->SetSettings(Crosshair);
			}
			if (ShareText.IsValid())
			{
				ShareText->SetText(FText::FromString(Crosshair.ToShareCode()));
			}
		}

		void PushAim()
		{
			AimUser.Clamp();
			if (AGFCharacter* Body = Character.Get())
			{
				Body->SetAimUserSettings(AimUser);
			}
		}

		TSharedRef<SWidget> ShapeButton(const TCHAR* Label, EGFCrosshairShape Shape)
		{
			return SNew(SButton)
				.OnClicked_Lambda([this, Shape]()
				{
					Crosshair.Shape = Shape;
					PushCrosshair();
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(Label))
				];
		}

		TSharedRef<SWidget> Spin(float Min, float Max, TAttribute<float> Value, TFunction<void(float)> Apply, TSharedPtr<SSpinBox<float>>& Slot)
		{
			return SAssignNew(Slot, SSpinBox<float>)
				.MinValue(Min)
				.MaxValue(Max)
				.MinSliderValue(Min)
				.MaxSliderValue(Max)
				.Delta(Max > 20.f ? 1.f : 0.1f)
				.Value(Value)
				.OnValueChanged_Lambda([Apply](float NewValue) { Apply(NewValue); });
		}

		void Rebuild()
		{
			if (!Column.IsValid())
			{
				return;
			}
			Column->ClearChildren();
			auto Header = [this](const TCHAR* Text)
			{
				Column->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Text))
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))
					.ColorAndOpacity(FLinearColor::White)
				];
			};
			auto Row = [this](TSharedRef<SWidget> Widget)
			{
				Column->AddSlot().AutoHeight().Padding(0.f, 2.f)
				[
					Widget
				];
			};

			Header(TEXT("Settings"));
			Header(TEXT("Crosshair"));
			Row(SAssignNew(Preview, SGFCrosshairPreview));
			Preview->SetSettings(Crosshair);

			Row(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("Plus"), EGFCrosshairShape::Plus) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("Dot"), EGFCrosshairShape::Dot) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("Circle"), EGFCrosshairShape::Circle) ]);
			Row(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("Plus+Dot"), EGFCrosshairShape::PlusDot) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("T"), EGFCrosshairShape::T) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)[ ShapeButton(TEXT("Chevron"), EGFCrosshairShape::Chevron) ]);

			TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
			for (const FGFCrosshairPreset& Preset : GFCrosshairPresets())
			{
				const FLinearColor Swatch = FLinearColor::FromSRGBColor(Preset.Color);
				Swatches->AddSlot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
					.ButtonColorAndOpacity(Swatch)
					.OnClicked_Lambda([this, Swatch]()
					{
						Crosshair.Color = Swatch;
						PushCrosshair();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(FText::FromString(Preset.Name))
					]
				];
			}
			Row(Swatches);

			Row(SNew(STextBlock).Text(FText::FromString(TEXT("Size"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(2.f, 40.f, TAttribute<float>::CreateLambda([this]() { return Crosshair.Size; }), [this](float V) { Crosshair.Size = V; PushCrosshair(); }, SizeBox));
			Row(SNew(STextBlock).Text(FText::FromString(TEXT("Gap"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(0.f, 20.f, TAttribute<float>::CreateLambda([this]() { return Crosshair.Gap; }), [this](float V) { Crosshair.Gap = V; PushCrosshair(); }, GapBox));
			Row(SNew(STextBlock).Text(FText::FromString(TEXT("Thickness"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(1.f, 6.f, TAttribute<float>::CreateLambda([this]() { return Crosshair.Thickness; }), [this](float V) { Crosshair.Thickness = V; PushCrosshair(); }, ThickBox));
			Row(SNew(STextBlock).Text(FText::FromString(TEXT("Opacity %"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(0.f, 100.f, TAttribute<float>::CreateLambda([this]() { return Crosshair.Opacity * 100.f; }), [this](float V) { Crosshair.Opacity = V / 100.f; PushCrosshair(); }, OpacityBox));
			Row(SNew(STextBlock).Text(FText::FromString(TEXT("Outline px (0 off)"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(0.f, 2.f, TAttribute<float>::CreateLambda([this]() { return Crosshair.bOutline ? Crosshair.OutlinePx : 0.f; }), [this](float V)
			{
				Crosshair.bOutline = V >= 0.5f;
				if (Crosshair.bOutline)
				{
					Crosshair.OutlinePx = FMath::Clamp(V, 1.f, 2.f);
				}
				PushCrosshair();
			}, OutlineBox));

			auto Check = [this](const TCHAR* Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
			{
				return SNew(SCheckBox)
					.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([Set](ECheckBoxState State) { Set(State == ECheckBoxState::Checked); })
					[
						SNew(STextBlock).Text(FText::FromString(Label)).ColorAndOpacity(FLinearColor::White)
					];
			};
			Row(Check(TEXT("Centre dot"), [this]() { return Crosshair.bCenterDot; }, [this](bool V) { Crosshair.bCenterDot = V; PushCrosshair(); }));
			Row(Check(TEXT("Dynamic spread"), [this]() { return Crosshair.bDynamicSpread; }, [this](bool V) { Crosshair.bDynamicSpread = V; PushCrosshair(); }));
			Row(Check(TEXT("Separate ADS crosshair"), [this]() { return Crosshair.bSeparateAdsCrosshair; }, [this](bool V) { Crosshair.bSeparateAdsCrosshair = V; PushCrosshair(); }));
			Row(Check(TEXT("Hold to aim (off = toggle)"), [this]() { return AimUser.bHoldToAim; }, [this](bool V) { AimUser.bHoldToAim = V; PushAim(); }));
			Row(Check(TEXT("Sniper scope overlay"), [this]() { return AimUser.bScopeOverlay; }, [this](bool V) { AimUser.bScopeOverlay = V; PushAim(); }));
			Row(SNew(STextBlock).Text(FText::FromString(TEXT("ADS sensitivity %"))).ColorAndOpacity(FLinearColor::White));
			Row(Spin(50.f, 150.f, TAttribute<float>::CreateLambda([this]() { return AimUser.SensitivityScale * 100.f; }), [this](float V) { AimUser.SensitivityScale = V / 100.f; PushAim(); }, SensBox));

			Header(TEXT("Share code"));
			Row(SAssignNew(ShareText, SEditableText)
				.Text(FText::FromString(Crosshair.ToShareCode()))
				.ColorAndOpacity(FLinearColor::White)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11)));
			Row(SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)
				[
					SNew(SButton)
					.OnClicked_Lambda([this]()
					{
						FGFCrosshairSettings Next;
						const FString Code = ShareText.IsValid() ? ShareText->GetText().ToString() : FString();
						if (FGFCrosshairSettings::FromShareCode(Code, Next))
						{
							Crosshair = Next;
							PushCrosshair();
							if (SizeBox.IsValid()) SizeBox->SetValue(Crosshair.Size);
							if (GapBox.IsValid()) GapBox->SetValue(Crosshair.Gap);
							if (ThickBox.IsValid()) ThickBox->SetValue(Crosshair.Thickness);
							if (OpacityBox.IsValid()) OpacityBox->SetValue(Crosshair.Opacity * 100.f);
							if (OutlineBox.IsValid()) OutlineBox->SetValue(Crosshair.bOutline ? Crosshair.OutlinePx : 0.f);
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Apply code")))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(1.f)
				[
					SNew(SButton)
					.OnClicked_Lambda([this]()
					{
						Crosshair = FGFCrosshairSettings();
						AimUser = FGFAimUserSettings();
						PushCrosshair();
						PushAim();
						if (ShareText.IsValid()) ShareText->SetText(FText::FromString(Crosshair.ToShareCode()));
						if (SizeBox.IsValid()) SizeBox->SetValue(Crosshair.Size);
						if (GapBox.IsValid()) GapBox->SetValue(Crosshair.Gap);
						if (ThickBox.IsValid()) ThickBox->SetValue(Crosshair.Thickness);
						if (OpacityBox.IsValid()) OpacityBox->SetValue(Crosshair.Opacity * 100.f);
						if (OutlineBox.IsValid()) OutlineBox->SetValue(0.f);
						if (SensBox.IsValid()) SensBox->SetValue(100.f);
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Reset")))
					]
				]);
			Row(SNew(SButton)
				.OnClicked_Lambda([this]()
				{
					OnClose.ExecuteIfBound();
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Close")))
				]);
		}

		TWeakObjectPtr<AGFCharacter> Character;
		FSimpleDelegate OnClose;
		FGFCrosshairSettings Crosshair;
		FGFAimUserSettings AimUser;
		TSharedPtr<SVerticalBox> Column;
		TSharedPtr<SGFCrosshairPreview> Preview;
		TSharedPtr<SEditableText> ShareText;
		TSharedPtr<SSpinBox<float>> SizeBox;
		TSharedPtr<SSpinBox<float>> GapBox;
		TSharedPtr<SSpinBox<float>> ThickBox;
		TSharedPtr<SSpinBox<float>> OpacityBox;
		TSharedPtr<SSpinBox<float>> OutlineBox;
		TSharedPtr<SSpinBox<float>> SensBox;
	};
}

TSharedRef<SWidget> GFMakeAimSettingsPanel(AGFCharacter* Character, TFunction<void()> OnClose)
{
	return SNew(SGFAimSettingsPanel)
		.Character(Character)
		.OnClose(FSimpleDelegate::CreateLambda([OnClose]()
		{
			if (OnClose)
			{
				OnClose();
			}
		}));
}
