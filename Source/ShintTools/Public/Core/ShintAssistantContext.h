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

	/**
	 * Ask the assistant to explain one finding.
	 *
	 * This is what the per-row "Explain" button now does: instead of opening
	 * a throwaway modal that answered once and forgot, it queues the question
	 * here and invokes the assistant tab, so the answer lands in a thread the
	 * user can keep asking into ("and why does that matter?").
	 *
	 * The panel consumes the request when it next hears OnChanged — including
	 * the case where the click is what opened the panel in the first place,
	 * because the pending request survives until something takes it.
	 */
	static void RequestExplain(const FString& RuleId, const FString& AssetPath,
	                           const FString& Question);

	/** Take the queued explain request, if any. Returns false when there is
	 *  none; the request is cleared on a successful take so a later refresh
	 *  cannot replay it. */
	static bool ConsumePendingExplain(FString& OutRuleId, FString& OutAssetPath,
	                                  FString& OutQuestion);


	/** Fired whenever the context changes, so an open panel can relabel its
	 *  context strip without polling. */
	DECLARE_MULTICAST_DELEGATE(FOnShintAssistantContextChanged);
	static FOnShintAssistantContextChanged OnChanged;
};
