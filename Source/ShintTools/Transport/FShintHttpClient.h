// Copyright ShintTools. All Rights Reserved.
//
// Pure HTTP transport for ShintTools — Sprint 1 of the plugin refactor.
//
// Goal: extract the HTTP plumbing (method enum, result struct, send + dispatch
// callback, JSON helper, timeout policy) out of FShintCoreClient so that:
//
//   * Per-endpoint Api/* classes (Sprint 2+) depend on this transport only,
//     never on the 2000-line FShintCoreClient monolith.
//   * Tests can substitute a fake transport without touching reflection or
//     Slate.
//   * Adding mTLS / cert pinning (security Capa B) lands here once instead
//     of in every endpoint method.
//
// Identical wire behaviour to the previous FShintCoreClient::SendRequest:
// same headers, same User-Agent, same timeout policy (180s for /agent/explain,
// 90s for the rest). Sprint 1 is a refactor, not a behaviour change.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ShintHttpTypes.h"  // EShintHttpMethod (shared with Core/)

struct FShintHttpResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};

DECLARE_DELEGATE_OneParam(FOnShintHttpComplete, const FShintHttpResult&);

/**
 * Stateless HTTP transport. Pure functions on a shared instance — no
 * connection pooling state, no per-endpoint logic. The instance is held by
 * FShintCoreClient via TSharedRef so OnProcessRequestComplete keeps a valid
 * `this` even if the owning client is torn down mid-flight.
 */
class FShintHttpClient : public TSharedFromThis<FShintHttpClient, ESPMode::ThreadSafe>
{
public:
	FShintHttpClient() = default;

	/** Configure log-tagged user-agent suffix once at construction. */
	explicit FShintHttpClient(FString InUserAgentSuffix)
		: UserAgent(FString::Printf(TEXT("ShintTools-UE5/%s"), *InUserAgentSuffix))
	{}

	/** Static helper: stringify a method for the HTTP verb. */
	static FString MethodToString(EShintHttpMethod Method);

	/** Static helper: serialize a JSON object to a compact string. */
	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);

	/** Fire a request. The callback is invoked on the game thread. */
	void Send(
		const FString& FullUrl,
		EShintHttpMethod Method,
		const FString& Body,
		FOnShintHttpComplete OnComplete,
		const TMap<FString, FString>& ExtraHeaders = {});

private:
	void HandleResponse(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully,
		FOnShintHttpComplete OnComplete);

	/** Timeout in seconds for a given URL. LLM endpoints get 180s, the rest 90s. */
	static float TimeoutForUrl(const FString& Url);

	FString UserAgent = TEXT("ShintTools-UE5/unknown");
};
