// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

enum class EShintConnState : uint8
{
	Unknown,
	Connecting,
	Connected,
	Disconnected,
};

class SShintTopBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintTopBar)
		: _Title()
		, _StatusText()
		, _TierText()
		, _ConnState(EShintConnState::Unknown)
	{}

		SLATE_ATTRIBUTE(FText, Title)

		SLATE_ATTRIBUTE(FText, StatusText)

		SLATE_ATTRIBUTE(FText, TierText)

		SLATE_ATTRIBUTE(EShintConnState, ConnState)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
