// Copyright 2026 ShintTools. All Rights Reserved.

#include "Assistant/SShintAssistantPanel.h"

#include "ShintStyle.h"
#include "ShintTools.h"
#include "SShintToolsPanel_Private.h"   // ShintShowErrorToast

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"

#define LOCTEXT_NAMESPACE "SShintAssistantPanel"

namespace
{
	// Quick-start prompts. Each carries the intent it means, so a click never
	// goes through the router — §2 of the contract: "send intent when you know
	// it", it is faster and cannot be misclassified.
	struct FQuickPrompt
	{
		const TCHAR* Label;
		const TCHAR* Message;
		const TCHAR* Intent;
	};

	const FQuickPrompt kQuickPrompts[] = {
		{ TEXT("Summarize this scan"), TEXT("Summarize this analysis."),
		  TEXT("summarize_module") },
		{ TEXT("Why this rule?"),      TEXT("Why does this rule exist?"),
		  TEXT("why_rule") },
		{ TEXT("What can you do?"),    TEXT("What can you help me with?"),
		  TEXT("general_help") },
	};

	FLinearColor RoleColor(const FString& Role)
	{
		return Role == TEXT("user")
			? FShintStyle::Colors::TextMuted()
			: FShintStyle::Colors::TextPrimary();
	}

	// A tier that cannot run an intent still gets an explanation, not a dead
	// button — the contract's 403 detail lists what IS allowed, and this turns
	// that into a sentence worth reading.
	FString FormatGateMessage(const FShintAssistantResponse& R)
	{
		if (R.AllowedIntents.Num() == 0)
			return R.ErrorMessage;

		return FString::Printf(
			TEXT("%s\n\nOn your current plan (%s) I can still: %s."),
			*R.ErrorMessage,
			R.CurrentTier.IsEmpty() ? TEXT("free") : *R.CurrentTier,
			*FString::Join(R.AllowedIntents, TEXT(", ")));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintAssistantPanel::Construct(const FArguments& InArgs)
{
	CoreClient = MakeShared<FShintCoreClient>();
	CoreClient->LoadConfig();

	// The context strip reads FShintAssistantContext through a bound attribute,
	// so a scan finishing while the panel is open relabels it on the next paint
	// with no subscription to keep alive.
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::Bg())
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight() [ BuildContextStrip() ]
			+ SVerticalBox::Slot().AutoHeight() [ BuildRail() ]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveView); })
				+ SWidgetSwitcher::Slot() [ BuildChatView()   ]
				+ SWidgetSwitcher::Slot() [ BuildMemoryView() ]
				+ SWidgetSwitcher::Slot() [ BuildRulesView()  ]
			]
		]
	];

	RefreshCapabilities();
}

// ─────────────────────────────────────────────────────────────────────────────
// Context strip — what the answer will be grounded in
// ─────────────────────────────────────────────────────────────────────────────

FText SShintAssistantPanel::GetContextLabel() const
{
	if (!FShintAssistantContext::HasContext())
	{
		return LOCTEXT("NoContext",
			"No analysis in view — run a scan to ask about your project");
	}
	return FText::Format(LOCTEXT("ViewingFmt", "Viewing: {0}"),
		FText::FromString(FShintAssistantContext::GetSummary()));
}

TSharedRef<SWidget> SShintAssistantPanel::BuildContextStrip()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgTopbar())
		.Padding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S3))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "ShintTools AI Assistant"))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &SShintAssistantPanel::GetContextLabel)
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				.AutoWrapText(true)
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Rail
// ─────────────────────────────────────────────────────────────────────────────

bool SShintAssistantPanel::CanUseView(EShintAssistantView View) const
{
	switch (View)
	{
	case EShintAssistantView::Memory:
		// Session memory has nothing durable to manage, so the destination
		// only earns its place at "full".
		return Capabilities.HasPersistentMemory();
	case EShintAssistantView::Rules:
		return !Capabilities.StudioRules.IsEmpty();
	default:
		return true;   // Chat is available on every tier, including Free
	}
}

