// Copyright ShintTools. All Rights Reserved.

#include "SShintSeverityBadge.h"
#include "ShintStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace ShintBadgePrivate
{
	// One brush per severity color, allocated once on first use. Slate stores
	// the raw pointer captured here so the TUniquePtr keeps the FSlateBrush
	// at a stable heap address for the whole module lifetime — TMap reallocs
	// would invalidate raw pointers stored inside a value-typed brush.
	static TMap<uint32, TUniquePtr<FSlateRoundedBoxBrush>> GBadgeBrushes;

	static const FSlateBrush* BadgeBrush(const FLinearColor& Tint)
	{
		// Quantise the color to 8-bit to keep the cache bounded — even with
		// HDR tints the worst case is 16M entries; in practice we hit at most
		// 4 (critical / high / medium / low). Tint goes into the fill at low
		// alpha for a soft pill, plus a stronger outline for legibility.
		const uint32 Key =
			(uint32(Tint.R * 255) << 16) |
			(uint32(Tint.G * 255) <<  8) |
			(uint32(Tint.B * 255));

		if (TUniquePtr<FSlateRoundedBoxBrush>* Found = GBadgeBrushes.Find(Key))
			return Found->Get();

		FLinearColor Fill = Tint;
		Fill.A = 0.18f; // soft tinted fill — keeps text legible on dark BG_CARD

		TUniquePtr<FSlateRoundedBoxBrush> Brush = MakeUnique<FSlateRoundedBoxBrush>(
			Fill,
			FShintStyle::Radius::Control,
			Tint,                             // outline matches severity
			/*OutlineWidth=*/1.f);

		const FSlateBrush* Raw = Brush.Get();
		GBadgeBrushes.Add(Key, MoveTemp(Brush));
		return Raw;
	}

	static FString Capitalise(const FString& In)
	{
		if (In.IsEmpty()) return In;
		return FString::Chr(FChar::ToUpper(In[0])) + In.Mid(1).ToLower();
	}
}

void SShintSeverityBadge::Construct(const FArguments& InArgs)
{
	const FString Sev = InArgs._Severity.Get();
	const FLinearColor Color = FShintStyle::Colors::FromSeverity(Sev);

	const FText Label = InArgs._Label.IsBound() || !InArgs._Label.Get().IsEmpty()
		? InArgs._Label.Get()
		: FText::FromString(ShintBadgePrivate::Capitalise(Sev));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ShintBadgePrivate::BadgeBrush(Color))
		.Padding(FMargin(FShintStyle::Space::S2, 2.f)) // 8px H, 2px V → tight pill
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(Color))
		]
	];
}
