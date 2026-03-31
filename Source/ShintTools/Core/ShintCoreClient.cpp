// Copyright ShintTools. All Rights Reserved.

#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Blueprint metadata (Asset Registry only — no loading)
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

FShintCoreClient::FShintCoreClient()  { LoadConfig(); }
FShintCoreClient::~FShintCoreClient() {}

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

bool FShintCoreClient::LoadConfig()
{
	Config = FShintCoreConfig();
	const FString CfgPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));
	if (!FPaths::FileExists(CfgPath)) return false;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *CfgPath)) return false;

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Json) || !Json.IsValid()) return false;

	int32 Port = 0;
	if (Json->TryGetNumberField(TEXT("core_port"), Port) && Port > 0) Config.CorePort = Port;

	bool bAuto = false;
	if (Json->TryGetBoolField(TEXT("auto_start_core"), bAuto)) Config.bAutoStartCore = bAuto;

	FString S;
	if (Json->TryGetStringField(TEXT("project_name"), S)) Config.ProjectName = S;
	if (Json->TryGetStringField(TEXT("project_id"),   S)) Config.ProjectId   = S;
	if (Json->TryGetStringField(TEXT("api_key"),      S)) Config.ApiKey      = S;
	if (Json->TryGetStringField(TEXT("dashboard_url"),S)) Config.DashboardUrl= S;

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: Config loaded. Port=%d"), Config.CorePort);
	return true;
}