TSharedRef<SWidget> SShintAssistantPanel::BuildRailButton(
	EShintAssistantView View, const FText& Label)
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(FShintStyle::Space::S3, FShintStyle::Space::S2))
		.Visibility_Lambda([this, View]()
		{
			return CanUseView(View) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.OnClicked_Lambda([this, View]()
		{
			ActiveView = View;
			if (View == EShintAssistantView::Memory) RefreshMemory();
			if (View == EShintAssistantView::Rules)  RefreshRules();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(FShintStyle::Fonts::Small())
			.ColorAndOpacity_Lambda([this, View]()
			{
				return FSlateColor(ActiveView == View
					? FShintStyle::Colors::TextPrimary()
					: FShintStyle::Colors::TextMuted());
			})
		];
}

TSharedRef<SWidget> SShintAssistantPanel::BuildRail()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FMargin(FShintStyle::Space::S2, 0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildRailButton(EShintAssistantView::Chat,   LOCTEXT("NavChat",   "Chat")) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildRailButton(EShintAssistantView::Memory, LOCTEXT("NavMemory", "Memory")) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ BuildRailButton(EShintAssistantView::Rules,  LOCTEXT("NavRules",  "Studio Rules")) ]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Chat
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintAssistantPanel::BuildChatView()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SAssignNew(ThreadScroll, SScrollBox)
			+ SScrollBox::Slot().Padding(FShintStyle::Space::S4)
			[
				SAssignNew(ThreadBox, SVerticalBox)
			]
		]

		+ SVerticalBox::Slot().AutoHeight() [ BuildComposer() ];
}

TSharedRef<SWidget> SShintAssistantPanel::BuildComposer()
{
	TSharedRef<SHorizontalBox> Quick = SNew(SHorizontalBox);
	for (const FQuickPrompt& P : kQuickPrompts)
	{
		const FString Message = P.Message;
		const FString Intent  = P.Intent;

		Quick->AddSlot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S2, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(FShintStyle::Space::S2, FShintStyle::Space::S1))
			// A chip for an intent this tier cannot run would be a button whose
			// only outcome is a 403 — hide it rather than teach the user to
			// expect failures.
			.Visibility_Lambda([this, Intent]()
			{
				return Capabilities.CanRun(Intent)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this, Message, Intent]()
			{
				SendMessage(Message, Intent);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(P.Label))
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::AccentBlue()))
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgTopbar())
		.Padding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S3))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
			[ Quick ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SAssignNew(Composer, SEditableTextBox)
					.HintText(LOCTEXT("ComposerHint", "Ask about this analysis…"))
					.Font(FShintStyle::Fonts::Body())
					.OnTextCommitted(this, &SShintAssistantPanel::OnComposerCommitted)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(FShintStyle::Space::S2, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(FShintStyle::Space::S4, FShintStyle::Space::S1))
					.IsEnabled_Lambda([this]{ return !bAwaitingReply; })
					.OnClicked(this, &SShintAssistantPanel::OnSendClicked)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Send", "Send"))
						.Font(FShintStyle::Fonts::Small())
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
			[
				SAssignNew(StatusLine, STextBlock)
				.Text(FText::GetEmpty())
				.Font(FShintStyle::Fonts::Caption())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				.AutoWrapText(true)
			]
		];
}

void SShintAssistantPanel::OnComposerCommitted(
	const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
		SendMessage(Text.ToString(), FString());
}

FReply SShintAssistantPanel::OnSendClicked()
{
	if (Composer.IsValid())
		SendMessage(Composer->GetText().ToString(), FString());
	return FReply::Handled();
}

