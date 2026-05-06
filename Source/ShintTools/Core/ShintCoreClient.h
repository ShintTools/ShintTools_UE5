// Copyright ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"

// ─────────────────────────────────────────────────────────────────────────────
// HTTP primitives
// ─────────────────────────────────────────────────────────────────────────────

enum class EShintHttpMethod : uint8 { GET, POST, PUT, DELETE_ };

struct FShintRequestResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintRequestComplete, const FShintRequestResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — enriched issue
// ─────────────────────────────────────────────────────────────────────────────

struct FShintCodeIssue
{
	FString RuleId;
	FString Severity;         // "error" | "warning"
	FString Message;
	FString FilePath;         // absolute path on disk
	int32   Line             = 0;
	FString Snippet;          // the problematic source line
	FString FixSuggestion;    // what the corrected line should look like
	bool    bIsAutoFixable   = false;
	int32   LinesCount       = 0;  // total lines in file (for dashboard)

	// Extended fields from server response
	FString Class;            // e.g. "ABrokenTestActor" or blueprint class name
	FString Category;         // e.g. "memory", "style", "performance", "blueprint"
	FString Graph;            // blueprint graph name (empty for C++ issues)

	// Full source file content — used by the tree-sitter fix validator to verify
	// that an applied change does not break the AST without re-reading from disk.
	FString FileContent;

	// Context window for before/after diff preview (populated by server)
	FString ContextBefore;        // ~5 source lines centred on this issue, newline-separated
	FString ContextAfter;         // same window with fix_suggestion applied
	int32   ContextLineStart = 0; // 1-based line number of the first context line

	// Runtime UI state — not sent over wire
	bool    bChecked         = false;
};

struct FShintValidateResult
{
	bool    bSuccess      = false;
	int32   StatusCode    = 0;
	FString ErrorMessage;
	int32   TotalIssues   = 0;
	int32   TotalErrors   = 0;
	int32   TotalWarnings = 0;
	int32   FilesScanned  = 0;
	TArray<FShintCodeIssue> Issues;

	// Kept for "Send to Dashboard" — populated during scan
	TArray<FString> ScannedFilePaths;  // absolute paths of all scanned files

	// Slice B: Quality Score overall echoed by /validate/project and /validate/blueprints.
	// -1.f = not present (older server, free SKU, or fix endpoint).
	float   QualityScoreOverall = -1.f;
};
DECLARE_DELEGATE_OneParam(FOnShintValidateComplete, const FShintValidateResult&);

struct FShintFixedFile
{
	FString FilePath;           // absolute path
	FString CorrectedContent;
	FString Additions;          // tree-sitter: suggested .h additions
	TArray<FString> Changes;    // tree-sitter: human-readable change list
	int32   FixesApplied = 0;
	int32   FixesSkipped = 0;
};

// Compiler error/warning from the post-fix incremental build check.
struct FShintCompileError
{
	FString FilePath;    // absolute path
	FString FileName;    // cached display name
	int32   Line     = 0;
	int32   Column   = 0;
	FString Code;        // e.g. "C2065"
	FString Message;
	FString Severity;    // "error" or "warning"
};