bool FShintCoreClient::SaveConfig() const
{
	const FString CfgPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));

	// Read the existing JSON so we preserve unknown fields (modules, naming, etc.)
	TSharedPtr<FJsonObject> Json;
	FString Raw;
	if (FFileHelper::LoadFileToString(Raw, *CfgPath))
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		FJsonSerializer::Deserialize(R, Json);
	}
	if (!Json.IsValid()) Json = MakeShared<FJsonObject>();

	// Overwrite config fields
	Json->SetNumberField(TEXT("core_port"),       Config.CorePort);
	Json->SetBoolField(TEXT("auto_start_core"),   Config.bAutoStartCore);
	Json->SetStringField(TEXT("project_name"),    Config.ProjectName);
	Json->SetStringField(TEXT("project_id"),      Config.ProjectId);
	Json->SetStringField(TEXT("api_key"),         Config.ApiKey);
	Json->SetStringField(TEXT("dashboard_url"),   Config.DashboardUrl);

	const FString Out = SerializeJson(Json.ToSharedRef());
	return FFileHelper::SaveStringToFile(Out, *CfgPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connectivity
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CheckHealth(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/health"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

void FShintCoreClient::Ping(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/ping"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — single file
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateCode(
	const FString& AbsFilePath, const FString& Content,
	const FString& Engine, FOnShintValidateComplete OnComplete)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("file_path"), AbsFilePath);
	Body->SetStringField(TEXT("content"),   Content);
	Body->SetStringField(TEXT("engine"),    Engine);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/code"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(ParseValidateResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — full project
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateProject(
	const FString& SourceDir, FOnShintValidateComplete OnComplete)
{
	TArray<FString> AbsFiles;
	CollectSourceFiles(SourceDir, AbsFiles);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Scanning %d source files from %s"), AbsFiles.Num(), *SourceDir);

	// Map filename -> absolute path (used later to resolve server responses)
	TMap<FString, FString> FilenameLookup;
	TArray<TSharedPtr<FJsonValue>> FilesArr;

	// Max allowed content per file to avoid oversized JSON payloads

	for (const FString& Abs : AbsFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Abs))
			continue;

		const FString Filename = FPaths::GetCleanFilename(Abs);
		const FString Ext = FPaths::GetExtension(Abs).ToLower();
		const FString TypeStr = (Ext == TEXT("h") || Ext == TEXT("hpp")) ? TEXT("header") : TEXT("cpp");

		TArray<FString> Lines;
		const int32 LineCount = Content.ParseIntoArray(Lines, TEXT("\n"), false);

		// No content truncation — send full file to server for accurate analysis

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"), Filename);
		FO->SetStringField(TEXT("path"), Abs);
		FO->SetStringField(TEXT("type"), TypeStr);
		FO->SetStringField(TEXT("content"), Content);
		FO->SetNumberField(TEXT("lines_count"), LineCount);

		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
		FilenameLookup.Add(Filename, Abs);
	}

	// Early exit if no valid files were collected
	if (FilesArr.IsEmpty())
	{
		FShintValidateResult Empty;
		Empty.bSuccess = true;
		OnComplete.ExecuteIfBound(Empty);
		return;
	}

	// Build request body
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_id"), Config.ProjectId);
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"), TEXT("unreal"));
	Body->SetArrayField(TEXT("files"), FilesArr);

	TArray<FString> CapturedFiles = AbsFiles;

	// Serialize JSON ONLY ONCE to avoid inconsistencies
	const FString BodyStr = SerializeJson(Body);

	// Log payload size for debugging purposes
	UE_LOG(LogShintTools, Log, TEXT("Validate Project JSON size: %d chars"), BodyStr.Len());

	// Chunked logging to avoid log truncation
	const int32 LogChunkSize = 1000;
	for (int32 i = 0; i < BodyStr.Len(); i += LogChunkSize)
	{
		UE_LOG(LogShintTools, Verbose, TEXT("%s"), *BodyStr.Mid(i, LogChunkSize));
	}

	// Optional: dump full request to disk for debugging
	// FFileHelper::SaveStringToFile(BodyStr, TEXT("C:/temp/validate_request.json"));
	
	SendRequest(
		Config.GetBaseUrl() + TEXT("/validate/project"),
		EShintHttpMethod::POST,
		BodyStr,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, CapturedFiles, FilenameLookup](const FShintRequestResult& Raw) mutable
			{
				FShintValidateResult Result = FShintCoreClient::ParseValidateResponse(Raw);
				Result.ScannedFilePaths = CapturedFiles;

				// Resolve server-returned file paths (which may contain only filenames)
				for (FShintCodeIssue& Issue : Result.Issues)
				{
					if (!Issue.FilePath.IsEmpty() && !FPaths::FileExists(Issue.FilePath))
					{
						const FString Fn = FPaths::GetCleanFilename(Issue.FilePath);
						if (const FString* Found = FilenameLookup.Find(Fn))
						{
							Issue.FilePath = *Found;
						}
					}
				}

				UE_LOG(LogShintTools, Log, TEXT("ValidateProject: %d issues from server"), Result.Issues.Num());

				OnComplete.ExecuteIfBound(Result);
			}
		)
	);
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — blueprints
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateBlueprints(
	const FString& ContentDir, FOnShintValidateComplete OnComplete)
{
	// Discover project blueprints via Asset Registry — metadata only, NO loading.
	// GetAsset() loads the full BP into memory and crashes with heavy/corrupt assets.
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(TEXT("/Game"));
	Filter.bRecursivePaths   = true;
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> BlueprintAssets;
	AR.GetAssets(Filter, BlueprintAssets);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Found %d project blueprints under /Game/"), BlueprintAssets.Num());

	TArray<TSharedPtr<FJsonValue>> FilesArr;

	// Empty arrays/object for the contract fields — no loading needed
	TArray<TSharedPtr<FJsonValue>> EmptyArr;
	TSharedRef<FJsonObject> EmptyStats = MakeShared<FJsonObject>();
	EmptyStats->SetNumberField(TEXT("total_nodes"),        0);
	EmptyStats->SetNumberField(TEXT("cast_nodes"),         0);
	EmptyStats->SetBoolField  (TEXT("tick_enabled"),       false);
	EmptyStats->SetNumberField(TEXT("disconnected_nodes"), 0);

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		const FString BPName    = AssetData.AssetName.ToString();
		const FString BPPackage = AssetData.PackageName.ToString(); // /Game/Blueprints/BP_X

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"),      BPName);
		FO->SetStringField(TEXT("path"),      BPPackage);
		FO->SetStringField(TEXT("type"),      TEXT("blueprint"));
		FO->SetArrayField (TEXT("graphs"),    EmptyArr);
		FO->SetArrayField (TEXT("variables"), EmptyArr);
		FO->SetArrayField (TEXT("functions"), EmptyArr);
		FO->SetObjectField(TEXT("stats"),     EmptyStats);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_id"),   Config.ProjectId);
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetArrayField (TEXT("files"),        FilesArr);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Sending %d blueprint entries to /validate/blueprints"), FilesArr.Num());

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/blueprints"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(FShintCoreClient::ParseValidateResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — apply fixes
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ApplyCodeFixes(
	const TArray<FShintCodeIssue>& AcceptedIssues, FOnShintFixComplete OnComplete)
{
	// Apply fixes LOCALLY — replace snippet lines with fix_suggestion in source files.
	// No server call needed: the scan already gave us snippet + fix_suggestion.

	// Group issues by absolute file path — skip blueprint / package paths
	TMap<FString, TArray<const FShintCodeIssue*>> ByFile;
	int32 SkippedBP = 0;
	int32 SkippedNoFix = 0;
	for (const FShintCodeIssue& Issue : AcceptedIssues)
	{
		if (Issue.FilePath.IsEmpty()) continue;
		if (Issue.FilePath.StartsWith(TEXT("/Game/")) || Issue.FilePath.StartsWith(TEXT("/Engine/")))
		{
			++SkippedBP;
			continue;
		}
		if (Issue.FixSuggestion.IsEmpty())
		{
			UE_LOG(LogShintTools, Warning,
				TEXT("ApplyFix: No fix_suggestion for [%s] line %d in %s — skipping"),
				*Issue.RuleId, Issue.Line, *Issue.FilePath);
			++SkippedNoFix;
			continue;
		}
		ByFile.FindOrAdd(Issue.FilePath).Add(&Issue);
	}
	if (SkippedNoFix > 0)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: %d issue(s) have no fix_suggestion (server did not provide one)"), SkippedNoFix);
	}

	if (SkippedBP > 0)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: Skipped %d blueprint issue(s) (not fixable via disk write)"), SkippedBP);
	}

	if (ByFile.IsEmpty())
	{
		FShintFixResult Empty;
		Empty.bSuccess          = true;
		Empty.TotalFixesSkipped = AcceptedIssues.Num();
		if (SkippedBP > 0)
			Empty.ErrorMessage = FString::Printf(
				TEXT("%d blueprint issue(s) skipped — blueprint fixes are not supported yet."), SkippedBP);
		OnComplete.ExecuteIfBound(Empty);
		return;
	}

	FShintFixResult Result;
	Result.bSuccess = true;

	for (auto& Pair : ByFile)
	{
		const FString& AbsPath = Pair.Key;
		const TArray<const FShintCodeIssue*>& Issues = Pair.Value;

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *AbsPath))
		{
			UE_LOG(LogShintTools, Error, TEXT("ApplyFix: Cannot read file: %s"), *AbsPath);
			Result.TotalFixesSkipped += Issues.Num();
			continue;
		}

		// Split into lines, apply fixes by line number (1-based), then reassemble.
		// Process from highest line number to lowest so indices stay valid.
		TArray<FString> Lines;
		Content.ParseIntoArray(Lines, TEXT("\n"), false);

		// Sort issues by line descending to avoid index shifting
		TArray<const FShintCodeIssue*> Sorted = Issues;
		Sorted.Sort([](const FShintCodeIssue& A, const FShintCodeIssue& B) {
			return A.Line > B.Line;
		});

		int32 Applied = 0;
		int32 Skipped = 0;

		for (const FShintCodeIssue* Issue : Sorted)
		{
			const int32 Idx = Issue->Line - 1; // 0-based

			if (Idx < 0 || Idx >= Lines.Num())
			{
				UE_LOG(LogShintTools, Warning,
					TEXT("ApplyFix: Line %d out of range (%d lines) in %s [%s]"),
					Issue->Line, Lines.Num(), *AbsPath, *Issue->RuleId);
				++Skipped;
				continue;
			}

			// Verify the snippet matches the line (trimmed comparison)
			const FString& CurrentLine = Lines[Idx];
			const FString TrimmedCurrent = CurrentLine.TrimStartAndEnd();
			const FString TrimmedSnippet = Issue->Snippet.TrimStartAndEnd();

			if (!TrimmedSnippet.IsEmpty() && !TrimmedCurrent.Contains(TrimmedSnippet))
			{
				UE_LOG(LogShintTools, Warning,
					TEXT("ApplyFix: Snippet mismatch at line %d in %s [%s]. Expected='%s' Got='%s'"),
					Issue->Line, *AbsPath, *Issue->RuleId,
					*TrimmedSnippet, *TrimmedCurrent);
				++Skipped;
				continue;
			}

			// Preserve leading whitespace from the original line
			FString Leading;
			for (int32 c = 0; c < CurrentLine.Len(); ++c)
			{
				const TCHAR Ch = CurrentLine[c];
				if (Ch == TEXT(' ') || Ch == TEXT('\t'))
					Leading.AppendChar(Ch);
				else
					break;
			}

			// Replace the line with fix_suggestion (preserving indentation)
			const FString FixTrimmed = Issue->FixSuggestion.TrimStartAndEnd();
			Lines[Idx] = Leading + FixTrimmed;

			UE_LOG(LogShintTools, Log,
				TEXT("ApplyFix: [%s] line %d: '%s' -> '%s'"),
				*Issue->RuleId, Issue->Line, *TrimmedCurrent, *FixTrimmed);
			++Applied;
		}

		if (Applied > 0)
		{
			// Reassemble and write back
			const FString NewContent = FString::Join(Lines, TEXT("\n"));
			if (FFileHelper::SaveStringToFile(NewContent, *AbsPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogShintTools, Log,
					TEXT("ApplyFix: Wrote %d fix(es) to %s"), Applied, *AbsPath);

				FShintFixedFile FF;
				FF.FilePath         = AbsPath;
				FF.CorrectedContent = NewContent;
				FF.FixesApplied     = Applied;
				FF.FixesSkipped     = Skipped;
				Result.FixedFiles.Add(MoveTemp(FF));
			}
			else
			{
				UE_LOG(LogShintTools, Error, TEXT("ApplyFix: Failed to write: %s"), *AbsPath);
				Result.bSuccess = false;
				Result.ErrorMessage += FString::Printf(TEXT("Write failed: %s\n"), *AbsPath);
				Skipped += Applied;
				Applied = 0;
			}
		}

		Result.TotalFixesApplied += Applied;
		Result.TotalFixesSkipped += Skipped;
	}

	UE_LOG(LogShintTools, Log, TEXT("ApplyFix: Done — %d applied, %d skipped across %d file(s)"),
		Result.TotalFixesApplied, Result.TotalFixesSkipped, Result.FixedFiles.Num());

	OnComplete.ExecuteIfBound(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// External Web Dashboard — Code Validator
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendCodeValidatorToDashboard(
	const FShintValidateResult& LastResult, FOnShintWebDashboardComplete OnComplete)
{
	if (!Config.HasExternalDashboard())
	{
		FShintWebDashboardResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("api_key, project_id, or dashboard_url not set in shinttools.config.json");
		OnComplete.ExecuteIfBound(Err);
		return;
	}

	// Build files array — re-read from disk
	TArray<TSharedPtr<FJsonValue>> FilesArr;
	TSet<FString> SeenPaths;

	for (const FShintCodeIssue& Issue : LastResult.Issues)
	{
		if (Issue.FilePath.IsEmpty()) continue;
		if (SeenPaths.Contains(Issue.FilePath)) continue;
		SeenPaths.Add(Issue.FilePath);
	}

	// Include all scanned files (even those with no issues)
	for (const FString& AbsPath : LastResult.ScannedFilePaths)
		SeenPaths.Add(AbsPath);

	for (const FString& AbsPath : SeenPaths)
	{
		FString Content;
		FFileHelper::LoadFileToString(Content, *AbsPath);

		const FString Filename = FPaths::GetCleanFilename(AbsPath);
		const FString RelPath  = FPaths::GetPath(AbsPath);
		const FString Ext      = FPaths::GetExtension(AbsPath).ToLower();
		const FString TypeStr  = (Ext == TEXT("h") || Ext == TEXT("hpp")) ? TEXT("header") : TEXT("cpp");
		TArray<FString> Lines;
		const int32 LineCount  = Content.IsEmpty() ? 0 : Content.ParseIntoArray(Lines, TEXT("\n"), false);

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"),        Filename);
		FO->SetStringField(TEXT("path"),        RelPath);
		FO->SetStringField(TEXT("type"),        TypeStr);
		FO->SetStringField(TEXT("content"),     Content);
		FO->SetNumberField(TEXT("lines_count"), LineCount);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_id"),   Config.ProjectId);
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("api_key"),      Config.ApiKey);
	Body->SetArrayField (TEXT("files"),        FilesArr);

	const FString Url = Config.DashboardUrl / TEXT("api/code-validator/analyze");
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Sending %d files to dashboard at %s"), FilesArr.Num(), *Url);

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			FShintWebDashboardResult R;
			R.bSuccess     = Raw.bSuccess;
			R.ErrorMessage = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			R.ResponseBody = Raw.ResponseBody;
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot — scan
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ScanAssetNaming(
	const FString& ContentDir, FOnShintAssetScanComplete OnComplete)
{
	// Use Asset Registry to get all project assets with their types
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter AssetFilter;
	AssetFilter.PackagePaths.Add(TEXT("/Game"));
	AssetFilter.bRecursivePaths = true;

	TArray<FAssetData> AllAssets;
	AR.GetAssets(AssetFilter, AllAssets);

	// Server expects: { project_id, project_name, engine,
	//   asset_paths: [{asset_path, name, type, category}] }
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AD : AllAssets)
	{
		const FString PackagePath = AD.PackageName.ToString();
		const FString AssetName   = AD.AssetName.ToString();
		const FString AssetClass  = AD.AssetClassPath.GetAssetName().ToString();

		TSharedRef<FJsonObject> AObj = MakeShared<FJsonObject>();
		AObj->SetStringField(TEXT("asset_path"), PackagePath);
		AObj->SetStringField(TEXT("name"),       AssetName);
		AObj->SetStringField(TEXT("type"),       AssetClass);
		AObj->SetStringField(TEXT("category"),   TEXT(""));
		Arr.Add(MakeShared<FJsonValueObject>(AObj));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_id"),   Config.ProjectId);
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetArrayField(TEXT("asset_paths"),   Arr);

	const FString BodyStr = SerializeJson(Body);

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Scanning %d assets from /Game/"), AllAssets.Num());
	UE_LOG(LogShintTools, Log, TEXT("AssetScan REQUEST JSON (first 3000 chars):\n%s"), *BodyStr.Left(3000));

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/scan"), EShintHttpMethod::POST, BodyStr,
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(ParseAssetScanResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot — report server-side (the actual rename happens in the panel
// via IAssetTools; this just records it for MongoDB / local history)
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ReportAssetFixesToServer(
	const TArray<FShintAssetIssue>& Fixed, FOnShintAssetFixComplete OnComplete)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FShintAssetIssue& I : Fixed)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"),     I.AssetPath);
		O->SetStringField(TEXT("current_name"),   I.CurrentName);
		O->SetStringField(TEXT("suggested_name"), I.SuggestedName);
		O->SetStringField(TEXT("asset_type"),     I.AssetType);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("issues"), Arr);

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/fix"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete, Count = Fixed.Num()](const FShintRequestResult& Raw) mutable {
			FShintAssetFixResult Result;
			Result.bSuccess      = Raw.bSuccess;
			Result.AssetsRenamed = Count;
			Result.ErrorMessage  = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			OnComplete.ExecuteIfBound(Result);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// External Web Dashboard — Asset Naming Bot
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendAssetNamingToDashboard(
	const FShintAssetScanResult& LastResult, FOnShintWebDashboardComplete OnComplete)
{
	if (!Config.HasExternalDashboard())
	{
		FShintWebDashboardResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("api_key, project_id, or dashboard_url not set in shinttools.config.json");
		OnComplete.ExecuteIfBound(Err);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ItemsArr;
	for (const FShintAssetIssue& Issue : LastResult.Issues)
	{
		const FString Name     = FPaths::GetBaseFilename(Issue.AssetPath);
		const FString Path     = FPaths::GetPath(Issue.AssetPath);
		const FString Category = AssetTypeToCategory(Issue.AssetType);

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),     Name);
		O->SetStringField(TEXT("path"),     Path);
		O->SetStringField(TEXT("type"),     TEXT("asset"));
		O->SetStringField(TEXT("category"), Category);
		ItemsArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_id"),   Config.ProjectId);
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("api_key"),      Config.ApiKey);
	Body->SetArrayField (TEXT("items"),        ItemsArr);

	const FString Url = Config.DashboardUrl / TEXT("api/naming-bot/analyze");
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Sending %d asset items to dashboard at %s"), ItemsArr.Num(), *Url);

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			FShintWebDashboardResult R;
			R.bSuccess     = Raw.bSuccess;
			R.ErrorMessage = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			R.ResponseBody = Raw.ResponseBody;
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Local MongoDB dashboard (legacy)
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendDashboardReport(
	const FShintDashboardReport& Report, FOnShintDashboardComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Report.ProjectName);
	Body->SetStringField(TEXT("engine"),       Report.Engine);
	Body->SetStringField(TEXT("report_type"),  Report.ReportType);

	if (Report.ReportType == TEXT("code_validator"))
	{
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("files_scanned"),  Report.Code_FilesScanned);
		D->SetNumberField(TEXT("total_issues"),   Report.Code_TotalIssues);
		D->SetNumberField(TEXT("total_errors"),   Report.Code_TotalErrors);
		D->SetNumberField(TEXT("total_warnings"), Report.Code_TotalWarnings);
		Body->SetObjectField(TEXT("code_validator"), D);
	}
	else if (Report.ReportType == TEXT("asset_naming"))
	{
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("total_scanned"),  Report.Asset_TotalScanned);
		D->SetNumberField(TEXT("invalid_assets"), Report.Asset_InvalidAssets);
		D->SetNumberField(TEXT("scan_time_s"),    Report.Asset_ScanTime);
		Body->SetObjectField(TEXT("asset_naming"), D);
	}

	SendRequest(Config.GetBaseUrl() + TEXT("/dashboard/report"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			FShintDashboardResult R;
			R.bSuccess     = Raw.bSuccess;
			R.ErrorMessage = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic HTTP request
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendRequest(
	const FString& FullUrl, EShintHttpMethod Method,
	const FString& Body, FOnShintRequestComplete OnComplete,
	const TMap<FString, FString>& ExtraHeaders)
{
	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: %s %s"), *MethodToString(Method), *FullUrl);

	FHttpModule& Http = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();

	Req->SetURL(FullUrl);
	Req->SetVerb(MethodToString(Method));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"),       TEXT("application/json"));
	Req->SetHeader(TEXT("User-Agent"),   TEXT("ShintTools-UE5/1.1"));

	for (const auto& KV : ExtraHeaders)
		Req->SetHeader(KV.Key, KV.Value);

	if (!Body.IsEmpty() &&
	    (Method == EShintHttpMethod::POST || Method == EShintHttpMethod::PUT))
	{
		Req->SetContentAsString(Body);
	}

	Req->OnProcessRequestComplete().BindRaw(
		this, &FShintCoreClient::OnHttpRequestComplete, OnComplete);
	Req->SetTimeout(90.0f);  // generous for full-project scans

	if (!Req->ProcessRequest())
	{
		FShintRequestResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		OnComplete.ExecuteIfBound(Err);
	}
}

void FShintCoreClient::OnHttpRequestComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete)
{
	FShintRequestResult Result;
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bSuccess     = false;
		Result.ErrorMessage = TEXT("Connection failed — Core Engine may not be running.");
		OnComplete.ExecuteIfBound(Result); return;
	}
	Result.StatusCode   = Response->GetResponseCode();
	Result.ResponseBody = Response->GetContentAsString();
	Result.bSuccess     = (Result.StatusCode >= 200 && Result.StatusCode < 300);
	if (!Result.bSuccess)
		Result.ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"),
			Result.StatusCode, *Result.ResponseBody);
	OnComplete.ExecuteIfBound(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse helpers
// ─────────────────────────────────────────────────────────────────────────────

FShintValidateResult FShintCoreClient::ParseValidateResponse(const FShintRequestResult& Raw)
{
	FShintValidateResult R;
	R.StatusCode = Raw.StatusCode;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Failed to parse validate response."); return R; }

	R.bSuccess = true;

	UE_LOG(LogShintTools, Log, TEXT("ParseValidate: Response length=%d, first 500 chars: %s"),
		Raw.ResponseBody.Len(), *Raw.ResponseBody.Left(500));

	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total"),         R.TotalIssues);
		(*Sum)->TryGetNumberField(TEXT("errors"),        R.TotalErrors);
		(*Sum)->TryGetNumberField(TEXT("warnings"),      R.TotalWarnings);
		(*Sum)->TryGetNumberField(TEXT("files_scanned"), R.FilesScanned);

		UE_LOG(LogShintTools, Log, TEXT("ParseValidate: summary total=%d errors=%d warnings=%d files=%d"),
			R.TotalIssues, R.TotalErrors, R.TotalWarnings, R.FilesScanned);
	}

	const TArray<TSharedPtr<FJsonValue>>* IssArr = nullptr;
	if (J->TryGetArrayField(TEXT("issues"), IssArr) && IssArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IssArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O) continue;
			FShintCodeIssue Issue;
			(*O)->TryGetStringField(TEXT("rule_id"),        Issue.RuleId);
			(*O)->TryGetStringField(TEXT("severity"),       Issue.Severity);
			(*O)->TryGetStringField(TEXT("message"),        Issue.Message);
			// Server may return "asset_path" (blueprints) or "file_path" (C++)
			if (!(*O)->TryGetStringField(TEXT("file_path"), Issue.FilePath))
				(*O)->TryGetStringField(TEXT("asset_path"), Issue.FilePath);
			(*O)->TryGetNumberField(TEXT("line"),           Issue.Line);
			(*O)->TryGetStringField(TEXT("snippet"),        Issue.Snippet);
			(*O)->TryGetStringField(TEXT("fix_suggestion"), Issue.FixSuggestion);
			(*O)->TryGetBoolField  (TEXT("is_auto_fixable"),Issue.bIsAutoFixable);
			(*O)->TryGetStringField(TEXT("class"),          Issue.Class);
			(*O)->TryGetStringField(TEXT("category"),       Issue.Category);
			(*O)->TryGetStringField(TEXT("graph"),          Issue.Graph);
			// Only truly auto-fixable if server provided both snippet and fix_suggestion
			if (Issue.Snippet.IsEmpty() || Issue.FixSuggestion.IsEmpty())
				Issue.bIsAutoFixable = false;
			Issue.bChecked = Issue.bIsAutoFixable;
			R.Issues.Add(MoveTemp(Issue));
		}
		UE_LOG(LogShintTools, Log, TEXT("ParseValidate: Parsed %d issues from 'issues' array (array had %d entries)"),
			R.Issues.Num(), IssArr->Num());
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("ParseValidate: No 'issues' array found in response"));
	}
	return R;
}

