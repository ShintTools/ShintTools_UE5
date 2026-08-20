// Copyright 2026 ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "Security/ShintSecurity.h"
#include "Transport/ShintHttpTypes.h"

enum TextureCompressionSettings : int;

struct FShintRequestResult
{
	bool    bSuccess     = false;
	int32   StatusCode   = 0;
	FString ResponseBody;
	FString ErrorMessage;
};
DECLARE_DELEGATE_OneParam(FOnShintRequestComplete, const FShintRequestResult&);

DECLARE_DELEGATE_OneParam(FOnShintStreamChunk, const FString&);

class FShintSseParser;

struct FShintCodeIssue
{
	FString RuleId;
	FString Severity;
	FString Message;
	FString FilePath;
	int32   Line             = 0;
	FString Snippet;
	FString FixSuggestion;
	bool    bIsAutoFixable   = false;
	int32   LinesCount       = 0;

	FString Class;
	FString Category;
	FString Graph;

	FString RuleName;
	FString RuleExplanation;

	FString FileContent;

	FString ContextBefore;
	FString ContextAfter;
	int32   ContextLineStart = 0;

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

	TArray<FString> ScannedFilePaths;

	float   QualityScoreOverall = -1.f;

	bool    bHasCategoryBreakdown = false;
	float   PerformanceScore      = 100.f;
	float   SecurityScore         = 100.f;
	float   BestPracticesScore    = 100.f;
	float   MaintainabilityScore  = 100.f;
	float   NamingScore           = 100.f;

	FString Tier;

	bool    bLimitApplied   = false;
	FString LimitKind;
	int32   LimitValue      = 0;
	int32   TotalAvailable  = 0;

	FString AnalysisId;
};
DECLARE_DELEGATE_OneParam(FOnShintValidateComplete, const FShintValidateResult&);

struct FShintFixedFile
{
	FString FilePath;
	FString CorrectedContent;
	FString Additions;
	TArray<FString> Changes;
	int32   FixesApplied = 0;
	int32   FixesSkipped = 0;
};

struct FShintCompileError
{
	FString FilePath;
	FString FileName;
	int32   Line     = 0;
	int32   Column   = 0;
	FString Code;
	FString Message;
	FString Severity;
};

struct FShintFixResult
{
	bool    bSuccess           = false;
	int32   TotalFixesApplied  = 0;
	int32   TotalFixesSkipped  = 0;

	int32   BlueprintFixesApplied = 0;
	FString ErrorMessage;
	TArray<FShintFixedFile>     FixedFiles;
	TArray<FShintCompileError>  CompileErrors;
	bool    bHasCompileErrors  = false;
};
DECLARE_DELEGATE_OneParam(FOnShintFixComplete, const FShintFixResult&);

struct FShintSafetyCheckResult
{

	bool            bCheckRan = false;
	bool            bSafe     = true;
	TArray<FString> Warnings;
	FString         Preview;
};
DECLARE_DELEGATE_OneParam(FOnShintSafetyCheckComplete, const FShintSafetyCheckResult&);

struct FShintAssetIssue
{
	FString AssetPath;
	FString CurrentName;
	FString SuggestedName;
	FString Reason;
	FString AssetType;

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

	FString Tier;

	bool    bLimitApplied  = false;
	FString LimitKind;
	int32   LimitValue     = 0;
	int32   TotalAvailable = 0;

	FString AnalysisId;

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

struct FShintQualityScoreSnapshot
{
	bool    bValid               = false;
	int32   StatusCode           = 0;
	FString ErrorMessage;

	float   OverallScore         = 0.f;

	float   PerformanceScore     = 100.f;
	float   SecurityScore        = 100.f;
	float   BestPracticesScore   = 100.f;
	float   MaintainabilityScore = 100.f;
	float   NamingScore          = 100.f;

	int32   Errors               = 0;
	int32   Warnings             = 0;
	int32   Infos                = 0;
	int32   FilesScanned         = 0;
	int32   TotalIssues          = 0;
	float   TotalPenalty         = 0.f;

	FString ProjectId;
	FString Timestamp;
	FString ScanType;
	FString Tier;
};
DECLARE_DELEGATE_OneParam(FOnShintQualityScoreComplete, const FShintQualityScoreSnapshot&);

struct FShintQualityScoreHistory
{
	bool    bValid     = false;
	int32   StatusCode = 0;
	FString ErrorMessage;
	FString ProjectId;
	TArray<FShintQualityScoreSnapshot> Scores;
};
DECLARE_DELEGATE_OneParam(FOnShintQualityScoreHistoryComplete, const FShintQualityScoreHistory&);

struct FShintAssistantTurn
{
	FString TurnId;
	FString Role;
	FString Intent;
	FString RawText;
	FString ContextRef;
	FString RuleId;
	FString AssetPath;
};

struct FShintAssistantResponse
{
	bool    bSuccess = false;
	FString ConversationId;
	FString Tier;
	FString Intent;
	bool    bContinued = false;
	bool    bDegraded  = false;
	FShintAssistantTurn Reply;

