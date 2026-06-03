// Copyright ShintTools. All Rights Reserved.
//
// FShintCoreClient — Config + Transport core.
//
// Feature-specific implementations live in sibling .cpp files of the same
// module; UBT compiles every .cpp in the module, so member functions can
// be physically scattered without touching headers or callsites:
//
//   • Core/ShintCoreClient_Validator.cpp      — /validate/*  + tree-sitter fix
//   • Core/ShintCoreClient_QualityScore.cpp   — /metrics/score/*
//   • Core/ShintCoreClient_Asset.cpp          — /assets/*    + legacy dashboard
//   • Core/ShintCoreClient_Agent.cpp          — /agent/explain + /agent/plan
//   • Core/ShintDashboardSync.cpp             — external shint.tools POSTs
//
// What stays here:
//   * Construction / destruction
//   * LoadConfig / SaveConfig (the only persistent state on the client)
//   * /health and /ping (transport sanity, no feature payload)
//   * SendRequest + OnHttpRequestComplete (the HTTP backbone every
//     subsystem dispatches through — keeping it next to the lifetime
//     management keeps shared-ref semantics obvious)
//   * MethodToString / SerializeJson (pure helpers reused everywhere)

#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

FShintCoreClient::FShintCoreClient()  { LoadConfig(); }
FShintCoreClient::~FShintCoreClient() {}

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

bool FShintCoreClient::LoadConfig()
{
	Config = FShintCoreConfig();
	const FString CfgPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));
	if (!FPaths::FileExists(CfgPath)) return false;

	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *CfgPath)) return false;

	TSharedPtr<FJsonObject> Json;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Json) || !Json.IsValid()) return false;

	int32 Port = 0;
	if (Json->TryGetNumberField(TEXT("core_port"), Port) && Port > 0) Config.CorePort = Port;

	bool bAuto = false;
	if (Json->TryGetBoolField(TEXT("auto_start_core"), bAuto)) Config.bAutoStartCore = bAuto;

	FString S;
	if (Json->TryGetStringField(TEXT("core_host"),    S) && !S.IsEmpty()) Config.CoreHost = S;
	if (Json->TryGetStringField(TEXT("project_name"), S)) Config.ProjectName = S;
	// project_id was removed from the config schema in 1.7.11. The
	// dashboard's per-project API key (st_<hex>, ApiKeyDashboard)
	// identifies the project implicitly, and the local core only used
	// project_id for correlation logging. Old configs that still carry
	// the field are tolerated — we just don't read it back.
	if (Json->TryGetStringField(TEXT("api_key"),       S)) Config.ApiKeyDashboard = S;
	if (Json->TryGetStringField(TEXT("api_key_mongo"), S)) Config.ApiKeyMongo    = S;
	if (Json->TryGetStringField(TEXT("session_token"), S)) Config.SessionToken   = S;
	if (Json->TryGetStringField(TEXT("dashboard_url"), S)) Config.DashboardUrl   = S;

	// Migrate the dead "app.shinttools.io" host — launcher <= 1.7.9 wrote
	// it as the default and Cloudflare serves an HTML 404 there, which
	// the plugin used to surface as the cryptic "Only HTML requests are
	// supported here" error. The new default is the live production host.
	if (Config.DashboardUrl.IsEmpty()
		|| Config.DashboardUrl.Contains(TEXT("app.shinttools.io")))
	{
		Config.DashboardUrl = TEXT("https://shint.tools");
		UE_LOG(LogShintTools, Display,
			TEXT("ShintCoreClient: migrated dashboard_url to shint.tools"));
	}

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: Config loaded. Port=%d"), Config.CorePort);
	return true;
}

