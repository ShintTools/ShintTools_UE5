// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintAssistantDock — the assistant's anchored surface.
//
// Replaces the dockable nomad tab as the way the assistant is reached. A tab
// competes for layout space with the thing the user is working on, so opening
// the assistant meant rearranging the editor — the opposite of what an
// assistant is for. This is the shape chat assistants converged on for that
// reason: a launcher pinned to the bottom-right that expands into a card in
// place and collapses back to the launcher, never displacing anything.
//
//   collapsed   a 56px circular launcher, bottom-right of the editor window
//   expanded    a 380x500 card floating directly above that launcher, whose
//               glyph becomes a close button while the card is open
//
// TWO windows, not one. The obvious implementation — a single window spanning
// card + gap + launcher — turns the empty L-shaped region beside the launcher
// into a click trap: an OS window swallows the mouse over its transparent
// pixels just as readily as over its painted ones, so ~320x70 of the editor's
// bottom-right corner would stop responding. Sizing each window to exactly the
// thing it draws is what keeps the gap genuinely empty.
//
// Both are borderless, per-pixel-transparent and parented natively to the
// editor's root window: that is what lets them float above every tab and
// follow the editor as it moves, resizes, maximises or changes monitor,
// without any docking work. Per-pixel transparency is also what makes the
// rounded corners read as round instead of as grey notches — see
// ResolveTransparency() in the .cpp for what happens when the platform cannot
// provide it.
//
// The chat itself is unchanged: SShintAssistantPanel is embedded whole, in its
// compact chrome mode (no context strip — the card header takes that job).

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Widgets/SCompoundWidget.h"

class SShintAssistantPanel;
class SWindow;

/**
 * The expanded card: header ("AI Assistant" + Clear) over the chat panel.
 * Also the owner of the whole dock's lifecycle — the statics below are the
 * only entry points the rest of the module uses.
 */
class SShintAssistantDock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintAssistantDock) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// ── Module entry points ──────────────────────────────────────────────────

	/** Show the dock expanded, creating it on first use, and focus the
	 *  composer — every caller is a user about to type. */
	static void Open();

	/** Expand ⇄ collapse, creating the dock on first use. What the Window menu
	 *  entry and the launcher's own click both call. */
	static void Toggle();

	/** Collapse to the launcher without destroying it. */
	static void Collapse();

	/** Remove the dock entirely, launcher included. Right-clicking the
	 *  launcher and ShutdownModule both land here; a Slate window that
	 *  outlives its module leaves a dangling widget the editor keeps painting.
	 */
	static void Shutdown();

	/** True while the card is on screen. */
	static bool IsExpanded();

private:
	TSharedRef<SWidget> BuildHeader();

	FReply OnClearClicked();

	TSharedPtr<SShintAssistantPanel> Panel;

	friend class SShintAssistantLauncher;
};

/**
 * The circular launcher. Its own widget rather than an SButton because it has
 * to answer the right mouse button too: left toggles the card, right removes
 * the dock. Without that second gesture the only way to get the button off
 * screen would be to unload the plugin.
 */
class SShintAssistantLauncher : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintAssistantLauncher) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
	                                 const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
	                               const FPointerEvent& MouseEvent) override;
};
