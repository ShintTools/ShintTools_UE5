// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// SShintTreemap — a compact squarified treemap for the LOD Auditor Summary view
// (§21). Each rect's AREA is proportional to an asset's estimated resident VRAM,
// so the heaviest assets dominate at a glance; colour encodes the asset family
// (texture / mesh / material). It is a leaf widget: it owns its own layout and
// paints rects + labels directly, so it needs no child Slate tree.
//
// Studio-only; stripped from the indie/marketplace trees with the LOD module.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

// One weighted cell. Value drives the area; Label/Detail are drawn when the cell
// is large enough to fit text.
struct FShintTreemapItem
{
	FString      Label;
	FString      Detail;                    // e.g. "42.7 MB" — second line if room
	double       Value = 0.0;               // area weight (VRAM MB)
	FLinearColor Color = FLinearColor::Gray;
};

class SShintTreemap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShintTreemap)
		: _MinDesiredHeight(220.f)
	{}
		SLATE_ARGUMENT(float, MinDesiredHeight)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Replace the data set: sorts by Value desc, drops non-positive values, and
	// repaints. When empty, the widget paints a centred hint instead.
	void SetItems(const TArray<FShintTreemapItem>& InItems);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	TArray<FShintTreemapItem> Items;        // sorted desc, positive values only
	double TotalValue        = 0.0;
	float  MinDesiredHeight  = 220.f;
	const FSlateBrush* WhiteBrush = nullptr;
};
// [LOD-STRIP-END]