void SShintAssistantPanel::AppendTurn(
	const FString& Role, const FString& Text, const FString& Intent,
	bool bContinued, bool bDegraded)
{
	if (!ThreadBox.IsValid()) return;

	const bool bIsUser = Role == TEXT("user");

	TSharedRef<SVerticalBox> Bubble = SNew(SVerticalBox);

	// A two-word question that gets a detailed answer looks like a coincidence
	// unless the panel says what it resolved against — §2 of the contract asks
	// clients to surface this explicitly.
	if (bContinued && !bIsUser)
	{
		Bubble->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S1)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FollowingUp", "following up on the previous answer"))
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()))
		];
	}

	Bubble->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Text))
		.Font(FShintStyle::Fonts::Body())
		.ColorAndOpacity(FSlateColor(RoleColor(Role)))
		.AutoWrapText(true)
	];

	if (bDegraded)
	{
		Bubble->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Degraded",
				"The model stopped early — this is the deterministic answer."))
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::Warning()))
			.AutoWrapText(true)
		];
	}

	ThreadBox->AddSlot().AutoHeight()
		.Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(bIsUser
			? FShintStyle::Colors::Bg() : FShintStyle::Colors::BgCard())
		.Padding(FMargin(FShintStyle::Space::S3))
		[ Bubble ]
	];

	if (ThreadScroll.IsValid())
		ThreadScroll->ScrollToEnd();
}

void SShintAssistantPanel::BeginStreamingTurn()
{
	if (!ThreadBox.IsValid()) return;

	StreamBuffer.Reset();

	ThreadBox->AddSlot().AutoHeight()
		.Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FMargin(FShintStyle::Space::S3))
		[
			SAssignNew(StreamingText, STextBlock)
			.Text(LOCTEXT("Thinking", "Thinking…"))
			.Font(FShintStyle::Fonts::Body())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
			.AutoWrapText(true)
		]
	];

	if (ThreadScroll.IsValid())
		ThreadScroll->ScrollToEnd();
}

void SShintAssistantPanel::SendMessage(const FString& Text, const FString& ForcedIntent)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || bAwaitingReply) return;

	AppendTurn(TEXT("user"), Trimmed, FString(), false, false);
	if (Composer.IsValid())
		Composer->SetText(FText::GetEmpty());

	FShintAssistantRequest Req;
	Req.Message        = Trimmed;
	Req.ConversationId = ConversationId;
	Req.Intent         = ForcedIntent;
	Req.ProjectId      = CoreClient->GetConfig().ProjectId;
	Req.StudioId       = CoreClient->GetConfig().ProjectName;

	// Ground the turn in whatever the user is looking at. Sending it on every
	// turn is safe: an explicit value simply overrides the inheritance the
	// server would otherwise apply, and a follow-up still resolves correctly.
	Req.ContextRef     = FShintAssistantContext::GetAnalysisId();
	Req.ModuleContext  = FShintAssistantContext::GetModuleContextString();
	// [LOD-STRIP-BEGIN]
	Req.ReportId       = FShintAssistantContext::GetReportId();
	// [LOD-STRIP-END]

	bAwaitingReply = true;
	const uint64 Token = ++RequestToken;

	if (StatusLine.IsValid())
		StatusLine->SetText(LOCTEXT("Sending", "Asking the local model…"));

	BeginStreamingTurn();

	TWeakPtr<SShintAssistantPanel> WeakSelf(SharedThis(this));

	FOnShintStreamChunk OnChunk = FOnShintStreamChunk::CreateLambda(
		[WeakSelf, Token](const FString& Chunk)
		{
			if (TSharedPtr<SShintAssistantPanel> P = WeakSelf.Pin())
				P->OnStreamChunk(Chunk, Token);
		});

	FOnShintAssistantComplete OnDone = FOnShintAssistantComplete::CreateLambda(
		[WeakSelf, Token](const FShintAssistantResponse& R)
		{
			if (TSharedPtr<SShintAssistantPanel> P = WeakSelf.Pin())
				P->OnMessageComplete(R, Token);
		});

	CoreClient->SendAssistantMessageStream(Req, OnChunk, OnDone);
}

