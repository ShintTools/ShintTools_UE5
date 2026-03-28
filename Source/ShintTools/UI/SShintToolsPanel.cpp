// Copyright ShintTools. All Rights Reserved.
//
// v9 — Compilation fixes:
//   [1] SShintToolsPanel.h now includes ShintCoreClient.h directly.
//       All struct types (FShintRaw, FValidateResult, FFixResult, FAssetScan,
//       FAssetFix, FWebResult) are fully defined before the class body.
//       No more "ambiguous symbol FFixResult" or "cannot resolve symbol R".
//
//   [2] Removed the broken `using ValidateResult = FValidateResult` alias.
//       The member `LastCode` is now typed correctly as `FValidateResult`.
//
//   [3] Removed `.ItemHeight()` calls — deprecated in UE5.4+
//       (only valid for tile mode, not for standard list views).
//
//   [4] AssetToolsModule.h / IAssetTools.h now included in the .h so the .cpp
//       doesn't need to re-include them; IAssetToolsModule resolves cleanly.
//
//   [5] All lambda parameters renamed from single-letter `R` to `Raw` /
//       `Res` / `Out` to avoid potential macro-name shadowing.

#include "SShintToolsPanel.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

// Slate layout
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
// Styling
#include "Styling/AppStyle.h"
// Misc
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "ShintTools"

// ─────────────────────────────────────────────────────────────────────────────
// Brush cache helper
// ─────────────────────────────────────────────────────────────────────────────

static const FSlateBrush* BoxBrush(FLinearColor C, float Radius = 0.f)
{
	static TMap<uint32, FSlateBrush> Cache;
	const uint32 Key =
		(uint32)(C.R * 255) |
		((uint32)(C.G * 255) << 8)  |
		((uint32)(C.B * 255) << 16) |
		((uint32)(Radius * 10) << 24);

	if (!Cache.Contains(Key))
	{
		FSlateBrush B;
		B.TintColor = FSlateColor(C);
		B.DrawAs    = Radius > 0.f ? ESlateBrushDrawType::RoundedBox : ESlateBrushDrawType::Box;
		if (Radius > 0.f)
		{
			B.OutlineSettings.CornerRadii  = FVector4(Radius, Radius, Radius, Radius);
			B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		}
		Cache.Add(Key, B);
	}
	return &Cache[Key];
}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::Div()
{
	return SNew(SBox).HeightOverride(1.f)
		[SNew(SBorder).BorderImage(BoxBrush(Brd())).Padding(0.f)];
}

FString SShintToolsPanel::N(int32 V)
{
	return V < 0 ? TEXT("—") : FString::Printf(TEXT("%d"), V);
}

static TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& Out, const FText& Cap, const FLinearColor& Clr)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SAssignNew(Out, STextBlock)
			.Text(FText::FromString(TEXT("—")))
			.Font(SShintToolsPanel::FSN())
			.ColorAndOpacity(FSlateColor(Clr))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Cap)
			.Font(SShintToolsPanel::FSC())
			.ColorAndOpacity(FSlateColor(SShintToolsPanel::Dim()))
		];
}

