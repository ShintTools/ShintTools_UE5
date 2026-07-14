// Copyright 2026 ShintTools. All Rights Reserved.
//
// [LOD-STRIP-BEGIN]
#include "ShintLodFixJournal.h"

#include "ShintTools.h"                  // LogShintTools
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

namespace
{
	// Monotonic counter so two fixes inside the same tick get distinct Ids.
	static int64 GShintJournalCounter = 0;

	TSharedRef<FJsonObject> MapToJson(const TMap<FString, FString>& Map)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		for (const auto& Pair : Map)
			Obj->SetStringField(Pair.Key, Pair.Value);
		return Obj;
	}

	void JsonToMap(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Out)
	{
		if (!Obj.IsValid()) return;
		for (const auto& Pair : Obj->Values)
		{
			FString S;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(S))
				Out.Add(Pair.Key, S);
		}
	}

	// Serialise one entry to a single-line (condensed) JSON string.
	FString EntryToLine(const FShintLodJournalEntry& E)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("id"), E.Id);
		Root->SetStringField(TEXT("timestamp"), E.Timestamp);
		Root->SetStringField(TEXT("asset_path"), E.AssetPath);
		Root->SetStringField(TEXT("rule_id"), E.RuleId);
		Root->SetStringField(TEXT("transaction"), E.TransactionName);
		Root->SetBoolField(TEXT("rebuild"), E.bRebuild);
		Root->SetObjectField(TEXT("before"), MapToJson(E.Before));
		Root->SetObjectField(TEXT("after"), MapToJson(E.After));

		FString Line;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(Root, Writer);
		return Line;
	}

	bool LineToEntry(const FString& Line, FShintLodJournalEntry& Out)
	{
		if (Line.IsEmpty()) return false;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			return false;

		Root->TryGetStringField(TEXT("id"), Out.Id);
		Root->TryGetStringField(TEXT("timestamp"), Out.Timestamp);
		Root->TryGetStringField(TEXT("asset_path"), Out.AssetPath);
		Root->TryGetStringField(TEXT("rule_id"), Out.RuleId);
		Root->TryGetStringField(TEXT("transaction"), Out.TransactionName);
		Root->TryGetBoolField(TEXT("rebuild"), Out.bRebuild);

		const TSharedPtr<FJsonObject>* Sub;
		if (Root->TryGetObjectField(TEXT("before"), Sub)) JsonToMap(*Sub, Out.Before);
		if (Root->TryGetObjectField(TEXT("after"), Sub))  JsonToMap(*Sub, Out.After);
		return !Out.Id.IsEmpty();
	}
}

FString FShintLodFixJournal::JournalPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("ShintTools"), TEXT("lod_fix_journal.jsonl"));
}

FString FShintLodFixJournal::Append(FShintLodJournalEntry& Entry)
{
	if (Entry.Timestamp.IsEmpty())
		Entry.Timestamp = FDateTime::UtcNow().ToIso8601();
	if (Entry.Id.IsEmpty())
		Entry.Id = FString::Printf(TEXT("%lld-%lld"),
			FDateTime::UtcNow().GetTicks(), ++GShintJournalCounter);

	const FString Path = JournalPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);

	const FString Line = EntryToLine(Entry) + LINE_TERMINATOR;
	if (!FFileHelper::SaveStringToFile(Line, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), EFileWrite::FILEWRITE_Append))
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("ShintLodFixJournal: could not append to %s"), *Path);
		return Entry.Id;
	}

	// Truncate oldest-first once past the cap. Cheap because it only fires when
	// the file actually crosses 5 MB (hundreds of thousands of fixes).
	if (IFileManager::Get().FileSize(*Path) > kMaxBytes)
	{
		TArray<FShintLodJournalEntry> All = LoadAll();
		// Drop the oldest half — keeps the rewrite rare and amortised O(1).
		const int32 Drop = All.Num() / 2;
		FString Rebuilt;
		for (int32 i = Drop; i < All.Num(); ++i)
			Rebuilt += EntryToLine(All[i]) + LINE_TERMINATOR;
		FFileHelper::SaveStringToFile(Rebuilt, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintLodFixJournal: truncated %d oldest entries (>5 MB)."), Drop);
	}

	return Entry.Id;
}

TArray<FShintLodJournalEntry> FShintLodFixJournal::LoadAll()
{
	TArray<FShintLodJournalEntry> Out;
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *JournalPath()))
		return Out;

	TArray<FString> Lines;
	Raw.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
	Out.Reserve(Lines.Num());
	for (const FString& Line : Lines)
	{
		FShintLodJournalEntry E;
		if (LineToEntry(Line, E)) Out.Add(MoveTemp(E));
	}
	return Out;
}

bool FShintLodFixJournal::Find(const FString& Id, FShintLodJournalEntry& Out)
{
	for (const FShintLodJournalEntry& E : LoadAll())
	{
		if (E.Id == Id) { Out = E; return true; }
	}
	return false;
}
// [LOD-STRIP-END]
