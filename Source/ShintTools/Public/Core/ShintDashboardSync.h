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
 * the Code Validator and Asset Naming tabs. Lives outside
 * FShintCoreClient because:
 *   - it talks to a different host (DashboardUrl vs the local core)
 *   - it uses a different auth scheme (Bearer ApiKeyDashboard)
 *   - it's a paid-tier-only feature; the wizard panel gates the
 *     buttons behind Tier != "free", so isolating the code here lets
 *     the free build skip a chunk of dead code.
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
	 * Privacy (issue #314): findings + counts + file metadata are sent; raw
	 * source `content`/snippets are NEVER transmitted (nor read from disk).
	 * Endpoint: POST {DashboardUrl}/api/public/code-validator/analyze
	 * Body: { project_name,
	 *         files:  [{name, path, type, lines_count, issue_count,
	 *                   issues: [{rule_id, rule_name, severity, category,
	 *                             line, message}]}],
	 *         totals: {files_scanned, total_issues, total_errors,
	 *                  total_warnings, quality_score?} }
	 * Auth:  Authorization: Bearer <ApiKeyDashboard>
	 */
	void SendCodeValidator(const FShintValidateResult& LastResult,
		FOnShintWebDashboardComplete OnComplete);

	/**
	 * Sends the asset-naming violations to the dashboard.
	 * Endpoint: POST {DashboardUrl}/api/public/naming-bot/analyze
	 * Body: { project_name, items: [{name, path, type, category}] }
	 * Auth:  Authorization: Bearer <ApiKeyDashboard>
	 */
	void SendAssetNaming(const FShintAssetScanResult& LastResult,
		FOnShintWebDashboardComplete OnComplete);

private:
	FShintCoreClient& Client;
};
