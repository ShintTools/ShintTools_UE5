// Copyright ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

// Slate layout
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

// Slate widgets
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
// Style
#include "Styling/AppStyle.h"
// Asset tools (for IAssetTools::RenameAssets)
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Count.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Brush cache
// ─────────────────────────────────────────────────────────────────────────────

namespace ST4
{
	static TMap<FString, FSlateBrush> BC;

	static const FSlateBrush* Solid(const FLinearColor& C, float R = 0.f)
	{
		const FString K = FString::Printf(TEXT("S%.3f%.3f%.3f%.1f"), C.R, C.G, C.B, R);
		if (!BC.Contains(K))
		{
			FSlateBrush B;
			B.TintColor = FSlateColor(C);
			B.DrawAs    = R > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
			if (R > 0.f) {
				B.OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
				B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			BC.Add(K, B);
		}
		return &BC[K];
	}

	static const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R = 4.f)
	{
		const FString K = FString::Printf(TEXT("O%.3f%.3f%.1f"), Fill.R, Brd.R, R);
		if (!BC.Contains(K))
		{
			FSlateBrush B;
			B.TintColor = FSlateColor(Fill);
			B.DrawAs    = ESlateBrushDrawType::RoundedBox;
			B.OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
			B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			B.OutlineSettings.Color        = FSlateColor(Brd);
			B.OutlineSettings.Width        = 1.f;
			BC.Add(K, B);
		}
		return &BC[K];
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	CoreClient     = MakeShared<FShintCoreClient>();
	ProcessManager = MakeShared<FCoreProcessManager>();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(0.f)
		[
			SNew(SScrollBox).Orientation(Orient_Vertical)
			+ SScrollBox::Slot().Padding(0.f) [ BuildHeader()               ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildConfigSection()        ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildStatusBar()            ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildCodeValidatorSection() ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildAssetNamingSection()   ]
		]
	];
}

SShintToolsPanel::~SShintToolsPanel()
{
}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::Divider()
{
	return SNew(SBox).HeightOverride(1.f)
		[ SNew(SBorder).BorderImage(ST4::Solid(C_Border())).Padding(0.f) ];
}

