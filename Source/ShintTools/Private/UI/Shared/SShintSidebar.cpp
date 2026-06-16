// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintSidebar.h"
#include "ShintStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"

namespace ShintSidebarPrivate
{
	// Heap-stable solid brushes — Slate captures the raw pointer, so we keep
	// owners in TUniquePtrs that live for the module lifetime.
	static TUniquePtr<FSlateColorBrush> GRailBg;
	static TUniquePtr<FSlateColorBrush> GActiveBg;

	static const FSlateBrush* RailBg()
	{
		if (!GRailBg.IsValid())
			GRailBg = MakeUnique<FSlateColorBrush>(FShintStyle::Colors::BgSidebar());
		return GRailBg.Get();
	}

	static const FSlateBrush* ActiveBg()
	{
		if (!GActiveBg.IsValid())
			GActiveBg = MakeUnique<FSlateColorBrush>(FShintStyle::Colors::BgCardHover());
		return GActiveBg.Get();
	}
}

void SShintSidebar::Construct(const FArguments& InArgs)
{
	ActiveAttr         = InArgs._Active;
	OnSelectedDelegate = InArgs._OnSelected;

	// Each destination gets a button. Glyphs are unicode block characters that
	// render reliably in Bahnschrift / Roboto without needing an icon font.
	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);

	// Brand block at the top of the rail
	Stack->AddSlot()
		.AutoHeight()
		.Padding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S5,
		                 FShintStyle::Space::S4, FShintStyle::Space::S5))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("ShintTools")))
			.Font(FShintStyle::Fonts::H2())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		];

	// Nav buttons
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Overview, NSLOCTEXT("Sidebar","Overview","Overview"), TEXT("Icons.Info")) ];
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Code,     NSLOCTEXT("Sidebar","Code",    "Code"),     TEXT("Icons.Edit")) ]; 
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Assets,   NSLOCTEXT("Sidebar","Assets",  "Assets"),   TEXT("Icons.FolderOpen")) ];
	Stack->AddSlot()
		.FillHeight(1.f)
		[ SNew(SSpacer) ];
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Settings, NSLOCTEXT("Sidebar","Settings","Settings"), TEXT("Icons.Settings")) ];

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(200.f) // fixed-width rail; the launcher uses the same.
		[
			SNew(SBorder)
			.BorderImage(ShintSidebarPrivate::RailBg())
			.Padding(0.f)
			[
				Stack
			]
		]
	];
}

TSharedRef<SWidget> SShintSidebar::BuildNavButton(
	EShintDestination Dest, const FText& Label, const FName& Icon)
{
	// Visibility of the active-state highlight strip — bound so it updates
	// instantly when the parent flips destinations.
	auto IsActive = [this, Dest]() { return ActiveAttr.Get() == Dest; };

	auto OnClick = [this, Dest]() -> FReply
	{
		if (OnSelectedDelegate.IsBound())
			OnSelectedDelegate.Execute(Dest);
		return FReply::Handled();
	};

	auto LabelColor = [IsActive]() -> FSlateColor
	{
		return FSlateColor(IsActive()
			? FShintStyle::Colors::TextPrimary()
			: FShintStyle::Colors::TextMuted());
	};

	auto RowBg = [IsActive]() -> const FSlateBrush*
	{
		return IsActive() ? ShintSidebarPrivate::ActiveBg() : FAppStyle::GetBrush("NoBorder");
	};

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(0.f)
		.OnClicked_Lambda(OnClick)
		[
			SNew(SBorder)
			.BorderImage_Lambda(RowBg)
			.Padding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S2 + 2.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, FShintStyle::Space::S3, 0.f))
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush(Icon))
					.ColorAndOpacity_Lambda(LabelColor)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity_Lambda(LabelColor)
				]
			]
		];
}
