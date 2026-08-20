// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

class SShintWelcomeDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintWelcomeDialog) {}
		SLATE_ARGUMENT(FString, Tier)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static bool HasBeenShown();

	static void MaybeShowForTier(const FString& Tier);

private:

	FReply OnDismissClicked();

	static void MarkShown();

	FString             Tier;
	TSharedPtr<SWindow> ParentWindow;
};
