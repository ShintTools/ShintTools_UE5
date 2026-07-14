// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
// Fix journal — rollback level 2 for the LOD auto-fix engine (TDD Part 2 §13.6.3).
//
// Every in-place fix snapshots the touched properties (before + after) to an
// append-only JSONL file under Saved/ShintTools/ before mutating the asset.
// Native editor Undo (Ctrl+Z, one FScopedTransaction per apply) is rollback
// level 1; this journal survives an editor restart and drives the Fixes-panel
// "Revert" button, which replays the `Before` map through the same property
// applier. The file is append-only and truncated oldest-first at 5 MB.
//
// Studio-only; stripped from the indie/marketplace trees with the LOD module.
#pragma once

#include "CoreMinimal.h"

// One journalled fix. Before/After are the flattened property maps the fixer
// registry writes (key → serialised value), so Revert is symmetric with Apply.
struct FShintLodJournalEntry
{
	FString Id;                 // unique: "<UtcTicks>-<counter>" — Revert targets this
	FString Timestamp;          // ISO-8601 UTC
	FString AssetPath;          // /Game/... object path
	FString RuleId;             // rule that motivated the fix (or "" for scripted)
	FString TransactionName;    // human label used in the FScopedTransaction
	bool    bRebuild = false;   // reimport-class: Revert must trigger one rebuild
	TMap<FString, FString> Before;
	TMap<FString, FString> After;
};

class FShintLodFixJournal
{
public:
	// Saved/ShintTools/lod_fix_journal.jsonl (per-project).
	static FString JournalPath();

	// Append one entry as a single JSONL line. Fills Id/Timestamp if empty and
	// returns the entry Id. Truncates the file oldest-first when it exceeds 5 MB.
	static FString Append(FShintLodJournalEntry& Entry);

	// Parse every journal line, newest last (file order is chronological).
	static TArray<FShintLodJournalEntry> LoadAll();

	// Look up a single entry by Id. Returns false if not found / unreadable.
	static bool Find(const FString& Id, FShintLodJournalEntry& Out);

private:
	static constexpr int64 kMaxBytes = 5 * 1024 * 1024;   // 5 MB cap (§13.6.3)
};
// [LOD-STRIP-END]