void SShintAssistantPanel::OnStreamChunk(const FString& Chunk, uint64 RequestId)
{
	// Drop tokens from a superseded turn — the user sent another message, or
	// reset the thread, while this one was still generating.
	if (RequestId != RequestToken) return;

	StreamBuffer += Chunk;
	if (StreamingText.IsValid())
		StreamingText->SetText(FText::FromString(StreamBuffer));
	if (ThreadScroll.IsValid())
		ThreadScroll->ScrollToEnd();
}

void SShintAssistantPanel::OnMessageComplete(
	const FShintAssistantResponse& Response, uint64 RequestId)
{
	if (RequestId != RequestToken) return;

	bAwaitingReply = false;

	if (!Response.ConversationId.IsEmpty())
		ConversationId = Response.ConversationId;

	if (!Response.bSuccess)
	{
		// The failure text is always displayable as-is; a gated intent gets the
		// extra sentence about what this tier CAN do.
		const FString Text = Response.AllowedIntents.Num() > 0
			? FormatGateMessage(Response)
			: Response.ErrorMessage;

		if (StreamingText.IsValid())
		{
			StreamingText->SetText(FText::FromString(Text));
			StreamingText->SetColorAndOpacity(
				FSlateColor(FShintStyle::Colors::Warning()));
		}
		if (StatusLine.IsValid())
			StatusLine->SetText(FText::GetEmpty());

		StreamingText.Reset();
		return;
	}

	// Replace the live bubble's provisional text with the final answer. The
	// streamed buffer and the terminal full_text agree, but the blocking path
	// (and a single-chunk table answer) only populates the latter.
	const FString Final = StreamBuffer.IsEmpty()
		? Response.Reply.RawText : StreamBuffer;

	if (StreamingText.IsValid())
		StreamingText->SetText(FText::FromString(Final));
	StreamingText.Reset();

	if (StatusLine.IsValid())
	{
		StatusLine->SetText(Response.bContinued
			? LOCTEXT("ContinuedStatus", "Answered as a follow-up to your last question.")
			: FText::GetEmpty());
	}

	// A remember_fact / define_rule turn produced a PROPOSAL. It does nothing
	// until the user accepts it, so the thread has to offer that choice — a
	// client that never renders this has an assistant that never learns.
	if (Response.Intent == TEXT("remember_fact"))
		RefreshMemory();
	else if (Response.Intent == TEXT("define_rule"))
		RefreshRules();
}

// ─────────────────────────────────────────────────────────────────────────────
// Memory
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintAssistantPanel::BuildMemoryView()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(FShintStyle::Space::S4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MemoryBlurb",
					"What your team has told the assistant. A fact does nothing "
					"until you confirm it."))
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ SAssignNew(MemoryList, SVerticalBox) ]
		];
}

void SShintAssistantPanel::AppendFactConfirmCard(const FShintAssistantFact& Fact)
{
	if (!MemoryList.IsValid()) return;

	const bool bProposed = Fact.Status == TEXT("proposed");
	const FString FactId = Fact.FactId;

	TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);

	Card->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Fact.Value))
		.Font(FShintStyle::Fonts::Body())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		.AutoWrapText(true)
	];

	Card->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("%s · %s"), *Fact.Type, *Fact.Status)))
		.Font(FShintStyle::Fonts::Caption())
		.ColorAndOpacity(FSlateColor(bProposed
			? FShintStyle::Colors::Warning() : FShintStyle::Colors::TextFaint()))
	];

	// Confirm/reject for a proposal; retract for something already confirmed.
	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	auto AddAction = [this, &Actions, FactId](const FText& Label, bool bAccept)
	{
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S2, 0.f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(FShintStyle::Space::S3, FShintStyle::Space::S1))
			.OnClicked_Lambda([this, FactId, bAccept]()
			{
				CoreClient->ConfirmAssistantFact(FactId, bAccept,
					FOnShintAssistantComplete::CreateSP(
						this, &SShintAssistantPanel::OnFactConfirmed));
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(FShintStyle::Fonts::Caption())
			]
		];
	};

	if (bProposed)
	{
		AddAction(LOCTEXT("Confirm", "Confirm"), true);
		AddAction(LOCTEXT("Reject",  "Reject"),  false);
	}
	else if (Fact.Status == TEXT("confirmed"))
	{
		AddAction(LOCTEXT("Forget", "Forget"), false);
	}

	Card->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S2, 0.f, 0.f)
	[ Actions ];

	MemoryList->AddSlot().AutoHeight()
		.Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FMargin(FShintStyle::Space::S3))
		[ Card ]
	];
}

