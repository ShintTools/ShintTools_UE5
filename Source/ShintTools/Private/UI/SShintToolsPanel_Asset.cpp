// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintCoreClient.h"

#include "ShintStyle.h"

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
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{

	const FSlateBrush* ShintAssetTypeIcon(const FString& AssetType)
	{
		if (!AssetType.IsEmpty())
		{
			const FName Key(*FString::Printf(TEXT("ClassIcon.%s"), *AssetType));
			if (const FSlateBrush* Found =
				FAppStyle::Get().GetOptionalBrush(Key, nullptr, nullptr))
			{
				return Found;
			}
		}
		return FAppStyle::Get().GetBrush("ClassIcon.Default");
	}
}

TSharedRef<SWidget> SShintToolsPanel::BuildAssetNamingSection()
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
					LOCTEXT("ANBTitle", "Asset Naming Bot"),
					LOCTEXT("ANBSub", "Scan entire project · detect invalid names · apply UE5 rename (refs preserved) · send to dashboard"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, Gap, 0.f))
				[ Tile(LOCTEXT("ANBT", "ASSETS"), AssetTotal_Label,
					LOCTEXT("ANBTSub", "Project-wide"), FShintStyle::Colors::TextMuted()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, Gap, 0.f))
				[ Tile(LOCTEXT("ANBI", "INVALID"), AssetInvalid_Label,
					LOCTEXT("ANBISub", "Need renaming"), FShintStyle::Colors::SevCritical()) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(Gap, 0.f, 0.f, 0.f))
				[ Tile(LOCTEXT("ANBMS", "TIME (s)"), AssetTime_Label,
					LOCTEXT("ANBMSSub", "Last scan"), FShintStyle::Colors::TextMuted()) ]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[
				BuildModuleProgressBar(AssetProgressBar,
					TAttribute<TOptional<float>>::Create(
						TAttribute<TOptional<float>>::FGetter::CreateSP(this, &SShintToolsPanel::GetAssetProgress)))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[

				SNew(SButton).ContentPadding(FMargin(14.f, 9.f))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SShintToolsPanel::OnScanAssetsClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("ScanAssets", "Scan All Assets")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_White()))
				]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ BuildAssetResultsPanel() ]
		];
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
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("ANBSearch", "Search"))
				.OnTextChanged_Lambda([this](const FText& T) {
					AssetSearchText = T.ToString();
					ApplyAssetFilter();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f) [ AssetTypeCombo ]

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnSelectAllAssetsClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("ANBSel", "Select All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SButton).ContentPadding(FMargin(12.f, 6.f))
				.ButtonColorAndOpacity(FSlateColor(C_Surface()))
				.OnClicked(this, &SShintToolsPanel::OnDeselectAllAssetsClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("ANBDes", "Deselect All")).Font(F_Small())
					.ColorAndOpacity(FSlateColor(C_Gray()))
				]
			]

			+ SHorizontalBox::Slot().AutoWidth()
			[
				SAssignNew(ApplyAssetBtn, SButton)
				.IsEnabled(false).ContentPadding(FMargin(12.f, 6.f))
				.OnClicked(this, &SShintToolsPanel::OnApplySelectedAssetFixesClicked)
				[
					SAssignNew(ApplyAssetBtnLabel, STextBlock)
					.Text(LOCTEXT("ApplyAsset", "Fix all (0)"))
					.Font(F_Small()).ColorAndOpacity(FSlateColor(C_White()))
				]
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

					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(0.f)
					.ToolTipText(LOCTEXT("ANBRevealTip", "Click to reveal in Content Browser"))
					.OnClicked(this, &SShintToolsPanel::OnAssetRowNavigateClicked, Item)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("⚠"))).Font(F_Small())
								.ColorAndOpacity(FSlateColor(C_Yellow()))
							]

							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							  .Padding(0.f, 0.f, 6.f, 0.f)
							[
								SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
								[
									SNew(SImage).Image(ShintAssetTypeIcon(Item->AssetType))
								]
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
			]
		];
}

FReply SShintToolsPanel::OnScanAssetsClicked()
{

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

	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = true;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = true;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnDeselectAllAssetsClicked()
{
	for (FShintAssetItemPtr& I : AllAssetItems)   if (I.IsValid()) I->bChecked = false;
	for (FShintAssetItemPtr& I : AssetIssueItems) if (I.IsValid()) I->bChecked = false;
	if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
	RefreshApplyAssetLabel();
	return FReply::Handled();
}

FReply SShintToolsPanel::OnAssetRowNavigateClicked(FShintAssetItemPtr Item)
{
	if (!Item.IsValid() || Item->AssetPath.IsEmpty())
	{
		return FReply::Handled();
	}

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Found;
	AR.GetAssetsByPackageName(FName(*Item->AssetPath), Found);
	if (Found.Num() == 0)
	{
		FNotificationInfo Info(LOCTEXT("ANBRevealMissing", "Asset no longer found — it may have been renamed or deleted since the scan."));
		Info.ExpireDuration       = 4.0f;
		Info.bUseLargeFont        = false;
		Info.bUseSuccessFailIcons = false;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	FContentBrowserModule& CBModule =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	CBModule.Get().SyncBrowserToAssets(Found);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
