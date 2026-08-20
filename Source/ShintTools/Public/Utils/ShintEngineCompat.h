// Copyright 2026 ShintTools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"

// ─────────────────────────────────────────────────────────────────────────────
// Engine-version shims
//
// The .uplugin declares EngineVersion 5.2.0 and Fab compiles the submitted
// source against every engine in the supported range, so an API that only
// exists from 5.3 or 5.4 onward is a hard build failure on the older ones —
// not a runtime warning. Fab's 5.2 and 5.3 runs failed on exactly three of
// those; each one is wrapped below so the call site stays readable and there
// is a single place to revisit when the floor moves off 5.2.
//
// Every shim degrades rather than disables: on an engine without the API the
// feature loses fidelity (no live token drip, no per-request activity bound)
// but keeps working.
// ─────────────────────────────────────────────────────────────────────────────

// Spelled out from ENGINE_MAJOR/MINOR_VERSION rather than pulling in
// Misc/EngineVersionComparison.h: Version.h is the one header guaranteed to
// exist and mean the same thing on every engine in the range, and a compat
// header that itself depends on a version-specific include defeats the point.
#define SHINT_UE_AT_LEAST(Major, Minor)                          \
	(ENGINE_MAJOR_VERSION > (Major) ||                            \
	 (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION >= (Minor)))

// IHttpRequest::SetActivityTimeout — added in 5.4.
#define SHINT_HTTP_HAS_ACTIVITY_TIMEOUT SHINT_UE_AT_LEAST(5, 4)

// IHttpRequest::SetResponseBodyReceiveStream — added in 5.3.
#define SHINT_HTTP_HAS_RECEIVE_STREAM   SHINT_UE_AT_LEAST(5, 3)

// FMessageDialog::Open taking the title by reference — added in 5.3, where it
// also deprecated the const FText* overload that 5.2 is limited to. Calling
// the deprecated one unconditionally would build on 5.2 but emit a
// deprecation warning from 5.3 up, and Fab builds warnings-as-errors.
#define SHINT_DIALOG_TITLE_BY_REF       SHINT_UE_AT_LEAST(5, 3)

namespace ShintCompat
{

// Bound how long the request may sit with no bytes moving. SetTimeout caps the
// request as a whole; this is the separate no-activity abort that defaults to
// ~30s and used to cut long silent LLM calls off mid-generation.
//
// Templated on the request pointer so this header does not drag the HTTP
// module into every translation unit that includes it.
//
// Pre-5.4 there is no per-request setter and no runtime override — FHttpModule
// exposes a getter only — so the engine-wide [HTTP] HttpActivityTimeout value
// applies. Streaming keeps bytes flowing token-by-token, so the practical
// exposure there is a cold-model prompt-eval that stays silent past the
// project's configured limit.
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

// Pipe the response body into an FArchive as it arrives, instead of buffering
// the whole thing until completion. Returns false when the engine has no such
// hook (5.2), which tells the caller to parse the buffered body at completion
// instead — same events, just delivered all at once.
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

// Modal message box with a title. Identical behaviour on every version — only
// the way the title is handed over changed.
FORCEINLINE EAppReturnType::Type OpenDialog(
	EAppMsgType::Type MessageType, const FText& Message, const FText& Title)
{
#if SHINT_DIALOG_TITLE_BY_REF
	return FMessageDialog::Open(MessageType, Message, Title);
#else
	return FMessageDialog::Open(MessageType, Message, &Title);
#endif
}

} // namespace ShintCompat
