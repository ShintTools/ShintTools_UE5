// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

class SShintLauncherWelcomeDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintLauncherWelcomeDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void MaybeShow();

private:

	static bool HasBeenShown();
	static void MarkShown();

	FReply OnGetLauncherClicked();
	FReply OnDismissClicked();

	TSharedPtr<SWindow> ParentWindow;
};
