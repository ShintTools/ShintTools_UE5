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

	// Both endpoints share the same auth scheme: a per-project bearer key.
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

	// Privacy: the dashboard receives METRICS ONLY — file metadata, per-issue
	// findings and aggregate counts, never raw source. The plugin has already
	// run the validator locally, so we transmit the resulting issues (severity,
	// category, line, rule title + message) instead of file content. Snippet,
	// FileContent, ContextBefore/After and FixSuggestion are deliberately never
	// sent — the file is not even read from disk here.
	//
	// Wire shape follows the v2 metrics-only contract:
	//   { project_name, engine, files[], issues[], stats }
	// where each files[] entry is a .strict() FileItem carrying ONLY
	// {name, path, type, lines_count} (any extra field is rejected with 400),
	// and issues[] is a flat top-level array keyed by project-relative
	// file_path. Files with issues are emitted in first-seen order; a capped
	// resend (HTTP 413) takes a prefix slice of files and their issues follow —
	// the plan's `limit` comes from the 413 body so the behaviour tracks the
	// server quota without hardcoding a per-tier number.
	struct FFileRecord
	{
		TSharedPtr<FJsonObject>        Meta;    // strict files[] entry
		TArray<TSharedPtr<FJsonValue>> Issues;  // this file's issues[] entries
	};
	TArray<FFileRecord> Records;
	{
		TMap<FString, int32> IndexByFile;
		TMap<FString, int32> LinesByFile;
		for (const FShintCodeIssue& Issue : LastResult.Issues)
		{
			if (Issue.FilePath.IsEmpty()) continue;

			int32 Idx;
			if (const int32* Found = IndexByFile.Find(Issue.FilePath))
			{
				Idx = *Found;
			}
			else
			{
				Idx = Records.AddDefaulted();
				IndexByFile.Add(Issue.FilePath, Idx);
			}
			if (Issue.LinesCount > 0 && !LinesByFile.Contains(Issue.FilePath))
				LinesByFile.Add(Issue.FilePath, Issue.LinesCount);

			// Project-relative path only — never leak the absolute local layout
			// (e.g. C:/Users/<name>/...) to the external dashboard.
			FString RelFile = Issue.FilePath;
			if (!FPaths::MakePathRelativeTo(RelFile, *FPaths::ProjectDir()))
				RelFile = FPaths::GetCleanFilename(Issue.FilePath);

			// Findings only — never Snippet / FileContent / FixSuggestion.
			TSharedRef<FJsonObject> IO = MakeShared<FJsonObject>();
			IO->SetStringField(TEXT("file_path"),   RelFile);
			IO->SetNumberField(TEXT("line"),        Issue.Line);
			IO->SetStringField(TEXT("category"),    Issue.Category);
			IO->SetStringField(TEXT("severity"),    Issue.Severity);
			IO->SetStringField(TEXT("title"),
				Issue.RuleName.IsEmpty() ? Issue.RuleId : Issue.RuleName);
			IO->SetStringField(TEXT("description"), Issue.Message);
			IO->SetStringField(TEXT("suggestion"),  TEXT(""));
			Records[Idx].Issues.Add(MakeShared<FJsonValueObject>(IO));
		}

		for (const TPair<FString, int32>& Pair : IndexByFile)
		{
			const FString& AbsPath = Pair.Key;
			const FString Ext = FPaths::GetExtension(AbsPath).ToLower();
			const FString TypeStr =
				(Ext == TEXT("h") || Ext == TEXT("hpp")) ? TEXT("header") : TEXT("cpp");

			FString RelDir = FPaths::GetPath(AbsPath);
			if (!FPaths::MakePathRelativeTo(RelDir, *FPaths::ProjectDir()))
				RelDir = FPaths::GetCleanFilename(FPaths::GetPath(AbsPath));

			TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
			FO->SetStringField(TEXT("name"),        FPaths::GetCleanFilename(AbsPath));
			FO->SetStringField(TEXT("path"),        RelDir);
			FO->SetStringField(TEXT("type"),        TypeStr);
			FO->SetNumberField(TEXT("lines_count"), LinesByFile.FindRef(AbsPath));
			Records[Pair.Value].Meta = FO;
		}
	}

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/code-validator/analyze");
	const FString ProjectName = Cfg.ProjectName;
	const TMap<FString, FString> Headers = BuildAuthHeaders(Cfg);
	const int32 TotalFiles = Records.Num();

	// Project-level metrics travel with every (re)send so the dashboard can
	// show totals even when the per-file list is capped by the plan. Issue /
	// error / warning counts are derivable server-side from issues[]; only the
	// non-derivable aggregates (files scanned, quality score) ride in stats.
	const int32 FilesScanned = LastResult.FilesScanned;
	const float QualityScore = LastResult.QualityScoreOverall;

	// Serialise the body from the first <Take> files (Take<=0 => all); the
	// issues for trimmed-out files are dropped with them so the two arrays stay
	// consistent under a 413 cap.
	auto BuildBody =
		[Records, ProjectName, FilesScanned, QualityScore](int32 Take) -> FString
	{
		const int32 N = (Take <= 0 || Take > Records.Num())
			? Records.Num() : Take;
		TArray<TSharedPtr<FJsonValue>> FilesArr;
		TArray<TSharedPtr<FJsonValue>> IssuesArr;
		FilesArr.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			if (Records[i].Meta.IsValid())
				FilesArr.Add(MakeShared<FJsonValueObject>(
					Records[i].Meta.ToSharedRef()));
			IssuesArr.Append(Records[i].Issues);
		}

		TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
		Stats->SetNumberField(TEXT("scanned_files"), FilesScanned);
		if (QualityScore >= 0.f)
			Stats->SetNumberField(TEXT("quality_score"), QualityScore);

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("project_name"), ProjectName);
		Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
		Body->SetArrayField (TEXT("files"),        FilesArr);
		Body->SetArrayField (TEXT("issues"),       IssuesArr);
		Body->SetObjectField(TEXT("stats"),        Stats);
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

	// v2 metrics-only contract: asset_paths[] with {asset_path, name, type,
	// category}. Only naming metadata travels — never asset bytes or content.
	// (The legacy items[] shape is still accepted server-side during the
	// deprecation window, but we emit the current form.)
	TArray<TSharedPtr<FJsonValue>> AssetsArr;
	for (const FShintAssetIssue& Issue : LastResult.Issues)
	{
		const FString Name     = FPaths::GetBaseFilename(Issue.AssetPath);
		const FString Category =
			FShintCoreClient::AssetTypeToCategory(Issue.AssetType);
		const FString TypeStr  =
			Issue.AssetType.IsEmpty() ? TEXT("asset") : Issue.AssetType;

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"), Issue.AssetPath);
		O->SetStringField(TEXT("name"),       Name);
		O->SetStringField(TEXT("type"),       TypeStr);
		O->SetStringField(TEXT("category"),   Category);
		AssetsArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Cfg.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetArrayField (TEXT("asset_paths"),  AssetsArr);

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/naming-bot/analyze");
	UE_LOG(LogShintTools, Verbose,
		TEXT("Dashboard: sending %d asset items to %s"),
		AssetsArr.Num(), *Url);

	Client.SendRequest(Url, EShintHttpMethod::POST,
		FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable {
				OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
			}),
		BuildAuthHeaders(Cfg));
}

