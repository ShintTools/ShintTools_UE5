// Copyright ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"

// ─────────────────────────────────────────────────────────────────────────────
// HTTP primitives
// ─────────────────────────────────────────────────────────────────────────────

enum class EShintHttpMethod : uint8 { GET, POST, PUT, DELETE_ };

struct FShintRequestResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintRequestComplete, const FShintRequestResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator — enriched issue
// ─────────────────────────────────────────────────────────────────────────────

struct FShintCodeIssue
{
	FString RuleId;
	FString Severity;         // "error" | "warning"
	FString Message;
	FString FilePath;         // absolute path on disk
	int32   Line             = 0;
	FString Snippet;          // the problematic source line
	FString FixSuggestion;    // what the corrected line should look like
	bool    bIsAutoFixable   = false;
	int32   LinesCount       = 0;  // total lines in file (for dashboard)

	// Extended fields from server response
	FString Class;            // e.g. "ABrokenTestActor" or blueprint class name
	FString Category;         // e.g. "memory", "style", "performance", "blueprint"
	FString Graph;            // blueprint graph name (empty for C++ issues)

	// Runtime UI state — not sent over wire
	bool    bChecked         = false;
};

struct FShintValidateResult
{
	bool    bSuccess      = false;
	int32   StatusCode    = 0;
	FString ErrorMessage;
	int32   TotalIssues   = 0;
	int32   TotalErrors   = 0;
	int32   TotalWarnings = 0;
	int32   FilesScanned  = 0;
	TArray<FShintCodeIssue> Issues;

	// Kept for "Send to Dashboard" — populated during scan
	TArray<FString> ScannedFilePaths;  // absolute paths of all scanned files
};
DECLARE_DELEGATE_OneParam(FOnShintValidateComplete, const FShintValidateResult&);

struct FShintFixedFile
{
	FString FilePath;           // absolute path
	FString CorrectedContent;
	int32   FixesApplied = 0;
	int32   FixesSkipped = 0;
};

