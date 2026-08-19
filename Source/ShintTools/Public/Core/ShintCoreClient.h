// Copyright 2026 ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "Security/ShintSecurity.h"
#include "Transport/ShintHttpTypes.h"

// Forward declare rather than pulling Engine/TextureDefines.h into a header
// this widely included — TextureCompressionToString() only needs the enum
// by value. Plain (non-class) UENUM, so the bare name is the true type.
enum TextureCompressionSettings : int;

// ─────────────────────────────────────────────────────────────────────────────
// HTTP primitives — EShintHttpMethod now lives in Transport/ShintHttpTypes.h
// so the new transport layer can share it without an ODR clash.
// ─────────────────────────────────────────────────────────────────────────────

struct FShintRequestResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintRequestComplete, const FShintRequestResult&);

// Per-chunk callback for Server-Sent-Events endpoints (agent/explain/stream).
// Fired on the game thread as each text fragment arrives. Generic (not
// agent-gated) so the streaming transport compiles in every tier even though
// only the paid explainer drives it.
DECLARE_DELEGATE_OneParam(FOnShintStreamChunk, const FString&);

// SSE response parser (FArchive). Defined in ShintCoreClient.cpp; forward
// declared here so the streaming send/complete signatures can reference it.
class FShintSseParser;

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

	// LLM pivot — issues come pre-enriched by the core's enrich_issue() helper
	// which reads the rule's docstring + RULE_NAMES table. The customer-facing
	// UI shows RuleName instead of RuleId; RuleExplanation is forwarded to
	// /agent/explain so the LLM has well-grounded material to paraphrase.
	FString RuleName;         // human-readable label, e.g. "GetWorld without null-check"
	FString RuleExplanation;  // 2-4 sentence rationale extracted from the rule docstring

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
	// -1.f = not present (older server or fix endpoint).
	float   QualityScoreOverall = -1.f;

	// Per-category breakdown echoed inline (when the server is new enough).
	// Avoids the second /metrics/score/latest round-trip — which broke after
	// project_id was removed from shinttools.config.json in 1.7.11. Default
	// 100 = "no issues in this category yet"; bHasCategoryBreakdown=false
	// tells the panel to show only the overall score, not the per-bucket row.
	bool    bHasCategoryBreakdown = false;
	float   PerformanceScore      = 100.f;
	float   SecurityScore         = 100.f;
	float   BestPracticesScore    = 100.f;
	float   MaintainabilityScore  = 100.f;
	float   NamingScore           = 100.f;

	// Top-level summary.tier string ("free" | "indie") echoed by every /validate
	// response. Used by the panel to hide the per-issue Explain button on Free
	// instead of waiting for a 403 mid-click.
	FString Tier;

	// ── Free-tier cap metadata ────────────────────────────────────────────────
	// Server-side fields advertising whether the request hit a tier limit.
	// Read by SShintToolsPanel to render an upgrade banner above the issue
	// list ("Scanned 40 of 96 rules — upgrade to Indie for full coverage").
	// Source of truth lives in core summary; signed in Phase A so clients
	// cannot fake limit_applied=false to disguise a Free scan.
	bool    bLimitApplied   = false;
	FString LimitKind;          // "rules" | "assets"
	int32   LimitValue      = 0; // rules / assets actually applied
	int32   TotalAvailable  = 0; // full catalog size on the server

	// Assistant contract §7 (additive). The handle the assistant resolves
	// findings from, so a question needs no project data resent. Empty on an
	// older Core — the assistant then says it cannot resolve that analysis,
	// which is the honest outcome rather than a silent wrong answer.
	FString AnalysisId;
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
	// How many of TotalFixesApplied were Blueprint fixes (Kismet-side). The
	// UI labels its "N fixes applied" toast from THIS split — labelling from
	// the last scan mode showed "Blueprint" for a C++ fix applied after a BP
	// scan.
	int32   BlueprintFixesApplied = 0;
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
	// Whether the server actually ran the dry-run and we got a parseable
	// body back — NOT whether the fix is safe. /validate/check-fix-safety
	// does not exist on the Core as of this writing (404 on every call), so
	// this stays false on every real request today. Callers MUST branch on
	// this before trusting bSafe: bSafe defaults to true so a check that
	// never ran doesn't read as "unsafe", but it must never be read as
	// "confirmed safe" either — that's what bCheckRan is for.
	bool            bCheckRan = false;
	bool            bSafe     = true;
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

	// Free-tier cap metadata. Mirrors FShintValidateResult — the panel uses
	// the same banner widget for both Code Validator and Asset Naming Bot.
	bool    bLimitApplied  = false;
	FString LimitKind;          // "assets" on this endpoint
	int32   LimitValue     = 0; // assets actually scanned (post-cap)
	int32   TotalAvailable = 0; // assets discovered before the cap

	// Assistant contract §7 (additive) — see FShintValidateResult::AnalysisId.
	FString AnalysisId;

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

