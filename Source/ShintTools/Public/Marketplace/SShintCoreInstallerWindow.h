// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "ShintCoreInstaller.h"

class SWindow;
class STextBlock;
class SButton;
class SProgressBar;

class SShintCoreInstallerWindow
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintCoreInstallerWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SShintCoreInstallerWindow();

	static void OpenIfNeededAsync(int32 Port);

	static void OpenNow();

private:

	void OnProgress(const FShintInstallProgress& Progress);
	FReply OnPrimaryClicked();
	FReply OnCancelClicked();

	void AppendLog(const FString& Line);

	void RefreshFromState();

	static void RunWorker(TWeakPtr<SShintCoreInstallerWindow> WeakSelf);

	TSharedPtr<SWindow>           ParentWindow;
	TSharedPtr<STextBlock>        StatusLabel;
	TSharedPtr<SProgressBar>      ProgressBar;
	TSharedPtr<STextBlock>        PrimaryButtonLabel;
	TSharedPtr<SListView<TSharedPtr<FString>>> LogList;

	TArray<TSharedPtr<FString>>   LogLines;

	EShintInstallStep CurrentStep = EShintInstallStep::CheckingDocker;
	int32             CurrentPercent = 0;
	bool              bIsRunning = false;

	TFuture<void>     WorkerFuture;
};
