// Copyright 2026 ShintTools. All Rights Reserved.

#include "ShintIconStyle.h"

#include "Interfaces/IPluginManager.h"  // also declares the IPlugin interface
#include "Styling/SlateStyle.h"   // FSlateStyleSet (UE5.7 removed Styling/SlateStyleSet.h)
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Misc/Paths.h"

// IMAGE_BRUSH_SVG (SlateStyleMacros.h) resolves the .svg path through a local
// RootToContentDir symbol; bind it to this style set's content root.
#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FShintIconStyle::StyleInstance = nullptr;

void FShintIconStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FShintIconStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		StyleInstance.Reset();
	}
}

FName FShintIconStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("ShintToolsIconStyle"));
	return StyleSetName;
}

const ISlateStyle& FShintIconStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}

const FSlateBrush* FShintIconStyle::GetBrush(const FName& Name)
{
	return StyleInstance.IsValid() ? StyleInstance->GetBrush(Name) : nullptr;
}

TSharedRef<FSlateStyleSet> FShintIconStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	// Resources/Icons under the plugin root holds the white 10x10 SVG glyphs.
	// FindPlugin keys on the .uplugin filename (ShintTools.uplugin).
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ShintTools")))
	{
		Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources/Icons"));
	}

	const FVector2D Icon16(16.0, 16.0);

	// Navigation + tab.
	Style->Set("ShintTools.Icons.Info",     new IMAGE_BRUSH_SVG(TEXT("info"),     Icon16));
	Style->Set("ShintTools.Icons.Search",   new IMAGE_BRUSH_SVG(TEXT("search"),   Icon16));
	Style->Set("ShintTools.Icons.Grid",     new IMAGE_BRUSH_SVG(TEXT("grid"),     Icon16));
	Style->Set("ShintTools.Icons.Settings", new IMAGE_BRUSH_SVG(TEXT("settings"), Icon16));
	Style->Set("ShintTools.Icons.UI",       new IMAGE_BRUSH_SVG(TEXT("ui"),       Icon16));

	// Per-module glyphs (Nieo icon pack v1.0.3 — commercial use, no
	// attribution required). One distinct icon per module: the rail used to
	// reuse "Grid" for both Assets and the LOD Auditor.
	Style->Set("ShintTools.Icons.Code",     new IMAGE_BRUSH_SVG(TEXT("code"),     Icon16));
	Style->Set("ShintTools.Icons.Tag",      new IMAGE_BRUSH_SVG(TEXT("tag"),      Icon16));
	Style->Set("ShintTools.Icons.Optimize", new IMAGE_BRUSH_SVG(TEXT("optimize"), Icon16));
	Style->Set("ShintTools.Icons.Profiler", new IMAGE_BRUSH_SVG(TEXT("profiler"), Icon16));

	// Action buttons.
	Style->Set("ShintTools.Icons.Refresh",  new IMAGE_BRUSH_SVG(TEXT("refresh"),  Icon16));
	Style->Set("ShintTools.Icons.Tick",     new IMAGE_BRUSH_SVG(TEXT("tick"),     Icon16));
	Style->Set("ShintTools.Icons.Cross",    new IMAGE_BRUSH_SVG(TEXT("cross"),    Icon16));
	Style->Set("ShintTools.Icons.Save",     new IMAGE_BRUSH_SVG(TEXT("save"),     Icon16));

	// Assistant dock. Sized at the point of use rather than reused at 16 —
	// the collapsed launcher is a 56px pill, so its glyph has to be bigger
	// than every other icon in the set or it reads as a mis-centred dot.
	Style->Set("ShintTools.Icons.Sparkles", new IMAGE_BRUSH_SVG(TEXT("sparkles"), FVector2D(14.0, 14.0)));
	Style->Set("ShintTools.Icons.Bot",      new IMAGE_BRUSH_SVG(TEXT("bot"),      FVector2D(22.0, 22.0)));
	Style->Set("ShintTools.Icons.Send",     new IMAGE_BRUSH_SVG(TEXT("send"),     Icon16));

	return Style;
}

#undef RootToContentDir
