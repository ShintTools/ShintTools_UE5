// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintCoreClient.h"
#include "ShintTools.h"
#include "ShintEngineCompat.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FShintSseParser : public FArchive
{
public:
	explicit FShintSseParser(TFunction<void(FString)> InChunkSink)
		: ChunkSink(MoveTemp(InChunkSink))
	{
		SetIsSaving(true);
	}

	virtual void Serialize(void* Data, int64 Length) override
	{
		if (!Data || Length <= 0) return;
		const uint8* Bytes = static_cast<const uint8*>(Data);
		Raw.Append(Bytes, static_cast<int32>(Length));
		Pending.Append(Bytes, static_cast<int32>(Length));
		DrainEvents();
	}

	virtual FString GetArchiveName() const override { return TEXT("FShintSseParser"); }

	FString FullText;
	FString ErrorText;
	float   GenSeconds = 0.f;
	bool    bDone = false;

	void ParseBufferedBody(const TArray<uint8>& Body)
	{
		ChunkSink = nullptr;
		if (Body.Num() > 0)
		{
			Serialize(const_cast<uint8*>(Body.GetData()), Body.Num());
		}
	}

	FString RawBody() const
	{
		if (Raw.Num() == 0) return FString();
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Raw.GetData()), Raw.Num());
		return FString(Conv.Length(), Conv.Get());
	}

private:
	void DrainEvents()
	{
		int32 Start = 0;
		for (int32 i = 0; i + 1 < Pending.Num(); ++i)
		{
			if (Pending[i] == 0x0A && Pending[i + 1] == 0x0A)
			{
				HandleEvent(Pending.GetData() + Start, i - Start);
				Start = i + 2;
				++i;
			}
		}
		if (Start > 0)
		{

			Pending = TArray<uint8>(Pending.GetData() + Start, Pending.Num() - Start);
		}
	}

	void HandleEvent(const uint8* EvtBytes, int32 EvtLen)
	{
		if (EvtLen <= 0) return;
		FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(EvtBytes), EvtLen);
		FString Line(Conv.Length(), Conv.Get());
		Line.TrimStartAndEndInline();
		if (!Line.StartsWith(TEXT("data:"))) return;

		FString Json = Line.RightChop(5);
		Json.TrimStartAndEndInline();
		if (Json.IsEmpty()) return;

		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return;

		FString Chunk;
		if (Obj->TryGetStringField(TEXT("chunk"), Chunk) && !Chunk.IsEmpty())
		{
			FullText += Chunk;
			if (ChunkSink) ChunkSink(Chunk);
			return;
		}
		FString Err;
		if (Obj->TryGetStringField(TEXT("error"), Err))
		{
			if (ErrorText.IsEmpty()) ErrorText = Err;
			return;
		}
		bool bDoneField = false;
		if (Obj->TryGetBoolField(TEXT("done"), bDoneField) && bDoneField)
		{
			bDone = true;
			FString ServerFull;
			if (Obj->TryGetStringField(TEXT("full_text"), ServerFull) && !ServerFull.IsEmpty())
			{
				FullText = ServerFull;
			}
			double G = 0.0;
			if (Obj->TryGetNumberField(TEXT("generation_seconds"), G))
			{
				GenSeconds = static_cast<float>(G);
			}
		}
	}

	TArray<uint8> Raw;
	TArray<uint8> Pending;
	TFunction<void(FString)> ChunkSink;
};