FShintAssetScanResult FShintCoreClient::ParseAssetScanResponse(const FShintRequestResult& Raw)
{
	FShintAssetScanResult R;
	R.StatusCode = Raw.StatusCode;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Parse failed."); return R; }

	R.bSuccess = true;

	UE_LOG(LogShintTools, Log, TEXT("AssetScan: Raw response: %s"),
		*Raw.ResponseBody.Left(2000));

	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total_assets"),       R.TotalAssets);
		(*Sum)->TryGetNumberField(TEXT("invalid_assets"),     R.InvalidAssets);
		(*Sum)->TryGetNumberField(TEXT("scan_time_seconds"),  R.ScanTimeSeconds);
	}

	// Try multiple possible array field names the server may return
	const TArray<TSharedPtr<FJsonValue>>* IssArr = nullptr;
	if (!J->TryGetArrayField(TEXT("issues"), IssArr) || !IssArr)
	{
		// Fallback: server may return "violations" or "results" instead of "issues"
		if (!J->TryGetArrayField(TEXT("violations"), IssArr) || !IssArr)
		{
			J->TryGetArrayField(TEXT("results"), IssArr);
		}
	}

	if (IssArr)
	{
		UE_LOG(LogShintTools, Log, TEXT("AssetScan: Found %d issue entries in response"), IssArr->Num());

		for (const TSharedPtr<FJsonValue>& V : *IssArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O) continue;
			FShintAssetIssue Issue;
			// Try both possible field names for path
			if (!(*O)->TryGetStringField(TEXT("asset_path"), Issue.AssetPath))
				(*O)->TryGetStringField(TEXT("path"), Issue.AssetPath);
			// Try both possible field names for current name
			if (!(*O)->TryGetStringField(TEXT("current_name"), Issue.CurrentName))
				(*O)->TryGetStringField(TEXT("name"), Issue.CurrentName);
			(*O)->TryGetStringField(TEXT("suggested_name"), Issue.SuggestedName);
			// Try both possible field names for reason
			if (!(*O)->TryGetStringField(TEXT("reason"), Issue.Reason))
				(*O)->TryGetStringField(TEXT("message"), Issue.Reason);
			// Try both possible field names for asset type
			if (!(*O)->TryGetStringField(TEXT("asset_type"), Issue.AssetType))
				(*O)->TryGetStringField(TEXT("type"), Issue.AssetType);
			Issue.bChecked = true;
			R.Issues.Add(MoveTemp(Issue));
		}
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("AssetScan: No 'issues', 'violations', or 'results' array found in response"));
	}

	UE_LOG(LogShintTools, Log, TEXT("AssetScan: Parsed %d issues, TotalAssets=%d, InvalidAssets=%d"),
		R.Issues.Num(), R.TotalAssets, R.InvalidAssets);

	return R;
}

