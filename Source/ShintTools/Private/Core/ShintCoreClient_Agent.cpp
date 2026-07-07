// Copyright 2026 ShintTools. All Rights Reserved.
//
// Agent endpoints (/agent/explain, /agent/plan) split out of
// ShintCoreClient.cpp. Both are Indie-tier; some builds do not surface the
// UI buttons that drive them, but the symbols stay in the same module so
// header dependencies don't fork between tiers.

#include "ShintCoreClient.h"
#include "ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// LLM pivot — POST /agent/explain
//
// One issue in, one explanation out. The SSE streaming path was removed
// because the 1.3B model could not reliably emit the JSON tool-call
// protocol it required. The wait is now a single 30-45s spinner on the
// panel side; latency lives entirely on the server.
//
// The Issue we forward is the SAME object the panel received from
// /validate/* — server-enriched with rule_name + rule_explanation. We do
// not synthesise either field on the client; if the deployed core is
// older and did not enrich, the LLM falls back to whatever fields are
// present (RuleId, Message, Snippet) and the explanation quality drops
// gracefully.
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::RequestExplainIssue(
	const FShintCodeIssue&       Issue,
	FOnShintAgentExplainComplete OnComplete)
{
	TSharedRef<FJsonObject> IssueJson = MakeShared<FJsonObject>();
	IssueJson->SetStringField(TEXT("rule_id"),          Issue.RuleId);
	IssueJson->SetStringField(TEXT("rule_name"),        Issue.RuleName);
	IssueJson->SetStringField(TEXT("rule_explanation"), Issue.RuleExplanation);
	IssueJson->SetStringField(TEXT("severity"),         Issue.Severity);
	IssueJson->SetStringField(TEXT("category"),         Issue.Category);
	IssueJson->SetStringField(TEXT("file_path"),        Issue.FilePath);
	IssueJson->SetNumberField(TEXT("line"),             Issue.Line);
	IssueJson->SetStringField(TEXT("message"),          Issue.Message);
	IssueJson->SetStringField(TEXT("fix_suggestion"),   Issue.FixSuggestion);
	IssueJson->SetStringField(TEXT("snippet"),          Issue.Snippet);
	IssueJson->SetBoolField  (TEXT("is_auto_fixable"),  Issue.bIsAutoFixable);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	// /agent/explain (Indie tier) routes through the local license
	// check, same as /agent/plan — use ApiKeyMongo, not the dashboard key.
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetObjectField(TEXT("issue"),   IssueJson);

	const FString Url = Config.GetBaseUrl() / TEXT("agent/explain");

	UE_LOG(LogShintTools, Verbose,
		TEXT("RequestExplainIssue: POST /agent/explain rule=%s line=%d"),
		*Issue.RuleId, Issue.Line);

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAgentExplainResponse R;

			if (!Raw.bSuccess)
			{
				R.bSuccess = false;
				if (Raw.StatusCode == 403)
				{
					R.Tier         = TEXT("free");
					R.ErrorMessage = TEXT(
						"Issue Explain requires the Indie tier. The connected core "
						"resolved your api_key as 'free'. Upgrade your subscription "
						"at https://shint.tools to enable LLM explanations.");
				}
				else if (Raw.StatusCode == 404)
				{
					R.ErrorMessage = TEXT(
						"/agent/explain is not exposed by this core build. Update the "
						"core engine to a release that ships the LLM explainer.");
				}
				else if (Raw.StatusCode > 0)
				{
					R.ErrorMessage = FString::Printf(
						TEXT("HTTP %d on /agent/explain."), Raw.StatusCode);
				}
				else
				{
					R.ErrorMessage = TEXT("Network error reaching /agent/explain.");
				}
				if (OnComplete.IsBound()) OnComplete.Execute(R);
				return;
			}

			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				R.bSuccess     = false;
				R.ErrorMessage = TEXT("Could not parse the response from /agent/explain.");
				if (OnComplete.IsBound()) OnComplete.Execute(R);
				return;
			}

			Root->TryGetBoolField  (TEXT("success"),            R.bSuccess);
			Root->TryGetStringField(TEXT("explanation"),        R.Explanation);
			double Gen = 0.0;
			if (Root->TryGetNumberField(TEXT("generation_seconds"), Gen))
				R.GenerationSeconds = static_cast<float>(Gen);
			Root->TryGetStringField(TEXT("tier"),               R.Tier);
			Root->TryGetStringField(TEXT("error_message"),      R.ErrorMessage);

			if (OnComplete.IsBound()) OnComplete.Execute(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// LLM pivot (streaming) — POST /agent/explain/stream
//
// Same request contract as RequestExplainIssue, but the response is an SSE
// stream of {"chunk"} events. OnChunk fires per token on the game thread so the
// modal fills in live; OnComplete fires once at stream end with the full text.
// Streaming keeps the socket active token-by-token, so it never trips the HTTP
// activity timeout the way the silent synchronous call did.
// ─────────────────────────────────────────────────────────────────────────────
void FShintCoreClient::RequestExplainIssueStream(
	const FShintCodeIssue&       Issue,
	FOnShintStreamChunk          OnChunk,
	FOnShintAgentExplainComplete OnComplete)
{
	TSharedRef<FJsonObject> IssueJson = MakeShared<FJsonObject>();
	IssueJson->SetStringField(TEXT("rule_id"),          Issue.RuleId);
	IssueJson->SetStringField(TEXT("rule_name"),        Issue.RuleName);
	IssueJson->SetStringField(TEXT("rule_explanation"), Issue.RuleExplanation);
	IssueJson->SetStringField(TEXT("severity"),         Issue.Severity);
	IssueJson->SetStringField(TEXT("category"),         Issue.Category);
	IssueJson->SetStringField(TEXT("file_path"),        Issue.FilePath);
	IssueJson->SetNumberField(TEXT("line"),             Issue.Line);
	IssueJson->SetStringField(TEXT("message"),          Issue.Message);
	IssueJson->SetStringField(TEXT("fix_suggestion"),   Issue.FixSuggestion);
	IssueJson->SetStringField(TEXT("snippet"),          Issue.Snippet);
	IssueJson->SetBoolField  (TEXT("is_auto_fixable"),  Issue.bIsAutoFixable);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetObjectField(TEXT("issue"),   IssueJson);

	const FString Url        = Config.GetBaseUrl() / TEXT("agent/explain/stream");
	const double  StartedAt  = FPlatformTime::Seconds();

	UE_LOG(LogShintTools, Verbose,
		TEXT("RequestExplainIssueStream: POST /agent/explain/stream rule=%s line=%d"),
		*Issue.RuleId, Issue.Line);

	SendRequestStream(Url, EShintHttpMethod::POST, SerializeJson(Body), OnChunk,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, StartedAt](const FShintRequestResult& Raw)
		{
			FShintAgentExplainResponse R;

			if (!Raw.bSuccess)
			{
				R.bSuccess = false;
				if (Raw.StatusCode == 403)
				{
					R.Tier         = TEXT("free");
					R.ErrorMessage = TEXT(
						"Issue Explain requires the Indie tier. The connected core "
						"resolved your api_key as 'free'. Upgrade your subscription "
						"at https://shint.tools to enable LLM explanations.");
				}
				else if (Raw.StatusCode == 404)
				{
					R.ErrorMessage = TEXT(
						"/agent/explain/stream is not exposed by this core build. "
						"Update the core engine to a release that ships the LLM explainer.");
				}
				else if (Raw.StatusCode > 0)
				{
					R.ErrorMessage = Raw.ErrorMessage.IsEmpty()
						? FString::Printf(TEXT("HTTP %d on /agent/explain/stream."), Raw.StatusCode)
						: Raw.ErrorMessage;
				}
				else
				{
					R.ErrorMessage = Raw.ErrorMessage.IsEmpty()
						? FString(TEXT("Network error reaching /agent/explain/stream."))
						: Raw.ErrorMessage;
				}
				if (OnComplete.IsBound()) OnComplete.Execute(R);
				return;
			}

			// 2xx — either a real explanation or an SSE {"error"} the parser caught.
			if (!Raw.ErrorMessage.IsEmpty() && Raw.ResponseBody.IsEmpty())
			{
				R.bSuccess     = false;
				R.ErrorMessage = Raw.ErrorMessage;
			}
			else
			{
				R.bSuccess          = !Raw.ResponseBody.IsEmpty();
				R.Explanation       = Raw.ResponseBody;
				R.GenerationSeconds = static_cast<float>(FPlatformTime::Seconds() - StartedAt);
				if (!R.bSuccess)
					R.ErrorMessage = TEXT("Stream ended with no text.");
			}
			if (OnComplete.IsBound()) OnComplete.Execute(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Agent — Auto-Fix Plan
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::RequestAgentPlan(
	const FShintValidateResult& Source, FOnShintAgentPlanComplete OnComplete)
{
	// T6 — Defensive payload sanitisation. The /agent/plan Pydantic schema
	// requires `rule_id` and `severity` (non-empty strings) and a non-negative
	// `line`. Sending issues that fail validation returns 422 with a nested
	// detail array. Filter the bad apples here so the request only carries
	// valid payload, and log how many we dropped for diagnostics.
	TArray<TSharedPtr<FJsonValue>> IssArr;
	IssArr.Reserve(Source.Issues.Num());
	int32 SkippedEmpty = 0;
	for (const FShintCodeIssue& I : Source.Issues)
	{
		if (I.RuleId.IsEmpty() || I.Severity.IsEmpty())
		{
			++SkippedEmpty;
			continue;
		}
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("rule_id"),         I.RuleId);
		O->SetStringField(TEXT("severity"),        I.Severity);
		O->SetStringField(TEXT("category"),        I.Category);
		O->SetStringField(TEXT("file_path"),       I.FilePath);
		O->SetNumberField(TEXT("line"),            FMath::Max(0, I.Line));
		O->SetStringField(TEXT("message"),         I.Message);
		O->SetStringField(TEXT("fix_suggestion"),  I.FixSuggestion);
		O->SetBoolField  (TEXT("is_auto_fixable"), I.bIsAutoFixable);
		IssArr.Add(MakeShared<FJsonValueObject>(O));
	}
	if (SkippedEmpty > 0)
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("RequestAgentPlan: dropped %d issue(s) with empty rule_id or severity"),
			SkippedEmpty);
	}
	UE_LOG(LogShintTools, Verbose,
		TEXT("RequestAgentPlan: sending %d issue(s) to /agent/plan"), IssArr.Num());

	if (Config.ApiKeyMongo.IsEmpty())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("RequestAgentPlan: api_key is empty in shinttools.config.json — "
			     "the server will resolve an empty key as 'free' tier and return 403."));
		FShintAgentPlanResult EarlyErr;
		EarlyErr.bSuccess     = false;
		EarlyErr.ErrorMessage = TEXT(
			"api_key is not set up. Add it on shinttools.config.json under label "
			"\"api_key\" and restart editor.");
		OnComplete.ExecuteIfBound(EarlyErr);
		return;
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetArrayField (TEXT("issues"),  IssArr);

	const FString Url = Config.GetBaseUrl() / TEXT("agent/plan");

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			OnComplete.ExecuteIfBound(ParseAgentPlanResponse(Raw));
		}));
}

