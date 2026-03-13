// Copyright ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Styling/AppStyle.h"
#include "Misc/DateTime.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Constructing UI panel."));

	// Instantiate the HTTP client and process manager
	CoreClient = MakeShared<FShintCoreClient>();
	ProcessManager = MakeShared<FCoreProcessManager>();

	// Build the widget tree
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f))
		[
			SNew(SVerticalBox)

			// ── Header ────────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				BuildHeaderRow()
			]

			// ── Separator ─────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			// ── Status Row ────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				BuildStatusRow()
			]

			// ── Action Buttons ────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				BuildButtonsSection()
			]

			// ── Separator ─────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			// ── Output Log ────────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildOutputSection()
			]
		]
	];

	AppendLog(TEXT("ShintTools Control Panel initialized."));
	AppendLog(FString::Printf(TEXT("Core Engine URL: %s"), *CoreClient->GetConfig().GetBaseUrl()));
}

SShintToolsPanel::~SShintToolsPanel()
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Destroyed."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget Factories
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintToolsPanel::BuildHeaderRow()
{
	return SNew(SHorizontalBox)

		// Plugin logo/icon placeholder
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HeaderIcon", "⚙"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
		]

		// Title
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HeaderTitle", "ShintTools Control Panel"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		]

		// Version label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("HeaderVersion", "v1.0.0"))
			.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildStatusRow()
{
	return SNew(SHorizontalBox)

		// Status label
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StatusLabel", "Core Engine Status:"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]

		// Status dot (colored circle indicator)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			// Unicode full circle used as status indicator dot
			.Text(LOCTEXT("StatusDot", "●"))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
			.ColorAndOpacity(TAttribute<FSlateColor>::Create(
				TAttribute<FSlateColor>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusDotColor)))
		]

		// Status text
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(
				TAttribute<FText>::FGetter::CreateSP(this, &SShintToolsPanel::GetStatusText)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildButtonsSection()
{
	// Common button padding
	const FMargin ButtonPadding(0.0f, 0.0f, 0.0f, 8.0f);
	const FVector2D ButtonSize(220.0f, 32.0f);

	return SNew(SVerticalBox)

		// ── Check Core Engine ─────────────────────────────────────────────────
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ButtonPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(16.0f, 4.0f))
			.OnClicked(this, &SShintToolsPanel::OnCheckCoreEngineClicked)
			.ToolTipText(LOCTEXT("CheckCoreTooltip", "Send GET /health to the Core Engine and report status."))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CheckIcon", "🔍"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CheckCoreLabel", "Check Core Engine"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				]
			]
		]

		// ── Start Core Engine ─────────────────────────────────────────────────
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ButtonPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(16.0f, 4.0f))
			.OnClicked(this, &SShintToolsPanel::OnStartCoreEngineClicked)
			.ToolTipText(LOCTEXT("StartCoreTooltip", "Launch the Core Engine process (python main.py or docker)."))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StartIcon", "▶"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StartCoreLabel", "Start Core Engine"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				]
			]
		]

		// ── Ping API ──────────────────────────────────────────────────────────
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ButtonPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(16.0f, 4.0f))
			.OnClicked(this, &SShintToolsPanel::OnPingCoreClicked)
			.ToolTipText(LOCTEXT("PingTooltip", "Send GET /ping as a lightweight round-trip test."))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PingIcon", "📡"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PingAPILabel", "Ping API"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				]
			]
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildOutputSection()
{
	return SNew(SVerticalBox)

		// Label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OutputLabel", "Output:"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]

		// Scrollable text area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(4.0f))
			[
				SAssignNew(OutputScrollBox, SScrollBox)
				.ScrollBarAlwaysVisible(false)
				.Orientation(Orient_Vertical)

				+ SScrollBox::Slot()
				[
					SAssignNew(OutputTextBox, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.BackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
					.ForegroundColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
					.Text(FText::FromString(TEXT("")))
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Button Handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnCheckCoreEngineClicked()
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Check Core Engine clicked."));
	AppendLog(TEXT("→ Checking Core Engine health..."));

	SetCoreStatus(ECoreStatus::Checking);

	CoreClient->CheckHealth(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnHealthCheckComplete));

	return FReply::Handled();
}

FReply SShintToolsPanel::OnStartCoreEngineClicked()
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Start Core Engine clicked."));
	AppendLog(TEXT("→ Attempting to start Core Engine..."));

	// Check if it's already running first
	if (ProcessManager->IsCoreRunning())
	{
		AppendLog(FString::Printf(
			TEXT("  Core Engine process already running (PID=%u)."), ProcessManager->GetCorePID()));
		return FReply::Handled();
	}

	// Resolve script path
	const FString ScriptPath = FCoreProcessManager::ResolveCoreScriptPath();

	uint32 OutPID = 0;
	const bool bLaunched = ProcessManager->StartCoreEngine(
		ECoreStartMode::PythonScript,
		ScriptPath,
		OutPID);

	if (bLaunched)
	{
		AppendLog(FString::Printf(TEXT("  ✔ Core Engine launched. PID=%u"), OutPID));
		AppendLog(TEXT("  Waiting for Core Engine to become ready..."));

		// Give the process a moment then fire a health check
		// In a production plugin you'd use a timer or retry loop
		SetCoreStatus(ECoreStatus::Checking);
	}
	else
	{
		AppendLogAndUELog(TEXT("  ✘ Failed to launch Core Engine. Check log for details."), /*bIsWarning=*/true);
		SetCoreStatus(ECoreStatus::Offline);
	}

	return FReply::Handled();
}

FReply SShintToolsPanel::OnPingCoreClicked()
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Ping API clicked."));
	AppendLog(TEXT("→ Pinging Core Engine..."));

	CoreClient->Ping(FOnShintRequestComplete::CreateSP(
		this, &SShintToolsPanel::OnPingComplete));

	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP Response Handlers
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::OnHealthCheckComplete(const FShintRequestResult& Result)
{
	if (Result.bSuccess)
	{
		SetCoreStatus(ECoreStatus::Online);
		AppendLog(FString::Printf(
			TEXT("  ✔ Core Engine is ONLINE. HTTP %d"), Result.StatusCode));
		AppendLog(FString::Printf(
			TEXT("  Response: %s"), *Result.ResponseBody));
	}
	else
	{
		SetCoreStatus(ECoreStatus::Offline);
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Core Engine is OFFLINE. %s"), *Result.ErrorMessage),
			/*bIsWarning=*/true);
	}
}

