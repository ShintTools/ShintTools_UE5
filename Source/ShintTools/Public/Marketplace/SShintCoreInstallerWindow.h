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

/**
 * SShintCoreInstallerWindow
 *
 * Modeless Slate window that drives the Core Engine first-time setup
 * wizard. Owns a worker thread that runs FShintCoreInstaller::Run() and
 * marshals progress events to the Game Thread for rendering.
 *
 * Opens only when no local Core is detected at startup.
 *
 * Lifecycle:
 *   - OpenIfNeededAsync(): static entrypoint, called from StartupModule.
 *     Checks IsCoreHealthy on a background thread; if not healthy, opens
 *     the window on the Game Thread.
 *   - User clicks "Install" -> worker thread starts.
 *   - On Done / Failed / DockerMissing the window swaps the primary
 *     button to "Close" (or "Open Docker Desktop website" on missing).
 */
class SShintCoreInstallerWindow
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintCoreInstallerWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SShintCoreInstallerWindow();

	/**
	 * Static helper: probe Core on a worker thread; if not healthy, open
	 * the wizard window on the Game Thread. Safe to call from any
	 * thread, returns immediately. Called from FShintToolsModule
	 * StartupModule on marketplace builds.
	 */
	static void OpenIfNeededAsync(int32 Port);

	/** Show the wizard immediately (manual entry from the panel). */
	static void OpenNow();

private:

	void OnProgress(const FShintInstallProgress& Progress);
	FReply OnPrimaryClicked();
	FReply OnCancelClicked();

	/** Append a log line on the Game Thread. */
	void AppendLog(const FString& Line);

	/** Refresh the visible state after Step/Percent changes. */
	void RefreshFromState();

	/**
	 * Worker thread entrypoint -- runs the installer.
	 *
	 * Static and keyed off a weak pointer on purpose: a docker pull can take
	 * minutes, and the user closing the window must not block the game
	 * thread waiting for it to finish. This function (and everything it
	 * schedules back onto the Game Thread) only ever touches the widget
	 * through WeakSelf.Pin() -- if the widget is already gone it bails
	 * silently instead of dereferencing a dangling `this`.
	 */
	static void RunWorker(TWeakPtr<SShintCoreInstallerWindow> WeakSelf);

	TSharedPtr<SWindow>           ParentWindow;
	TSharedPtr<STextBlock>        StatusLabel;
	TSharedPtr<SProgressBar>      ProgressBar;
	TSharedPtr<STextBlock>        PrimaryButtonLabel;
	TSharedPtr<SListView<TSharedPtr<FString>>> LogList;

	TArray<TSharedPtr<FString>>   LogLines;

	/** State machine -- read on Game Thread, written via AsyncTask. */
	EShintInstallStep CurrentStep = EShintInstallStep::CheckingDocker;
	int32             CurrentPercent = 0;
	bool              bIsRunning = false;

	/**
	 * The worker future. The dtor does NOT wait on it -- Async(Thread, ...)
	 * keeps the task running independently of this TFuture handle, so
	 * dropping it here is a fire-and-forget, not a cancel. Kept mainly so
	 * OnPrimaryClicked has somewhere to stash the handle.
	 */
	TFuture<void>     WorkerFuture;
};