// [LOD-STRIP-BEGIN]
void FShintDashboardSync::SendLodAudit(
	const FShintLodAuditResult& LastResult,
	FOnShintWebDashboardComplete OnComplete)
{
	const FShintCoreConfig& Cfg = Client.GetConfig();
	if (ShortCircuitOnMissingConfig(Cfg, OnComplete))
	{
		return;
	}

	// Metrics-only: per-finding metadata + the aggregate KPIs the panel shows.
	// Asset paths are logical /Game/... object paths (not local filesystem
	// paths), so they carry no user-machine layout. No mesh/texture bytes,
	// guidance text or AI output travel.
	TArray<TSharedPtr<FJsonValue>> FindingsArr;
	FindingsArr.Reserve(LastResult.Findings.Num());
	for (const FShintLodFinding& F : LastResult.Findings)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"),          F.AssetPath);
		O->SetStringField(TEXT("rule_id"),             F.RuleId);
		O->SetStringField(TEXT("rule_name"),           F.RuleName);
		O->SetStringField(TEXT("category"),            F.Category);
		O->SetStringField(TEXT("severity"),            F.Severity);
		O->SetStringField(TEXT("message"),             F.Message);
		O->SetBoolField  (TEXT("auto_fixable"),        F.bAutoFixable);
		O->SetNumberField(TEXT("vram_mb"),             F.VramMb);
		O->SetNumberField(TEXT("shader_instructions"), F.ShaderInstructions);
		FindingsArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("assets_audited"),          LastResult.AssetsAudited);
	Stats->SetNumberField(TEXT("issues_found"),            LastResult.IssuesFound);
	Stats->SetNumberField(TEXT("auto_fixable"),            LastResult.AutoFixable);
	Stats->SetNumberField(TEXT("textures"),                LastResult.TexturesAudited);
	Stats->SetNumberField(TEXT("meshes"),                  LastResult.MeshesAudited);
	Stats->SetNumberField(TEXT("materials"),               LastResult.MaterialsAudited);
	Stats->SetNumberField(TEXT("total_vram_mb"),           LastResult.TotalVramMb);
	Stats->SetNumberField(TEXT("estimated_vram_saved_mb"), LastResult.EstimatedVramSavedMb);
	Stats->SetNumberField(TEXT("estimated_shader_saved"),
		LastResult.EstimatedShaderInstructionsSaved);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Cfg.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetArrayField (TEXT("findings"),     FindingsArr);
	Body->SetObjectField(TEXT("stats"),        Stats);

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/lod-auditor/analyze");
	UE_LOG(LogShintTools, Verbose,
		TEXT("Dashboard: sending %d LOD findings to %s"),
		FindingsArr.Num(), *Url);

	Client.SendRequest(Url, EShintHttpMethod::POST,
		FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable {
				OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
			}),
		BuildAuthHeaders(Cfg));
}
// [LOD-STRIP-END]

