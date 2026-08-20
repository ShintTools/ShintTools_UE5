// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintSidebar.h"
#include "ShintStyle.h"
#include "ShintIconStyle.h"
#include "ShintTools.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateColorBrush.h"

namespace ShintSidebarPrivate
{

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

	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);

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

	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Overview, NSLOCTEXT("Sidebar","Overview","Overview"), TEXT("ShintTools.Icons.Info")) ];
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Code,     NSLOCTEXT("Sidebar","Code",    "Code"),     TEXT("ShintTools.Icons.Code")) ];
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Assets,   NSLOCTEXT("Sidebar","Assets",  "Assets"),   TEXT("ShintTools.Icons.Tag")) ];
	Stack->AddSlot()
		.FillHeight(1.f)
		[ SNew(SSpacer) ];
	Stack->AddSlot().AutoHeight()
		[ BuildNavButton(EShintDestination::Settings, NSLOCTEXT("Sidebar","Settings","Settings"), TEXT("ShintTools.Icons.Settings")) ];

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(200.f)
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
	EShintDestination Dest, const FText& Label, const FName& Icon,
	bool bStudioOnly)
{

	auto IsActive = [this, Dest]() { return ActiveAttr.Get() == Dest; };

	auto RowVisibility = [bStudioOnly]() -> EVisibility
	{
		if (!bStudioOnly)
		{
			return EVisibility::Visible;
		}
		const FString Tier = FShintToolsModule::GetCachedTier().ToLower();
		return (Tier == TEXT("studio") || Tier == TEXT("enterprise"))
			? EVisibility::Visible
			: EVisibility::Collapsed;
	};

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
		.Visibility_Lambda(RowVisibility)
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
					.Image(FShintIconStyle::GetBrush(Icon))
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
