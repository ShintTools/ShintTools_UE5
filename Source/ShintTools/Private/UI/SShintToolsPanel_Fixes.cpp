// Copyright 2026 ShintTools. All Rights Reserved.
//
// Apply-fix flows for both the Code Validator and the Asset Naming Bot.
//
// Code path:
//   OnApplySelectedCodeFixesClicked / OnApplySingleFix
//     → Yes/No safety dry-run prompt
//       — No  → ProceedWithCodeFixes directly (user chose to skip)
//       — Yes → CoreClient->CheckFixSafety → OnSafetyCheckComplete, which
//                branches THREE ways on the result, not two:
//                  • bCheckRan && bSafe   → ProceedWithCodeFixes
//                  • bCheckRan && !bSafe  → ShowSafetyWarningDialog
//                  • !bCheckRan           → ShowSafetyUnavailableDialog
//                    (the dry-run endpoint 404s on every Core today — this
//                    must never be silently treated as "safe")
//     → CoreClient->ApplyCodeFixes(...) (HTTP)
//   FetchFixPreview — on-demand /validate/fix call for one issue's
//     tree-sitter window, used by the row's expand button.
//
// Asset path:
//   OnApplySelectedAssetFixesClicked — the heavy AssetTools rename flow,
//     emits CoreRedirects entries to DefaultEngine.ini, recompiles
//     descendant Blueprints, re-points referencers while KEEPING the redirector
//     stubs (so no reference ever dangles), auto-saves dirty packages, then
//     reports the batch to the server.
//
// Pulled out of the main panel TU because these are the only flows that
// reach into AssetTools / AssetRegistry / Kismet / FileHelpers and they
// drag a heavy include surface that the rest of the panel does not need.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "ShintEngineCompat.h"

#include "ShintStyle.h"

#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"

#include "Styling/AppStyle.h"

// Asset tools (for IAssetTools::RenameAssets + FixupReferencers)
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/ObjectRedirector.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "FileHelpers.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Code fix entry points
// ─────────────────────────────────────────────────────────────────────────────

FReply SShintToolsPanel::OnApplySelectedCodeFixesClicked()
{
	TArray<FShintCodeIssue> Accepted;

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Only accept issues that are checked AND actually auto-fixable.
		if (!Item->bChecked || !Item->bIsAutoFixable) continue;
		// BP issues use plugin-side handlers — no FixSuggestion required.
		// #27 — C++ AST-level rules are auto-fixable via the TreeSitter
		// endpoint using FileContent, with no line-level fix_suggestion. Only
		// reject items that have nothing to work with: no suggestion AND no
		// file content. ApplyCodeFixes already routes FileContent issues to
		// the TreeSitter path, so excluding them here was dropping valid fixes.
		if (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()
			&& Item->FileContent.IsEmpty()) continue;

		FShintCodeIssue I;
		I.RuleId         = Item->RuleId;
		I.Severity       = Item->Severity;
		I.Message        = Item->Message;
		I.FilePath       = Item->FilePath;
		I.Line           = Item->Line;
		I.Snippet        = Item->Snippet;
		I.FixSuggestion  = Item->FixSuggestion;
		I.bIsAutoFixable = Item->bIsAutoFixable;
		I.Class          = Item->Class;
		I.Category       = Item->Category;
		I.Graph          = Item->Graph;
		I.FileContent    = Item->FileContent;
		I.bChecked       = true;
		Accepted.Add(I);
	}

	// Log why Accepted might be empty — counters are independent of bIsAutoFixable.
	int32 TotalChecked = 0, TotalNotFixable = 0, TotalNoFixSuggestion = 0;
	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		if (Item->bChecked) ++TotalChecked;
		if (Item->bChecked && !Item->bIsAutoFixable)        ++TotalNotFixable;
		if (Item->bChecked && Item->FixSuggestion.IsEmpty()) ++TotalNoFixSuggestion;
	}
	UE_LOG(LogShintTools, Verbose,
		TEXT("ApplyFix: Checked=%d, NotAutoFixable=%d, NoFixSuggestion=%d, Accepted=%d"),
		TotalChecked, TotalNotFixable, TotalNoFixSuggestion, Accepted.Num());

	if (Accepted.IsEmpty())
	{
		UE_LOG(LogShintTools, Warning, TEXT("ApplyFix: Nothing to apply — no auto-fixable issues selected"));
		return FReply::Handled();
	}

	UE_LOG(LogShintTools, Verbose, TEXT("ApplyFix: Applying %d fix(es) locally."), Accepted.Num());

	PendingCodeFixes = Accepted;

	// T4 — Always ask first. The safety check is the dry-run that flags fixes
	// which would alter signatures, public API, or otherwise risk breaking
	// dependent code. The previous flow ran it implicitly, so the user never
	// knew it happened and either trusted the silent green path or was confused
	// by the warning popping up out of nowhere.
	const FText DialogTitle = FText::FromString(TEXT("ShintTools — Safety Check"));
	const FText DialogBody  = FText::FromString(TEXT(
		"Do you want to check if the fix breaks any code structure?\n\n"
		"Recommended: pick Yes. ShintTools will try to dry-run the fix "
		"server-side and warn you about anything that could ripple into "
		"other files. If the dry-run isn't available, you'll be asked "
		"again before anything is applied.\n\n"
		"Pick No to apply immediately without attempting the safety dry-run."));
	const EAppReturnType::Type Choice =
		ShintCompat::OpenDialog(EAppMsgType::YesNo, DialogBody, DialogTitle);

	if (Choice == EAppReturnType::Yes)
	{
		SetCodeState(EModuleState::Running);
		CoreClient->CheckFixSafety(Accepted,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		// User explicitly chose to skip the dry-run — apply straight away,
		// no extra prompt. This is their decision, not a failed check.
		ProceedWithCodeFixes();
	}
	return FReply::Handled();
}