// [LOD-STRIP-BEGIN]
// ─────────────────────────────────────────────────────────────────────────────
// LOD Auditor (Studio tier) — mesh/texture/material optimisation audit.
// Mirrors the core Finding shape from /assets/lod/audit. The plugin extracts
// per-asset metadata (LOD counts, triangles, texture sizes, material slots)
// via the editor APIs and POSTs them; the server returns one finding per
// violation plus an aggregate summary. ai_guidance is only present when the
// request opted into bounded LLM enrichment (explain=true).
// ─────────────────────────────────────────────────────────────────────────────

struct FShintLodFinding
{
	FString AssetPath;
	FString RuleId;            // e.g. "LD003"
	FString RuleName;          // humanised title from the server
	FString Category;          // "Mesh" | "Texture" | "Material" | ...
	FString Severity;          // "warning" | "info" | "error"
	FString Message;
	FString Guidance;          // deterministic, engine-aware fix guidance
	FString AiGuidance;        // optional LLM guidance (top-N when explain=true)
	bool    bAutoFixable = false;

	// Estimated saving if the fix is applied — drives the summary + sort order.
	double  VramMb             = 0.0;
	int32   ShaderInstructions = 0;

	// ── Asset Optimizer table display fields ────────────────────────────────
	// Width/Height/Group/Format are joined client-side from the collection pass
	// (the server findings don't echo them back). Current/PotentialVramMb are
	// parsed from the finding's current/recommended dicts (present only for the
	// size-changing rules — others leave them 0 and the table shows "—").
	int32   Width           = 0;
	int32   Height          = 0;
	FString Group;             // per-family: texture LOD group / Static / Master…
	FString Format;            // per-family: compression / "Nanite" / blend mode
	FString ResText;           // per-family RESOLUTION cell when W/H don't apply
	                           //   (meshes: "12,345 tris", materials: "140 instr")
	double  CurrentVramMb   = 0.0;
	double  PotentialVramMb = 0.0;

	// ── Auto-fix targets (parsed from the finding's "recommended" dict) ──────
	// Drive the per-row "Fix" flow, which writes an optimised *duplicate* and
	// leaves the original untouched. Absent for non-size rules (left 0/empty).
	int32   RecMaxSize = 0;     // recommended.max_texture_size  (LT003 oversized)
	FString RecCompression;    // recommended.compression       (LT001/LT007)

	// ── In-place auto-fix descriptor (§20.5) ─────────────────────────────────
	// The full flattened "recommended" dict (string→string) drives the in-place
	// fixer registry: each recognised key (compression, recompute_normals,
	// two_sided, …) maps to one property write / build-settings change. Empty
	// for rules the server did not mark auto-fixable. Confidence gates batch
	// apply (§13.5: high pre-checked, medium unchecked, low per-row confirm).
	TMap<FString, FString> Recommended;
	FString Confidence;        // "high" | "medium" | "low" (default treated high)
};

struct FShintLodAuditResult
{
	bool    bSuccess        = false;
	int32   StatusCode      = 0;
	FString ErrorMessage;

	int32   AssetsAudited   = 0;
	int32   IssuesFound     = 0;
	int32   AutoFixable     = 0;
	double  EstimatedVramSavedMb            = 0.0;
	int32   EstimatedShaderInstructionsSaved = 0;

	// Client-computed during the collection pass (not from the server) — drive
	// the Asset Optimizer KPI tiles (per-category file counts + total VRAM).
	int32   TexturesAudited  = 0;
	int32   MeshesAudited     = 0;
	int32   MaterialsAudited  = 0;
	double  TotalVramMb       = 0.0;   // sum of resident texture VRAM

	// Assistant contract §7 — see FShintValidateResult::AnalysisId. The audit
	// is collected in chained batches, so only the LAST batch's id survives
	// into the merged result: it is the one whose stored document holds the
	// findings the user is looking at.
	FString AnalysisId;

	TArray<FShintLodFinding> Findings;
};
DECLARE_DELEGATE_OneParam(FOnShintLodAuditComplete, const FShintLodAuditResult&);
// [LOD-STRIP-END]

// [LOD-STRIP-BEGIN]
// ─────────────────────────────────────────────────────────────────────────────
// Predictive Profiler (Studio tier) — mirrors the core /predict/* contract v1.0
// (docs/predictive/API.md). The plugin collects the scene digest + render/build
// config + raw source, POSTs to /predict/analyze, and renders a risk dashboard
// (score gauges, frame-budget bar, top-issue list) + an Impact Simulator
// (/predict/simulate). Every number is a band: {Min, Expected, Max} + a
// confidence tag. Predictive PRICES cost — it does not diagnose; the title is
// the entity name/location, never a rule sentence.
// ─────────────────────────────────────────────────────────────────────────────