	FString         ErrorMessage;
	int32           StatusCode = 0;
	FString         CurrentTier;
	TArray<FString> AllowedIntents;
};

struct FShintAssistantCapabilities
{
	bool            bSuccess = false;
	FString         Tier;
	TArray<FString> Intents;
	FString         Memory;
	FString         ModelProfile;
	FString         StudioRules;
	FString         ErrorMessage;

	bool CanRun(const FString& Intent) const { return Intents.Contains(Intent); }
	bool HasPersistentMemory() const { return Memory == TEXT("full"); }
};

struct FShintAssistantFact
{
	FString FactId;
	FString Type;
	FString Value;
	FString Status;
	FString SourceModule;
	FString SourceConversationId;
};

struct FShintAssistantMemory
{
	bool bSuccess = false;
	bool bMuted   = false;
	TArray<FShintAssistantFact> Facts;
	FString ErrorMessage;
};

struct FShintAssistantRule
{
	FString RuleId;
	FString Name;
	FString Tier;
	FString Status;
	FString Description;
};

struct FShintAssistantRules
{
	bool bSuccess = false;
	TArray<FShintAssistantRule> Rules;
	FString ErrorMessage;
};

struct FShintAssistantContradiction
{
	FString FactId;
	FString FactValue;
	FString RuleId;
	FString AssetPath;
	FString Nudge;
};

struct FShintAssistantDecisions
{
	bool bSuccess = false;
	TArray<FShintAssistantContradiction> Contradictions;
	FString ErrorMessage;
};

struct FShintAssistantRequest
{
	FString Message;
	FString ConversationId;
	FString Intent;
	FString ContextRef;
	FString RuleId;
	FString AssetPath;
	FString PlatformProfile;
	FString StudioId;
	FString ProjectId;
	FString ModuleContext;
};

DECLARE_DELEGATE_OneParam(FOnShintAssistantComplete,     const FShintAssistantResponse&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantCapabilities, const FShintAssistantCapabilities&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantMemory,       const FShintAssistantMemory&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantRules,        const FShintAssistantRules&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantDecisions,    const FShintAssistantDecisions&);
DECLARE_DELEGATE_OneParam(FOnShintAssistantThread,       const TArray<FShintAssistantTurn>&);

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

struct FShintCoreConfig
{

	FString CoreHost        = TEXT("127.0.0.1");
	int32   CorePort        = 18200;
	bool    bAutoStartCore  = false;

	FString ProjectName     = TEXT("MyGame");
	FString ProjectId       = TEXT("");

	FString ApiKeyDashboard = TEXT("");
	FString ApiKeyMongo     = TEXT("");
	FString SessionToken    = TEXT("");
	FString DashboardUrl    = TEXT("https://shint.tools");

	TArray<FString> ExcludedPaths;
	FString         ExportPath      = TEXT("");

	FString GetBaseUrl() const
	{
#if SHINT_FREE_TIER

		return FString::Printf(TEXT("http://%s:%d"),
			SHINT_HARDCODED_CORE_HOST, SHINT_HARDCODED_CORE_PORT);
#else
		FString Host = CoreHost.IsEmpty() ? TEXT("127.0.0.1") : CoreHost;

		if (Host == TEXT("0.0.0.0") || Host == TEXT("::") || Host == TEXT("localhost"))
		{
			Host = TEXT("127.0.0.1");
		}
		return FString::Printf(TEXT("http://%s:%d"), *Host, CorePort);
#endif
	}

	bool HasExternalDashboard() const
	{

		return !ApiKeyDashboard.IsEmpty() && !DashboardUrl.IsEmpty();
	}
};

class SHINTTOOLS_API FShintCoreClient : public TSharedFromThis<FShintCoreClient>
{
public:
	FShintCoreClient();
	~FShintCoreClient();

	bool LoadConfig();
	bool SaveConfig() const;
	const FShintCoreConfig& GetConfig() const { return Config; }
	FShintCoreConfig& GetConfigMutable() { return Config; }

	void CheckHealth(FOnShintRequestComplete OnComplete);
	void Ping(FOnShintRequestComplete OnComplete);

	void ValidateCode(const FString& AbsFilePath, const FString& Content,
	                  const FString& Engine, FOnShintValidateComplete OnComplete);
	void ValidateProject(const FString& SourceDir, FOnShintValidateComplete OnComplete);
	void ValidateBlueprints(const FString& ContentDir, FOnShintValidateComplete OnComplete);
	void ApplyCodeFixes(const TArray<FShintCodeIssue>& AcceptedIssues,
	                    FOnShintFixComplete OnComplete);

