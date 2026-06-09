// Copyright 2026 ShintTools. All Rights Reserved.
//
// SShintTopBar — slim horizontal bar above the destination switcher.
//
// Anatomy (left → right):
//   • Section title (current destination, big)   • spacer
//   • Connection LED (red/amber/green)
//   • Status text (e.g. "Connected · v1.2.14")
//
// All state is fed via TAttribute so the parent can update it without
// rebuilding the widget. The connection LED is a simple colored circle that
// reads from a bound enum.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

enum class EShintConnState : uint8
{
	Unknown,    // initial state, before first health check
	Connecting, // probing health
	Connected,  // HTTP 200 from /health
	Disconnected,
};

class SShintTopBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShintTopBar)
		: _Title()
		, _StatusText()
		, _TierText()
		, _ConnState(EShintConnState::Unknown)
	{}
		/** Big destination title shown left-aligned. */
		SLATE_ATTRIBUTE(FText, Title)

		/** Free-form status text shown next to the connection LED. */
		SLATE_ATTRIBUTE(FText, StatusText)

		/** License tier label rendered as a pill on the right edge.
		 *  Empty string hides the badge — useful before the /license/status
		 *  probe at StartupModule has resolved. */
		SLATE_ATTRIBUTE(FText, TierText)

		/** Connection state — drives the LED color. */
		SLATE_ATTRIBUTE(EShintConnState, ConnState)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
