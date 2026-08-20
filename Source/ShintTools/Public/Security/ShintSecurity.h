// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShintTools.h"

#ifndef SHINT_FREE_TIER
	#define SHINT_FREE_TIER 0
#endif

#define SHINT_HARDCODED_CORE_HOST TEXT("127.0.0.1")
#define SHINT_HARDCODED_CORE_PORT 18200

#if SHINT_FREE_TIER
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Verbose, Fmt, ##__VA_ARGS__)
#else
	#define SHINT_LOG_SECURE(Fmt, ...) \
		UE_LOG(LogShintTools, Verbose, Fmt, ##__VA_ARGS__)
#endif

inline FString ShintMaskKey(const FString& Key)
{
	if (Key.Len() <= 8) return TEXT("***");
	return FString::Printf(TEXT("%s***%s"),
		*Key.Left(6), *Key.Right(4));
}
