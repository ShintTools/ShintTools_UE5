// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class STextBlock;

class SShintCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintCard)
		: _Title()
		, _ContentPadding(16.f)
		, _bShowHeader(true)
	{}

		SLATE_ATTRIBUTE(FText, Title)

		SLATE_ARGUMENT(float, ContentPadding)

		SLATE_ARGUMENT(bool, bShowHeader)

		SLATE_NAMED_SLOT(FArguments, HeaderRight)

		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<FText> TitleAttr;
	TSharedPtr<STextBlock> TitleText;
};