FShintFixResult FShintCoreClient::ParseFixResponse(const FShintRequestResult& Raw)
{
	FShintFixResult R;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Failed to parse fix response."); return R; }

	R.bSuccess = true;
	J->TryGetNumberField(TEXT("total_fixes_applied"), R.TotalFixesApplied);
	J->TryGetNumberField(TEXT("total_fixes_skipped"), R.TotalFixesSkipped);

	// Try "fixed_files" then "files" as fallback array name
	const TArray<TSharedPtr<FJsonValue>>* FilesArr = nullptr;
	if (!J->TryGetArrayField(TEXT("fixed_files"), FilesArr) || !FilesArr)
		J->TryGetArrayField(TEXT("files"), FilesArr);

	if (FilesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *FilesArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O) continue;
			FShintFixedFile FF;
			// Try multiple field names for path
			if (!(*O)->TryGetStringField(TEXT("file_path"), FF.FilePath))
				(*O)->TryGetStringField(TEXT("path"), FF.FilePath);
			// Try multiple field names for corrected content
			if (!(*O)->TryGetStringField(TEXT("corrected_content"), FF.CorrectedContent))
				(*O)->TryGetStringField(TEXT("content"), FF.CorrectedContent);
			(*O)->TryGetNumberField(TEXT("fixes_applied"), FF.FixesApplied);
			(*O)->TryGetNumberField(TEXT("fixes_skipped"), FF.FixesSkipped);

			// If server returned content but no fixes_applied count, infer it's at least 1
			if (FF.FixesApplied == 0 && !FF.CorrectedContent.IsEmpty())
				FF.FixesApplied = 1;

			R.FixedFiles.Add(MoveTemp(FF));
		}
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("ParseFix: No 'fixed_files' or 'files' array in response"));
	}
	return R;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CollectSourceFiles(const FString& Dir, TArray<FString>& Out)
{
	TArray<FString> Cpp, H;
	IFileManager::Get().FindFilesRecursive(Cpp, *Dir, TEXT("*.cpp"), true, false);
	IFileManager::Get().FindFilesRecursive(H,   *Dir, TEXT("*.h"),   true, false);
	Out.Append(Cpp); Out.Append(H);
}