// One banded figure. Mirrors the core's Prediction type. Zeroed band == "no
// value" (Expected 0 with empty Unit).
struct FShintPrediction
{
	double  Expected  = 0.0;
	double  Min       = 0.0;
	double  Max       = 0.0;
	FString Unit;              // "ms_frame" | "mb" | "mb_min" | "s" | "min"
	FString Confidence;        // "high" | "medium" | "low"
	FString Basis;             // one human sentence explaining the figure

	bool IsSet() const { return !Unit.IsEmpty(); }

	// "+0.9–1.8 ms · est. +1.4 ms" style, sign-aware (deltas are negative).
	FString ToDisplay() const;
	// "est. +1.4 ms" — just the headline value + unit.
	FString ToHeadline() const;
};

// One 0-100 risk score plus the item ids that drive it (top-5).
struct FShintPredictScore
{
	int32           Value = 0;
	TArray<FString> Drivers;
};

// One priced item — a row of the report and the simulator's selection unit.
// impact/recovery are keyed by dimension ("cpu_ms_frame"|"gpu_ms_frame"|
// "vram_mb"|"ram_mb"|"gc_mb_min"|"build_mb"). Remediation present only when
// there IS a known optimization (bHasRemediation); otherwise it's just
// name + cost.
struct FShintPredictIssue
{
	FString ItemId;            // "ci-0042" — stable within a report
	int32   Layer     = 0;     // 1 assets | 2 scene | 3 code
	int32   Rank      = 0;
	FString Severity;          // "critical" | "warning" | "info"
	FString Title;             // entity name/location — NOT a description
	FString RuleId;            // source rule (metadata, not shown as title)
	FString SourceKind;        // "texture" | "code" | "scene" | ...
	FString SourcePath;
	int32   SourceLine = 0;

	// The item's TOTAL cost, keyed by dimension. The primary "cost" chip.
	TMap<FString, FShintPrediction> Impact;

	// Remediation — present only when there is something to recover.
	bool                            bHasRemediation = false;
	FString                         RemediationAction;
	TMap<FString, FShintPrediction> Recovery;   // what a fix buys back
	bool                            bAutoFixable = false;

	// Runtime UI state — not sent over wire.
	bool    bChecked = false;

	// Budget-normalized dominant dimension, as picked by the Core (which knows
	// the platform budgets — 40 MB of VRAM and 0.4 ms of CPU aren't comparable
	// as raw magnitudes, only as shares of their own budget). Empty on older
	// Core payloads that predate this field; DominantDimension() falls back to
	// a magnitude comparison only in that case.
	FString PrimaryDimension;

	// The dominant impact dimension + its headline value, for the table's
	// right-aligned cost chip (e.g. "MEM  +18–26 MB  est. +23 MB").
	FString DominantDimension() const;
};

// One stacked segment of the frame-budget bar (a layer/module contribution).
struct FShintBudgetSegment
{
	FString Label;             // "Code patterns" | "Scene dispatch" | ...
	double  ExpectedMs = 0.0;
};

// One CPU/GPU budget line: predicted spend vs budget, with a stack breakdown.
struct FShintBudgetLine
{
	double                       BudgetMs = 0.0;
	FShintPrediction             Predicted;   // .IsSet() false == no data
	TArray<FShintBudgetSegment>  Breakdown;
};

// The Core's authoritative frame-time figure — NOT cpu + gpu. CPU/GPU work
// on a frame is pipelined, so frame time is governed by the slower of the
// two (the bottleneck); summing both axes overstates the frame.
struct FShintFrameLine
{
	double  BudgetMs    = 0.0;
	double  PredictedMs = 0.0;
	FString Bottleneck;   // "cpu" | "gpu" | "" when unscored
	bool    bIsSet      = false;
};

// The full analyze report.
struct FShintPredictReport
{
	bool    bSuccess    = false;
	int32   StatusCode  = 0;
	FString ErrorMessage;

	FString ReportId;
	FString Engine;
	FString ProjectName;

	// Platform profile (denominators the dashboard renders raw).
	FString ProfileName;         // "desktop_60"
	FString ProfileDisplayName;
	double  FrameBudgetMs = 0.0;
	FString ReferenceHw;

	// Scores.
	FShintPredictScore CpuRisk;
	FShintPredictScore GpuRisk;
	FShintPredictScore MemoryRisk;
	FShintPredictScore BuildHealth;
	int32              OverallHealth = 0;

	// Frame budget.
	FShintBudgetLine Cpu;
	FShintBudgetLine Gpu;
	FShintFrameLine  Frame;   // authoritative frame prediction (bottleneck-based)

	// Memory / build headline predictions (may be unset).
	FShintPrediction Vram;
	int32            VramBudgetMb = 0;
	FShintPrediction Ram;
	int32            RamBudgetMb  = 0;
	FShintPrediction BuildSizeMb;

