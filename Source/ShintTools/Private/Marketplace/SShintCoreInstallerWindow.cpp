// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintCoreInstallerWindow.h"
#include "SShintConsentDialog.h"
#include "ShintTools.h"

#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "SShintCoreInstallerWindow"

namespace
{
	FText StepToText(EShintInstallStep Step)
	{
		switch (Step)
		{
		case EShintInstallStep::CheckingDocker:
			return LOCTEXT("StepDocker", "Checking Docker...");
		case EShintInstallStep::DockerMissing:
			return LOCTEXT("StepDockerMissing", "Docker Desktop not found");
		case EShintInstallStep::PullingImage:
			return LOCTEXT("StepPull", "Downloading Core Engine image...");
		case EShintInstallStep::CreatingContainer:
			return LOCTEXT("StepContainer", "Starting Core Engine...");
		case EShintInstallStep::WaitingForHealth:
			return LOCTEXT("StepHealth", "Waiting for Core Engine...");
		case EShintInstallStep::Done:
			return LOCTEXT("StepDone", "Core Engine is ready.");
		case EShintInstallStep::Failed:
			return LOCTEXT("StepFailed", "Installation failed.");
		default:
			return LOCTEXT("StepIdle", "Ready to install.");
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct / dtor
// ─────────────────────────────────────────────────────────────────────────────

void SShintCoreInstallerWindow::Construct(const FArguments& InArgs)
{
	AppendLog(TEXT("ShintTools Core Engine setup."));
	// One logical sentence per AppendLog call — the row's AutoWrapText handles
	// reflow at the current dialog width, so we don't pre-wrap with C++ string
	// concatenation.
	AppendLog(TEXT("This downloads the Core Engine Docker image (~600 MB) and starts it locally on port 18200."));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.f)
		[
			SNew(SVerticalBox)

			// Header
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "ShintTools - First-time Setup"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]

			// Status line. AutoWrapText so terminal-state messages like
			// "Failed to start container: docker daemon not running…" wrap
			// instead of running off the right edge of the dialog.
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SAssignNew(StatusLabel, STextBlock)
				.Text(StepToText(EShintInstallStep::CheckingDocker))
				.AutoWrapText(true)
			]

			// Progress bar
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(ProgressBar, SProgressBar)
				.Percent(0.f)
			]

			// Log scroll
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 4, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(4.f)
				[
					SAssignNew(LogList, SListView<TSharedPtr<FString>>)
					.ListItemsSource(&LogLines)
					.OnGenerateRow_Lambda(
						[](TSharedPtr<FString> Item,
						   const TSharedRef<STableViewBase>& Owner)
						{
							// AutoWrapText is required here: docker pull
							// emits long lines including image digests +
							// status URLs that exceed the dialog width.
							// Without wrapping, those rows get clipped at
							// the right edge and the user only ever sees
							// the first half of the message.
							return SNew(STableRow<TSharedPtr<FString>>, Owner)
								[
									SNew(STextBlock)
									.Text(FText::FromString(*Item))
									.AutoWrapText(true)
								];
						})
				]
			]

			// Buttons
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(this, &SShintCoreInstallerWindow::OnCancelClicked)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.OnClicked(this, &SShintCoreInstallerWindow::OnPrimaryClicked)
					[
						SAssignNew(PrimaryButtonLabel, STextBlock)
						.Text(LOCTEXT("Install", "Install"))
					]
				]
			]
		]
	];
}

SShintCoreInstallerWindow::~SShintCoreInstallerWindow()
{
	// Wait for the worker thread to finish before the widget is freed
	// -- the worker captures `this` and posts back to the Game Thread.
	if (WorkerFuture.IsValid())
	{
		WorkerFuture.Wait();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

void SShintCoreInstallerWindow::OpenIfNeededAsync(int32 Port)
{
	// Probe Core on a worker thread to avoid blocking the editor at
	// startup. The probe has its own 3s deadline inside IsCoreHealthy.
	Async(EAsyncExecution::Thread, [Port]()
	{
		const bool bHealthy = FShintCoreInstaller::IsCoreHealthy(Port);
		if (bHealthy)
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("[CoreInstaller] Core already healthy on %d"
				     " -- skipping setup wizard."), Port);
			return;
		}

		AsyncTask(ENamedThreads::GameThread, []()
		{
			// Fab guidelines require explicit consent before any
			// download or third-party process is launched. Skip the
			// dialog only if the user already accepted in a prior
			// session.
			if (SShintConsentDialog::HasUserConsented())
			{
				OpenNow();
				return;
			}
			SShintConsentDialog::OpenModal([]()
			{
				OpenNow();
			});
		});
	});
}

