// Copyright 2026 ShintTools. All Rights Reserved.

#include "Assistant/SShintAssistantDock.h"
#include "Assistant/SShintAssistantPanel.h"

#include "ShintStyle.h"
#include "ShintIconStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SShintAssistantDock"

namespace ShintAssistantDockPrivate
{

	constexpr float kCardWidth    = 380.f;
	constexpr float kCardHeight   = 500.f;
	constexpr float kLauncherSize =  56.f;
	constexpr float kGap          =  14.f;
	constexpr float kMargin       =  24.f;

	static TSharedPtr<SWindow>            GCardWindow;
	static TSharedPtr<SWindow>            GLauncherWindow;
	static TWeakPtr<SShintAssistantDock>  GDock;
	static FTSTicker::FDelegateHandle     GTicker;

	static TUniquePtr<FSlateRoundedBoxBrush> GCardBrush;
	static TUniquePtr<FSlateRoundedBoxBrush> GLauncherBrush;
	static TUniquePtr<FSlateRoundedBoxBrush> GLauncherHoverBrush;

	const FSlateBrush* CardBrush()
	{
		if (!GCardBrush.IsValid())
		{
			GCardBrush = MakeUnique<FSlateRoundedBoxBrush>(
				FShintStyle::Colors::BgCard(),
				FShintStyle::Radius::Modal,
				FShintStyle::Colors::BorderSubtle(),
				1.f);
		}
		return GCardBrush.Get();
	}

	const FSlateBrush* LauncherBrush(bool bHovered)
	{
		TUniquePtr<FSlateRoundedBoxBrush>& Slot =
			bHovered ? GLauncherHoverBrush : GLauncherBrush;

		if (!Slot.IsValid())
		{
			Slot = MakeUnique<FSlateRoundedBoxBrush>(
				bHovered ? FLinearColor(0.90f, 0.90f, 0.90f)
				         : FShintStyle::Colors::TextPrimary(),
				kLauncherSize * 0.5f);
		}
		return Slot.Get();
	}

	EWindowTransparency ResolveTransparency()
	{
		if (const TSharedPtr<GenericApplication> App =
				FSlateApplication::Get().GetPlatformApplication())
		{
			return App->GetWindowTransparencySupport();
		}
		return EWindowTransparency::PerWindow;
	}

	TSharedPtr<SWindow> AnchorWindow()
	{
		if (TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow())
			return Root;
		return FSlateApplication::Get().GetActiveTopLevelWindow();
	}

	void PlaceFromBottomRight(const TSharedPtr<SWindow>& Window,
	                          const TSharedPtr<SWindow>& Anchor,
	                          const FVector2D& SizeSlate,
	                          float RightInset, float BottomInset)
	{
		if (!Window.IsValid() || !Anchor.IsValid()) return;

		const float DPI = Anchor->GetDPIScaleFactor();
		const FVector2D AnchorPos  = FVector2D(Anchor->GetPositionInScreen());
		const FVector2D AnchorSize = FVector2D(Anchor->GetSizeInScreen());
		const FVector2D SizePx     = SizeSlate * DPI;

		const FVector2D PosPx(
			AnchorPos.X + AnchorSize.X - (RightInset  * DPI) - SizePx.X,
			AnchorPos.Y + AnchorSize.Y - (BottomInset * DPI) - SizePx.Y);

		const FVector2D CurrentPos  = FVector2D(Window->GetPositionInScreen());
		const FVector2D CurrentSize = FVector2D(Window->GetSizeInScreen());
		if (CurrentPos.Equals(PosPx, 1.f) && CurrentSize.Equals(SizePx, 1.f))
			return;

		Window->ReshapeWindow(PosPx, SizePx);
	}

	TSharedPtr<SWindow> MakeDockWindow(const FVector2D& SizeSlate,
	                                   TSharedRef<SWidget> Content,
	                                   bool bFocusWhenShown)
	{
		const TSharedPtr<SWindow> Anchor = AnchorWindow();
		if (!Anchor.IsValid()) return nullptr;

		TSharedRef<SWindow> Window = SNew(SWindow)
			.CreateTitleBar(false)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.HasCloseButton(false)
			.SizingRule(ESizingRule::FixedSize)
			.SupportsTransparency(ResolveTransparency())
			.InitialOpacity(1.f)
			.FocusWhenFirstShown(bFocusWhenShown)
			.UseOSWindowBorder(false)
			.AutoCenter(EAutoCenter::None)
			.ClientSize(SizeSlate)
			[
				Content
			];

		FSlateApplication::Get().AddWindowAsNativeChild(
			Window, Anchor.ToSharedRef(), true);

		return Window;
	}