// Shared button builder — guarantees identical padding on ALL action buttons
static TSharedRef<SButton> MakeBtn(
	const FText& Label, const FLinearColor& LabelColor, FOnClicked Callback,
	TSharedPtr<STextBlock>* OutLabel = nullptr)
{
	TSharedPtr<STextBlock> Txt;
	TSharedRef<SButton> Btn = SNew(SButton)
		.ContentPadding(FMargin(14.f, 7.f))
		.OnClicked(Callback)
		[
			SAssignNew(Txt, STextBlock)
			.Text(Label)
			.Font(SShintToolsPanel::FS())
			.ColorAndOpacity(FSlateColor(LabelColor))
		];
	if (OutLabel) *OutLabel = Txt;
	return Btn;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct / Destruct
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::Construct(const FArguments&)
{
	Client = MakeShared<FShintClient>();
	Proc   = MakeShared<FCoreProcessManager>();

	ChildSlot
	[
		SNew(SBorder).BorderImage(BoxBrush(BG())).Padding(0.f)
		[
			SNew(SScrollBox).Orientation(Orient_Vertical)
			+ SScrollBox::Slot().Padding(0.f) [BuildHeader()]
			+ SScrollBox::Slot().Padding(0.f) [BuildCfg()]
			+ SScrollBox::Slot().Padding(0.f) [BuildStatus()]
			+ SScrollBox::Slot().Padding(0.f) [BuildCode()]
			+ SScrollBox::Slot().Padding(0.f) [BuildAssets()]
		]
	];
}

SShintToolsPanel::~SShintToolsPanel() {}

// ─────────────────────────────────────────────────────────────────────────────
// Header
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildHeader()
{
	return SNew(SBorder).BorderImage(BoxBrush(BG())).Padding(FMargin(20.f, 18.f, 20.f, 12.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Brand", "ShintTools"))
			.Font(FT())
			.ColorAndOpacity(FSlateColor(White()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Sub", "Automation and optimization tools for Unreal Engine and Unity"))
			.Font(FL())
			.ColorAndOpacity(FSlateColor(Gray()))
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Dashboard Config
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCfg()
{
	const FShintCfg& Cfg = Client->Cfg();

	// Helper lambda to build a labelled text box row
	auto MakeRow = [this](const FText& Label, const FString& Value,
		TSharedPtr<SEditableTextBox>& Box, bool bPassword = false) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(98.f)
				[
					SNew(STextBlock).Text(Label).Font(FL()).ColorAndOpacity(FSlateColor(Gray()))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SAssignNew(Box, SEditableTextBox)
				.Text(FText::FromString(Value))
				.IsPassword(bPassword)
				.Font(FM())
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { FlushCfg(); })
			];
	};

	return SNew(SBorder).BorderImage(BoxBrush(Surf())).Padding(FMargin(18.f, 10.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CfgTitle", "Dashboard Configuration"))
			.Font(FH())
			.ColorAndOpacity(FSlateColor(White()))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
		[MakeRow(LOCTEXT("ProjId",  "Project ID"),   Cfg.ProjectId, BxProjId)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
		[MakeRow(LOCTEXT("ApiKey",  "API Key"),       Cfg.Key,       BxKey, true)]
		+ SVerticalBox::Slot().AutoHeight()
		[MakeRow(LOCTEXT("ProjName","Project Name"),  Cfg.Name,      BxName)]
	];
}

void SShintToolsPanel::FlushCfg()
{
	FShintCfg& Cfg = Client->Cfg();
	if (BxProjId.IsValid()) Cfg.ProjectId = BxProjId->GetText().ToString().TrimStartAndEnd();
	if (BxKey.IsValid())    Cfg.Key       = BxKey->GetText().ToString().TrimStartAndEnd();
	if (BxName.IsValid())   Cfg.Name      = BxName->GetText().ToString().TrimStartAndEnd();
}

// ─────────────────────────────────────────────────────────────────────────────
// Status bar
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildStatus()
{
	return SNew(SBorder).BorderImage(BoxBrush(Surf())).Padding(FMargin(18.f, 10.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(FS())
			.ColorAndOpacity(TAttribute<FSlateColor>::Create(
				TAttribute<FSlateColor>::FGetter::CreateSP(this, &SShintToolsPanel::StatusColor)))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("CE", "CORE ENGINE")).Font(FL()).ColorAndOpacity(FSlateColor(Dim()))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(
				TAttribute<FText>::FGetter::CreateSP(this, &SShintToolsPanel::StatusText)))
			.Font(FS()).ColorAndOpacity(FSlateColor(White()))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton).ButtonColorAndOpacity(BG()).ContentPadding(FMargin(14.f, 7.f))
			.OnClicked(this, &SShintToolsPanel::OnHealth)
			[
				SNew(STextBlock).Text(LOCTEXT("ChkConn", "Check Connection"))
				.Font(FS()).ColorAndOpacity(FSlateColor(Blue()))
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator section
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCode()
{
	return SNew(SBorder).BorderImage(BoxBrush(BG())).Padding(FMargin(20.f, 18.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[SNew(STextBlock).Text(LOCTEXT("CVT", "CODE VALIDATOR")).Font(FH()).ColorAndOpacity(FSlateColor(White()))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[SNew(STextBlock).Text(LOCTEXT("CVS", "Analyse C++ and Blueprints · apply corrections · send to dashboard")).Font(FL()).ColorAndOpacity(FSlateColor(Gray()))]

		// Stats row
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LFiles,    LOCTEXT("F", "FILES"),    Blue())]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LErrCode,  LOCTEXT("E", "ERRORS"),   Red())]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LWarnCode, LOCTEXT("W", "WARNINGS"), Yellow())]
		]

		// Progress bar
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SBox).HeightOverride(2.f)
			[
				SAssignNew(PbCode, SProgressBar)
				.Percent(TAttribute<TOptional<float>>::Create(
					TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::CodePct)))
				.FillColorAndOpacity(FSlateColor(Blue()))
				.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
			]
		]

		// Scan buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding({8.f, 6.f})
			+ SWrapBox::Slot() [MakeBtn(LOCTEXT("ScanSrc", "⟳  Scan All Source"), White(), FOnClicked::CreateSP(this, &SShintToolsPanel::OnScanSrc))]
			+ SWrapBox::Slot() [MakeBtn(LOCTEXT("ScanBP",  "⟳  Scan Blueprints"), White(), FOnClicked::CreateSP(this, &SShintToolsPanel::OnScanBP))]
		]

		// Results list
		+ SVerticalBox::Slot().AutoHeight() [BuildCodeList()]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code results panel
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildCodeList()
{
	SAssignNew(EmptyCode, SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(56.f)
	[
		SNew(STextBlock).Text(LOCTEXT("CVEmp", "Run a scan to see results."))
		.Font(FS()).ColorAndOpacity(FSlateColor(Dim()))
	];

	// Filter button helper
	auto FB = [this](const FText& Label, EFilter Filter) -> TSharedRef<SWidget>
	{
		return SNew(SButton).ContentPadding(FMargin(9.f, 4.f))
			.OnClicked_Lambda([this, Filter]() -> FReply
			{
				Flt = Filter;
				ApplyFilter();
				return FReply::Handled();
			})
			[SNew(STextBlock).Text(Label).Font(FL()).ColorAndOpacity(FSlateColor(Gray()))];
	};

	TSharedRef<SWidget> Toolbar =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[SNew(STextBlock).Text(LOCTEXT("Res", "RESULTS")).Font(FL()).ColorAndOpacity(FSlateColor(Dim()))]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f) [FB(LOCTEXT("FAl", "All"),      EFilter::All)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f) [FB(LOCTEXT("FEr", "Errors"),   EFilter::Errors)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f) [FB(LOCTEXT("FWr", "Warnings"), EFilter::Warnings)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f) [FB(LOCTEXT("FFx", "Fixable"),  EFilter::Fixable)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton).ContentPadding(FMargin(9.f, 4.f)).OnClicked(this, &SShintToolsPanel::OnSelAll)
			[SNew(STextBlock).Text(LOCTEXT("SelA", "Select All")).Font(FL()).ColorAndOpacity(FSlateColor(Blue()))]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton).ContentPadding(FMargin(9.f, 4.f)).OnClicked(this, &SShintToolsPanel::OnDeselAll)
			[SNew(STextBlock).Text(LOCTEXT("DesA", "Deselect All")).Font(FL()).ColorAndOpacity(FSlateColor(Gray()))]
		];

	TSharedRef<SWidget> ListContent =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f) [Toolbar]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f) [Div()]
		// NO ItemHeight (deprecated) — NO MaxDesiredHeight — outer SScrollBox scrolls
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(CodeList, SListView<FCodePtr>)
			.ListItemsSource(&ViewCode)
			.OnGenerateRow(this, &SShintToolsPanel::CodeRow)
			.SelectionMode(ESelectionMode::None)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f) [Div()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding({8.f, 6.f})
			+ SWrapBox::Slot()
			[
				SAssignNew(BtnApply, SButton).IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnApply)
				[
					SAssignNew(LApply, STextBlock)
					.Text(LOCTEXT("ApplyC", "✓  Apply Corrections (0)"))
					.Font(FS()).ColorAndOpacity(FSlateColor(Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(BtnCpp, SButton).IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnPushCpp)
				[SNew(STextBlock).Text(LOCTEXT("PushCpp", "↑  C++ → Dashboard")).Font(FS()).ColorAndOpacity(FSlateColor(Blue()))]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(BtnBp, SButton).IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnPushBP)
				[SNew(STextBlock).Text(LOCTEXT("PushBP", "↑  BP → Dashboard")).Font(FS()).ColorAndOpacity(FSlateColor(Blue()))]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [EmptyCode.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight() [ListContent];
}

// ─────────────────────────────────────────────────────────────────────────────
// Code issue row
//
// CHECKBOX FIX:
//   IsChecked reads Item->bChecked AT CONSTRUCTION TIME.
//   RebuildList() (called by SelectAll/Filter) destroys+regenerates all rows,
//   so each new widget reads the fresh bChecked value.
//   OnCheckStateChanged captures TSharedPtr — safe after row recycling.
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<ITableRow> SShintToolsPanel::CodeRow(
	FCodePtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor SevColor = (Item->Sev == TEXT("error")) ? Red() : Yellow();
	const FLinearColor RowBG    = (Item->Idx % 2 == 0) ? EvenRow() : OddRow();
	const FString      Badge    = Item->bFixable ? TEXT("  ✦ AUTO") : TEXT("");
	const bool         bChk     = Item->bChecked;   // read fresh at construction

	return SNew(STableRow<FCodePtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(BoxBrush(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)

				// Checkbox — static initial value, OnChanged writes back via TSharedPtr
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 3.f, 10.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(bChk ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State)
					{
						Item->bChecked = (State == ECheckBoxState::Checked);
						RefApplyBtn();
					})
				]

				// Content
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)

					// Row 1: severity dot + rule ID + filename:line + AUTO badge
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 5.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(FS()).ColorAndOpacity(FSlateColor(SevColor))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(Item->Rule)).Font(FB()).ColorAndOpacity(FSlateColor(White()))]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s : %d"), *Item->FileName, Item->Line))).Font(FM()).ColorAndOpacity(FSlateColor(Gray()))]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[SNew(STextBlock).Text(FText::FromString(Badge)).Font(FL()).ColorAndOpacity(FSlateColor(Blue()))]
					]

					// Row 2: message
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
					[
						SNew(STextBlock).Text(FText::FromString(Item->Msg))
						.Font(FS()).ColorAndOpacity(FSlateColor(White())).AutoWrapText(true)
					]

					// Row 3: snippet → fix suggestion
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.Visibility(Item->Snippet.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.BorderImage(BoxBrush(FLinearColor(0.055f, 0.055f, 0.055f, 1.f)))
						.Padding(FMargin(8.f, 5.f))
						[
							SNew(SVerticalBox)
							// Current code (red)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
								[SNew(STextBlock).Text(FText::FromString(TEXT("▸"))).Font(FM()).ColorAndOpacity(FSlateColor(Red()))]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[SNew(STextBlock).Text(FText::FromString(Item->Snippet)).Font(FM()).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.48f, 0.48f, 1.f))).AutoWrapText(true)]
							]
							// Fix suggestion (green)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
							[
								SNew(SHorizontalBox)
								.Visibility(Item->FixHint.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
								+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
								[SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(FM()).ColorAndOpacity(FSlateColor(Green()))]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[SNew(STextBlock).Text(FText::FromString(Item->FixHint)).Font(FM()).ColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.9f, 0.48f, 1.f))).AutoWrapText(true)]
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

