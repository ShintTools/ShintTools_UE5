// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// SShintFrameBudgetBar — a horizontal stacked bar of predicted frame spend vs
// the platform's frame budget (SLeafWidget, painted). Each segment is one
// layer/module contribution (Code patterns, Scene dispatch, Dynamic lights…),
// coloured from a categorical ramp. A 1px white marker sits at the budget line;
// the portion of a segment between its expected and max reading is drawn as a
// low-opacity "uncertainty tail" — the product's visual signature: bars that
// admit a range. Spend past the budget marker tints red.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

struct FShintBudgetBarSegment
{
	FString      Label;
	double       ExpectedMs = 0.0;
	double       MaxMs      = 0.0;   // for the uncertainty tail (>= ExpectedMs)
	FLinearColor Color      = FLinearColor::White;
};

class SShintFrameBudgetBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShintFrameBudgetBar)
		: _BarHeight(28.f)
	{}
		SLATE_ARGUMENT(float, BarHeight)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Push segments + the budget line (ms). Repaints. */
	void SetData(const TArray<FShintBudgetBarSegment>& InSegments, double InBudgetMs,
	             const FString& InCaption);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	float  BarHeight = 28.f;
	double BudgetMs  = 16.67;
	FString Caption;
	TArray<FShintBudgetBarSegment> Segments;

	const FSlateBrush* WhiteBrush = nullptr;
};
// [LOD-STRIP-END]
