// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SShintKpiTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintKpiTile)
		: _Caption()
		, _Value()
		, _Trend()
	{}

		SLATE_ATTRIBUTE(FText, Caption)

		SLATE_ATTRIBUTE(FText, Value)

		SLATE_ATTRIBUTE(FText, Trend)

		SLATE_ATTRIBUTE(FSlateColor, ValueColor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