	void FetchSingleFixPreview(const FShintCodeIssue& Issue, FOnShintFixComplete OnComplete);

	void CheckFixSafety(const TArray<FShintCodeIssue>& Issues, FOnShintSafetyCheckComplete OnComplete);

	void GetLatestQualityScore(const FString& ProjectId,
	                           FOnShintQualityScoreComplete OnComplete);

	void GetQualityScoreHistory(const FString& ProjectId, int32 Limit,
	                            FOnShintQualityScoreHistoryComplete OnComplete);

	void ScanAssetNaming(const FString& ContentDir, FOnShintAssetScanComplete OnComplete);
	void ReportAssetFixesToServer(const TArray<FShintAssetIssue>& Fixed,
	                              FOnShintAssetFixComplete OnComplete);

	void SendDashboardReport(const FShintDashboardReport& Report,
	                         FOnShintDashboardComplete OnComplete);

	void SendAssistantMessage(const FShintAssistantRequest& Request,
	                          FOnShintAssistantComplete OnComplete);

	void SendAssistantMessageStream(const FShintAssistantRequest& Request,
	                                FOnShintStreamChunk       OnChunk,
	                                FOnShintAssistantComplete OnComplete);

	void GetAssistantCapabilities(FOnShintAssistantCapabilities OnComplete);

	void GetAssistantConversation(const FString& ConversationId,
	                              FOnShintAssistantThread OnComplete);

	void GetAssistantMemory(const FString& StudioId, const FString& ProjectId,
	                        FOnShintAssistantMemory OnComplete);

	void ConfirmAssistantFact(const FString& FactId, bool bAccept,
	                          FOnShintAssistantComplete OnComplete);

	void MuteAssistantMemory(const FString& StudioId, const FString& ProjectId,
	                         bool bMuted, FOnShintAssistantComplete OnComplete);

	void PurgeAssistantProject(const FString& StudioId, const FString& ProjectId,
	                           FOnShintAssistantComplete OnComplete);

	void GetAssistantRules(const FString& StudioId, const FString& ProjectId,
	                       FOnShintAssistantRules OnComplete);

	void ConfirmAssistantRule(const FString& RuleId, bool bAccept,
	                          FOnShintAssistantComplete OnComplete);

	void CheckAssistantDecisions(const FString& StudioId, const FString& ProjectId,
	                             const FString& ContextRef,
	                             FOnShintAssistantDecisions OnComplete);

	void SendRequest(const FString& FullUrl, EShintHttpMethod Method,
	                 const FString& Body, FOnShintRequestComplete OnComplete,
	                 const TMap<FString, FString>& ExtraHeaders = {});

	void SendRequestStream(const FString& FullUrl, EShintHttpMethod Method,
	                       const FString& Body, FOnShintStreamChunk OnChunk,
	                       FOnShintRequestComplete OnComplete,
	                       const TMap<FString, FString>& ExtraHeaders = {});

	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj);
	static FString AssetTypeToCategory(const FString& AssetType);

	static FShintValidateResult  ParseValidateResponse (const FShintRequestResult& Raw);

private:
	void OnHttpRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                           bool bConnectedSuccessfully, FOnShintRequestComplete OnComplete);

	void OnHttpStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response,
	                          bool bConnectedSuccessfully,
	                          TSharedRef<FShintSseParser> Parser,
	                          FOnShintRequestComplete OnComplete);

	static FString MethodToString(EShintHttpMethod Method);
	static FShintAssetScanResult ParseAssetScanResponse(const FShintRequestResult& Raw);
	static FShintFixResult       ParseFixResponse      (const FShintRequestResult& Raw);
	static FShintFixResult       ParseTreeSitterFixResponse(const FShintRequestResult& Raw);

	static bool                  ParseScoreObject(const TSharedPtr<class FJsonObject>& Obj,
	                                              FShintQualityScoreSnapshot& Out);
	static FShintQualityScoreSnapshot         ParseLatestScoreResponse (const FShintRequestResult& Raw);
	static FShintQualityScoreHistory          ParseScoreHistoryResponse(const FShintRequestResult& Raw);
	static void CollectSourceFiles(const FString& Dir, TArray<FString>& Out);

	void HandleTreeSitterFixResponse(const FShintRequestResult& Raw,
	                                 FShintFixResult             LocalResult,
	                                 TArray<FShintCodeIssue>     TreeSitterIssues,
	                                 FOnShintFixComplete         OnComplete);

	FShintCoreConfig Config;
};