	// Items — top_issues is the ranked head; we keep the full cost_items so the
	// simulator can run stateless if the cached report expires.
	TArray<FShintPredictIssue> TopIssues;
	TArray<FShintPredictIssue> CostItems;

	// Transparency footer.
	FString CalibrationVersion;
	int32   CodeIssuesUncosted = 0;
	FString Disclaimer;
};
DECLARE_DELEGATE_OneParam(FOnShintPredictComplete, const FShintPredictReport&);

// Impact Simulator result — deltas per dimension + before/after scores.
struct FShintSimScores
{
	FShintPredictScore CpuRisk;
	FShintPredictScore GpuRisk;
	FShintPredictScore MemoryRisk;
	FShintPredictScore BuildHealth;
	int32              OverallHealth = 0;
};

struct FShintPredictRecommendation
{
	FString ItemId;
	FString Reason;            // "Largest remaining recovery: +0.29 ms (cpu…)"
	bool    bAutoFixable = false;
};

struct FShintSimulateResult
{
	bool    bSuccess    = false;
	int32   StatusCode  = 0;
	FString ErrorMessage;

	int32                            SelectedCount = 0;
	TMap<FString, FShintPrediction>  Deltas;        // negative = recovered
	FShintSimScores                  Before;
	FShintSimScores                  After;
	TArray<FShintPredictRecommendation> Recommendations;
};
DECLARE_DELEGATE_OneParam(FOnShintSimulateComplete, const FShintSimulateResult&);
// [LOD-STRIP-END]

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

// External web dashboard types (FShintWebDashboardResult,
// FOnShintWebDashboardComplete) moved to Core/ShintDashboardSync.h.
// The "Send to Dashboard" feature is a paid-tier-only POST that lives in
// its own translation unit so other builds can skip the code path.

// [AGENT-STRIP-BEGIN]
// ─────────────────────────────────────────────────────────────────────────────
// Agent — Auto-Fix Plan (Indie tier)
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
// LLM pivot — POST /agent/explain (single request/response, Indie tier)
//
// Replaces the previous /agent/review SSE flow: the core team confirmed that
// DeepSeek 1.3B Q4 cannot reliably emit JSON tool-call protocols, so the
// agent-with-tools pipeline was scrapped in favour of a single-shot
// explainer. One HTTP call carrying one already-enriched issue, one text
// response. Latency 30-45s on CPU; the customer dialog shows a spinner and
// rotating status text instead of streaming chunks.
//
// Tier gating: Free clients get 403. The panel hides the "Explain" button
// when LastCodeResult.Tier == "free" so the user never sees the 403.
// ─────────────────────────────────────────────────────────────────────────────

struct FShintAgentExplainResponse
{
	bool    bSuccess          = false;
	FString Explanation;            // LLM's 2-4 sentence answer (success path)
	float   GenerationSeconds = 0.f; // wall-clock the server spent generating
	FString Tier;                   // "free" → 403, "indie" → ok
	FString ErrorMessage;           // populated when bSuccess == false
};

DECLARE_DELEGATE_OneParam(FOnShintAgentExplainComplete, const FShintAgentExplainResponse&);
// [AGENT-STRIP-END]


// ─────────────────────────────────────────────────────────────────────────────
// Assistant (client contract v1.1) — /assistant/*
//
// Deliberately NOT behind a strip sentinel. Unlike /agent/* and the LOD
// Auditor, the assistant ships in EVERY edition including the free
// marketplace image: Free gets a working assistant with two intents and no
// memory. The panel must never be hidden behind a paid check — only the
// individual features are gated, and the gate comes from the server via
// FShintAssistantCapabilities, never from a hardcoded client-side table.
// ─────────────────────────────────────────────────────────────────────────────

/** One turn in a conversation thread (user or assistant). */
struct FShintAssistantTurn
{
	FString TurnId;
	FString Role;        // "user" | "assistant"
	FString Intent;
	FString RawText;
	FString ContextRef;  // analysis_id this turn resolved against
	FString RuleId;      // echoed grounding — lets a reopened thread restore
	FString AssetPath;   // its own context without the client remembering it
};

/** Result of POST /assistant/message (and the terminal event of the SSE twin). */
struct FShintAssistantResponse
{
	bool    bSuccess = false;
	FString ConversationId;
	FString Tier;
	FString Intent;
	bool    bContinued = false;  // true -> intent/grounding inherited server-side
	bool    bDegraded  = false;  // generation died; RawText is the deterministic fallback
	FShintAssistantTurn Reply;

	// Populated on failure. A 403 is a per-intent gate, never an endpoint gate:
	// AllowedIntents lists what this tier CAN run so the UI can explain itself.
	FString         ErrorMessage;
	int32           StatusCode = 0;
	FString         CurrentTier;
	TArray<FString> AllowedIntents;
};

