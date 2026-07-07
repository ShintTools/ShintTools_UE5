// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

/**
 * SShintLauncherWelcomeDialog
 *
 * One-time welcome shown on the Fab marketplace build that promotes
 * downloading the ShintTools launcher for full platform access / upgrades.
 * The plugin dropped straight into a project from Fab has no launcher, so
 * this funnels the user to the dashboard to get it.
 *
 * Persists dismissal to <ProjectDir>/Saved/ShintTools/launcher_welcome.txt so
 * it appears once and never nags again.
 *
 * Only invoked from FShintToolsModule::StartupModule under
 * #if SHINT_MARKETPLACE_BUILD; paid and launcher-free builds never call it
 * (they show the generic tier welcome instead).
 */
class SShintLauncherWelcomeDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintLauncherWelcomeDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Open the welcome once per project. No-op after the first dismissal
	 * (persisted). Must be called on the Game Thread.
	 */
	static void MaybeShow();

private:

	static bool HasBeenShown();
	static void MarkShown();

	FReply OnGetLauncherClicked();
	FReply OnDismissClicked();

	TSharedPtr<SWindow> ParentWindow;
};
