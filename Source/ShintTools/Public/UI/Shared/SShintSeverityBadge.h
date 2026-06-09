// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintSeverityBadge — small colored pill that conveys an issue's severity.
//
// Replaces hand-rolled SBorder + STextBlock + tint stacks scattered across the
// code-issue / asset-issue / score views. One widget, one source of truth for
// severity color, label casing and pill geometry.
//
// Usage:
//   SNew(SShintSeverityBadge).Severity(TEXT("critical"))
//   SNew(SShintSeverityBadge).Severity(TEXT("warning")).Label(LOCTEXT("Warn","Warning"))

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
		/** Raw severity string from the server (critical / high / medium / low / error / warning / info). */
		SLATE_ATTRIBUTE(FString, Severity)

		/** Optional override label. Defaults to the severity string upper-cased. */
		SLATE_ATTRIBUTE(FText, Label)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
