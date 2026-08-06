// Copyright 2026 ShintTools. All Rights Reserved.
//
// Assistant endpoints (/assistant/*) — client contract v1.1.
//
// Split out of ShintCoreClient.cpp for the same reason the agent and LOD
// endpoints are: one TU per module surface. Unlike those two this file is
// NEVER strip-gated — the assistant router ships in the free image as well,
// and Free is expected to get a working (two-intent, memory-less) assistant.
// Feature gating happens per-intent, server-side, and reaches the UI through
// GetAssistantCapabilities — there is no tier table on this side.

#include "ShintCoreClient.h"
#include "ShintTools.h"

#include "GenericPlatform/GenericPlatformHttp.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	// ── Shared parsing helpers ───────────────────────────────────────────────

	TSharedPtr<FJsonObject> ParseRoot(const FString& Body)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			return nullptr;
		return Root;
	}

	FShintAssistantTurn ParseTurn(const TSharedPtr<FJsonObject>& O)
	{
		FShintAssistantTurn T;
		if (!O.IsValid()) return T;
		O->TryGetStringField(TEXT("turn_id"),     T.TurnId);
		O->TryGetStringField(TEXT("role"),        T.Role);
		O->TryGetStringField(TEXT("intent"),      T.Intent);
		O->TryGetStringField(TEXT("raw_text"),    T.RawText);
		O->TryGetStringField(TEXT("context_ref"), T.ContextRef);
		O->TryGetStringField(TEXT("rule_id"),     T.RuleId);
		O->TryGetStringField(TEXT("asset_path"),  T.AssetPath);
		return T;
	}

	// FastAPI raises HTTPException with either a plain string detail or the
	// structured object the assistant contract documents for a 403. Both shapes
	// carry the message we want on screen; the structured one also tells the UI
	// which intents this tier CAN run, so a gated click can explain itself
	// instead of just failing.
	void ApplyErrorDetail(const FString& Body, FShintAssistantResponse& R)
	{
		TSharedPtr<FJsonObject> Root = ParseRoot(Body);
		if (!Root.IsValid()) return;

		if (Root->TryGetStringField(TEXT("detail"), R.ErrorMessage))
			return;

		const TSharedPtr<FJsonObject>* Detail = nullptr;
		if (!Root->TryGetObjectField(TEXT("detail"), Detail) || !Detail || !Detail->IsValid())
			return;

		(*Detail)->TryGetStringField(TEXT("error"),        R.ErrorMessage);
		(*Detail)->TryGetStringField(TEXT("current_tier"), R.CurrentTier);

		const TArray<TSharedPtr<FJsonValue>>* Allowed = nullptr;
		if ((*Detail)->TryGetArrayField(TEXT("allowed_intents"), Allowed) && Allowed)
		{
			for (const TSharedPtr<FJsonValue>& V : *Allowed)
			{
				FString S;
				if (V->TryGetString(S)) R.AllowedIntents.Add(S);
			}
		}
	}

	// Every /assistant/* failure funnels through here so the panel gets one
	// consistent, user-facing sentence instead of a raw status code. The
	// contract guarantees the reply is always displayable as-is; this keeps
	// that promise on the transport failures the server never sees.
	void ApplyTransportError(const FShintRequestResult& Raw, const TCHAR* Route,
	                         FShintAssistantResponse& R)
	{
		R.bSuccess   = false;
		R.StatusCode = Raw.StatusCode;

		ApplyErrorDetail(Raw.ResponseBody, R);
		if (!R.ErrorMessage.IsEmpty()) return;

		if (Raw.StatusCode == 404)
		{
			R.ErrorMessage = FString::Printf(
				TEXT("This conversation is no longer available (it expires after "
				     "12 hours idle). Send another message to start a new one. [%s]"),
				Route);
		}
		else if (Raw.StatusCode > 0)
		{
			R.ErrorMessage = FString::Printf(TEXT("HTTP %d on %s."),
			                                 Raw.StatusCode, Route);
		}
		else
		{
			R.ErrorMessage = FString::Printf(
				TEXT("Could not reach the local Core Engine on %s. Check that the "
				     "shinttools-core container is running."), Route);
		}
	}

	FString QueryEscape(const FString& In)
	{
		return FGenericPlatformHttp::UrlEncode(In);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Request body — one builder for both the blocking and streaming turn.
//
// Only non-empty fields are written: the server treats an absent field and an
// empty one differently for the grounding inheritance (§2 of the contract, an
// explicit value always overrides what would have been inherited), so sending
// "" would suppress inheritance rather than allow it.
// ─────────────────────────────────────────────────────────────────────────────
static TSharedRef<FJsonObject> BuildAssistantBody(
	const FShintAssistantRequest& Req, const FString& ApiKey)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), ApiKey);
	Body->SetStringField(TEXT("message"), Req.Message);

	auto SetIf = [&Body](const TCHAR* Field, const FString& Value)
	{
		if (!Value.IsEmpty()) Body->SetStringField(Field, Value);
	};

	SetIf(TEXT("conversation_id"),  Req.ConversationId);
	SetIf(TEXT("intent"),           Req.Intent);
	SetIf(TEXT("context_ref"),      Req.ContextRef);
	SetIf(TEXT("rule_id"),          Req.RuleId);
	SetIf(TEXT("asset_path"),       Req.AssetPath);
	SetIf(TEXT("report_id"),        Req.ReportId);
	SetIf(TEXT("platform_profile"), Req.PlatformProfile);
	SetIf(TEXT("studio_id"),        Req.StudioId);
	SetIf(TEXT("project_id"),       Req.ProjectId);
	SetIf(TEXT("module_context"),   Req.ModuleContext);

	Body->SetStringField(TEXT("engine"), TEXT("unreal"));

	if (Req.SelectedItemIds.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Ids;
		Ids.Reserve(Req.SelectedItemIds.Num());
		for (const FString& Id : Req.SelectedItemIds)
			Ids.Add(MakeShared<FJsonValueString>(Id));
		Body->SetArrayField(TEXT("selected_item_ids"), Ids);
	}

	return Body;
}

