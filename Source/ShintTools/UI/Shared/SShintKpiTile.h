// Copyright ShintTools. All Rights Reserved.
//
// SShintKpiTile — large numeric tile for the Overview dashboard.
//
// Anatomy:
//
//   ┌──────────────────────────┐
//   │ CAPTION (small, muted)   │
//   │                          │
//   │ 87.5            ▲ +3.2   │
//   │ Quality Score   trend    │
//   └──────────────────────────┘
//
// The widget is layout-only (no borders) so callers can drop it into a
// SShintCard or wrap it in their own surface. KPI values that need a
// severity tint pass a Color; the default is TextPrimary.
//
// Usage:
//   SNew(SShintKpiTile)
//     .Caption(LOCTEXT("Score", "Quality Score"))
//     .Value_Lambda([this]{ return FText::AsNumber(LastQualityScore); })
//     .Trend(LOCTEXT("Trend", "+3.2 vs last scan"))
//     .ValueColor(FShintStyle::Colors::SevLow())

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SShintKpiTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintKpiTile)
		: _Caption()
		, _Value()
		, _Trend()
	{}
		/** Small caption above the value. */
		SLATE_ATTRIBUTE(FText, Caption)

		/** The big number / metric. Bound so it can update reactively. */
		SLATE_ATTRIBUTE(FText, Value)

		/** Optional trend label rendered to the right of the value. */
		SLATE_ATTRIBUTE(FText, Trend)

		/** Color of the big value text. Defaults to TextPrimary. */
		SLATE_ATTRIBUTE(FSlateColor, ValueColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
