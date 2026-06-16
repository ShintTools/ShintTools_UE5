// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintDashboardSync.h"

#include "ShintCoreClient.h"
#include "ShintTools.h"

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
	// (the per-project st_<hex> key, NOT the session_token — the launcher
	// deliberately keeps session_token out of the project config since it
	// leaked through git; see config_gen._identity_fields.)
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

	// Order files so the most valuable ones survive the plan's per-scan
	// file cap (indie=50): files WITH issues first (deduped), then the
	// remaining scanned files so the dashboard still gets the full corpus
	// for its noise-vs-signal ratio when the plan allows it. On HTTP 413
	// we resend the first <limit> entries — <limit> read from the 413
	// response body — so the behaviour tracks the server quota without
	// hardcoding the per-tier number here.
	TArray<FString> OrderedPaths;
	TSet<FString> Seen;
	for (const FShintCodeIssue& Issue : LastResult.Issues)
	{
		if (!Issue.FilePath.IsEmpty() && !Seen.Contains(Issue.FilePath))
		{
			Seen.Add(Issue.FilePath);
			OrderedPaths.Add(Issue.FilePath);
		}
	}
	for (const FString& AbsPath : LastResult.ScannedFilePaths)
	{
		if (!Seen.Contains(AbsPath))
		{
			Seen.Add(AbsPath);
			OrderedPaths.Add(AbsPath);
		}
	}

	// Build every file object once (ordered); a capped resend just takes a
	// prefix slice, so file contents are never loaded twice.
	TArray<TSharedPtr<FJsonValue>> AllFiles;
	for (const FString& AbsPath : OrderedPaths)
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
		AllFiles.Add(MakeShared<FJsonValueObject>(FO));
	}

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/code-validator/analyze");
	const FString ProjectName = Cfg.ProjectName;
	const TMap<FString, FString> Headers = BuildAuthHeaders(Cfg);
	const int32 TotalFiles = AllFiles.Num();

	// Serialise the body from the first <Take> ordered files (Take<=0 => all).
	auto BuildBody = [AllFiles, ProjectName](int32 Take) -> FString
	{
		const int32 N = (Take <= 0 || Take > AllFiles.Num())
			? AllFiles.Num() : Take;
		TArray<TSharedPtr<FJsonValue>> Slice;
		Slice.Reserve(N);
		for (int32 i = 0; i < N; ++i) Slice.Add(AllFiles[i]);
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("project_name"), ProjectName);
		Body->SetArrayField (TEXT("files"),        Slice);
		return FShintCoreClient::SerializeJson(Body);
	};

	UE_LOG(LogShintTools, Log,
		TEXT("Dashboard: sending %d files to %s"), TotalFiles, *Url);

	// Weak self so a 413 resend can't deref a destroyed instance if the
	// panel/tab closes mid-upload (DashboardSync is a panel-owned shared ptr).
	TWeakPtr<FShintDashboardSync> WeakSelf = AsShared();
	Client.SendRequest(Url, EShintHttpMethod::POST, BuildBody(0),
		FOnShintRequestComplete::CreateLambda(
			[WeakSelf, BuildBody, Url, Headers, OnComplete, TotalFiles]
			(const FShintRequestResult& Raw) mutable
		{
			// 413 = over the plan's per-scan file cap. The body carries the
			// allowed "limit"; resend once trimmed to it (issues already
			// sort first). Guard limit < TotalFiles to avoid a pointless
			// second round-trip.
			if (Raw.StatusCode == 413 && !Raw.ResponseBody.IsEmpty())
			{
				int32 Limit = 0;
				TSharedPtr<FJsonObject> Obj;
				const TSharedRef<TJsonReader<>> R =
					TJsonReaderFactory<>::Create(Raw.ResponseBody);
				if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
				{
					double LimitNum = 0.0;
					if (Obj->TryGetNumberField(TEXT("limit"), LimitNum))
						Limit = static_cast<int32>(LimitNum);
				}
				TSharedPtr<FShintDashboardSync> Self = WeakSelf.Pin();
				if (Limit > 0 && Limit < TotalFiles && Self.IsValid())
				{
					UE_LOG(LogShintTools, Warning,
						TEXT("Dashboard: %d files over plan cap; resending "
						     "%d (issues first)."), TotalFiles, Limit);
					Self->Client.SendRequest(Url, EShintHttpMethod::POST,
						BuildBody(Limit),
						FOnShintRequestComplete::CreateLambda(
							[OnComplete](const FShintRequestResult& Raw2) mutable
							{
								OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw2));
							}),
						Headers);
					return;
				}
			}
			OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
		}),
		Headers);
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
