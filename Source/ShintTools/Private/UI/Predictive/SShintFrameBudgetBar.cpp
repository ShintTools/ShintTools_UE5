// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "Predictive/SShintFrameBudgetBar.h"

#include "ShintStyle.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SShintFrameBudgetBar"

void SShintFrameBudgetBar::Construct(const FArguments& InArgs)
{
	BarHeight  = InArgs._BarHeight;
	WhiteBrush = FAppStyle::Get().GetBrush("WhiteBrush");
}

void SShintFrameBudgetBar::SetData(
	const TArray<FShintBudgetBarSegment>& InSegments, double InBudgetMs,
	const FString& InCaption)
{
	Segments = InSegments;
	BudgetMs = InBudgetMs > 0.0 ? InBudgetMs : 16.67;
	Caption  = InCaption;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SShintFrameBudgetBar::ComputeDesiredSize(float) const
{
	return FVector2D(0.f, BarHeight + 20.f);
}

int32 SShintFrameBudgetBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!WhiteBrush) return LayerId;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float W = (float)Size.X;
	const float Y = 16.f;   // leave room for the caption above

	const int32 TrackLayer = LayerId;
	const int32 FillLayer  = LayerId + 1;
	const int32 MarkerLayer = LayerId + 2;
	const int32 TextLayer  = LayerId + 3;

	// The full-width scale maps [0 .. max(budget, total spend)] so an overrun
	// stays visible instead of clipping. Budget sits at BudgetFrac of the width.
	double TotalMax = 0.0;
	for (const FShintBudgetBarSegment& S : Segments)
	{
		TotalMax += FMath::Max(S.ExpectedMs, S.MaxMs);
	}
	const double Scale = FMath::Max(BudgetMs, TotalMax);
	if (Scale <= 0.0) return TextLayer;
	const float PxPerMs = W / (float)Scale;

	// Track.
	FSlateDrawElement::MakeBox(OutDrawElements, TrackLayer,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(W, BarHeight), FSlateLayoutTransform(FVector2f(0.f, Y))),
		WhiteBrush, ESlateDrawEffect::None, FShintStyle::Colors::BgTopbar());

	// Stacked segments (expected) + uncertainty tail (expected→max, 30% alpha).
	float Cursor = 0.f;
	const float BudgetPx = (float)BudgetMs * PxPerMs;
	for (const FShintBudgetBarSegment& S : Segments)
	{
		const float ExpW = (float)S.ExpectedMs * PxPerMs;
		if (ExpW > 0.1f)
		{
			// Overrun portion (past the budget marker) tints red.
			FLinearColor Fill = S.Color;
			if (Cursor >= BudgetPx) Fill = FShintStyle::Colors::SevCritical();
			FSlateDrawElement::MakeBox(OutDrawElements, FillLayer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(ExpW, BarHeight),
					FSlateLayoutTransform(FVector2f(Cursor, Y))),
				WhiteBrush, ESlateDrawEffect::None, Fill);
			Cursor += ExpW;
		}
		const float TailW = (float)FMath::Max(0.0, S.MaxMs - S.ExpectedMs) * PxPerMs;
		if (TailW > 0.1f)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, FillLayer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(TailW, BarHeight),
					FSlateLayoutTransform(FVector2f(Cursor, Y))),
				WhiteBrush, ESlateDrawEffect::None,
				S.Color.CopyWithNewOpacity(0.3f));
			Cursor += TailW;
		}
	}

	// Budget marker — 1px white line at the budget position.
	FSlateDrawElement::MakeBox(OutDrawElements, MarkerLayer,
		AllottedGeometry.ToPaintGeometry(
			FVector2f(1.5f, BarHeight + 4.f),
			FSlateLayoutTransform(FVector2f(BudgetPx, Y - 2.f))),
		WhiteBrush, ESlateDrawEffect::None, FShintStyle::Colors::TextPrimary());

	// Caption (left) + budget label (right).
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	if (!Caption.IsEmpty())
	{
		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(200.f, 12.f), FSlateLayoutTransform(FVector2f(0.f, 0.f))),
			Caption, Font, ESlateDrawEffect::None, FShintStyle::Colors::TextMuted());
	}

	return TextLayer;
}

#undef LOCTEXT_NAMESPACE
// [LOD-STRIP-END]