bool FShintCoreClient::SaveConfig() const
{
	const FString CfgPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));

	// Read the existing JSON so we preserve unknown fields (modules, naming, etc.)
	TSharedPtr<FJsonObject> Json;
	FString Raw;
	if (FFileHelper::LoadFileToString(Raw, *CfgPath))
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		FJsonSerializer::Deserialize(R, Json);
	}
	if (!Json.IsValid()) Json = MakeShared<FJsonObject>();

	// Overwrite config fields. project_id is intentionally NOT written —
	// removed from the schema in 1.7.11; see LoadConfig for the rationale.
	Json->SetStringField(TEXT("core_host"),       Config.CoreHost);
	Json->SetNumberField(TEXT("core_port"),       Config.CorePort);
	Json->SetBoolField(TEXT("auto_start_core"),   Config.bAutoStartCore);
	Json->SetStringField(TEXT("project_name"),    Config.ProjectName);
	Json->SetStringField(TEXT("api_key"),         Config.ApiKeyDashboard);
	Json->SetStringField(TEXT("api_key_mongo"),   Config.ApiKeyMongo);
	Json->SetStringField(TEXT("dashboard_url"),   Config.DashboardUrl);

	const FString Out = SerializeJson(Json.ToSharedRef());
	return FFileHelper::SaveStringToFile(Out, *CfgPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ─────────────────────────────────────────────────────────────────────────────
// Connectivity
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CheckHealth(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/health"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

void FShintCoreClient::Ping(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/ping"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic HTTP request
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendRequest(
	const FString& FullUrl, EShintHttpMethod Method,
	const FString& Body, FOnShintRequestComplete OnComplete,
	const TMap<FString, FString>& ExtraHeaders)
{
	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: %s %s"), *MethodToString(Method), *FullUrl);

	FHttpModule& Http = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();

	Req->SetURL(FullUrl);
	Req->SetVerb(MethodToString(Method));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"),       TEXT("application/json"));
	Req->SetHeader(TEXT("User-Agent"),   TEXT("ShintTools-UE5/1.1"));

	for (const auto& KV : ExtraHeaders)
		Req->SetHeader(KV.Key, KV.Value);

	if (!Body.IsEmpty() &&
	    (Method == EShintHttpMethod::POST || Method == EShintHttpMethod::PUT))
	{
		Req->SetContentAsString(Body);
	}

	// BindSP keeps FShintCoreClient alive via shared ref — safe if destroyed before response
	Req->OnProcessRequestComplete().BindSP(
		AsShared(), &FShintCoreClient::OnHttpRequestComplete, OnComplete);
	// 90s covers full-project scans; /agent/explain runs the local LLM and
	// takes 30-45s typical / 60-90s on slow CPUs — give it 180s so a single
	// slow generation doesn't cut the spinner off mid-stream.
	const bool bIsLlmCall = FullUrl.Contains(TEXT("/agent/explain"));
	Req->SetTimeout(bIsLlmCall ? 180.0f : 90.0f);

	if (!Req->ProcessRequest())
	{
		FShintRequestResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		OnComplete.ExecuteIfBound(Err);
	}
}

void FShintCoreClient::OnHttpRequestComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete)
{
	FShintRequestResult Result;
	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bSuccess = false;
		const FString TriedUrl = Request.IsValid() ? Request->GetURL() : FString();
		Result.ErrorMessage = FString::Printf(
			TEXT("Connection failed — could not reach Core Engine at %s. "
			     "Is start_engine.bat running? (uvicorn on 127.0.0.1:%d)"),
			*TriedUrl, Config.CorePort);
		OnComplete.ExecuteIfBound(Result); return;
	}
	Result.StatusCode   = Response->GetResponseCode();
	Result.ResponseBody = Response->GetContentAsString();
	Result.bSuccess     = (Result.StatusCode >= 200 && Result.StatusCode < 300);
	if (!Result.bSuccess)
		Result.ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"),
			Result.StatusCode, *Result.ResponseBody);
	OnComplete.ExecuteIfBound(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pure helpers — used by every subsystem
// ─────────────────────────────────────────────────────────────────────────────

FString FShintCoreClient::MethodToString(EShintHttpMethod Method)
{
	switch (Method)
	{
	case EShintHttpMethod::GET:     return TEXT("GET");
	case EShintHttpMethod::POST:    return TEXT("POST");
	case EShintHttpMethod::PUT:     return TEXT("PUT");
	case EShintHttpMethod::DELETE_: return TEXT("DELETE");
	default:                        return TEXT("GET");
	}
}

FString FShintCoreClient::SerializeJson(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj, W);
	return Out;
}
