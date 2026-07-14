// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "SShintTreemap.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SShintTreemap"

namespace
{
	// A laid-out cell: pixel rect + index back into the items array.
	struct FTreemapCell { double X, Y, W, H; int32 Index; };

	// "Worst" aspect ratio of a candidate row (Bruls/Huizing/van Wijk squarified):
	// the fixed side the row spans is `Len`; the row's summed area is `Sum` with
	// smallest/largest cell area MinA/MaxA. Lower is squarer.
	double WorstRatio(double Sum, double MinA, double MaxA, double Len)
	{
		if (Sum <= 0.0 || Len <= 0.0 || MinA <= 0.0)
			return TNumericLimits<double>::Max();
		const double S2 = Sum * Sum;
		const double L2 = Len * Len;
		return FMath::Max((L2 * MaxA) / S2, S2 / (L2 * MinA));
	}

	// Squarified layout: `Areas` are pre-scaled to pixels² and sorted descending,
	// summing to (W*H) of the starting rect. Fills Out with one cell per area.
	void Squarify(const TArray<double>& Areas, double X, double Y, double W, double H,
		TArray<FTreemapCell>& Out)
	{
		const int32 N = Areas.Num();
		int32 i = 0;
		while (i < N && W > 0.5 && H > 0.5)
		{
			// Lay the row along the shorter side so cells stay square-ish.
			const bool bAlongHeight = (W >= H);
			const double Len = bAlongHeight ? H : W;

			// Grow the row while it keeps improving the worst aspect ratio.
			int32 Count = 0;
			double Sum = 0.0, MaxA = 0.0;
			double MinA = TNumericLimits<double>::Max();
			double BestWorst = TNumericLimits<double>::Max();
			for (int32 j = i; j < N; ++j)
			{
				const double A       = Areas[j];
				const double NewSum  = Sum + A;
				const double NewMin  = FMath::Min(MinA, A);
				const double NewMax  = FMath::Max(MaxA, A);
				const double W2      = WorstRatio(NewSum, NewMin, NewMax, Len);
				if (Count > 0 && W2 > BestWorst) break;
				Sum = NewSum; MinA = NewMin; MaxA = NewMax; BestWorst = W2;
				++Count;
			}

			// Place [i, i+Count) as strips across Len; row thickness = Sum/Len.
			const double Thick = Sum / Len;
			double Pos = 0.0;
			for (int32 k = i; k < i + Count; ++k)
			{
				const double Frac = Areas[k] / Sum;   // fraction of Len
				FTreemapCell C;
				C.Index = k;
				if (bAlongHeight)
				{
					C.X = X;            C.Y = Y + Pos;
					C.W = Thick;        C.H = Len * Frac;
				}
				else
				{
					C.X = X + Pos;      C.Y = Y;
					C.W = Len * Frac;   C.H = Thick;
				}
				Out.Add(C);
				Pos += Len * Frac;
			}

			// Shrink the free rect by the row just placed.
			if (bAlongHeight) { X += Thick; W -= Thick; }
			else              { Y += Thick; H -= Thick; }
			i += Count;
		}
	}

	// Readable text colour over a filled cell: near-black on light fills,
	// near-white on dark ones (Rec. 601 luma).
	FLinearColor TextOn(const FLinearColor& Fill)
	{
		const double Luma = 0.299 * Fill.R + 0.587 * Fill.G + 0.114 * Fill.B;
		return Luma > 0.6 ? FLinearColor(0.05f, 0.06f, 0.08f)
						  : FLinearColor(0.96f, 0.97f, 0.98f);
	}
}

void SShintTreemap::Construct(const FArguments& InArgs)
{
	MinDesiredHeight = InArgs._MinDesiredHeight;
	WhiteBrush       = FAppStyle::Get().GetBrush("WhiteBrush");
}

void SShintTreemap::SetItems(const TArray<FShintTreemapItem>& InItems)
{
	Items.Reset();
	TotalValue = 0.0;
	for (const FShintTreemapItem& It : InItems)
	{
		if (It.Value > 0.0)
		{
			Items.Add(It);
			TotalValue += It.Value;
		}
	}
	Items.Sort([](const FShintTreemapItem& A, const FShintTreemapItem& B)
	{
		return A.Value > B.Value;
	});
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SShintTreemap::ComputeDesiredSize(float) const
{
	return FVector2D(0.f, MinDesiredHeight);
}

int32 SShintTreemap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	// Empty / too-small: centred hint, nothing to lay out.
	if (Items.Num() == 0 || TotalValue <= 0.0 || Size.X < 8.0 || Size.Y < 8.0)
	{
		if (WhiteBrush)
		{
			FSlateDrawElement::MakeText(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(Size.X, 18.f),
					FSlateLayoutTransform(FVector2f(0.f, (float)Size.Y * 0.5f - 9.f))),
				LOCTEXT("TreemapEmpty", "Run a scan to see the VRAM breakdown.").ToString(),
				Font, ESlateDrawEffect::None, FLinearColor(0.55f, 0.60f, 0.66f));
		}
		return LayerId;
	}

	// Scale values → pixels² so the layout fills the widget exactly.
	const double Area  = Size.X * Size.Y;
	const double Scale = Area / TotalValue;
	TArray<double> Areas;
	Areas.Reserve(Items.Num());
	for (const FShintTreemapItem& It : Items)
	{
		Areas.Add(It.Value * Scale);
	}

	TArray<FTreemapCell> Cells;
	Cells.Reserve(Items.Num());
	Squarify(Areas, 0.0, 0.0, Size.X, Size.Y, Cells);

	const int32 BoxLayer  = LayerId;
	const int32 TextLayer = LayerId + 1;
	constexpr float Gap   = 1.5f;   // hairline gutter between cells

	for (const FTreemapCell& C : Cells)
	{
		const float X = (float)C.X + Gap;
		const float Y = (float)C.Y + Gap;
		const float W = (float)C.W - Gap * 2.f;
		const float H = (float)C.H - Gap * 2.f;
		if (W <= 0.5f || H <= 0.5f) continue;

		const FShintTreemapItem& It = Items[C.Index];

		if (WhiteBrush)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BoxLayer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
				WhiteBrush, ESlateDrawEffect::None, It.Color);
		}

		// Labels only when the cell can hold them without ugly overflow.
		if (W >= 52.f && H >= 22.f)
		{
			const FLinearColor Ink = TextOn(It.Color);
			// Truncate to what fits (~6 px per glyph at size 8).
			const int32 MaxChars = FMath::Max(3, (int32)((W - 8.f) / 6.f));
			FString Label = It.Label;
			if (Label.Len() > MaxChars) Label = Label.Left(MaxChars - 1) + TEXT("…");

			FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(W - 6.f, 14.f), FSlateLayoutTransform(FVector2f(X + 4.f, Y + 3.f))),
				Label, Font, ESlateDrawEffect::None, Ink);

			if (H >= 36.f && !It.Detail.IsEmpty())
			{
				FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
					AllottedGeometry.ToPaintGeometry(
						FVector2f(W - 6.f, 14.f), FSlateLayoutTransform(FVector2f(X + 4.f, Y + 17.f))),
					It.Detail, Font, ESlateDrawEffect::None,
					Ink.CopyWithNewOpacity(0.8f));
			}
		}
	}

	return TextLayer;
}

#undef LOCTEXT_NAMESPACE
// [LOD-STRIP-END]
