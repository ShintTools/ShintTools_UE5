// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintAssistantPanel — the assistant's single anchored surface (M5).
//
// One nomad tab, dockable to the side of the layout like an assistant
// sidebar, NOT a second free-floating window. It follows SShintToolsPanel's
// internal pattern (context strip + destination rail + SWidgetSwitcher)
// rather than SShintPredictiveDashboard's independent-window pattern,
// because the assistant is meant to sit beside whatever the user is already
// working in.
//
//   Context strip   "Viewing: LOD Audit — 40 findings", read from
//                   FShintAssistantContext, so a question needs no copying
//   Chat            the thread; history persists across editor restarts
//   Memory          confirm / reject / retract remembered facts
//   Rules           activate or discard drafted studio rules
//
// Availability: EVERY tier. Free gets a working two-intent assistant with no
// memory. The rail hides destinations the resolved capabilities do not grant,
// and that resolution comes from GET /assistant/capabilities — never from a
// hardcoded tier table here. This is why the whole file carries no strip
// sentinel: it ships in the free marketplace image too.
//
// Deprecates SShintToolsPanel_Explain.cpp's single-shot modal: that dialog
// answered one finding, kept nothing, and was destroyed on every click.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/ShintCoreClient.h"
#include "Core/ShintAssistantContext.h"

class SVerticalBox;
class SScrollBox;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class STextBlock;
class SButton;

/** Which destination the assistant rail points at. */
enum class EShintAssistantView : uint8
{
	Chat,
	Memory,
	Rules,
};

class SShintAssistantPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintAssistantPanel)
		: _bCompact(false)
	{}
		/** Compact chrome, for the anchored dock (SShintAssistantDock).
		 *
		 *  Two differences, both because the dock draws its own card around
		 *  this widget: the context strip is dropped (the dock's header
		 *  already names the surface, and the context line moves inline above
		 *  the thread), and every section paints no background of its own —
		 *  otherwise a square fill would cover the card's rounded corners. */
		SLATE_ARGUMENT(bool, bCompact)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Drop the thread and start a new conversation. Nothing is deleted
	 *  server-side: confirmed facts and active rules survive: this only stops
	 *  the old thread from being the one new turns continue. */
	void ClearConversation();

	/** Put the caret in the composer. The dock calls this on open, since
	 *  opening an assistant is always a prelude to typing. */
	void FocusComposer();

private:
	// ── Sections ─────────────────────────────────────────────────────────────
	TSharedRef<SWidget> BuildContextStrip();
	TSharedRef<SWidget> BuildRail();
	TSharedRef<SWidget> BuildChatView();
	TSharedRef<SWidget> BuildMemoryView();
	TSharedRef<SWidget> BuildRulesView();
	TSharedRef<SWidget> BuildComposer();

	/** One rail button. Collapsed when the tier does not grant the view. */
	TSharedRef<SWidget> BuildRailButton(EShintAssistantView View, const FText& Label);

	/** The "ask me something" placeholder shown while the thread is empty.
	 *  Replaced by the first real turn, restored by ClearConversation. */
	void ShowEmptyState();

	/** Background for a section, honouring compact mode: transparent there so
	 *  the dock card's rounded corners are not squared off by a fill. */
	FSlateColor SectionBg(const FLinearColor& Opaque) const;

	// ── Chat ─────────────────────────────────────────────────────────────────
	void    SendMessage(const FString& Text, const FString& ForcedIntent);
	FReply  OnSendClicked();
	void    OnComposerCommitted(const FText& Text, ETextCommit::Type CommitType);

	/** Append a bubble to the thread and scroll to it. */
	void AppendTurn(const FString& Role, const FString& Text, const FString& Intent,
	                bool bContinued, bool bDegraded);

	/** The live assistant bubble a stream writes into, so tokens land in place
	 *  instead of appending a new row per chunk. */
	void BeginStreamingTurn();
	void OnStreamChunk(const FString& Chunk, uint64 RequestId);
	void OnMessageComplete(const FShintAssistantResponse& Response, uint64 RequestId);

	/** Confirmation card rendered inline after a remember_fact / define_rule
	 *  turn. Until the user accepts, the proposal is invisible to the rest of
	 *  the system — the card IS the gate, not a notification about one. */
	void AppendFactConfirmCard(const FShintAssistantFact& Fact);
	void AppendRuleConfirmCard(const FShintAssistantRule& Rule);

	/** Re-read after a confirm/reject so the card reflects the new status
	 *  rather than the one the user just acted on. */
	void OnFactConfirmed(const FShintAssistantResponse& Result);
	void OnRuleConfirmed(const FShintAssistantResponse& Result);

	// ── Data ─────────────────────────────────────────────────────────────────
	void RefreshCapabilities();
	void RefreshMemory();
	void RefreshRules();

	/** Context strip label, bound so it follows FShintAssistantContext. */
	FText GetContextLabel() const;

	/** Take a queued "Explain this finding" from a results row and answer it
	 *  in the thread. Fires on FShintAssistantContext::OnChanged. */
	void ConsumePendingExplain();

	/** True when the resolved capabilities grant this view. */
	bool CanUseView(EShintAssistantView View) const;

	// ── State ────────────────────────────────────────────────────────────────
	TSharedPtr<FShintCoreClient> CoreClient;

	FShintAssistantCapabilities Capabilities;
	FString                     ConversationId;

	EShintAssistantView ActiveView = EShintAssistantView::Chat;

	/** See the bCompact argument. Read in Construct and by SectionBg. */
	bool bCompact = false;

	/** The thread currently holds the placeholder, not real turns — so the
	 *  next AppendTurn has to clear it before adding anything. */
	bool bShowingEmptyState = false;

	/** Guards against a slow reply landing in a thread the user has since
	 *  reset, or a second message overtaking the first. Mirrors the
	 *  ExplainRequestId token the modal used. */
	uint64 RequestToken = 0;
	bool   bAwaitingReply = false;

	FString StreamBuffer;

	/** Grounding for the turn being sent right now, when it came from a
	 *  results row's Explain button rather than free text. Cleared once the
	 *  request is built — a later free-text question must not inherit it
	 *  client-side (the server's own inheritance is the correct mechanism). */
	FString PendingExplainRuleId;
	FString PendingExplainAssetPath;

	TArray<FShintAssistantFact> Facts;
	TArray<FShintAssistantRule> Rules;

	// ── Widget handles ───────────────────────────────────────────────────────
	TSharedPtr<SVerticalBox>     ThreadBox;
	TSharedPtr<SScrollBox>       ThreadScroll;
	TSharedPtr<SEditableTextBox> Composer;
	TSharedPtr<STextBlock>       StreamingText;   // the in-progress bubble
	TSharedPtr<STextBlock>       StatusLine;
	TSharedPtr<SVerticalBox>     MemoryList;
	TSharedPtr<SVerticalBox>     RulesList;
};