TSharedRef<SWidget> SShintToolsPanel::BuildAssets()
{
	return SNew(SBorder).BorderImage(BoxBrush(BG())).Padding(FMargin(20.f, 18.f))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
		[SNew(STextBlock).Text(LOCTEXT("ANT", "ASSET NAMING BOT")).Font(FH()).ColorAndOpacity(FSlateColor(White()))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[SNew(STextBlock).Text(LOCTEXT("ANS", "Scan project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard")).Font(FL()).ColorAndOpacity(FSlateColor(Gray()))]

		// Stats row
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LAssets,  LOCTEXT("ANT2", "ASSETS"),   Blue())]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LInvalid, LOCTEXT("ANI",  "INVALID"),   Red())]
			+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center) [StatBadge(LTime,    LOCTEXT("ANTm", "TIME (s)"),  Gray())]
		]

		// Progress bar
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			SNew(SBox).HeightOverride(2.f)
			[
				SAssignNew(PbAsset, SProgressBar)
				.Percent(TAttribute<TOptional<float>>::Create(
					TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::AssetPct)))
				.FillColorAndOpacity(FSlateColor(Blue()))
				.BackgroundImage(FAppStyle::GetBrush("ProgressBar.Background"))
			]
		]

		// Scan button — same MakeBtn helper → identical size as Code Validator buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
		[
			MakeBtn(LOCTEXT("ScanA", "⟳  Scan All Assets"), White(),
				FOnClicked::CreateSP(this, &SShintToolsPanel::OnScanAssets))
		]

		+ SVerticalBox::Slot().AutoHeight() [BuildAssetList()]
	];
}

