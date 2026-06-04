// Copyright ShintTools. All Rights Reserved.
//
// Asset Naming Bot destination — every widget builder under the Assets
// section (KPI tile row, scan button, results panel, type filter combo,
// per-asset row factory) plus the lightweight handlers
// (OnScanAssets / select-all helpers / dashboard push).
//
// The heavy rename + redirect-write flow lives in _Fixes.cpp because it
// pulls in the AssetTools / AssetRegistry / Kismet / FileHelpers stack.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"
#include "ShintDashboardSync.h"

#include "ShintStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
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
TSharedRef<SWidget> SShintToolsPanel::BuildAssetNamingSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("ANBTitle", "ASSET NAMING BOT"),
					LOCTEXT("ANBSub", "Scan entire project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard"))
			]

			// Stats — 3-up KPI grid (asset count / invalid / scan time).
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(AssetTotal_Label,   LOCTEXT("ANBT", "ASSETS"),    FShintStyle::Colors::TextPrimary()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f))
				[ StatBadge(AssetInvalid_Label, LOCTEXT("ANBI", "INVALID"),   FShintStyle::Colors::SevCritical()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f))
				[ StatBadge(AssetTime_Label,    LOCTEXT("ANBMS", "TIME (s)"), FShintStyle::Colors::TextMuted()) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[
				BuildModuleProgressBar(AssetProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetAssetProgress)))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 6.f))
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f, 7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanAssetsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanAssets", ">  Scan All Assets")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildAssetResultsPanel() ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Type filter dropdown
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildAssetTypeMenuContent()
{
	struct FTypeEntry { FText Label; EAssetTypeFilter Value; };
	const TArray<FTypeEntry> Entries = {
		{ LOCTEXT("ATAll",   "All Types"),   EAssetTypeFilter::All        },
		{ LOCTEXT("ATMat",   "Materials"),   EAssetTypeFilter::Materials  },
		{ LOCTEXT("ATTex",   "Textures"),    EAssetTypeFilter::Textures   },
		{ LOCTEXT("ATMesh",  "Meshes"),      EAssetTypeFilter::Meshes     },
		{ LOCTEXT("ATBP",    "Blueprints"),  EAssetTypeFilter::Blueprints },
		{ LOCTEXT("ATVFX",   "VFX"),         EAssetTypeFilter::VFX        },
		{ LOCTEXT("ATAudio", "Audio"),       EAssetTypeFilter::Audio      },
		{ LOCTEXT("ATAnim",  "Animations"),  EAssetTypeFilter::Animations },
		{ LOCTEXT("ATData",  "Data"),        EAssetTypeFilter::Data       },
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FTypeEntry& E : Entries)
	{
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Value = E.Value, Label = E.Label]() -> FReply
			{
				CurrentAssetTypeFilter = Value;
				if (AssetTypeFilterLabel.IsValid())
					AssetTypeFilterLabel->SetText(Label);
				ApplyAssetFilter();
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

// ─────────────────────────────────────────────────────────────────────────────
// Results panel — free-tier banner + filter row + list + action row.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> SShintToolsPanel::BuildAssetResultsPanel()
{
	SAssignNew(AssetEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SAssignNew(AssetEmptyText, STextBlock)
		.Text(LOCTEXT("ANBEmpty", "Run a scan to see naming violations."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> AssetTypeCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildAssetTypeMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(AssetTypeFilterLabel, STextBlock)
				.Text(LOCTEXT("ATAll", "All Types"))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]
		];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)

		// Free-tier cap banner — visible when summary.limit_applied=true on
		// /assets/scan (Free tier list is always capped at 500 issues; see
		// PopulateAssetIssueList for the row-level cap).
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() {
				if (!LastAssetResult.bLimitApplied)
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
						// Numerator: rows actually loaded into the backing store
						// after the free-tier issue cap. Denominator: total issues
						// the server reported pre-cap. They match when the project
						// produces ≤ cap issues.
						const int32 Shown = AllAssetItems.Num();
						return FText::FromString(FString::Printf(
							TEXT("Free tier: %d of %d issues. Upgrade to Indie for full coverage."),
							Shown,
							LastAssetResult.InvalidAssets));
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SHyperlink)
					.Text(LOCTEXT("ANBUpgrade", "Upgrade"))
					.OnNavigate_Lambda([]() {
						FPlatformProcess::LaunchURL(
							TEXT("https://shint.tools/pricing"), nullptr, nullptr);
					})
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ANBRes", "RESULTS")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f) [ AssetTypeCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(10.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllAssetsClicked)
				[ SNew(STextBlock).Text(LOCTEXT("ANBSel", "Select All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Blue())) ]
			]
			// T6 — Deselect All companion button. Lives next to Select All so
			// users have symmetric controls for the asset rename batch.
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(10.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllAssetsClicked)
				[ SNew(STextBlock).Text(LOCTEXT("ANBDes", "Deselect All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_DimGray())) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f) [ Divider() ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(900.f)
			[
				SAssignNew(AssetIssueListView, SListView<FShintAssetItemPtr>)
				.ListItemsSource(&AssetIssueItems)
				.OnGenerateRow(this, &SShintToolsPanel::GenerateAssetIssueRow)
				.SelectionMode(ESelectionMode::None)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f) [ Divider() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 6.f))
			+ SWrapBox::Slot()
			[
				SAssignNew(ApplyAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedAssetFixesClicked)
				[
					SAssignNew(ApplyAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyAsset", "✓  Apply Corrections (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				// Same tier gate as SendCodeBtn — shint.tools dashboard ingest
				// is paid-only.
				SAssignNew(SendAssetBtn, SButton)
				.Visibility_Lambda([]() -> EVisibility {
					return FShintToolsModule::GetCachedTier()
							.Equals(TEXT("free"), ESearchCase::IgnoreCase)
						? EVisibility::Collapsed
						: EVisibility::Visible;
				})
				.IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnSendAssetToDashboardClicked)
				[
					SAssignNew(SendAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("SendAsset", "↑  Send to Dashboard"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ AssetEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-asset row
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<ITableRow> SShintToolsPanel::GenerateAssetIssueRow(
	FShintAssetItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor RowBG = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();

	return SNew(STableRow<FShintAssetItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
						Item->bChecked = (S == ECheckBoxState::Checked);
						RefreshApplyAssetLabel();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					// Type + path row
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("⚠"))).Font(F_Small())
							.ColorAndOpacity(FSlateColor(C_Yellow()))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetType))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FSlateColor(C_White()))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetPath))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(ST4::Solid(C_CodeBG()))
						.Padding(FMargin(8.f, 4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Red())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->CurrentName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DiffRed())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Green())) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->SuggestedName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DiffGreen())) ]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Light handlers
// ─────────────────────────────────────────────────────────────────────────────
FReply SShintToolsPanel::OnScanAssetsClicked()
{
	// All-clear guard — if the user just applied every fix and the list is
	// empty, the first click should show the "click again" hint instead of
	// silently triggering a fresh full scan.
	if (AllAssetItems.IsEmpty() && AssetFixesApplied > 0)
	{
		AssetFixesApplied = 0;
		if (AssetEmptyText.IsValid())
			AssetEmptyText->SetText(LOCTEXT("ANBAllFixed",
				"✓  All violations resolved — click 'Scan' again to do a full re-scan."));
		if (AssetEmptyState.IsValid())
			AssetEmptyState->SetVisibility(EVisibility::Visible);
		return FReply::Handled();
	}

	SetAssetState(EModuleState::Running);
	// Reset to default empty text before new results arrive.
	if (AssetEmptyText.IsValid())
		AssetEmptyText->SetText(LOCTEXT("ANBEmpty", "Run a scan to see naming violations."));
	AllAssetItems.Empty();
	AssetIssueItems.Empty();
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	CoreClient->ScanAssetNaming(FPaths::ProjectContentDir(),
		FOnShintAssetScanComplete::CreateSP(this, &SShintToolsPanel::OnAssetScanComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllAssetsClicked()
{
	// Toggle the FULL backing store, not just the filtered view, so a partial
	// type filter doesn't leave items off-screen unchanged.
	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = true;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = true;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

// T6 — Deselect All for the asset naming bot. Operates on AllAssetItems first
// so any items hidden by the active type filter are also unticked.
FReply SShintToolsPanel::OnDeselectAllAssetsClicked()
{
	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = false;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = false;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendAssetToDashboardClicked()
{
	DashboardSync->SendAssetNaming(LastAssetResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnAssetDashboardComplete));
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
