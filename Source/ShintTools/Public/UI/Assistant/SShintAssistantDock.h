// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Widgets/SCompoundWidget.h"

class SShintAssistantPanel;
class SWindow;

class SShintAssistantDock : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintAssistantDock) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void Open();

	static void Toggle();

	static void Collapse();

	static void Shutdown();

	static bool IsExpanded();

private:
	TSharedRef<SWidget> BuildHeader();

	FReply OnClearClicked();

	TSharedPtr<SShintAssistantPanel> Panel;

	friend class SShintAssistantLauncher;
};

class SShintAssistantLauncher : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintAssistantLauncher) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry,
	                                 const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry,
	                               const FPointerEvent& MouseEvent) override;
};
