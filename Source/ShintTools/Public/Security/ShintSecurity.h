// Copyright 2026 ShintTools. All Rights Reserved.
//
// Compile-time security knobs for the Free tier of the ShintTools plugin.
//
// ``SHINT_FREE_TIER`` is set to 1 by the build script that produces the
// stripped Free-tier variant of the plugin (see
// ``tools/minify_plugin.py`` in the UE5 plugin repo) and to 0 in
// Indie/Studio builds. Code that branches on this macro hardens the
// Free variant against trivial tampering while leaving paid-tier builds
// flexible enough for development and support workflows.
//
// What lives here:
//   * SHINT_FREE_TIER macro definition fallback
//   * SHINT_LOG_SECURE — scrubs api_key / tier strings from logs in Free
//   * SHINT_HARDCODED_CORE_HOST / _PORT — lockdown values used by
//     FShintCoreConfig::GetBaseUrl() when SHINT_FREE_TIER == 1

#pragma once

#include "CoreMinimal.h"
#include "ShintTools.h"  // LogShintTools

// ─────────────────────────────────────────────────────────────────────────────
// Tier compile-time flag
// ─────────────────────────────────────────────────────────────────────────────

#ifndef SHINT_FREE_TIER
	#define SHINT_FREE_TIER 0
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Core endpoint lockdown for Free builds
//
// In Free, FShintCoreConfig::GetBaseUrl() ignores any host/port override
// loaded from shinttools.config.json and forces the canonical loopback
// values. That prevents a customer from redirecting the plugin at a
// rogue local proxy that always returns tier=indie.
// ─────────────────────────────────────────────────────────────────────────────

#define SHINT_HARDCODED_CORE_HOST TEXT("127.0.0.1")
#define SHINT_HARDCODED_CORE_PORT 18200

// ─────────────────────────────────────────────────────────────────────────────
// Secure logging
//
// Free builds collapse SHINT_LOG_SECURE to UE_LOG(Verbose) so sensitive
// values do not surface in the default editor log. Verbose category is
// suppressed by default in shipping builds; developers can re-enable it
// per category via DefaultEngine.ini if a support escalation needs it.
//
// Paid builds keep the original Log verbosity so internal QA and
// customer-support tooling still see the lines.
// ─────────────────────────────────────────────────────────────────────────────

#if SHINT_FREE_TIER
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Verbose, Fmt, ##__VA_ARGS__)
#else
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Log, Fmt, ##__VA_ARGS__)
#endif

// Helper: mask an api_key / license key for the few places it MUST be
// logged for support (HTTP failure where the customer is told to share
// the masked key). Returns "shint_***xxxx" given "shint_aabbccddxxxx".
inline FString ShintMaskKey(const FString& Key)
{
	if (Key.Len() <= 8) return TEXT("***");
	return FString::Printf(TEXT("%s***%s"),
		*Key.Left(6), *Key.Right(4));
}
