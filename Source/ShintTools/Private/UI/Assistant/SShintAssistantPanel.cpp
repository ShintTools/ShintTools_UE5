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
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/StyleDefaults.h"
#include "Styling/SlateTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "ShintIconStyle.h"
#include "Widgets/Images/SImage.h"

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

namespace ShintAssistantPanelPrivate
{
	// Heap-stable for the module's lifetime: Slate keeps the raw pointer, so a
	// brush built on the stack would be read after it died. Same pattern as
	// SShintCard.
	static TUniquePtr<FSlateRoundedBoxBrush> GInputBrush;
	static TUniquePtr<FSlateRoundedBoxBrush> GSendBrush;
	static TUniquePtr<FEditableTextBoxStyle> GFlatInputStyle;

	/** The composer field: a dark pill with a hairline outline. */
	const FSlateBrush* InputBrush()
	{
		if (!GInputBrush.IsValid())
		{
			GInputBrush = MakeUnique<FSlateRoundedBoxBrush>(
				FShintStyle::Colors::BgCard(),
				FShintStyle::Radius::Card,
				FShintStyle::Colors::BorderSubtle(),
				/*OutlineWidth=*/1.f);
		}
		return GInputBrush.Get();
	}

	/** The send button's disc. */
	const FSlateBrush* SendBrush()
	{
		if (!GSendBrush.IsValid())
		{
			GSendBrush = MakeUnique<FSlateRoundedBoxBrush>(
				FShintStyle::Colors::BgCardHover(),
				FShintStyle::Radius::Card);
		}
		return GSendBrush.Get();
	}

	/** The editor's text box with its own background removed, so the rounded
	 *  border wrapping it is the only frame the user sees. Every state has to
	 *  be cleared, not just Normal — otherwise the square editor brush
	 *  reappears the moment the field takes focus, which is exactly when the
	 *  user is looking at it. */
	const FEditableTextBoxStyle* FlatInputStyle()
	{
		if (!GFlatInputStyle.IsValid())
		{
			const FSlateBrush* None = FStyleDefaults::GetNoBrush();

			GFlatInputStyle = MakeUnique<FEditableTextBoxStyle>(
				FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(
					"NormalEditableTextBox"));

			GFlatInputStyle->SetBackgroundImageNormal(*None)
			                .SetBackgroundImageHovered(*None)
			                .SetBackgroundImageFocused(*None)
			                .SetBackgroundImageReadOnly(*None)
			                .SetPadding(FMargin(0.f));
		}
		return GFlatInputStyle.Get();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintAssistantPanel::Construct(const FArguments& InArgs)
{
	bCompact = InArgs._bCompact;

	CoreClient = MakeShared<FShintCoreClient>();
	CoreClient->LoadConfig();

	// The context strip reads FShintAssistantContext through a bound attribute,
	// so a scan finishing while the panel is open relabels itself on the next
	// paint. The subscription is for the other direction: a results row queuing
	// "Explain this finding", which has to be answered as a real turn rather
	// than just relabelled. AddSP holds a weak reference, so a closed panel
	// simply stops receiving — no removal needed.
	FShintAssistantContext::OnChanged.AddSP(
		this, &SShintAssistantPanel::ConsumePendingExplain);

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	// The dock draws its own header, so a second title bar here would be a
	// title above a title. Only the tab-hosted panel needs the strip.
	if (!bCompact)
		Root->AddSlot().AutoHeight() [ BuildContextStrip() ];

	Root->AddSlot().AutoHeight() [ BuildRail() ];

	Root->AddSlot().FillHeight(1.f)
	[
		SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveView); })
		+ SWidgetSwitcher::Slot() [ BuildChatView()   ]
		+ SWidgetSwitcher::Slot() [ BuildMemoryView() ]
		+ SWidgetSwitcher::Slot() [ BuildRulesView()  ]
	];

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(SectionBg(FShintStyle::Colors::Bg()))
		.Padding(0.f)
		[
			Root
		]
	];

	ShowEmptyState();

	RefreshCapabilities();
}

// A fill of its own would square off the rounded card the dock wraps this in,
// so compact mode paints nothing and lets the card show through. Everywhere
// else the panel is the whole surface and needs its own background.
FSlateColor SShintAssistantPanel::SectionBg(const FLinearColor& Opaque) const
{
	return FSlateColor(bCompact ? FLinearColor::Transparent : Opaque);
}

// The thread starts empty, which without this reads as a broken panel rather
// than an invitation. The sentence also states the one thing a new user cannot
// guess: that questions are already grounded in whatever they have open, so
// nothing needs pasting in.
void SShintAssistantPanel::ShowEmptyState()
{
	if (!ThreadBox.IsValid()) return;

	ThreadBox->ClearChildren();
	bShowingEmptyState = true;

	ThreadBox->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("EmptyState",
			"Ask anything about your project. The assistant already has the "
			"context of the panel you are viewing."))
		.Font(FShintStyle::Fonts::Body())
		.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextMuted()))
		.AutoWrapText(true)
	];
}

