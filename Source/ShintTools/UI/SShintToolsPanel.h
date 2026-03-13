// Copyright ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"

// Forward declarations
class FShintCoreClient;
class FCoreProcessManager;
struct FShintRequestResult;

/**
 * ECoreStatus
 * Tracks the last known connectivity status of the Core Engine.
 */
enum class ECoreStatus : uint8
{
	/** Not yet checked */
	Unknown,

	/** GET /health returned 2xx */
	Online,

	/** Connection refused or timeout */
	Offline,

	/** Waiting for an in-flight HTTP request */
	Checking,
};

/**
 * SShintToolsPanel
 *
 * Main Slate widget for the ShintTools Editor Panel.
 *
 * Layout:
 *   ┌──────────────────────────────────────────┐
 *   │  ● ShintTools Control Panel              │
 *   ├──────────────────────────────────────────┤
 *   │  Status: ● Online / ● Offline / Unknown  │
 *   ├──────────────────────────────────────────┤
 *   │  [ Check Core Engine ]                   │
 *   │  [ Start Core Engine ]                   │
 *   │  [ Ping API ]                            │
 *   ├──────────────────────────────────────────┤
 *   │  Output:                                 │
 *   │  ┌────────────────────────────────────┐  │
 *   │  │  (scrollable multiline log area)   │  │
 *   │  └────────────────────────────────────┘  │
 *   └──────────────────────────────────────────┘
 */
class SShintToolsPanel : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SShintToolsPanel) {}
	SLATE_END_ARGS()

	/** Constructs the widget. Called by Slate macro expansion. */
	void Construct(const FArguments& InArgs);

	/** SWidget destructor - performs cleanup */
	virtual ~SShintToolsPanel() override;

private:

	// ── Button Handlers ───────────────────────────────────────────────────────

	/** Fires GET /health to the Core Engine */
	FReply OnCheckCoreEngineClicked();

	/** Attempts to start the Core Engine process */
	FReply OnStartCoreEngineClicked();

	/** Fires GET /ping to the Core Engine */
	FReply OnPingCoreClicked();

	// ── HTTP Response Handlers ────────────────────────────────────────────────

	/** Called when the health check request completes */
	void OnHealthCheckComplete(const FShintRequestResult& Result);

	/** Called when the ping request completes */
	void OnPingComplete(const FShintRequestResult& Result);

	// ── UI Helpers ────────────────────────────────────────────────────────────

	/** Appends a timestamped line to the output log area */
	void AppendLog(const FString& Message);

	/** Appends a line and also emits it via UE_LOG */
	void AppendLogAndUELog(const FString& Message, bool bIsWarning = false);

	/** Updates the status indicator dot color and label */
	void SetCoreStatus(ECoreStatus NewStatus);

	/** Returns the display color for the status dot */
	FSlateColor GetStatusDotColor() const;

	/** Returns the text label for the status */
	FText GetStatusText() const;

	/** Returns the human-readable time prefix: [HH:MM:SS] */
	static FString GetTimePrefix();

	// ── Widget Factories ──────────────────────────────────────────────────────

	/** Builds the top header row */
	TSharedRef<SWidget> BuildHeaderRow();

	/** Builds the status indicator row */
	TSharedRef<SWidget> BuildStatusRow();

	/** Builds the action buttons column */
	TSharedRef<SWidget> BuildButtonsSection();

	/** Builds the scrollable output log area */
	TSharedRef<SWidget> BuildOutputSection();

	// ── State ─────────────────────────────────────────────────────────────────

	/** HTTP client - owns all communication with the Core Engine */
	TSharedPtr<FShintCoreClient> CoreClient;

	/** Process manager - handles launching the Core Engine */
	TSharedPtr<FCoreProcessManager> ProcessManager;

	/** Current connectivity status */
	ECoreStatus CurrentStatus = ECoreStatus::Unknown;

	/** Text accumulator for the output log */
	FString LogBuffer;

	// ── Slate Widget References ───────────────────────────────────────────────

	/** Multiline output text box - we keep a reference to update its content */
	TSharedPtr<SMultiLineEditableTextBox> OutputTextBox;

	/** Scroll box wrapping the output - used to auto-scroll to bottom */
	TSharedPtr<SScrollBox> OutputScrollBox;
};
