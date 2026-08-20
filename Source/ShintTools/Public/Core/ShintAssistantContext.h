// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EShintAssistantModule : uint8
{
	None,
	CodeValidator,
	AssetNaming,
};

struct SHINTTOOLS_API FShintAssistantContext
{

	static void Publish(const FString& AnalysisId, EShintAssistantModule Module,
	                    const FString& Summary);

	static void Clear();

	static FString               GetAnalysisId();
	static EShintAssistantModule GetModule();
	static FString               GetSummary();

	static FString GetModuleContextString();

	static bool HasContext();

	static void RequestExplain(const FString& RuleId, const FString& AssetPath,
	                           const FString& Question);

	static bool ConsumePendingExplain(FString& OutRuleId, FString& OutAssetPath,
	                                  FString& OutQuestion);

	DECLARE_MULTICAST_DELEGATE(FOnShintAssistantContextChanged);
	static FOnShintAssistantContextChanged OnChanged;
};
