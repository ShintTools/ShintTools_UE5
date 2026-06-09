// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintDashboardSync.h"

#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"


namespace
{
	// Both endpoints return JSON like { "error": "..." } on failure;
	// the previous implementation duplicated the exact same lambda in
	// SendCodeValidator and SendAssetNaming to extract that field. One
	// helper consumed by both call sites keeps the wire-error UX
	// consistent: "HTTP 401: Invalid api_key" instead of a generic
	// "Send failed" toast.
	FShintWebDashboardResult MakeResultFromRaw(const FShintRequestResult& Raw)
	{
		FShintWebDashboardResult R;
		R.bSuccess     = Raw.bSuccess;
		R.ResponseBody = Raw.ResponseBody;
		if (Raw.bSuccess)
		{
			return R;
		}
		R.ErrorMessage = Raw.ErrorMessage;
		if (Raw.ResponseBody.IsEmpty())
		{
			return R;
		}
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Raw.ResponseBody);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			FString ErrField;
			if (Obj->TryGetStringField(TEXT("error"), ErrField)
				&& !ErrField.IsEmpty())
			{
				R.ErrorMessage = FString::Printf(
					TEXT("HTTP %d: %s"),
					Raw.StatusCode, *ErrField);
			}
		}
		if (R.ErrorMessage.IsEmpty())
		{
			R.ErrorMessage = FString::Printf(
				TEXT("HTTP %d: %s"),
				Raw.StatusCode,
				*Raw.ResponseBody.Left(120));
		}
		return R;
	}

	// Short-circuit when the prerequisite config is missing. Returns
	// true and fires OnComplete with the error if config is incomplete;
	// false means the caller should proceed with the actual POST.
	bool ShortCircuitOnMissingConfig(const FShintCoreConfig& Cfg,
		FOnShintWebDashboardComplete OnComplete)
	{
		if (!Cfg.HasExternalDashboard())
		{
			FShintWebDashboardResult Err;
			Err.bSuccess     = false;
			Err.ErrorMessage = TEXT(
				"api_key or dashboard_url not set in "
				"shinttools.config.json");
			OnComplete.ExecuteIfBound(Err);
			return true;
		}
		if (Cfg.ApiKeyDashboard.IsEmpty())
		{
			FShintWebDashboardResult Err;
			Err.bSuccess     = false;
			Err.ErrorMessage = TEXT(
				"No dashboard API key. Open Settings, create a "
				"project on shint.tools and paste the st_… key "
				"into 'Dashboard API Key'.");
			OnComplete.ExecuteIfBound(Err);
			return true;
		}
		return false;
	}

	// Both endpoints share the same auth scheme:
	//   Authorization: Bearer <ApiKeyDashboard>
	TMap<FString, FString> BuildAuthHeaders(const FShintCoreConfig& Cfg)
	{
		TMap<FString, FString> Headers;
		Headers.Add(TEXT("Authorization"),
			FString::Printf(TEXT("Bearer %s"), *Cfg.ApiKeyDashboard));
		return Headers;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — full project scan
// ─────────────────────────────────────────────────────────────────────────────

void FShintDashboardSync::SendCodeValidator(
	const FShintValidateResult& LastResult,
	FOnShintWebDashboardComplete OnComplete)
{
	const FShintCoreConfig& Cfg = Client.GetConfig();
	if (ShortCircuitOnMissingConfig(Cfg, OnComplete))
	{
		return;
	}

	// Deduplicate file paths first (issues may reference the same
	// file multiple times). Then add every scanned file so the
	// dashboard sees the full corpus, including files with zero
	// issues — the dashboard's noise-vs-signal ratio depends on it.
	TSet<FString> SeenPaths;
	for (const FShintCodeIssue& Issue : LastResult.Issues)
	{
		if (!Issue.FilePath.IsEmpty())
		{
			SeenPaths.Add(Issue.FilePath);
		}
	}
	for (const FString& AbsPath : LastResult.ScannedFilePaths)
	{
		SeenPaths.Add(AbsPath);
	}

	TArray<TSharedPtr<FJsonValue>> FilesArr;
	for (const FString& AbsPath : SeenPaths)
	{
		FString Content;
		FFileHelper::LoadFileToString(Content, *AbsPath);

		const FString Filename = FPaths::GetCleanFilename(AbsPath);
		const FString RelPath  = FPaths::GetPath(AbsPath);
		const FString Ext      = FPaths::GetExtension(AbsPath).ToLower();
		const FString TypeStr  =
			(Ext == TEXT("h") || Ext == TEXT("hpp"))
				? TEXT("header")
				: TEXT("cpp");
		TArray<FString> Lines;
		const int32 LineCount = Content.IsEmpty()
			? 0
			: Content.ParseIntoArray(Lines, TEXT("\n"), false);

		TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"),        Filename);
		FO->SetStringField(TEXT("path"),        RelPath);
		FO->SetStringField(TEXT("type"),        TypeStr);
		FO->SetStringField(TEXT("content"),     Content);
		FO->SetNumberField(TEXT("lines_count"), LineCount);
		FilesArr.Add(MakeShared<FJsonValueObject>(FO));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Cfg.ProjectName);
	Body->SetArrayField (TEXT("files"),        FilesArr);

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/code-validator/analyze");
	UE_LOG(LogShintTools, Log,
		TEXT("Dashboard: sending %d files to %s"),
		FilesArr.Num(), *Url);

	Client.SendRequest(Url, EShintHttpMethod::POST,
		FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable {
				OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
			}),
		BuildAuthHeaders(Cfg));
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot — violations list
// ─────────────────────────────────────────────────────────────────────────────

void FShintDashboardSync::SendAssetNaming(
	const FShintAssetScanResult& LastResult,
	FOnShintWebDashboardComplete OnComplete)
{
	const FShintCoreConfig& Cfg = Client.GetConfig();
	if (ShortCircuitOnMissingConfig(Cfg, OnComplete))
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ItemsArr;
	for (const FShintAssetIssue& Issue : LastResult.Issues)
	{
		const FString Name     = FPaths::GetBaseFilename(Issue.AssetPath);
		const FString Path     = FPaths::GetPath(Issue.AssetPath);
		const FString Category =
			FShintCoreClient::AssetTypeToCategory(Issue.AssetType);

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),     Name);
		O->SetStringField(TEXT("path"),     Path);
		O->SetStringField(TEXT("type"),     TEXT("asset"));
		O->SetStringField(TEXT("category"), Category);
		ItemsArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Cfg.ProjectName);
	Body->SetArrayField (TEXT("items"),        ItemsArr);

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/naming-bot/analyze");
	UE_LOG(LogShintTools, Log,
		TEXT("Dashboard: sending %d asset items to %s"),
		ItemsArr.Num(), *Url);

	Client.SendRequest(Url, EShintHttpMethod::POST,
		FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable {
				OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
			}),
		BuildAuthHeaders(Cfg));
}