static FShintAssistantResponse ParseAssistantResponse(const FShintRequestResult& Raw)
{
	FShintAssistantResponse R;

	if (!Raw.bSuccess)
	{
		ApplyTransportError(Raw, TEXT("/assistant/message"), R);
		return R;
	}

	TSharedPtr<FJsonObject> Root = ParseRoot(Raw.ResponseBody);
	if (!Root.IsValid())
	{
		R.bSuccess     = false;
		R.ErrorMessage = TEXT("Could not parse the response from /assistant/message.");
		return R;
	}

	// The contract puts a non-empty top-level `error` on a 200 when the turn
	// itself failed (as opposed to the request being rejected). Surface it as
	// the reply rather than inventing our own wording — §1 "honest degradation"
	// says the server's text is always suitable to display.
	FString ServerError;
	Root->TryGetStringField(TEXT("error"), ServerError);

	Root->TryGetStringField(TEXT("conversation_id"), R.ConversationId);
	Root->TryGetStringField(TEXT("tier"),            R.Tier);
	Root->TryGetStringField(TEXT("intent"),          R.Intent);
	Root->TryGetBoolField  (TEXT("continued"),       R.bContinued);
	Root->TryGetBoolField  (TEXT("degraded"),        R.bDegraded);

	const TSharedPtr<FJsonObject>* ReplyObj = nullptr;
	if (Root->TryGetObjectField(TEXT("reply"), ReplyObj) && ReplyObj)
		R.Reply = ParseTurn(*ReplyObj);

	if (!ServerError.IsEmpty() && R.Reply.RawText.IsEmpty())
	{
		R.bSuccess     = false;
		R.ErrorMessage = ServerError;
	}
	else
	{
		R.bSuccess = !R.Reply.RawText.IsEmpty();
		if (!R.bSuccess)
			R.ErrorMessage = TEXT("The assistant returned an empty reply.");
	}
	return R;
}

