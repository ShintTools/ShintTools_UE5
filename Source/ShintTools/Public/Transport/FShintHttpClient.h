// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "ShintHttpTypes.h"

struct FShintHttpResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};

DECLARE_DELEGATE_OneParam(FOnShintHttpComplete, const FShintHttpResult&);

class FShintHttpClient : public TSharedFromThis<FShintHttpClient, ESPMode::ThreadSafe>
{
public:
	FShintHttpClient() = default;

	explicit FShintHttpClient(FString InUserAgentSuffix)
		: UserAgent(FString::Printf(TEXT("ShintTools-UE5/%s"), *InUserAgentSuffix))
	{}

	static FString MethodToString(EShintHttpMethod Method);

	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);

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

	static float TimeoutForUrl(const FString& Url);

	FString UserAgent = TEXT("ShintTools-UE5/unknown");
};
