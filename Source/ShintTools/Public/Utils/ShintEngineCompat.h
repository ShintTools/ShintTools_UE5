// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"

#define SHINT_UE_AT_LEAST(Major, Minor)                          \
	(ENGINE_MAJOR_VERSION > (Major) ||                            \
	 (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

#define SHINT_HTTP_HAS_ACTIVITY_TIMEOUT SHINT_UE_AT_LEAST(5, 4)

#define SHINT_HTTP_HAS_RECEIVE_STREAM   SHINT_UE_AT_LEAST(5, 3)

#define SHINT_DIALOG_TITLE_BY_REF       SHINT_UE_AT_LEAST(5, 3)

namespace ShintCompat
{

template <typename TRequestPtr>
FORCEINLINE void SetActivityTimeout(const TRequestPtr& Request, float TimeoutSecs)
{
#if SHINT_HTTP_HAS_ACTIVITY_TIMEOUT
	Request->SetActivityTimeout(TimeoutSecs);
#else
	(void)Request;
	(void)TimeoutSecs;
#endif
}

template <typename TRequestPtr, typename TStreamRef>
FORCEINLINE bool SetResponseBodyReceiveStream(const TRequestPtr& Request, const TStreamRef& Stream)
{
#if SHINT_HTTP_HAS_RECEIVE_STREAM
	return Request->SetResponseBodyReceiveStream(Stream);
#else
	(void)Request;
	(void)Stream;
	return false;
#endif
}

FORCEINLINE EAppReturnType::Type OpenDialog(
	EAppMsgType::Type MessageType, const FText& Message, const FText& Title)
{
#if SHINT_DIALOG_TITLE_BY_REF
	return FMessageDialog::Open(MessageType, Message, Title);
#else
	return FMessageDialog::Open(MessageType, Message, &Title);
#endif
}

}
