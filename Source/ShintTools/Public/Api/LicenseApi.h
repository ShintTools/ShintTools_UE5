// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Transport/ShintHttpTypes.h"

class FShintHttpClient;

struct FShintLicenseStatus
{
	FString Tier;
	FString ErrorMessage;
	float   ElapsedSeconds = 0.f;
	bool    bSuccess = false;
};

DECLARE_DELEGATE_OneParam(FOnShintLicenseStatusComplete,
                          const FShintLicenseStatus&);

class FShintLicenseApi
{
public:
	FShintLicenseApi(TSharedRef<FShintHttpClient> InTransport,
	                 FString InBaseUrl);

	void RequestStatus(const FString& ApiKey,
	                   FOnShintLicenseStatusComplete OnComplete);

private:
	TSharedRef<FShintHttpClient> Transport;
	FString BaseUrl;
};
