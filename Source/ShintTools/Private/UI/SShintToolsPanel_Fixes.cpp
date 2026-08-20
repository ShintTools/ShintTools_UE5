// Copyright 2026 ShintTools. All Rights Reserved.

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

FReply SShintToolsPanel::OnApplySelectedCodeFixesClicked()
{
	TArray<FShintCodeIssue> Accepted;

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{

		if (!Item->bChecked || !Item->bIsAutoFixable) continue;

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

		ProceedWithCodeFixes();
	}
	return FReply::Handled();
}

FReply SShintToolsPanel::OnApplySingleFix(FShintIssueItemPtr Item)
{

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

		ProceedWithCodeFixes();
	}
	return FReply::Handled();
}

void SShintToolsPanel::OnSafetyCheckComplete(const FShintSafetyCheckResult& Result)
{
	if (!Result.bCheckRan)
	{

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

	const int32 ContextStart = Item->ContextLineStart;
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

FReply SShintToolsPanel::OnApplySelectedAssetFixesClicked()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools"))) return FReply::Handled();

	const FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	IAssetTools& AssetTools = AssetToolsModule.Get();

	IAssetRegistry& AR =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetRenameData> RenameData;

	struct FShintPendingRename
	{
		UObject*           Asset = nullptr;
		FShintAssetItemPtr Item;
		FString            OldPackage;
		FString            NewPackage;
	};
	TArray<FShintPendingRename> Pending;
	int32 SkippedCircular  = 0;
	int32 SkippedCollision = 0;
	int32 SkippedLoadFail  = 0;

	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		if (!Item->bChecked) continue;

		if (Item->SuggestedName.IsEmpty() ||
			Item->SuggestedName.Equals(Item->CurrentName, ESearchCase::CaseSensitive))
		{
			++SkippedCircular;
			continue;
		}

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
		P.OldPackage = Item->AssetPath;
		P.NewPackage = NewPackagePath / Item->SuggestedName;
		Pending.Add(MoveTemp(P));
	}

	if (SkippedCircular + SkippedCollision + SkippedLoadFail > 0)
	{
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintPanel: asset rename pre-check skipped %d circular, %d collision(s), %d load-fail"),
			SkippedCircular, SkippedCollision, SkippedLoadFail);
	}

	if (RenameData.IsEmpty()) return FReply::Handled();

	TSet<UBlueprint*> DescendantBPsToRecompile;
	{

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

	if (!OurRedirectors.IsEmpty())
	{
		UE_LOG(LogShintTools, Verbose,
			TEXT("ShintPanel: re-pointing referencers of %d redirector(s), keeping the stubs"),
			OurRedirectors.Num());
		AssetTools.FixupReferencers(OurRedirectors,
			false,
			ERedirectFixupMode::LeaveFixedUpRedirectors);
	}

	{
		const int32 Added = WriteShintCoreRedirects(RedirectEntries);
		if (Added > 0)
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("ShintPanel: wrote %d new entr(ies) to [CoreRedirects] in DefaultEngine.ini"),
				Added);
		}
	}

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
				true,
				false) == FEditorFileUtils::EPromptReturnCode::PR_Success;
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