void SShintAssistantPanel::ClearConversation()
{
	// Bump the token first: a reply still in flight for the old thread must
	// not land in the new one — same guard SendMessage relies on.
	++RequestToken;
	bAwaitingReply = false;

	ConversationId.Reset();
	StreamBuffer.Reset();
	StreamingText.Reset();
	PendingExplainRuleId.Reset();
	PendingExplainAssetPath.Reset();

	if (StatusLine.IsValid())
		StatusLine->SetText(FText::GetEmpty());

	ShowEmptyState();
}

void SShintAssistantPanel::FocusComposer()
{
	if (Composer.IsValid())
		FSlateApplication::Get().SetKeyboardFocus(Composer);
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

void SShintAssistantPanel::ConsumePendingExplain()
{
	FString RuleId, AssetPath, Question;
	if (!FShintAssistantContext::ConsumePendingExplain(RuleId, AssetPath, Question))
		return;

	// Answering while another turn is still generating would drop the request
	// silently (SendMessage refuses when busy), so put it back and let the
	// next OnChanged pick it up.
	if (bAwaitingReply)
	{
		FShintAssistantContext::RequestExplain(RuleId, AssetPath, Question);
		return;
	}

	PendingExplainRuleId    = RuleId;
	PendingExplainAssetPath = AssetPath;

	// The button knows its own intent, so this never goes through the router.
	SendMessage(Question, TEXT("explain_finding"));
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
		.BorderBackgroundColor(SectionBg(FShintStyle::Colors::BgCard()))
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
	TSharedRef<SVerticalBox> Chat = SNew(SVerticalBox);

	// Compact mode dropped the context strip, but the grounding line is the
	// one thing that must survive it: without it a detailed answer to a
	// three-word question looks like a guess.
	if (bCompact)
	{
		Chat->AddSlot().AutoHeight()
			.Padding(FShintStyle::Space::S4, FShintStyle::Space::S3,
			         FShintStyle::Space::S4, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SShintAssistantPanel::GetContextLabel)
			.Font(FShintStyle::Fonts::Caption())
			.ColorAndOpacity(FSlateColor(FShintStyle::Colors::TextFaint()))
			.AutoWrapText(true)
		];
	}

	Chat->AddSlot().FillHeight(1.f)
	[
		SAssignNew(ThreadScroll, SScrollBox)
		+ SScrollBox::Slot().Padding(FShintStyle::Space::S4)
		[
			SAssignNew(ThreadBox, SVerticalBox)
		]
	];

	Chat->AddSlot().AutoHeight() [ BuildComposer() ];

	return Chat;
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
		.BorderBackgroundColor(SectionBg(FShintStyle::Colors::BgTopbar()))
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
					// The rounded field is drawn by this border, not by the
					// text box: FEditableTextBoxStyle's own background is a
					// square editor brush, and overriding the whole style just
					// to round one corner set is more surface than wrapping it.
					SNew(SBorder)
					.BorderImage(ShintAssistantPanelPrivate::InputBrush())
					.VAlign(VAlign_Center)
					.Padding(FMargin(FShintStyle::Space::S3, FShintStyle::Space::S2))
					[
						SAssignNew(Composer, SEditableTextBox)
						.Style(ShintAssistantPanelPrivate::FlatInputStyle())
						.HintText(LOCTEXT("ComposerHint", "Ask about the active panel…"))
						.Font(FShintStyle::Fonts::Body())
						.OnTextCommitted(this, &SShintAssistantPanel::OnComposerCommitted)
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(FShintStyle::Space::S2, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(0.f)
					.ToolTipText(LOCTEXT("SendTip", "Send"))
					.IsEnabled_Lambda([this]{ return !bAwaitingReply; })
					.OnClicked(this, &SShintAssistantPanel::OnSendClicked)
					[
						SNew(SBox).WidthOverride(38.f).HeightOverride(38.f)
						[
							SNew(SBorder)
							.BorderImage(ShintAssistantPanelPrivate::SendBrush())
							.HAlign(HAlign_Center).VAlign(VAlign_Center)
							.Padding(0.f)
							[
								SNew(SImage)
								.Image(FShintIconStyle::GetBrush("ShintTools.Icons.Send"))
								.ColorAndOpacity_Lambda([this]()
								{
									return FSlateColor(bAwaitingReply
										? FShintStyle::Colors::TextFaint()
										: FShintStyle::Colors::TextPrimary());
								})
							]
						]
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

	// The placeholder is a child of the thread, so the first real turn has to
	// evict it rather than stack under it.
	if (bShowingEmptyState)
	{
		ThreadBox->ClearChildren();
		bShowingEmptyState = false;
	}

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

	// Selector within the analysis, set only when a results row asked. Consumed
	// here so the next free-text turn resolves through the server's own
	// inheritance instead of silently reusing this row.
	Req.RuleId    = MoveTemp(PendingExplainRuleId);
	Req.AssetPath = MoveTemp(PendingExplainAssetPath);
	PendingExplainRuleId.Reset();
	PendingExplainAssetPath.Reset();
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
