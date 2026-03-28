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
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
// Style
#include "Styling/AppStyle.h"
// Plugin manager (for banner path)
#include "Interfaces/IPluginManager.h"
// Asset tools (for IAssetTools::RenameAssets)
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
// Misc
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Algo/Count.h"

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

	LoadBannerBrush();

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(0.f)
		[
			SNew(SScrollBox).Orientation(Orient_Vertical)
			+ SScrollBox::Slot().Padding(0.f) [ BuildHeader()               ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildStatusBar()            ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildCodeValidatorSection() ]
			+ SScrollBox::Slot().Padding(0.f) [ BuildAssetNamingSection()   ]
		]
	];
}

SShintToolsPanel::~SShintToolsPanel()
{
	BannerBrush.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Banner loader
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::LoadBannerBrush()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ShintTools"));
	if (!Plugin.IsValid()) return;

	const FString BannerPath = Plugin->GetBaseDir() / TEXT("Resources/ShintTools_Banner.png");
	if (!FPaths::FileExists(BannerPath)) return;

	// Display at 240×48 in the panel header (scaled from 4K source)
	BannerBrush = MakeShared<FSlateDynamicImageBrush>(*BannerPath, FVector2D(240.f, 48.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Divider helper
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::Divider()
{
	return SNew(SBox).HeightOverride(1.f)
		[ SNew(SBorder).BorderImage(ST4::Solid(C_Border())).Padding(0.f) ];
}

// ─────────────────────────────────────────────────────────────────────────────
// Header — black strip, bold "ShintTools", banner image upper-right
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildHeader()
{
	// Right side: banner or fallback version text
	TSharedRef<SWidget> RightWidget = BannerBrush.IsValid()
		? StaticCastSharedRef<SWidget>(
			SNew(SBox).WidthOverride(240.f).HeightOverride(48.f).VAlign(VAlign_Center)
			[ SNew(SImage).Image(BannerBrush.Get()) ])
		: StaticCastSharedRef<SWidget>(
			SNew(STextBlock).Text(LOCTEXT("Ver","v4.0")).Font(F_Label())
			.ColorAndOpacity(FSlateColor(C_DimGray())));

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_BG()))
		.Padding(FMargin(20.f, 18.f, 20.f, 14.f))
		[
			SNew(SHorizontalBox)

			// Left: Brand text
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
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
			]

			// Right: banner image (upper-right corner)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f,0.f,0.f,0.f)
			[ RightWidget ]
		];
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

			// Title
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,3.f)
			[
				SNew(STextBlock).Text(LOCTEXT("CVTitle","CODE VALIDATOR")).Font(F_H2())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,16.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CVSub","Analyse C++ source and Blueprints · review issues · apply fixes · send to dashboard"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray()))
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

			// Progress bar
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				SNew(SBox).HeightOverride(2.f)
				[
					SAssignNew(CodeProgressBar, SProgressBar)
					.Percent(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetCodeProgress)))
					.FillColorAndOpacity(FSlateColor(C_Blue()))
					.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
				]
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

		// Virtual list — fixed item height for performance with hundreds of rows
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).MaxDesiredHeight(420.f)
			[
				SAssignNew(CodeIssueListView, SListView<FShintIssueItemPtr>)
				.ListItemsSource(&CodeIssueItems)
				.OnGenerateRow(this, &SShintToolsPanel::GenerateCodeIssueRow)
				.SelectionMode(ESelectionMode::None)// fixed height → no re-measure per row → fast scroll
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
					SNew(STextBlock).Text(LOCTEXT("SendCode","↑  Send to Dashboard"))
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

					// Row 1: severity ● + rule_id + file:line + AUTO badge
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
							.Text(FText::FromString(
								FString::Printf(TEXT("%s : %d"), *Item->FileName, Item->Line)))
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

					// Row 3: code diff (▸ bad → ✓ good)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.Visibility(Item->Snippet.IsEmpty()
							? EVisibility::Collapsed : EVisibility::Visible)
						.BorderImage(ST4::Solid(FLinearColor(0.055f,0.055f,0.055f,1.f)))
						.Padding(FMargin(8.f,5.f))
						[
							SNew(SVerticalBox)

							// Current (red)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
								[ SNew(STextBlock).Text(FText::FromString(TEXT("▸"))).Font(F_Mono())
								  .ColorAndOpacity(FSlateColor(C_Red())) ]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[ SNew(STextBlock).Text(FText::FromString(Item->Snippet))
								  .Font(F_Mono()).ColorAndOpacity(FSlateColor(
								      FLinearColor(0.90f,0.48f,0.48f,1.f))).AutoWrapText(true) ]
							]

							// Fix suggestion (green)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f,3.f,0.f,0.f)
							[
								SNew(SHorizontalBox)
								.Visibility(Item->FixSuggestion.IsEmpty()
									? EVisibility::Collapsed : EVisibility::Visible)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
								[ SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(F_Mono())
								  .ColorAndOpacity(FSlateColor(C_Green())) ]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[ SNew(STextBlock).Text(FText::FromString(Item->FixSuggestion))
								  .Font(F_Mono()).ColorAndOpacity(FSlateColor(
								      FLinearColor(0.48f,0.90f,0.48f,1.f))).AutoWrapText(true) ]
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

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,3.f)
			[
				SNew(STextBlock).Text(LOCTEXT("ANBTitle","ASSET NAMING BOT")).Font(F_H2())
				.ColorAndOpacity(FSlateColor(C_White()))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,16.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ANBSub","Scan entire project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard"))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Gray()))
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

			// Progress
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,14.f)
			[
				SNew(SBox).HeightOverride(2.f)
				[
					SAssignNew(AssetProgressBar, SProgressBar)
					.Percent(TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetAssetProgress)))
					.FillColorAndOpacity(FSlateColor(C_Blue()))
					.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
				]
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
			SNew(SBox).MaxDesiredHeight(360.f)
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
					SNew(STextBlock).Text(LOCTEXT("SendAsset","↑  Send to Dashboard"))
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

					// Current → suggested
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(ST4::Solid(FLinearColor(0.055f,0.055f,0.055f,1.f)))
						.Padding(FMargin(8.f,4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("▸"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Red())) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,12.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->CurrentName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(FLinearColor(0.9f,0.45f,0.45f,1.f))) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f)
							[ SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(F_Mono())
							  .ColorAndOpacity(FSlateColor(C_Green())) ]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[ SNew(STextBlock).Text(FText::FromString(Item->SuggestedName))
							  .Font(F_Mono()).ColorAndOpacity(FSlateColor(FLinearColor(0.45f,0.9f,0.45f,1.f))) ]
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
	AllCodeItems.Empty(); CodeIssueItems.Empty();
	CoreClient->ValidateProject(FPaths::GameSourceDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnProjectValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBlueprintsClicked()
{
	SetCodeState(EModuleState::Running);
	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelectAllCodeClicked()
{
	for (FShintIssueItemPtr& I : AllCodeItems)
		if (I->bIsAutoFixable) I->bChecked = true;
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
		if (!Item->bChecked || !Item->bIsAutoFixable) continue;

		FShintCodeIssue I;
		I.RuleId         = Item->RuleId;
		I.Severity       = Item->Severity;
		I.Message        = Item->Message;
		I.FilePath       = Item->FilePath;
		I.Line           = Item->Line;
		I.bIsAutoFixable = true;
		I.bChecked       = true;
		Accepted.Add(I);
	}

	if (Accepted.IsEmpty()) return FReply::Handled();

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
	if (!Result.bSuccess) { SetCodeState(EModuleState::Error); return; }
	LastCodeResult = Result;
	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(Result);
	RefreshCodeStats();
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	if (!Result.bSuccess) { SetCodeState(EModuleState::Error); return; }
	// Merge
	LastCodeResult.bSuccess       = true;
	LastCodeResult.TotalIssues   += Result.TotalIssues;
	LastCodeResult.TotalErrors   += Result.TotalErrors;
	LastCodeResult.TotalWarnings += Result.TotalWarnings;
	LastCodeResult.FilesScanned  += Result.FilesScanned;
	for (const FShintCodeIssue& I : Result.Issues) LastCodeResult.Issues.Add(I);

	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(LastCodeResult);
	RefreshCodeStats();
}

void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result)
{
	SetCodeState(EModuleState::Done);
	if (!Result.bSuccess) return;

	for (FShintIssueItemPtr& I : AllCodeItems)
		if (I->bChecked && I->bIsAutoFixable) I->bChecked = false;

	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
}

void SShintToolsPanel::OnCodeDashboardComplete(const FShintWebDashboardResult& Result)
{
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
}

void SShintToolsPanel::OnAssetDashboardComplete(const FShintWebDashboardResult& Result)
{
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
		[](const FShintIssueItemPtr& P){ return P->bChecked && P->bIsAutoFixable; });
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

FString SShintToolsPanel::TimeStr()
{
	const FDateTime N = FDateTime::Now();
	return FString::Printf(TEXT("[%02d:%02d:%02d]"), N.GetHour(), N.GetMinute(), N.GetSecond());
}

FString SShintToolsPanel::FmtN(int32 N)
{ return N < 0 ? TEXT("—") : FString::Printf(TEXT("%d"), N); }

#undef LOCTEXT_NAMESPACE
