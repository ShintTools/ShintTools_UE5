// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintLauncherWelcomeDialog.h"
#include "ShintTools.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SShintLauncherWelcomeDialog"

namespace
{
	// Dashboard entry point Fab users are funnelled to for the launcher /
	// upgrades. Single literal so the URL is trivial to update.
	static const TCHAR* GLauncherUrl = TEXT("https://shint.tools/login");

	FString LauncherWelcomeFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("ShintTools"), TEXT("launcher_welcome.txt"));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Static API
// ─────────────────────────────────────────────────────────────────────────────

bool SShintLauncherWelcomeDialog::HasBeenShown()
{
	return FPaths::FileExists(LauncherWelcomeFilePath());
}

void SShintLauncherWelcomeDialog::MarkShown()
{
	const FString Path = LauncherWelcomeFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/ true);
	const FString Body = FString::Printf(
		TEXT("SHOWN %s\n"), *FDateTime::UtcNow().ToIso8601());
	FFileHelper::SaveStringToFile(Body, *Path);
}

void SShintLauncherWelcomeDialog::MaybeShow()
{
	if (HasBeenShown())
	{
		return;
	}

	check(IsInGameThread());

	const TSharedRef<SWindow> Window =
		SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Welcome to ShintTools"))
		.ClientSize(FVector2D(560.f, 360.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	const TSharedRef<SShintLauncherWelcomeDialog> Content =
		SNew(SShintLauncherWelcomeDialog);
	Content->ParentWindow = Window;
	Window->SetContent(Content);

	// Non-modal (like SShintWelcomeDialog): a welcome shouldn't block the
	// editor, and AddWindow needs no active parent right after the module
	// starts (AddModalWindow would).
	FSlateApplication::Get().AddWindow(Window);

	// Persist immediately so closing via the window 'X' (not just a button)
	// still counts as shown and it never reappears.
	MarkShown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintLauncherWelcomeDialog::Construct(const FArguments& InArgs)
{
	const FText Heading = LOCTEXT("Heading", "Welcome to ShintTools Free");

	const FText Body = LOCTEXT("Body",
		"Thanks for installing ShintTools from Fab. This free edition includes:\n\n"
		"  - Code Validator - core C++ and Blueprint rules with one-click Auto-Fix.\n"
		"  - Asset Naming Bot - project-wide naming audit + safe rename.\n\n"
		"To get full access to the platform, licensing and automatic updates - visit "
		"shint.tools/login and start today. The launcher installs and manages the Core "
		"engine for you and unlocks upgrades.\n\n"
		"Click \"Get the Launcher\" below to open your dashboard.");

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
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Later", "Maybe later"))
					.OnClicked(this, &SShintLauncherWelcomeDialog::OnDismissClicked)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("GetLauncher", "Get the Launcher"))
					.OnClicked(this, &SShintLauncherWelcomeDialog::OnGetLauncherClicked)
				]
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintLauncherWelcomeDialog::OnGetLauncherClicked()
{
	FPlatformProcess::LaunchURL(GLauncherUrl, nullptr, nullptr);
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SShintLauncherWelcomeDialog::OnDismissClicked()
{
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