// ─────────────────────────────────────────────────────────────────────────────
// POST /assistant/message
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendAssistantMessage(
	const FShintAssistantRequest& Request, FOnShintAssistantComplete OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/message");

	UE_LOG(LogShintTools, Verbose,
		TEXT("SendAssistantMessage: intent=%s conv=%s ctx=%s"),
		Request.Intent.IsEmpty() ? TEXT("(router)") : *Request.Intent,
		Request.ConversationId.IsEmpty() ? TEXT("(new)") : *Request.ConversationId,
		*Request.ContextRef);

	SendRequest(Url, EShintHttpMethod::POST,
		SerializeJson(BuildAssistantBody(Request, Config.ApiKeyMongo)),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			OnComplete.ExecuteIfBound(ParseAssistantResponse(Raw));
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// POST /assistant/message/stream
//
// The SSE event schema is the one /agent/explain/stream already uses, so the
// existing parser handles it unchanged. What differs is the terminal event:
// it carries conversation_id / intent / continued / turn_id, which the panel
// needs to keep threading. SendRequestStream hands the accumulated text back
// in ResponseBody, so the metadata is recovered by re-reading the final
// `done` frame — which the transport also surfaces there.
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendAssistantMessageStream(
	const FShintAssistantRequest& Request,
	FOnShintStreamChunk           OnChunk,
	FOnShintAssistantComplete     OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/message/stream");

	// The turn's own identity is not knowable until the stream ends, but the
	// panel has to render *something* threaded immediately. Echo back what the
	// caller already knew so a failure before the first frame still lands in
	// the right conversation.
	const FString RequestedConversation = Request.ConversationId;
	const FString RequestedIntent       = Request.Intent;

	UE_LOG(LogShintTools, Verbose,
		TEXT("SendAssistantMessageStream: intent=%s conv=%s"),
		RequestedIntent.IsEmpty() ? TEXT("(router)") : *RequestedIntent,
		RequestedConversation.IsEmpty() ? TEXT("(new)") : *RequestedConversation);

	SendRequestStream(Url, EShintHttpMethod::POST,
		SerializeJson(BuildAssistantBody(Request, Config.ApiKeyMongo)), OnChunk,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete, RequestedConversation, RequestedIntent]
			(const FShintRequestResult& Raw)
		{
			FShintAssistantResponse R;
			R.ConversationId = RequestedConversation;
			R.Intent         = RequestedIntent;

			if (!Raw.bSuccess)
			{
				// 403/404 are raised before the stream opens, so they arrive
				// here exactly as they would on the blocking endpoint.
				ApplyTransportError(Raw, TEXT("/assistant/message/stream"), R);
				OnComplete.ExecuteIfBound(R);
				return;
			}

			// 2xx — the parser either accumulated text or caught an SSE error
			// frame. A mid-generation failure still delivers the grounded
			// deterministic fallback first, so text-with-an-error is a
			// degraded success, not a failure.
			if (!Raw.ResponseBody.IsEmpty())
			{
				R.bSuccess        = true;
				R.bDegraded       = !Raw.ErrorMessage.IsEmpty();
				R.Reply.RawText   = Raw.ResponseBody;
				R.Reply.Role      = TEXT("assistant");
				R.Reply.Intent    = RequestedIntent;
				R.ErrorMessage    = Raw.ErrorMessage;
			}
			else
			{
				R.bSuccess     = false;
				R.ErrorMessage = Raw.ErrorMessage.IsEmpty()
					? FString(TEXT("The assistant stream ended with no text."))
					: Raw.ErrorMessage;
			}
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// GET /assistant/capabilities
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::GetAssistantCapabilities(
	FOnShintAssistantCapabilities OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/capabilities")
		+ TEXT("?api_key=") + QueryEscape(Config.ApiKeyMongo);

	SendRequest(Url, EShintHttpMethod::GET, FString(),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantCapabilities C;

			if (!Raw.bSuccess)
			{
				// A core too old to serve this router is the common case here.
				// Fall back to the contract's Free floor rather than showing
				// nothing: the two free intents work on every published core.
				C.bSuccess     = false;
				C.Tier         = TEXT("free");
				C.Intents      = { TEXT("explain_finding"), TEXT("general_help") };
				C.Memory       = TEXT("none");
				C.ErrorMessage = Raw.StatusCode == 404
					? TEXT("This Core Engine build does not ship the assistant. "
					       "Update it to enable memory and studio rules.")
					: TEXT("Could not read assistant capabilities from the Core Engine.");
				OnComplete.ExecuteIfBound(C);
				return;
			}

			TSharedPtr<FJsonObject> Root = ParseRoot(Raw.ResponseBody);
			if (!Root.IsValid())
			{
				C.bSuccess     = false;
				C.ErrorMessage = TEXT("Could not parse /assistant/capabilities.");
				OnComplete.ExecuteIfBound(C);
				return;
			}

			Root->TryGetStringField(TEXT("tier"),          C.Tier);
			Root->TryGetStringField(TEXT("memory"),        C.Memory);
			Root->TryGetStringField(TEXT("model_profile"), C.ModelProfile);
			Root->TryGetStringField(TEXT("studio_rules"),  C.StudioRules);

			const TArray<TSharedPtr<FJsonValue>>* Intents = nullptr;
			if (Root->TryGetArrayField(TEXT("intents"), Intents) && Intents)
			{
				for (const TSharedPtr<FJsonValue>& V : *Intents)
				{
					FString S;
					if (V->TryGetString(S)) C.Intents.Add(S);
				}
			}

			C.bSuccess = true;
			UE_LOG(LogShintTools, Verbose,
				TEXT("Assistant capabilities: tier=%s intents=%d memory=%s"),
				*C.Tier, C.Intents.Num(), *C.Memory);
			OnComplete.ExecuteIfBound(C);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// GET /assistant/conversations/{id}
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::GetAssistantConversation(
	const FString& ConversationId, FOnShintAssistantThread OnComplete)
{
	if (ConversationId.IsEmpty())
	{
		OnComplete.ExecuteIfBound(TArray<FShintAssistantTurn>());
		return;
	}

	const FString Url = Config.GetBaseUrl()
		/ TEXT("assistant/conversations") / ConversationId
		+ TEXT("?api_key=") + QueryEscape(Config.ApiKeyMongo);

	SendRequest(Url, EShintHttpMethod::GET, FString(),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			TArray<FShintAssistantTurn> Turns;

			TSharedPtr<FJsonObject> Root =
				Raw.bSuccess ? ParseRoot(Raw.ResponseBody) : nullptr;
			if (Root.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (Root->TryGetArrayField(TEXT("turns"), Arr) && Arr)
				{
					Turns.Reserve(Arr->Num());
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						const TSharedPtr<FJsonObject>* O = nullptr;
						if (V->TryGetObject(O) && O)
							Turns.Add(ParseTurn(*O));
					}
				}
			}
			OnComplete.ExecuteIfBound(Turns);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Memory
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::GetAssistantMemory(
	const FString& StudioId, const FString& ProjectId,
	FOnShintAssistantMemory OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/memory")
		+ TEXT("?api_key=")    + QueryEscape(Config.ApiKeyMongo)
		+ TEXT("&studio_id=")  + QueryEscape(StudioId)
		+ TEXT("&project_id=") + QueryEscape(ProjectId);

	SendRequest(Url, EShintHttpMethod::GET, FString(),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantMemory M;

			if (!Raw.bSuccess)
			{
				M.ErrorMessage = Raw.StatusCode == 403
					? TEXT("Persistent memory is a Studio feature.")
					: TEXT("Could not read the assistant's memory from the Core Engine.");
				OnComplete.ExecuteIfBound(M);
				return;
			}

			TSharedPtr<FJsonObject> Root = ParseRoot(Raw.ResponseBody);
			if (!Root.IsValid())
			{
				M.ErrorMessage = TEXT("Could not parse /assistant/memory.");
				OnComplete.ExecuteIfBound(M);
				return;
			}

			Root->TryGetBoolField(TEXT("memory_muted"), M.bMuted);

			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(TEXT("facts"), Arr) && Arr)
			{
				M.Facts.Reserve(Arr->Num());
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;

					FShintAssistantFact F;
					(*O)->TryGetStringField(TEXT("fact_id"), F.FactId);
					(*O)->TryGetStringField(TEXT("type"),    F.Type);
					(*O)->TryGetStringField(TEXT("value"),   F.Value);
					(*O)->TryGetStringField(TEXT("status"),  F.Status);

					const TSharedPtr<FJsonObject>* Src = nullptr;
					if ((*O)->TryGetObjectField(TEXT("source"), Src) && Src && Src->IsValid())
					{
						(*Src)->TryGetStringField(TEXT("module"),          F.SourceModule);
						(*Src)->TryGetStringField(TEXT("conversation_id"), F.SourceConversationId);
					}
					M.Facts.Add(MoveTemp(F));
				}
			}

			M.bSuccess = true;
			OnComplete.ExecuteIfBound(M);
		}));
}

void FShintCoreClient::ConfirmAssistantFact(
	const FString& FactId, bool bAccept, FOnShintAssistantComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetStringField(TEXT("fact_id"), FactId);
	Body->SetBoolField  (TEXT("accept"),  bAccept);

	const FString Url = Config.GetBaseUrl() / TEXT("assistant/memory/confirm");

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantResponse R;
			R.bSuccess = Raw.bSuccess;
			if (!Raw.bSuccess)
				ApplyTransportError(Raw, TEXT("/assistant/memory/confirm"), R);
			OnComplete.ExecuteIfBound(R);
		}));
}

