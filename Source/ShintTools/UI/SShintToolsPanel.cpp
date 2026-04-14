// Copyright ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

// Slate windows / dialogs
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

// Slate layout
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"

// Slate widgets
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"
// Style
#include "Styling/AppStyle.h"
// Asset tools (for IAssetTools::RenameAssets + FixupReferencers)
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/ObjectRedirector.h"
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
	// TUniquePtr keeps FSlateBrush at a stable heap address — TMap reallocation
	// does NOT invalidate the brush pointer stored in Slate widget attributes.
	static TMap<FString, TUniquePtr<FSlateBrush>> BC;

	static const FSlateBrush* Solid(const FLinearColor& C, float R = 0.f)
	{
		const FString K = FString::Printf(TEXT("S%.3f%.3f%.3f%.1f"), C.R, C.G, C.B, R);
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(C);
			B->DrawAs    = R > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
			if (R > 0.f) {
				B->OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
				B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			}
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
	}

	static const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R = 4.f)
	{
		const FString K = FString::Printf(TEXT("O%.3f%.3f%.1f"), Fill.R, Brd.R, R);
		if (!BC.Contains(K))
		{
			TUniquePtr<FSlateBrush> B = MakeUnique<FSlateBrush>();
			B->TintColor = FSlateColor(Fill);
			B->DrawAs    = ESlateBrushDrawType::RoundedBox;
			B->OutlineSettings.CornerRadii  = FVector4(R,R,R,R);
			B->OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			B->OutlineSettings.Color        = FSlateColor(Brd);
			B->OutlineSettings.Width        = 1.f;
			BC.Add(K, MoveTemp(B));
		}
		return BC[K].Get();
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

TSharedRef<SWidget> SShintToolsPanel::BuildContextPanel(
	const FString& Label, const FString& ContextText,
	int32 ContextLineStart, int32 IssueLineNo,
	const FLinearColor& HighlightColor)
{
	// Build a titled code block that highlights IssueLineNo
	TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

	// Header label (BEFORE / AFTER)
	Lines->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
	[
		SNew(STextBlock).Text(FText::FromString(Label)).Font(F_Label())
		.ColorAndOpacity(FSlateColor(C_DimGray()))
	];

	TArray<FString> SrcLines;
	ContextText.ParseIntoArray(SrcLines, TEXT("\n"), false);

	for (int32 Idx = 0; Idx < SrcLines.Num(); ++Idx)
	{
		const int32 LineNo = ContextLineStart + Idx;
		const bool  bIsIssueLine = (LineNo == IssueLineNo);
		const FLinearColor TextCol = bIsIssueLine ? HighlightColor : C_Gray();
		const FString Prefix = FString::Printf(TEXT("%4d  "), LineNo);

		Lines->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Text(FText::FromString(Prefix))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).Text(FText::FromString(SrcLines[Idx]))
				.Font(F_Mono()).ColorAndOpacity(FSlateColor(TextCol))
				.AutoWrapText(false)
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(ST4::Solid(C_CodeBG()))
		.Padding(FMargin(8.f, 6.f))
		[ Lines ];
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
						SNew(STextBlock).Text(LOCTEXT("ScanSrc","⟳  Scan All C++ Source")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanBlueprintsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanBP","⟳  Scan All BP")).Font(F_Small())
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

TSharedRef<SWidget> SShintToolsPanel::BuildCodeTypeMenuContent()
{
	struct FEntry { FText Label; ECodeTypeFilter Value; };
	const TArray<FEntry> Entries = {
		{ LOCTEXT("CodeTypeAll", "All Types"),       ECodeTypeFilter::All           },
		{ LOCTEXT("CodeTypeCpp", "C++ Only"),         ECodeTypeFilter::CppOnly       },
		{ LOCTEXT("CodeTypeBP",  "Blueprints Only"),  ECodeTypeFilter::BlueprintsOnly},
	};

	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
	for (const FEntry& E : Entries)
	{
		const FText  Label = E.Label;
		const ECodeTypeFilter Value = E.Value;
		Menu->AddSlot().AutoHeight()
		[
			SNew(SButton).ContentPadding(FMargin(10.f, 5.f))
			.ButtonColorAndOpacity(FSlateColor(C_Surface()))
			.OnClicked_Lambda([this, Label, Value]() -> FReply
			{
				CurrentCodeTypeFilter = Value;
				if (CodeTypeFilterLabel.IsValid())
					CodeTypeFilterLabel->SetText(Label);
				ApplyCodeFilter();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
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
	// Category dropdown
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
				.Text(LOCTEXT("CatAll","All Categories"))
				.Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f,0.f,0.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("\u25BE")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
		];

	// Severity dropdown
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
				.Text(LOCTEXT("SevAll","All Severities"))
				.Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f,0.f,0.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("\u25BE")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
		];

	// Code type dropdown (C++ / Blueprints / All)
	TSharedRef<SWidget> CodeTypeCombo =
		SNew(SComboButton)
		.ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnGetMenuContent(this, &SShintToolsPanel::BuildCodeTypeMenuContent)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(CodeTypeFilterLabel, STextBlock)
				.Text(LOCTEXT("CodeTypeAll","All Types"))
				.Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f,0.f,0.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("\u25BE")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
		];

	// Fixable toggle
	TSharedRef<SWidget> FixableBtn =
		SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
		.ButtonColorAndOpacity(FSlateColor(C_Surface()))
		.OnClicked_Lambda([this]() -> FReply
		{
			CurrentFilter = (CurrentFilter == EIssueFilter::FixableOnly)
				? EIssueFilter::All : EIssueFilter::FixableOnly;
			ApplyCodeFilter();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(LOCTEXT("FFix","Fixable Only")).Font(F_Label())
			.ColorAndOpacity(FSlateColor(C_Green()))
		];

	return SNew(SVerticalBox)

		// Row 1: "RESULTS" label + dropdowns + fixable toggle
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("ResLbl","RESULTS")).Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_DimGray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ CodeTypeCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ CategoryCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ SeverityCombo ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,10.f,0.f) [ FixableBtn    ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,4.f,0.f)
			[
				SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllCodeClicked)
				[ SNew(STextBlock).Text(LOCTEXT("SelAll","Select All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Blue())) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(8.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllCodeClicked)
				[ SNew(STextBlock).Text(LOCTEXT("DeselAll","Deselect All")).Font(F_Label())
				  .ColorAndOpacity(FSlateColor(C_Gray())) ]
			]
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
		SAssignNew(CodeEmptyText, STextBlock)
		.Text(LOCTEXT("CVEmpty","Run a scan to see results here."))
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

	// bHasContext: true when server sent context window OR when we can fetch it (tree-sitter)
	const bool bHasContext  = !Item->ContextBefore.IsEmpty() || !Item->FileContent.IsEmpty();
	// bIsFixable: auto-fixable issues (tree-sitter, local line-replacement, or plugin-side BP handler)
	const bool bIsFixable   = Item->bIsAutoFixable
		&& (!Item->FixSuggestion.IsEmpty() || !Item->FileContent.IsEmpty() || Item->bIsBlueprint);

	// ── Context diff panels (shown when Preview is toggled) ──────────────────
	// Priority: FixPreviewCode (fetched on-demand from /validate/fix) >
	//           ContextAfter  (pre-computed by server fixer during scan — real code).
	// ContextAfter is now always actual fixed code from the tree-sitter/pattern fixer,
	// never a human-readable description, so it is safe to display for all issue types.
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

	// ── Compact diff fallback (blueprints / issues without context window) ───
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
				[ BuildDiffLine(TEXT("\u25B8"), Item->Snippet, C_Red(), C_DiffRed()) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SBox).Visibility(Item->FixSuggestion.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[ BuildDiffLine(TEXT("\u2192"), Item->FixSuggestion, C_Green(), C_DiffGreen()) ]
			]
		];

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

					// Row 1: severity ● + rule_id + location + preview toggle + AUTO badge
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
							SNew(STextBlock).Text(FText::FromString(LocationStr))
							.Font(F_Mono()).ColorAndOpacity(FSlateColor(C_Gray()))
						]
						// Preview toggle — only for issues with context
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f,0.f,6.f,0.f)
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
											? TEXT("\u25BC Preview") : TEXT("\u25B6 Preview"));
									})
									.Font(F_Label()).ColorAndOpacity(FSlateColor(C_Blue()))
								]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(AutoBadge)).Font(F_Label())
							.ColorAndOpacity(FSlateColor(C_Blue()))
						]
					]

					// Row 2: message
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,5.f)
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
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,8.f,0.f)
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnApplySingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("ApplySingle","✓  Apply"))
									.Font(F_Small()).ColorAndOpacity(FSlateColor(C_Green()))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton).ContentPadding(FMargin(12.f, 5.f))
								.OnClicked(this, &SShintToolsPanel::OnIgnoreSingleFix, Item)
								[
									SNew(STextBlock).Text(LOCTEXT("IgnoreSingle","✗  Ignore"))
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
				SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f,6.f))
				+ SWrapBox::Slot()
				[
					SNew(SButton).ContentPadding(FMargin(14.f,7.f))
					.OnClicked(this, &SShintToolsPanel::OnScanAssetsClicked)
					[
						SNew(STextBlock).Text(LOCTEXT("ScanAssets","⟳  Scan All Assets")).Font(F_Small())
						.ColorAndOpacity(FSlateColor(C_White()))
					]
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
		SAssignNew(AssetEmptyText, STextBlock)
		.Text(LOCTEXT("ANBEmpty","Run a scan to see naming violations."))
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
				.Text(LOCTEXT("ATAll","All Types"))
				.Font(F_Label())
				.ColorAndOpacity(FSlateColor(C_Gray()))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f,0.f,0.f,0.f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("\u25BE")))
				.Font(F_Label()).ColorAndOpacity(FSlateColor(C_DimGray()))
			]
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
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f,0.f,6.f,0.f) [ AssetTypeCombo ]
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
	// ── All-clear guard ───────────────────────────────────────────────────────
	// If every issue found in the previous scan has already been fixed this
	// session, skip the server round-trip and show an informational message.
	// Clearing AppliedFixFingerprints means the *next* click triggers a real scan.
	if (AllCodeItems.IsEmpty() && !AppliedFixFingerprints.IsEmpty())
	{
		AppliedFixFingerprints.Empty();
		if (CodeEmptyText.IsValid())
			CodeEmptyText->SetText(LOCTEXT("CVAllFixed",
				"✓  All issues resolved — click 'Scan' again to do a full re-scan."));
		if (CodeEmptyState.IsValid())
			CodeEmptyState->SetVisibility(EVisibility::Visible);
		return FReply::Handled();
	}

	++ScanGeneration;
	bBlueprintScanActive = false;   // full source scan — no BP-only filter

	// Auto-set the code type filter to C++ Only for this scan
	CurrentCodeTypeFilter = ECodeTypeFilter::CppOnly;
	if (CodeTypeFilterLabel.IsValid())
		CodeTypeFilterLabel->SetText(LOCTEXT("CodeTypeCpp","C++ Only"));

	SetCodeState(EModuleState::Running);
	// Reset to default empty text before new results arrive
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));
	// Reset everything — fresh scan.  Clear visible list and notify Slate
	// BEFORE emptying backing data, so no stale pointers are accessed.
	CodeIssueItems.Empty();
	AllCodeItems.Empty();
	AppliedFixFingerprints.Empty(); // fresh scan: re-evaluate all issues
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	LastCodeResult = FShintValidateResult();
	CoreClient->ValidateProject(FPaths::GameSourceDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnProjectValidateComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBlueprintsClicked()
{
	++ScanGeneration;
	bBlueprintScanActive = true;

	// Auto-set the code type filter to Blueprints Only for this scan
	CurrentCodeTypeFilter = ECodeTypeFilter::BlueprintsOnly;
	if (CodeTypeFilterLabel.IsValid())
		CodeTypeFilterLabel->SetText(LOCTEXT("CodeTypeBP","Blueprints Only"));

	// ── Code Validator: replace list with BP-only results ─────────────────────
	SetCodeState(EModuleState::Running);
	CodeIssueItems.Empty();
	AllCodeItems.Empty();
	AppliedFixFingerprints.Empty();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	LastCodeResult = FShintValidateResult();
	if (CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVEmpty", "Run a scan to see results here."));

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

// ─────────────────────────────────────────────────────────────────────────────
// Apply code fixes
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedCodeFixesClicked()
{
	TArray<FShintCodeIssue> Accepted;

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Only accept issues that are checked AND actually auto-fixable
		if (!Item->bChecked || !Item->bIsAutoFixable) continue;
		// BP issues use plugin-side handlers — no FixSuggestion required
		if (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()) continue;

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
		I.FileContent    = Item->FileContent;
		I.bChecked       = true;
		Accepted.Add(I);
	}

	// Log why Accepted might be empty — counters are independent of bIsAutoFixable
	int32 TotalChecked = 0, TotalNotFixable = 0, TotalNoFixSuggestion = 0;
	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		if (Item->bChecked) ++TotalChecked;
		if (Item->bChecked && !Item->bIsAutoFixable)        ++TotalNotFixable;
		if (Item->bChecked && Item->FixSuggestion.IsEmpty()) ++TotalNoFixSuggestion;
	}
	UE_LOG(LogShintTools, Log,
		TEXT("ApplyFix: Checked=%d, NotAutoFixable=%d, NoFixSuggestion=%d, Accepted=%d"),
		TotalChecked, TotalNotFixable, TotalNoFixSuggestion, Accepted.Num());

	if (Accepted.IsEmpty())
	{
		UE_LOG(LogShintTools, Warning, TEXT("ApplyFix: Nothing to apply — no auto-fixable issues selected"));
		return FReply::Handled();
	}

	UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Applying %d fix(es) locally."), Accepted.Num());

	PendingCodeFixes = Accepted;
	SetCodeState(EModuleState::Running);
	CoreClient->CheckFixSafety(Accepted,
		FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSendCodeToDashboardClicked()
{
	CoreClient->SendCodeValidatorToDashboard(LastCodeResult,
		FOnShintWebDashboardComplete::CreateSP(this, &SShintToolsPanel::OnCodeDashboardComplete));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnApplySingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid() || !Item->bIsAutoFixable
		|| (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()))
		return FReply::Handled();

	TArray<FShintCodeIssue> Issues;
	FShintCodeIssue I;
	I.RuleId         = Item->RuleId;
	I.Severity       = Item->Severity;
	I.Message        = Item->Message;
	I.FilePath       = Item->FilePath;
	I.Line           = Item->Line;
	I.Snippet        = Item->Snippet;
	I.FixSuggestion  = Item->FixSuggestion;
	I.bIsAutoFixable = true;
	I.Class          = Item->Class;
	I.Category       = Item->Category;
	I.Graph          = Item->Graph;
	I.FileContent    = Item->FileContent;
	I.bChecked       = true;
	Issues.Add(I);

	PendingCodeFixes = Issues;
	SetCodeState(EModuleState::Running);
	CoreClient->CheckFixSafety(Issues,
		FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Safety Check + Modal Dialog
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnSafetyCheckComplete(const FShintSafetyCheckResult& Result)
{
	if (Result.bSafe)
	{
		ProceedWithCodeFixes();
	}
	else
	{
		// Not safe — restore idle and show warning dialog
		SetCodeState(EModuleState::Idle);
		ShowSafetyWarningDialog(Result);
	}
}

void SShintToolsPanel::ShowSafetyWarningDialog(const FShintSafetyCheckResult& Result)
{
	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(NSLOCTEXT("ShintTools", "SafetyTitle", "Safety Check"))
		.ClientSize(FVector2D(560, 420))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize);

	// ── Header ────────────────────────────────────────────────────────────────
	TSharedRef<SVerticalBox> WarningList = SNew(SVerticalBox);
	for (int32 i = 0; i < Result.Warnings.Num(); ++i)
	{
		WarningList->AddSlot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d."), i + 1)))
				.Font(F_Small())
				.ColorAndOpacity(C_Gray())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Result.Warnings[i]))
				.Font(F_Small())
				.ColorAndOpacity(C_White())
				.AutoWrapText(true)
			]
		];
	}

	// ── Buttons ───────────────────────────────────────────────────────────────
	TSharedPtr<SWindow> DialogPtr = TSharedPtr<SWindow>(&Dialog.Get(), [](SWindow*){});
	TWeakPtr<SWindow> WeakDialog(Dialog);

	Dialog->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(24.f)
		[
			SNew(SVerticalBox)
			// Header
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ShintTools", "SafetyHeader", "These fixes may affect your code"))
				.Font(F_H2())
				.ColorAndOpacity(C_Yellow())
			]
			// Warning list
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					WarningList
				]
			]
			// Preview (if provided)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Visibility(Result.Preview.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Result.Preview))
					.Font(F_Mono())
					.ColorAndOpacity(C_Gray())
					.AutoWrapText(true)
				]
			]
			// Buttons
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Surface())
					.OnClicked_Lambda([WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(NSLOCTEXT("ShintTools","Cancel","Cancel"))
						.Font(F_Body()).ColorAndOpacity(C_Gray())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Yellow())
					.OnClicked_Lambda([this, WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						ProceedWithCodeFixes();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("ShintTools","ApplyAnyway","Apply Anyway"))
						.Font(F_Body()).ColorAndOpacity(C_BG())
					]
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
}

