// Copyright ShintTools. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "Dom/JsonObject.h"

// ─────────────────────────────────────────────────────────────────────────────
// HTTP result
// ─────────────────────────────────────────────────────────────────────────────

struct FShintRaw
{
	bool    bOk  = false;
	int32   Code = 0;
	FString Body;
	FString Err;
};
DECLARE_DELEGATE_OneParam(FOnRaw, const FShintRaw&);

// ─────────────────────────────────────────────────────────────────────────────
// Fix request structs (AI-engineer spec)
// ─────────────────────────────────────────────────────────────────────────────

struct FAcceptedFix   { FString RuleId; int32 Line = 0; FString FilePath; };
struct FFixFileRequest { FString FilePath; FString Source; TArray<FAcceptedFix> Fixes; };

// ─────────────────────────────────────────────────────────────────────────────
// Code Validator
// ─────────────────────────────────────────────────────────────────────────────

struct FShintIssue
{
	FString Rule, Sev, Msg, File, Snippet, FixHint;
	int32   Line     = 0;
	bool    bFixable = false;
};

struct FValidateResult
{
	bool    bOk    = false;
	FString Err;
	int32   Issues = 0, Errors = 0, Warns = 0, Files = 0;
	TArray<FShintIssue> List;
	TArray<FString>     ScannedPaths;
};
DECLARE_DELEGATE_OneParam(FOnValidate, const FValidateResult&);

struct FFixedFile { FString Path; FString Content; int32 Applied = 0, Skipped = 0; };
DECLARE_DELEGATE_OneParam(FOnFix, const FFixResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Asset Naming
// ─────────────────────────────────────────────────────────────────────────────

struct FAssetIssue { FString Path, Current, Suggested, Reason, Type; };
struct FAssetScan  { bool bOk = false; FString Err; int32 Total = 0, Invalid = 0; float Secs = 0.f; TArray<FAssetIssue> List; };
DECLARE_DELEGATE_OneParam(FOnAssetScan, const FAssetScan&);

struct FAssetFix { bool bOk = false; int32 Renamed = 0; FString Err; };
DECLARE_DELEGATE_OneParam(FOnAssetFix, const FAssetFix&);

// ─────────────────────────────────────────────────────────────────────────────
// Web / Dashboard
// ─────────────────────────────────────────────────────────────────────────────

struct FWebResult { bool bOk = false; FString Err, Body; };
DECLARE_DELEGATE_OneParam(FOnWeb, const FWebResult&);

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

struct FShintCfg
{
	int32   Port      = 18200;
	FString Name      = TEXT("MyGame");
	FString ProjectId, Key;
	FString DashUrl   = TEXT("https://app.shinttools.io");

	FString Base()    const { return FString::Printf(TEXT("http://localhost:%d"), Port); }
	bool    HasDash() const { return !Key.IsEmpty() && !ProjectId.IsEmpty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Client
// ─────────────────────────────────────────────────────────────────────────────

class SHINTTOOLS_API FShintClient
{
public:
	FShintClient();
	~FShintClient() = default;

	bool          LoadConfig();
	FShintCfg&        Cfg()       { return C; }
	const FShintCfg&  Cfg() const { return C; }

	// Connectivity
	void Health(FOnRaw Done);

	// Code Validator
	void ScanProject   (const FString& SrcDir,     FOnValidate Done);
	void ScanBlueprints(const FString& ContentDir, FOnValidate Done);
	void ApplyFixes    (const TArray<FFixFileRequest>& Reqs, FOnFix Done);

	// Dashboard
	void PushCode      (const FValidateResult& R, FOnWeb Done);
	void PushBlueprints(const TArray<TSharedPtr<FJsonObject>>& BPs, FOnWeb Done);
	void PushAssets    (const FAssetScan& R, FOnWeb Done);

	// Asset Naming
	void ScanAssets (const FString& ContentDir, FOnAssetScan Done);
	void ReportFixes(const TArray<FAssetIssue>& Fixed, FOnAssetFix Done);

	// Cached BP JSON for dashboard push
	const TArray<TSharedPtr<FJsonObject>>& BpCache() const { return BPs; }

private:
	void Post(const FString& Url, const FString& Body, FOnRaw Done);
	void Get (const FString& Url, FOnRaw Done);
	void Http(const FString& Url, const FString& Verb, const FString& Body, FOnRaw Done);
	void OnDone(FHttpRequestPtr, FHttpResponsePtr, bool, FOnRaw);

	static FValidateResult ParseValidate(const FShintRaw& R);
	static FAssetScan      ParseAssets  (const FShintRaw& R);
	static FFixResult      ParseFix     (const FShintRaw& R);
	static FString         ToJson(TSharedRef<FJsonObject> Obj);
	static void            CollectCpp(const FString& Dir, TArray<FString>& Out);
	static FString         AssetCat(const FString& Type);

	TSharedPtr<FJsonObject> ReadBP(const FString& ObjPath) const;
	static void             AnalyseBP(const TSharedPtr<FJsonObject>& Bp, TArray<FShintIssue>& Out);

	FShintCfg C;
	TArray<TSharedPtr<FJsonObject>> BPs;
};