/** GET /assistant/capabilities — asked once at startup; drives the whole UI. */
struct FShintAssistantCapabilities
{
	bool            bSuccess = false;
	FString         Tier;
	TArray<FString> Intents;
	FString         Memory;       // "none" | "session" | "full"
	FString         ModelProfile; // "light" | "advanced"
	FString         StudioRules;  // "" | "llm_evaluated" | "all"
	FString         ErrorMessage;

	bool CanRun(const FString& Intent) const { return Intents.Contains(Intent); }
	bool HasPersistentMemory() const { return Memory == TEXT("full"); }
};

/** A remembered fact. Nothing acts on it until Status == "confirmed". */
struct FShintAssistantFact
{
	FString FactId;
	FString Type;    // studio_fact | preference | decision
	FString Value;   // the user's own sentence, never paraphrased
	FString Status;  // proposed | confirmed | superseded | retracted
	FString SourceModule;
	FString SourceConversationId;
};

struct FShintAssistantMemory
{
	bool bSuccess = false;
	bool bMuted   = false;   // NDA silent mode for this project
	TArray<FShintAssistantFact> Facts;
	FString ErrorMessage;
};

/** A studio rule. A draft until a person activates it. */
struct FShintAssistantRule
{
	FString RuleId;
	FString Name;
	FString Tier;        // "template" (A) | "llm_evaluated" (B)
	FString Status;      // draft | active | deprecated
	FString Description; // how the compiler understood it — show verbatim
};

struct FShintAssistantRules
{
	bool bSuccess = false;
	TArray<FShintAssistantRule> Rules;
	FString ErrorMessage;
};

/** "You already decided this" — deterministic, errs toward silence. */
struct FShintAssistantContradiction
{
	FString FactId;
	FString FactValue;
	FString RuleId;
	FString AssetPath;
	FString Nudge;      // render as-is
};

struct FShintAssistantDecisions
{
	bool bSuccess = false;
	TArray<FShintAssistantContradiction> Contradictions;
	FString ErrorMessage;
};

/** Everything a turn may carry. Only Message is always required. */
struct FShintAssistantRequest
{
	FString Message;
	FString ConversationId;   // omit on the first turn
	FString Intent;           // omit to let the router classify
	FString ContextRef;       // analysis_id from a scan
	FString RuleId;
	FString AssetPath;
	// [LOD-STRIP-BEGIN]
	// simulate_change grounding. Studio-only end to end — the report comes
	// from the Predictive Profiler, which is not present in lower tiers.
	FString ReportId;
	TArray<FString> SelectedItemIds;
	// [LOD-STRIP-END]
	FString PlatformProfile;
	FString StudioId;
	FString ProjectId;
	FString ModuleContext;    // "lod_audit" | "code_validator" | ...
};

DECLARE_DELEGATE_OneParam(FOnShintAssistantComplete,     const FShintAssistantResponse&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantCapabilities, const FShintAssistantCapabilities&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantMemory,       const FShintAssistantMemory&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantRules,        const FShintAssistantRules&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantDecisions,    const FShintAssistantDecisions&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantThread,       const TArray<FShintAssistantTurn>&);