FReply SShintToolsPanel::OnApplySingleFix(FShintIssueItemPtr Item)
{
	// #27 — mirror the batch-apply guard: a C++ item with FileContent is
	// fixable via the TreeSitter path even without a line-level fix_suggestion.
	if (!Item.IsValid() || !Item->bIsAutoFixable
		|| (!Item->bIsBlueprint && Item->FixSuggestion.IsEmpty()
			&& Item->FileContent.IsEmpty()))
		return FReply::Handled();

	TArray<FShintCodeIssue> Issues;
	FShintCodeIssue I;
	I.RuleId         = Item->RuleId;
	I.Severity       = Item->Severity;
	I.Message        = Item->Message;
	I.FilePath       = Item->FilePath;
	I.Line           = Item->Line;
	I.Snippet        = Item->Snippet;
	I.FixSuggestion  = Item->FixSuggestion;
	I.bIsAutoFixable = true;
	I.Class          = Item->Class;
	I.Category       = Item->Category;
	I.Graph          = Item->Graph;
	I.FileContent    = Item->FileContent;
	I.bChecked       = true;
	Issues.Add(I);

	PendingCodeFixes = Issues;

	// T4 — same Yes/No prompt as the batch path so the single-issue Apply
	// button gives the user the same control over the safety dry-run.
	const FText DialogTitle = FText::FromString(TEXT("ShintTools — Safety Check"));
	const FText DialogBody  = FText::FromString(TEXT(
		"Do you want to check if the fix breaks any code structure?\n\n"
		"Recommended: pick Yes. ShintTools will try to dry-run the fix "
		"server-side and warn you about anything that could ripple into "
		"other files. If the dry-run isn't available, you'll be asked "
		"again before anything is applied.\n\n"
		"Pick No to apply immediately without attempting the safety dry-run."));
	const EAppReturnType::Type Choice =
		ShintCompat::OpenDialog(EAppMsgType::YesNo, DialogBody, DialogTitle);

	if (Choice == EAppReturnType::Yes)
	{
		SetCodeState(EModuleState::Running);
		CoreClient->CheckFixSafety(Issues,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		// User explicitly chose to skip the dry-run — apply straight away,
		// no extra prompt. This is their decision, not a failed check.
		ProceedWithCodeFixes();
	}
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Safety check
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnSafetyCheckComplete(const FShintSafetyCheckResult& Result)
{
	if (!Result.bCheckRan)
	{
		// The dry-run never happened — 404 (the endpoint doesn't exist on
		// this Core), a transport error, or an unparseable body. This is
		// NOT the same as "checked and safe": applying silently here would
		// be exactly the false promise this fix exists to remove. Restore
		// idle and let the user decide explicitly.
		SetCodeState(EModuleState::Idle);
		ShowSafetyUnavailableDialog();
		return;
	}

	if (Result.bSafe)
	{
		ProceedWithCodeFixes();
	}
	else
	{
		// Checked, and the server flagged it unsafe — restore idle and show
		// the warning dialog.
		SetCodeState(EModuleState::Idle);
		ShowSafetyWarningDialog(Result);
	}
}

void SShintToolsPanel::ShowSafetyUnavailableDialog()
{
	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(LOCTEXT("SafetyUnavailableTitle", "Safety Check Unavailable"))
		.ClientSize(FVector2D(480, 240))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize);

	TWeakPtr<SWindow> WeakDialog(Dialog);

	Dialog->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(24.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SafetyUnavailableHeader", "The safety dry-run isn't available"))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(C_Yellow())
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 16.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SafetyUnavailableBody",
					"ShintTools couldn't reach the fix safety check on this Core, so "
					"it has NOT verified whether these fixes could ripple into other "
					"files. You can apply them without that check, or cancel and try "
					"again later."))
				.Font(F_Small())
				.ColorAndOpacity(C_White())
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Surface())
					.OnClicked_Lambda([WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Cancel", "Cancel"))
						.Font(F_Body()).ColorAndOpacity(C_Gray())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Yellow())
					.OnClicked_Lambda([this, WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						ProceedWithCodeFixes();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ApplyAnyway", "Apply Anyway"))
						.Font(F_Body()).ColorAndOpacity(C_BG())
					]
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
}

void SShintToolsPanel::ShowSafetyWarningDialog(const FShintSafetyCheckResult& Result)
{
	TSharedRef<SWindow> Dialog = SNew(SWindow)
		.Title(LOCTEXT("SafetyTitle", "Safety Check"))
		.ClientSize(FVector2D(560, 420))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::FixedSize);

	TSharedRef<SVerticalBox> WarningList = SNew(SVerticalBox);
	for (int32 i = 0; i < Result.Warnings.Num(); ++i)
	{
		WarningList->AddSlot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d."), i + 1)))
				.Font(F_Small())
				.ColorAndOpacity(C_Gray())
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Result.Warnings[i]))
				.Font(F_Small())
				.ColorAndOpacity(C_White())
				.AutoWrapText(true)
			]
		];
	}

	TWeakPtr<SWindow> WeakDialog(Dialog);

	Dialog->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		.Padding(24.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SafetyHeader", "These fixes may affect your code"))
				.Font(FShintStyle::Fonts::H2())
				.ColorAndOpacity(C_Yellow())
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[ WarningList ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Visibility(Result.Preview.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Result.Preview))
					.Font(F_Mono())
					.ColorAndOpacity(C_Gray())
					.AutoWrapText(true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Surface())
					.OnClicked_Lambda([WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Cancel", "Cancel"))
						.Font(F_Body()).ColorAndOpacity(C_Gray())
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(14.f, 7.f))
					.ButtonColorAndOpacity(C_Yellow())
					.OnClicked_Lambda([this, WeakDialog]() -> FReply
					{
						if (WeakDialog.IsValid()) WeakDialog.Pin()->RequestDestroyWindow();
						ProceedWithCodeFixes();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ApplyAnyway", "Apply Anyway"))
						.Font(F_Body()).ColorAndOpacity(C_BG())
					]
				]
			]
		]
	);

	FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
}

