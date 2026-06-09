// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

/**
 * SShintConsentDialog
 *
 * One-time privacy consent shown before the Core install wizard runs
 * on the marketplace build. Required by Epic's Fab review guidelines:
 * any plugin that downloads or executes third-party software must
 * obtain explicit user consent and disclose what is downloaded and
 * where it runs.
 *
 * Persists acceptance to <ProjectDir>/Saved/ShintTools/consent.txt so
 * the user is not prompted on every editor restart. The wizard reads
 * HasUserConsented() before opening.
 */
class SShintConsentDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintConsentDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Static helper: returns true if the user previously accepted in
	 * a prior session. False on first run or after they decline.
	 */
	static bool HasUserConsented();

	/**
	 * Show the consent dialog modally. Calls OnAccept on Accept; does
	 * nothing on Decline (caller should handle the "no consent" path).
	 */
	static void OpenModal(TFunction<void()> OnAccept);

private:

	FReply OnAcceptClicked();
	FReply OnDeclineClicked();

	void PersistAcceptance();

	TSharedPtr<SWindow>   ParentWindow;
	TFunction<void()>     AcceptCallback;
};
