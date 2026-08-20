// Copyright 2026 ShintTools. All Rights Reserved.

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

		SLATE_ARGUMENT(bool, bCompact)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void ClearConversation();

	void FocusComposer();

private:

	TSharedRef<SWidget> BuildContextStrip();
	TSharedRef<SWidget> BuildRail();
	TSharedRef<SWidget> BuildChatView();
	TSharedRef<SWidget> BuildMemoryView();
	TSharedRef<SWidget> BuildRulesView();
	TSharedRef<SWidget> BuildComposer();

	TSharedRef<SWidget> BuildRailButton(EShintAssistantView View, const FText& Label);

	void ShowEmptyState();

	FSlateColor SectionBg(const FLinearColor& Opaque) const;

	void    SendMessage(const FString& Text, const FString& ForcedIntent);
	FReply  OnSendClicked();
	void    OnComposerCommitted(const FText& Text, ETextCommit::Type CommitType);

	void AppendTurn(const FString& Role, const FString& Text, const FString& Intent,
	                bool bContinued, bool bDegraded);

	void BeginStreamingTurn();
	void OnStreamChunk(const FString& Chunk, uint64 RequestId);
	void OnMessageComplete(const FShintAssistantResponse& Response, uint64 RequestId);

	void AppendFactConfirmCard(const FShintAssistantFact& Fact);
	void AppendRuleConfirmCard(const FShintAssistantRule& Rule);

	void OnFactConfirmed(const FShintAssistantResponse& Result);
	void OnRuleConfirmed(const FShintAssistantResponse& Result);

	void RefreshCapabilities();
	void RefreshMemory();
	void RefreshRules();

	FText GetContextLabel() const;

	void ConsumePendingExplain();

	bool CanUseView(EShintAssistantView View) const;

	TSharedPtr<FShintCoreClient> CoreClient;

	FShintAssistantCapabilities Capabilities;
	FString                     ConversationId;

	EShintAssistantView ActiveView = EShintAssistantView::Chat;

	bool bCompact = false;

	bool bShowingEmptyState = false;

	uint64 RequestToken = 0;
	bool   bAwaitingReply = false;

	FString StreamBuffer;

	FString PendingExplainRuleId;
	FString PendingExplainAssetPath;

	TArray<FShintAssistantFact> Facts;
	TArray<FShintAssistantRule> Rules;

	TSharedPtr<SVerticalBox>     ThreadBox;
	TSharedPtr<SScrollBox>       ThreadScroll;
	TSharedPtr<SEditableTextBox> Composer;
	TSharedPtr<STextBlock>       StreamingText;
	TSharedPtr<STextBlock>       StatusLine;
	TSharedPtr<SVerticalBox>     MemoryList;
	TSharedPtr<SVerticalBox>     RulesList;
};
