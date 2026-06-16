// Copyright 2026 ShintTools. All Rights Reserved.
//
// ─────────────────────────────────────────────────────────────────────────────
//  ShintIconStyle.h — Slate style set for ShintTools' custom SVG icon library.
//
//  Registers the white vector glyphs under Resources/Icons/*.svg as named
//  brushes so the panel renders ShintTools' own iconography instead of the
//  native editor (Starship) glyphs. The brushes are white masks; callers tint
//  them via SImage::ColorAndOpacity (the sidebar binds the active/muted color).
//
//  Lifecycle: Initialize() at module StartupModule, Shutdown() at
//  ShutdownModule. Header-light so widgets only pull in what they use.
//
//  Usage:
//      SImage().Image(FShintIconStyle::GetBrush("ShintTools.Icons.Info"))
//      FSlateIcon(FShintIconStyle::GetStyleSetName(), "ShintTools.Icons.UI")
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;
class ISlateStyle;
class FSlateStyleSet;

class FShintIconStyle
{
public:
	/** Create + register the style set. Idempotent; call from StartupModule. */
	static void Initialize();

	/** Unregister + destroy the style set. Call from ShutdownModule. */
	static void Shutdown();

	/** Style-set name — pass to FSlateIcon(GetStyleSetName(), "ShintTools.Icons.X"). */
	static FName GetStyleSetName();

	/** The registered style set (valid only between Initialize and Shutdown). */
	static const ISlateStyle& Get();

	/** Convenience: brush for an icon key, e.g. "ShintTools.Icons.Info". */
	static const FSlateBrush* GetBrush(const FName& Name);

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
