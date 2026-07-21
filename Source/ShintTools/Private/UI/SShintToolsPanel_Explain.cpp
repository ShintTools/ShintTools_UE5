// Copyright 2026 ShintTools. All Rights Reserved.
//
// /agent/explain modal — the per-issue LLM explainer dialog launched
// from a row in the Code Validator results. Split out of the main
// panel TU because the modal is independent: it owns its own SWindow,
// ticker handle, and result text box, and the rest of the panel
// doesn't reach into any of those.

#include "SShintToolsPanel.h"
#include "ShintCoreClient.h"
#include "ShintStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SThrobber.h"

#include "Brushes/SlateColorBrush.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{
	// One-shot brush for the modal's dark background. The main panel TU
	// keeps a TMap-backed brush cache (`ST4::Solid`) for re-use across
	// the dozen-plus borders it paints; the explain modal opens once
	// per click and the cost of a static brush here is in the noise.
	const FSlateBrush& ExplainModalBgBrush()
	{
		static FSlateColorBrush B(
			SShintToolsPanel::C_BG());
		return B;
	}
}

bool SShintToolsPanel::TickExplainStatus(float /*DeltaTime*/)
{
	if (!ExplainStatusLine.IsValid()) return false;
	static const TCHAR* Labels[] = {
		TEXT("Analyzing code…"),
		TEXT("Consulting rules…"),
		TEXT("Drafting explanation…"),
	};
	ExplainStatusIndex = (ExplainStatusIndex + 1) % UE_ARRAY_COUNT(Labels);
	ExplainStatusLine->SetText(FText::FromString(Labels[ExplainStatusIndex]));
	return true; // keep ticking until the response arrives
}

FReply SShintToolsPanel::OnExplainIssueClicked(FShintIssueItemPtr Item)
{
	if (!Item.IsValid()) return FReply::Handled();

	// Map the listview item back into a transport-ready FShintCodeIssue.
	// The listview row carries every field we need; the client side does
	// not synthesise rule_name / rule_explanation — those come from the
	// validate response and we forward them verbatim.
	FShintCodeIssue Issue;
	Issue.RuleId          = Item->RuleId;
	Issue.RuleName        = Item->RuleName;
	Issue.RuleExplanation = Item->RuleExplanation;
	Issue.Severity        = Item->Severity;
	Issue.Category        = Item->Category;
	Issue.Message         = Item->Message;
	Issue.FilePath        = Item->FilePath;
	Issue.Line            = Item->Line;
	Issue.Snippet         = Item->Snippet;
	Issue.FixSuggestion   = Item->FixSuggestion;
	Issue.bIsAutoFixable  = Item->bIsAutoFixable;

	// Tear down any previous modal — only one explain in flight at a time.
	if (ExplainWindow.IsValid())
	{
		if (ExplainTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ExplainTickerHandle);
			ExplainTickerHandle.Reset();
		}
		ExplainWindow->RequestDestroyWindow();
		ExplainWindow.Reset();
	}

	ExplainStatusIndex = 0;
	ExplainStreamBuffer.Reset();

	// Token this request so a stale, slow answer for a previously-clicked row
	// can't overwrite the modal that's now showing a different issue.
	const uint64 ThisRequestId = ++ExplainRequestId;

	SAssignNew(ExplainWindow, SWindow)
		.Title(LOCTEXT("ExplainTitle", "ShintTools AI Assistant"))
		.ClientSize(FVector2D(680.f, 460.f))
		.SizingRule(ESizingRule::UserSized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
			.BorderImage(&ExplainModalBgBrush())
			.Padding(FMargin(20.f))
			[
				SNew(SVerticalBox)

				// Header — rule name + severity badge so the customer sees a
				// human label instead of "CS001".
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(
						Item->RuleName.IsEmpty() ? Item->RuleId : Item->RuleName))
					.Font(FShintStyle::Fonts::H2())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
				]

				// Spinner + rotating status text
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 10.f, 0.f)
					[
						SAssignNew(ExplainSpinner, SCircularThrobber)
						.Radius(10.f)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SAssignNew(ExplainStatusLine, STextBlock)
						.Text(LOCTEXT("ExplainStatusBoot", "Analyzing code…"))
						.Font(FShintStyle::Fonts::Body())
						.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
					]
				]

				// Result textbox — read-only, becomes the LLM's answer or
				// the deterministic fallback (message + fix_suggestion).
				// AutoWrapText keeps the LLM output inside the window
				// width — the previous behaviour rendered everything on
				// a single long line and forced the user to scroll right
				// (Daniel reported this as "cero responsive").
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SAssignNew(ExplainResultBox, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.AlwaysShowScrollbars(true)
					.Font(FShintStyle::Fonts::Small())
					.Text(FText::FromString(TEXT("")))
					.BackgroundColor(FSlateColor(FShintStyle::Colors::BgCard()))
				]

				// Close button
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ContentPadding(FMargin(20.f, 6.f))
					// WeakPtr capture: the only strong ref to this panel may
					// be the ticker delegate created below, which `this`
					// captures into via CreateSP. Removing it inside the
					// lambda would otherwise drop the ref count to 0 and
					// the next line would dereference `this->ExplainWindow`
					// on a destroyed panel — see bug-hunt issue #5.
					.OnClicked_Lambda(
						[WeakThis = TWeakPtr<SShintToolsPanel>(SharedThis(this))]() -> FReply
					{
						if (TSharedPtr<SShintToolsPanel> Pin = WeakThis.Pin())
						{
							// Invalidate the in-flight request so a slow answer
							// arriving after Close is dropped, not written into a
							// reopened modal.
							++Pin->ExplainRequestId;
							if (Pin->ExplainTickerHandle.IsValid())
							{
								FTSTicker::GetCoreTicker().RemoveTicker(Pin->ExplainTickerHandle);
								Pin->ExplainTickerHandle.Reset();
							}
							if (Pin->ExplainWindow.IsValid())
							{
								Pin->ExplainWindow->RequestDestroyWindow();
								Pin->ExplainWindow.Reset();
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Close", "Close"))
						.Font(FShintStyle::Fonts::Body())
					]
				]
			]
		];

	FSlateApplication::Get().AddWindow(ExplainWindow.ToSharedRef());

	// Rotate the status text every 8 seconds while the request is in flight.
	ExplainTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SShintToolsPanel::TickExplainStatus),
		8.0f);

	// Stream the explanation: tokens land via OnExplainChunk as they generate
	// (first token in ~3-5s), then OnExplainComplete finalises. Both route on
	// the game thread — the transport marshals SSE chunks there — and both
	// carry the request token so a stale stream can't bleed into a newer modal.
	TWeakPtr<SShintToolsPanel> WeakSelf(SharedThis(this));

	FOnShintStreamChunk OnChunk =
		FOnShintStreamChunk::CreateLambda(
			[WeakSelf, ThisRequestId](const FString& Chunk)
			{
				if (TSharedPtr<SShintToolsPanel> Pinned = WeakSelf.Pin())
					Pinned->OnExplainChunk(Chunk, ThisRequestId);
			});

	FOnShintAgentExplainComplete OnDone =
		FOnShintAgentExplainComplete::CreateLambda(
			[WeakSelf, Item, ThisRequestId]
			(const FShintAgentExplainResponse& Resp)
			{
				if (TSharedPtr<SShintToolsPanel> Pinned = WeakSelf.Pin())
					Pinned->OnExplainComplete(Resp, Item, ThisRequestId);
			});

	CoreClient->RequestExplainIssueStream(Issue, OnChunk, OnDone);
	return FReply::Handled();
}

