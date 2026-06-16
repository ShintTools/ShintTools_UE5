// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintEmptyState.h"
#include "ShintStyle.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

void SShintEmptyState::Construct(const FArguments& InArgs)
{
	const FOnEmptyActionClicked OnAction = InArgs._OnActionClicked;

	const bool bHasAction =
		InArgs._ActionLabel.IsBound() || !InArgs._ActionLabel.Get().IsEmpty();

	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);

	// Headline
	Stack->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.f, 0.f, 0.f, FShintStyle::Space::S2))
		[
			SNew(STextBlock)
			.Text(InArgs._Headline)
			.Font(FShintStyle::Fonts::H2())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		];

	// Subtitle
	Stack->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(FMargin(0.f, 0.f, 0.f, bHasAction ? FShintStyle::Space::S4 : 0.f))
		[
			SNew(SBox)
			.MaxDesiredWidth(420.f) // keep the line measure readable on wide panels
			[
				SNew(STextBlock)
				.Text(InArgs._Subtitle)
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
			]
		];

	// Action button (optional)
	if (bHasAction)
	{
		Stack->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S2))
				.OnClicked_Lambda([OnAction]() -> FReply
				{
					return OnAction.IsBound() ? OnAction.Execute() : FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(InArgs._ActionLabel)
					.Font(FShintStyle::Fonts::Body())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]
			];
	}

	// Center the stack vertically + horizontally so it reads as a centered hero
	// regardless of the parent slot's size.
	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(FShintStyle::Space::S5))
		[
			Stack
		]
	];
}
