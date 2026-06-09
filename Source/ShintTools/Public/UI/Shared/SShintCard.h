// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintCard — bordered surface for grouping related content.
//
// One of three shared widgets (with SShintSeverityBadge and SShintKpiTile)
// that replace ad-hoc SBorder + ST4::Solid stacks throughout the panel.
// A card has:
//   * BG_CARD fill (#161616), BORDER_SUBTLE outline (#2a2a2a), 8px radius
//   * Optional title row (Bahnschrift Bold 16) with a right-aligned slot
//     for actions (filter buttons, "view all" links, etc.)
//   * Configurable inner padding (defaults to S4 = 16px)
//
// Usage:
//   SNew(SShintCard)
//     .Title(LOCTEXT("ScanResults", "Scan Results"))
//     .HeaderRight()[ SNew(SButton).Text(...) ]
//     [
//       SNew(SVerticalBox) + SVerticalBox::Slot() [ ... ]
//     ];

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class STextBlock;

class SShintCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintCard)
		: _Title()
		, _ContentPadding(16.f)
		, _bShowHeader(true)
	{}
		/** Title shown in the header row. Empty hides the header entirely. */
		SLATE_ATTRIBUTE(FText, Title)

		/** Inner padding around the body content. Defaults to FShintStyle::Space::S4. */
		SLATE_ARGUMENT(float, ContentPadding)

		/** Force-hide the header even if Title is set. */
		SLATE_ARGUMENT(bool, bShowHeader)

		/** Slot dropped into the header's right-aligned area (e.g. filter chips). */
		SLATE_NAMED_SLOT(FArguments, HeaderRight)

		/** Body content of the card. */
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<FText> TitleAttr;
	TSharedPtr<STextBlock> TitleText;
};
