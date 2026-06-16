// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintSidebar — left-rail navigation for the ShintTools editor panel.
//
// One vertical column of icon + label buttons that switch the active
// destination. Pure presentation — selection state is owned by the parent
// (SShintToolsPanel keeps an `EShintDestination CurrentDestination`) and
// pushed to the sidebar via Active(). Click events propagate via OnSelected.
//
// Width is fixed at 200px (matches the launcher's nav rail) so the layout
// stays predictable across DPI scales.
//
// Usage:
//   SNew(SShintSidebar)
//     .Active_Lambda([this]{ return CurrentDestination; })
//     .OnSelected_Raw(this, &SShintToolsPanel::SetDestination);
//
// EShintDestination is declared in SShintToolsPanel.h so callers don't pull
// the panel header just to use the sidebar — but the sidebar API uses it
// directly to keep the call-site ergonomic.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Identifies which destination the sidebar currently points at. Kept here
 * (instead of nested in SShintToolsPanel) so SShintSidebar.h is self-contained
 * and doesn't drag in the whole panel header just to use the enum.
 */
enum class EShintDestination : uint8
{
	Overview,
	Code,
	Assets,
	Settings,
};

class SShintSidebar : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSidebarSelected, EShintDestination);

	SLATE_BEGIN_ARGS(SShintSidebar)
		: _Active(EShintDestination::Overview)
	{}
		/** Currently-active destination. Bound so the parent can push state. */
		SLATE_ATTRIBUTE(EShintDestination, Active)

		/** Fires when the user clicks a different nav button. */
		SLATE_EVENT(FOnSidebarSelected, OnSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<EShintDestination> ActiveAttr;
	FOnSidebarSelected            OnSelectedDelegate;

	/** Build a single nav button bound to the given destination. */
	TSharedRef<SWidget> BuildNavButton(
		EShintDestination Dest, const FText& Label, const FName& Icon);
};
