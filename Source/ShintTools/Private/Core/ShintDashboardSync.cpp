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

	// Privacy (issue #314): the dashboard receives METRICS ONLY — per-file
	// findings, counts and file metadata — never raw source. The plugin has
	// already run the validator locally, so we transmit the resulting issues
	// (rule id, severity, category, line, message) instead of file content.
	// `content`, snippets and source context are deliberately never sent — the
	// file is not even read from disk here. The dashboard's noise-vs-signal
	// ratio is preserved by the `totals.files_scanned` aggregate below.
	//
	// Files with issues are emitted in first-seen order; a capped resend (HTTP
	// 413) takes a prefix slice — the plan's `limit` comes from the 413 body so
	// the behaviour tracks the server quota without hardcoding a per-tier number.
	TArray<TSharedPtr<FJsonValue>> AllFiles;
	{
		TArray<FString> FileOrder;
		TMap<FString, TArray<TSharedPtr<FJsonValue>>> IssuesByFile;
		TMap<FString, int32> LinesByFile;
		for (const FShintCodeIssue& Issue : LastResult.Issues)
		{
			if (Issue.FilePath.IsEmpty()) continue;
			TArray<TSharedPtr<FJsonValue>>& Arr = IssuesByFile.FindOrAdd(Issue.FilePath);
			if (Arr.Num() == 0) FileOrder.Add(Issue.FilePath);
			if (Issue.LinesCount > 0 && !LinesByFile.Contains(Issue.FilePath))
				LinesByFile.Add(Issue.FilePath, Issue.LinesCount);

			// Findings only — never Issue.Snippet / FileContent / fix text.
			TSharedRef<FJsonObject> IO = MakeShared<FJsonObject>();
			IO->SetStringField(TEXT("rule_id"),   Issue.RuleId);
			IO->SetStringField(TEXT("rule_name"), Issue.RuleName);
			IO->SetStringField(TEXT("severity"),  Issue.Severity);
			IO->SetStringField(TEXT("category"),  Issue.Category);
			IO->SetNumberField(TEXT("line"),      Issue.Line);
			IO->SetStringField(TEXT("message"),   Issue.Message);
			Arr.Add(MakeShared<FJsonValueObject>(IO));
		}

		for (const FString& AbsPath : FileOrder)
		{
			const FString Ext = FPaths::GetExtension(AbsPath).ToLower();
			const FString TypeStr =
				(Ext == TEXT("h") || Ext == TEXT("hpp")) ? TEXT("header") : TEXT("cpp");

			TArray<TSharedPtr<FJsonValue>>& FileIssues = IssuesByFile[AbsPath];
			TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
			FO->SetStringField(TEXT("name"),        FPaths::GetCleanFilename(AbsPath));
			FO->SetStringField(TEXT("path"),        FPaths::GetPath(AbsPath));
			FO->SetStringField(TEXT("type"),        TypeStr);
			FO->SetNumberField(TEXT("lines_count"), LinesByFile.FindRef(AbsPath));
			FO->SetNumberField(TEXT("issue_count"), FileIssues.Num());
			FO->SetArrayField (TEXT("issues"),      FileIssues);
			AllFiles.Add(MakeShared<FJsonValueObject>(FO));
		}
	}

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/code-validator/analyze");
	const FString ProjectName = Cfg.ProjectName;
	const TMap<FString, FString> Headers = BuildAuthHeaders(Cfg);
	const int32 TotalFiles = AllFiles.Num();

	// Project-level metrics travel with every (re)send so the dashboard can
	// show totals even when the per-file list is capped by the plan.
	const int32 FilesScanned  = LastResult.FilesScanned;
	const int32 TotalIssues   = LastResult.TotalIssues;
	const int32 TotalErrors   = LastResult.TotalErrors;
	const int32 TotalWarnings = LastResult.TotalWarnings;
	const float QualityScore  = LastResult.QualityScoreOverall;

	// Serialise the body from the first <Take> files (Take<=0 => all).
	auto BuildBody =
		[AllFiles, ProjectName, FilesScanned, TotalIssues, TotalErrors,
		 TotalWarnings, QualityScore](int32 Take) -> FString
	{
		const int32 N = (Take <= 0 || Take > AllFiles.Num())
			? AllFiles.Num() : Take;
		TArray<TSharedPtr<FJsonValue>> Slice;
		Slice.Reserve(N);
		for (int32 i = 0; i < N; ++i) Slice.Add(AllFiles[i]);

		TSharedRef<FJsonObject> Totals = MakeShared<FJsonObject>();
		Totals->SetNumberField(TEXT("files_scanned"),  FilesScanned);
		Totals->SetNumberField(TEXT("total_issues"),   TotalIssues);
		Totals->SetNumberField(TEXT("total_errors"),   TotalErrors);
		Totals->SetNumberField(TEXT("total_warnings"), TotalWarnings);
		if (QualityScore >= 0.f)
			Totals->SetNumberField(TEXT("quality_score"), QualityScore);

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("project_name"), ProjectName);
		Body->SetArrayField (TEXT("files"),        Slice);
		Body->SetObjectField(TEXT("totals"),       Totals);
		return FShintCoreClient::SerializeJson(Body);
	};

	UE_LOG(LogShintTools, Verbose,
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
	UE_LOG(LogShintTools, Verbose,
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