void SShintAssistantPanel::RefreshMemory()
{
	if (!Capabilities.HasPersistentMemory() || !MemoryList.IsValid()) return;

	const FShintCoreConfig& Cfg = CoreClient->GetConfig();
	TWeakPtr<SShintAssistantPanel> WeakSelf(SharedThis(this));

	CoreClient->GetAssistantMemory(Cfg.ProjectName, Cfg.ProjectId,
		FOnShintAssistantMemory::CreateLambda(
			[WeakSelf](const FShintAssistantMemory& M)
		{
			TSharedPtr<SShintAssistantPanel> P = WeakSelf.Pin();
			if (!P.IsValid() || !P->MemoryList.IsValid()) return;

			P->Facts = M.Facts;
			P->MemoryList->ClearChildren();

			if (M.Facts.Num() == 0)
			{
				P->MemoryList->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoFacts",
						"Nothing remembered yet. Tell the assistant a convention "
						"in chat and confirm it here."))
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()))
					.AutoWrapText(true)
				];
				return;
			}

			for (const FShintAssistantFact& F : M.Facts)
			{
				// Superseded/retracted facts are history, not state the user
				// needs to act on — the store keeps them, the panel does not
				// clutter itself with them.
				if (F.Status == TEXT("proposed") || F.Status == TEXT("confirmed"))
					P->AppendFactConfirmCard(F);
			}
		}));
}

void SShintAssistantPanel::OnFactConfirmed(const FShintAssistantResponse& R)
{
	if (!R.bSuccess)
	{
		ShintShowErrorToast(TEXT("Could not update memory"), R.ErrorMessage);
		return;
	}
	RefreshMemory();
}

// ─────────────────────────────────────────────────────────────────────────────
// Studio rules
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<SWidget> SShintAssistantPanel::BuildRulesView()
{
	return SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(FShintStyle::Space::S4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, FShintStyle::Space::S3)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RulesBlurb",
					"Conventions your team defined in chat. A rule stays a draft "
					"until you activate it — what you accept is exactly what runs."))
				.Font(FShintStyle::Fonts::Small())
				.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[ SAssignNew(RulesList, SVerticalBox) ]
		];
}

