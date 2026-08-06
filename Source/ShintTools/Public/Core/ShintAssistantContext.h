// Copyright 2026 ShintTools. All Rights Reserved.
//
// FShintAssistantContext — the shared "what is the user looking at right now"
// object the assistant panel grounds its answers in.
//
// Every module publishes the analysis_id it just received here when a scan
// completes. The assistant panel reads it and sends it as `context_ref`, so
// the user can ask "why is this flagged?" without copying anything into the
// conversation. The Core resolves the actual findings server-side from that
// id — the panel never re-collects project data of its own.
//
// Deliberately a process-wide static rather than a member of any panel: the
// publisher (a results view) and the consumer (a separate nomad tab) never
// hold a reference to each other, and either can be closed while the other
// stays open. Same posture as FShintToolsModule::GetCachedTier().
//
// Not strip-gated — the assistant ships in every tier. Publishing from a
// paid-only module is what carries a sentinel, not this file.

#pragma once

#include "CoreMinimal.h"

/** Which module produced the active analysis. Sent as `module_context`. */
enum class EShintAssistantModule : uint8
{
	None,
	CodeValidator,
	AssetNaming,
	// [LOD-STRIP-BEGIN]
	LodAudit,
	Predictive,
	// [LOD-STRIP-END]
};

struct SHINTTOOLS_API FShintAssistantContext
{
	/**
	 * Record the analysis a scan just produced. Call from the completion
	 * handler of any scan that returns an `analysis_id`.
	 *
	 * @param AnalysisId  the id echoed by the scan response; empty clears
	 *                    the context (an older Core that does not send one
	 *                    should leave the panel ungrounded rather than
	 *                    pointing at a stale previous scan).
	 * @param Module      which module ran, for the `module_context` field.
	 * @param Summary     one short line for the panel's context strip, e.g.
	 *                    "LOD Audit — 40 findings".
	 */
	static void Publish(const FString& AnalysisId, EShintAssistantModule Module,
	                    const FString& Summary);

	/** Clear the context — no analysis is currently in view. */
	static void Clear();

	static FString               GetAnalysisId();
	static EShintAssistantModule GetModule();
	static FString               GetSummary();

	/** `module_context` wire value for the active module ("lod_audit", …). */
	static FString GetModuleContextString();

	/** True when an analysis is available to ground a question against. */
	static bool HasContext();

	// [LOD-STRIP-BEGIN]
	/**
	 * The Predictive Profiler's report id, kept separately from AnalysisId
	 * because `simulate_change` needs `report_id` + `selected_item_ids`
	 * rather than a `context_ref` — they are different grounding channels in
	 * the contract, and a LOD audit must not overwrite a live report.
	 */
	static void    PublishReport(const FString& ReportId);
	static FString GetReportId();
	// [LOD-STRIP-END]

	/** Fired whenever the context changes, so an open panel can relabel its
	 *  context strip without polling. */
	DECLARE_MULTICAST_DELEGATE(FOnShintAssistantContextChanged);
	static FOnShintAssistantContextChanged OnChanged;
};
