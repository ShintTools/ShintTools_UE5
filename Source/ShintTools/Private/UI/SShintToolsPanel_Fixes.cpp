// Copyright 2026 ShintTools. All Rights Reserved.
//
// Apply-fix flows for both the Code Validator and the Asset Naming Bot.
//
// Code path:
//   OnApplySelectedCodeFixesClicked / OnApplySingleFix
//     → Yes/No safety dry-run prompt
//     → OnSafetyCheckComplete → ProceedWithCodeFixes
//     → CoreClient->ApplyCodeFixes(...) (HTTP)
//   FetchFixPreview — on-demand /validate/fix call for one issue's
//     tree-sitter window, used by the row's expand button.
//
// Asset path:
//   OnApplySelectedAssetFixesClicked — the heavy AssetTools rename flow,
//     emits CoreRedirects entries to DefaultEngine.ini, recompiles
//     descendant Blueprints, fixes up ObjectRedirectors, auto-saves
//     dirty packages, then reports the batch to the server.
//
// Pulled out of the main panel TU because these are the only flows that
// reach into AssetTools / AssetRegistry / Kismet / FileHelpers and they
// drag a heavy include surface that the rest of the panel does not need.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"

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
		"Recommended: pick Yes. ShintTools will dry-run the fix server-side "
		"and warn you about anything that could ripple into other files.\n\n"
		"Pick No to apply immediately without the safety dry-run."));
	const EAppReturnType::Type Choice =
		FMessageDialog::Open(EAppMsgType::YesNo, DialogBody, DialogTitle);

	SetCodeState(EModuleState::Running);

	if (Choice == EAppReturnType::Yes)
	{
		CoreClient->CheckFixSafety(Accepted,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		// User opted to skip the dry-run — short-circuit straight to apply.
		FShintSafetyCheckResult Skipped;
		Skipped.bSafe = true;
		OnSafetyCheckComplete(Skipped);
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
		"Recommended: pick Yes. ShintTools will dry-run the fix server-side "
		"and warn you about anything that could ripple into other files.\n\n"
		"Pick No to apply immediately without the safety dry-run."));
	const EAppReturnType::Type Choice =
		FMessageDialog::Open(EAppMsgType::YesNo, DialogBody, DialogTitle);

	SetCodeState(EModuleState::Running);

	if (Choice == EAppReturnType::Yes)
	{
		CoreClient->CheckFixSafety(Issues,
			FOnShintSafetyCheckComplete::CreateSP(this, &SShintToolsPanel::OnSafetyCheckComplete));
	}
	else
	{
		FShintSafetyCheckResult Skipped;
		Skipped.bSafe = true;
		OnSafetyCheckComplete(Skipped);
	}
	return FReply::Handled();
}

