// Copyright ShintTools. All Rights Reserved.

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
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

FShintCoreClient::FShintCoreClient()
{
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Initializing HTTP client."));
	LoadConfig();
}

FShintCoreClient::~FShintCoreClient()
{
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Destroying HTTP client."));
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

bool FShintCoreClient::LoadConfig()
{
	// Reset to defaults first so we always have a usable config
	Config = FShintCoreConfig();

	const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));

	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Looking for config at: %s"), *ConfigPath);

	if (!FPaths::FileExists(ConfigPath))
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("ShintCoreClient: Config file not found. Using defaults (port=%d, auto_start=%s)."),
			Config.CorePort, Config.bAutoStartCore ? TEXT("true") : TEXT("false"));
		return false;
	}

	FString RawJson;
	if (!FFileHelper::LoadFileToString(RawJson, *ConfigPath))
	{
		UE_LOG(LogShintTools, Error,
			TEXT("ShintCoreClient: Failed to read config file: %s"), *ConfigPath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("ShintCoreClient: Failed to parse config JSON. Content: %s"), *RawJson);
		return false;
	}

	// Read "core_port"
	int32 ParsedPort = 0;
	if (JsonObject->TryGetNumberField(TEXT("core_port"), ParsedPort) && ParsedPort > 0)
	{
		Config.CorePort = ParsedPort;
	}

	// Read "auto_start_core"
	bool bAutoStart = false;
	if (JsonObject->TryGetBoolField(TEXT("auto_start_core"), bAutoStart))
	{
		Config.bAutoStartCore = bAutoStart;
	}

	UE_LOG(LogShintTools, Log,
		TEXT("ShintCoreClient: Config loaded. Port=%d, AutoStart=%s"),
		Config.CorePort, Config.bAutoStartCore ? TEXT("true") : TEXT("false"));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connectivity
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::CheckHealth(FOnShintRequestComplete OnComplete)
{
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Sending health check to Core Engine."));
	SendRequest(TEXT("/health"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

void FShintCoreClient::Ping(FOnShintRequestComplete OnComplete)
{
	UE_LOG(LogShintTools, Log, TEXT("ShintCoreClient: Pinging Core Engine."));
	SendRequest(TEXT("/ping"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::ValidateCode(
	const FString& FilePath,
	const FString& Content,
	const FString& Engine,
	FOnShintValidateComplete OnComplete)
{
	UE_LOG(LogShintTools, Log,
		TEXT("ShintCoreClient: Sending POST /validate/code for file: %s"), *FilePath);

	// ── Build JSON payload ────────────────────────────────────────────────────
	//
	//   { "file_path": "PlayerController.cpp",
	//     "content":   "// full source …",
	//     "engine":    "unreal" }
	//
	TSharedRef<FJsonObject> BodyObj = MakeShared<FJsonObject>();
	BodyObj->SetStringField(TEXT("file_path"), FilePath);
	BodyObj->SetStringField(TEXT("content"),   Content);
	BodyObj->SetStringField(TEXT("engine"),    Engine);

	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(BodyObj, Writer);

	// ── Dispatch and parse ────────────────────────────────────────────────────
	// Capture OnComplete by value so the lambda owns it safely across the async gap.
	SendRequest(TEXT("/validate/code"), EShintHttpMethod::POST, BodyString,
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& RawResult) mutable
			{
				FShintValidateResult Result;
				Result.StatusCode = RawResult.StatusCode;

				if (!RawResult.bSuccess)
				{
					Result.bSuccess     = false;
					Result.ErrorMessage = RawResult.ErrorMessage;
					OnComplete.ExecuteIfBound(Result);
					return;
				}

				// Parse the JSON response
				//
				//  {
				//    "summary": { "total": 0, "errors": 0, "warnings": 0 },
				//    "issues":  [ { "rule_id":"…", "severity":"…",
				//                   "message":"…", "line": 42 } ]
				//  }
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> JsonReader =
					TJsonReaderFactory<>::Create(RawResult.ResponseBody);

				if (!FJsonSerializer::Deserialize(JsonReader, JsonObj) || !JsonObj.IsValid())
				{
					Result.bSuccess     = false;
					Result.ErrorMessage = FString::Printf(
						TEXT("Failed to parse response JSON: %s"), *RawResult.ResponseBody);
					OnComplete.ExecuteIfBound(Result);
					return;
				}

				Result.bSuccess = true;

				// summary block
				const TSharedPtr<FJsonObject>* SummaryObj = nullptr;
				if (JsonObj->TryGetObjectField(TEXT("summary"), SummaryObj) && SummaryObj)
				{
					(*SummaryObj)->TryGetNumberField(TEXT("total"),    Result.TotalIssues);
					(*SummaryObj)->TryGetNumberField(TEXT("errors"),   Result.TotalErrors);
					(*SummaryObj)->TryGetNumberField(TEXT("warnings"), Result.TotalWarnings);
				}

				// issues array
				const TArray<TSharedPtr<FJsonValue>>* IssuesArr = nullptr;
				if (JsonObj->TryGetArrayField(TEXT("issues"), IssuesArr) && IssuesArr)
				{
					for (const TSharedPtr<FJsonValue>& IssueVal : *IssuesArr)
					{
						if (!IssueVal.IsValid()) continue;

						const TSharedPtr<FJsonObject>* IssueObj = nullptr;
						if (!IssueVal->TryGetObject(IssueObj) || !IssueObj) continue;

						FShintCodeIssue Issue;
						(*IssueObj)->TryGetStringField(TEXT("rule_id"),  Issue.RuleId);
						(*IssueObj)->TryGetStringField(TEXT("severity"), Issue.Severity);
						(*IssueObj)->TryGetStringField(TEXT("message"),  Issue.Message);
						(*IssueObj)->TryGetNumberField(TEXT("line"),     Issue.Line);

						Result.Issues.Add(MoveTemp(Issue));
					}
				}

				OnComplete.ExecuteIfBound(Result);
			}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic Request
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::SendRequest(
	const FString& Endpoint,
	EShintHttpMethod Method,
	const FString& Body,
	FOnShintRequestComplete OnComplete)
{
	const FString FullUrl   = Config.GetBaseUrl() + Endpoint;
	const FString MethodStr = MethodToString(Method);

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: %s %s"), *MethodStr, *FullUrl);

	// FIX: FHttpModule::Get() returns a reference, not a pointer.
	// The original code took the address and then tested for null — a check
	// that can never fire and generates a warning on some UE5 toolchains.
	FHttpModule& HttpModule = FHttpModule::Get();

	// Build the request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule.CreateRequest();

	HttpRequest->SetURL(FullUrl);
	HttpRequest->SetVerb(MethodStr);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"),        TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("User-Agent"),    TEXT("ShintTools-UE5-Plugin/1.0"));

	// Attach body for methods that support it
	if (!Body.IsEmpty() && (Method == EShintHttpMethod::POST || Method == EShintHttpMethod::PUT))
	{
		HttpRequest->SetContentAsString(Body);
	}

	// Bind the completion callback
	HttpRequest->OnProcessRequestComplete().BindRaw(
		this,
		&FShintCoreClient::OnHttpRequestComplete,
		OnComplete);

	// Reasonable timeout (5 seconds)
	HttpRequest->SetTimeout(5.0f);

	if (!HttpRequest->ProcessRequest())
	{
		UE_LOG(LogShintTools, Error,
			TEXT("ShintCoreClient: Failed to dispatch HTTP request to %s"), *FullUrl);

		FShintRequestResult ErrorResult;
		ErrorResult.bSuccess     = false;
		ErrorResult.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		OnComplete.ExecuteIfBound(ErrorResult);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Callback
// ─────────────────────────────────────────────────────────────────────────────

void FShintCoreClient::OnHttpRequestComplete(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	FOnShintRequestComplete OnComplete)
{
	FShintRequestResult Result;

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bSuccess      = false;
		Result.StatusCode    = 0;
		Result.ErrorMessage  = TEXT("Connection failed. Core Engine may not be running.");

		UE_LOG(LogShintTools, Warning,
			TEXT("ShintCoreClient: Request failed — %s"), *Result.ErrorMessage);

		OnComplete.ExecuteIfBound(Result);
		return;
	}

	Result.StatusCode  = Response->GetResponseCode();
	Result.ResponseBody = Response->GetContentAsString();

	// Treat 2xx codes as success
	Result.bSuccess = (Result.StatusCode >= 200 && Result.StatusCode < 300);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ShintCoreClient: Request succeeded. Status=%d Body=%s"),
			Result.StatusCode, *Result.ResponseBody);
	}
	else
	{
		Result.ErrorMessage = FString::Printf(
			TEXT("HTTP %d: %s"), Result.StatusCode, *Result.ResponseBody);

		UE_LOG(LogShintTools, Warning,
			TEXT("ShintCoreClient: Request failed. Status=%d Body=%s"),
			Result.StatusCode, *Result.ResponseBody);
	}

	OnComplete.ExecuteIfBound(Result);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

FString FShintCoreClient::MethodToString(EShintHttpMethod Method)
{
	switch (Method)
	{
	case EShintHttpMethod::GET:     return TEXT("GET");
	case EShintHttpMethod::POST:    return TEXT("POST");
	case EShintHttpMethod::PUT:     return TEXT("PUT");
	case EShintHttpMethod::DELETE_: return TEXT("DELETE");
	default:
		UE_LOG(LogShintTools, Error, TEXT("ShintCoreClient: Unknown HTTP method enum value."));
		return TEXT("GET");
	}
}
