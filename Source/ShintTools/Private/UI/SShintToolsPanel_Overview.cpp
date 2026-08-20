// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintCoreClient.h"

#include "ShintStyle.h"
#include "SShintCard.h"
#include "SShintKpiTile.h"
#include "SShintEmptyState.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> SShintToolsPanel::BuildOverviewHero()
{
	auto Snap = [this]() -> const FShintQualityScoreSnapshot& { return LastQualityScore; };

	auto QualityValue = [Snap]() -> FText
	{
		const FShintQualityScoreSnapshot& S = Snap();
		return S.bValid
			? FText::FromString(FString::Printf(TEXT("%.0f"), S.OverallScore))
			: FText::FromString(TEXT("—"));
	};

	auto QualityColor = [Snap]() -> FSlateColor
	{
		const FShintQualityScoreSnapshot& S = Snap();
		if (!S.bValid)             return FSlateColor(FShintStyle::Colors::TextMuted());
		if (S.OverallScore >= 80)  return FSlateColor(FShintStyle::Colors::SevLow());
		if (S.OverallScore >= 50)  return FSlateColor(FShintStyle::Colors::SevHigh());
		return                              FSlateColor(FShintStyle::Colors::SevCritical());
	};

	auto IntText = [](int32 N) -> FText { return FText::AsNumber(N); };

	auto IssuesValue = [this, IntText]() -> FText
	{
		return IntText(LastCodeResult.TotalIssues + LastAssetResult.InvalidAssets);
	};
	auto ErrorsValue = [this, IntText]() -> FText
	{
		return IntText(LastCodeResult.TotalErrors);
	};
	auto FilesValue  = [this, IntText]() -> FText
	{
		return IntText(LastCodeResult.FilesScanned + LastAssetResult.TotalAssets);
	};

	const FMargin GapL  = FMargin(0.f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f);
	const FMargin GapM  = FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, FShintStyle::Space::S2 * 0.5f, 0.f);
	const FMargin GapR  = FMargin(FShintStyle::Space::S2 * 0.5f, 0.f, 0.f, 0.f);

	TSharedRef<SHorizontalBox> Grid = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapL)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KQ","QUALITY SCORE"))
				.Value_Lambda(QualityValue)
				.ValueColor_Lambda(QualityColor)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapM)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KI","TOTAL ISSUES"))
				.Value_Lambda(IssuesValue)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapM)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KE","ERRORS"))
				.Value_Lambda(ErrorsValue)
				.ValueColor(FSlateColor(FShintStyle::Colors::SevCritical()))
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(GapR)
		[
			SNew(SShintCard).bShowHeader(false).ContentPadding(FShintStyle::Space::S4)
			[
				SNew(SShintKpiTile)
				.Caption(NSLOCTEXT("OverviewHero","KF","FILES SCANNED"))
				.Value_Lambda(FilesValue)
				.ValueColor(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S5))
		[
			BuildSectionTitle(
				NSLOCTEXT("OverviewHero","Title", "PROJECT OVERVIEW"),
				NSLOCTEXT("OverviewHero","Sub",   "Quality Score and issue counters · last scan summary"))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S5))
		[ Grid ]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SShintCard)
			.bShowHeader(false)
			.ContentPadding(FShintStyle::Space::S5)
			[
				SNew(SShintEmptyState)
				.Headline(NSLOCTEXT("OverviewHero","Hero", "Run your first scan"))
				.Subtitle(NSLOCTEXT("OverviewHero","HeroSub",
					"Switch to the Code or Assets tab on the left to begin a scan. "
					"Scan results populate the tiles above."))
			]
		];
}