void SShintAssistantPanel::AppendRuleConfirmCard(const FShintAssistantRule& Rule)
{
	if (!RulesList.IsValid()) return;

	const bool    bDraft = Rule.Status == TEXT("draft");
	const FString RuleId = Rule.RuleId;

	TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);

	Card->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Rule.Name))
		.Font(FShintStyle::Fonts::Body())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextPrimary()))
		.AutoWrapText(true)
	];

	// Show the compiler's own reading verbatim: the user is accepting THIS
	// interpretation, not their original sentence.
	if (!Rule.Description.IsEmpty())
	{
		Card->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Rule.Description))
			.Font(FShintStyle::Fonts::Small())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
			.AutoWrapText(true)
		];
	}

	Card->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S1, 0.f, 0.f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(FString::Printf(
			TEXT("%s · %s"),
			Rule.Tier == TEXT("template")
				? TEXT("deterministic check") : TEXT("evaluated by the model"),
			*Rule.Status)))
		.Font(FShintStyle::Fonts::Caption())
		.ColorAndOpacity(FSlateColor(bDraft
			? FShintStyle::Colors::Warning() : FShintStyle::Colors::Success()))
	];

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	auto AddAction = [this, &Actions, RuleId](const FText& Label, bool bAccept)
	{
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, FShintStyle::Space::S2, 0.f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(FShintStyle::Space::S3, FShintStyle::Space::S1))
			.OnClicked_Lambda([this, RuleId, bAccept]()
			{
				CoreClient->ConfirmAssistantRule(RuleId, bAccept,
					FOnShintAssistantComplete::CreateSP(
						this, &SShintAssistantPanel::OnRuleConfirmed));
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label).Font(FShintStyle::Fonts::Caption())
			]
		];
	};

	if (bDraft)
	{
		AddAction(LOCTEXT("Activate", "Activate"), true);
		AddAction(LOCTEXT("Discard",  "Discard"),  false);
	}
	else if (Rule.Status == TEXT("active"))
	{
		AddAction(LOCTEXT("Deactivate", "Deactivate"), false);
	}

	Card->AddSlot().AutoHeight().Padding(0.f, FShintStyle::Space::S2, 0.f, 0.f)
	[ Actions ];

	RulesList->AddSlot().AutoHeight()
		.Padding(0.f, 0.f, 0.f, FShintStyle::Space::S2)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FShintStyle::Colors::BgCard())
		.Padding(FMargin(FShintStyle::Space::S3))
		[ Card ]
	];
}

void SShintAssistantPanel::RefreshRules()
{
	if (Capabilities.StudioRules.IsEmpty() || !RulesList.IsValid()) return;

	const FShintCoreConfig& Cfg = CoreClient->GetConfig();
	TWeakPtr<SShintAssistantPanel> WeakSelf(SharedThis(this));

	CoreClient->GetAssistantRules(Cfg.ProjectName, Cfg.ProjectId,
		FOnShintAssistantRules::CreateLambda(
			[WeakSelf](const FShintAssistantRules& Result)
		{
			TSharedPtr<SShintAssistantPanel> P = WeakSelf.Pin();
			if (!P.IsValid() || !P->RulesList.IsValid()) return;

			P->Rules = Result.Rules;
			P->RulesList->ClearChildren();

			if (Result.Rules.Num() == 0)
			{
				P->RulesList->AddSlot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoRules",
						"No studio rules yet. Describe one in chat — for example "
						"\"never call GetWorld inside a loop\" — and activate it here."))
					.Font(FShintStyle::Fonts::Small())
					.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()))
					.AutoWrapText(true)
				];
				return;
			}

			for (const FShintAssistantRule& R : Result.Rules)
				P->AppendRuleConfirmCard(R);
		}));
}

void SShintAssistantPanel::OnRuleConfirmed(const FShintAssistantResponse& R)
{
	if (!R.bSuccess)
	{
		ShintShowErrorToast(TEXT("Could not update the rule"), R.ErrorMessage);
		return;
	}
	RefreshRules();
}

// ─────────────────────────────────────────────────────────────────────────────
// Capabilities — the only thing that decides what this panel offers
// ─────────────────────────────────────────────────────────────────────────────

void SShintAssistantPanel::RefreshCapabilities()
{
	TWeakPtr<SShintAssistantPanel> WeakSelf(SharedThis(this));

	CoreClient->GetAssistantCapabilities(
		FOnShintAssistantCapabilities::CreateLambda(
			[WeakSelf](const FShintAssistantCapabilities& C)
		{
			TSharedPtr<SShintAssistantPanel> P = WeakSelf.Pin();
			if (!P.IsValid()) return;

			P->Capabilities = C;

			// A core too old to serve the router still reports the Free floor,
			// so the panel stays usable rather than showing an empty shell.
			if (!C.ErrorMessage.IsEmpty() && P->StatusLine.IsValid())
				P->StatusLine->SetText(FText::FromString(C.ErrorMessage));

			if (C.HasPersistentMemory()) P->RefreshMemory();
			if (!C.StudioRules.IsEmpty()) P->RefreshRules();
		}));
}

#undef LOCTEXT_NAMESPACE