// ─────────────────────────────────────────────────────────────────────────────
// Predictive Profiler — scores + top issues
// ─────────────────────────────────────────────────────────────────────────────

void FShintDashboardSync::SendPredictive(
	const FShintPredictReport& LastReport,
	FOnShintWebDashboardComplete OnComplete)
{
	const FShintCoreConfig& Cfg = Client.GetConfig();
	if (ShortCircuitOnMissingConfig(Cfg, OnComplete))
	{
		return;
	}

	// Metrics-only, same posture as SendLodAudit: priced-item metadata + the
	// aggregate scores the window shows. No asset bytes, no remediation/
	// recovery bands (those are pricing detail, not what a studio-wide
	// dashboard needs to track trend over time).
	auto ScoreObj = [](const FShintPredictScore& S) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("value"), S.Value);
		O->SetNumberField(TEXT("driver_count"), S.Drivers.Num());
		return O;
	};

	TSharedRef<FJsonObject> Scores = MakeShared<FJsonObject>();
	Scores->SetObjectField(TEXT("cpu_risk"),    ScoreObj(LastReport.CpuRisk));
	Scores->SetObjectField(TEXT("gpu_risk"),    ScoreObj(LastReport.GpuRisk));
	Scores->SetObjectField(TEXT("memory_risk"), ScoreObj(LastReport.MemoryRisk));
	Scores->SetObjectField(TEXT("build_health"), ScoreObj(LastReport.BuildHealth));
	Scores->SetNumberField(TEXT("overall_project_health"), LastReport.OverallHealth);

	TArray<TSharedPtr<FJsonValue>> IssuesArr;
	IssuesArr.Reserve(LastReport.TopIssues.Num());
	for (const FShintPredictIssue& Issue : LastReport.TopIssues)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("item_id"),      Issue.ItemId);
		O->SetNumberField(TEXT("layer"),        Issue.Layer);
		O->SetStringField(TEXT("severity"),     Issue.Severity);
		O->SetStringField(TEXT("title"),        Issue.Title);
		O->SetStringField(TEXT("rule_id"),      Issue.RuleId);
		O->SetBoolField  (TEXT("auto_fixable"), Issue.bHasRemediation);
		IssuesArr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("top_issues_count"),       LastReport.TopIssues.Num());
	Stats->SetNumberField(TEXT("cost_items_count"),       LastReport.CostItems.Num());
	Stats->SetNumberField(TEXT("code_issues_uncosted"),   LastReport.CodeIssuesUncosted);
	Stats->SetStringField(TEXT("calibration_version"),    LastReport.CalibrationVersion);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Cfg.ProjectName);
	Body->SetStringField(TEXT("engine"),       LastReport.Engine.IsEmpty() ? TEXT("unreal") : LastReport.Engine);
	Body->SetStringField(TEXT("profile"),      LastReport.ProfileName);
	Body->SetObjectField(TEXT("scores"),       Scores);
	Body->SetArrayField (TEXT("top_issues"),   IssuesArr);
	Body->SetObjectField(TEXT("stats"),        Stats);

	const FString Url = Cfg.DashboardUrl
		/ TEXT("api/public/predictive-profiler/analyze");
	UE_LOG(LogShintTools, Verbose,
		TEXT("Dashboard: sending %d predictive top issues to %s"),
		IssuesArr.Num(), *Url);

	Client.SendRequest(Url, EShintHttpMethod::POST,
		FShintCoreClient::SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw) mutable {
				OnComplete.ExecuteIfBound(MakeResultFromRaw(Raw));
			}),
		BuildAuthHeaders(Cfg));
}
