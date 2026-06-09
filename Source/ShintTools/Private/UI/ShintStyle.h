// Copyright 2026 ShintTools. All Rights Reserved.
//
// ─────────────────────────────────────────────────────────────────────────────
//  ShintStyle.h — design tokens for the ShintTools editor panel.
//
//  Centralizes the colors, typography, spacing and radii that the panel and
//  its shared widgets render with. Mirrors the launcher's palette in
//  app/constants.py so the editor feels visually continuous with the
//  ShintTools Launcher (a "web-dashboard" look).
//
//  Header-only on purpose: every accessor returns a value type (FLinearColor /
//  float / FSlateFontInfo). No global Slate style set is needed, and there's
//  no link-time singleton to keep alive across module reloads.
//
//  Usage:
//      #include "ShintStyle.h"
//      ...
//      .BorderBackgroundColor(FShintStyle::Colors::BgCard())
//      .Padding(FShintStyle::Space::S4)
//      .Font(FShintStyle::Fonts::H2())
//
//  Notes:
//   * Colors are sRGB hex from the launcher palette (constants.py),
//     converted to FLinearColor at construction (UE handles the gamma curve).
//   * Bahnschrift is the primary face on Windows; if the OS lacks it we fall
//     back to UE's built-in Sans via FCoreStyle::GetDefaultFontStyle().
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

class FShintStyle
{
public:

	// ── Colors ───────────────────────────────────────────────────────────────
	// All values mirror app/constants.py so launcher and plugin share one
	// palette. Building from sRGB → linear is done once per call; no caching
	// needed because callers feed these into Slate attributes that copy.
	struct Colors
	{
		static FLinearColor Bg()             { return FromHex(0x000000); } // canvas
		static FLinearColor BgCard()         { return FromHex(0x161616); } // surfaces
		static FLinearColor BgCardHover()    { return FromHex(0x202020); }
		static FLinearColor BgTopbar()       { return FromHex(0x0d0d0d); }
		static FLinearColor BgSidebar()      { return FromHex(0x000000); }

		static FLinearColor BorderSubtle()   { return FromHex(0x2a2a2a); }
		static FLinearColor BorderStrong()   { return FromHex(0x3a3a3a); }

		static FLinearColor TextPrimary()    { return FromHex(0xffffff); }
		static FLinearColor TextMuted()      { return FromHex(0x999999); }
		static FLinearColor TextFaint()      { return FromHex(0x3a3a3a); }

		// Severity ramp — matches launcher: critical red, high orange,
		// medium grey, low green. Same hexes the dashboard uses.
		static FLinearColor SevCritical()    { return FromHex(0xef4444); }
		static FLinearColor SevHigh()        { return FromHex(0xf97316); }
		static FLinearColor SevMedium()      { return FromHex(0xa1a1aa); }
		static FLinearColor SevLow()         { return FromHex(0x22c55e); }

		// Status / accent. Blue is preserved for compatibility with existing
		// connection-LED code; the redesigned UI prefers severity-driven cues.
		static FLinearColor Success()        { return FromHex(0x22c55e); }
		static FLinearColor Warning()        { return FromHex(0xf97316); }
		static FLinearColor Error()          { return FromHex(0xef4444); }
		static FLinearColor AccentBlue()     { return FromHex(0x3b82f6); }
		static FLinearColor AccentBlueDim()  { return FromHex(0x2563eb); }

		// Map a severity string ("critical"/"error" / "high"/"warning" / ...)
		// to a color. Used by SShintSeverityBadge so callers pass the raw
		// server severity without a lookup. Unknown values fall through to
		// medium so the UI never renders an invisible badge.
		static FLinearColor FromSeverity(const FString& Severity)
		{
			const FString S = Severity.ToLower();
			if (S == TEXT("critical") || S == TEXT("error"))   return SevCritical();
			if (S == TEXT("high")     || S == TEXT("warning")) return SevHigh();
			if (S == TEXT("low")      || S == TEXT("info"))    return SevLow();
			return SevMedium();
		}

	private:
		// 0xRRGGBB → FLinearColor (sRGB-space). FColor::FromHex would also work
		// but requires a string allocation; this path keeps token lookups
		// allocation-free.
		static FLinearColor FromHex(uint32 RGB)
		{
			const uint8 R = (RGB >> 16) & 0xff;
			const uint8 G = (RGB >>  8) & 0xff;
			const uint8 B =  RGB        & 0xff;
			return FLinearColor(FColor(R, G, B, 0xff));
		}
	};

	// ── Spacing scale ────────────────────────────────────────────────────────
	// 4 / 8 / 12 / 16 / 24 / 32 — used as FMargin padding and SBox slot gaps.
	// Single source of truth so sections feel rhythmically consistent.
	struct Space
	{
		static constexpr float S1 =  4.f;
		static constexpr float S2 =  8.f;
		static constexpr float S3 = 12.f;
		static constexpr float S4 = 16.f;
		static constexpr float S5 = 24.f;
		static constexpr float S6 = 32.f;
	};

	// ── Radii ────────────────────────────────────────────────────────────────
	struct Radius
	{
		static constexpr float Control =  4.f; // buttons, badges, inputs
		static constexpr float Card    =  8.f; // panels, cards, list rows
		static constexpr float Modal   = 12.f; // dialogs, popovers
	};

	// ── Typography ───────────────────────────────────────────────────────────
	// Bahnschrift on Windows; fallback to UE's bundled Roboto elsewhere. The
	// font is loaded once from C:/Windows/Fonts/bahnschrift.ttf if present —
	// it ships with every Windows 10+ system, which is the only platform UE5
	// editor targets, so the fallback path is for unusual cases (server-class
	// Win Server SKUs without the Fonts folder, custom UE engine builds with
	// stripped resources).
	//
	// Bahnschrift is technically a variable font; FreeType (Slate's backend)
	// reads it as the regular axis. For "Bold" we re-use the regular face at
	// the same size — Slate's text shaper does synthetic emboldening when the
	// font asset itself doesn't expose a Bold typeface, which is acceptable
	// for our header sizes (16px+ remains crisp).
	struct Fonts
	{
		// 24px — section headers, KPI value
		static FSlateFontInfo H1()      { return Make(24); }
		// 16px — card titles
		static FSlateFontInfo H2()      { return Make(16); }
		// 14px — list-row primary text
		static FSlateFontInfo Body()    { return Make(14); }
		// 12px — labels, secondary text
		static FSlateFontInfo Small()   { return Make(12); }
		// 10px — captions, badges, footnotes
		static FSlateFontInfo Caption() { return Make(10); }

	private:
		// Resolve Bahnschrift path lazily, once. Returning the cached path lets
		// every Make() call avoid an FFileManager check after the first hit.
		static const FString& BahnschriftPath()
		{
			static const FString Cached = []()
			{
				const FString Path = TEXT("C:/Windows/Fonts/bahnschrift.ttf");
				return FPaths::FileExists(Path) ? Path : FString();
			}();
			return Cached;
		}

		static FSlateFontInfo Make(int32 Size)
		{
			const FString& Path = BahnschriftPath();
			if (!Path.IsEmpty())
			{
				// FSlateFontInfo built from a TTF path is supported by Slate's
				// FreeType backend without registering a composite font asset.
				// The (path, size) constructor is deprecated in UE 5.7 but still
				// functional; suppress the deprecation rather than pull in a
				// composite-font asset just to set a branding font.
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				return FSlateFontInfo(Path, Size);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
			// Fallback — Engine/Content/Slate/Fonts/Roboto-Regular.ttf is
			// guaranteed to exist on every UE5 install.
			return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
		}
	};
};
