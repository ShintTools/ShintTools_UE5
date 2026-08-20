// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintCoreClient.h"
#include "ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

void FShintCoreClient::ScanAssetNaming(
	const FString& ContentDir, FOnShintAssetScanComplete OnComplete)
{
	const double BenchStart = FPlatformTime::Seconds();
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter AssetFilter;
	AssetFilter.PackagePaths.Add(TEXT("/Game"));
	AssetFilter.bRecursivePaths = true;

	TArray<FAssetData> AllAssets;
	AR.GetAssets(AssetFilter, AllAssets);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AD : AllAssets)
	{
		const FString AssetClass = AD.AssetClassPath.GetAssetName().ToString();

		if (AssetClass == TEXT("ObjectRedirector")) continue;

		const FString PackagePath = AD.PackageName.ToString();
		const FString AssetName   = AD.AssetName.ToString();

		TSharedRef<FJsonObject> AObj = MakeShared<FJsonObject>();
		AObj->SetStringField(TEXT("asset_path"), PackagePath);
		AObj->SetStringField(TEXT("name"),       AssetName);
		AObj->SetStringField(TEXT("type"),       AssetClass);
		AObj->SetStringField(TEXT("category"),   TEXT(""));
		Arr.Add(MakeShared<FJsonValueObject>(AObj));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Config.ProjectName);
	Body->SetStringField(TEXT("engine"),       TEXT("unreal"));
	Body->SetStringField(TEXT("api_key"),      Config.ApiKeyMongo);
	Body->SetArrayField(TEXT("asset_paths"),   Arr);

	const FString BodyStr = SerializeJson(Body);

	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient: Scanning %d assets from /Game/"), AllAssets.Num());
	UE_LOG(LogShintTools, Verbose, TEXT("AssetScan REQUEST JSON (first 3000 chars):\n%s"), *BodyStr.Left(3000));

	const int32 SentAssets = Arr.Num();
	SendRequest(Config.GetBaseUrl() + TEXT("/assets/scan"), EShintHttpMethod::POST, BodyStr,
		FOnShintRequestComplete::CreateLambda([OnComplete, BenchStart, SentAssets](const FShintRequestResult& Raw) mutable {
			FShintAssetScanResult R = ParseAssetScanResponse(Raw);
			UE_LOG(LogShintTools, Verbose,
				TEXT("[BENCH] ScanAssetNaming: %.2f s, %d assets sent, %d violations"),
				FPlatformTime::Seconds() - BenchStart, SentAssets, R.Issues.Num());
			OnComplete.ExecuteIfBound(R);
		}));
}

void FShintCoreClient::ReportAssetFixesToServer(
	const TArray<FShintAssetIssue>& Fixed, FOnShintAssetFixComplete OnComplete)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FShintAssetIssue& I : Fixed)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("asset_path"),     I.AssetPath);
		O->SetStringField(TEXT("current_name"),   I.CurrentName);
		O->SetStringField(TEXT("suggested_name"), I.SuggestedName);
		O->SetStringField(TEXT("asset_type"),     I.AssetType);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("issues"), Arr);

	SendRequest(Config.GetBaseUrl() + TEXT("/assets/fix"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete, Count = Fixed.Num()](const FShintRequestResult& Raw) mutable {
			FShintAssetFixResult Result;
			Result.bSuccess      = Raw.bSuccess;
			Result.AssetsRenamed = Count;
			Result.ErrorMessage  = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			OnComplete.ExecuteIfBound(Result);
		}));
}

void FShintCoreClient::SendDashboardReport(
	const FShintDashboardReport& Report, FOnShintDashboardComplete OnComplete)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("project_name"), Report.ProjectName);
	Body->SetStringField(TEXT("engine"),       Report.Engine);
	Body->SetStringField(TEXT("report_type"),  Report.ReportType);

	if (Report.ReportType == TEXT("code_validator"))
	{
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("files_scanned"),  Report.Code_FilesScanned);
		D->SetNumberField(TEXT("total_issues"),   Report.Code_TotalIssues);
		D->SetNumberField(TEXT("total_errors"),   Report.Code_TotalErrors);
		D->SetNumberField(TEXT("total_warnings"), Report.Code_TotalWarnings);
		Body->SetObjectField(TEXT("code_validator"), D);
	}
	else if (Report.ReportType == TEXT("asset_naming"))
	{
		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetNumberField(TEXT("total_scanned"),  Report.Asset_TotalScanned);
		D->SetNumberField(TEXT("invalid_assets"), Report.Asset_InvalidAssets);
		D->SetNumberField(TEXT("scan_time_s"),    Report.Asset_ScanTime);
		Body->SetObjectField(TEXT("asset_naming"), D);
	}

	SendRequest(Config.GetBaseUrl() + TEXT("/dashboard/report"), EShintHttpMethod::POST, SerializeJson(Body),
		FOnShintRequestComplete::CreateLambda([OnComplete](const FShintRequestResult& Raw) mutable {
			FShintDashboardResult R;
			R.bSuccess     = Raw.bSuccess;
			R.ErrorMessage = Raw.bSuccess ? TEXT("") : Raw.ErrorMessage;
			OnComplete.ExecuteIfBound(R);
		}));
}