FShintAgentPlanResult FShintCoreClient::ParseAgentPlanResponse(
	const FShintRequestResult& Raw)
{
	FShintAgentPlanResult R;
	if (!Raw.bSuccess)
	{
		R.bSuccess = false;
		// T2 — A 404 here means the connected core engine doesn't expose the
		// agent router. Some core builds do not include it.
		// Translate the HTTP status
		// into a user-meaningful message instead of leaking "404 Not Found".
		if (Raw.StatusCode == 404)
		{
			R.ErrorMessage = TEXT(
				"Auto-Fix Plan requires the Indie core engine. "
				"The connected server (free tier) doesn't expose /agent/plan — "
				"upgrade your subscription at https://shint.tools to enable it.");
		}
		else if (Raw.StatusCode == 403)
		{
			R.ErrorMessage = TEXT(
				"Auto-Fix Plan is an Indie-tier feature. "
				"The connected core engine resolved your api_key as tier 'free'. "
				"Verify that api_key is set correctly on shinttools.config.json, that "
				"your license is active, and restart the core engine.");
		}
		else
		{
			R.ErrorMessage = Raw.ErrorMessage.IsEmpty()
				? FString::Printf(TEXT("HTTP %d on /agent/plan"), Raw.StatusCode)
				: Raw.ErrorMessage;
		}
		return R;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		R.bSuccess     = false;
		R.ErrorMessage = TEXT("Malformed JSON in /agent/plan response");
		return R;
	}

	bool bServerOk = false;
	Root->TryGetBoolField(TEXT("success"), bServerOk);
	Root->TryGetStringField(TEXT("tier"),    R.Tier);
	Root->TryGetStringField(TEXT("summary"), R.Summary);

	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("steps"), StepsArr) && StepsArr)
	{
		R.Steps.Reserve(StepsArr->Num());
		for (const TSharedPtr<FJsonValue>& V : *StepsArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;

			FShintAgentPlanStep S;
			(*O)->TryGetNumberField(TEXT("order"),           S.Order);
			(*O)->TryGetStringField(TEXT("rule_id"),         S.RuleId);
			(*O)->TryGetStringField(TEXT("file_path"),       S.FilePath);
			(*O)->TryGetNumberField(TEXT("line"),            S.Line);
			(*O)->TryGetStringField(TEXT("severity"),        S.Severity);
			(*O)->TryGetStringField(TEXT("priority"),        S.Priority);
			(*O)->TryGetStringField(TEXT("rationale"),       S.Rationale);
			(*O)->TryGetBoolField  (TEXT("is_auto_fixable"), S.bIsAutoFixable);
			R.Steps.Add(MoveTemp(S));
		}
	}

	R.bSuccess = bServerOk;
	if (!R.bSuccess && R.ErrorMessage.IsEmpty())
	{
		// T6 — FastAPI sends two shapes when /agent/plan rejects a request:
		//   • HTTPException → {"detail": "Auto-Fix Plan is an Indie-tier feature."}
		//   • Pydantic 422 → {"detail": [{"loc":[...], "msg":"...", "type":"..."}]}
		if (Root->TryGetStringField(TEXT("detail"), R.ErrorMessage)
		    && !R.ErrorMessage.IsEmpty())
		{
			// String detail — already in R.ErrorMessage.
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* DetailArr = nullptr;
			if (Root->TryGetArrayField(TEXT("detail"), DetailArr) && DetailArr)
			{
				TArray<FString> Msgs;
				Msgs.Reserve(DetailArr->Num());
				for (const TSharedPtr<FJsonValue>& V : *DetailArr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;
					FString Msg, Loc;
					(*O)->TryGetStringField(TEXT("msg"), Msg);
					const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
					if ((*O)->TryGetArrayField(TEXT("loc"), LocArr) && LocArr && LocArr->Num() > 0)
					{
						TArray<FString> Parts;
						for (const TSharedPtr<FJsonValue>& LV : *LocArr)
						{
							FString S;
							if (LV->TryGetString(S)) Parts.Add(S);
						}
						Loc = FString::Join(Parts, TEXT("."));
					}
					Msgs.Add(Loc.IsEmpty() ? Msg : FString::Printf(TEXT("%s: %s"), *Loc, *Msg));
				}
				R.ErrorMessage = FString::Join(Msgs, TEXT(" | "));
			}
		}
		if (R.ErrorMessage.IsEmpty())
		{
			R.ErrorMessage = TEXT("Agent plan request rejected by server");
		}
		UE_LOG(LogShintTools, Warning,
			TEXT("ParseAgentPlanResponse: server rejected request — %s | raw: %s"),
			*R.ErrorMessage, *Raw.ResponseBody.Left(800));
	}
	return R;
}