TSharedRef<SWidget> SShintToolsPanel::BuildSectionTitle(const FText& Title, const FText& Subtitle)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
		[
			SNew(STextBlock).Text(Title).Font(F_H2())
			.ColorAndOpacity(FSlateColor(C_White()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
		[
			SNew(STextBlock).Text(Subtitle).Font(F_Label())
			.ColorAndOpacity(FSlateColor(C_Gray()))
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildDiffLine(
	const FString& Icon, const FString& Text,
	const FLinearColor& IconColor, const FLinearColor& TextColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(STextBlock).Text(FText::FromString(Icon)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(IconColor))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(STextBlock).Text(FText::FromString(Text)).Font(F_Mono())
			.ColorAndOpacity(FSlateColor(TextColor)).AutoWrapText(true)
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildModuleProgressBar(
	TSharedPtr<SProgressBar>& OutBar,
	TAttribute<TOptional<float>> PercentAttr)
{
	return SNew(SBox).HeightOverride(2.f)
		[
			SAssignNew(OutBar, SProgressBar)
			.Percent(PercentAttr)
			.FillColorAndOpacity(FSlateColor(C_Blue()))
			.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
		];
}

FString SShintToolsPanel::FmtN(int32 N)
{
	return N < 0 ? TEXT("\u2014") : FString::Printf(TEXT("%d"), N);
}

// ─────────────────────────────────────────────────────────────────────────────
// Header
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildHeader()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f, 20.f, 14.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(LOCTEXT("Brand","ShintTools"))
				.Font(F_Title()).ColorAndOpacity(FSlateColor(C_White()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Sub","Automation and optimization tools for Unreal Engine and Unity"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray()))
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Config section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildConfigSection()
{
	const FShintCoreConfig& Cfg = CoreClient->GetConfig();

	auto ConfigRow = [this](const FText& Label, TSharedPtr<SEditableTextBox>& OutField,
		const FString& InitialValue, const FText& Hint) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,10.f,0.f)
			[
				SNew(SBox).WidthOverride(110.f)
				[
					SNew(STextBlock).Text(Label).Font(F_Label())
					.ColorAndOpacity(FSlateColor(C_DimGray()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(OutField, SEditableTextBox)
				.Text(FText::FromString(InitialValue))
				.HintText(Hint)
				.Font(F_Mono())
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { SaveConfigOverrides(); })
			];
	};

	return SNew(SBorder)
		.BorderImage(ST4::Outline(C_Surface(), C_Border()))
		.Padding(FMargin(20.f, 12.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CfgTitle","PROJECT CONFIG"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
			[
				ConfigRow(LOCTEXT("CfgProjId","Project ID"), ProjectIdField,
					Cfg.ProjectId, LOCTEXT("CfgProjIdHint","proj_..."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
			[
				ConfigRow(LOCTEXT("CfgApiKey","API Key"), ApiKeyField,
					Cfg.ApiKey, LOCTEXT("CfgApiKeyHint","shint_..."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ConfigRow(LOCTEXT("CfgDashUrl","Dashboard URL"), DashboardUrlField,
					Cfg.DashboardUrl, LOCTEXT("CfgDashUrlHint","https://shint.tools"))
			]
		];
}

void SShintToolsPanel::SaveConfigOverrides()
{
	if (!CoreClient.IsValid()) return;

	FShintCoreConfig& Cfg = CoreClient->GetConfigMutable();

	if (ProjectIdField.IsValid())    Cfg.ProjectId    = ProjectIdField->GetText().ToString();
	if (ApiKeyField.IsValid())       Cfg.ApiKey       = ApiKeyField->GetText().ToString();
	if (DashboardUrlField.IsValid()) Cfg.DashboardUrl = DashboardUrlField->GetText().ToString();

	CoreClient->SaveConfig();
}

// ─────────────────────────────────────────────────────────────────────────────
// Status bar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildStatusBar()
{
	return SNew(SBorder)
		.BorderImage(ST4::Outline(C_Surface(), C_Border()))
		.Padding(FMargin(18.f, 10.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,8.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(F_Body())
				.ColorAndOpacity(TAttribute<FSlateColor>::Create(
					TAttribute<FSlateColor>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusColor)))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,6.f,0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CoreLbl","CORE ENGINE")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]

			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(TAttribute<FText>::Create(
					TAttribute<FText>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusText)))
				.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
			]

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonColorAndOpacity(C_BG())
				.ContentPadding(FMargin(14.f,5.f))
				.OnClicked(this, &SShintToolsPanel::OnCheckConnectionClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("CheckBtn","Check Connection"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Stat badge
// ─────────────────────────────────────────────────────────────────────────────

static TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& OutLabel, const FText& Caption, const FLinearColor& Clr)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SAssignNew(OutLabel, STextBlock)
			.Text(FText::FromString(TEXT("—")))
			.Font(SShintToolsPanel::F_StatNum())
			.ColorAndOpacity(FSlateColor(Clr))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f,3.f,0.f,0.f)
		[
			SNew(STextBlock).Text(Caption).Font(SShintToolsPanel::F_StatCap())
			.ColorAndOpacity(FSlateColor(SShintToolsPanel::C_DimGray()))
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeValidatorSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("CVTitle","CODE VALIDATOR"),
					LOCTEXT("CVSub","Analyse C++ source and Blueprints · review issues · apply fixes · send to dashboard"))
			]

			// Stats
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,16.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(CodeFiles_Label,    LOCTEXT("CVF","FILES"),    C_Blue())   ]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(CodeErrors_Label,   LOCTEXT("CVE","ERRORS"),   C_Red())    ]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(CodeWarnings_Label, LOCTEXT("CVW","WARNINGS"), C_Yellow()) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				BuildModuleProgressBar(CodeProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetCodeProgress)))
			]

			// Scan buttons
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,18.f)
			[
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanProjectClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanSrc","⟳  Scan All Source")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanBlueprintsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanBP","⟳  Scan Blueprints")).Font(F_Small())
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
// Code filter bar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeFilterBar()
{
	auto FilterBtn = [this](const FText& Label, EIssueFilter Filter) -> TSharedRef<SWidget>
	{
		return SNew(SButton).ContentPadding(FMargin(10.f,4.f))
			.OnClicked_Lambda([this, Filter]() -> FReply {
				CurrentFilter = Filter;
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("ResLbl","RESULTS")).Font(F_Label())
			.ColorAndOpacity(FSlateColor(C_DimGray()))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
		[ FilterBtn(LOCTEXT("FAll","All"),         EIssueFilter::All)        ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
		[ FilterBtn(LOCTEXT("FErr","Errors"),      EIssueFilter::ErrorsOnly) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
		[ FilterBtn(LOCTEXT("FWrn","Warnings"),    EIssueFilter::WarningsOnly)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f)
		[ FilterBtn(LOCTEXT("FFix","Fixable"),     EIssueFilter::FixableOnly) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
		[
			SNew(SButton).ContentPadding(FMargin(10.f,4.f))
			.OnClicked(this, &SShintToolsPanel::OnSelectAllCodeClicked)
			[ SNew(STextBlock).Text(LOCTEXT("SelAll","Select All")).Font(F_Label())
			  .ColorAndOpacity(FSlateColor(C_Blue())) ]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).ContentPadding(FMargin(10.f,4.f))
			.OnClicked(this, &SShintToolsPanel::OnDeselectAllCodeClicked)
			[ SNew(STextBlock).Text(LOCTEXT("DeselAll","Deselect All")).Font(F_Label())
			  .ColorAndOpacity(FSlateColor(C_Gray())) ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code results panel
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeResultsPanel()
{
	SAssignNew(CodeEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SNew(STextBlock).Text(LOCTEXT("CVEmpty","Run a scan to see results here."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)

		// Filter + select toolbar
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ BuildCodeFilterBar() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ Divider() ]

		// Virtual list
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
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f) [ Divider() ]

		// Action row
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
			+ SWrapBox::Slot()
			[
				SAssignNew(ApplyCodeBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedCodeFixesClicked)
				[
					SAssignNew(ApplyCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyCode","✓  Apply Selected (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(SendCodeBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnSendCodeToDashboardClicked)
				[
					SAssignNew(SendCodeBtnLabel, STextBlock)
					.Text(LOCTEXT("SendCode","↑  Send to Dashboard"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ CodeEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code issue row
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<ITableRow> SShintToolsPanel::GenerateCodeIssueRow(
	FShintIssueItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const bool bError = (Item->Severity == TEXT("error"));
	const FLinearColor SevColor  = bError ? C_Red() : C_Yellow();
	const FLinearColor RowBG     = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();
	const FString      AutoBadge = Item->bIsAutoFixable ? TEXT("  AUTO") : TEXT("");

	// Location string: for blueprints show "ClassName > GraphName", for C++ show "File:Line"
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

	return SNew(STableRow<FShintIssueItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)

				// Checkbox
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f,2.f,10.f,0.f)
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

					// Row 1: severity ● + rule_id + location + AUTO badge
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,5.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(F_Small())
							.ColorAndOpacity(FSlateColor(SevColor))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->RuleId)).Font(F_RuleId())
							.ColorAndOpacity(FSlateColor(C_White()))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(LocationStr))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(AutoBadge)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_Blue()))
						]
					]

					// Row 2: message (wraps at container width)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,5.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Message)).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White())).AutoWrapText(true)
					]

					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.Visibility(Item->Snippet.IsEmpty()
							? EVisibility::Collapsed : EVisibility::Visible)
						.BorderImage(ST4::Solid(C_CodeBG()))
						.Padding(FMargin(8.f,5.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[ BuildDiffLine(TEXT("\u25B8"), Item->Snippet, C_Red(), C_DiffRed()) ]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f,3.f,0.f,0.f)
							[
								SNew(SBox)
								.Visibility(Item->FixSuggestion.IsEmpty()
									? EVisibility::Collapsed : EVisibility::Visible)
								[ BuildDiffLine(TEXT("\u2192"), Item->FixSuggestion, C_Green(), C_DiffGreen()) ]
							]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildAssetNamingSection()
{
	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f,18.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildSectionTitle(
					LOCTEXT("ANBTitle","ASSET NAMING BOT"),
					LOCTEXT("ANBSub","Scan entire project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard"))
			]

			// Stats
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,16.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(AssetTotal_Label,   LOCTEXT("ANBT","ASSETS"),   C_Blue())   ]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(AssetInvalid_Label, LOCTEXT("ANBI","INVALID"),  C_Red())    ]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
				[ StatBadge(AssetTime_Label,    LOCTEXT("ANBMS","TIME (s)"), C_Gray()) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				BuildModuleProgressBar(AssetProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetAssetProgress)))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,18.f)
			[
				SNew(SButton).ContentPadding(FMargin(14.f,7.f)).HAlign(HAlign_Left)
				.OnClicked(this, &SShintToolsPanel::OnScanAssetsClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("ScanAssets","⟳  Scan All Assets")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildAssetResultsPanel() ]
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildAssetResultsPanel()
{
	SAssignNew(AssetEmptyState, SBox)
	.HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(64.f)
	[
		SNew(STextBlock).Text(LOCTEXT("ANBEmpty","Run a scan to see naming violations."))
		.Font(F_Small()).ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TSharedRef<SWidget> ListArea =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ANBRes","RESULTS")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(10.f,4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllAssetsClicked)
				[ SNew(STextBlock).Text(LOCTEXT("ANBSel","Select All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Blue())) ]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,8.f) [ Divider() ]
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
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f) [ Divider() ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
			+ SWrapBox::Slot()
			[
				SAssignNew(ApplyAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedAssetFixesClicked)
				[
					SAssignNew(ApplyAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyAsset","✓  Apply Corrections (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(SendAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(14.f,7.f))
				.OnClicked(this, &SShintToolsPanel::OnSendAssetToDashboardClicked)
				[
					SAssignNew(SendAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("SendAsset","↑  Send to Dashboard"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Blue()))
				]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [ AssetEmptyState.ToSharedRef() ]
		+ SVerticalBox::Slot().AutoHeight() [ ListArea ];
}

TSharedRef<ITableRow> SShintToolsPanel::GenerateAssetIssueRow(
	FShintAssetItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor RowBG = (Item->OriginalIndex % 2 == 0) ? C_RowEven() : C_RowOdd();

	return SNew(STableRow<FShintAssetItemPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(ST4::Solid(RowBG)).Padding(FMargin(12.f,9.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,10.f,0.f)
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
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("⚠"))).Font(F_Small())
							.ColorAndOpacity(FSlateColor(C_Yellow()))
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetType))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold",10))
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
						.Padding(FMargin(8.f,4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("\u25B8"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Red())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,12.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->CurrentName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DiffRed())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("\u2192"))).Font(F_Mono())
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
// Button handlers
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
	SetCodeState(EModuleState::Running);
	// Reset everything — fresh scan
	AllCodeItems.Empty(); CodeIssueItems.Empty();
	LastCodeResult = FShintValidateResult();
	CoreClient->ValidateProject(FPaths::GameSourceDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnProjectValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBlueprintsClicked()
{
	SetCodeState(EModuleState::Running);
	// Merge into existing results (append to source scan)
	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems) I->bChecked = true;
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

// ─────────────────────────────────────────────────────────────────────────────
// Apply code fixes
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedCodeFixesClicked()
{
	TArray<FShintCodeIssue> Accepted;

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		if (!Item->bChecked) continue;

		FShintCodeIssue I;
		I.RuleId         = Item->RuleId;
		I.Severity       = Item->Severity;
		I.Message        = Item->Message;
		I.FilePath       = Item->FilePath;
		I.Line           = Item->Line;
		I.Snippet        = Item->Snippet;
		I.FixSuggestion  = Item->FixSuggestion;
		I.bIsAutoFixable = Item->bIsAutoFixable;
		I.Class          = Item->Class;
		I.Category       = Item->Category;
		I.Graph          = Item->Graph;
		I.bChecked       = true;
		Accepted.Add(I);
	}

	if (Accepted.IsEmpty()) return FReply::Handled();

	UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Sending %d issue(s) to Core Engine."), Accepted.Num());

	SetCodeState(EModuleState::Running);
	CoreClient->ApplyCodeFixes(Accepted,
		FOnShintFixComplete::CreateSP(this, &SShintToolsPanel::OnCodeFixComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendCodeToDashboardClicked()
{
	CoreClient->SendCodeValidatorToDashboard(LastCodeResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnCodeDashboardComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanAssetsClicked()
{
	SetAssetState(EModuleState::Running);
	AssetIssueItems.Empty();
	CoreClient->ScanAssetNaming(FPaths::ProjectContentDir(),
		FOnShintAssetScanComplete::CreateSP(this, &SShintToolsPanel::OnAssetScanComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllAssetsClicked()
{
	for (FShintAssetItemPtr& I : AssetIssueItems) I->bChecked = true;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply asset fixes
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedAssetFixesClicked()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools"))) return FReply::Handled();

	const FAssetToolsModule& AssetToolsModule =
	FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	IAssetTools& AssetTools = AssetToolsModule.Get();
	
	TArray<FAssetRenameData> RenameData;
	TArray<FShintAssetIssue> ForServer;

	for (const FShintAssetItemPtr& Item : AssetIssueItems)
	{
		if (!Item->bChecked) continue;

		// Load the UObject from its package path (/Game/...AssetName)
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Item->AssetPath);
		if (!Asset) continue;

		const FString NewPackagePath = FPaths::GetPath(Item->AssetPath);
		RenameData.Add(FAssetRenameData(Asset, NewPackagePath, Item->SuggestedName));

		FShintAssetIssue I;
		I.AssetPath    = Item->AssetPath;
		I.CurrentName  = Item->CurrentName;
		I.SuggestedName= Item->SuggestedName;
		I.AssetType    = Item->AssetType;
		ForServer.Add(I);
	}

	if (RenameData.IsEmpty()) return FReply::Handled();

	AssetTools.RenameAssets(RenameData);

	CoreClient->ReportAssetFixesToServer(ForServer,
		FOnShintAssetFixComplete::CreateSP(this, &SShintToolsPanel::OnAssetFixComplete));

	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendAssetToDashboardClicked()
{
	CoreClient->SendAssetNamingToDashboard(LastAssetResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnAssetDashboardComplete));
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP callbacks
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnHealthCheckComplete(const FShintRequestResult& Result)
{
	SetStatus(Result.bSuccess ? ECoreStatus::Online : ECoreStatus::Offline);
}

void SShintToolsPanel::OnProjectValidateComplete(const FShintValidateResult& Result)
{
	HandleValidateResult(Result, false);
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	HandleValidateResult(Result, true);
}

void SShintToolsPanel::HandleValidateResult(const FShintValidateResult& Result, bool bMerge)
{
	if (!Result.bSuccess) { SetCodeState(EModuleState::Error); return; }

	if (bMerge)
	{
		LastCodeResult.bSuccess       = true;
		LastCodeResult.TotalIssues   += Result.TotalIssues;
		LastCodeResult.TotalErrors   += Result.TotalErrors;
		LastCodeResult.TotalWarnings += Result.TotalWarnings;
		LastCodeResult.FilesScanned  += Result.FilesScanned;
		LastCodeResult.Issues.Append(Result.Issues);
	}
	else
	{
		LastCodeResult = Result;
	}

	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(LastCodeResult);
	RefreshCodeStats();
}

void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result)
{
	SetCodeState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: %d fix(es) applied, %d skipped."),
			Result.TotalFixesApplied, Result.TotalFixesSkipped);

		// Remove fixed issues from the master list
		AllCodeItems.RemoveAll([](const FShintIssueItemPtr& I) { return I->bChecked; });

		// Re-apply current filter so the visible list is updated
		ApplyCodeFilter();
		RefreshCodeStats();
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("ApplyFix failed: %s"), *Result.ErrorMessage);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
}

void SShintToolsPanel::OnCodeDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendCodeBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendCodeBtnLabel->SetText(LOCTEXT("SendCodeOk", "✓  Sent!"));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		SendCodeBtnLabel->SetText(LOCTEXT("SendCodeErr", "✗  Send failed"));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error, TEXT("Dashboard send failed: %s"), *Result.ErrorMessage);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (TSharedPtr<SShintToolsPanel> Pin = weak_this.Pin())
			{
				if (Pin->SendCodeBtnLabel.IsValid())
				{
					Pin->SendCodeBtnLabel->SetText(LOCTEXT("SendCodeRst", "↑  Send to Dashboard"));
					Pin->SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

void SShintToolsPanel::OnAssetScanComplete(const FShintAssetScanResult& Result)
{
	if (!Result.bSuccess) { SetAssetState(EModuleState::Error); return; }
	LastAssetResult = Result;
	SetAssetState(EModuleState::Done);
	PopulateAssetIssueList(Result);
	RefreshAssetStats();
}

void SShintToolsPanel::OnAssetFixComplete(const FShintAssetFixResult& Result)
{
	SetAssetState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log, TEXT("AssetFix: %d asset(s) renamed."), Result.AssetsRenamed);

		// Remove renamed assets from the list
		AssetIssueItems.RemoveAll([](const FShintAssetItemPtr& I) { return I->bChecked; });
		if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
		RefreshAssetStats();
		RefreshApplyAssetLabel();
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("AssetFix failed: %s"), *Result.ErrorMessage);
	}
}

void SShintToolsPanel::OnAssetDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendAssetBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendAssetBtnLabel->SetText(LOCTEXT("SendAssetOk", "✓  Sent!"));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		SendAssetBtnLabel->SetText(LOCTEXT("SendAssetErr", "✗  Send failed"));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error, TEXT("Dashboard send failed: %s"), *Result.ErrorMessage);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (const TSharedPtr<SShintToolsPanel> pin = weak_this.Pin())
			{
				if (pin->SendAssetBtnLabel.IsValid())
				{
					pin->SendAssetBtnLabel->SetText(LOCTEXT("SendAssetRst", "↑  Send to Dashboard"));
					pin->SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Populate + refresh
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::PopulateCodeIssueList(const FShintValidateResult& Result)
{
	AllCodeItems.Reset();
	AllCodeItems.Reserve(Result.Issues.Num());

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintCodeIssue& Src = Result.Issues[i];
		FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
		Item->RuleId         = Src.RuleId;
		Item->Severity       = Src.Severity;
		Item->Message        = Src.Message;
		Item->FilePath       = Src.FilePath;
		Item->FileName       = FPaths::GetCleanFilename(Src.FilePath);
		Item->Line           = Src.Line;
		Item->Snippet        = Src.Snippet;
		Item->FixSuggestion  = Src.FixSuggestion;
		Item->bIsAutoFixable = Src.bIsAutoFixable;
		Item->bChecked       = Src.bIsAutoFixable;
		Item->OriginalIndex  = i;
		Item->Class          = Src.Class;
		Item->Category       = Src.Category;
		Item->Graph          = Src.Graph;
		AllCodeItems.Add(MoveTemp(Item));
	}

	ApplyCodeFilter();

	if (CodeEmptyState.IsValid())
		CodeEmptyState->SetVisibility(
			AllCodeItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);

	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(!AllCodeItems.IsEmpty());
	if (SendCodeBtn.IsValid())  SendCodeBtn->SetEnabled(true);
	RefreshApplyCodeLabel();
}

void SShintToolsPanel::ApplyCodeFilter()
{
	CodeIssueItems.Reset();
	CodeIssueItems.Reserve(AllCodeItems.Num());

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		switch (CurrentFilter)
		{
		case EIssueFilter::ErrorsOnly:   if (Item->Severity != TEXT("error"))   continue; break;
		case EIssueFilter::WarningsOnly: if (Item->Severity != TEXT("warning")) continue; break;
		case EIssueFilter::FixableOnly:  if (!Item->bIsAutoFixable)             continue; break;
		default: break;
		}
		CodeIssueItems.Add(Item);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();
}

void SShintToolsPanel::PopulateAssetIssueList(const FShintAssetScanResult& Result)
{
	AssetIssueItems.Reset();
	AssetIssueItems.Reserve(Result.Issues.Num());

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintAssetIssue& Src = Result.Issues[i];
		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath    = Src.AssetPath;
		Item->CurrentName  = Src.CurrentName;
		Item->SuggestedName= Src.SuggestedName;
		Item->Reason       = Src.Reason;
		Item->AssetType    = Src.AssetType;
		Item->bChecked     = true;
		Item->OriginalIndex= i;
		AssetIssueItems.Add(MoveTemp(Item));
	}

	if (AssetIssueListView.IsValid()) AssetIssueListView->RequestListRefresh();

	if (AssetEmptyState.IsValid())
		AssetEmptyState->SetVisibility(
			AssetIssueItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);

	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(!AssetIssueItems.IsEmpty());
	if (SendAssetBtn.IsValid())  SendAssetBtn->SetEnabled(true);
	RefreshApplyAssetLabel();
}

void SShintToolsPanel::RefreshCodeStats()
{
	if (CodeFiles_Label.IsValid())    CodeFiles_Label->SetText(FText::FromString(FmtN(LastCodeResult.FilesScanned)));
	if (CodeErrors_Label.IsValid())   CodeErrors_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalErrors)));
	if (CodeWarnings_Label.IsValid()) CodeWarnings_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalWarnings)));
}

void SShintToolsPanel::RefreshAssetStats()
{
	if (AssetTotal_Label.IsValid())   AssetTotal_Label->SetText(FText::FromString(FmtN(LastAssetResult.TotalAssets)));
	if (AssetInvalid_Label.IsValid()) AssetInvalid_Label->SetText(FText::FromString(FmtN(LastAssetResult.InvalidAssets)));
	if (AssetTime_Label.IsValid())    AssetTime_Label->SetText(FText::FromString(
		FString::Printf(TEXT("%.2f"), LastAssetResult.ScanTimeSeconds)));
}

void SShintToolsPanel::RefreshApplyCodeLabel()
{
	const int32 N = Algo::CountIf(AllCodeItems,
		[](const FShintIssueItemPtr& P){ return P->bChecked; });
	if (ApplyCodeBtnLabel.IsValid())
		ApplyCodeBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Selected (%d)"), N)));
	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(N > 0);
}

void SShintToolsPanel::RefreshApplyAssetLabel()
{
	const int32 N = Algo::CountIf(AssetIssueItems,
		[](const FShintAssetItemPtr& P){ return P->bChecked; });
	if (ApplyAssetBtnLabel.IsValid())
		ApplyAssetBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Corrections (%d)"), N)));
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(N > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// State setters + attribute getters
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::SetStatus(ECoreStatus S)
{ StatusState = S; Invalidate(EInvalidateWidget::Paint); }

void SShintToolsPanel::SetCodeState(EModuleState S)
{ CodeState = S; Invalidate(EInvalidateWidget::Paint); }

void SShintToolsPanel::SetAssetState(EModuleState S)
{ AssetState = S; Invalidate(EInvalidateWidget::Paint); }

FSlateColor SShintToolsPanel::GetStatusColor() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return FSlateColor(C_Green());
	case ECoreStatus::Offline:  return FSlateColor(C_Red());
	case ECoreStatus::Checking: return FSlateColor(C_Yellow());
	default:                    return FSlateColor(C_DimGray());
	}
}

FText SShintToolsPanel::GetStatusText() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return LOCTEXT("On",  "Online");
	case ECoreStatus::Offline:  return LOCTEXT("Off", "Offline");
	case ECoreStatus::Checking: return LOCTEXT("Chk", "Checking…");
	default:                    return LOCTEXT("Unk", "Not checked");
	}
}

TOptional<float> SShintToolsPanel::GetCodeProgress() const
{
	if (CodeState == EModuleState::Running) return TOptional<float>();
	if (CodeState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

TOptional<float> SShintToolsPanel::GetAssetProgress() const
{
	if (AssetState == EModuleState::Running) return TOptional<float>();
	if (AssetState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

#undef LOCTEXT_NAMESPACE
