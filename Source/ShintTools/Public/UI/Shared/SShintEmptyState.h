// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"

class SShintEmptyState : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(FReply, FOnEmptyActionClicked);

	SLATE_BEGIN_ARGS(SShintEmptyState)
		: _Headline()
		, _Subtitle()
		, _ActionLabel()
	{}
		SLATE_ATTRIBUTE(FText, Headline)
		SLATE_ATTRIBUTE(FText, Subtitle)

		SLATE_ATTRIBUTE(FText, ActionLabel)
		SLATE_EVENT(FOnEmptyActionClicked, OnActionClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
