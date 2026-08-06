// Copyright 2026 ShintTools. All Rights Reserved.
//
// Code Validator destination — every widget builder under the Code section
// (the KPI tile row, scan buttons, filter combos, results panel, per-issue
// row factory) plus the lightweight button handlers that drive the scan
// flow (OnCheckConnection / OnScanProject / OnScanBlueprints / select-all
// helpers / dashboard push / IgnoreSingleFix).
//
// Heavier flows live elsewhere:
//   - OnApplySelectedCodeFixesClicked / OnApplySingleFix → _Fixes.cpp
//   - FetchFixPreview                                    → _Fixes.cpp
//   - OnExplainIssueClicked                              → _Explain.cpp

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "Core/ShintAssistantContext.h"

#include "Framework/Docking/TabManager.h"   // TryInvokeTab — Explain -> Assistant
#include "Misc/Paths.h"
// [DASH-STRIP-BEGIN]
#include "ShintDashboardSync.h"
// [DASH-STRIP-END]

#include "ShintStyle.h"
#include "SShintSeverityBadge.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
#include "Styling/AppStyle.h"

#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Section root
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildCodeValidatorSection()
{
	// LOD/Asset-Optimizer design language — caption (muted) over a big H1
	// value over a coloured subtitle, on a Surface card. Mirrors the Tile
	// lambda in BuildLodKpiRow so all three modules read as one system.
	auto Tile = [](const FText& Caption, TSharedPtr<STextBlock>& OutValue,
		const FText& Sub, const FLinearColor& SubColor) -> TSharedRef<SWidget>
	{
		// Rounded card treatment (BgCard fill + subtle border) — the same brush
		// SShintCard paints, so the KPI tiles read as one system with the
		// Overview hero cards instead of flat squared surfaces.
		return SNew(SBorder)
			.BorderImage(ST4::Outline(FShintStyle::Colors::BgCard(),
				FShintStyle::Colors::BorderSubtle(), FShintStyle::Radius::Card))
			.Padding(FMargin(16.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(Caption).Font(F_Label())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(OutValue, STextBlock).Text(FText::FromString(TEXT("—")))
					.Font(FShintStyle::Fonts::H1())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Sub)
					.Font(F_Label()).ColorAndOpacity(FSlateColor(SubColor))
				]
			];
	};
	const float Gap = FShintStyle::Space::S2 * 0.5f;

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				BuildSectionTitle(
					LOCTEXT("CVTitle","Code Validator"),
					LOCTEXT("CVSub","Analyse C++ source and Blueprints · review issues · apply fixes · send to dashboard"))
			]

			// KPI tile row — FILES · ERRORS · WARNINGS · QUALITY, same tile
			// anatomy as the Asset Optimizer.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, Gap, 0.f))
				[ Tile(LOCTEXT("CVF","FILES"), CodeFiles_Label,
					LOCTEXT("CVFSub","Scanned"), FShintStyle::Colors::TextMuted()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
				[ Tile(LOCTEXT("CVE","ERRORS"), CodeErrors_Label,
					LOCTEXT("CVESub","Blocking"), FShintStyle::Colors::SevCritical()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
				[ Tile(LOCTEXT("CVW","WARNINGS"), CodeWarnings_Label,
					LOCTEXT("CVWSub","Review advised"), FShintStyle::Colors::SevHigh()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, 0.f, 0.f))
				[ Tile(LOCTEXT("CVQ","QUALITY"), CodeScore_Label,
					LOCTEXT("CVQSub","Project score"), FShintStyle::Colors::SevLow()) ]
			]
			// The overall QUALITY tile above is the single quality readout; the
			// per-category breakdown strip (perf / sec / bp / maint / naming) was
			// removed as redundant.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[
				BuildModuleProgressBar(CodeProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetCodeProgress)))
			]

			// Scan bar — Asset Optimizer layout: primary scan fills the row,
			// secondary scan sits at the right.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
				[
					// LOD Auditor button language: plain label, no icon.
					SNew(SButton).ContentPadding(FMargin(14.f, 9.f))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SShintToolsPanel::OnScanProjectClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanSrc", "Scan All C++ Source")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).ContentPadding(FMargin(14.f, 9.f))
					.ButtonColorAndOpacity(FSlateColor(C_Surface()))
					.OnClicked(this, &SShintToolsPanel::OnScanBlueprintsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanBP", "Scan All BP")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
			]

			// Results panel
			+ SVerticalBox::Slot().AutoHeight()
			[ BuildCodeResultsPanel() ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter dropdowns
//
// One factory per dropdown menu — `OnGetMenuContent` on the SComboButton
// invokes them to build the menu lazily so we don't allocate widgets we
// never show.
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCategoryMenuContent()
{
	struct FCatEntry { FText Label; EIssueCategoryFilter Value; };
	const TArray<FCatEntry> Entries = {
		{ LOCTEXT("CatAll",  "All Categories"),   EIssueCategoryFilter::All            },
		{ LOCTEXT("CatPerf", "Performance"),      EIssueCategoryFilter::Performance    },
		{ LOCTEXT("CatBest", "Best Practices"),   EIssueCategoryFilter::BestPractices  },
		{ LOCTEXT("CatSec",  "Security"),         EIssueCategoryFilter::Security       },
		{ LOCTEXT("CatMain", "Maintainability"),  EIssueCategoryFilter::Maintainability},
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FCatEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentCategoryFilter = Value;
				if (CategoryFilterLabel.IsValid())
					CategoryFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(E.Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSeverityMenuContent()
{
	struct FSevEntry { FText Label; EIssueSeverityFilter Value; };
	const TArray<FSevEntry> Entries = {
		{ LOCTEXT("SevAll",  "All Severities"), EIssueSeverityFilter::All      },
		{ LOCTEXT("SevCrit", "Critical"),       EIssueSeverityFilter::Critical  },
		{ LOCTEXT("SevErr",  "Error"),          EIssueSeverityFilter::Error     },
		{ LOCTEXT("SevWarn", "Warning"),        EIssueSeverityFilter::Warning   },
		{ LOCTEXT("SevInfo", "Info"),           EIssueSeverityFilter::Info      },
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FSevEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentSeverityFilter = Value;
				if (SeverityFilterLabel.IsValid())
					SeverityFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(E.Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
		];
	}
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_Surface()))
		.Padding(2.f)
		[ Menu ];
}

// BuildCodeTypeMenuContent was retired with the LOD-design toolbar: the
// C++/Blueprints choice is now the tab strip in BuildCodeFilterBar, matching
// the Asset Optimizer's Textures/Meshes/Materials tabs.

// ─────────────────────────────────────────────────────────────────────────────
// Filter bar — tabs + search + dropdowns + Fixable toggle + select/deselect.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildCodeFilterBar()
{
	// LOD-design tab strip — replaces the old "All Types" dropdown. Active
	// tab = Surface background + white text, exactly like the Asset
	// Optimizer's Textures/Meshes/Materials tabs.
	auto TabBtn = [this](const FText& Label, ECodeTypeFilter Tab) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ContentPadding(FMargin(14.f, 7.f))
			.ButtonColorAndOpacity_Lambda([this, Tab]() {
				return FSlateColor(CurrentCodeTypeFilter == Tab ? C_Surface() : C_BG());
			})
			.OnClicked_Lambda([this, Tab]() {
				CurrentCodeTypeFilter = Tab;
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(F_Small())
				.ColorAndOpacity_Lambda([this, Tab]() {
					return FSlateColor(CurrentCodeTypeFilter == Tab ? C_White() : C_Gray());
				})
			];
	};

	TSharedRef<SWidget> CategoryCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildCategoryMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(CategoryFilterLabel, STextBlock)
				.Text(LOCTEXT("CatAll", "All Categories"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
			// SComboButton already renders Unreal's native dropdown arrow icon;
			// the manual unicode ▾ glyph rendered as a missing-glyph box on
			// Bahnschrift's variable axis. Removed in favor of the engine icon.
		];

	TSharedRef<SWidget> SeverityCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildSeverityMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(SeverityFilterLabel, STextBlock)
				.Text(LOCTEXT("SevAll", "All Severities"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
		];

	// Toggle filter — LOD Auditor button language; the label brightens while
	// the filter is active so the toggle state stays readable without an icon.
	TSharedRef<SWidget> FixableBtn =
		SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnClicked_Lambda([this]() -> FReply
		{
			CurrentFilter = (CurrentFilter == EIssueFilter::FixableOnly)
				? EIssueFilter::All : EIssueFilter::FixableOnly;
			ApplyCodeFilter();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(LOCTEXT("FFix", "Fixable Only")).Font(F_Small())
			.ColorAndOpacity_Lambda([this]() {
				return FSlateColor(CurrentFilter == EIssueFilter::FixableOnly
					? C_White() : C_Gray());
			})
		];

	return SNew(SVerticalBox)
		// Tabs — All / C++ / Blueprints (Asset Optimizer design).
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[ TabBtn(LOCTEXT("CodeTabAll", "All"),        ECodeTypeFilter::All) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[ TabBtn(LOCTEXT("CodeTabCpp", "C++"),        ECodeTypeFilter::CppOnly) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ TabBtn(LOCTEXT("CodeTabBP",  "Blueprints"), ECodeTypeFilter::BlueprintsOnly) ]
		]
		// Filter row — search (fills) + combos + bulk selection.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("CodeSearch", "Search"))
				.OnTextChanged_Lambda([this](const FText& T) {
					CodeSearchText = T.ToString();
					ApplyCodeFilter();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f) [ CategoryCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f) [ SeverityCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f) [ FixableBtn    ]
			// Secondary actions — LOD Auditor button language (flat Surface,
			// plain label, no icon), matching the Asset Optimizer's Export.
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllCodeClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("SelAll", "Select All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllCodeClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("DeselAll", "Deselect All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			// Send to Dashboard — same toolbar position as the Asset Optimizer's
			// (Select All / Deselect All / Export / Send / Fix). Paid-tier only:
			// POSTs the scan to shint.tools, part of the paid SaaS offering.
			// Hidden completely on free so the user never sees an affordance that
			// always 403s. The cached tier comes from the launcher's startup
			// /license/status probe; it defaults to "free" until that resolves,
			// which is intentional — a paid user simply sees the button appear
			// after the probe lands.
			// [DASH-STRIP-BEGIN]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				SAssignNew(SendCodeBtn, SButton)
				.Visibility_Lambda([]() -> EVisibility {
					return FShintToolsModule::GetCachedTier()
							.Equals(TEXT("free"), ESearchCase::IgnoreCase)
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				.IsEnabled(false).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnSendCodeToDashboardClicked)
				[
					SAssignNew(SendCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("SendCode", "Send to Dashboard")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]
			// [DASH-STRIP-END]
			// Primary action — default button + white label, matching the
			// Asset Optimizer's bulk Fix.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SAssignNew(ApplyCodeBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(12.f, 6.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedCodeFixesClicked)
				[
					SAssignNew(ApplyCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyCode", "Fix all (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Results panel — free-tier banner + filter bar + list + action row.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildCodeResultsPanel()
{
	SAssignNew(CodeEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SAssignNew(CodeEmptyText, STextBlock)
		.Text(LOCTEXT("CVEmpty", "Run a scan to see results here."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)

		// Free-tier cap banner — visible when the server reports the scan hit
		// a tier limit (e.g. 40 of 96 rules). Server is the source of truth via
		// summary.limit_applied.
		//
		// Cross-check with the launcher's startup probe (GetCachedTier): the
		// server occasionally resolves a freshly-activated key to "free" before
		// the local Mongo licenses row is seeded, which previously surfaced the
		// banner to a paid customer on every rescan until they restarted the
		// launcher. Trust the cached tier when it disagrees.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() {
				if (!LastCodeResult.bLimitApplied)
					return EVisibility::Collapsed;
				const FString Tier = FShintToolsModule::GetCachedTier().ToLower();
				if (!Tier.IsEmpty() && Tier != TEXT("free"))
					return EVisibility::Collapsed;
				return EVisibility::Visible;
			})
			.BorderImage(ST4::Outline(C_Surface(), C_Border()))
			.Padding(FMargin(12.f, 8.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("⚠")))
					.Font(F_Label())
					.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.65f, 0.20f)))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
					.Text_Lambda([this]() {
						return FText::FromString(FString::Printf(
							TEXT("Free tier: %d of %d %s applied. Upgrade to Indie for full coverage."),
							LastCodeResult.LimitValue,
							LastCodeResult.TotalAvailable,
							*LastCodeResult.LimitKind));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SHyperlink)
					.Text(LOCTEXT("CVUpgrade", "Upgrade"))
					.OnNavigate_Lambda([]() {
						FPlatformProcess::LaunchURL(
							TEXT("https://shint.tools/pricing"), nullptr, nullptr);
					})
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f) [ BuildCodeFilterBar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f) [ Divider() ]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(900.f)
			[
				SAssignNew(CodeIssueListView, SListView<FShintIssueItemPtr>)
				.ListItemsSource(&CodeIssueItems)
				.OnGenerateRow(this, &SShintToolsPanel::GenerateCodeIssueRow)
				.SelectionMode(ESelectionMode::None)
				.AllowOverscroll(EAllowOverscroll::Yes)
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ CodeEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Issue row factory
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<ITableRow> SShintToolsPanel::GenerateCodeIssueRow(
	FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const bool bError = (Item->Severity == TEXT("error"));
	const FLinearColor SevColor  = bError ? C_Red() : C_Yellow();
	const FLinearColor RowBG     = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();
	const FString      AutoBadge = Item->bIsAutoFixable ? TEXT("  AUTO") : TEXT("");

	// Location string: for blueprints show "ClassName > GraphName", for C++ show "File:Line".
	const bool bIsBlueprintIssue = !Item->Graph.IsEmpty();
	FString LocationStr;
	if (bIsBlueprintIssue)
	{
		LocationStr = Item->Class.IsEmpty()
			? FString::Printf(TEXT("%s > %s"), *Item->FileName, *Item->Graph)
			: FString::Printf(TEXT("%s > %s"), *Item->Class, *Item->Graph);
	}
	else if (!Item->Class.IsEmpty())
	{
		LocationStr = FString::Printf(TEXT("%s :: %s : %d"), *Item->Class, *Item->FileName, Item->Line);
	}
	else
	{
		LocationStr = FString::Printf(TEXT("%s : %d"), *Item->FileName, Item->Line);
	}

	const bool bHasContext = !Item->ContextBefore.IsEmpty() || !Item->FileContent.IsEmpty();
	const bool bIsFixable  = Item->bIsAutoFixable
		&& (!Item->FixSuggestion.IsEmpty() || !Item->FileContent.IsEmpty() || Item->bIsBlueprint);

	// Context diff panels. Priority: FixPreviewCode (fetched on-demand from
	// /validate/fix) > ContextAfter (pre-computed by the server fixer during
	// the scan — already actual fixed code, safe to display for all issue types).
	const FString& AfterText = !Item->FixPreviewCode.IsEmpty()
		? Item->FixPreviewCode
		: Item->ContextAfter;
	const bool bAfterAvailable = !AfterText.IsEmpty();
	const bool bAfterLoading   = Item->bFixPreviewLoading;

	TSharedRef<SWidget> ContextDiff = SNullWidget::NullWidget;
	if (bHasContext)
	{
		TSharedRef<SWidget> AntesPanelWidget =
			BuildContextPanel(TEXT("BEFORE"), Item->ContextBefore,
				Item->ContextLineStart, Item->Line, C_DiffRed());

		TSharedRef<SWidget> DespuesPanelWidget = SNullWidget::NullWidget;
		if (bAfterLoading)
		{
			DespuesPanelWidget = SNew(STextBlock)
				.Text(FText::FromString(TEXT("Fetching preview...")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()));
		}
		else if (bAfterAvailable)
		{
			DespuesPanelWidget = BuildContextPanel(TEXT("AFTER"), AfterText,
				Item->ContextLineStart, Item->Line, C_DiffGreen());
		}

		ContextDiff =
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 4.f, 0.f)
			[ AntesPanelWidget ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(4.f, 0.f, 0.f, 0.f).HAlign(HAlign_Fill)
			[
				SNew(SBox)
				.Visibility((bAfterAvailable || bAfterLoading) ? EVisibility::Visible : EVisibility::Collapsed)
				[ DespuesPanelWidget ]
			];
	}

	// Compact diff fallback (blueprints / issues without context window).
	TSharedRef<SWidget> CompactDiff = SNew(SBorder)
		.Visibility((Item->ContextBefore.IsEmpty() && !Item->Snippet.IsEmpty())
			? EVisibility::Visible : EVisibility::Collapsed)
		.BorderImage(ST4::Solid(C_CodeBG()))
		.Padding(FMargin(8.f, 5.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).Visibility(Item->Snippet.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[ BuildDiffLine(TEXT(">"), Item->Snippet, C_Red(), C_DiffRed()) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility(Item->FixSuggestion.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[ BuildDiffLine(TEXT("→"), Item->FixSuggestion, C_Green(), C_DiffGreen()) ]
			]
		];

	return SNew(STableRow<FShintIssueItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)

				// Checkbox
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, 10.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
						Item->bChecked = (S == ECheckBoxState::Checked);
						RefreshApplyCodeLabel();
					})
				]

				// Content column
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					// Row 1: severity badge + rule name + location + preview + AUTO badge.
					// SevColor stays in scope so older call-sites still compile until they migrate.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(SShintSeverityBadge).Severity(Item->Severity)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
						[
							// LLM pivot — show the human label (RuleName from
							// server-side enrich_issue) instead of the rule_id;
							// fall back to the id when an older core didn't enrich.
							SNew(STextBlock)
							.Text(FText::FromString(
								Item->RuleName.IsEmpty() ? Item->RuleId : Item->RuleName))
							.Font(FShintStyle::Fonts::Small())
							.ColorAndOpacity(FSlateColor(C_White()))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(LocationStr))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
						// Preview toggle — only for issues with context
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 6.f, 0.f)
						[
							SNew(SBox).Visibility(bHasContext ? EVisibility::Visible : EVisibility::Collapsed)
							[
								SNew(SButton).ContentPadding(FMargin(6.f, 2.f))
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.OnClicked_Lambda([this, Item]() -> FReply {
									Item->bPreviewExpanded = !Item->bPreviewExpanded;
									// Fetch on-demand only when ContextAfter is also empty
									// (server could not run the fixer at scan time).
									if (Item->bPreviewExpanded
										&& !Item->FileContent.IsEmpty()
										&& Item->ContextAfter.IsEmpty()
										&& Item->FixPreviewCode.IsEmpty()
										&& !Item->bFixPreviewLoading)
									{
										FetchFixPreview(Item);
									}
									if (CodeIssueListView.IsValid())
										CodeIssueListView->RequestListRefresh();
									return FReply::Handled();
								})
								[
									SNew(STextBlock)
									.Text_Lambda([Item]() {
										return FText::FromString(Item->bPreviewExpanded
											? TEXT("▼ Preview") : TEXT("▶ Preview"));
									})
									.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Blue()))
								]
							]
						]
						// Per-issue "Explain" — hands the question to the AI
						// Assistant tab. Shown on every tier: the old modal drove
						// /agent/explain (Indie and up) and so was hidden on Free,
						// but the assistant answers explain_finding on every plan.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(SButton)
							.ContentPadding(FMargin(6.f, 2.f))
							.ButtonColorAndOpacity(FSlateColor(C_Surface()))
							.ToolTipText(LOCTEXT("ExplainTip",
								"Ask the AI Assistant to explain this issue in plain language."))
							.OnClicked_Lambda([this, Item]() -> FReply
							{
								return OnExplainIssueClicked(Item);
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ExplainBtn", "✎  Explain"))
								.Font(F_Label())
								.ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.75f, 0.95f)))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(AutoBadge)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_Blue()))
						]
					]

					// Row 2: message
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Message)).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White())).AutoWrapText(true)
					]

					// Row 3: compact diff (no context) OR expanded context diff
					+ SVerticalBox::Slot().AutoHeight() [ CompactDiff ]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, bHasContext ? 6.f : 0.f, 0.f, 0.f)
					[
						SNew(SBox)
						.Visibility_Lambda([Item]() {
							return (Item->bPreviewExpanded && !Item->ContextBefore.IsEmpty())
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[ ContextDiff ]
					]

					// Row 4: Apply / Ignore — always visible for auto-fixable issues
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(SBox).Visibility(bIsFixable ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnApplySingleFix, Item)
								[
									ShintBtnContent(TEXT("ShintTools.Icons.Tick"),
									SNew(STextBlock).Text(LOCTEXT("ApplySingle", "Apply"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green())),
									FSlateColor(C_Green()))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnIgnoreSingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("IgnoreSingle", "✗  Ignore"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
								]
							]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Light handlers
//
// All do roughly one of: bump scan state, send an HTTP request via the
// shared CoreClient / DashboardSync, or mutate the local backing store.
// The heavy fix flows are in _Fixes.cpp.
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnCheckConnectionClicked()
{
	SetStatus(ECoreStatus::Checking);
	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanProjectClicked()
{
	// Bug #35: the previous "all-clear guard" early-returned without hitting
	// the server, so right after applying every auto-fix the user's first
	// click on Scan only got the "click Scan again" hint — they read it as
	// the server not responding. We now always do the real scan; if the
	// server confirms 0 issues, PopulateCodeIssueList renders the empty
	// state with the all-resolved message naturally.
	AppliedFixFingerprints.Empty();

	++ScanGeneration;
	bBlueprintScanActive = false;   // full source scan — no BP-only filter

	// T1 — Auto-switch the visible filter to "All" so the user sees both
	// kinds at once (the previous flow forced CppOnly/BlueprintsOnly on every
	// click, which silently hid the other half of the merged list). The tab
	// strip reads CurrentCodeTypeFilter reactively — no label to update.
	CurrentCodeTypeFilter = ECodeTypeFilter::All;

	SetCodeState(EModuleState::Running);
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

	// T1 — Do NOT wipe LastCodeResult or AllCodeItems here. HandleValidateResult
	// merges the new C++ findings against the existing Blueprint findings (and
	// vice-versa) by RuleId prefix; clearing them on click defeats the merge
	// and the BP results disappear the moment a C++ scan starts. We only reset
	// the visible (filtered) list so the panel doesn't paint stale rows during
	// the scan.
	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

	CoreClient->ValidateProject(FPaths::GameSourceDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnProjectValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBlueprintsClicked()
{
	++ScanGeneration;
	bBlueprintScanActive = true;

	// T1 — see OnScanProjectClicked. Auto-switch to "All" instead of forcing
	// BlueprintsOnly, and preserve previous-scan state so the merge survives.
	CurrentCodeTypeFilter = ECodeTypeFilter::All;

	SetCodeState(EModuleState::Running);
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintValidateComplete));

	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems)
		I->bChecked = true;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnDeselectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems) I->bChecked = false;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

// [DASH-STRIP-BEGIN]
FReply SShintToolsPanel::OnSendCodeToDashboardClicked()
{
	DashboardSync->SendCodeValidator(LastCodeResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnCodeDashboardComplete));
	return FReply::Handled();
}
// [DASH-STRIP-END]

FReply SShintToolsPanel::OnIgnoreSingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	// Fingerprint so this issue is skipped on the next incremental scan.
	const FString Fingerprint = FString::Printf(
		TEXT("%s:%d:%s"), *Item->FilePath, Item->Line, *Item->RuleId);
	AppliedFixFingerprints.Add(Fingerprint);

	// Remove from backing store and rebuild visible list.
	AllCodeItems.RemoveAll([&Item](const FShintIssueItemPtr& P){ return P == Item; });
	ApplyCodeFilter();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Explain — hand the finding to the AI Assistant
//
// Replaces the single-shot modal (SShintToolsPanel_Explain.cpp, removed): it
// opened a throwaway window, answered once, and was destroyed on the next
// click, so a follow-up had nowhere to go. The question now becomes a turn in
// a durable thread the user can keep asking into.
//
// The finding is identified by rule_id + asset/file path, not re-sent: the
// assistant resolves it server-side from the analysis the scan published.
// ─────────────────────────────────────────────────────────────────────────────
FReply SShintToolsPanel::OnExplainIssueClicked(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	const FString Label = Item->RuleName.IsEmpty() ? Item->RuleId : Item->RuleName;
	const FString Question = FString::Printf(
		TEXT("Why is \"%s\" flagged in %s?"),
		*Label, *FPaths::GetCleanFilename(Item->FilePath));

	// Queue first, then invoke: the panel consumes the pending request when it
	// hears OnChanged, and that fires whether the tab was already open or this
	// click is what created it.
	FShintAssistantContext::RequestExplain(Item->RuleId, Item->FilePath, Question);
	FGlobalTabmanager::Get()->TryInvokeTab(FShintToolsModule::ShintAssistantTabName);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