struct FShintFixResult
{
	bool    bSuccess           = false;
	int32   TotalFixesApplied  = 0;
	int32   TotalFixesSkipped  = 0;
	FString ErrorMessage;
	TArray<FShintFixedFile> FixedFiles;
};
DECLARE_DELEGATE_OneParam(FOnShintFixComplete, const FShintFixResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming Bot
// ─────────────────────────────────────────────────────────────────────────────

struct FShintAssetIssue
{
	FString AssetPath;       // UE package path e.g. /Game/Characters/hero_body
	FString CurrentName;
	FString SuggestedName;
	FString Reason;
	FString AssetType;       // "Texture2D", "StaticMesh", etc.

	bool    bChecked = true;
};

struct FShintAssetScanResult
{
	bool    bSuccess        = false;
	int32   StatusCode      = 0;
	FString ErrorMessage;
	int32   TotalAssets     = 0;
	int32   InvalidAssets   = 0;
	float   ScanTimeSeconds = 0.0f;
	TArray<FShintAssetIssue> Issues;
};
DECLARE_DELEGATE_OneParam(FOnShintAssetScanComplete, const FShintAssetScanResult&);

struct FShintAssetFixResult
{
	bool    bSuccess      = false;
	int32   AssetsRenamed = 0;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintAssetFixComplete, const FShintAssetFixResult&);

// ─────────────────────────────────────────────────────────────────────────────
// External web dashboard results
// ─────────────────────────────────────────────────────────────────────────────

struct FShintWebDashboardResult
{
	bool    bSuccess     = false;
	FString ErrorMessage;
	FString ResponseBody;
};
DECLARE_DELEGATE_OneParam(FOnShintWebDashboardComplete, const FShintWebDashboardResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Local dashboard report (legacy — keeps local MongoDB sync)
// ─────────────────────────────────────────────────────────────────────────────

struct FShintDashboardReport
{
	FString ProjectName;
	FString Engine = TEXT("unreal");
	FString ReportType;
	int32   Code_FilesScanned   = 0;
	int32   Code_TotalIssues    = 0;
	int32   Code_TotalErrors    = 0;
	int32   Code_TotalWarnings  = 0;
	int32   Asset_TotalScanned  = 0;
	int32   Asset_InvalidAssets = 0;
	float   Asset_ScanTime      = 0.0f;
};
struct FShintDashboardResult { bool bSuccess = false; FString ErrorMessage; };
DECLARE_DELEGATE_OneParam(FOnShintDashboardComplete, const FShintDashboardResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

struct FShintCoreConfig
{
	// Local core engine
	int32   CorePort        = 18200;
	bool    bAutoStartCore  = false;

	// Project identity
	FString ProjectName     = TEXT("MyGame");
	FString ProjectId       = TEXT("");

	// External web dashboard (app.shinttools.io or emergent)
	FString ApiKey          = TEXT("");
	FString DashboardUrl    = TEXT("https://app.shinttools.io");

	FString GetBaseUrl() const
	{
		return FString::Printf(TEXT("http://localhost:%d"), CorePort);
	}

	bool HasExternalDashboard() const
	{
		return !ApiKey.IsEmpty() && !DashboardUrl.IsEmpty() && !ProjectId.IsEmpty();
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Client
// ─────────────────────────────────────────────────────────────────────────────

class SHINTTOOLS_API FShintCoreClient
{
public:
	FShintCoreClient();
	~FShintCoreClient();

	bool LoadConfig();
	bool SaveConfig() const;
	const FShintCoreConfig& GetConfig() const { return Config; }
	FShintCoreConfig& GetConfigMutable() { return Config; }

	// ── Connectivity ─────────────────────────────────────────────────────────
	void CheckHealth(FOnShintRequestComplete OnComplete);
	void Ping(FOnShintRequestComplete OnComplete);

	// ── Code Validator — local engine ─────────────────────────────────────────
	void ValidateCode(const FString& AbsFilePath, const FString& Content,
	                  const FString& Engine, FOnShintValidateComplete OnComplete);
	void ValidateProject(const FString& SourceDir, FOnShintValidateComplete OnComplete);
	void ValidateBlueprints(const FString& ContentDir, FOnShintValidateComplete OnComplete);
	void ApplyCodeFixes(const TArray<FShintCodeIssue>& AcceptedIssues,
	                    FOnShintFixComplete OnComplete);

	// ── Code Validator — external web dashboard ───────────────────────────────
	/**
	 * Sends the full project scan to the web dashboard.
	 * Payload: POST {DashboardUrl}/api/code-validator/analyze
	 * Body: { project_id, project_name, api_key, files:[{name,path,type,content,lines_count}] }
	 */
	void SendCodeValidatorToDashboard(const FShintValidateResult& LastResult,
	                                  FOnShintWebDashboardComplete OnComplete);

	// ── Asset Naming Bot — local engine ──────────────────────────────────────
	void ScanAssetNaming(const FString& ContentDir, FOnShintAssetScanComplete OnComplete);
	void ReportAssetFixesToServer(const TArray<FShintAssetIssue>& Fixed,
	                              FOnShintAssetFixComplete OnComplete);

	// ── Asset Naming Bot — external web dashboard ─────────────────────────────
	/**
	 * Sends naming violations to the web dashboard.
	 * Payload: POST {DashboardUrl}/api/naming-bot/analyze
	 * Body: { project_id, project_name, api_key, items:[{name,path,type,category}] }
	 */
	void SendAssetNamingToDashboard(const FShintAssetScanResult& LastResult,
	                                FOnShintWebDashboardComplete OnComplete);

	// ── Local MongoDB dashboard report (legacy) ───────────────────────────────
	void SendDashboardReport(const FShintDashboardReport& Report,
	                         FOnShintDashboardComplete OnComplete);

	// ── Generic ───────────────────────────────────────────────────────────────
	void SendRequest(const FString& FullUrl, EShintHttpMethod Method,
	                 const FString& Body, FOnShintRequestComplete OnComplete,
	                 const TMap<FString, FString>& ExtraHeaders = {});

private:
	void OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                           bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete);

	static FString MethodToString(EShintHttpMethod Method);
	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);
	static FShintValidateResult  ParseValidateResponse (const FShintRequestResult& Raw);
	static FShintAssetScanResult ParseAssetScanResponse(const FShintRequestResult& Raw);
	static FShintFixResult       ParseFixResponse      (const FShintRequestResult& Raw);
	static void CollectSourceFiles(const FString& Dir, TArray<FString>& Out);

	// Infer asset category from UE type string (for dashboard payload)
	static FString AssetTypeToCategory(const FString& AssetType);

	FShintCoreConfig Config;
};
