// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintWelcomeDialog.h"
#include "ShintTools.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SShintWelcomeDialog"

namespace
{
	FString WelcomeFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("ShintTools"), TEXT("welcome.txt"));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Static API
// ─────────────────────────────────────────────────────────────────────────────

bool SShintWelcomeDialog::HasBeenShown()
{
	return FPaths::FileExists(WelcomeFilePath());
}

void SShintWelcomeDialog::MarkShown()
{
	const FString Path = WelcomeFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/ true);
	const FString Body = FString::Printf(
		TEXT("SHOWN %s\n"), *FDateTime::UtcNow().ToIso8601());
	FFileHelper::SaveStringToFile(Body, *Path);
}

void SShintWelcomeDialog::MaybeShowForTier(const FString& Tier)
{
	// Shown once per machine/project, for every tier including Free. Called on
	// every panel open, so the HasBeenShown() guard is what keeps it to one
	// appearance. The body text below adapts to the tier (Free gets its own
	// copy + an upgrade CTA).
	if (HasBeenShown())
	{
		return;
	}

	check(IsInGameThread());

	const TSharedRef<SWindow> Window =
		SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Welcome to ShintTools"))
		.ClientSize(FVector2D(560.f, 420.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	const TSharedRef<SShintWelcomeDialog> Content =
		SNew(SShintWelcomeDialog).Tier(Tier);
	Content->ParentWindow = Window;
	Window->SetContent(Content);

	// Non-modal: a welcome shouldn't block the editor, and AddWindow needs no
	// active parent (AddModalWindow would, which can be absent right after the
	// tab spawns).
	FSlateApplication::Get().AddWindow(Window);

	// Persist immediately so closing via the window 'X' (not just Got it) still
	// counts as shown and it never reappears.
	MarkShown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintWelcomeDialog::Construct(const FArguments& InArgs)
{
	Tier = InArgs._Tier;

	const bool bStudioPlus =
		Tier.ToLower() == TEXT("studio") || Tier.ToLower() == TEXT("enterprise");

	// Tier label with a capitalised first letter for the heading.
	FString TierLabel = Tier;
	if (!TierLabel.IsEmpty())
	{
		TierLabel = TierLabel.Left(1).ToUpper() + TierLabel.RightChop(1).ToLower();
	}

	const FText Heading = FText::FromString(
		FString::Printf(TEXT("Welcome — ShintTools %s"), *TierLabel));

	// What the tier unlocks + how to start. Free gets its own copy (the free
	// edition is rule-capped, has no license "activation", and ends on an
	// upgrade CTA); paid tiers describe the full unlock. Studio adds Phase-2.
	const bool bIsFree = Tier.IsEmpty() || Tier.ToLower() == TEXT("free");

	FString BodyStr;
	if (bIsFree)
	{
		BodyStr += TEXT("Thanks for installing ShintTools Free. You get:\n\n");
		BodyStr += TEXT("  - Code Validator — core C++ and Blueprint rules with one-click Auto-Fix.\n");
		BodyStr += TEXT("  - Asset Naming Bot — project-wide naming audit + safe rename.\n");
		BodyStr += TEXT("\nGetting started:\n");
		BodyStr += TEXT("  1. Open Window > ShintTools to dock the panel.\n");
		BodyStr += TEXT("  2. Pick a module and click Scan.\n");
		BodyStr += TEXT("  3. Click \"Fix all\" on the findings to apply them.\n\n");
		BodyStr += TEXT("The local Core Engine does the analysis on your machine; nothing leaves it.\n\n");
		BodyStr += TEXT("Upgrade to Indie or Studio for the full rule set, AI Explain, the\n");
		BodyStr += TEXT("LOD Auditor and more:  https://shint.tools/pricing");
	}
	else
	{
		BodyStr += TEXT("Thanks for activating ShintTools. Your license unlocks:\n\n");
		BodyStr += TEXT("  - Deep Code Validator (full edition) — every C++ and Blueprint rule\n");
		BodyStr += TEXT("    and one-click Auto-Fix.\n");
		// [AGENT-STRIP-BEGIN]
		BodyStr += TEXT("  - AI \"Explain\" — plain-language rationale on any finding.\n");
		// [AGENT-STRIP-END]
		BodyStr += TEXT("  - Asset Naming Bot (full edition) — project-wide naming audit + rename.\n");
		if (bStudioPlus)
		{
			BodyStr += TEXT("  - LOD Auditor — mesh/texture/material optimisation findings.\n");
			BodyStr += TEXT("  - Predictive Profiler — early-warning performance hints.\n");
		}
		BodyStr += TEXT("\nGetting started:\n");
		BodyStr += TEXT("  1. Open Window > ShintTools to dock the panel.\n");
		BodyStr += TEXT("  2. Pick a module and click Scan.\n");
		BodyStr += TEXT("  3. Click Apply on a finding to fix it");
		// [AGENT-STRIP-BEGIN]
		BodyStr += TEXT(", or Explain for a plain-language rationale");
		// [AGENT-STRIP-END]
		BodyStr += TEXT(".\n\n");
		BodyStr += TEXT("The local Core Engine does the analysis on your machine; nothing leaves\n");
		BodyStr += TEXT("it");
		// [DASH-STRIP-BEGIN]
		BodyStr += TEXT(" unless you click \"Send to Dashboard\"");
		// [DASH-STRIP-END]
		BodyStr += TEXT(".");
	}

	const FText Body = FText::FromString(BodyStr);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(Heading)
				.Font(FAppStyle::GetFontStyle("HeadingSmall"))
			]

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(STextBlock)
				.Text(Body)
				.AutoWrapText(true)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
				.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("GotIt", "Got it"))
				.OnClicked(this, &SShintWelcomeDialog::OnDismissClicked)
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintWelcomeDialog::OnDismissClicked()
{
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