TSharedRef<SWidget> SShintToolsPanel::BuildAssetList()
{
	SAssignNew(EmptyAsset, SBox).HAlign(HAlign_Center).VAlign(VAlign_Center).MinDesiredHeight(56.f)
	[
		SNew(STextBlock).Text(LOCTEXT("ANEmp", "Run a scan to see naming violations."))
		.Font(FS()).ColorAndOpacity(FSlateColor(Dim()))
	];

	TSharedRef<SWidget> ListContent =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[SNew(STextBlock).Text(LOCTEXT("ANRes", "RESULTS")).Font(FL()).ColorAndOpacity(FSlateColor(Dim()))]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton).ContentPadding(FMargin(9.f, 4.f))
				.OnClicked(this, &SShintToolsPanel::OnSelAllAssets)
				[SNew(STextBlock).Text(LOCTEXT("ANSel", "Select All")).Font(FL()).ColorAndOpacity(FSlateColor(Blue()))]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f) [Div()]
		// NO ItemHeight (deprecated)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(AssetList, SListView<FAssetPtr>)
			.ListItemsSource(&AllAssets)
			.OnGenerateRow(this, &SShintToolsPanel::AssetRow)
			.SelectionMode(ESelectionMode::None)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f) [Div()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding({8.f, 6.f})
			+ SWrapBox::Slot()
			[
				SAssignNew(BtnApplyAsset, SButton).IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnApplyAssets)
				[
					SAssignNew(LApplyAsset, STextBlock)
					.Text(LOCTEXT("ApplyA", "✓  Apply Corrections (0)"))
					.Font(FS()).ColorAndOpacity(FSlateColor(Green()))
				]
			]
			+ SWrapBox::Slot()
			[
				SAssignNew(BtnPushAsset, SButton).IsEnabled(false).ContentPadding(FMargin(14.f, 7.f))
				.OnClicked(this, &SShintToolsPanel::OnPushAssets)
				[SNew(STextBlock).Text(LOCTEXT("PushA", "↑  Send to Dashboard")).Font(FS()).ColorAndOpacity(FSlateColor(Blue()))]
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight() [EmptyAsset.ToSharedRef()]
		+ SVerticalBox::Slot().AutoHeight() [ListContent];
}

