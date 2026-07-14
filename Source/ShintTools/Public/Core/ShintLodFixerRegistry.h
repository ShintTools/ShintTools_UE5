// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// In-place LOD auto-fix engine (TDD Part 2 §20.5 + §13.6).
//
// Analysis stays in the Core; every fix executes here in the editor. Core's
// per-finding `recommended` dict is the machine-readable descriptor: each
// recognised key (compression, recompute_normals, two_sided, max_texture_size,
// …) maps to exactly one property write or build-settings change. The registry:
//
//   * wraps each apply in one FScopedTransaction (Ctrl+Z = rollback level 1),
//   * snapshots the touched properties to FShintLodFixJournal before mutating
//     (Revert = rollback level 2, survives editor restart),
//   * routes all writes through Modify() → set → PostEditChange() so source
//     control checkout hooks fire and reimport-class fixes rebuild once.
//
// The same registry drives the interactive Fixes panel, the Python
// UShintLodAutoFixLibrary, and the commandlet's -applyfixes path.
//
// Studio-only; stripped from the indie/marketplace trees with the LOD module.
#pragma once

#include "CoreMinimal.h"

struct FShintLodFinding;

// Outcome of a single apply / revert, surfaced to every caller (panel toast,
// Python return value, commandlet log line).
struct FShintLodFixResult
{
	bool    bApplied = false;    // a property actually changed
	bool    bRebuilt = false;    // a reimport-class rebuild ran
	FString JournalId;           // journal entry id (for later Revert)
	FString Error;               // empty on success
	int32   PropertiesChanged = 0;
};

class FShintLodFixerRegistry
{
public:
	// True when at least one key in `Recommended` maps to a fixer this asset
	// class supports — i.e. the finding is genuinely applicable in-editor.
	static bool CanApply(const FString& AssetPath, const TMap<FString, FString>& Recommended);

	// Apply the recommended property changes to the asset at AssetPath. Wraps a
	// transaction + journal snapshot. `RuleId`/`RuleName` label the journal and
	// transaction. No-op (bApplied=false, empty Error) when nothing is applicable.
	static FShintLodFixResult ApplyFix(
		const FString& AssetPath,
		const FString& RuleId,
		const FString& RuleName,
		const TMap<FString, FString>& Recommended);

	// Convenience overload from a server finding (uses Finding.Recommended).
	static FShintLodFixResult ApplyFromFinding(const FShintLodFinding& Finding);

	// Replay a journal entry's `Before` map through the same applier, restoring
	// the pre-fix state (and rebuilding once for reimport-class fixes).
	static FShintLodFixResult RevertFix(const FString& JournalId);
};
// [LOD-STRIP-END]
