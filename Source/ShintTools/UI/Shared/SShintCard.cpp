// Copyright ShintTools. All Rights Reserved.

#include "SShintCard.h"
#include "ShintStyle.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace ShintCardPrivate
{
	// Heap-stable brushes — Slate captures the raw pointer, so a TMap of
	// TUniquePtr keeps them alive for the module lifetime. One per card visual
	// state: idle and (future) hover.
	static TUniquePtr<FSlateRoundedBoxBrush> GCardBrush;

	static const FSlateBrush* CardBrush()
	{
		if (!GCardBrush.IsValid())
		{
			GCardBrush = MakeUnique<FSlateRoundedBoxBrush>(
				FShintStyle::Colors::BgCard(),                  // fill
				FShintStyle::Radius::Card,                      // corner radius
				FShintStyle::Colors::BorderSubtle(),            // outline
				/*OutlineWidth=*/1.f);
		}
		return GCardBrush.Get();
	}
}

void SShintCard::Construct(const FArguments& InArgs)
{
	TitleAttr = InArgs._Title;

	const float Pad = InArgs._ContentPadding;
	const bool bRenderHeader =
		InArgs._bShowHeader && (TitleAttr.IsBound() || !TitleAttr.Get().IsEmpty());

	// Vertical stack: header (optional) + body. A single SBorder wraps the
	// stack so the rounded outline + fill apply uniformly.
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	if (bRenderHeader)
	{
		Body->AddSlot()
			.AutoHeight()
			.Padding(FMargin(Pad, Pad, Pad, FShintStyle::Space::S2))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SAssignNew(TitleText, STextBlock)
						.Text(TitleAttr)
						.Font(FShintStyle::Fonts::H2())
						.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[ SNew(SSpacer) ]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[ InArgs._HeaderRight.Widget ]
			];

		// Visual separation between header and body is handled by spacing alone
		// (the body slot below adds S3 = 12px top padding when the header is
		// present). A full divider line tends to look heavy on a dark surface;
		// the dashboard palette relies on tonal contrast instead.
	}

	Body->AddSlot()
		.FillHeight(1.f)
		.Padding(FMargin(Pad, bRenderHeader ? FShintStyle::Space::S3 : Pad, Pad, Pad))
		[ InArgs._Content.Widget ];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ShintCardPrivate::CardBrush())
		.Padding(0.f) // padding is owned by the inner stack so the divider can extend edge-to-edge
		[
			Body
		]
	];
}