struct FShintFixResult
{
	bool    bSuccess           = false;
	int32   TotalFixesApplied  = 0;
	int32   TotalFixesSkipped  = 0;
	FString ErrorMessage;
	TArray<FShintFixedFile>     FixedFiles;
	TArray<FShintCompileError>  CompileErrors;  // populated after incremental build check
	bool    bHasCompileErrors  = false;
};
DECLARE_DELEGATE_OneParam(FOnShintFixComplete, const FShintFixResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Fix Safety Check
// ─────────────────────────────────────────────────────────────────────────────

struct FShintSafetyCheckResult
{
	bool            bSafe    = true;
	TArray<FString> Warnings;
	FString         Preview;
};
DECLARE_DELEGATE_OneParam(FOnShintSafetyCheckComplete, const FShintSafetyCheckResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot
// ─────────────────────────────────────────────────────────────────────────────

struct FShintAssetIssue
{
	FString AssetPath;       // UE package path e.g. /Game/Characters/hero_body
	FString CurrentName;
	FString SuggestedName;
	FString Reason;
	FString AssetType;       // "Texture2D", "StaticMesh", etc.

	bool    bChecked = true;
};

struct FShintAssetScanResult
{
	bool    bSuccess        = false;
	int32   StatusCode      = 0;
	FString ErrorMessage;
	int32   TotalAssets     = 0;
	int32   InvalidAssets   = 0;
	float   ScanTimeSeconds = 0.0f;
	// Subscription tier the server resolved this request to ("free" | "indie").
	// Empty = not parsed (older server build). Populated from summary.tier.
	FString Tier;
	TArray<FShintAssetIssue> Issues;
};
DECLARE_DELEGATE_OneParam(FOnShintAssetScanComplete, const FShintAssetScanResult&);

struct FShintAssetFixResult
{
	bool    bSuccess      = false;
	int32   AssetsRenamed = 0;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintAssetFixComplete, const FShintAssetFixResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Quality Score (Slice B) — full breakdown fetched via /metrics/score/latest
// ─────────────────────────────────────────────────────────────────────────────

struct FShintQualityScoreSnapshot
{
	bool    bValid               = false;
	int32   StatusCode           = 0;
	FString ErrorMessage;

	float   OverallScore         = 0.f;
	// Sub-scores (0-100). Default to 100 = "no issues in this category yet".
	float   PerformanceScore     = 100.f;
	float   SecurityScore        = 100.f;
	float   BestPracticesScore   = 100.f;
	float   MaintainabilityScore = 100.f;
	float   NamingScore          = 100.f;

	int32   Errors               = 0;
	int32   Warnings             = 0;
	int32   Infos                = 0;
	int32   FilesScanned         = 0;
	int32   TotalIssues          = 0;
	float   TotalPenalty         = 0.f;

	FString ProjectId;
	FString Timestamp;     // ISO-8601 from server
	FString ScanType;      // "full" | "incremental" | "fix_update"
	FString Tier;          // "free" | "indie"
};
DECLARE_DELEGATE_OneParam(FOnShintQualityScoreComplete, const FShintQualityScoreSnapshot&);

struct FShintQualityScoreHistory
{
	bool    bValid     = false;
	int32   StatusCode = 0;
	FString ErrorMessage;
	FString ProjectId;
	TArray<FShintQualityScoreSnapshot> Scores;  // newest first, as the server returns them
};
DECLARE_DELEGATE_OneParam(FOnShintQualityScoreHistoryComplete, const FShintQualityScoreHistory&);

// ─────────────────────────────────────────────────────────────────────────────
// External web dashboard results
// ─────────────────────────────────────────────────────────────────────────────

struct FShintWebDashboardResult
{
	bool    bSuccess     = false;
	FString ErrorMessage;
	FString ResponseBody;
};
DECLARE_DELEGATE_OneParam(FOnShintWebDashboardComplete, const FShintWebDashboardResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Agent — Auto-Fix Plan (Indie tier; free SKU never builds the UI button)
// ─────────────────────────────────────────────────────────────────────────────

struct FShintAgentPlanStep
{
	int32   Order = 0;
	FString RuleId;
	FString FilePath;
	int32   Line = 0;
	FString Severity;
	FString Priority;     // critical | high | medium | low
	FString Rationale;
	bool    bIsAutoFixable = false;
};

struct FShintAgentPlanResult
{
	bool                          bSuccess = false;
	FString                       ErrorMessage;
	FString                       Tier;     // free | indie | …
	FString                       Summary;  // "5 critical · 3 high · …"
	TArray<FShintAgentPlanStep>   Steps;
};
DECLARE_DELEGATE_OneParam(FOnShintAgentPlanComplete, const FShintAgentPlanResult&);


// ─────────────────────────────────────────────────────────────────────────────
// Sprint C — POST /agent/review (SSE streaming, Indie tier)
//
// The endpoint isn't merged to core/main yet (lives in Genesis's `develop`
// branch). This block stays in the public API as a forward-compatible
// contract so the panel can wire its UI before the core lands. The plugin
// invokes RequestAgentReview() and gets typed callbacks for each SSE event
// kind — thinking tokens, tool calls, tool results, final answer.
//
// Until the endpoint exists in the deployed core, the implementation
// short-circuits: it reports a "not available" event via OnDone with
// bSuccess=false and a hint pointing the user at the next core release.
// When Genesis merges Fase 4, only the .cpp body changes; the plugin UI is
// untouched.
// ─────────────────────────────────────────────────────────────────────────────

/** One SSE event from /agent/review. Mirrors the documented event kinds:
 *  - "thinking"     → LLM tokens streamed mid-reasoning
 *  - "tool_call"    → "I will call analyze_cpp_source(…)"
 *  - "tool_result"  → result of the tool invocation
 *  - "done"         → final answer (also emitted on graceful failures)
 */
struct FShintAgentReviewEvent
{
	FString Kind;        // see comment above
	FString Payload;     // raw text for "thinking" / "done"; JSON for tools
	FString ToolName;    // populated when Kind == "tool_call" / "tool_result"
};

struct FShintAgentReviewResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString Tier;          // "free" → 403, "indie" → ok
	FString FinalAnswer;   // == last "done" event payload when bSuccess
};

DECLARE_DELEGATE_OneParam(FOnShintAgentReviewEvent,    const FShintAgentReviewEvent&);
DECLARE_DELEGATE_OneParam(FOnShintAgentReviewComplete, const FShintAgentReviewResult&);


// ─────────────────────────────────────────────────────────────────────────────
// Local dashboard report (legacy — keeps local MongoDB sync)
// ─────────────────────────────────────────────────────────────────────────────

struct FShintDashboardReport
{
	FString ProjectName;
	FString Engine = TEXT("unreal");
	FString ReportType;
	int32   Code_FilesScanned   = 0;
	int32   Code_TotalIssues    = 0;
	int32   Code_TotalErrors    = 0;
	int32   Code_TotalWarnings  = 0;
	int32   Asset_TotalScanned  = 0;
	int32   Asset_InvalidAssets = 0;
	float   Asset_ScanTime      = 0.0f;
};
struct FShintDashboardResult { bool bSuccess = false; FString ErrorMessage; };
DECLARE_DELEGATE_OneParam(FOnShintDashboardComplete, const FShintDashboardResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

struct FShintCoreConfig
{
	// Local core engine.
	// Default is 127.0.0.1 (NOT "localhost") because on some Windows installs
	// "localhost" resolves to ::1 (IPv6 loopback) while uvicorn bound with
	// --host 0.0.0.0 only listens on IPv4 — the plugin would then get
	// "connection refused" with the engine clearly running. Force IPv4.
	FString CoreHost        = TEXT("127.0.0.1");
	int32   CorePort        = 18200;
	bool    bAutoStartCore  = false;

	// Project identity
	FString ProjectName     = TEXT("MyGame");
	FString ProjectId       = TEXT("");

	// External web dashboard (app.shinttools.io or emergent)
	FString ApiKeyDashboard          = TEXT("");
	FString ApiKeyMongo    = TEXT("");
	FString DashboardUrl    = TEXT("https://app.shinttools.io");

	FString GetBaseUrl() const
	{
		FString Host = CoreHost.IsEmpty() ? TEXT("127.0.0.1") : CoreHost;
		// Reject obvious bind-only addresses — they are valid for the server
		// (uvicorn --host 0.0.0.0 means "listen on every interface") but they
		// are NOT routable from a client. If the user accidentally pasted the
		// uvicorn bind address into shinttools.config.json, fall back to IPv4
		// loopback so requests still reach the local engine.
		if (Host == TEXT("0.0.0.0") || Host == TEXT("::") || Host == TEXT("localhost"))
		{
			Host = TEXT("127.0.0.1");
		}
		return FString::Printf(TEXT("http://%s:%d"), *Host, CorePort);
	}

	bool HasExternalDashboard() const
	{
		return !ApiKeyDashboard.IsEmpty() && !ApiKeyMongo.IsEmpty() && !DashboardUrl.IsEmpty() && !ProjectId.IsEmpty();
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Client
// ─────────────────────────────────────────────────────────────────────────────

class SHINTTOOLS_API FShintCoreClient : public TSharedFromThis<FShintCoreClient>
{
public:
	FShintCoreClient();
	~FShintCoreClient();

	bool LoadConfig();
	bool SaveConfig() const;
	const FShintCoreConfig& GetConfig() const { return Config; }
	FShintCoreConfig& GetConfigMutable() { return Config; }

	// ── Connectivity ─────────────────────────────────────────────────────────
	void CheckHealth(FOnShintRequestComplete OnComplete);
	void Ping(FOnShintRequestComplete OnComplete);

	// ── Code Validator — local engine ─────────────────────────────────────────
	void ValidateCode(const FString& AbsFilePath, const FString& Content,
	                  const FString& Engine, FOnShintValidateComplete OnComplete);
	void ValidateProject(const FString& SourceDir, FOnShintValidateComplete OnComplete);
	void ValidateBlueprints(const FString& ContentDir, FOnShintValidateComplete OnComplete);
	void ApplyCodeFixes(const TArray<FShintCodeIssue>& AcceptedIssues,
	                    FOnShintFixComplete OnComplete);

	/** Preview-only: calls /validate/fix for a single issue and returns the result without writing to disk. */
	void FetchSingleFixPreview(const FShintCodeIssue& Issue, FOnShintFixComplete OnComplete);

	/** Pre-flight safety check: calls /validate/check-fix-safety. On HTTP error, treats as safe. */
	void CheckFixSafety(const TArray<FShintCodeIssue>& Issues, FOnShintSafetyCheckComplete OnComplete);

	// ── Code Validator — external web dashboard ───────────────────────────────
	/**
	 * Sends the full project scan to the web dashboard.
	 * Payload: POST {DashboardUrl}/api/code-validator/analyze
	 * Body: { project_id, project_name, api_key, files:[{name,path,type,content,lines_count}] }
	 */
	void SendCodeValidatorToDashboard(const FShintValidateResult& LastResult,
	                                  FOnShintWebDashboardComplete OnComplete);

	// ── Quality Score (Slice B) ──────────────────────────────────────────────
	/**
	 * Fetches the latest Quality Score for the configured project.
	 * Calls GET {core}/metrics/score/latest?project_id=...
	 * Returns bValid=false if the server has no score yet for this project.
	 */
	void GetLatestQualityScore(const FString& ProjectId,
	                           FOnShintQualityScoreComplete OnComplete);

	/** Fetches the score history (newest first). Used by the trend chart. */
	void GetQualityScoreHistory(const FString& ProjectId, int32 Limit,
	                            FOnShintQualityScoreHistoryComplete OnComplete);

	// ── Asset Naming Bot — local engine ──────────────────────────────────────
	void ScanAssetNaming(const FString& ContentDir, FOnShintAssetScanComplete OnComplete);
	void ReportAssetFixesToServer(const TArray<FShintAssetIssue>& Fixed,
	                              FOnShintAssetFixComplete OnComplete);

	// ── Asset Naming Bot — external web dashboard ─────────────────────────────
	/**
	 * Sends naming violations to the web dashboard.
	 * Payload: POST {DashboardUrl}/api/naming-bot/analyze
	 * Body: { project_id, project_name, api_key, items:[{name,path,type,category}] }
	 */
	void SendAssetNamingToDashboard(const FShintAssetScanResult& LastResult,
	                                FOnShintWebDashboardComplete OnComplete);

	// ── Local MongoDB dashboard report (legacy) ───────────────────────────────
	void SendDashboardReport(const FShintDashboardReport& Report,
	                         FOnShintDashboardComplete OnComplete);

	// ── Agent — Auto-Fix Plan (Indie tier) ────────────────────────────────────
	/**
	 * Sends the validator's last result to /agent/plan and receives a
	 * prioritized fix plan with per-step rationale. Free-tier servers
	 * respond 403 — callers should hide the UI affordance there rather
	 * than display the error.
	 */
	void RequestAgentPlan(const FShintValidateResult& Source,
	                      FOnShintAgentPlanComplete OnComplete);

	/** Sprint C / Fase 4 stub — POST /agent/review (SSE).
	 *
	 *  When Genesis merges the SSE endpoint to core/main, this routes through
	 *  HTTP to /agent/review and demultiplexes the stream into typed events.
	 *  Until then, the implementation immediately invokes OnDone with
	 *  bSuccess=false and an informational ErrorMessage so the panel can show
	 *  a "Coming in next core release" toast without a network round-trip.
	 *
	 *  OnEvent fires per SSE message (thinking / tool_call / tool_result).
	 *  OnDone fires once when the agent finishes, errors out, or falls back.
	 */
	void RequestAgentReview(const FShintValidateResult& Source,
	                        FOnShintAgentReviewEvent    OnEvent,
	                        FOnShintAgentReviewComplete OnDone);

	// ── Generic ───────────────────────────────────────────────────────────────
	void SendRequest(const FString& FullUrl, EShintHttpMethod Method,
	                 const FString& Body, FOnShintRequestComplete OnComplete,
	                 const TMap<FString, FString>& ExtraHeaders = {});

private:
	void OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                           bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete);

	static FString MethodToString(EShintHttpMethod Method);
	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);
	static FShintValidateResult  ParseValidateResponse (const FShintRequestResult& Raw);
	static FShintAssetScanResult ParseAssetScanResponse(const FShintRequestResult& Raw);
	static FShintFixResult       ParseFixResponse      (const FShintRequestResult& Raw);
	static FShintFixResult       ParseTreeSitterFixResponse(const FShintRequestResult& Raw);
	// Slice B helpers — populate one snapshot from a JSON object that matches
	// the score document shape persisted in MongoDB by the core engine.
	static bool                  ParseScoreObject(const TSharedPtr<class FJsonObject>& Obj,
	                                              FShintQualityScoreSnapshot& Out);
	static FShintQualityScoreSnapshot         ParseLatestScoreResponse (const FShintRequestResult& Raw);
	static FShintQualityScoreHistory          ParseScoreHistoryResponse(const FShintRequestResult& Raw);
	static void CollectSourceFiles(const FString& Dir, TArray<FString>& Out);

	void HandleTreeSitterFixResponse(const FShintRequestResult& Raw,
	                                 FShintFixResult             LocalResult,
	                                 TArray<FShintCodeIssue>     TreeSitterIssues,
	                                 FOnShintFixComplete         OnComplete);

	// Infer asset category from UE type string (for dashboard payload)
	static FString AssetTypeToCategory(const FString& AssetType);
	static FShintAgentPlanResult ParseAgentPlanResponse(const FShintRequestResult& Raw);

	FShintCoreConfig Config;
};
