// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

/**
 * SShintWelcomeDialog
 *
 * One-time onboarding shown to paid (Indie / Studio / Enterprise) users the
 * first time they open the ShintTools panel. It summarises what the tier
 * unlocks and how to get started. Free users never see it — MaybeShowForTier
 * is a no-op for the free tier.
 *
 * Persists a "shown" marker to <ProjectDir>/Saved/ShintTools/welcome.txt so it
 * appears once, not on every editor restart. Non-modal so it never blocks the
 * editor and needs no active parent window at startup.
 */
class SShintWelcomeDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintWelcomeDialog) {}
		SLATE_ARGUMENT(FString, Tier)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** True if the welcome was already shown in a prior session. */
	static bool HasBeenShown();

	/**
	 * Show the welcome dialog once, only for paid tiers. No-op for the free
	 * tier or if it has already been shown. Safe to call on every panel open
	 * — the persisted marker makes it idempotent.
	 */
	static void MaybeShowForTier(const FString& Tier);

private:

	FReply OnDismissClicked();

	static void MarkShown();

	FString             Tier;
	TSharedPtr<SWindow> ParentWindow;
};
