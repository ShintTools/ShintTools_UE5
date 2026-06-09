// Copyright 2026 ShintTools. All Rights Reserved.
//
// Panel shell — Construct (the SCompoundWidget body), the destructor (only
// non-trivial because of the /agent/explain modal teardown), and the
// SetDestinationIndex router.
//
// The rest of the panel lives in sibling translation units:
//   * SShintToolsPanel_Private.h   — shared types + free-function decls
//   * SShintToolsPanel_Common.cpp  — brush cache, toast, KPI tile, static
//                                    SShintToolsPanel helpers, CoreRedirects writer
//   * SShintToolsPanel_State.cpp   — Populate/Filter/Refresh/QualityScore,
//                                    state setters, attribute getters,
//                                    HandleValidateResult
//   * SShintToolsPanel_Config.cpp  — Settings destination
//   * SShintToolsPanel_Code.cpp    — Code Validator section + light handlers
//   * SShintToolsPanel_Asset.cpp   — Asset Naming Bot section + light handlers
//   * SShintToolsPanel_Fixes.cpp   — code fix + asset rename heavy flows
//   * SShintToolsPanel_Http.cpp    — every HTTP completion callback
//   * SShintToolsPanel_Overview.cpp — BuildOverviewHero (4-up KPI grid)
//   * SShintToolsPanel_Explain.cpp  — /agent/explain modal
//
// Why this split — the original single TU grew past 3.5k lines and forced
// every panel change to recompile the full file. The split groups code by
// concern: each section owns its widget builders + handlers, and the
// stateful controller logic (filter, populate, score) lives in its own TU.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "ShintDashboardSync.h"
#include "CoreProcessManager.h"

// Shared design-system widgets (UI redesign foundation)
#include "ShintStyle.h"
#include "SShintSidebar.h"
#include "SShintTopBar.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

// Slate layout
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"

#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	CoreClient     = MakeShared<FShintCoreClient>();
	DashboardSync  = MakeShared<FShintDashboardSync>(*CoreClient);
	ProcessManager = MakeShared<FCoreProcessManager>();

	// Kick off an initial /health probe so the Settings tab's LED + the
	// TopBar's status text both reflect reality on first paint instead of
	// waiting for the user to click Refresh.
	SetStatus(ECoreStatus::Checking);
	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));

	// UI-REDESIGN — dashboard shell: VBox(TopBar) over HBox(Sidebar, SwitcherContent).
	// The sidebar pushes destination changes to SetDestinationIndex(); the
	// switcher reads CurrentDestinationIndex via a lambda so the routing stays
	// declarative without manual SetActiveWidgetIndex() calls.
	//
	// Each destination wraps a single existing Build*Section() inside a
	// SScrollBox so per-section vertical scrolling works independently of the
	// sidebar — the rail itself never scrolls.

	auto CurrentTitle = [this]() -> FText
	{
		switch (static_cast<EShintDestination>(CurrentDestinationIndex))
		{
		case EShintDestination::Code:     return LOCTEXT("TitleCode",     "Code Validator");
		case EShintDestination::Assets:   return LOCTEXT("TitleAssets",   "Asset Naming Bot");
		case EShintDestination::Settings: return LOCTEXT("TitleSettings", "Settings");
		case EShintDestination::Overview:
		default:                          return LOCTEXT("TitleOverview", "Overview");
		}
	};

	auto StatusText = [this]() -> FText
	{
		switch (static_cast<EShintConnState>(CurrentConnStateIndex))
		{
		case EShintConnState::Connected:    return LOCTEXT("Conn",  "Connected");
		case EShintConnState::Connecting:   return LOCTEXT("Probe", "Connecting…");
		case EShintConnState::Disconnected: return LOCTEXT("Down",  "Core offline");
		case EShintConnState::Unknown:
		default:
			// Empty before the first health check resolves — the LED dot alone
			// signals "no info yet" and the previous "Idle" label was clutter
			// that customers found confusing (looked like the plugin was disabled).
			return FText::GetEmpty();
		}
	};

	auto ActiveDest = [this]() -> EShintDestination
	{
		return static_cast<EShintDestination>(CurrentDestinationIndex);
	};

	auto ConnState = [this]() -> EShintConnState
	{
		return static_cast<EShintConnState>(CurrentConnStateIndex);
	};

	// License tier label fed by FShintToolsModule's startup probe of
	// /license/status (added in Sprint 2). Renders as a pill badge in the
	// top-right of the TopBar so paid users immediately see "Indie" / "Studio"
	// / "Enterprise" instead of waiting for the first scan to confirm their
	// tier. Hidden until the probe resolves to avoid a confusing "Free" flash
	// for paid customers during boot.
	auto TierText = []() -> FText
	{
		const FString Tier = FShintToolsModule::GetCachedTier();
		if (Tier.IsEmpty())
		{
			return FText::GetEmpty();
		}
		// Capitalise for display: "indie" -> "Indie".
		FString Display = Tier.ToLower();
		if (!Display.IsEmpty())
		{
			Display[0] = FChar::ToUpper(Display[0]);
		}
		return FText::FromString(Display);
	};

	// Wrap a section in a vertical scrollbox so long content doesn't push the
	// sidebar/topbar off-screen. Padding around the section uses S5 (24px) to
	// give the dashboard feel some breathing room from the edges.
	auto WrapSection = [](TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
	{
		return SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			.Padding(FMargin(FShintStyle::Space::S5))
			[ Content ];
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			// Top bar
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SShintTopBar)
				.Title_Lambda(CurrentTitle)
				.StatusText_Lambda(StatusText)
				.TierText_Lambda(TierText)
				.ConnState_Lambda(ConnState)
			]

			// Body: sidebar | content
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SHorizontalBox)

				// Left rail
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SShintSidebar)
					.Active_Lambda(ActiveDest)
					.OnSelected_Lambda([this](EShintDestination Dest)
					{
						SetDestinationIndex(static_cast<int32>(Dest));
					})
				]

				// Destination switcher
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(DestinationSwitcher, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return CurrentDestinationIndex; })

					// 0 — Overview hero: 4-up KPI grid bound to LastQualityScore.
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildOverviewHero()) ]

					// 1 — Code Validator
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildCodeValidatorSection()) ]

					// 2 — Asset Naming Bot
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildAssetNamingSection()) ]

					// 3 — Settings (was Config Section, now its own destination)
					+ SWidgetSwitcher::Slot()
					[ WrapSection(BuildConfigSection()) ]
				]
			]
		]
	];
}

void SShintToolsPanel::SetDestinationIndex(int32 Index)
{
	// Clamp defensively so an out-of-range value can't crash the switcher.
	if (Index < 0) Index = 0;
	if (Index > 3) Index = 3;
	CurrentDestinationIndex = Index;
	// The switcher's WidgetIndex_Lambda will read the new value on the next
	// tick — no explicit refresh needed.
}

SShintToolsPanel::~SShintToolsPanel()
{
	// Clean up the Explain modal + its rotating-status ticker. Without this,
	// the SWindow was orphaned in FSlateApplication's window list whenever the
	// panel was destroyed without the user clicking Close, and the ticker
	// delegate would keep firing against a dead `this` (the ticker also
	// previously held the last strong ref — see bug-hunt issue #5).
	if (ExplainTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ExplainTickerHandle);
		ExplainTickerHandle.Reset();
	}
	if (ExplainWindow.IsValid())
	{
		ExplainWindow->RequestDestroyWindow();
		ExplainWindow.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
