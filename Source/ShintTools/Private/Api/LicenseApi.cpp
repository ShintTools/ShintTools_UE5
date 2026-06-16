// Copyright 2026 ShintTools. All Rights Reserved.

#include "LicenseApi.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Transport/FShintHttpClient.h"

FShintLicenseApi::FShintLicenseApi(TSharedRef<FShintHttpClient> InTransport,
                                   FString InBaseUrl)
	: Transport(MoveTemp(InTransport))
	, BaseUrl(MoveTemp(InBaseUrl))
{
}

void FShintLicenseApi::RequestStatus(const FString& ApiKey,
                                     FOnShintLicenseStatusComplete OnComplete)
{
	// Body: {"api_key": "..."} — same field name as every other endpoint.
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("api_key"), ApiKey);
	const FString BodyStr = FShintHttpClient::SerializeJson(Body);

	const FString Url = BaseUrl + TEXT("/license/status");

	Transport->Send(
		Url,
		EShintHttpMethod::POST,
		BodyStr,
		FOnShintHttpComplete::CreateLambda(
			[OnComplete](const FShintHttpResult& Raw)
			{
				FShintLicenseStatus Out;
				Out.ElapsedSeconds = 0.f;

				if (!Raw.bSuccess || Raw.ResponseBody.IsEmpty())
				{
					Out.bSuccess     = false;
					Out.Tier         = TEXT("free");
					Out.ErrorMessage = Raw.ErrorMessage.IsEmpty()
						? FString::Printf(TEXT("HTTP %d"), Raw.StatusCode)
						: Raw.ErrorMessage;
					OnComplete.ExecuteIfBound(Out);
					return;
				}

				// Parse {"tier": "...", "error": "...", "time": 0.0}.
				const TSharedRef<TJsonReader<TCHAR>> Reader =
					TJsonReaderFactory<TCHAR>::Create(Raw.ResponseBody);
				TSharedPtr<FJsonObject> Json;
				if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
				{
					Out.bSuccess     = false;
					Out.Tier         = TEXT("free");
					Out.ErrorMessage = TEXT("Malformed /license/status response");
					OnComplete.ExecuteIfBound(Out);
					return;
				}

				Out.Tier           = Json->GetStringField(TEXT("tier"));
				Out.ErrorMessage   = Json->GetStringField(TEXT("error"));
				Out.ElapsedSeconds = static_cast<float>(
					Json->GetNumberField(TEXT("time")));
				Out.bSuccess       = Out.ErrorMessage.IsEmpty();

				// Defensive: blank tier is treated as free, never empty.
				if (Out.Tier.IsEmpty())
				{
					Out.Tier = TEXT("free");
				}

				OnComplete.ExecuteIfBound(Out);
			}));
}
