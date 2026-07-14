// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "ShintLodAutoFixLibrary.h"

#include "ShintLodFixerRegistry.h"

namespace
{
	// Confidence rank: high = 3, medium = 2, low = 1. Unknown / empty is treated
	// as high (§13.5: an unspecified confidence is a high-confidence fix), so an
	// empty MinConfidence floor (rank 3) still admits every "high"/unspecified
	// request while excluding medium/low.
	int32 ConfidenceRank(const FString& C)
	{
		if (C.Equals(TEXT("low"), ESearchCase::IgnoreCase))    return 1;
		if (C.Equals(TEXT("medium"), ESearchCase::IgnoreCase)) return 2;
		return 3;   // "high" or unspecified
	}

	FShintLodFixOutcome ToOutcome(const FShintLodFixResult& R)
	{
		FShintLodFixOutcome O;
		O.bApplied          = R.bApplied;
		O.bRebuilt          = R.bRebuilt;
		O.PropertiesChanged = R.PropertiesChanged;
		O.JournalId         = R.JournalId;
		O.Error             = R.Error;
		return O;
	}
}

bool UShintLodAutoFixLibrary::CanApplyFix(
	const FString& AssetPath, const TMap<FString, FString>& Recommended)
{
	return FShintLodFixerRegistry::CanApply(AssetPath, Recommended);
}

FShintLodFixOutcome UShintLodAutoFixLibrary::ApplyFix(
	const FString& AssetPath, const FString& RuleId, const FString& RuleName,
	const TMap<FString, FString>& Recommended)
{
	return ToOutcome(FShintLodFixerRegistry::ApplyFix(
		AssetPath, RuleId, RuleName, Recommended));
}

FShintLodFixBatchOutcome UShintLodAutoFixLibrary::ApplyAllFixes(
	const TArray<FShintLodFixRequest>& Requests, const FString& MinConfidence)
{
	FShintLodFixBatchOutcome Out;
	const int32 Floor = ConfidenceRank(MinConfidence);

	for (const FShintLodFixRequest& Req : Requests)
	{
		if (ConfidenceRank(Req.Confidence) < Floor)
		{
			++Out.Skipped;
			continue;
		}

		const FShintLodFixResult R = FShintLodFixerRegistry::ApplyFix(
			Req.AssetPath, Req.RuleId, Req.RuleName, Req.Recommended);

		if (!R.Error.IsEmpty())
		{
			++Out.Failed;
			Out.Errors.Add(FString::Printf(
				TEXT("%s: %s"), *Req.AssetPath, *R.Error));
		}
		else if (R.bApplied)
		{
			++Out.Applied;
			Out.PropertiesChanged += R.PropertiesChanged;
			if (!R.JournalId.IsEmpty())
			{
				Out.JournalIds.Add(R.JournalId);
			}
		}
		else
		{
			// Applicable but nothing differed (state already matched the
			// recommendation) — an idempotent no-op, counted as skipped.
			++Out.Skipped;
		}
	}
	return Out;
}

FShintLodFixOutcome UShintLodAutoFixLibrary::RevertFix(const FString& JournalId)
{
	return ToOutcome(FShintLodFixerRegistry::RevertFix(JournalId));
}
// [LOD-STRIP-END]
