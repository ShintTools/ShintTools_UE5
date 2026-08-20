// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;
class ISlateStyle;
class FSlateStyleSet;

class FShintIconStyle
{
public:

	static void Initialize();

	static void Shutdown();

	static FName GetStyleSetName();

	static const ISlateStyle& Get();

	static const FSlateBrush* GetBrush(const FName& Name);

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