void SShintToolsPanel::ProceedWithCodeFixes()
{
	if (PendingCodeFixes.IsEmpty()) return;

	const uint32 FixGeneration = ScanGeneration;
	SetCodeState(EModuleState::Running);
	CoreClient->ApplyCodeFixes(PendingCodeFixes,
		FOnShintFixComplete::CreateSP(this, &SShintToolsPanel::OnCodeFixComplete, FixGeneration));
	PendingCodeFixes.Empty();
}


FReply SShintToolsPanel::OnIgnoreSingleFix(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	// Fingerprint so this issue is skipped on the next incremental scan
	const FString Fingerprint = FString::Printf(
		TEXT("%s:%d:%s"), *Item->FilePath, Item->Line, *Item->RuleId);
	AppliedFixFingerprints.Add(Fingerprint);

	// Remove from backing store and rebuild visible list
	AllCodeItems.RemoveAll([&Item](const FShintIssueItemPtr& P){ return P == Item; });
	ApplyCodeFilter();
	RefreshApplyCodeLabel();
	return FReply::Handled();
}

void SShintToolsPanel::FetchFixPreview(FShintIssueItemPtr Item)
{
	if (!Item.IsValid() || Item->FileContent.IsEmpty() || Item->bFixPreviewLoading) return;

	Item->bFixPreviewLoading = true;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	FShintCodeIssue Issue;
	Issue.RuleId      = Item->RuleId;
	Issue.FilePath    = Item->FilePath;
	Issue.Line        = Item->Line;
	Issue.FileContent = Item->FileContent;

	// Capture info needed to extract the window from fixed_code
	const int32 ContextStart = Item->ContextLineStart; // 1-based
	TArray<FString> BeforeLines;
	Item->ContextBefore.ParseIntoArray(BeforeLines, TEXT("\n"), false);
	const int32 NumContextLines = FMath::Max(1, BeforeLines.Num());

	TWeakPtr<SShintToolsPanel> WeakPtr = SharedThis(this);
	TWeakPtr<FShintIssueItem>  WeakItem = Item;

	CoreClient->FetchSingleFixPreview(Issue,
		FOnShintFixComplete::CreateLambda(
			[WeakPtr, WeakItem, ContextStart, NumContextLines](const FShintFixResult& Result) mutable
		{
			TSharedPtr<SShintToolsPanel> PinnedPanel = WeakPtr.Pin();
			TSharedPtr<FShintIssueItem>  PinnedItem  = WeakItem.Pin();
			if (!PinnedPanel.IsValid() || !PinnedItem.IsValid()) return;

			PinnedItem->bFixPreviewLoading = false;

			if (Result.bSuccess && Result.FixedFiles.Num() > 0)
			{
				const FString& FixedCode = Result.FixedFiles[0].CorrectedContent;
				TArray<FString> AllLines;
				FixedCode.ParseIntoArray(AllLines, TEXT("\n"), false);

				const int32 StartIdx = FMath::Max(0, ContextStart - 1);
				TArray<FString> Window;
				for (int32 i = StartIdx; i < StartIdx + NumContextLines && i < AllLines.Num(); ++i)
					Window.Add(AllLines[i]);

				PinnedItem->FixPreviewCode = FString::Join(Window, TEXT("\n"));
			}
			else
			{
				UE_LOG(LogShintTools, Warning, TEXT("FetchFixPreview: server error for [%s] — %s"),
					*PinnedItem->RuleId, *Result.ErrorMessage);
			}

			if (PinnedPanel->CodeIssueListView.IsValid())
				PinnedPanel->CodeIssueListView->RequestListRefresh();
		}));
}

