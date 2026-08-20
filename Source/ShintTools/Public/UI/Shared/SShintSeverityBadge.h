// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SShintSeverityBadge : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintSeverityBadge)
		: _Severity(TEXT("medium"))
		, _Label()
	{}

		SLATE_ATTRIBUTE(FString, Severity)

		SLATE_ATTRIBUTE(FText, Label)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
