// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "Core/ShintAssistantContext.h"

#include "Framework/Docking/TabManager.h"
#include "Misc/Paths.h"

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

TSharedRef<SWidget> SShintToolsPanel::BuildCodeValidatorSection()
{

	auto Tile = [](const FText& Caption, TSharedPtr<STextBlock>& OutValue,
		const FText& Sub, const FLinearColor& SubColor) -> TSharedRef<SWidget>
	{

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

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[
				BuildModuleProgressBar(CodeProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetCodeProgress)))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
				[

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

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildCodeResultsPanel() ]
		];
}

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

TSharedRef<SWidget> SShintToolsPanel::BuildCodeFilterBar()
{

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

TSharedRef<ITableRow> SShintToolsPanel::GenerateCodeIssueRow(
	FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const bool bError = (Item->Severity == TEXT("error"));
	const FLinearColor SevColor  = bError ? C_Red() : C_Yellow();
	const FLinearColor RowBG     = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();
	const FString      AutoBadge = Item->bIsAutoFixable ? TEXT("  AUTO") : TEXT("");

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

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, 10.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(Item->bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState S) {
						Item->bChecked = (S == ECheckBoxState::Checked);
						RefreshApplyCodeLabel();
					})
				]

				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(SShintSeverityBadge).Severity(Item->Severity)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
						[

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

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 6.f, 0.f)
						[
							SNew(SBox).Visibility(bHasContext ? EVisibility::Visible : EVisibility::Collapsed)
							[

								SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.OnClicked_Lambda([this, Item]() -> FReply {
									Item->bPreviewExpanded = !Item->bPreviewExpanded;

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
										return Item->bPreviewExpanded
											? LOCTEXT("HidePreviewBtn", "Hide Preview")
											: LOCTEXT("PreviewBtn", "Preview");
									})
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Gray()))
								]
							]
						]

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(SButton)
							.ContentPadding(FMargin(12.f, 6.f))
							.ButtonColorAndOpacity(FSlateColor(C_Surface()))
							.ToolTipText(LOCTEXT("ExplainTip",
								"Ask the AI Assistant to explain this issue in plain language."))
							.OnClicked_Lambda([this, Item]() -> FReply
							{
								return OnExplainIssueClicked(Item);
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("ExplainBtn", "Explain"))
								.Font(F_Small())

								.ColorAndOpacity(FSlateColor(C_Gray()))
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(AutoBadge)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_Blue()))
						]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Message)).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White())).AutoWrapText(true)
					]

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

					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(SBox).Visibility(bIsFixable ? EVisibility::Visible : EVisibility::Collapsed)
						[

							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.OnClicked(this, &SShintToolsPanel::OnApplySingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("ApplySingle", "Apply"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
								.ButtonColorAndOpacity(FSlateColor(C_Surface()))
								.OnClicked(this, &SShintToolsPanel::OnIgnoreSingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("IgnoreSingle", "Ignore"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Gray()))
								]
							]
						]
					]
				]
			]
		];
}

FReply SShintToolsPanel::OnCheckConnectionClicked()
{
	SetStatus(ECoreStatus::Checking);
	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanProjectClicked()
{

	AppliedFixFingerprints.Empty();

	++ScanGeneration;
	bBlueprintScanActive = false;

	CurrentCodeTypeFilter = ECodeTypeFilter::All;

	SetCodeState(EModuleState::Running);
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

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

FReply SShintToolsPanel::OnIgnoreSingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	const FString Fingerprint = FString::Printf(
		TEXT("%s:%d:%s"), *Item->FilePath, Item->Line, *Item->RuleId);
	AppliedFixFingerprints.Add(Fingerprint);

	AllCodeItems.RemoveAll([&Item](const FShintIssueItemPtr& P){ return P == Item; });
	ApplyCodeFilter();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnExplainIssueClicked(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	const FString Label = Item->RuleName.IsEmpty() ? Item->RuleId : Item->RuleName;
	const FString Question = FString::Printf(
		TEXT("Why is \"%s\" flagged in %s?"),
		*Label, *FPaths::GetCleanFilename(Item->FilePath));

	FShintAssistantContext::RequestExplain(Item->RuleId, Item->FilePath, Question);
	FGlobalTabmanager::Get()->TryInvokeTab(FShintToolsModule::ShintAssistantTabName);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