FReply SShintToolsPanel::OnScanAssetsClicked()
{
	// ── All-clear guard ───────────────────────────────────────────────────────
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
	// Reset to default empty text before new results arrive
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

	// ── Fix redirectors left at old paths ─────────────────────────────────────
	// After RenameAssets, UE5 creates an ObjectRedirector at the original package
	// path. Collect all redirectors under /Game and fix references so no stale
	// pointers remain and DefaultEngine.ini stays clean.
	{
		IAssetRegistry& AR =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FARFilter RedirFilter;
		RedirFilter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		RedirFilter.PackagePaths.Add(TEXT("/Game"));
		RedirFilter.bRecursivePaths = true;

		TArray<FAssetData> RedirAssets;
		AR.GetAssets(RedirFilter, RedirAssets);

		TArray<UObjectRedirector*> Redirectors;
		Redirectors.Reserve(RedirAssets.Num());
		for (const FAssetData& RD : RedirAssets)
		{
			if (UObjectRedirector* Redir = Cast<UObjectRedirector>(RD.GetAsset()))
				Redirectors.Add(Redir);
		}
		if (!Redirectors.IsEmpty())
		{
			UE_LOG(LogShintTools, Log,
				TEXT("ShintPanel: fixing %d redirector(s) after asset rename"), Redirectors.Num());
			AssetTools.FixupReferencers(Redirectors);
		}
	}

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
	HandleValidateResult(Result, /*bMerge=*/false, /*bIsBPScan=*/false);
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	// Separate naming issues (BPB001) → route to Asset Naming panel
	FShintValidateResult QualityResult;
	QualityResult.bSuccess       = Result.bSuccess;
	QualityResult.FilesScanned   = Result.FilesScanned;
	QualityResult.Issues.Reserve(Result.Issues.Num());

	int32 NamingRouted = 0;

	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		// BPB001 (naming) → Asset Naming panel ONLY when triggered by the legacy
		// naming-chain path (bBlueprintScanActive=false). When the user clicks
		// "Scan All BP", all issues stay in the Code Validator — no asset panel side-effect.
		if (!bBlueprintScanActive && Issue.RuleId == TEXT("BPB001"))
		{
			// Convert to asset naming item and add to backing store
			FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
			Item->AssetPath     = Issue.FilePath;
			Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
			Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
			Item->Reason        = Issue.Message;
			Item->AssetType     = TEXT("Blueprint");
			Item->bChecked      = true;
			Item->OriginalIndex = AllAssetItems.Num();
			AllAssetItems.Add(MoveTemp(Item));
			++NamingRouted;
		}
		else
		{
			QualityResult.Issues.Add(Issue);
			if (Issue.Severity == TEXT("error"))   ++QualityResult.TotalErrors;
			if (Issue.Severity == TEXT("warning")) ++QualityResult.TotalWarnings;
		}
	}
	QualityResult.TotalIssues = QualityResult.Issues.Num();

	// Refresh asset list view if naming items were added
	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}

	// Quality issues → code validator panel.
	// bBlueprintScanActive=true: REPLACE the list so only BP issues are shown.
	// bBlueprintScanActive=false (legacy path): merge with existing C++ results.
	HandleValidateResult(QualityResult, /*bMerge=*/!bBlueprintScanActive, /*bIsBPScan=*/bBlueprintScanActive);
}