// ─────────────────────────────────────────────────────────────────────────────
// Local dashboard report (legacy — keeps a local sync)
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

	// External web dashboard auth.
	//
	//   * SessionToken — the user's per-login Bearer credential, written
	//     into shinttools.config.json by the launcher's apply_login_result.
	//     Required by every dashboard endpoint (Authorization header).
	//     Expires when the portal session expires (~30 days).
	//
	//   * ApiKeyDashboard — historical body field, kept for backward
	//     compatibility with older lovable handlers that read api_key
	//     from the JSON body. Current handlers ignore it.
	//
	//   * ApiKeyMongo — license key the LOCAL core resolves to a tier.
	//     Sent to /validate/*, /assets/scan, /agent/*. Distinct from
	//     the dashboard auth path — do not mix them.
	//
	// The default DashboardUrl points at the current dashboard host (the
	// previous default was a dead host that
	// swallowed every request silently). Override from
	// shinttools.config.json's `dashboard_url` field for staging.
	FString ApiKeyDashboard = TEXT("");
	FString ApiKeyMongo     = TEXT("");
	FString SessionToken    = TEXT("");
	FString DashboardUrl    = TEXT("https://shint.tools");

	// User-editable scan / export prefs — parity with the Unity Settings tab.
	//
	//   * ExcludedPaths — substrings checked against every absolute path
	//     CollectSourceFiles emits. If any entry is a prefix-substring of
	//     the file's path, the file is dropped before the scan request is
	//     built. Stored newline-separated in shinttools.config.json so the
	//     user can edit them by hand without re-quoting commas.
	//   * ExportPath — default destination folder for JSON exports of scan
	//     results. The plugin does not write here yet (Indie export feature
	//     pending); the field persists the user preference so the future
	//     export command can pre-fill it.
	TArray<FString> ExcludedPaths;
	FString         ExportPath      = TEXT("");

	FString GetBaseUrl() const
	{
#if SHINT_FREE_TIER
		// Free tier: host/port come from hardcoded loopback values;
		// overrides from the config are ignored in this tier so the plugin
		// always targets the local loopback core. Override
		// flexibility is available in
		// paid builds.
		return FString::Printf(TEXT("http://%s:%d"),
			SHINT_HARDCODED_CORE_HOST, SHINT_HARDCODED_CORE_PORT);
#else
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
#endif
	}

	bool HasExternalDashboard() const
	{
		// External dashboard authenticates via Authorization: Bearer
		// <ApiKeyDashboard>, a per-project credential. session_token is
		// deliberately NOT used here — it is not stored in the
		// project config. ApiKeyMongo is the
		// local-core license key used for tier resolution —
		// orthogonal to the dashboard and required only on /validate/* +
		// /assets/scan (those call sites send it explicitly).
		return !ApiKeyDashboard.IsEmpty() && !DashboardUrl.IsEmpty();
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

	/**
	 * Pre-flight safety check: calls /validate/check-fix-safety.
	 * On HTTP error (including 404 — this endpoint does not exist on the
	 * Core today) the returned result has bCheckRan=false. It does NOT
	 * "treat the fix as safe" — the caller is responsible for telling the
	 * user the dry-run didn't run and asking explicitly before applying.
	 */
	void CheckFixSafety(const TArray<FShintCodeIssue>& Issues, FOnShintSafetyCheckComplete OnComplete);

	// ── Code Validator — external web dashboard ──────────────────────────────
	//   Moved to FShintDashboardSync::SendCodeValidator
	//   (see Core/ShintDashboardSync.h).

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

	// [LOD-STRIP-BEGIN]
	// ── LOD Auditor (Studio tier) — local engine ─────────────────────────────
	/**
	 * Audits every mesh / texture / material under /Game for LOD and
	 * optimisation issues. Loads each asset, extracts the metadata the core
	 * rules need (LOD counts + per-LOD triangles, texture dimensions +
	 * compression, material slot counts), and POSTs to /assets/lod/audit.
	 *
	 * @param Profile     "default" | "mobile" — selects the threshold set.
	 * @param bExplainTop When true, asks the server to attach LLM ai_guidance
	 *                    to the top findings (Studio only; ~30s/finding on CPU).
	 */
	void AuditLods(const FString& Profile, bool bExplainTop,
	               FOnShintLodAuditComplete OnComplete, bool bDeepScan = false);
	// Public so the batched-audit driver (a file-local helper in
	// ShintCoreClient_Lod.cpp, which chains one request per asset chunk) can
	// parse each batch response. Pure static JSON->struct helper, no state.
	static FShintLodAuditResult ParseLodAuditResponse(const FShintRequestResult& Raw);

	// Map the TextureCompressionSettings enum to the TC_* string the core's
	// lod_auditor.vram_model._FORMAT_ALIASES table understands. Shared by the
	// LOD Auditor's extractor AND the Predictive Profiler's asset collector
	// (ShintCoreClient_Predictive.cpp) — Predictive used to derive this via
	// raw UEnum::GetNameStringByValue() reflection, which is one registration
	// quirk away from silently emitting a name the core doesn't recognize
	// (falls back to RGBA8 sizing, the same class of bug as the Unity DXT1
	// VRAM-inflation fix). One curated mapping, one place it can be wrong.
	static FString TextureCompressionToString(TextureCompressionSettings TC);

	// ── Predictive Profiler (Studio tier) — local engine ──────────────────────
	/**
	 * Analyze a project's predicted cost. Sends assets + a scene digest
	 * (actors/ticking Blueprints/skeletal meshes/lights) + raw source via the
	 * core's batched-ingest session (assets chunked at 150 — the LOD-audit
	 * OOM lesson). The report is the simulator's input — keep it
	 * (SimulatePrediction can also run stateless from CostItems).
	 *
	 * @param Profile  platform profile name ("desktop_60", "mobile_30", …).
	 */
	void AnalyzePrediction(const FString& Profile, FOnShintPredictComplete OnComplete);

	/**
	 * Impact Simulator — POST /predict/simulate against a cached report. Passes
	 * the selected item ids; the optional NewProfile answers "what if I port
	 * this?" (both before/after recomputed on that profile). InlineCostItems is
	 * the stateless fallback when the cached report has expired.
	 */
	void SimulatePrediction(const FString& ReportId,
	                        const TArray<FString>& SelectedItemIds,
	                        const FString& NewProfile,
	                        const TArray<FShintPredictIssue>& InlineCostItems,
	                        FOnShintSimulateComplete OnComplete);

	// Public pure JSON->struct helpers (no state) — mirror ParseLodAuditResponse.
	static FShintPredictReport   ParsePredictResponse(const FShintRequestResult& Raw);
	static FShintSimulateResult  ParseSimulateResponse(const FShintRequestResult& Raw);
	// [LOD-STRIP-END]

	// ── Asset Naming Bot — external web dashboard ────────────────────────────
	//   Moved to FShintDashboardSync::SendAssetNaming
	//   (see Core/ShintDashboardSync.h).

	// ── Local dashboard report (legacy) ───────────────────────────────
	void SendDashboardReport(const FShintDashboardReport& Report,
	                         FOnShintDashboardComplete OnComplete);

	// [AGENT-STRIP-BEGIN]
	// ── Agent — Auto-Fix Plan (Indie tier) ────────────────────────────────────
	/**
	 * Sends the validator's last result to /agent/plan and receives a
	 * prioritized fix plan with per-step rationale. Free-tier servers
	 * respond 403 — callers should hide the UI affordance there rather
	 * than display the error.
	 */
	void RequestAgentPlan(const FShintValidateResult& Source,
	                      FOnShintAgentPlanComplete OnComplete);

	/** LLM pivot — POST /agent/explain.
	 *
	 *  Sends one already-enriched issue (rule_name + rule_explanation come
	 *  from the validate response, the panel does not synthesise them) to
	 *  the explainer endpoint and parses the JSON reply into
	 *  FShintAgentExplainResponse. The server takes 30-45 seconds to answer
	 *  on CPU; the panel must show a spinner with rotating status text.
	 *
	 *  Failure paths surface through OnComplete with bSuccess=false:
	 *    HTTP 403   → Tier="free", upgrade hint in ErrorMessage
	 *    HTTP 404   → "core too old" hint
	 *    LLM unavail→ server returns success=false with its own message
	 *    Network    → generic transport error
	 */
	void RequestExplainIssue(const FShintCodeIssue&      Issue,
	                         FOnShintAgentExplainComplete OnComplete);

	/**
	 *  Streaming twin of RequestExplainIssue — POSTs /agent/explain/stream and
	 *  surfaces tokens as they generate (first token in ~3-5s) via OnChunk,
	 *  then OnComplete with the final text. Streaming keeps the connection
	 *  active token-by-token, so it never hits the silent-generation gap that
	 *  made the synchronous call trip the HTTP activity timeout.
	 */
	void RequestExplainIssueStream(const FShintCodeIssue&       Issue,
	                               FOnShintStreamChunk          OnChunk,
	                               FOnShintAgentExplainComplete OnComplete);
	// [AGENT-STRIP-END]

	// ── Assistant (all tiers) — /assistant/* ─────────────────────────────────
	// Implemented in ShintCoreClient_Assistant.cpp. Never strip-gated: the
	// free image serves this router too, and the panel is expected to be
	// visible on every tier (features gate per-intent, from the server).

	/**
	 * One conversation turn — POST /assistant/message.
	 *
	 * Omit ConversationId on the first turn; the response carries the id to
	 * reuse. A short follow-up ("and why?") needs no context at all: the
	 * server inherits the previous turn's intent and grounding and answers
	 * with bContinued=true. Sending context anyway is always safe — an
	 * explicit value overrides what would have been inherited.
	 *
	 * Failure paths surface through OnComplete with bSuccess=false:
	 *   403 → per-intent gate; AllowedIntents lists what this tier can run
	 *   404 → conversation unknown or idle-expired (12h); start a new one
	 */
	void SendAssistantMessage(const FShintAssistantRequest& Request,
	                          FOnShintAssistantComplete OnComplete);

	/**
	 * Streaming twin — POST /assistant/message/stream. Same inputs, same
	 * gating, same persistence; pick per call. Only explain_finding streams
	 * token-by-token — everything else answers from a table and arrives as a
	 * single chunk, so do not animate it.
	 *
	 * 403/404 are real status codes raised before the stream opens, not
	 * error events inside a 200, and surface through OnComplete as usual.
	 */
	void SendAssistantMessageStream(const FShintAssistantRequest& Request,
	                                FOnShintStreamChunk       OnChunk,
	                                FOnShintAssistantComplete OnComplete);

	/** GET /assistant/capabilities — ask once at startup, drive the UI from
	 *  it. Never hardcode a tier table client-side. */
	void GetAssistantCapabilities(FOnShintAssistantCapabilities OnComplete);

	/** GET /assistant/conversations/{id} — full thread in order, for
	 *  restoring the panel after an editor restart. */
	void GetAssistantConversation(const FString& ConversationId,
	                              FOnShintAssistantThread OnComplete);

	// ── Memory (capabilities.Memory == "full" only) ──────────────────────────
	void GetAssistantMemory(const FString& StudioId, const FString& ProjectId,
	                        FOnShintAssistantMemory OnComplete);

	/** Promote a proposed fact to confirmed (bAccept) or retract it. Until a
	 *  person accepts, the fact is invisible to every other part of the
	 *  system — this call is the only gate. */
	void ConfirmAssistantFact(const FString& FactId, bool bAccept,
	                          FOnShintAssistantComplete OnComplete);

	/** NDA silent mode — stop recording memory for one project. */
	void MuteAssistantMemory(const FString& StudioId, const FString& ProjectId,
	                         bool bMuted, FOnShintAssistantComplete OnComplete);

	/** Purge every fact for a closed project. Irreversible server-side. */
	void PurgeAssistantProject(const FString& StudioId, const FString& ProjectId,
	                           FOnShintAssistantComplete OnComplete);

	// ── Studio rules ─────────────────────────────────────────────────────────
	void GetAssistantRules(const FString& StudioId, const FString& ProjectId,
	                       FOnShintAssistantRules OnComplete);

	/** Activate a draft rule (bAccept) or discard it. Only active,
	 *  user-confirmed rules ever run during a scan. */
	void ConfirmAssistantRule(const FString& RuleId, bool bAccept,
	                          FOnShintAssistantComplete OnComplete);

	/** POST /assistant/decisions/check — call after a scan to surface
	 *  "you already decided this" contradictions. Deterministic and errs
	 *  toward silence; an empty array is the normal case. */
	void CheckAssistantDecisions(const FString& StudioId, const FString& ProjectId,
	                             const FString& ContextRef,
	                             FOnShintAssistantDecisions OnComplete);

	// ── Generic ───────────────────────────────────────────────────────────────
	void SendRequest(const FString& FullUrl, EShintHttpMethod Method,
	                 const FString& Body, FOnShintRequestComplete OnComplete,
	                 const TMap<FString, FString>& ExtraHeaders = {});

	// Streaming variant of SendRequest for Server-Sent-Events endpoints.
	// OnChunk fires on the game thread per {"chunk"} event; OnComplete fires
	// once at stream end, ResponseBody carrying the accumulated text (or the
	// raw error body on a non-2xx status).
	void SendRequestStream(const FString& FullUrl, EShintHttpMethod Method,
	                       const FString& Body, FOnShintStreamChunk OnChunk,
	                       FOnShintRequestComplete OnComplete,
	                       const TMap<FString, FString>& ExtraHeaders = {});

	// GetConfig() / GetConfigMutable() are already declared above
	// (lines ~409). FShintDashboardSync uses GetConfig() to read
	// DashboardUrl + ApiKeyDashboard without touching internals.

	// Helpers exposed for sibling classes that build payloads
	// against the same JSON shape (FShintDashboardSync). Kept static
	// + pure so they can stay free functions in spirit.
	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);
	static FString AssetTypeToCategory(const FString& AssetType);

	// Public so the batched ValidateBlueprints driver (a file-local helper
	// in ShintCoreClient_Validator.cpp, which chains one request per
	// Blueprint chunk — see kBlueprintValidateBatchSize) can parse each
	// batch response. Pure static JSON->struct helper, no state. Mirrors
	// ParseLodAuditResponse's reason for being public.
	static FShintValidateResult  ParseValidateResponse (const FShintRequestResult& Raw);

