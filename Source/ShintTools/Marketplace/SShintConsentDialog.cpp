// Copyright ShintTools. All Rights Reserved.

#include "SShintConsentDialog.h"
#include "ShintTools/ShintTools.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SShintConsentDialog"

namespace
{
	FString ConsentFilePath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("ShintTools"), TEXT("consent.txt"));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Static API
// ─────────────────────────────────────────────────────────────────────────────

bool SShintConsentDialog::HasUserConsented()
{
	FString Body;
	return FFileHelper::LoadFileToString(Body, *ConsentFilePath())
		&& Body.Contains(TEXT("ACCEPTED"));
}

void SShintConsentDialog::OpenModal(TFunction<void()> OnAccept)
{
	check(IsInGameThread());

	const TSharedRef<SWindow> Window =
		SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "ShintTools - Privacy Notice"))
		.ClientSize(FVector2D(560.f, 400.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	const TSharedRef<SShintConsentDialog> Content =
		SNew(SShintConsentDialog);
	Content->ParentWindow    = Window;
	Content->AcceptCallback  = MoveTemp(OnAccept);
	Window->SetContent(Content);

	FSlateApplication::Get().AddModalWindow(Window,
		FSlateApplication::Get().GetActiveTopLevelWindow());
}

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SShintConsentDialog::Construct(const FArguments& InArgs)
{
	const FText Body = LOCTEXT("ConsentBody",
		"ShintTools requires a local Core Engine to operate.\n\n"
		"To set up the Core Engine, ShintTools will:\n"
		"  - Detect Docker Desktop on this machine.\n"
		"  - Download a Docker image (~600 MB) from ghcr.io.\n"
		"  - Start a container named 'shinttools-core'.\n"
		"  - Bind port 18200 on localhost so the editor plugin\n"
		"    can talk to it.\n\n"
		"What stays local: your project assets, source code, and\n"
		"scan results never leave your machine. The Core Engine\n"
		"runs entirely in the Docker container on localhost.\n\n"
		"What we contact remotely: only ghcr.io (to download the\n"
		"image) and shinttools-api.com (to validate your license\n"
		"tier, if you provide an API key).\n\n"
		"By clicking Accept you agree to download and run the\n"
		"Core Engine container.");

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
				.Text(LOCTEXT("Heading", "First-time setup"))
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
					.Text(LOCTEXT("Decline", "Decline"))
					.OnClicked(this, &SShintConsentDialog::OnDeclineClicked)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Accept", "Accept and continue"))
					.OnClicked(this, &SShintConsentDialog::OnAcceptClicked)
				]
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintConsentDialog::OnAcceptClicked()
{
	PersistAcceptance();
	const TFunction<void()> Cb = AcceptCallback;
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	if (Cb)
	{
		Cb();
	}
	return FReply::Handled();
}

FReply SShintConsentDialog::OnDeclineClicked()
{
	UE_LOG(LogShintTools, Display,
		TEXT("[Consent] user declined Core Engine setup."));
	if (ParentWindow.IsValid())
	{
		ParentWindow->RequestDestroyWindow();
	}
	return FReply::Handled();
}

void SShintConsentDialog::PersistAcceptance()
{
	const FString Path = ConsentFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),
		/*Tree=*/ true);
	const FString Body = FString::Printf(
		TEXT("ACCEPTED %s\n"), *FDateTime::UtcNow().ToIso8601());
	FFileHelper::SaveStringToFile(Body, *Path);
}

#undef LOCTEXT_NAMESPACE