FShintCoreClient::FShintCoreClient()  { LoadConfig(); }
FShintCoreClient::~FShintCoreClient() {}

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

	if (Json->TryGetStringField(TEXT("api_key"),       S)) Config.ApiKeyDashboard = S;
	if (Json->TryGetStringField(TEXT("api_key_mongo"), S)) Config.ApiKeyMongo    = S;
	if (Json->TryGetStringField(TEXT("session_token"), S)) Config.SessionToken   = S;
	if (Json->TryGetStringField(TEXT("dashboard_url"), S)) Config.DashboardUrl   = S;
	if (Json->TryGetStringField(TEXT("export_path"),   S)) Config.ExportPath     = S;

	Config.ExcludedPaths.Reset();
	const TArray<TSharedPtr<FJsonValue>>* ExcArr = nullptr;
	if (Json->TryGetArrayField(TEXT("excluded_paths"), ExcArr) && ExcArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ExcArr)
		{
			FString Entry;
			if (V->TryGetString(Entry) && !Entry.IsEmpty())
				Config.ExcludedPaths.Add(MoveTemp(Entry));
		}
	}
	else
	{
		FString ExcRaw;
		if (Json->TryGetStringField(TEXT("excluded_paths"), ExcRaw))
		{
			ExcRaw.ParseIntoArray(Config.ExcludedPaths, TEXT("\n"), true);
		}
	}

	if (Config.DashboardUrl.IsEmpty()
		|| Config.DashboardUrl.Contains(TEXT("app.shinttools.io")))
	{
		Config.DashboardUrl = TEXT("https://shint.tools");
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintCoreClient: migrated dashboard_url to shint.tools"));
	}

	Config.DashboardUrl.TrimStartAndEndInline();
	Config.DashboardUrl.RemoveFromEnd(TEXT("/"));
	if (Config.DashboardUrl.EndsWith(TEXT("/dashboard")))
	{
		Config.DashboardUrl.LeftChopInline(10);
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintCoreClient: stripped /dashboard suffix from dashboard_url"));
	}

	UE_LOG(LogShintTools, Verbose,
		TEXT("ShintCoreClient: Config loaded. Port=%d"), Config.CorePort);
	return true;
}

