// Copyright 2026 ShintTools. All Rights Reserved.

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

namespace ShintTopBarPrivate
{
	// Pill-shaped background for the license badge. One brush per tier so
	// the colour matches the tier (free=muted grey, indie=accent blue,
	// studio/enterprise=gold). The map mirrors the LedBrushes pattern
	// above — lazily allocated, lives for the module lifetime.
	static TMap<FString, TUniquePtr<FSlateRoundedBoxBrush>> GTierPillBrushes;

	static FLinearColor TierAccent(const FString& Tier)
	{
		const FString Lower = Tier.ToLower();
		if (Lower == TEXT("indie"))      return FShintStyle::Colors::AccentBlue();
		if (Lower == TEXT("studio"))     return FShintStyle::Colors::Warning();
		if (Lower == TEXT("enterprise")) return FShintStyle::Colors::Warning();
		return FShintStyle::Colors::TextMuted();  // free / unknown / empty
	}

	static const FSlateBrush* TierPill(const FString& Tier)
	{
		const FString Key = Tier.ToLower();
		if (TUniquePtr<FSlateRoundedBoxBrush>* Found = GTierPillBrushes.Find(Key))
			return Found->Get();

		const FLinearColor Accent = TierAccent(Tier);
		FLinearColor Fill = Accent;
		Fill.A = 0.15f;  // soft tint so the text stays the focal point

		TUniquePtr<FSlateRoundedBoxBrush> Brush = MakeUnique<FSlateRoundedBoxBrush>(
			Fill,
			/*Radius=*/8.f,
			Accent,
			/*OutlineWidth=*/1.f);
		const FSlateBrush* Raw = Brush.Get();
		GTierPillBrushes.Add(Key, MoveTemp(Brush));
		return Raw;
	}
}

void SShintTopBar::Construct(const FArguments& InArgs)
{
	const TAttribute<EShintConnState> ConnAttr = InArgs._ConnState;
	const TAttribute<FText> TierAttr           = InArgs._TierText;

	auto LedBrushAttr = [ConnAttr]() -> const FSlateBrush*
	{
		return ShintTopBarPrivate::LedBrush(ConnAttr.Get());
	};

	auto TierBrushAttr = [TierAttr]() -> const FSlateBrush*
	{
		return ShintTopBarPrivate::TierPill(TierAttr.Get().ToString());
	};

	auto TierColorAttr = [TierAttr]() -> FSlateColor
	{
		return FSlateColor(ShintTopBarPrivate::TierAccent(
			TierAttr.Get().ToString()));
	};

	auto TierVisibility = [TierAttr]() -> EVisibility
	{
		return TierAttr.Get().IsEmpty()
			? EVisibility::Collapsed
			: EVisibility::Visible;
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

			// License tier pill — collapsed until /license/status resolves.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(FShintStyle::Space::S3, 0.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.Visibility_Lambda(TierVisibility)
				.BorderImage_Lambda(TierBrushAttr)
				.Padding(FMargin(FShintStyle::Space::S2,
				                 FShintStyle::Space::S1))
				[
					SNew(STextBlock)
					.Text(InArgs._TierText)
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity_Lambda(TierColorAttr)
				]
			]
		]
	];
}
