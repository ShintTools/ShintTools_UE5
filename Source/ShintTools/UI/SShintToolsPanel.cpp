// Copyright ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "CoreProcessManager.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Styling/AppStyle.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

void SShintToolsPanel::Construct(const FArguments& InArgs)
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Constructing UI panel."));

	// Instantiate the HTTP client and process manager
	CoreClient    = MakeShared<FShintCoreClient>();
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
	const FMargin ButtonPadding(0.0f, 0.0f, 0.0f, 8.0f);

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
				SNew(STextBlock)
				.Text(LOCTEXT("CheckCoreLabel", "Check Core Engine"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
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
			.ToolTipText(LOCTEXT("StartCoreTooltip", "Launch the Core Engine process."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StartCoreLabel", "Start Core Engine"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
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
				SNew(STextBlock)
				.Text(LOCTEXT("PingLabel", "Ping API"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			]
		]

		// ── Separator ─────────────────────────────────────────────────────────
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 8.0f)
		[
			SNew(SSeparator)
		]

		// ── Validate Code section ─────────────────────────────────────────────
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildValidateSection()
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildValidateSection()
{
	return SNew(SVerticalBox)

		// Section label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ValidateSectionLabel", "Validate Code"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		// File path label + input row
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FilePathLabel", "File:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(FilePathInputBox, SEditableTextBox)
				.HintText(LOCTEXT("FilePathHint", "Source/MyGame/PlayerController.cpp"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ToolTipText(LOCTEXT("FilePathTooltip",
					"Path relative to the project root. The plugin reads this file "
					"and sends its content to POST /validate/code."))
			]
		]

		// Validate button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(16.0f, 4.0f))
			.OnClicked(this, &SShintToolsPanel::OnValidateCodeClicked)
			.ToolTipText(LOCTEXT("ValidateTooltip",
				"Read the file at the path above and send it to POST /validate/code on the Core Engine."))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ValidateLabel", "Validate Code"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			]
		];
}

TSharedRef<SWidget> SShintToolsPanel::BuildOutputSection()
{
	return SNew(SVerticalBox)

		// "Output:" label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OutputLabel", "Output:"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]

		// Scrollable log area
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(OutputScrollBox, SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			[
				SAssignNew(OutputTextBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(false)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.BackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
				.ForegroundColor(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f))
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

	if (ProcessManager->IsCoreRunning())
	{
		AppendLog(FString::Printf(
			TEXT("  Core Engine process already running (PID=%u)."), ProcessManager->GetCorePID()));
		return FReply::Handled();
	}

	//const FString ScriptPath = FCoreProcessManager::ResolveCoreScriptPath();

	uint32 OutPID = 0;
	const bool bLaunched = ProcessManager->StartCoreEngine(
		ECoreStartMode::Docker,
		OutPID);

	if (bLaunched)
	{
		AppendLog(FString::Printf(TEXT("  ✔ Core Engine launched. PID=%u"), OutPID));
		AppendLog(TEXT("  Core Engine successfully launched!"));
		SetCoreStatus(ECoreStatus::Online);
	}
	else
	{
		AppendLogAndUELog(TEXT("  ✘ Failed to launch Core Engine. Check log for details."), true);
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

FReply SShintToolsPanel::OnValidateCodeClicked()
{
	UE_LOG(LogShintTools, Log, TEXT("SShintToolsPanel: Validate Code clicked."));

	// ── Read file path from the input box ─────────────────────────────────────
	if (!FilePathInputBox.IsValid() || FilePathInputBox->GetText().IsEmpty())
	{
		AppendLogAndUELog(TEXT("  ✘ Please enter a file path before validating."), true);
		return FReply::Handled();
	}

	const FString RelativePath = FilePathInputBox->GetText().ToString();
	const FString AbsolutePath = FPaths::Combine(FPaths::ProjectDir(), RelativePath);

	AppendLog(FString::Printf(TEXT("→ Validating: %s"), *RelativePath));

	if (!FPaths::FileExists(AbsolutePath))
	{
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ File not found: %s"), *AbsolutePath),
			/*bIsWarning=*/true);
		return FReply::Handled();
	}

	// ── Read file content ─────────────────────────────────────────────────────
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
	{
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Failed to read file: %s"), *AbsolutePath),
			/*bIsWarning=*/true);
		return FReply::Handled();
	}

	AppendLog(FString::Printf(TEXT("  File read OK (%d chars). Sending to Core Engine..."),
		FileContent.Len()));

	// ── Send POST /validate/code ──────────────────────────────────────────────
	CoreClient->ValidateCode(
		RelativePath,
		FileContent,
		TEXT("unreal"),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnValidateComplete));

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
		AppendLog(FString::Printf(TEXT("  ✔ Core Engine ONLINE. HTTP %d"), Result.StatusCode));
		AppendLog(FString::Printf(TEXT("  Response: %s"), *Result.ResponseBody));
	}
	else
	{
		SetCoreStatus(ECoreStatus::Offline);
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Core Engine OFFLINE. %s"), *Result.ErrorMessage), true);
	}
}

void SShintToolsPanel::OnPingComplete(const FShintRequestResult& Result)
{
	if (Result.bSuccess)
	{
		AppendLog(FString::Printf(TEXT("  ✔ Ping OK. HTTP %d | %s"),
			Result.StatusCode, *Result.ResponseBody));
	}
	else
	{
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Ping FAILED. %s"), *Result.ErrorMessage), true);
	}
}

void SShintToolsPanel::OnValidateComplete(const FShintValidateResult& Result)
{
	if (!Result.bSuccess)
	{
		AppendLogAndUELog(
			FString::Printf(TEXT("  ✘ Validation FAILED. HTTP %d — %s"),
				Result.StatusCode, *Result.ErrorMessage),
			/*bIsWarning=*/true);
		return;
	}

	// ── Summary ───────────────────────────────────────────────────────────────
	AppendLog(TEXT("  ✔ Validation complete."));
	AppendLog(TEXT("  ┌─ Summary ────────────────────────────────────────────────"));
	AppendLog(FString::Printf(TEXT("  │  Total issues : %d"), Result.TotalIssues));
	AppendLog(FString::Printf(TEXT("  │  Errors       : %d"), Result.TotalErrors));
	AppendLog(FString::Printf(TEXT("  │  Warnings     : %d"), Result.TotalWarnings));
	AppendLog(TEXT("  └─────────────────────────────────────────────────────────"));

	if (Result.Issues.Num() == 0)
	{
		AppendLog(TEXT("  ✔ No issues found."));
		return;
	}

	// ── Per-issue breakdown ───────────────────────────────────────────────────
	AppendLog(FString::Printf(TEXT("  Issues (%d):"), Result.Issues.Num()));

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintCodeIssue& Issue = Result.Issues[i];
		const FString Prefix = (Issue.Severity == TEXT("error")) ? TEXT("✘") : TEXT("⚠");

		AppendLog(FString::Printf(
			TEXT("  %s [%s] %s  (line %d)"),
			*Prefix,
			Issue.RuleId.IsEmpty() ? TEXT("--") : *Issue.RuleId,
			*Issue.Message,
			Issue.Line));
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
	Invalidate(EInvalidateWidget::Paint);
}

FSlateColor SShintToolsPanel::GetStatusDotColor() const
{
	switch (CurrentStatus)
	{
	case ECoreStatus::Online:   return FSlateColor(FLinearColor(0.0f,  0.85f, 0.2f,  1.0f));
	case ECoreStatus::Offline:  return FSlateColor(FLinearColor(0.85f, 0.1f,  0.1f,  1.0f));
	case ECoreStatus::Checking: return FSlateColor(FLinearColor(0.95f, 0.75f, 0.0f,  1.0f));
	default:                    return FSlateColor(FLinearColor(0.5f,  0.5f,  0.5f,  1.0f));
	}
}

FText SShintToolsPanel::GetStatusText() const
{
	switch (CurrentStatus)
	{
	case ECoreStatus::Online:   return LOCTEXT("StatusOnline",   "Online");
	case ECoreStatus::Offline:  return LOCTEXT("StatusOffline",  "Offline");
	case ECoreStatus::Checking: return LOCTEXT("StatusChecking", "Checking...");
	default:                    return LOCTEXT("StatusUnknown",  "Unknown (click Check)");
	}
}

FString SShintToolsPanel::GetTimePrefix()
{
	const FDateTime Now = FDateTime::Now();
	return FString::Printf(TEXT("[%02d:%02d:%02d]"),
		Now.GetHour(), Now.GetMinute(), Now.GetSecond());
}

#undef LOCTEXT_NAMESPACE
