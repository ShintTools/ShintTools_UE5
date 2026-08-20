// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;

class SShintConsentDialog
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintConsentDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static bool HasUserConsented();

	static void OpenModal(TFunction<void()> OnAccept);

private:

	FReply OnAcceptClicked();
	FReply OnDeclineClicked();

	void PersistAcceptance();

	TSharedPtr<SWindow>   ParentWindow;
	TFunction<void()>     AcceptCallback;
};
