// Copyright ShintTools. All Rights Reserved.
//
// SShintEmptyState — neutral "nothing to show" surface for lists / sections.
//
// Replaces the various ad-hoc empty placeholders scattered across the panel
// (CodeEmptyState / AssetEmptyState raw STextBlocks). Centered headline +
// subtitle, optional inline action button.
//
// Usage:
//   SNew(SShintEmptyState)
//     .Headline(LOCTEXT("NoIssues", "No issues found"))
//     .Subtitle(LOCTEXT("RunScan", "Run a scan to surface code-quality issues."))
//     .ActionLabel(LOCTEXT("Scan", "Scan project"))
//     .OnActionClicked(this, &SShintToolsPanel::OnScanProjectClicked)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"

class SShintEmptyState : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(FReply, FOnEmptyActionClicked);

	SLATE_BEGIN_ARGS(SShintEmptyState)
		: _Headline()
		, _Subtitle()
		, _ActionLabel()
	{}
		SLATE_ATTRIBUTE(FText, Headline)
		SLATE_ATTRIBUTE(FText, Subtitle)

		/** Optional action button. Empty label hides the button entirely. */
		SLATE_ATTRIBUTE(FText, ActionLabel)
		SLATE_EVENT(FOnEmptyActionClicked, OnActionClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
