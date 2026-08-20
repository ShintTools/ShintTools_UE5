// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintSeverityBadge.h"
#include "ShintStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace ShintBadgePrivate
{

	static TMap<uint32, TUniquePtr<FSlateRoundedBoxBrush>> GBadgeBrushes;

	static const FSlateBrush* BadgeBrush(const FLinearColor& Tint)
	{

		const uint32 Key =
			(uint32(Tint.R * 255) << 16) |
			(uint32(Tint.G * 255) <<  8) |
			(uint32(Tint.B * 255));

		if (TUniquePtr<FSlateRoundedBoxBrush>* Found = GBadgeBrushes.Find(Key))
			return Found->Get();

		FLinearColor Fill = Tint;
		Fill.A = 0.18f;

		TUniquePtr<FSlateRoundedBoxBrush> Brush = MakeUnique<FSlateRoundedBoxBrush>(
			Fill,
			FShintStyle::Radius::Control,
			Tint,
			1.f);

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
		.Padding(FMargin(FShintStyle::Space::S2, 2.f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(Color))
		]
	];
}
