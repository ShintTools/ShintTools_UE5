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
	// ── Geometry, in Slate units ─────────────────────────────────────────────
	// The card is deliberately narrow. A wide assistant invites the user to
	// treat it as a document view, and it then covers the details panel it is
	// supposed to be commenting on.
	constexpr float kCardWidth    = 380.f;
	constexpr float kCardHeight   = 500.f;
	constexpr float kLauncherSize =  56.f;
	constexpr float kGap          =  14.f;   // card ↔ launcher
	constexpr float kMargin       =  24.f;   // launcher ↔ editor corner

	// ── State ────────────────────────────────────────────────────────────────
	// Module-scoped rather than owned by a widget: the launcher outlives the
	// card, so neither can own the other.
	static TSharedPtr<SWindow>            GCardWindow;
	static TSharedPtr<SWindow>            GLauncherWindow;
	static TWeakPtr<SShintAssistantDock>  GDock;
	static FTSTicker::FDelegateHandle     GTicker;

	// ── Brushes ──────────────────────────────────────────────────────────────
	// Slate stores the raw pointer, so these have to outlive every widget that
	// references them — module-lifetime TUniquePtrs, the pattern SShintCard
	// already uses.
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
				/*OutlineWidth=*/1.f);
		}
		return GCardBrush.Get();
	}

	/** A circle is just a rounded box at half its own size. */
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

	/** PerPixel where the platform offers it, which on Windows means "whenever
	 *  the desktop compositor is running" — i.e. always, in practice. The
	 *  fallback is not a failure: the window still shows, it just paints its
	 *  own background into the rounded corners and the launcher reads as a
	 *  square. Worth degrading rather than refusing to open. */
	EWindowTransparency ResolveTransparency()
	{
		if (const TSharedPtr<GenericApplication> App =
				FSlateApplication::Get().GetPlatformApplication())
		{
			return App->GetWindowTransparencySupport();
		}
		return EWindowTransparency::PerWindow;
	}

	/** The editor window the dock anchors to. */
	TSharedPtr<SWindow> AnchorWindow()
	{
		if (TSharedPtr<SWindow> Root = FGlobalTabmanager::Get()->GetRootWindow())
			return Root;
		return FSlateApplication::Get().GetActiveTopLevelWindow();
	}

	/** Park one window's bottom-right corner at an offset from the editor's,
	 *  in physical pixels. Slate sizes are DPI-independent; window rects are
	 *  not, so everything crossing that boundary is scaled here and nowhere
	 *  else. */
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

		// Only reshape on an actual change. ReshapeWindow is a real OS call and
		// this runs on a ticker; issuing it every tick makes the window visibly
		// lag the editor while it is being dragged.
		const FVector2D CurrentPos  = FVector2D(Window->GetPositionInScreen());
		const FVector2D CurrentSize = FVector2D(Window->GetSizeInScreen());
		if (CurrentPos.Equals(PosPx, 1.f) && CurrentSize.Equals(SizePx, 1.f))
			return;

		Window->ReshapeWindow(PosPx, SizePx);
	}

	/** Create one borderless, transparent, always-above-the-editor window. */
	TSharedPtr<SWindow> MakeDockWindow(const FVector2D& SizeSlate,
	                                   TSharedRef<SWidget> Content,
	                                   bool bFocusWhenShown)
	{
		const TSharedPtr<SWindow> Anchor = AnchorWindow();
		if (!Anchor.IsValid()) return nullptr;   // editor frame not up yet

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
			Window, Anchor.ToSharedRef(), /*bShowImmediately=*/true);

		return Window;
	}

	/** Keep both windows glued to the editor's bottom-right corner, and follow
	 *  the editor into and out of minimisation. A window-moved delegate is not
	 *  enough here: maximising, changing monitor and a DPI change all move the
	 *  anchor without firing one. */
	bool TickAnchor(float)
	{
		const TSharedPtr<SWindow> Anchor = AnchorWindow();
		if (!Anchor.IsValid())
		{
			SShintAssistantDock::Shutdown();
			return false;   // unregister the ticker
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
				// Right edges aligned with the launcher, sitting one gap above
				// it — the launcher's own height plus the gap is the offset.
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

// ─────────────────────────────────────────────────────────────────────────────
// Card
// ─────────────────────────────────────────────────────────────────────────────

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

			// Hairline under the header. A full SSeparator carries its own
			// padding and would push the thread away from the rule.
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
				// Compact chrome: the panel drops its own context strip and
				// paints no background, so the card's rounded corners stay
				// rounded instead of being covered by a square fill.
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

// ─────────────────────────────────────────────────────────────────────────────
// Launcher
// ─────────────────────────────────────────────────────────────────────────────

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
					// The launcher IS the close button while the card is up —
					// two controls in the same corner doing opposite things
					// would be the confusing version of this.
					return FShintIconStyle::GetBrush(
						SShintAssistantDock::IsExpanded()
							? "ShintTools.Icons.Cross"
							: "ShintTools.Icons.Bot");
				})
				// Dark glyph on the light disc.
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::Bg()))
			]
		]
	];
}

FReply SShintAssistantLauncher::OnMouseButtonDown(const FGeometry& MyGeometry,
                                                  const FPointerEvent& MouseEvent)
{
	// Claim only the right button — the left one belongs to the SButton child,
	// which never sees the event if this returns Handled for it.
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		return FReply::Handled().CaptureMouse(SharedThis(this));

	return FReply::Unhandled();
}

FReply SShintAssistantLauncher::OnMouseButtonUp(const FGeometry& MyGeometry,
                                                const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Deferred to the next frame on purpose: Shutdown() destroys the window
		// that owns this very widget, and doing that from inside its own input
		// handler tears the widget down mid-event. The one-shot ticker is the
		// cheapest "after this event has finished routing" there is.
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

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool SShintAssistantDock::IsExpanded()
{
	return GCardWindow.IsValid();
}

void SShintAssistantDock::Open()
{
	// The launcher is what makes the dock persistent, so it comes first and
	// stays for the rest of the session unless explicitly dismissed.
	if (!GLauncherWindow.IsValid())
	{
		GLauncherWindow = MakeDockWindow(
			FVector2D(kLauncherSize, kLauncherSize),
			SNew(SShintAssistantLauncher),
			/*bFocusWhenShown=*/false);

		if (!GLauncherWindow.IsValid())
			return;   // no editor frame yet — nothing to anchor to

		GTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickAnchor), 0.f);
	}

	if (!GCardWindow.IsValid())
	{
		TSharedPtr<SShintAssistantDock> Dock;
		GCardWindow = MakeDockWindow(
			FVector2D(kCardWidth, kCardHeight),
			SAssignNew(Dock, SShintAssistantDock),
			/*bFocusWhenShown=*/true);
		GDock = Dock;
	}

	// Place both immediately rather than waiting for the first tick, so the
	// card does not flash at the origin before sliding into the corner.
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