void SShintToolsPanel::OnExplainChunk(const FString& Chunk, uint64 RequestId)
{
	// Drop chunks from a superseded request (user clicked Explain on another
	// row, or closed the modal, while this stream was still generating).
	if (RequestId != ExplainRequestId)
		return;

	// First token: the model is producing output, so retire the rotating
	// "Analyzing…/Drafting…" ticker and switch the status to a steady label.
	// The spinner keeps turning until the stream completes.
	if (ExplainStreamBuffer.IsEmpty())
	{
		if (ExplainTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(ExplainTickerHandle);
			ExplainTickerHandle.Reset();
		}
		if (ExplainStatusLine.IsValid())
			ExplainStatusLine->SetText(LOCTEXT("ExplainGenerating", "Generating explanation…"));
	}

	ExplainStreamBuffer += Chunk;
	if (ExplainResultBox.IsValid())
		ExplainResultBox->SetText(FText::FromString(ExplainStreamBuffer));
}

void SShintToolsPanel::OnExplainComplete(
	const FShintAgentExplainResponse& Result,
	FShintIssueItemPtr                Item,
	uint64                            RequestId)
{
	// Ignore a stale response: the user clicked Explain on another row (or
	// closed the modal) while this one was still generating. Writing its text
	// now would show the wrong issue's explanation in the current modal.
	if (RequestId != ExplainRequestId)
		return;

	// Stop the rotating ticker and hide the spinner; the wait is over.
	if (ExplainTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ExplainTickerHandle);
		ExplainTickerHandle.Reset();
	}
	if (ExplainSpinner.IsValid())
		ExplainSpinner->SetVisibility(EVisibility::Collapsed);

	FString FinalText;
	FString StatusText;

	if (Result.bSuccess && !Result.Explanation.IsEmpty())
	{
		FinalText  = Result.Explanation;
		StatusText = FString::Printf(
			TEXT("Generated in %.1fs"), Result.GenerationSeconds);
	}
	else
	{
		// Fallback path — show the deterministic message + fix_suggestion
		// from the issue itself (already human-readable text). Customer
		// never sees a raw "Error: LLM unavailable" screen.
		FString Header;
		if (Result.Tier == TEXT("free"))
		{
			Header = TEXT(
				"Issue Explain requires the Indie tier. Showing the "
				"deterministic suggestion instead:\n\n");
		}
		else if (!Result.ErrorMessage.IsEmpty())
		{
			Header = TEXT(
				"Could not reach the LLM. Showing the deterministic suggestion "
				"instead:\n\n");
		}

		const FString MsgBody =
			(Item.IsValid() ? Item->Message : FString());
		const FString FixBody =
			(Item.IsValid() ? Item->FixSuggestion : FString());

		FinalText = Header + MsgBody;
		if (!FixBody.IsEmpty())
			FinalText += TEXT("\n\n") + FixBody;

		StatusText = Result.ErrorMessage.IsEmpty()
			? FString(TEXT("Stream ended with error."))
			: Result.ErrorMessage;
	}

	if (ExplainResultBox.IsValid())
		ExplainResultBox->SetText(FText::FromString(FinalText));
	if (ExplainStatusLine.IsValid())
		ExplainStatusLine->SetText(FText::FromString(StatusText));
}

#undef LOCTEXT_NAMESPACE
