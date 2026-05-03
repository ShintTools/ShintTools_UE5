// Copyright ShintTools. All Rights Reserved.

#include "SShintTopBar.h"
#include "ShintStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace ShintTopBarPrivate
{
	static TUniquePtr<FSlateColorBrush>      GBarBg;

	// One LED brush per state, allocated lazily on first request. They live
	// for the module lifetime — the brush pointer Slate captures must remain
	// valid until the panel is destroyed.
	static TMap<uint8, TUniquePtr<FSlateRoundedBoxBrush>> GLedBrushes;

	static const FSlateBrush* BarBg()
	{
		if (!GBarBg.IsValid())
			GBarBg = MakeUnique<FSlateColorBrush>(FShintStyle::Colors::BgTopbar());
		return GBarBg.Get();
	}

	static const FSlateBrush* LedBrush(EShintConnState State)
	{
		const uint8 Key = static_cast<uint8>(State);
		if (TUniquePtr<FSlateRoundedBoxBrush>* Found = GLedBrushes.Find(Key))
			return Found->Get();

		FLinearColor Color = FShintStyle::Colors::TextMuted();
		switch (State)
		{
		case EShintConnState::Connected:    Color = FShintStyle::Colors::Success(); break;
		case EShintConnState::Connecting:   Color = FShintStyle::Colors::Warning(); break;
		case EShintConnState::Disconnected: Color = FShintStyle::Colors::Error();   break;
		case EShintConnState::Unknown:
		default:                                                                    break;
		}

		// Half-transparent fill + solid outline gives the LED a soft glow look
		// at small sizes (10x10) without using a bitmap.
		FLinearColor Fill = Color;
		Fill.A = 0.5f;

		TUniquePtr<FSlateRoundedBoxBrush> Brush = MakeUnique<FSlateRoundedBoxBrush>(
			Fill,
			/*Radius=*/5.f,
			Color,
			/*OutlineWidth=*/1.f);
		const FSlateBrush* Raw = Brush.Get();
		GLedBrushes.Add(Key, MoveTemp(Brush));
		return Raw;
	}
}

void SShintTopBar::Construct(const FArguments& InArgs)
{
	const TAttribute<EShintConnState> ConnAttr = InArgs._ConnState;

	auto LedBrushAttr = [ConnAttr]() -> const FSlateBrush*
	{
		return ShintTopBarPrivate::LedBrush(ConnAttr.Get());
	};

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(ShintTopBarPrivate::BarBg())
		.Padding(FMargin(FShintStyle::Space::S5, FShintStyle::Space::S3))
		[
			SNew(SHorizontalBox)

			// Title — current destination
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._Title)
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]

			// Spacer
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[ SNew(SSpacer) ]

			// LED + status text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, FShintStyle::Space::S2, 0.f))
			[
				SNew(SBox)
				.WidthOverride(10.f).HeightOverride(10.f)
				[
					SNew(SBorder).BorderImage_Lambda(LedBrushAttr)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(InArgs._StatusText)
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			]
		]
	];
}