void FShintCoreClient::MuteAssistantMemory(
	const FString& StudioId, const FString& ProjectId, bool bMuted,
	FOnShintAssistantComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"),    Config.ApiKeyMongo);
	Body->SetStringField(TEXT("studio_id"),  StudioId);
	Body->SetStringField(TEXT("project_id"), ProjectId);
	Body->SetBoolField  (TEXT("muted"),      bMuted);

	const FString Url = Config.GetBaseUrl() / TEXT("assistant/memory/mute");

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantResponse R;
			R.bSuccess = Raw.bSuccess;
			if (!Raw.bSuccess)
				ApplyTransportError(Raw, TEXT("/assistant/memory/mute"), R);
			OnComplete.ExecuteIfBound(R);
		}));
}

void FShintCoreClient::PurgeAssistantProject(
	const FString& StudioId, const FString& ProjectId,
	FOnShintAssistantComplete OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/memory/project")
		+ TEXT("?api_key=")    + QueryEscape(Config.ApiKeyMongo)
		+ TEXT("&studio_id=")  + QueryEscape(StudioId)
		+ TEXT("&project_id=") + QueryEscape(ProjectId);

	SendRequest(Url, EShintHttpMethod::DELETE_, FString(),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantResponse R;
			R.bSuccess = Raw.bSuccess;
			if (!Raw.bSuccess)
				ApplyTransportError(Raw, TEXT("/assistant/memory/project"), R);
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Studio rules
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::GetAssistantRules(
	const FString& StudioId, const FString& ProjectId,
	FOnShintAssistantRules OnComplete)
{
	const FString Url = Config.GetBaseUrl() / TEXT("assistant/rules")
		+ TEXT("?api_key=")    + QueryEscape(Config.ApiKeyMongo)
		+ TEXT("&studio_id=")  + QueryEscape(StudioId)
		+ TEXT("&project_id=") + QueryEscape(ProjectId);

	SendRequest(Url, EShintHttpMethod::GET, FString(),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantRules Out;

			if (!Raw.bSuccess)
			{
				Out.ErrorMessage = Raw.StatusCode == 403
					? TEXT("Studio rules require the Indie tier or higher.")
					: TEXT("Could not read studio rules from the Core Engine.");
				OnComplete.ExecuteIfBound(Out);
				return;
			}

			TSharedPtr<FJsonObject> Root = ParseRoot(Raw.ResponseBody);
			if (!Root.IsValid())
			{
				Out.ErrorMessage = TEXT("Could not parse /assistant/rules.");
				OnComplete.ExecuteIfBound(Out);
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(TEXT("rules"), Arr) && Arr)
			{
				Out.Rules.Reserve(Arr->Num());
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;

					FShintAssistantRule Rule;
					(*O)->TryGetStringField(TEXT("rule_id"),     Rule.RuleId);
					(*O)->TryGetStringField(TEXT("name"),        Rule.Name);
					(*O)->TryGetStringField(TEXT("tier"),        Rule.Tier);
					(*O)->TryGetStringField(TEXT("status"),      Rule.Status);
					(*O)->TryGetStringField(TEXT("description"), Rule.Description);
					Out.Rules.Add(MoveTemp(Rule));
				}
			}

			Out.bSuccess = true;
			OnComplete.ExecuteIfBound(Out);
		}));
}