void SShintToolsPanel::HandleValidateResult(const FShintValidateResult& Result, bool bMerge, bool bIsBPScan)
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
		// Bump generation so any in-flight async build check is discarded — its
		// BUILD001 errors belong to the previous set of files, not this fresh scan.
		++ScanGeneration;
	}

	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(LastCodeResult, bIsBPScan);
	RefreshCodeStats();
}

void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration)
{
	SetCodeState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: %d fix(es) applied, %d skipped."),
			Result.TotalFixesApplied, Result.TotalFixesSkipped);

		// Record fingerprints so these issues are suppressed on any future
		// incremental re-scan within this session.
		for (const FShintCodeIssue& I : PendingCodeFixes)
		{
			AppliedFixFingerprints.Add(
				FString::Printf(TEXT("%s:%d:%s"), *I.FilePath, I.Line, *I.RuleId));
		}
		PendingCodeFixes.Empty();

		// Clear visible list FIRST so Slate never touches stale pointers
		CodeIssueItems.Reset();
		if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

		// Now safe to remove from backing data
		AllCodeItems.RemoveAll([](const FShintIssueItemPtr& I) {
			return I->bChecked && I->bIsAutoFixable
				&& (!I->FixSuggestion.IsEmpty() || I->bIsBlueprint);
		});

		// ── Inject compile errors from the incremental build check ──────────────
		// Only inject if no new scan has been triggered since the fix was applied.
		// A changed ScanGeneration means the user already launched a fresh scan
		// that wiped AllCodeItems — stale BUILD001 items must not be re-added.
		if (Result.bHasCompileErrors && Result.CompileErrors.Num() > 0
			&& FixGeneration == ScanGeneration)
		{
			UE_LOG(LogShintTools, Warning,
				TEXT("OnCodeFixComplete: %d compile error(s) injected into panel"),
				Result.CompileErrors.Num());

			for (const FShintCompileError& CE : Result.CompileErrors)
			{
				FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
				Item->RuleId         = TEXT("BUILD001");
				// Compile errors are always critical — they block compilation.
				// "critical" maps to the Critical filter chip in the panel.
				Item->Severity       = TEXT("critical");
				Item->Message        = CE.Code.IsEmpty()
					? CE.Message
					: FString::Printf(TEXT("[%s] %s"), *CE.Code, *CE.Message);
				Item->FilePath       = CE.FilePath;
				Item->FileName       = CE.FileName;
				Item->Line           = CE.Line;
				Item->Snippet        = TEXT("");
				Item->FixSuggestion  = TEXT("");
				Item->bIsAutoFixable = false;
				Item->bChecked       = false;
				Item->Category       = TEXT("Build");
				Item->OriginalIndex  = AllCodeItems.Num();
				AllCodeItems.Add(MoveTemp(Item));
			}
		}
		else if (Result.bHasCompileErrors && FixGeneration != ScanGeneration)
		{
			UE_LOG(LogShintTools, Log,
				TEXT("OnCodeFixComplete: build errors discarded — scan generation changed (fix=%u current=%u)"),
				FixGeneration, ScanGeneration);
		}

		// Re-populate visible list with updated data
		ApplyCodeFilter();
		RefreshCodeStats();

		// If all items are now gone, show the "all resolved" state immediately
		if (AllCodeItems.IsEmpty())
		{
			if (CodeEmptyText.IsValid())
				CodeEmptyText->SetText(LOCTEXT("CVAllFixed",
					"✓  All issues resolved — click 'Scan' again to do a full re-scan."));
			if (CodeEmptyState.IsValid())
				CodeEmptyState->SetVisibility(EVisibility::Visible);
		}
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
	PopulateAssetIssueList(Result);
	RefreshAssetStats();

	// Chain a BP validation pass to pick up BPB001 (naming violations).
	// Results go ONLY to the asset naming panel — code validator is not touched.
	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintNamingScanComplete));
}

