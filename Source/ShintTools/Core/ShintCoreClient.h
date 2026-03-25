// Copyright ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"

/**
 * EShintHttpMethod
 * Supported HTTP verbs for Core Engine requests.
 */
enum class EShintHttpMethod : uint8
{
	GET,
	POST,
	PUT,
	DELETE_
};

/**
 * FShintRequestResult
 * Immutable result object returned (via delegate) after a raw HTTP request completes.
 */
struct FShintRequestResult
{
	/** True if the request completed with a 2xx HTTP status code */
	bool bSuccess = false;

	/** HTTP status code (0 if no response received) */
	int32 StatusCode = 0;

	/** Raw response body as a string */
	FString ResponseBody;

	/** Human-readable error message when bSuccess == false */
	FString ErrorMessage;
};

/** Delegate fired when a raw HTTP request completes. Executed on the Game Thread. */
DECLARE_DELEGATE_OneParam(FOnShintRequestComplete, const FShintRequestResult& /*Result*/);

// ─────────────────────────────────────────────────────────────────────────────
// Validate types
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FShintCodeIssue
 * A single code issue returned by POST /validate/code.
 */
struct FShintCodeIssue
{
	/** Rule identifier, e.g. "C001" */
	FString RuleId;

	/** "error" or "warning" */
	FString Severity;

	/** Human-readable issue description */
	FString Message;

	/** Source line where the issue was detected (1-based, 0 = unknown) */
	int32 Line = 0;
};

/**
 * FShintValidateResult
 * Parsed result from POST /validate/code.
 */
struct FShintValidateResult
{
	/** True if the HTTP request succeeded (2xx) and the response was parsed */
	bool bSuccess = false;

	/** HTTP status code (0 if no response received) */
	int32 StatusCode = 0;

	/** Populated when bSuccess == false */
	FString ErrorMessage;

	// ── Summary ──────────────────────────────────────────────────────────────
	int32 TotalIssues   = 0;
	int32 TotalErrors   = 0;
	int32 TotalWarnings = 0;

	/** Per-issue breakdown */
	TArray<FShintCodeIssue> Issues;
};

/** Delegate fired when a POST /validate/code request completes. */
DECLARE_DELEGATE_OneParam(FOnShintValidateComplete, const FShintValidateResult& /*Result*/);

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FShintCoreConfig
 * Runtime configuration loaded from shinttools.config.json.
 */
struct FShintCoreConfig
{
	/** TCP port the Core Engine REST API listens on */
	int32 CorePort = 18200;

	/** If true, the plugin will attempt to start the Core Engine automatically */
	bool bAutoStartCore = false;

	/** Base URL built from CorePort - refreshed whenever CorePort changes */
	FString GetBaseUrl() const
	{
		return FString::Printf(TEXT("http://localhost:%d"), CorePort);
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Client
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FShintCoreClient
 *
 * Thin HTTP client that communicates with the ShintTools Core Engine REST API.
 * All business logic lives in the Core Engine; this class only handles
 * serialization, transport, and response delivery.
 *
 * Thread safety: All public methods must be called from the Game Thread.
 */
class SHINTTOOLS_API FShintCoreClient
{
public:

	FShintCoreClient();
	~FShintCoreClient();

	// ── Configuration ────────────────────────────────────────────────────────

	/**
	 * Loads configuration from <ProjectDir>/shinttools.config.json.
	 * Falls back to default values if the file is missing or malformed.
	 * @return True if the file was found and parsed successfully
	 */
	bool LoadConfig();

	/** Returns the currently active configuration */
	const FShintCoreConfig& GetConfig() const { return Config; }

	// ── Connectivity ─────────────────────────────────────────────────────────

	/**
	 * Fires a GET /health request to check whether the Core Engine is running.
	 * @param OnComplete - Called on completion with the request result
	 */
	void CheckHealth(FOnShintRequestComplete OnComplete);

	/**
	 * Fires a GET /ping request as a lightweight round-trip test.
	 * @param OnComplete - Called on completion with the request result
	 */
	void Ping(FOnShintRequestComplete OnComplete);

	// ── Validation ───────────────────────────────────────────────────────────

	/**
	 * Sends POST /validate/code with the given file details.
	 * Internally builds the JSON payload, dispatches the request, parses the
	 * response and fires OnComplete with a structured FShintValidateResult.
	 *
	 * Expected Core Engine JSON body:
	 *   { "file_path": "…", "content": "…", "engine": "unreal" }
	 *
	 * @param FilePath   - File name or relative path (for display only)
	 * @param Content    - Full source text of the file
	 * @param Engine     - Target engine tag, e.g. "unreal"
	 * @param OnComplete - Delegate fired on the Game Thread with parsed results
	 */
	void ValidateCode(
		const FString& FilePath,
		const FString& Content,
		const FString& Engine,
		FOnShintValidateComplete OnComplete);

	// ── Generic Request ───────────────────────────────────────────────────────

	/**
	 * Sends an HTTP request to the Core Engine.
	 *
	 * @param Endpoint   - Path relative to base URL, e.g. "/health"
	 * @param Method     - HTTP verb
	 * @param Body       - JSON body (ignored for GET/DELETE)
	 * @param OnComplete - Delegate fired on completion (Game Thread)
	 */
	void SendRequest(
		const FString& Endpoint,
		EShintHttpMethod Method,
		const FString& Body,
		FOnShintRequestComplete OnComplete);

private:

	/** Internal callback wired to IHttpRequest::OnProcessRequestComplete */
	void OnHttpRequestComplete(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully,
		FOnShintRequestComplete OnComplete);

	/** Converts EShintHttpMethod to the string expected by FHttpModule */
	static FString MethodToString(EShintHttpMethod Method);

	/** Loaded runtime configuration */
	FShintCoreConfig Config;
};