void FShintCoreClient::ConfirmAssistantRule(
	const FString& RuleId, bool bAccept, FOnShintAssistantComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), Config.ApiKeyMongo);
	Body->SetStringField(TEXT("rule_id"), RuleId);
	Body->SetBoolField  (TEXT("accept"),  bAccept);

	const FString Url = Config.GetBaseUrl() / TEXT("assistant/rules/confirm");

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantResponse R;
			R.bSuccess = Raw.bSuccess;
			if (!Raw.bSuccess)
				ApplyTransportError(Raw, TEXT("/assistant/rules/confirm"), R);
			OnComplete.ExecuteIfBound(R);
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// POST /assistant/decisions/check
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CheckAssistantDecisions(
	const FString& StudioId, const FString& ProjectId, const FString& ContextRef,
	FOnShintAssistantDecisions OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"),     Config.ApiKeyMongo);
	Body->SetStringField(TEXT("studio_id"),   StudioId);
	Body->SetStringField(TEXT("project_id"),  ProjectId);
	Body->SetStringField(TEXT("context_ref"), ContextRef);

	const FString Url = Config.GetBaseUrl() / TEXT("assistant/decisions/check");

	SendRequest(Url, EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw)
		{
			FShintAssistantDecisions D;

			// The detector errs toward silence and an empty array is the normal
			// case, so a failure here is never worth interrupting the user
			// over — it degrades to "no contradictions" and stays quiet.
			TSharedPtr<FJsonObject> Root =
				Raw.bSuccess ? ParseRoot(Raw.ResponseBody) : nullptr;
			if (!Root.IsValid())
			{
				OnComplete.ExecuteIfBound(D);
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(TEXT("contradictions"), Arr) && Arr)
			{
				D.Contradictions.Reserve(Arr->Num());
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;

					FShintAssistantContradiction C;
					(*O)->TryGetStringField(TEXT("fact_id"),    C.FactId);
					(*O)->TryGetStringField(TEXT("fact_value"), C.FactValue);
					(*O)->TryGetStringField(TEXT("rule_id"),    C.RuleId);
					(*O)->TryGetStringField(TEXT("asset_path"), C.AssetPath);
					(*O)->TryGetStringField(TEXT("nudge"),      C.Nudge);
					D.Contradictions.Add(MoveTemp(C));
				}
			}

			D.bSuccess = true;
			OnComplete.ExecuteIfBound(D);
		}));
}