void SShintToolsPanel::OnAssetScanFromBPComplete(const FShintAssetScanResult& Result)
{
	// Asset scan triggered by "Scan Blueprints":
	//   • populate the asset list from full scan results
	//   • auto-set filter to Blueprints so only BP naming violations are visible
	//   • do NOT chain another ValidateBlueprints call — BP code scan is already running
	if (!Result.bSuccess) { SetAssetState(EModuleState::Error); return; }
	LastAssetResult = Result;
	PopulateAssetIssueList(Result);

	CurrentAssetTypeFilter = EAssetTypeFilter::Blueprints;
	ApplyAssetFilter();
	RefreshAssetStats();
}

void SShintToolsPanel::OnBlueprintNamingScanComplete(const FShintValidateResult& Result)
{
	// Extract only BPB001 (wrong/missing BP_ prefix) and add to asset panel.
	// Every other BP issue is silently discarded — code validator stays untouched.
	int32 NamingRouted = 0;
	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		if (Issue.RuleId != TEXT("BPB001")) continue;

		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath     = Issue.FilePath;
		Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
		Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
		Item->Reason        = Issue.Message;
		Item->AssetType     = TEXT("Blueprint");
		Item->bChecked      = true;
		Item->OriginalIndex = AllAssetItems.Num();
		AllAssetItems.Add(MoveTemp(Item));
		++NamingRouted;
	}

	SetAssetState(EModuleState::Done);

	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}
}

