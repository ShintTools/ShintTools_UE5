// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// Blueprint / Python surface for the in-place LOD auto-fix engine (TDD Part 2
// §20.5). Editor Python and Blueprint call these static functions; they are thin
// wrappers over FShintLodFixerRegistry, so the interactive Fixes panel, scripted
// batch fixes, and the commandlet's -applyfixes path all share one deterministic
// applier + one journal (Ctrl+Z = rollback L1, Revert = rollback L2).
//
// Studio-only; stripped from the indie/marketplace trees with the LOD module.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShintLodAutoFixLibrary.generated.h"

// Result of a single apply / revert, mirrored from FShintLodFixResult so it is
// visible to Blueprint and Python (Python sees it as a struct with these props).
USTRUCT(BlueprintType)
struct FShintLodFixOutcome
{
	GENERATED_BODY()

	// A property actually changed (false + empty Error == idempotent no-op).
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	bool bApplied = false;

	// A reimport-class rebuild ran (static-mesh build settings / texture recompress).
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	bool bRebuilt = false;

	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	int32 PropertiesChanged = 0;

	// Journal entry id — pass to RevertFix to undo this apply.
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	FString JournalId;

	// Empty on success; a human-readable reason otherwise.
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	FString Error;
};

// One auto-fix request — the machine-readable descriptor the Core emits per
// finding. Python builds these directly (Recommended is a str -> str dict).
USTRUCT(BlueprintType)
struct FShintLodFixRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "ShintTools|LOD")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "ShintTools|LOD")
	FString RuleId;

	UPROPERTY(BlueprintReadWrite, Category = "ShintTools|LOD")
	FString RuleName;

	// "high" | "medium" | "low" — empty is treated as "high" (§13.5).
	UPROPERTY(BlueprintReadWrite, Category = "ShintTools|LOD")
	FString Confidence;

	// Flattened recommended dict: each recognised key maps to one property write.
	UPROPERTY(BlueprintReadWrite, Category = "ShintTools|LOD")
	TMap<FString, FString> Recommended;
};

// Aggregate outcome of ApplyAllFixes.
USTRUCT(BlueprintType)
struct FShintLodFixBatchOutcome
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	int32 Applied = 0;

	// Below the confidence floor, or applicable-but-already-matching (no-op).
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	int32 Skipped = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	int32 Failed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	int32 PropertiesChanged = 0;

	// One id per applied fix — feed to RevertFix to roll the batch back.
	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	TArray<FString> JournalIds;

	UPROPERTY(BlueprintReadOnly, Category = "ShintTools|LOD")
	TArray<FString> Errors;
};

UCLASS()
class UShintLodAutoFixLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// True when at least one recommended key maps to a supported property on the
	// asset's class — i.e. the fix is genuinely applicable in-editor.
	UFUNCTION(BlueprintCallable, Category = "ShintTools|LOD",
		meta = (DisplayName = "Can Apply LOD Fix"))
	static bool CanApplyFix(const FString& AssetPath,
		const TMap<FString, FString>& Recommended);

	// Apply one finding's recommended changes in place. Wraps a single
	// transaction (Ctrl+Z) + journal snapshot (Revert). Idempotent no-op when
	// nothing applicable differs.
	UFUNCTION(BlueprintCallable, Category = "ShintTools|LOD",
		meta = (DisplayName = "Apply LOD Fix"))
	static FShintLodFixOutcome ApplyFix(const FString& AssetPath,
		const FString& RuleId, const FString& RuleName,
		const TMap<FString, FString>& Recommended);

	// Apply many findings at once, honouring a confidence floor
	// ("high" | "medium" | "low" | "" = apply all). Requests below the floor are
	// counted as Skipped; each applied fix is journalled independently.
	UFUNCTION(BlueprintCallable, Category = "ShintTools|LOD",
		meta = (DisplayName = "Apply All LOD Fixes"))
	static FShintLodFixBatchOutcome ApplyAllFixes(
		const TArray<FShintLodFixRequest>& Requests,
		const FString& MinConfidence);

	// Undo a previously applied fix by its journal id (rollback level 2 —
	// survives an editor restart, unlike Ctrl+Z).
	UFUNCTION(BlueprintCallable, Category = "ShintTools|LOD",
		meta = (DisplayName = "Revert LOD Fix"))
	static FShintLodFixOutcome RevertFix(const FString& JournalId);
};
// [LOD-STRIP-END]
