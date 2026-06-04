// Copyright ShintTools. All Rights Reserved.
//
// Private header shared by the SShintToolsPanel translation-unit family.
// NOT exported — only the SShintToolsPanel_*.cpp files include this. It
// exists to share three things that the original single-file panel kept
// at file-static scope:
//
//   * ST4::Solid / ST4::Outline — process-wide brush cache so every TU
//     paints with the same FSlateBrush instances (and the cache stays a
//     single TMap instead of getting duplicated per TU).
//   * ShintShowErrorToast — uniform error notification surface, used by
//     both the HTTP callbacks TU and any future panel TU that needs to
//     surface a backend failure inline.
//   * StatBadge + the [CoreRedirects] writer — shared between the Code
//     section, the Asset section, and the asset-fix flow.
//
// Keep this header lean. If a helper is only used by ONE TU, leave it
// file-local in that TU.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

class STextBlock;
class SWidget;

// ─────────────────────────────────────────────────────────────────────────────
// Brush cache.
//
// TUniquePtr keeps FSlateBrush at a stable heap address — TMap reallocation
// does NOT invalidate the brush pointer stored in Slate widget attributes.
// The cache is process-lifetime; the TMap is owned by Common.cpp as a
// function-local static accessed through these two free functions.
// ─────────────────────────────────────────────────────────────────────────────
namespace ST4
{
	const FSlateBrush* Solid(const FLinearColor& C, float R = 0.f);
	const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R = 4.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Toast helper.
//
// Surfaces backend / connectivity failures to the user instead of silently
// leaving the UI in an empty state. Without this, "no response from server"
// looked like the buttons were dead. Drops a CS_Fail notification with the
// supplied title and detail (or a generic recovery hint if detail is empty).
// ─────────────────────────────────────────────────────────────────────────────
void ShintShowErrorToast(const FString& Title, const FString& Detail);

// ─────────────────────────────────────────────────────────────────────────────
// KPI tile — the dashboard's metric badge.
//
// Wrapped in SShintCard so each value reads as a discrete surface. OutLabel
// captures the STextBlock holding the value so callers can mutate it
// (`OutLabel->SetText("123")`) without rebuilding the widget.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& OutLabel,
	const FText&            Caption,
	const FLinearColor&     Clr);

// ─────────────────────────────────────────────────────────────────────────────
// CoreRedirects writer — DefaultEngine.ini patcher.
//
// One entry per logical redirect; the writer dedupes by exact textual match
// so re-running the asset bot does not bloat the ini. See Common.cpp for the
// full rationale on why every BP rename emits three redirect kinds.
// ─────────────────────────────────────────────────────────────────────────────
struct FShintRedirectEntry
{
	FString Key;     // "+ClassRedirects" / "+PackageRedirects" / "+ObjectRedirects"
	FString OldName; // /Game/Path/Asset (caller adds .Asset / _C suffixes per kind)
	FString NewName;
};

int32 WriteShintCoreRedirects(const TArray<FShintRedirectEntry>& Entries);