void SShintToolsPanel::OnAssetFixComplete(const FShintAssetFixResult& Result)
{
	SetAssetState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log, TEXT("AssetFix: %d asset(s) renamed."), Result.AssetsRenamed);

		// Clear both backing store and visible list
		AllAssetItems.Reset();
		AssetIssueItems.Reset();
		if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
		RefreshAssetStats();
		RefreshApplyAssetLabel();

		// Track that fixes were applied; show "all resolved" message
		++AssetFixesApplied;
		if (AssetEmptyText.IsValid())
			AssetEmptyText->SetText(LOCTEXT("ANBAllFixed",
				"✓  All violations resolved — click 'Scan' again to do a full re-scan."));
		if (AssetEmptyState.IsValid())
			AssetEmptyState->SetVisibility(EVisibility::Visible);
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

void SShintToolsPanel::PopulateCodeIssueList(const FShintValidateResult& Result, bool bIsBPScan)
{
	const double PopStart = FPlatformTime::Seconds();
	// Clear visible list FIRST so Slate never references stale items during a paint tick
	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	AllCodeItems.Reset();
	AllCodeItems.Reserve(Result.Issues.Num());

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintCodeIssue& Src = Result.Issues[i];

		// Skip issues that were already fixed this session
		const FString Fingerprint = FString::Printf(
			TEXT("%s:%d:%s"), *Src.FilePath, Src.Line, *Src.RuleId);
		if (AppliedFixFingerprints.Contains(Fingerprint))
			continue;

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
		Item->bIsBlueprint   = bIsBPScan;
		Item->OriginalIndex  = i;
		Item->Class            = Src.Class;
		Item->Category         = Src.Category;
		Item->Graph            = Src.Graph;
		Item->FileContent      = Src.FileContent;
		Item->ContextBefore    = Src.ContextBefore;
		Item->ContextAfter     = Src.ContextAfter;
		Item->ContextLineStart = Src.ContextLineStart;
		AllCodeItems.Add(MoveTemp(Item));
	}

	ApplyCodeFilter();

	const bool bCodeEmpty = AllCodeItems.IsEmpty();
	if (CodeEmptyState.IsValid())
		CodeEmptyState->SetVisibility(bCodeEmpty ? EVisibility::Visible : EVisibility::Collapsed);
	if (bCodeEmpty && CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVNoIssues", "✓  No issues found in your project."));

	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(!AllCodeItems.IsEmpty());
	if (SendCodeBtn.IsValid())  SendCodeBtn->SetEnabled(true);
	RefreshApplyCodeLabel();

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] PopulateCodeIssueList: %.3f s, %d items (filtered from %d)"),
		FPlatformTime::Seconds() - PopStart, AllCodeItems.Num(), Result.Issues.Num());
}