FShintAssetScanResult FShintCoreClient::ParseAssetScanResponse(const FShintRequestResult& Raw)
{
	FShintAssetScanResult R;
	R.StatusCode = Raw.StatusCode;
	if (!Raw.bSuccess) { R.bSuccess = false; R.ErrorMessage = Raw.ErrorMessage; return R; }

	TSharedPtr<FJsonObject> J;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, J) || !J.IsValid())
	{ R.bSuccess = false; R.ErrorMessage = TEXT("Parse failed."); return R; }

	R.bSuccess = true;

	UE_LOG(LogShintTools, Verbose, TEXT("AssetScan: Raw response: %s"),
		*Raw.ResponseBody.Left(2000));

	const TSharedPtr<FJsonObject>* Sum = nullptr;
	if (J->TryGetObjectField(TEXT("summary"), Sum) && Sum)
	{
		(*Sum)->TryGetNumberField(TEXT("total_assets"),       R.TotalAssets);
		(*Sum)->TryGetNumberField(TEXT("invalid_assets"),     R.InvalidAssets);
		(*Sum)->TryGetNumberField(TEXT("scan_time_seconds"),  R.ScanTimeSeconds);
		(*Sum)->TryGetStringField(TEXT("tier"),               R.Tier);

		(*Sum)->TryGetBoolField  (TEXT("limit_applied"),   R.bLimitApplied);
		(*Sum)->TryGetStringField(TEXT("limit_kind"),      R.LimitKind);
		(*Sum)->TryGetNumberField(TEXT("limit_value"),     R.LimitValue);
		(*Sum)->TryGetNumberField(TEXT("total_available"), R.TotalAvailable);
		if (R.LimitKind.IsEmpty() && R.Tier.Equals(TEXT("free"), ESearchCase::IgnoreCase))
		{
			bool bLegacyCapped = false;
			(*Sum)->TryGetBoolField(TEXT("assets_capped"), bLegacyCapped);
			R.bLimitApplied  = bLegacyCapped;
			R.LimitKind      = TEXT("assets");
			R.TotalAvailable = R.TotalAssets;
		}
	}

	J->TryGetStringField(TEXT("analysis_id"), R.AnalysisId);

	const TArray<TSharedPtr<FJsonValue>>* IssArr = nullptr;
	if (!J->TryGetArrayField(TEXT("issues"), IssArr) || !IssArr)
	{

		if (!J->TryGetArrayField(TEXT("violations"), IssArr) || !IssArr)
		{
			J->TryGetArrayField(TEXT("results"), IssArr);
		}
	}

	if (IssArr)
	{
		UE_LOG(LogShintTools, Verbose, TEXT("AssetScan: Found %d issue entries in response"), IssArr->Num());

		for (const TSharedPtr<FJsonValue>& V : *IssArr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;
			FShintAssetIssue Issue;
			if (!(*O)->TryGetStringField(TEXT("asset_path"), Issue.AssetPath))
				(*O)->TryGetStringField(TEXT("path"), Issue.AssetPath);
			if (!(*O)->TryGetStringField(TEXT("current_name"), Issue.CurrentName))
				(*O)->TryGetStringField(TEXT("name"), Issue.CurrentName);

			if (!(*O)->TryGetStringField(TEXT("fix_suggestion"), Issue.SuggestedName))
				(*O)->TryGetStringField(TEXT("suggested_name"), Issue.SuggestedName);
			if (!(*O)->TryGetStringField(TEXT("reason"), Issue.Reason))
				(*O)->TryGetStringField(TEXT("message"), Issue.Reason);
			if (!(*O)->TryGetStringField(TEXT("asset_type"), Issue.AssetType))
				(*O)->TryGetStringField(TEXT("type"), Issue.AssetType);
			Issue.bChecked = true;
			R.Issues.Add(MoveTemp(Issue));
		}
	}
	else
	{
		UE_LOG(LogShintTools, Warning, TEXT("AssetScan: No 'issues', 'violations', or 'results' array found in response"));
	}

	UE_LOG(LogShintTools, Verbose, TEXT("AssetScan: Parsed %d issues, TotalAssets=%d, InvalidAssets=%d"),
		R.Issues.Num(), R.TotalAssets, R.InvalidAssets);

	return R;
}

FString FShintCoreClient::AssetTypeToCategory(const FString& AssetType)
{
	if (AssetType == TEXT("Texture2D") || AssetType.Contains(TEXT("Texture")))
		return TEXT("texture");
	if (AssetType.Contains(TEXT("Mesh")))
		return TEXT("mesh");
	if (AssetType.Contains(TEXT("Material")))
		return TEXT("material");
	if (AssetType.Contains(TEXT("Blueprint")) || AssetType.Contains(TEXT("Widget")))
		return TEXT("blueprint");
	if (AssetType.Contains(TEXT("Sound")) || AssetType.Contains(TEXT("Audio")))
		return TEXT("audio");
	if (AssetType.Contains(TEXT("Anim")))
		return TEXT("animation");
	return TEXT("asset");
}