private:
	void OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                           bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete);

	void OnHttpStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                          bool bConnectedSuccessfully,
	                          TSharedRef<FShintSseParser> Parser,
	                          FOnShintRequestComplete OnComplete);

	static FString MethodToString(EShintHttpMethod Method);
	static FShintAssetScanResult ParseAssetScanResponse(const FShintRequestResult& Raw);
	static FShintFixResult       ParseFixResponse      (const FShintRequestResult& Raw);
	static FShintFixResult       ParseTreeSitterFixResponse(const FShintRequestResult& Raw);
	// Slice B helpers — populate one snapshot from a JSON object that matches
	// the score document shape returned by the core engine.
	static bool                  ParseScoreObject(const TSharedPtr<class FJsonObject>& Obj,
	                                              FShintQualityScoreSnapshot& Out);
	static FShintQualityScoreSnapshot         ParseLatestScoreResponse (const FShintRequestResult& Raw);
	static FShintQualityScoreHistory          ParseScoreHistoryResponse(const FShintRequestResult& Raw);
	static void CollectSourceFiles(const FString& Dir, TArray<FString>& Out);

	void HandleTreeSitterFixResponse(const FShintRequestResult& Raw,
	                                 FShintFixResult             LocalResult,
	                                 TArray<FShintCodeIssue>     TreeSitterIssues,
	                                 FOnShintFixComplete         OnComplete);

	// [AGENT-STRIP-BEGIN]
	static FShintAgentPlanResult ParseAgentPlanResponse(const FShintRequestResult& Raw);
	// [AGENT-STRIP-END]

	FShintCoreConfig Config;
};