// Copyright ShintTools. All Rights Reserved.
//
// Overview hero — the 4-up KPI grid + first-run CTA shown on the
// landing tab of SShintToolsPanel. Lives in its own translation unit
// because the panel's main .cpp already exceeds 3.5k lines and the
// hero is self-contained: it reads LastCodeResult + LastAssetResult +
// LastQualityScore through `this->` and emits a SVerticalBox.

#include "SShintToolsPanel.h"
#include "ShintCoreClient.h"

#include "ShintStyle.h"
#include "SShintCard.h"
#include "SShintKpiTile.h"
#include "SShintEmptyState.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// ─────────────────────────────────────────────────────────────────────────────
// UI-REDESIGN step 8 — Overview hero
//
// Replaces the legacy BuildHeader + BuildStatusBar combo with a 4-up KPI grid
// reading from LastQualityScore. Values are bound via Value_Lambda so they
// refresh automatically as scans complete (RefreshQualityScore mutates
// LastQualityScore; Slate re-reads on the next paint).
//
// Action row beneath the KPIs offers a primary CTA to scan the project, so
// the empty Overview is still actionable for first-time users.
// ─────────────────────────────────────────────────────────────────────────────
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
		if (S.OverallScore >= 80)  return FSlateColor(FShintStyle::Colors::SevLow());      // green
		if (S.OverallScore >= 50)  return FSlateColor(FShintStyle::Colors::SevHigh());     // orange
		return                              FSlateColor(FShintStyle::Colors::SevCritical());// red
	};

	auto IntText = [](int32 N) -> FText { return FText::AsNumber(N); };

	// Overview tiles aggregate code + asset stats so the row reflects the
	// full project state regardless of which module ran last. Previously
	// the tiles bound to LastQualityScore only — running just the Asset
	// scan left "TOTAL ISSUES = 0" because LastQualityScore was empty,
	// and running both modules in sequence still only showed the code
	// side of the summary.
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

	// 4-up tile grid — inlined (avoids TAttribute<FSlateColor>::Create gymnastics).
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

		// KPI grid
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S5))
		[ Grid ]

		// Empty-state CTA shown when no scan has run yet
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
