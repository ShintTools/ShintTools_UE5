// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

enum class EShintDestination : uint8
{
	Overview,
	Code,
	Assets,
	Settings,
};

class SShintSidebar : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnSidebarSelected, EShintDestination);

	SLATE_BEGIN_ARGS(SShintSidebar)
		: _Active(EShintDestination::Overview)
	{}

		SLATE_ATTRIBUTE(EShintDestination, Active)

		SLATE_EVENT(FOnSidebarSelected, OnSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TAttribute<EShintDestination> ActiveAttr;
	FOnSidebarSelected            OnSelectedDelegate;

	TSharedRef<SWidget> BuildNavButton(
		EShintDestination Dest, const FText& Label, const FName& Icon,
		bool bStudioOnly = false);
};