void SShintToolsPanel::OnPingComplete(const FShintRequestResult& Result)
{
	if (Result.bSuccess)
	{
		AppendLog(FString::Printf(
			TEXT("  ✔ Ping OK. HTTP %d | Response: %s"),
			Result.StatusCode, *Result.ResponseBody));
	}
	else
	{
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Ping FAILED. %s"), *Result.ErrorMessage),
			/*bIsWarning=*/true);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Helpers
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::AppendLog(const FString& Message)
{
	const FString Line = GetTimePrefix() + TEXT(" ") + Message;

	if (!LogBuffer.IsEmpty())
	{
		LogBuffer += TEXT("\n");
	}
	LogBuffer += Line;

	if (OutputTextBox.IsValid())
	{
		OutputTextBox->SetText(FText::FromString(LogBuffer));
	}

	// Auto-scroll to the bottom
	if (OutputScrollBox.IsValid())
	{
		OutputScrollBox->ScrollToEnd();
	}
}

void SShintToolsPanel::AppendLogAndUELog(const FString& Message, bool bIsWarning)
{
	AppendLog(Message);

	if (bIsWarning)
	{
		UE_LOG(LogShintTools, Warning, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogShintTools, Log, TEXT("%s"), *Message);
	}
}

void SShintToolsPanel::SetCoreStatus(ECoreStatus NewStatus)
{
	CurrentStatus = NewStatus;

	// Trigger a slate repaint to update the status indicator
	Invalidate(EInvalidateWidget::Paint);
}

FSlateColor SShintToolsPanel::GetStatusDotColor() const
{
	switch (CurrentStatus)
	{
	case ECoreStatus::Online:
		// Green
		return FSlateColor(FLinearColor(0.0f, 0.85f, 0.2f, 1.0f));

	case ECoreStatus::Offline:
		// Red
		return FSlateColor(FLinearColor(0.85f, 0.1f, 0.1f, 1.0f));

	case ECoreStatus::Checking:
		// Yellow / amber
		return FSlateColor(FLinearColor(0.95f, 0.75f, 0.0f, 1.0f));

	case ECoreStatus::Unknown:
	default:
		// Gray
		return FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
	}
}

FText SShintToolsPanel::GetStatusText() const
{
	switch (CurrentStatus)
	{
	case ECoreStatus::Online:
		return LOCTEXT("StatusOnline", "Online");

	case ECoreStatus::Offline:
		return LOCTEXT("StatusOffline", "Offline");

	case ECoreStatus::Checking:
		return LOCTEXT("StatusChecking", "Checking...");

	case ECoreStatus::Unknown:
	default:
		return LOCTEXT("StatusUnknown", "Unknown (click Check)");
	}
}

FString SShintToolsPanel::GetTimePrefix()
{
	const FDateTime Now = FDateTime::Now();
	return FString::Printf(TEXT("[%02d:%02d:%02d]"),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

#undef LOCTEXT_NAMESPACE
