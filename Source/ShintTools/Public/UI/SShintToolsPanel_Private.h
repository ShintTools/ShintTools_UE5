// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

class STextBlock;
class SWidget;

namespace ST4
{
	const FSlateBrush* Solid(const FLinearColor& C, float R = 0.f);
	const FSlateBrush* Outline(const FLinearColor& Fill, const FLinearColor& Brd, float R = 4.f);
}

void ShintShowErrorToast(const FString& Title, const FString& Detail);

TSharedRef<SWidget> StatBadge(
	TSharedPtr<STextBlock>& OutLabel,
	const FText&            Caption,
	const FLinearColor&     Clr);

TSharedRef<SWidget> ShintBtnContent(
	const FName&               Icon,
	const TSharedRef<SWidget>& Label,
	const FSlateColor&         Tint);

struct FShintRedirectEntry
{
	FString Key;
	FString OldName;
	FString NewName;
};

int32 WriteShintCoreRedirects(const TArray<FShintRedirectEntry>& Entries);