void SShintToolsPanel::ApplyCodeFilter()
{
	CodeIssueItems.Reset();
	CodeIssueItems.Reserve(AllCodeItems.Num());

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Compile errors always show regardless of active filters — they are critical
		const bool bIsBuildError = (Item->RuleId == TEXT("BUILD001"));

		// ── Code type filter (C++ vs Blueprint) ──────────────────────────────
		if (!bIsBuildError && CurrentCodeTypeFilter != ECodeTypeFilter::All)
		{
			if (CurrentCodeTypeFilter == ECodeTypeFilter::CppOnly        &&  Item->bIsBlueprint) continue;
			if (CurrentCodeTypeFilter == ECodeTypeFilter::BlueprintsOnly && !Item->bIsBlueprint) continue;
		}

		// ── Fixable filter ────────────────────────────────────────────────────
		if (!bIsBuildError && CurrentFilter == EIssueFilter::FixableOnly && !Item->bIsAutoFixable)
			continue;

		// ── Severity filter ───────────────────────────────────────────────────
		if (!bIsBuildError && CurrentSeverityFilter != EIssueSeverityFilter::All)
		{
			const FString SevLower = Item->Severity.ToLower();
			bool bSevMatch = false;
			switch (CurrentSeverityFilter)
			{
			case EIssueSeverityFilter::Critical: bSevMatch = (SevLower == TEXT("critical")); break;
			case EIssueSeverityFilter::Error:    bSevMatch = (SevLower == TEXT("error"));    break;
			case EIssueSeverityFilter::Warning:  bSevMatch = (SevLower == TEXT("warning"));  break;
			case EIssueSeverityFilter::Info:     bSevMatch = (SevLower == TEXT("info"));     break;
			default: bSevMatch = true; break;
			}
			if (!bSevMatch) continue;
		}

		// ── Category filter ───────────────────────────────────────────────────
		if (!bIsBuildError && CurrentCategoryFilter != EIssueCategoryFilter::All)
		{
			const FString CatLower = Item->Category.ToLower();
			bool bCatMatch = false;
			switch (CurrentCategoryFilter)
			{
			case EIssueCategoryFilter::Performance:
				bCatMatch = CatLower.Contains(TEXT("performance")); break;
			case EIssueCategoryFilter::BestPractices:
				bCatMatch = CatLower.Contains(TEXT("best")) || CatLower.Contains(TEXT("practice")); break;
			case EIssueCategoryFilter::Security:
				bCatMatch = CatLower.Contains(TEXT("security")); break;
			case EIssueCategoryFilter::Maintainability:
				bCatMatch = CatLower.Contains(TEXT("maintain")); break;
			default: bCatMatch = true; break;
			}
			if (!bCatMatch) continue;
		}

		CodeIssueItems.Add(Item);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();
}