void SShintToolsPanel::ProceedWithCodeFixes()
{
	if (PendingCodeFixes.IsEmpty()) return;

	const uint32 FixGeneration = ScanGeneration;
	SetCodeState(EModuleState::Running);
	CoreClient->ApplyCodeFixes(PendingCodeFixes,
		FOnShintFixComplete::CreateSP(this, &SShintToolsPanel::OnCodeFixComplete, FixGeneration));
	PendingCodeFixes.Empty();
}

// ─────────────────────────────────────────────────────────────────────────────
// On-demand fix preview — /validate/fix call for a single row.
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::FetchFixPreview(FShintIssueItemPtr Item)
{
	if (!Item.IsValid() || Item->FileContent.IsEmpty() || Item->bFixPreviewLoading) return;

	Item->bFixPreviewLoading = true;
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	FShintCodeIssue Issue;
	Issue.RuleId      = Item->RuleId;
	Issue.FilePath    = Item->FilePath;
	Issue.Line        = Item->Line;
	Issue.FileContent = Item->FileContent;

	// Capture info needed to extract the window from fixed_code.
	const int32 ContextStart = Item->ContextLineStart; // 1-based
	TArray<FString> BeforeLines;
	Item->ContextBefore.ParseIntoArray(BeforeLines, TEXT("\n"), false);
	const int32 NumContextLines = FMath::Max(1, BeforeLines.Num());

	TWeakPtr<SShintToolsPanel> WeakPtr = SharedThis(this);
	TWeakPtr<FShintIssueItem>  WeakItem = Item;

	CoreClient->FetchSingleFixPreview(Issue,
		FOnShintFixComplete::CreateLambda(
			[WeakPtr, WeakItem, ContextStart, NumContextLines](const FShintFixResult& Result) mutable
		{
			TSharedPtr<SShintToolsPanel> PinnedPanel = WeakPtr.Pin();
			TSharedPtr<FShintIssueItem>  PinnedItem  = WeakItem.Pin();
			if (!PinnedPanel.IsValid() || !PinnedItem.IsValid()) return;

			PinnedItem->bFixPreviewLoading = false;

			if (Result.bSuccess && Result.FixedFiles.Num() > 0)
			{
				const FString& FixedCode = Result.FixedFiles[0].CorrectedContent;
				TArray<FString> AllLines;
				FixedCode.ParseIntoArray(AllLines, TEXT("\n"), false);

				const int32 StartIdx = FMath::Max(0, ContextStart - 1);
				TArray<FString> Window;
				for (int32 i = StartIdx; i < StartIdx + NumContextLines && i < AllLines.Num(); ++i)
					Window.Add(AllLines[i]);

				PinnedItem->FixPreviewCode = FString::Join(Window, TEXT("\n"));
			}
			else
			{
				UE_LOG(LogShintTools, Warning, TEXT("FetchFixPreview: server error for [%s] — %s"),
					*PinnedItem->RuleId, *Result.ErrorMessage);
			}

			if (PinnedPanel->CodeIssueListView.IsValid())
				PinnedPanel->CodeIssueListView->RequestListRefresh();
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset rename + CoreRedirects flow
// ─────────────────────────────────────────────────────────────────────────────
FReply SShintToolsPanel::OnApplySelectedAssetFixesClicked()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools"))) return FReply::Handled();

	const FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	IAssetTools& AssetTools = AssetToolsModule.Get();

	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetRenameData> RenameData;
	// Old/new paths captured BEFORE RenameAssets() (after the rename,
	// Asset->GetPathName() reports the new path). Redirect entries + server
	// rows are emitted AFTER the rename, per asset, only for renames that
	// verifiably happened — RenameAssets can partially fail, and emitting up
	// front wrote poisoned [CoreRedirects] mappings for renames that never
	// occurred.
	struct FShintPendingRename
	{
		UObject*           Asset = nullptr;
		FShintAssetItemPtr Item;
		FString            OldPackage;   // /Game/.../OldName
		FString            NewPackage;   // /Game/.../NewName
	};
	TArray<FShintPendingRename> Pending;
	int32 SkippedCircular  = 0;
	int32 SkippedCollision = 0;
	int32 SkippedLoadFail  = 0;

	// Iterate the FULL backing store, not the filtered view. The previous
	// behavior renamed only the currently-visible items, so any active type
	// filter (Materials / Textures / etc.) silently skipped everything else
	// even though "Apply Corrections" advertises "all selected".
	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		if (!Item->bChecked) continue;

		// E-001: skip circular / no-op renames. If the suggested name equals
		// the current on-disk name, firing a rename creates a self-referencing
		// ObjectRedirector and triggers an UE5 ensure.
		if (Item->SuggestedName.IsEmpty() ||
			Item->SuggestedName.Equals(Item->CurrentName, ESearchCase::CaseSensitive))
		{
			++SkippedCircular;
			continue;
		}

		// Load the UObject from its package path (/Game/...AssetName).
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Item->AssetPath);
		if (!Asset)
		{
			++SkippedLoadFail;
			UE_LOG(LogShintTools, Warning,
				TEXT("ShintPanel: skipped rename '%s' — failed to load UObject at %s"),
				*Item->CurrentName, *Item->AssetPath);
			continue;
		}

		const FString NewPackagePath = FPaths::GetPath(Item->AssetPath);

		// E-002: skip if the destination package already exists. Otherwise UE5
		// fails the rename with "An object named 'X' already exists".
		{
			const FString NewPackageName = NewPackagePath / Item->SuggestedName;
			TArray<FAssetData> ExistingAssets;
			AR.GetAssetsByPackageName(FName(*NewPackageName), ExistingAssets);
			if (ExistingAssets.Num() > 0)
			{
				UE_LOG(LogShintTools, Warning,
					TEXT("ShintPanel: skipped rename '%s' → '%s' (target already exists at %s)"),
					*Item->CurrentName, *Item->SuggestedName, *NewPackageName);
				++SkippedCollision;
				continue;
			}
		}

		RenameData.Add(FAssetRenameData(Asset, NewPackagePath, Item->SuggestedName));

		FShintPendingRename P;
		P.Asset      = Asset;
		P.Item       = Item;
		P.OldPackage = Item->AssetPath;                       // /Game/.../OldName
		P.NewPackage = NewPackagePath / Item->SuggestedName;  // /Game/.../NewName
		Pending.Add(MoveTemp(P));
	}

	if (SkippedCircular + SkippedCollision + SkippedLoadFail > 0)
	{
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintPanel: asset rename pre-check skipped %d circular, %d collision(s), %d load-fail"),
			SkippedCircular, SkippedCollision, SkippedLoadFail);
	}

	if (RenameData.IsEmpty()) return FReply::Handled();

	// Pre-rename: gather every BP whose parent class is in this rename batch.
	// After the parents rename, RenameAssets/FixupReferencers updates loaded
	// references but leaves the **in-memory generated class pointer** on each
	// child BP stale (it still resolves to the old class name via cached
	// UClass*). Re-compiling each child after the rename forces the kismet
	// compiler to rebuild the parent pointer through the new class path.
	TSet<UBlueprint*> DescendantBPsToRecompile;
	{
		// Build map of OLD generated-class path -> NEW generated-class path
		// from the pending Blueprint renames (must run BEFORE the rename —
		// afterwards the old class path is gone). A child collected for a
		// rename that then fails just gets a harmless recompile.
		TMap<FString, FString> OldBPClassToNew;
		for (const FShintPendingRename& P : Pending)
		{
			if (!P.Asset->IsA<UBlueprint>())
				continue;
			const FString OldBase = FPaths::GetBaseFilename(P.OldPackage);
			OldBPClassToNew.Add(
				P.OldPackage + TEXT(".") + OldBase + TEXT("_C"),
				P.NewPackage + TEXT(".") + P.Item->SuggestedName + TEXT("_C"));
		}

		if (!OldBPClassToNew.IsEmpty())
		{
			FARFilter ChildFilter;
			ChildFilter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			ChildFilter.bRecursiveClasses = true;
			ChildFilter.PackagePaths.Add(TEXT("/Game"));
			ChildFilter.bRecursivePaths = true;

			TArray<FAssetData> AllBPs;
			AR.GetAssets(ChildFilter, AllBPs);

			for (const FAssetData& BPData : AllBPs)
			{
				FString ParentClassPath;
				if (!BPData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath))
					continue;

				// The tag is stored either bare ("/Game/Foo.Foo_C") or wrapped
				// ("/Script/Engine.Class'/Game/Foo.Foo_C'"); strip the wrapper.
				if (ParentClassPath.Contains(TEXT("'")))
				{
					int32 First = INDEX_NONE, Last = INDEX_NONE;
					ParentClassPath.FindChar(TEXT('\''), First);
					Last = ParentClassPath.Find(TEXT("'"),
						ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (First != INDEX_NONE && Last != INDEX_NONE && Last > First)
						ParentClassPath = ParentClassPath.Mid(First + 1, Last - First - 1);
				}

				if (OldBPClassToNew.Contains(ParentClassPath))
				{
					// GetAsset() forces a synchronous load — required so the
					// child is in memory when we recompile it after the rename.
					if (UBlueprint* Child = Cast<UBlueprint>(BPData.GetAsset()))
						DescendantBPsToRecompile.Add(Child);
				}
			}

			if (!DescendantBPsToRecompile.IsEmpty())
			{
				UE_LOG(LogShintTools, Verbose,
					TEXT("ShintPanel: %d descendant BP(s) queued for recompile after parent rename"),
					DescendantBPsToRecompile.Num());
			}
		}
	}

	AssetTools.RenameAssets(RenameData);

	// Verify EACH rename before emitting anything derived from it.
	// RenameAssets can partially fail (checkout refusal, in-memory-only
	// package, external locks); the previous flow ignored its result and
	// emitted [CoreRedirects] mappings, server rows and success counts for
	// renames that never happened.
	TArray<FShintRedirectEntry>  RedirectEntries;
	TArray<FShintAssetIssue>     ForServer;
	TArray<UObjectRedirector*>   OurRedirectors;
	TArray<UPackage*>            TouchedPackages;
	int32 FailedRenames = 0;

	for (const FShintPendingRename& P : Pending)
	{
		const bool bRenamed =
			IsValid(P.Asset) &&
			P.Asset->GetName() == P.Item->SuggestedName;
		if (!bRenamed)
		{
			++FailedRenames;
			UE_LOG(LogShintTools, Warning,
				TEXT("ShintPanel: rename '%s' → '%s' did not apply — skipping "
				     "its redirects/report so no stale mapping is written."),
				*P.Item->CurrentName, *P.Item->SuggestedName);
			continue;
		}

		// Redirect mappings for DefaultEngine.ini. Three kinds, because UE5
		// resolves references through different paths depending on context:
		//   +PackageRedirects (always)  — soft asset paths "/Game/.../OldName"
		//   +ObjectRedirects  (always)  — UObject paths    ".../OldName.OldName"
		//   +ClassRedirects   (BP only) — generated class  ".../OldName.OldName_C"
		// Emitting only one of these per asset used to break soft references
		// in unloaded packages and child Blueprints of a renamed parent.
		const FString OldBase = FPaths::GetBaseFilename(P.OldPackage);
		const FString NewBase = P.Item->SuggestedName;
		{
			FShintRedirectEntry RE;
			RE.Key     = TEXT("+PackageRedirects");
			RE.OldName = P.OldPackage;
			RE.NewName = P.NewPackage;
			RedirectEntries.Add(MoveTemp(RE));
		}
		{
			FShintRedirectEntry RE;
			RE.Key     = TEXT("+ObjectRedirects");
			RE.OldName = P.OldPackage + TEXT(".") + OldBase;
			RE.NewName = P.NewPackage + TEXT(".") + NewBase;
			RedirectEntries.Add(MoveTemp(RE));
		}
		if (P.Asset->IsA<UBlueprint>())
		{
			FShintRedirectEntry RE;
			RE.Key     = TEXT("+ClassRedirects");
			RE.OldName = P.OldPackage + TEXT(".") + OldBase + TEXT("_C");
			RE.NewName = P.NewPackage + TEXT(".") + NewBase + TEXT("_C");
			RedirectEntries.Add(MoveTemp(RE));
		}

		FShintAssetIssue I;
		I.AssetPath     = P.OldPackage;
		I.CurrentName   = P.Item->CurrentName;
		I.SuggestedName = P.Item->SuggestedName;
		I.AssetType     = P.Item->AssetType;
		ForServer.Add(MoveTemp(I));

		TouchedPackages.AddUnique(P.Asset->GetOutermost());

		// Collect ONLY the redirector this rename left at the old package
		// path. The previous flow swept every redirector under /Game and
		// fixed up assets completely unrelated to this batch.
		TArray<FAssetData> OldPkgAssets;
		AR.GetAssetsByPackageName(FName(*P.OldPackage), OldPkgAssets);
		for (const FAssetData& AD : OldPkgAssets)
		{
			if (UObjectRedirector* Redir = Cast<UObjectRedirector>(AD.GetAsset()))
				OurRedirectors.AddUnique(Redir);
		}
	}

	if (FailedRenames > 0)
	{
		UE_LOG(LogShintTools, Warning,
			TEXT("ShintPanel: %d of %d rename(s) failed to apply."),
			FailedRenames, Pending.Num());
	}

	// Re-point every referencer at the new asset but KEEP the redirector
	// (ERedirectFixupMode::LeaveFixedUpRedirectors). Deleting it here
	// (DeleteFixedUpRedirectors) was the cause of "renaming breaks references":
	// FixupReferencers can only re-save referencers it can load AND check out
	// — the currently-open level, read-only packages, and anything it fails to
	// resolve are left dangling the instant the redirector is gone. The
	// [CoreRedirects] fallback we also write does NOT cover them, because
	// CoreRedirects are read from the .ini only at editor startup, so nothing
	// catches those references until the next launch. Leaving the redirector
	// makes the rename reference-safe immediately: every reference form (soft /
	// hard / by-package / by-object / unloaded) resolves through it. The stub
	// is cosmetic and the user can run Content Browser → "Fix Up Redirectors"
	// whenever they want to sweep them.
	if (!OurRedirectors.IsEmpty())
	{
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintPanel: re-pointing referencers of %d redirector(s), keeping the stubs"),
			OurRedirectors.Num());
		AssetTools.FixupReferencers(OurRedirectors,
			/*bCheckoutDialogPrompt=*/false,
			ERedirectFixupMode::LeaveFixedUpRedirectors);
	}

	// T2 — Persist redirect mappings to DefaultEngine.ini. ObjectRedirector
	// .uasset files cover live references but are fragile (deleted by clean,
	// missed by native parent-class lookup on child Blueprints). The
	// [CoreRedirects] entries make the rename survive both.
	{
		const int32 Added = WriteShintCoreRedirects(RedirectEntries);
		if (Added > 0)
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("ShintPanel: wrote %d new entr(ies) to [CoreRedirects] in DefaultEngine.ini"),
				Added);
		}
	}

	// Recompile every descendant BP we collected before the rename. Without
	// this, child BPs still resolve their ParentClass through the old in-memory
	// pointer and report "Class not found" the next time they're loaded by
	// name (which the user perceives as "the bot broke my parents").
	if (!DescendantBPsToRecompile.IsEmpty())
	{
		int32 Recompiled = 0;
		for (UBlueprint* Child : DescendantBPsToRecompile)
		{
			if (!IsValid(Child)) continue;
			FKismetEditorUtilities::CompileBlueprint(Child);
			++Recompiled;
		}
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintPanel: recompiled %d descendant BP(s) after parent rename"),
			Recompiled);
	}

	// Save the packages THIS batch touched (renamed assets + recompiled
	// children) so the rename survives an editor close. Saving every dirty
	// content package here silently committed the user's unrelated
	// half-finished edits — auto-save must stay scoped to our own writes.
	// (FixupReferencers already saves the referencer packages it re-points.)
	{
		for (UBlueprint* Child : DescendantBPsToRecompile)
		{
			if (IsValid(Child))
				TouchedPackages.AddUnique(Child->GetOutermost());
		}
		TArray<UPackage*> ToSave;
		for (UPackage* Pkg : TouchedPackages)
		{
			if (IsValid(Pkg) && Pkg->IsDirty())
				ToSave.Add(Pkg);
		}
		if (!ToSave.IsEmpty())
		{
			const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(
				ToSave,
				/*bCheckDirty=*/true,
				/*bPromptToSave=*/false) == FEditorFileUtils::EPromptReturnCode::PR_Success;
			UE_LOG(LogShintTools, Verbose,
				TEXT("ShintPanel: auto-saved %d package(s) from this batch (success=%d)"),
				ToSave.Num(), bSaved ? 1 : 0);
		}
	}

	CoreClient->ReportAssetFixesToServer(ForServer,
		FOnShintAssetFixComplete::CreateSP(this, &SShintToolsPanel::OnAssetFixComplete));

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
