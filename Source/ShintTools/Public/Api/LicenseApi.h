// Copyright 2026 ShintTools. All Rights Reserved.
//
// Per-endpoint API class for license/tier resolution. Exposes a
// module-wide cached result so the License badge and feature gates
// don't have to wait for the first scan to learn the tier.
//
// Depends only on the transport layer (FShintHttpClient + the shared
// EShintHttpMethod from ShintHttpTypes.h).

#pragma once

#include "CoreMinimal.h"
#include "Transport/ShintHttpTypes.h"

class FShintHttpClient;

/** Resolved license payload, returned by /license/status. */
struct FShintLicenseStatus
{
	FString Tier;          // "free" | "indie" | "studio" | "enterprise"
	FString ErrorMessage;  // empty on success
	float   ElapsedSeconds = 0.f;
	bool    bSuccess = false;
};

DECLARE_DELEGATE_OneParam(FOnShintLicenseStatusComplete,
                          const FShintLicenseStatus&);

/**
 * Thin wrapper around POST /license/status.
 *
 * Construction takes the shared HTTP transport. Multiple Api/* classes
 * will follow this pattern in subsequent Sprint 2 commits (Validate,
 * Assets, Agent, etc.). The transport is held by reference because the
 * module owns its lifetime and outlives every API helper.
 */
class FShintLicenseApi
{
public:
	FShintLicenseApi(TSharedRef<FShintHttpClient> InTransport,
	                 FString InBaseUrl);

	/** Fire /license/status; OnComplete is invoked on the game thread. */
	void RequestStatus(const FString& ApiKey,
	                   FOnShintLicenseStatusComplete OnComplete);

private:
	TSharedRef<FShintHttpClient> Transport;
	FString BaseUrl;
};
