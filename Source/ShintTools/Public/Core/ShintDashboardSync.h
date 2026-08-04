// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

// Forward declarations so we don't pull the whole ShintCoreClient.h
// into every translation unit that includes this header. The .cpp
// includes the full header to access the result types and
// SendRequest.
class FShintCoreClient;
struct FShintValidateResult;
struct FShintAssetScanResult;
// [LOD-STRIP-BEGIN]
struct FShintLodAuditResult;
// [LOD-STRIP-END]
struct FShintPredictReport;

/**
 * Result of a POST to the external shint.tools dashboard.
 * bSuccess=true means HTTP 2xx. On failure, ErrorMessage carries a
 * human-readable string (extracted from the JSON `error` field when
 * present, else `HTTP <code>: <body>`).
 */
struct FShintWebDashboardResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString ResponseBody;
};
DECLARE_DELEGATE_OneParam(FOnShintWebDashboardComplete,
	const FShintWebDashboardResult&);

/**
 * FShintDashboardSync
 *
 * Encapsulates POSTs from the editor plugin to the external web
 * dashboard (shint.tools) — i.e. the "Send to Dashboard" buttons on
 * the Code Validator, Asset Naming, LOD Auditor and Predictive Profiler
 * tabs. Lives outside FShintCoreClient because:
 *   - it talks to a different host (DashboardUrl vs the local core)
 *   - it uses a different auth scheme (Bearer ApiKeyDashboard)
 *   - it's a paid-tier-only feature; the wizard panel gates the
 *     buttons behind Tier != "free" (Predictive Profiler is Studio-only
 *     end to end, so it gates on IsStudioTier() instead), so isolating
 *     the code here lets the free build skip a chunk of dead code.
 *
 * Thin wrapper: borrows the underlying transport (SendRequest, the
 * config holder) from the FShintCoreClient passed at construction.
 * Doesn't own the client; the caller (SShintToolsPanel) owns both.
 */
class FShintDashboardSync : public TSharedFromThis<FShintDashboardSync>
{
public:
	explicit FShintDashboardSync(FShintCoreClient& InClient)
		: Client(InClient) {}

	/**
	 * Sends the project scan RESULTS (metrics only) to the dashboard.
	 * Privacy: findings + counts + file metadata are sent; raw
	 * source `content`/snippets are NEVER transmitted (nor read from disk).
	 * Endpoint: POST {DashboardUrl}/api/public/code-validator/analyze
	 * Body: { project_name,
	 *         files:  [{name, path, type, lines_count, issue_count,
	 *                   issues: [{rule_id, rule_name, severity, category,
	 *                             line, message}]}],
	 *         totals: {files_scanned, total_issues, total_errors,
	 *                  total_warnings, quality_score?} }
	 * Auth:  Authorization: Bearer <per-project key>
	 */
	void SendCodeValidator(const FShintValidateResult& LastResult,
		FOnShintWebDashboardComplete OnComplete);

	/**
	 * Sends the asset-naming violations to the dashboard.
	 * Endpoint: POST {DashboardUrl}/api/public/naming-bot/analyze
	 * Body: { project_name, items: [{name, path, type, category}] }
	 * Auth:  Authorization: Bearer <per-project key>
	 */
	void SendAssetNaming(const FShintAssetScanResult& LastResult,
		FOnShintWebDashboardComplete OnComplete);

	// [LOD-STRIP-BEGIN]
	/**
	 * Sends the LOD Auditor RESULTS (metrics only) to the dashboard.
	 * Privacy: per-finding metadata + aggregate KPIs are sent; no asset bytes.
	 * Endpoint: POST {DashboardUrl}/api/public/lod-auditor/analyze
	 * Body: { project_name, engine,
	 *         findings: [{asset_path, rule_id, rule_name, category, severity,
	 *                     message, auto_fixable, vram_mb, shader_instructions}],
	 *         stats:    {assets_audited, issues_found, auto_fixable,
	 *                    textures, meshes, materials, total_vram_mb,
	 *                    estimated_vram_saved_mb, estimated_shader_saved} }
	 * Auth:  Authorization: Bearer <per-project key>
	 */
	void SendLodAudit(const FShintLodAuditResult& LastResult,
		FOnShintWebDashboardComplete OnComplete);
	// [LOD-STRIP-END]

	/**
	 * Sends the Predictive Profiler RESULTS (scores + top issues) to the
	 * dashboard. Privacy: same posture as SendLodAudit — priced-item
	 * metadata + aggregate scores, no asset bytes.
	 * Endpoint: POST {DashboardUrl}/api/public/predictive-profiler/analyze
	 * Body: { project_name, engine, profile,
	 *         scores: {cpu_risk, gpu_risk, memory_risk, build_health,
	 *                  overall_project_health},
	 *         top_issues: [{item_id, layer, severity, title, rule_id,
	 *                       auto_fixable}],
	 *         stats: {top_issues_count, cost_items_count,
	 *                 code_issues_uncosted, calibration_version} }
	 * Auth:  Authorization: Bearer <per-project key>
	 */
	void SendPredictive(const FShintPredictReport& LastReport,
		FOnShintWebDashboardComplete OnComplete);

private:
	FShintCoreClient& Client;
};
