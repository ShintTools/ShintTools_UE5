// Copyright 2026 ShintTools. All Rights Reserved.
//
// Compile-time tier knobs for the ShintTools plugin.
//
// SHINT_FREE_TIER is 0 in standard builds. It gates a few helper macros
// (declared below) used for logging hygiene and endpoint configuration.

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
// Core endpoint defaults
// ─────────────────────────────────────────────────────────────────────────────

#define SHINT_HARDCODED_CORE_HOST TEXT("127.0.0.1")
#define SHINT_HARDCODED_CORE_PORT 18200

// ─────────────────────────────────────────────────────────────────────────────
// Secure logging
//
// Routes sensitive values to a low-verbosity log category.
// ─────────────────────────────────────────────────────────────────────────────

#if SHINT_FREE_TIER
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Verbose, Fmt, ##__VA_ARGS__)
#else
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Verbose, Fmt, ##__VA_ARGS__)
#endif

// Helper: mask a key for the rare cases where it must appear in a log line.
inline FString ShintMaskKey(const FString& Key)
{
	if (Key.Len() <= 8) return TEXT("***");
	return FString::Printf(TEXT("%s***%s"),
		*Key.Left(6), *Key.Right(4));
}
