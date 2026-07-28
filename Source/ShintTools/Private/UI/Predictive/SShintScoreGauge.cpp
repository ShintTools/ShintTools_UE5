// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "Predictive/SShintScoreGauge.h"

#include "ShintStyle.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

#define LOCTEXT_NAMESPACE "SShintScoreGauge"

namespace
{
	// 270° sweep, starting at the bottom-left (135°) going clockwise — the
	// classic open-bottom gauge. Angles in radians.
	constexpr float kStartAngle = PI * 0.75f;   // 135°
	constexpr float kSweep      = PI * 1.5f;     // 270°
	constexpr int32 kSegments   = 96;            // arc smoothness
	constexpr float kStroke     = 7.f;

	// Build a poly-line approximating an arc from Frac0..Frac1 of the sweep.
	void ArcPoints(const FVector2D& Center, float Radius, float Frac0, float Frac1,
		TArray<FVector2f>& Out)
	{
		const int32 N = FMath::Max(2, FMath::CeilToInt(kSegments * (Frac1 - Frac0)));
		Out.Reset(N + 1);
		for (int32 i = 0; i <= N; ++i)
		{
			const float T = FMath::Lerp(Frac0, Frac1, (float)i / (float)N);
			const float A = kStartAngle + kSweep * T;
			Out.Add(FVector2f(
				(float)Center.X + Radius * FMath::Cos(A),
				(float)Center.Y + Radius * FMath::Sin(A)));
		}
	}
}

void SShintScoreGauge::Construct(const FArguments& InArgs)
{
	Caption    = InArgs._Caption;
	Diameter   = InArgs._Diameter;
	bIsOverall = InArgs._bIsOverall;
}

void SShintScoreGauge::SetScore(int32 InScore)
{
	Score        = FMath::Clamp(InScore, 0, 100);
	DisplayScore = Score;
	bAnimating   = false;
	bNoData      = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SShintScoreGauge::AnimateScore(int32 Before, int32 After)
{
	AnimFrom      = FMath::Clamp(Before, 0, 100);
	AnimTo        = FMath::Clamp(After, 0, 100);
	Score         = AnimTo;
	DisplayScore  = AnimFrom;
	AnimStartTime = FSlateApplication::Get().GetCurrentTime();
	bAnimating    = true;
	bNoData       = false;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SShintScoreGauge::SetNoData(bool bInNoData)
{
	bNoData = bInNoData;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SShintScoreGauge::ComputeDesiredSize(float) const
{
	// Ring + caption row underneath.
	return FVector2D(Diameter + 8.f, Diameter + 26.f);
}

FLinearColor SShintScoreGauge::BandColor(int32 Value) const
{
	// Overall health is inverted (100 = healthy). Convert to a risk value so
	// one ramp serves both.
	const int32 Risk = bIsOverall ? (100 - Value) : Value;
	if (Risk <= 20) return FShintStyle::Colors::SevLow();      // green
	if (Risk <= 50) return FShintStyle::Colors::SevMedium();   // grey
	if (Risk <= 80) return FShintStyle::Colors::SevHigh();     // orange
	return FShintStyle::Colors::SevCritical();                 // red
}

int32 SShintScoreGauge::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float R = (Diameter - kStroke) * 0.5f;
	const FVector2D Center(Size.X * 0.5, (Diameter * 0.5) + 2.0);

	const int32 TrackLayer = LayerId;
	const int32 ValueLayer = LayerId + 1;
	const int32 TextLayer  = LayerId + 2;

	// Advance the before→after animation (self-driving via repaint).
	int32 Painted = DisplayScore;
	if (bAnimating)
	{
		const double Now = FSlateApplication::Get().GetCurrentTime();
		const float  T   = FMath::Clamp((float)(Now - AnimStartTime) / 0.3f, 0.f, 1.f);
		Painted = FMath::RoundToInt(FMath::Lerp((float)AnimFrom, (float)AnimTo, T));
		if (T < 1.f)
		{
			// Keep repainting until the animation settles.
			const_cast<SShintScoreGauge*>(this)->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	// Track (full 270°).
	{
		TArray<FVector2f> Pts;
		ArcPoints(Center, R, 0.f, 1.f, Pts);
		FSlateDrawElement::MakeLines(OutDrawElements, TrackLayer,
			AllottedGeometry.ToPaintGeometry(), Pts, ESlateDrawEffect::None,
			FShintStyle::Colors::BorderSubtle(), true, kStroke);
	}

	// Value arc.
	if (!bNoData && Painted > 0)
	{
		TArray<FVector2f> Pts;
		ArcPoints(Center, R, 0.f, (float)Painted / 100.f, Pts);
		FSlateDrawElement::MakeLines(OutDrawElements, ValueLayer,
			AllottedGeometry.ToPaintGeometry(), Pts, ESlateDrawEffect::None,
			BandColor(Painted), true, kStroke);
	}

	// Centre number (or "—" for no data).
	{
		const FString Label = bNoData ? TEXT("—") : FString::FromInt(Painted);
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 20);
		const FVector2D TextSize =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
				->Measure(Label, Font);
		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f((float)TextSize.X, (float)TextSize.Y),
				FSlateLayoutTransform(FVector2f(
					(float)(Center.X - TextSize.X * 0.5),
					(float)(Center.Y - TextSize.Y * 0.5)))),
			Label, Font, ESlateDrawEffect::None,
			bNoData ? FShintStyle::Colors::TextFaint()
			        : FShintStyle::Colors::TextPrimary());
	}

	// Caption below.
	if (!Caption.IsEmpty())
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const FString Text = Caption.ToString();
		const FVector2D TextSize =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService()
				->Measure(Text, Font);
		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f((float)TextSize.X, (float)TextSize.Y),
				FSlateLayoutTransform(FVector2f(
					(float)(Center.X - TextSize.X * 0.5),
					(float)(Center.Y + R + 6.0)))),
			Text, Font, ESlateDrawEffect::None,
			FShintStyle::Colors::TextMuted());
	}

	return TextLayer;
}

#undef LOCTEXT_NAMESPACE
// [LOD-STRIP-END]