// ─────────────────────────────────────────────────────────────────────────────
// Safety check
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnSafetyCheckComplete(const FShintSafetyCheckResult& Result)
{
	if (Result.bSafe)
	{
		ProceedWithCodeFixes();
	}
	else
	{
		// Not safe — restore idle and show warning dialog.
		SetCodeState(EModuleState::Idle);
		ShowSafetyWarningDialog(Result);
	}
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
	TArray<FShintAssetIssue> ForServer;
	// Captured BEFORE RenameAssets() so we know the old object path. After the
	// rename, Asset->GetPathName() reports the new path, so any later attempt
	// to derive OldName would only emit identity mappings.
	TArray<FShintRedirectEntry> RedirectEntries;
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

		// Record redirect mappings for DefaultEngine.ini. Three redirect kinds,
		// emitted per asset, because UE5 resolves references through different
		// paths depending on context:
		//
		//   +PackageRedirects (always)  — soft asset paths "/Game/.../OldName"
		//   +ObjectRedirects  (always)  — UObject paths    "/Game/.../OldName.OldName"
		//   +ClassRedirects   (BP only) — generated class  "/Game/.../OldName.OldName_C"
		//
		// Previously we emitted only one of these per asset, so renames broke
		// soft references in unloaded packages and child Blueprints whose parent
		// was renamed (parent class lookup fell through to "Class not found").
		{
			const FString OldPackage = Item->AssetPath;                          // /Game/.../OldName
			const FString NewPackage = NewPackagePath / Item->SuggestedName;     // /Game/.../NewName
			const FString OldBase    = FPaths::GetBaseFilename(OldPackage);
			const FString NewBase    = Item->SuggestedName;

			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+PackageRedirects");
				RE.OldName = OldPackage;
				RE.NewName = NewPackage;
				RedirectEntries.Add(MoveTemp(RE));
			}
			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+ObjectRedirects");
				RE.OldName = OldPackage + TEXT(".") + OldBase;
				RE.NewName = NewPackage + TEXT(".") + NewBase;
				RedirectEntries.Add(MoveTemp(RE));
			}
			if (Asset->IsA<UBlueprint>())
			{
				FShintRedirectEntry RE;
				RE.Key     = TEXT("+ClassRedirects");
				RE.OldName = OldPackage + TEXT(".") + OldBase + TEXT("_C");
				RE.NewName = NewPackage + TEXT(".") + NewBase + TEXT("_C");
				RedirectEntries.Add(MoveTemp(RE));
			}
		}

		FShintAssetIssue I;
		I.AssetPath     = Item->AssetPath;
		I.CurrentName   = Item->CurrentName;
		I.SuggestedName = Item->SuggestedName;
		I.AssetType     = Item->AssetType;
		ForServer.Add(I);
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
		// Build map of OLD generated-class path -> NEW generated-class path,
		// extracted from the +ClassRedirects entries we just queued.
		TMap<FString, FString> OldBPClassToNew;
		for (const FShintRedirectEntry& RE : RedirectEntries)
		{
			if (RE.Key.Equals(TEXT("+ClassRedirects")))
				OldBPClassToNew.Add(RE.OldName, RE.NewName);
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

	// Fix redirectors left at old paths. After RenameAssets, UE5 creates an
	// ObjectRedirector at the original package path. Collect all redirectors
	// under /Game and fix references so no stale pointers remain and
	// DefaultEngine.ini stays clean.
	{
		FARFilter RedirFilter;
		RedirFilter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		RedirFilter.PackagePaths.Add(TEXT("/Game"));
		RedirFilter.bRecursivePaths = true;

		TArray<FAssetData> RedirAssets;
		AR.GetAssets(RedirFilter, RedirAssets);

		TArray<UObjectRedirector*> Redirectors;
		Redirectors.Reserve(RedirAssets.Num());
		for (const FAssetData& RD : RedirAssets)
		{
			if (UObjectRedirector* Redir = Cast<UObjectRedirector>(RD.GetAsset()))
				Redirectors.Add(Redir);
		}
		if (!Redirectors.IsEmpty())
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("ShintPanel: fixing %d redirector(s) after asset rename"), Redirectors.Num());
			AssetTools.FixupReferencers(Redirectors);
		}
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

	// Save renamed packages + their referencers so the rename (and the .uasset
	// redirector left behind) survives an editor close. With the redirectors
	// un-saved, closing the editor without saving silently reverts the rename
	// and the user reports "broken references".
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		if (!DirtyPackages.IsEmpty())
		{
			const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(
				DirtyPackages,
				/*bCheckDirty=*/true,
				/*bPromptToSave=*/false) == FEditorFileUtils::EPromptReturnCode::PR_Success;
			UE_LOG(LogShintTools, Verbose,
				TEXT("ShintPanel: auto-saved %d dirty package(s) post-rename (success=%d)"),
				DirtyPackages.Num(), bSaved ? 1 : 0);
		}
	}

	CoreClient->ReportAssetFixesToServer(ForServer,
		FOnShintAssetFixComplete::CreateSP(this, &SShintToolsPanel::OnAssetFixComplete));

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