	bool TickAnchor(float)
	{
		const TSharedPtr<SWindow> Anchor = AnchorWindow();
		if (!Anchor.IsValid())
		{
			SShintAssistantDock::Shutdown();
			return false;
		}

		const bool bVisible = !Anchor->IsWindowMinimized();

		if (GLauncherWindow.IsValid())
		{
			bVisible ? GLauncherWindow->ShowWindow() : GLauncherWindow->HideWindow();
			if (bVisible)
			{
				PlaceFromBottomRight(
					GLauncherWindow, Anchor,
					FVector2D(kLauncherSize, kLauncherSize),
					kMargin, kMargin);
			}
		}

		if (GCardWindow.IsValid())
		{
			bVisible ? GCardWindow->ShowWindow() : GCardWindow->HideWindow();
			if (bVisible)
			{

				PlaceFromBottomRight(
					GCardWindow, Anchor,
					FVector2D(kCardWidth, kCardHeight),
					kMargin, kMargin + kLauncherSize + kGap);
			}
		}

		return true;
	}
}

using namespace ShintAssistantDockPrivate;

void SShintAssistantDock::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(CardBrush())
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildHeader()
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(1.f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::BorderSubtle()))
				]
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[

				SAssignNew(Panel, SShintAssistantPanel)
				.bCompact(true)
			]
		]
	];
}

TSharedRef<SWidget> SShintAssistantDock::BuildHeader()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth().VAlign(VAlign_Center)
		.Padding(FShintStyle::Space::S4, FShintStyle::Space::S3,
		         0.f, FShintStyle::Space::S3)
		[
			SNew(SImage)
			.Image(FShintIconStyle::GetBrush("ShintTools.Icons.Sparkles"))
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth().VAlign(VAlign_Center)
		.Padding(FShintStyle::Space::S2, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DockTitle", "AI Assistant"))
			.Font(FShintStyle::Fonts::H2())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		]

		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNullWidget::NullWidget
		]

		+ SHorizontalBox::Slot()
		.AutoWidth().VAlign(VAlign_Center)
		.Padding(0.f, 0.f, FShintStyle::Space::S3, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(FShintStyle::Space::S2, FShintStyle::Space::S1))
			.ToolTipText(LOCTEXT("ClearTip",
				"Start a new conversation. Nothing already remembered is lost — "
				"this thread simply stops being the active one."))
			.OnClicked(this, &SShintAssistantDock::OnClearClicked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Clear", "Clear"))
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		];
}

FReply SShintAssistantDock::OnClearClicked()
{
	if (Panel.IsValid())
		Panel->ClearConversation();
	return FReply::Handled();
}

void SShintAssistantLauncher::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(0.f)
		.ToolTipText(LOCTEXT("LauncherTip",
			"ShintTools AI Assistant — click to open, right-click to hide"))
		.OnClicked_Lambda([]()
		{
			SShintAssistantDock::Toggle();
			return FReply::Handled();
		})
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]()
			{
				return LauncherBrush(IsHovered());
			})
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.f)
			[
				SNew(SImage)
				.Image_Lambda([]()
				{

					return FShintIconStyle::GetBrush(
						SShintAssistantDock::IsExpanded()
							? "ShintTools.Icons.Cross"
							: "ShintTools.Icons.Bot");
				})

				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::Bg()))
			]
		]
	];
}

FReply SShintAssistantLauncher::OnMouseButtonDown(const FGeometry& MyGeometry,
                                                  const FPointerEvent& MouseEvent)
{

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		return FReply::Handled().CaptureMouse(SharedThis(this));

	return FReply::Unhandled();
}

FReply SShintAssistantLauncher::OnMouseButtonUp(const FGeometry& MyGeometry,
                                                const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([](float) -> bool
			{
				SShintAssistantDock::Shutdown();
				return false;
			}), 0.f);

		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

bool SShintAssistantDock::IsExpanded()
{
	return GCardWindow.IsValid();
}

void SShintAssistantDock::Open()
{

	if (!GLauncherWindow.IsValid())
	{
		GLauncherWindow = MakeDockWindow(
			FVector2D(kLauncherSize, kLauncherSize),
			SNew(SShintAssistantLauncher),
			false);

		if (!GLauncherWindow.IsValid())
			return;

		GTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickAnchor), 0.f);
	}

	if (!GCardWindow.IsValid())
	{
		TSharedPtr<SShintAssistantDock> Dock;
		GCardWindow = MakeDockWindow(
			FVector2D(kCardWidth, kCardHeight),
			SAssignNew(Dock, SShintAssistantDock),
			true);
		GDock = Dock;
	}

	TickAnchor(0.f);

	if (const TSharedPtr<SShintAssistantDock> Dock = GDock.Pin())
	{
		if (Dock->Panel.IsValid())
			Dock->Panel->FocusComposer();
	}
}

void SShintAssistantDock::Collapse()
{
	if (GCardWindow.IsValid())
	{
		GCardWindow->RequestDestroyWindow();
		GCardWindow.Reset();
		GDock.Reset();
	}
}

void SShintAssistantDock::Toggle()
{
	IsExpanded() ? Collapse() : Open();
}

void SShintAssistantDock::Shutdown()
{
	Collapse();

	if (GTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GTicker);
		GTicker.Reset();
	}

	if (GLauncherWindow.IsValid())
	{
		GLauncherWindow->RequestDestroyWindow();
		GLauncherWindow.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
