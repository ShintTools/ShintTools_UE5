// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// SShintScoreGauge — a radial 0-100 risk gauge painted directly (SLeafWidget,
// same idiom as SShintTreemap). A 270° track in the subtle border colour with
// the value arc drawn on top in the risk-band colour (green→grey→orange→red),
// the number centred inside, and an uppercase caption below.
//
// Data-in is a post-construction SetScore(): the dashboard pushes new values
// after each /predict/analyze or /predict/simulate and the widget repaints.
// The value can animate from a "before" to an "after" reading for the Impact
// Simulator's before→after transition.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SShintScoreGauge : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShintScoreGauge)
		: _Caption()
		, _Diameter(96.f)
		, _bIsOverall(false)
	{}
		/** Uppercase caption under the gauge, e.g. "CPU RISK". */
		SLATE_ARGUMENT(FText, Caption)
		/** Ring diameter in px. Overall gauge is larger. */
		SLATE_ARGUMENT(float, Diameter)
		/** Overall health reads inverted (100 = healthy → green). */
		SLATE_ARGUMENT(bool, bIsOverall)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the displayed value (0-100). Snaps; no animation. */
	void SetScore(int32 InScore);

	/** Animate from Before→After over ~300 ms (the simulator transition). */
	void AnimateScore(int32 Before, int32 After);

	/** True when this gauge has no data (renders a hollow "—"). */
	void SetNoData(bool bInNoData);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	FText  Caption;
	float  Diameter   = 96.f;
	bool   bIsOverall = false;
	bool   bNoData    = false;

	int32  Score        = 0;   // the settled value
	int32  DisplayScore = 0;   // what's painted (animates toward Score)

	// Animation state.
	double AnimStartTime = 0.0;
	int32  AnimFrom      = 0;
	int32  AnimTo        = 0;
	bool   bAnimating    = false;

	// Risk-band colour for a 0-100 value (respecting bIsOverall inversion).
	FLinearColor BandColor(int32 Value) const;
};
// [LOD-STRIP-END]
