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
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("file_path"), AbsFilePath);
	Body->SetStringField(TEXT("content"),   Content);
	Body->SetStringField(TEXT("engine"),    Engine);

	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/code"), EShintHttpMethod::POST, Str,
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(FShintCoreClient::ParseValidateResponse(Raw));
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

	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: Scanning %d source files."), AbsFiles.Num());

	TArray<TSharedPtr<FJsonValue>> FilesArr;
	for (const FString& Abs : AbsFiles)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Abs)) continue;

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("file_path"), Abs);
		FO->SetStringField(TEXT("content"),   Content);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("files"),  FilesArr);
	Body->SetStringField(TEXT("engine"), TEXT("unreal"));

	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	TArray<FString> CapturedFiles = AbsFiles;

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/project"), EShintHttpMethod::POST, Str,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, CapturedFiles](const FShintRequestResult& Raw) mutable {
				FShintValidateResult Result = FShintCoreClient::ParseValidateResponse(Raw);
				Result.ScannedFilePaths = CapturedFiles;
				OnComplete.ExecuteIfBound(Result);
			}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — blueprints
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateBlueprints(
	const FString& ContentDir, FOnShintValidateComplete OnComplete)
{
	TArray<FString> Assets;
	IFileManager::Get().FindFilesRecursive(Assets, *ContentDir, TEXT("*.uasset"), true, false);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& P : Assets)
		Arr.Add(MakeShared<FJsonValueString>(P));

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("asset_paths"), Arr);
	Body->SetStringField(TEXT("engine"), TEXT("unreal"));

	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/blueprints"), EShintHttpMethod::POST, Str,
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
	// Group issues by absolute file path
	TMap<FString, TArray<const FShintCodeIssue*>> ByFile;
	for (const FShintCodeIssue& Issue : AcceptedIssues)
	{
		if (Issue.bIsAutoFixable && !Issue.FilePath.IsEmpty())
			ByFile.FindOrAdd(Issue.FilePath).Add(&Issue);
	}

	if (ByFile.IsEmpty())
	{
		FShintFixResult Empty;
		Empty.bSuccess          = true;
		Empty.TotalFixesSkipped = AcceptedIssues.Num();
		OnComplete.ExecuteIfBound(Empty);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> FilesArr;

	for (auto& Pair : ByFile)
	{
		const FString& AbsPath = Pair.Key;

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *AbsPath))
		{
			UE_LOG(LogShintTools, Error,
				TEXT("ShintCoreClient: Cannot read file for fix: %s"), *AbsPath);
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> IssArr;
		for (const FShintCodeIssue* Issue : Pair.Value)
		{
			TSharedRef<FJsonObject> IObj = MakeShared<FJsonObject>();
			IObj->SetStringField(TEXT("rule_id"),  Issue->RuleId);
			IObj->SetNumberField(TEXT("line"),      Issue->Line);
			IObj->SetStringField(TEXT("severity"), Issue->Severity);
			IssArr.Add(MakeShared<FJsonValueObject>(IObj));
		}

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("file_path"), AbsPath);
		FO->SetStringField(TEXT("content"),   Content);
		FO->SetArrayField (TEXT("issues"),    IssArr);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("files"), FilesArr);
	FString BodyStr; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body, W);

	SendRequest(Config.GetBaseUrl() + TEXT("/validate/fix"), EShintHttpMethod::POST, BodyStr,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable
			{
				FShintFixResult Result = FShintCoreClient::ParseFixResponse(Raw);
				if (!Result.bSuccess)
				{
					OnComplete.ExecuteIfBound(Result);
					return;
				}

				// Write corrected files back to disk
				for (FShintFixedFile& FF : Result.FixedFiles)
				{
					if (FF.FixesApplied == 0 || FF.FilePath.IsEmpty()) continue;

					if (!FFileHelper::SaveStringToFile(
						FF.CorrectedContent, *FF.FilePath,
						FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
					{
						UE_LOG(LogShintTools, Error,
							TEXT("ShintCoreClient: Failed to write fixed file: %s"), *FF.FilePath);
						Result.bSuccess      = false;
						Result.ErrorMessage += FString::Printf(TEXT("Write failed: %s\n"), *FF.FilePath);
					}
					else
					{
						UE_LOG(LogShintTools, Verbose,
							TEXT("ShintCoreClient: Wrote %d fix(es) -> %s"),
							FF.FixesApplied, *FF.FilePath);
					}
				}

				OnComplete.ExecuteIfBound(Result);
			}));
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

	FString BodyStr; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body, W);

	const FString Url = Config.DashboardUrl / TEXT("api/code-validator/analyze");

	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: Sending %d files to dashboard."), FilesArr.Num());

	SendRequest(Url, EShintHttpMethod::POST, BodyStr,
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
	TArray<FString> Assets;
	IFileManager::Get().FindFilesRecursive(Assets, *ContentDir, TEXT("*.uasset"), true, false);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& P : Assets)
	{
		// Convert disk path to UE package-style path for the server
		FString RelPath = P;
		FPaths::MakePathRelativeTo(RelPath, *ContentDir);
		RelPath = TEXT("/Game/") + RelPath.Replace(TEXT("\\"), TEXT("/"));
		RelPath = FPaths::GetBaseFilename(RelPath, false);   // remove .uasset
		Arr.Add(MakeShared<FJsonValueString>(RelPath));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("asset_paths"), Arr);
	Body->SetStringField(TEXT("engine"),     TEXT("unreal"));
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: Scanning %d assets."), Assets.Num());

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/scan"), EShintHttpMethod::POST, Str,
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			OnComplete.ExecuteIfBound(FShintCoreClient::ParseAssetScanResponse(Raw));
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
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/fix"), EShintHttpMethod::POST, Str,
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

	FString BodyStr; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&BodyStr);
	FJsonSerializer::Serialize(Body, W);

	const FString Url = Config.DashboardUrl / TEXT("api/naming-bot/analyze");

	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: Sending %d asset items to dashboard."), ItemsArr.Num());

	SendRequest(Url, EShintHttpMethod::POST, BodyStr,
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

	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Body, W);

	SendRequest(Config.GetBaseUrl() + TEXT("/dashboard/report"), EShintHttpMethod::POST, Str,
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
	Req->SetHeader(TEXT("User-Agent"),   TEXT("ShintTools-UE5/3.0"));

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
	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total"),         R.TotalIssues);
		(*Sum)->TryGetNumberField(TEXT("errors"),        R.TotalErrors);
		(*Sum)->TryGetNumberField(TEXT("warnings"),      R.TotalWarnings);
		(*Sum)->TryGetNumberField(TEXT("files_scanned"), R.FilesScanned);
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
			(*O)->TryGetStringField(TEXT("file_path"),      Issue.FilePath);
			(*O)->TryGetNumberField(TEXT("line"),           Issue.Line);
			(*O)->TryGetStringField(TEXT("snippet"),        Issue.Snippet);
			(*O)->TryGetStringField(TEXT("fix_suggestion"), Issue.FixSuggestion);
			(*O)->TryGetBoolField  (TEXT("is_auto_fixable"),Issue.bIsAutoFixable);
			Issue.bChecked = Issue.bIsAutoFixable;
			R.Issues.Add(MoveTemp(Issue));
		}
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
	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total_assets"),       R.TotalAssets);
		(*Sum)->TryGetNumberField(TEXT("invalid_assets"),     R.InvalidAssets);
		(*Sum)->TryGetNumberField(TEXT("scan_time_seconds"),  R.ScanTimeSeconds);
	}

	const TArray<TSharedPtr<FJsonValue>>* IssArr = nullptr;
	if (J->TryGetArrayField(TEXT("issues"), IssArr) && IssArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IssArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O) continue;
			FShintAssetIssue Issue;
			(*O)->TryGetStringField(TEXT("asset_path"),     Issue.AssetPath);
			(*O)->TryGetStringField(TEXT("current_name"),   Issue.CurrentName);
			(*O)->TryGetStringField(TEXT("suggested_name"), Issue.SuggestedName);
			(*O)->TryGetStringField(TEXT("reason"),         Issue.Reason);
			(*O)->TryGetStringField(TEXT("asset_type"),     Issue.AssetType);
			Issue.bChecked = true;
			R.Issues.Add(MoveTemp(Issue));
		}
	}
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

	const TArray<TSharedPtr<FJsonValue>>* FilesArr = nullptr;
	if (J->TryGetArrayField(TEXT("fixed_files"), FilesArr) && FilesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *FilesArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O) continue;
			FShintFixedFile FF;
			(*O)->TryGetStringField(TEXT("file_path"),         FF.FilePath);
			(*O)->TryGetStringField(TEXT("corrected_content"), FF.CorrectedContent);
			(*O)->TryGetNumberField(TEXT("fixes_applied"),     FF.FixesApplied);
			(*O)->TryGetNumberField(TEXT("fixes_skipped"),     FF.FixesSkipped);
			R.FixedFiles.Add(MoveTemp(FF));
		}
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