void SShintCoreInstallerWindow::OpenNow()
{
	check(IsInGameThread());

	const TSharedRef<SWindow> Window =
		SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "ShintTools Setup"))
		.ClientSize(FVector2D(640.f, 480.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	const TSharedRef<SShintCoreInstallerWindow> Content =
		SNew(SShintCoreInstallerWindow);
	Content->ParentWindow = Window;
	Window->SetContent(Content);

	FSlateApplication::Get().AddWindow(Window, /*bShowImmediately=*/ true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Button handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintCoreInstallerWindow::OnPrimaryClicked()
{
	// Terminal states swap the primary button to "Close" / "Open URL".
	if (CurrentStep == EShintInstallStep::Done)
	{
		if (ParentWindow.IsValid()) { ParentWindow->RequestDestroyWindow(); }
		return FReply::Handled();
	}
	if (CurrentStep == EShintInstallStep::DockerMissing)
	{
		FPlatformProcess::LaunchURL(
			TEXT("https://www.docker.com/products/docker-desktop"),
			nullptr, nullptr);
		return FReply::Handled();
	}
	if (CurrentStep == EShintInstallStep::Failed)
	{
		// Allow retry: reset state and re-run.
		LogLines.Reset();
		if (LogList.IsValid()) { LogList->RequestListRefresh(); }
	}

	if (bIsRunning)
	{
		return FReply::Handled();
	}

	bIsRunning = true;
	PrimaryButtonLabel->SetText(LOCTEXT("Installing", "Installing..."));

	// Spawn worker.
	WorkerFuture = Async(EAsyncExecution::Thread, [this]()
	{
		RunWorker();
	});

	return FReply::Handled();
}

FReply SShintCoreInstallerWindow::OnCancelClicked()
{
	// We don't actively kill the worker (docker pull is hard to interrupt
	// cleanly). Just close the window -- the dtor waits for the worker.
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker / progress
// ─────────────────────────────────────────────────────────────────────────────

void SShintCoreInstallerWindow::RunWorker()
{
	FShintCoreInstaller Installer;
	Installer.OnProgress = [this](const FShintInstallProgress& P)
	{
		// Marshal to the Game Thread -- Slate is not thread-safe.
		FShintInstallProgress Copy = P;
		AsyncTask(ENamedThreads::GameThread, [this, Copy]()
		{
			OnProgress(Copy);
		});
	};
	Installer.Run();
}

void SShintCoreInstallerWindow::OnProgress(const FShintInstallProgress& P)
{
	CurrentStep    = P.Step;
	CurrentPercent = P.Percent;
	AppendLog(P.Message);
	RefreshFromState();

	const bool bTerminal =
		P.Step == EShintInstallStep::Done ||
		P.Step == EShintInstallStep::Failed ||
		P.Step == EShintInstallStep::DockerMissing;
	if (bTerminal)
	{
		bIsRunning = false;
	}
}

void SShintCoreInstallerWindow::AppendLog(const FString& Line)
{
	LogLines.Add(MakeShared<FString>(Line));
	if (LogList.IsValid())
	{
		LogList->RequestListRefresh();
		LogList->ScrollToBottom();
	}
}

void SShintCoreInstallerWindow::RefreshFromState()
{
	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(StepToText(CurrentStep));
	}
	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(FMath::Clamp(CurrentPercent, 0, 100) / 100.f);
	}
	if (PrimaryButtonLabel.IsValid())
	{
		FText Label;
		switch (CurrentStep)
		{
		case EShintInstallStep::Done:
			Label = LOCTEXT("Close", "Close"); break;
		case EShintInstallStep::Failed:
			Label = LOCTEXT("Retry", "Retry"); break;
		case EShintInstallStep::DockerMissing:
			Label = LOCTEXT("OpenDocker", "Get Docker Desktop"); break;
		default:
			Label = bIsRunning
				? LOCTEXT("Installing", "Installing...")
				: LOCTEXT("Install", "Install");
		}
		PrimaryButtonLabel->SetText(Label);
	}
}

#undef LOCTEXT_NAMESPACE
