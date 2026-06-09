// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintKpiTile.h"
#include "ShintStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

void SShintKpiTile::Construct(const FArguments& InArgs)
{
	const TAttribute<FSlateColor> ValueColor =
		InArgs._ValueColor.IsBound() || InArgs._ValueColor.IsSet()
			? InArgs._ValueColor
			: TAttribute<FSlateColor>(FSlateColor(FShintStyle::Colors::TextPrimary()));

	ChildSlot
	[
		SNew(SVerticalBox)

		// Row 1: caption (small, muted) — uppercase look implied by font choice
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S2))
		[
			SNew(STextBlock)
			.Text(InArgs._Caption)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		]

		// Row 2: value + optional trend, aligned baselines
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Text(InArgs._Value)
				.Font(FShintStyle::Fonts::H1())
				.ColorAndOpacity(ValueColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[ SNew(SSpacer) ]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(FShintStyle::Space::S2, 0.f, 0.f, FShintStyle::Space::S1))
			[
				SNew(STextBlock)
				.Text(InArgs._Trend)
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		]
	];
}
