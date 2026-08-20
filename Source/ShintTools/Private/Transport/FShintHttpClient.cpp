// Copyright 2026 ShintTools. All Rights Reserved.

#include "Transport/FShintHttpClient.h"

#include "HttpModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShintTools.h"  // LogShintTools
#include "ShintEngineCompat.h"

FString FShintHttpClient::MethodToString(EShintHttpMethod Method)
{
	switch (Method)
	{
		case EShintHttpMethod::GET:     return TEXT("GET");
		case EShintHttpMethod::POST:    return TEXT("POST");
		case EShintHttpMethod::PUT:     return TEXT("PUT");
		case EShintHttpMethod::DELETE_: return TEXT("DELETE");
	}
	return TEXT("GET");
}

FString FShintHttpClient::SerializeJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj, W);
	return Out;
}

float FShintHttpClient::TimeoutForUrl(const FString& Url)
{
	// [AGENT-STRIP-BEGIN]
	// /agent/explain runs the local LLM and takes 30-45s typical / 60-90s
	// on slow CPUs. 180s gives the spinner enough headroom not to cut off
	// mid-stream.
	if (Url.Contains(TEXT("/agent/explain")))
	{
		return 180.0f;
	}
	// [AGENT-STRIP-END]
	// 90s covers full-project scans.
	return 90.0f;
}

void FShintHttpClient::Send(
	const FString& FullUrl,
	EShintHttpMethod Method,
	const FString& Body,
	FOnShintHttpComplete OnComplete,
	const TMap<FString, FString>& ExtraHeaders)
{
	UE_LOG(LogShintTools, Verbose, TEXT("FShintHttpClient: %s %s"),
		*MethodToString(Method), *FullUrl);

	FHttpModule& Http = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();

	Req->SetURL(FullUrl);
	Req->SetVerb(MethodToString(Method));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"),       TEXT("application/json"));
	Req->SetHeader(TEXT("User-Agent"),   UserAgent);

	for (const auto& KV : ExtraHeaders)
		Req->SetHeader(KV.Key, KV.Value);

	if (!Body.IsEmpty() &&
	    (Method == EShintHttpMethod::POST || Method == EShintHttpMethod::PUT))
	{
		Req->SetContentAsString(Body);
	}

	// BindSP via AsShared keeps `this` alive through the dispatch — safe if
	// the owning FShintCoreClient is torn down mid-flight (uncommon but
	// possible during editor reload).
	Req->OnProcessRequestComplete().BindSP(
		AsShared(),
		&FShintHttpClient::HandleResponse,
		OnComplete);
	const float RequestTimeout = TimeoutForUrl(FullUrl);
	Req->SetTimeout(RequestTimeout);
	// CRITICAL for /agent/explain: SetTimeout bounds the TOTAL request, but UE's
	// HTTP backend also enforces a separate ACTIVITY timeout (no bytes sent or
	// received) that defaults to ~30s. The synchronous /agent/explain endpoint
	// streams nothing — it holds the connection silent for the full 30-45s (60-90s
	// on slow CPUs) CPU generation, then sends the whole body at once. With the
	// 30s default, that silent gap tripped the activity abort at ~30s and the
	// plugin reported "Could not reach the LLM" even though the core returned a
	// valid 200 (short <30s generations slipped under it, which is why it looked
	// intermittent). Match the activity timeout to the total so a long, quiet
	// generation is never mistaken for a dead connection. The per-request setter
	// only exists from 5.4 up; on 5.2/5.3 ShintCompat::SetActivityTimeout is a
	// no-op and the engine-wide [HTTP] HttpActivityTimeout applies instead.
	ShintCompat::SetActivityTimeout(Req, RequestTimeout);

	if (!Req->ProcessRequest())
	{
		FShintHttpResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		OnComplete.ExecuteIfBound(Err);
	}
}

void FShintHttpClient::HandleResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	FOnShintHttpComplete OnComplete)
{
	FShintHttpResult Result;
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bSuccess = false;
		const FString TriedUrl = Request.IsValid() ? Request->GetURL() : FString();
		Result.ErrorMessage = FString::Printf(
			TEXT("Connection failed — could not reach Core Engine at %s."),
			*TriedUrl);
		OnComplete.ExecuteIfBound(Result);
		return;
	}
	Result.StatusCode   = Response->GetResponseCode();
	Result.ResponseBody = Response->GetContentAsString();
	Result.bSuccess     = (Result.StatusCode >= 200 && Result.StatusCode < 300);
	if (!Result.bSuccess)
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("HTTP %d: %s"),
			Result.StatusCode, *Result.ResponseBody);
	}
	OnComplete.ExecuteIfBound(Result);
}