void SShintToolsPanel::ApplyAssetFilter()
{
	AssetIssueItems.Reset();
	AssetIssueItems.Reserve(AllAssetItems.Num());

	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		if (CurrentAssetTypeFilter != EAssetTypeFilter::All)
		{
			const FString TypeLower = Item->AssetType.ToLower();
			bool bTypeMatch = false;
			switch (CurrentAssetTypeFilter)
			{
			case EAssetTypeFilter::Materials:  bTypeMatch = TypeLower.Contains(TEXT("material"));  break;
			case EAssetTypeFilter::Textures:   bTypeMatch = TypeLower.Contains(TEXT("texture"));   break;
			case EAssetTypeFilter::Meshes:     bTypeMatch = TypeLower.Contains(TEXT("mesh"));      break;
			case EAssetTypeFilter::Blueprints: bTypeMatch = TypeLower.Contains(TEXT("blueprint")); break;
			case EAssetTypeFilter::VFX:        bTypeMatch = TypeLower.Contains(TEXT("niagara")) || TypeLower.Contains(TEXT("particle")); break;
			case EAssetTypeFilter::Audio:      bTypeMatch = TypeLower.Contains(TEXT("sound")) || TypeLower.Contains(TEXT("audio")); break;
			case EAssetTypeFilter::Animations: bTypeMatch = TypeLower.Contains(TEXT("anim"));      break;
			case EAssetTypeFilter::Data:       bTypeMatch = TypeLower.Contains(TEXT("data")) || TypeLower.Contains(TEXT("table")) || TypeLower.Contains(TEXT("curve")); break;
			default: bTypeMatch = true; break;
			}
			if (!bTypeMatch) continue;
		}
		AssetIssueItems.Add(Item);
	}

	if (AssetIssueListView.IsValid()) AssetIssueListView->RequestListRefresh();

	const bool bHasItems = !AssetIssueItems.IsEmpty();
	if (AssetEmptyState.IsValid())
		AssetEmptyState->SetVisibility(bHasItems ? EVisibility::Collapsed : EVisibility::Visible);
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(bHasItems);
	if (SendAssetBtn.IsValid())  SendAssetBtn->SetEnabled(!AllAssetItems.IsEmpty());
	RefreshApplyAssetLabel();
}

void SShintToolsPanel::PopulateAssetIssueList(const FShintAssetScanResult& Result)
{
	const double PopStart = FPlatformTime::Seconds();
	AllAssetItems.Reset();
	AllAssetItems.Reserve(Result.Issues.Num());

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
		AllAssetItems.Add(MoveTemp(Item));
	}

	ApplyAssetFilter();
	if (SendAssetBtn.IsValid()) SendAssetBtn->SetEnabled(true);

	// If the scan returned no violations, show a clear success message
	if (AllAssetItems.IsEmpty() && AssetEmptyText.IsValid())
		AssetEmptyText->SetText(LOCTEXT("ANBNoIssues", "✓  No naming violations found."));

	UE_LOG(LogShintTools, Log,
		TEXT("[BENCH] PopulateAssetIssueList: %.3f s, %d items"),
		FPlatformTime::Seconds() - PopStart, AllAssetItems.Num());
}

void SShintToolsPanel::RefreshCodeStats()
{
	if (CodeFiles_Label.IsValid())    CodeFiles_Label->SetText(FText::FromString(FmtN(LastCodeResult.FilesScanned)));
	if (CodeErrors_Label.IsValid())   CodeErrors_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalErrors)));
	if (CodeWarnings_Label.IsValid()) CodeWarnings_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalWarnings)));
}

void SShintToolsPanel::RefreshAssetStats()
{
	// Drive counters from the backing store so stats reflect total, not the filtered view.
	const int32 Total = AllAssetItems.Num();
	if (AssetTotal_Label.IsValid())   AssetTotal_Label->SetText(FText::FromString(FmtN(Total)));
	if (AssetInvalid_Label.IsValid()) AssetInvalid_Label->SetText(FText::FromString(FmtN(Total)));
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