TSharedRef<ITableRow> SShintToolsPanel::AssetRow(
	FAssetPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FLinearColor RowBG = (Item->Idx % 2 == 0) ? EvenRow() : OddRow();
	const bool         bChk  = Item->bChecked;

	return SNew(STableRow<FAssetPtr>, Owner)
		.Style(FAppStyle::Get(), "TableView.Row").Padding(0.f)
		[
			SNew(SBorder).BorderImage(BoxBrush(RowBG)).Padding(FMargin(12.f, 9.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked(bChk ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
					.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State)
					{
						Item->bChecked = (State == ECheckBoxState::Checked);
						RefAssetApplyBtn();
					})
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("⚠"))).Font(FS()).ColorAndOpacity(FSlateColor(Yellow()))]
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
						[SNew(STextBlock).Text(FText::FromString(Item->Type)).Font(FB()).ColorAndOpacity(FSlateColor(White()))]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[SNew(STextBlock).Text(FText::FromString(Item->Path)).Font(FM()).ColorAndOpacity(FSlateColor(Gray()))]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(BoxBrush(FLinearColor(0.055f, 0.055f, 0.055f, 1.f))).Padding(FMargin(8.f, 4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[SNew(STextBlock).Text(FText::FromString(TEXT("▸"))).Font(FM()).ColorAndOpacity(FSlateColor(Red()))]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
							[SNew(STextBlock).Text(FText::FromString(Item->Current)).Font(FM()).ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.45f, 0.45f, 1.f)))]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[SNew(STextBlock).Text(FText::FromString(TEXT("→"))).Font(FM()).ColorAndOpacity(FSlateColor(Green()))]
							+ SHorizontalBox::Slot().FillWidth(1.f)
							[SNew(STextBlock).Text(FText::FromString(Item->Suggested)).Font(FM()).ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.9f, 0.45f, 1.f)))]
						]
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Button handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnHealth()
{
	SetSt(EStatus::Checking);
	Client->Health(FOnRaw::CreateSP(this, &SShintToolsPanel::OnHealthDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanSrc()
{
	SetCode(EModule::Running);
	AllCode.Empty();
	ViewCode.Empty();
	Client->ScanProject(FPaths::GameSourceDir(),
		FOnValidate::CreateSP(this, &SShintToolsPanel::OnCodeDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanBP()
{
	SetCode(EModule::Running);
	Client->ScanBlueprints(FPaths::ProjectContentDir(),
		FOnValidate::CreateSP(this, &SShintToolsPanel::OnBpDone));
	return FReply::Handled();
}

// Select All / Deselect All — RebuildList forces row regeneration with fresh bChecked
FReply SShintToolsPanel::OnSelAll()
{
	for (const FCodePtr& Item : AllCode)
		if (Item->bFixable) Item->bChecked = true;
	if (CodeList.IsValid()) CodeList->RebuildList();
	RefApplyBtn();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnDeselAll()
{
	for (const FCodePtr& Item : AllCode) Item->bChecked = false;
	if (CodeList.IsValid()) CodeList->RebuildList();
	RefApplyBtn();
	return FReply::Handled();
}

// Apply Corrections — reads files from disk, POSTs to /validate/fix, writes back
FReply SShintToolsPanel::OnApply()
{
	// Iterate ALL issues (not just the filtered view)
	TMap<FString, TArray<FAcceptedFix>> ByFile;
	for (const FCodePtr& Item : AllCode)
	{
		if (!Item->bChecked || !Item->bFixable || Item->File.IsEmpty()) continue;
		FAcceptedFix Fix;
		Fix.RuleId   = Item->Rule;
		Fix.Line     = Item->Line;
		Fix.FilePath = Item->File;
		ByFile.FindOrAdd(Item->File).Add(Fix);
	}

	if (ByFile.IsEmpty())
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("ShintTools: Nothing to apply — check fixable (✦ AUTO) issues or click Select All."));
		return FReply::Handled();
	}

	TArray<FFixFileRequest> Requests;
	for (auto& KV : ByFile)
	{
		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *KV.Key))
		{
			UE_LOG(LogShintTools, Error, TEXT("ShintTools: Cannot read file: %s"), *KV.Key);
			continue;
		}
		FFixFileRequest Req;
		Req.FilePath = KV.Key;
		Req.Source   = Source;
		Req.Fixes    = KV.Value;
		Requests.Add(MoveTemp(Req));
		UE_LOG(LogShintTools, Log,
			TEXT("ShintTools: Queuing %d fix(es) for %s"),
			KV.Value.Num(), *FPaths::GetCleanFilename(KV.Key));
	}

	if (Requests.IsEmpty())
	{
		UE_LOG(LogShintTools, Error, TEXT("ShintTools: Could not read any selected files."));
		return FReply::Handled();
	}

	SetCode(EModule::Running);
	Client->ApplyFixes(Requests, FOnFix::CreateSP(this, &SShintToolsPanel::OnFixDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnPushCpp()
{
	FlushCfg();
	Client->PushCode(LastCode, FOnWeb::CreateSP(this, &SShintToolsPanel::OnCppPushDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnPushBP()
{
	FlushCfg();
	Client->PushBlueprints(Client->BpCache(), FOnWeb::CreateSP(this, &SShintToolsPanel::OnBpPushDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnScanAssets()
{
	SetAsset(EModule::Running);
	AllAssets.Empty();
	Client->ScanAssets(FPaths::ProjectContentDir(),
		FOnAssetScan::CreateSP(this, &SShintToolsPanel::OnAssetsDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnSelAllAssets()
{
	for (const FAssetPtr& Item : AllAssets) Item->bChecked = true;
	if (AssetList.IsValid()) AssetList->RebuildList();
	RefAssetApplyBtn();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnApplyAssets()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
	{
		UE_LOG(LogShintTools, Error, TEXT("ShintTools: AssetTools module unavailable."));
		return FReply::Handled();
	}

	FAssetToolsModule& ATModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = ATModule.Get();

	TArray<FAssetRenameData> RenameData;
	TArray<FAssetIssue>      ForServer;

	for (const FAssetPtr& Item : AllAssets)
	{
		if (!Item->bChecked) continue;
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Item->Path);
		if (!Asset)
		{
			UE_LOG(LogShintTools, Warning, TEXT("ShintTools: Cannot load asset: %s"), *Item->Path);
			continue;
		}
		RenameData.Add(FAssetRenameData(Asset, FPaths::GetPath(Item->Path), Item->Suggested));
		FAssetIssue Issue;
		Issue.Path      = Item->Path;
		Issue.Current   = Item->Current;
		Issue.Suggested = Item->Suggested;
		Issue.Type      = Item->Type;
		ForServer.Add(Issue);
	}

	if (RenameData.IsEmpty())
	{
		UE_LOG(LogShintTools, Warning, TEXT("ShintTools: No checked assets to rename."));
		return FReply::Handled();
	}

	AssetTools.RenameAssets(RenameData);
	UE_LOG(LogShintTools, Log, TEXT("ShintTools: ✔ Renamed %d asset(s)."), RenameData.Num());

	Client->ReportFixes(ForServer, FOnAssetFix::CreateSP(this, &SShintToolsPanel::OnAssetFixDone));
	return FReply::Handled();
}

FReply SShintToolsPanel::OnPushAssets()
{
	FlushCfg();
	Client->PushAssets(LastAsset, FOnWeb::CreateSP(this, &SShintToolsPanel::OnAssetPushDone));
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP callbacks — parameters named Result/Out to avoid reserved-name conflicts
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnHealthDone(const FShintRaw& Result)
{
	SetSt(Result.bOk ? EStatus::Online : EStatus::Offline);
	if (!Result.bOk)
		UE_LOG(LogShintTools, Warning, TEXT("ShintTools: Core OFFLINE — %s"), *Result.Err);
}

void SShintToolsPanel::OnCodeDone(const FValidateResult& Result)
{
	if (!Result.bOk)
	{
		SetCode(EModule::Err);
		UE_LOG(LogShintTools, Error, TEXT("ShintTools: C++ scan failed — %s"), *Result.Err);
		return;
	}

	// Clear and rebuild AllCode from C++ scan results
	AllCode.Empty();
	for (int32 i = 0; i < Result.List.Num(); ++i)
	{
		const FShintIssue& Src = Result.List[i];
		FCodePtr Item = MakeShared<FCodeItem>();
		Item->Rule     = Src.Rule;
		Item->Sev      = Src.Sev;
		Item->Msg      = Src.Msg;
		Item->File     = Src.File;
		Item->FileName = FPaths::GetCleanFilename(Src.File);
		Item->Line     = Src.Line;
		Item->Snippet  = Src.Snippet;
		Item->FixHint  = Src.FixHint;
		Item->bFixable = Src.bFixable;
		Item->bChecked = Src.bFixable;   // pre-check auto-fixable issues
		Item->Idx      = i;
		AllCode.Add(MoveTemp(Item));
	}

	LastCode = Result;
	SetCode(EModule::Done);
	ApplyFilter();
	RefCodeStats();
	if (BtnCpp.IsValid()) BtnCpp->SetEnabled(true);
}

void SShintToolsPanel::OnBpDone(const FValidateResult& Result)
{
	if (!Result.bOk)
	{
		SetCode(EModule::Err);
		UE_LOG(LogShintTools, Error, TEXT("ShintTools: BP scan failed — %s"), *Result.Err);
		return;
	}

	// APPEND blueprint issues — do not clear C++ results
	const int32 Offset = AllCode.Num();
	for (int32 i = 0; i < Result.List.Num(); ++i)
	{
		const FShintIssue& Src = Result.List[i];
		FCodePtr Item = MakeShared<FCodeItem>();
		Item->Rule     = Src.Rule;
		Item->Sev      = Src.Sev;
		Item->Msg      = Src.Msg;
		Item->File     = Src.File;
		Item->FileName = FPaths::GetBaseFilename(Src.File);
		Item->Line     = Src.Line;
		Item->Snippet  = Src.Snippet;
		Item->FixHint  = Src.FixHint;
		Item->bFixable = Src.bFixable;
		Item->bChecked = false;
		Item->Idx      = Offset + i;
		AllCode.Add(MoveTemp(Item));
	}

	// Merge counts
	LastCode.Files  += Result.Files;
	LastCode.Issues += Result.Issues;
	LastCode.Errors += Result.Errors;
	LastCode.Warns  += Result.Warns;

	SetCode(EModule::Done);
	ApplyFilter();
	RefCodeStats();

	if (BtnBp.IsValid()) BtnBp->SetEnabled(!Client->BpCache().IsEmpty());
}

void SShintToolsPanel::OnFixDone(const FFixResult& Result)
{
	SetCode(EModule::Done);
	if (!Result.bIsSuccess)
	{
		//UE_LOG(LogShintTools, Error, TEXT("ShintTools: Fix failed — %s"), Result.Failure());
		return;
	}
	// UE_LOG(LogShintTools, Log, TEXT("ShintTools: ✔ %d applied, %d skipped."),
	// 	Result., Result.Skipped);

	// Uncheck fixed items
	for (const FCodePtr& Item : AllCode)
		if (Item->bChecked && Item->bFixable) Item->bChecked = false;

	if (CodeList.IsValid()) CodeList->RebuildList();
	RefApplyBtn();
}

/*
void SShintToolsPanel::OnCppPushDone(const FWebResult& Result)
{
	UE_LOG(LogShintTools, Result.bOk, ELogVerbosity::Log : ELogVerbosity::Warning,
		TEXT("ShintTools: C++ Dashboard %s — %s"),
		Result.bOk ? TEXT("✔") : TEXT("✘"),
		Result.bOk ? *Result.Body.Left(150) : *Result.Err);
}

void SShintToolsPanel::OnBpPushDone(const FWebResult& Result)
{
	UE_LOG(LogShintTools, Result.bOk ? ELogVerbosity::Log : ELogVerbosity::Warning,
		TEXT("ShintTools: BP Dashboard %s — %s"),
		Result.bOk ? TEXT("✔") : TEXT("✘"),
		Result.bOk ? *Result.Body.Left(150) : *Result.Err);
}
*/

void SShintToolsPanel::OnAssetsDone(const FAssetScan& Result)
{
	if (!Result.bOk)
	{
		SetAsset(EModule::Err);
		UE_LOG(LogShintTools, Error, TEXT("ShintTools: Asset scan failed — %s"), *Result.Err);
		return;
	}

	LastAsset = Result;
	AllAssets.Empty();

	for (int32 i = 0; i < Result.List.Num(); ++i)
	{
		const FAssetIssue& Src = Result.List[i];
		FAssetPtr Item = MakeShared<FAssetItem>();
		Item->Path      = Src.Path;
		Item->Current   = Src.Current;
		Item->Suggested = Src.Suggested;
		Item->Reason    = Src.Reason;
		Item->Type      = Src.Type;
		Item->bChecked  = true;
		Item->Idx       = i;
		AllAssets.Add(MoveTemp(Item));
	}

	SetAsset(EModule::Done);

	if (AssetList.IsValid()) AssetList->RebuildList();
	if (EmptyAsset.IsValid())
		EmptyAsset->SetVisibility(AllAssets.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);
	if (BtnApplyAsset.IsValid()) BtnApplyAsset->SetEnabled(!AllAssets.IsEmpty());
	if (BtnPushAsset.IsValid())  BtnPushAsset->SetEnabled(true);

	RefAssetStats();
	RefAssetApplyBtn();
}

void SShintToolsPanel::OnAssetFixDone(const FAssetFix& Result)
{
	SetAsset(EModule::Done);
	// UE_LOG(LogShintTools, Result.bOk ? ELogVerbosity::Log : ELogVerbosity::Warning,
	// 	TEXT("ShintTools: Asset fix %s (%d renamed)."),
	// 	Result.bOk ? TEXT("✔") : TEXT("✘"), Result.Renamed);
}

void SShintToolsPanel::OnAssetPushDone(const FWebResult& Result)
{
	// UE_LOG(LogShintTools, Result.bOk ? ELogVerbosity::Log : ELogVerbosity::Warning,
	// 	TEXT("ShintTools: Asset Dashboard %s — %s"),
	// 	Result.bOk ? TEXT("✔") : TEXT("✘"),
	// 	Result.bOk ? *Result.Body.Left(150) : *Result.Err);
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter + refresh
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::ApplyFilter()
{
	ViewCode.Empty();
	ViewCode.Reserve(AllCode.Num());

	for (const FCodePtr& Item : AllCode)
	{
		if (Flt == EFilter::Errors   && Item->Sev != TEXT("error"))   continue;
		if (Flt == EFilter::Warnings && Item->Sev != TEXT("warning")) continue;
		if (Flt == EFilter::Fixable  && !Item->bFixable)              continue;
		ViewCode.Add(Item);
	}

	// RebuildList: all row widgets regenerated → fresh bChecked at construction
	if (CodeList.IsValid()) CodeList->RebuildList();

	if (EmptyCode.IsValid())
		EmptyCode->SetVisibility(AllCode.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed);

	if (BtnApply.IsValid()) BtnApply->SetEnabled(false);
	RefApplyBtn();
}

void SShintToolsPanel::RefCodeStats()
{
	// Count from AllCode (includes C++ + BP)
	int32 Errs = 0, Warns = 0;
	for (const FCodePtr& Item : AllCode)
	{
		if (Item->Sev == TEXT("error")) ++Errs; else ++Warns;
	}
	if (LFiles.IsValid())    LFiles->SetText(FText::FromString(N(LastCode.Files)));
	if (LErrCode.IsValid())  LErrCode->SetText(FText::FromString(N(Errs)));
	if (LWarnCode.IsValid()) LWarnCode->SetText(FText::FromString(N(Warns)));
}

void SShintToolsPanel::RefAssetStats()
{
	if (LAssets.IsValid())  LAssets->SetText(FText::FromString(N(LastAsset.Total)));
	if (LInvalid.IsValid()) LInvalid->SetText(FText::FromString(N(LastAsset.Invalid)));
	if (LTime.IsValid())    LTime->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), LastAsset.Secs)));
}

void SShintToolsPanel::RefApplyBtn()
{
	int32 Count = 0;
	for (const FCodePtr& Item : AllCode)
		if (Item->bChecked && Item->bFixable) ++Count;

	if (LApply.IsValid())
		LApply->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Corrections (%d)"), Count)));
	if (BtnApply.IsValid())
		BtnApply->SetEnabled(Count > 0);
}

void SShintToolsPanel::RefAssetApplyBtn()
{
	int32 Count = 0;
	for (const FAssetPtr& Item : AllAssets)
		if (Item->bChecked) ++Count;

	if (LApplyAsset.IsValid())
		LApplyAsset->SetText(FText::FromString(
			FString::Printf(TEXT("✓  Apply Corrections (%d)"), Count)));
	if (BtnApplyAsset.IsValid())
		BtnApplyAsset->SetEnabled(Count > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// State setters + attribute getters
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::SetSt   (EStatus  S) { St      = S; Invalidate(EInvalidateWidget::Paint); }
void SShintToolsPanel::SetCode (EModule  S) { CodeSt  = S; Invalidate(EInvalidateWidget::Paint); }
void SShintToolsPanel::SetAsset(EModule  S) { AssetSt = S; Invalidate(EInvalidateWidget::Paint); }

FSlateColor SShintToolsPanel::StatusColor() const
{
	switch (St)
	{
	case EStatus::Online:   return FSlateColor(Green());
	case EStatus::Offline:  return FSlateColor(Red());
	case EStatus::Checking: return FSlateColor(Yellow());
	default:                return FSlateColor(Dim());
	}
}

FText SShintToolsPanel::StatusText() const
{
	switch (St)
	{
	case EStatus::Online:   return LOCTEXT("StOn",  "Online");
	case EStatus::Offline:  return LOCTEXT("StOff", "Offline");
	case EStatus::Checking: return LOCTEXT("StChk", "Checking…");
	default:                return LOCTEXT("StUnk", "Not checked");
	}
}

TOptional<float> SShintToolsPanel::CodePct() const
{
	if (CodeSt  == EModule::Running) return TOptional<float>();
	if (CodeSt  == EModule::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

TOptional<float> SShintToolsPanel::AssetPct() const
{
	if (AssetSt == EModule::Running) return TOptional<float>();
	if (AssetSt == EModule::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

#undef LOCTEXT_NAMESPACE