bool FShintCoreClient::SaveConfig() const
{
	const FString CfgPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("shinttools.config.json"));

	TSharedPtr<FJsonObject> Json;
	FString Raw;
	if (FFileHelper::LoadFileToString(Raw, *CfgPath))
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		FJsonSerializer::Deserialize(R, Json);
	}
	if (!Json.IsValid()) Json = MakeShared<FJsonObject>();

	Json->SetStringField(TEXT("core_host"),       Config.CoreHost);
	Json->SetNumberField(TEXT("core_port"),       Config.CorePort);
	Json->SetBoolField(TEXT("auto_start_core"),   Config.bAutoStartCore);
	Json->SetStringField(TEXT("project_name"),    Config.ProjectName);
	Json->SetStringField(TEXT("api_key"),         Config.ApiKeyDashboard);
	Json->SetStringField(TEXT("api_key_mongo"),   Config.ApiKeyMongo);
	Json->SetStringField(TEXT("dashboard_url"),   Config.DashboardUrl);
	Json->SetStringField(TEXT("export_path"),     Config.ExportPath);

	TArray<TSharedPtr<FJsonValue>> ExcArr;
	ExcArr.Reserve(Config.ExcludedPaths.Num());
	for (const FString& P : Config.ExcludedPaths)
		ExcArr.Add(MakeShared<FJsonValueString>(P));
	Json->SetArrayField(TEXT("excluded_paths"), ExcArr);

	const FString Out = SerializeJson(Json.ToSharedRef());
	return FFileHelper::SaveStringToFile(Out, *CfgPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FShintCoreClient::CheckHealth(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/health"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

void FShintCoreClient::Ping(FOnShintRequestComplete OnComplete)
{
	SendRequest(Config.GetBaseUrl() + TEXT("/ping"), EShintHttpMethod::GET, TEXT(""), OnComplete);
}

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

	Req->OnProcessRequestComplete().BindSP(
		AsShared(), &FShintCoreClient::OnHttpRequestComplete, OnComplete);

	float TimeoutSecs = 90.0f;
	Req->SetTimeout(TimeoutSecs);

	ShintCompat::SetActivityTimeout(Req, TimeoutSecs);

	if (!Req->ProcessRequest())
	{
		FShintRequestResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		OnComplete.ExecuteIfBound(Err);
	}
}

void FShintCoreClient::SendRequestStream(
	const FString& FullUrl, EShintHttpMethod Method, const FString& Body,
	FOnShintStreamChunk OnChunk, FOnShintRequestComplete OnComplete,
	const TMap<FString, FString>& ExtraHeaders)
{
	UE_LOG(LogShintTools, Verbose, TEXT("ShintCoreClient (stream): %s %s"),
		*MethodToString(Method), *FullUrl);

	FHttpModule& Http = FHttpModule::Get();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = Http.CreateRequest();

	Req->SetURL(FullUrl);
	Req->SetVerb(MethodToString(Method));
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Req->SetHeader(TEXT("Accept"),       TEXT("text/event-stream"));
	Req->SetHeader(TEXT("User-Agent"),   TEXT("ShintTools-UE5/1.1"));

	for (const auto& KV : ExtraHeaders)
		Req->SetHeader(KV.Key, KV.Value);

	if (!Body.IsEmpty() &&
	    (Method == EShintHttpMethod::POST || Method == EShintHttpMethod::PUT))
	{
		Req->SetContentAsString(Body);
	}

	TSharedRef<FShintSseParser> Parser = MakeShared<FShintSseParser>(
		[OnChunk](FString Chunk)
		{
			AsyncTask(ENamedThreads::GameThread,
				[OnChunk, Chunk = MoveTemp(Chunk)]()
				{
					OnChunk.ExecuteIfBound(Chunk);
				});
		});

	ShintCompat::SetResponseBodyReceiveStream(Req, Parser);

	Req->OnProcessRequestComplete().BindSP(
		AsShared(), &FShintCoreClient::OnHttpStreamComplete, Parser, OnComplete);

	Req->SetTimeout(180.0f);
	ShintCompat::SetActivityTimeout(Req, 180.0f);

	if (!Req->ProcessRequest())
	{
		FShintRequestResult Err;
		Err.bSuccess     = false;
		Err.ErrorMessage = TEXT("Failed to dispatch streaming HTTP request.");
		OnComplete.ExecuteIfBound(Err);
	}
}

void FShintCoreClient::OnHttpStreamComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bConnectedSuccessfully, TSharedRef<FShintSseParser> Parser,
	FOnShintRequestComplete OnComplete)
{
	FShintRequestResult Result;

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		Result.bSuccess = false;
		const FString TriedUrl = Request.IsValid() ? Request->GetURL() : FString();
		Result.ErrorMessage = FString::Printf(
			TEXT("Connection failed — could not reach Core Engine at %s."), *TriedUrl);
		OnComplete.ExecuteIfBound(Result);
		return;
	}

#if !SHINT_HTTP_HAS_RECEIVE_STREAM

	Parser->ParseBufferedBody(Response->GetContent());
#endif

	Result.StatusCode = Response->GetResponseCode();
	const bool bHttpOk = (Result.StatusCode >= 200 && Result.StatusCode < 300);
	if (!bHttpOk)
	{

		Result.bSuccess     = false;
		Result.ResponseBody = Parser->RawBody();
		Result.ErrorMessage = FString::Printf(TEXT("HTTP %d"), Result.StatusCode);
		OnComplete.ExecuteIfBound(Result);
		return;
	}

	if (!Parser->ErrorText.IsEmpty())
	{
		Result.bSuccess     = false;
		Result.ErrorMessage = Parser->ErrorText;
		Result.ResponseBody = Parser->FullText;
	}
	else
	{
		Result.bSuccess     = !Parser->FullText.IsEmpty();
		Result.ResponseBody = Parser->FullText;
		if (!Result.bSuccess)
			Result.ErrorMessage = TEXT("Stream ended with no text.");
	}
	OnComplete.ExecuteIfBound(Result);
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
