// Copyright ShintTools. All Rights Reserved.
//
// Quality Score (Slice B) endpoints split out of ShintCoreClient.cpp.

#include "ShintCoreClient.h"
#include "ShintTools/ShintTools.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

bool FShintCoreClient::ParseScoreObject(
	const TSharedPtr<FJsonObject>& Obj,
	FShintQualityScoreSnapshot& Out)
{
	if (!Obj.IsValid()) return false;

	// Older error responses look like { "error": "...", "project_id": "..." }
	FString ErrorField;
	if (Obj->TryGetStringField(TEXT("error"), ErrorField) && !ErrorField.IsEmpty())
	{
		Out.bValid       = false;
		Out.ErrorMessage = ErrorField;
		Obj->TryGetStringField(TEXT("project_id"), Out.ProjectId);
		return false;
	}

	double Tmp = 0.0;
	if (Obj->TryGetNumberField(TEXT("overall_score"), Tmp))
	{
		Out.OverallScore = static_cast<float>(Tmp);
	}
	else
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Cat = nullptr;
	if (Obj->TryGetObjectField(TEXT("category_scores"), Cat) && Cat)
	{
		double V = 100.0;
		if ((*Cat)->TryGetNumberField(TEXT("performance"),     V)) Out.PerformanceScore     = static_cast<float>(V);
		if ((*Cat)->TryGetNumberField(TEXT("security"),        V)) Out.SecurityScore        = static_cast<float>(V);
		if ((*Cat)->TryGetNumberField(TEXT("best_practices"),  V)) Out.BestPracticesScore   = static_cast<float>(V);
		if ((*Cat)->TryGetNumberField(TEXT("maintainability"), V)) Out.MaintainabilityScore = static_cast<float>(V);
		if ((*Cat)->TryGetNumberField(TEXT("naming"),          V)) Out.NamingScore          = static_cast<float>(V);
	}

	const TSharedPtr<FJsonObject>* Counts = nullptr;
	if (Obj->TryGetObjectField(TEXT("issue_counts"), Counts) && Counts)
	{
		(*Counts)->TryGetNumberField(TEXT("errors"),   Out.Errors);
		(*Counts)->TryGetNumberField(TEXT("warnings"), Out.Warnings);
		(*Counts)->TryGetNumberField(TEXT("infos"),    Out.Infos);
	}

	Obj->TryGetNumberField(TEXT("files_scanned"), Out.FilesScanned);
	Obj->TryGetNumberField(TEXT("total_issues"),  Out.TotalIssues);
	if (Obj->TryGetNumberField(TEXT("total_penalty"), Tmp))
	{
		Out.TotalPenalty = static_cast<float>(Tmp);
	}

	Obj->TryGetStringField(TEXT("project_id"), Out.ProjectId);
	Obj->TryGetStringField(TEXT("timestamp"),  Out.Timestamp);
	Obj->TryGetStringField(TEXT("scan_type"),  Out.ScanType);
	Obj->TryGetStringField(TEXT("tier"),       Out.Tier);

	Out.bValid = true;
	return true;
}

FShintQualityScoreSnapshot FShintCoreClient::ParseLatestScoreResponse(const FShintRequestResult& Raw)
{
	FShintQualityScoreSnapshot Out;
	Out.StatusCode = Raw.StatusCode;

	if (!Raw.bSuccess)
	{
		Out.ErrorMessage = Raw.ErrorMessage;
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid())
	{
		Out.ErrorMessage = TEXT("Failed to parse score response.");
		return Out;
	}

	ParseScoreObject(Root, Out);
	return Out;
}

FShintQualityScoreHistory FShintCoreClient::ParseScoreHistoryResponse(const FShintRequestResult& Raw)
{
	FShintQualityScoreHistory Out;
	Out.StatusCode = Raw.StatusCode;

	if (!Raw.bSuccess)
	{
		Out.ErrorMessage = Raw.ErrorMessage;
		return Out;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Raw.ResponseBody);
	if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid())
	{
		Out.ErrorMessage = TEXT("Failed to parse score history response.");
		return Out;
	}

	Root->TryGetStringField(TEXT("project_id"), Out.ProjectId);

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("scores"), Arr) && Arr)
	{
		Out.Scores.Reserve(Arr->Num());
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O) || !O || !O->IsValid()) continue;
			FShintQualityScoreSnapshot Snap;
			if (ParseScoreObject(*O, Snap))
			{
				Out.Scores.Add(MoveTemp(Snap));
			}
		}
	}

	Out.bValid = true;
	return Out;
}

void FShintCoreClient::GetLatestQualityScore(
	const FString& ProjectId, FOnShintQualityScoreComplete OnComplete)
{
	if (ProjectId.IsEmpty())
	{
		FShintQualityScoreSnapshot Empty;
		Empty.ErrorMessage = TEXT("project_id is empty (set it in shinttools.config.json)");
		OnComplete.ExecuteIfBound(Empty);
		return;
	}

	const FString Url = FString::Printf(
		TEXT("%s/metrics/score/latest?project_id=%s"),
		*Config.GetBaseUrl(),
		*FGenericPlatformHttp::UrlEncode(ProjectId));

	SendRequest(Url, EShintHttpMethod::GET, TEXT(""),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw)
			{
				OnComplete.ExecuteIfBound(ParseLatestScoreResponse(Raw));
			}));
}

void FShintCoreClient::GetQualityScoreHistory(
	const FString& ProjectId, int32 Limit,
	FOnShintQualityScoreHistoryComplete OnComplete)
{
	if (ProjectId.IsEmpty())
	{
		FShintQualityScoreHistory Empty;
		Empty.ErrorMessage = TEXT("project_id is empty (set it in shinttools.config.json)");
		OnComplete.ExecuteIfBound(Empty);
		return;
	}
	const int32 ClampedLimit = FMath::Clamp(Limit, 1, 100);
	const FString Url = FString::Printf(
		TEXT("%s/metrics/score/history?project_id=%s&limit=%d"),
		*Config.GetBaseUrl(),
		*FGenericPlatformHttp::UrlEncode(ProjectId),
		ClampedLimit);

	SendRequest(Url, EShintHttpMethod::GET, TEXT(""),
		FOnShintRequestComplete::CreateLambda(
			[OnComplete](const FShintRequestResult& Raw)
			{
				OnComplete.ExecuteIfBound(ParseScoreHistoryResponse(Raw));
			}));
}