FString FShintCoreClient::AssetTypeToCategory(const FString& AssetType)
{
	if (AssetType == TEXT("Texture2D") || AssetType.Contains(TEXT("Texture")))
		return TEXT("texture");
	if (AssetType.Contains(TEXT("Mesh")))
		return TEXT("mesh");
	if (AssetType.Contains(TEXT("Material")))
		return TEXT("material");
	if (AssetType.Contains(TEXT("Blueprint")) || AssetType.Contains(TEXT("Widget")))
		return TEXT("blueprint");
	if (AssetType.Contains(TEXT("Sound")) || AssetType.Contains(TEXT("Audio")))
		return TEXT("audio");
	if (AssetType.Contains(TEXT("Anim")))
		return TEXT("animation");
	return TEXT("asset");
}

FString FShintCoreClient::MethodToString(EShintHttpMethod Method)
{
	switch (Method)
	{
	case EShintHttpMethod::GET:     return TEXT("GET");
	case EShintHttpMethod::POST:    return TEXT("POST");
	case EShintHttpMethod::PUT:     return TEXT("PUT");
	case EShintHttpMethod::DELETE_: return TEXT("DELETE");
	default:                        return TEXT("GET");
	}
}

FString FShintCoreClient::SerializeJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj, W);
	return Out;
}
