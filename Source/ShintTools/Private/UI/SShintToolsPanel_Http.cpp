// Copyright 2026 ShintTools. All Rights Reserved.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "Core/ShintAssistantContext.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#include "Containers/Ticker.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

void SShintToolsPanel::OnHealthCheckComplete(const FShintRequestResult& Result)
{
	SetStatus(Result.bSuccess ? ECoreStatus::Online : ECoreStatus::Offline);
}

void SShintToolsPanel::OnProjectValidateComplete(const FShintValidateResult& Result)
{

	HandleValidateResult(Result, true, false);
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	UE_LOG(LogShintTools, Verbose,
		TEXT("OnBlueprintValidateComplete: bSuccess=%d, %d issues from server (bBlueprintScanActive=%d)"),
		Result.bSuccess ? 1 : 0, Result.Issues.Num(), bBlueprintScanActive ? 1 : 0);

	FShintValidateResult QualityResult;
	QualityResult.bSuccess     = Result.bSuccess;
	QualityResult.FilesScanned = Result.FilesScanned;
	QualityResult.Issues.Reserve(Result.Issues.Num());

	int32 NamingRouted = 0;

	for (const FShintCodeIssue& Issue : Result.Issues)
	{

		if (!bBlueprintScanActive && Issue.RuleId == TEXT("BPB001"))
		{
			FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
			Item->AssetPath     = Issue.FilePath;
			Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
			Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
			Item->Reason        = Issue.Message;
			Item->AssetType     = TEXT("Blueprint");
			Item->bChecked      = true;
			Item->OriginalIndex = AllAssetItems.Num();
			AllAssetItems.Add(MoveTemp(Item));
			++NamingRouted;
		}
		else
		{
			QualityResult.Issues.Add(Issue);
			if (Issue.Severity == TEXT("error"))   ++QualityResult.TotalErrors;
			if (Issue.Severity == TEXT("warning")) ++QualityResult.TotalWarnings;
		}
	}
	QualityResult.TotalIssues = QualityResult.Issues.Num();

	QualityResult.QualityScoreOverall   = Result.QualityScoreOverall;
	QualityResult.bHasCategoryBreakdown = Result.bHasCategoryBreakdown;
	QualityResult.PerformanceScore      = Result.PerformanceScore;
	QualityResult.SecurityScore         = Result.SecurityScore;
	QualityResult.BestPracticesScore    = Result.BestPracticesScore;
	QualityResult.MaintainabilityScore  = Result.MaintainabilityScore;
	QualityResult.NamingScore           = Result.NamingScore;
	QualityResult.Tier                  = Result.Tier;

	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}

	UE_LOG(LogShintTools, Verbose,
		TEXT("OnBlueprintValidateComplete: routed %d to asset panel, %d kept for code merge"),
		NamingRouted, QualityResult.Issues.Num());

	HandleValidateResult(QualityResult, true, bBlueprintScanActive);
}

void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration)
{
	SetCodeState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Verbose,
			TEXT("ApplyFix: %d fix(es) applied, %d skipped."),
			Result.TotalFixesApplied, Result.TotalFixesSkipped);

		{
			const int32 BPCount  = Result.BlueprintFixesApplied;
			const int32 CppCount = Result.TotalFixesApplied - BPCount;
			FString Headline;
			if (BPCount > 0 && CppCount > 0)
			{
				Headline = FString::Printf(
					TEXT("✓  %d fix(es) applied — %d C++ · %d Blueprint"),
					Result.TotalFixesApplied, CppCount, BPCount);
			}
			else
			{
				Headline = FString::Printf(TEXT("✓  %d %s fix(es) applied"),
					Result.TotalFixesApplied,
					BPCount > 0 ? TEXT("Blueprint") : TEXT("C++"));
			}
			FNotificationInfo Info(FText::FromString(Headline));
			if (Result.TotalFixesSkipped > 0)
			{
				Info.SubText = FText::FromString(
					FString::Printf(TEXT("%d skipped — open the log for details."),
						Result.TotalFixesSkipped));
			}
			Info.ExpireDuration       = 5.0f;
			Info.bUseSuccessFailIcons = true;
			TSharedPtr<SNotificationItem> N =
				FSlateNotificationManager::Get().AddNotification(Info);
			if (N.IsValid())
			{
				N->SetCompletionState(Result.TotalFixesApplied > 0
					? SNotificationItem::CS_Success
					: SNotificationItem::CS_None);
			}
		}

		for (const FShintCodeIssue& I : PendingCodeFixes)
		{
			AppliedFixFingerprints.Add(
				FString::Printf(TEXT("%s:%d:%s"), *I.FilePath, I.Line, *I.RuleId));
		}
		PendingCodeFixes.Empty();

		CodeIssueItems.Reset();
		if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

		AllCodeItems.RemoveAll([](const FShintIssueItemPtr& I) {
			return I->bChecked && I->bIsAutoFixable
				&& (!I->FixSuggestion.IsEmpty() || I->bIsBlueprint);
		});

		if (Result.bHasCompileErrors && Result.CompileErrors.Num() > 0
			&& FixGeneration == ScanGeneration)
		{
			UE_LOG(LogShintTools, Warning,
				TEXT("OnCodeFixComplete: %d compile error(s) injected into panel"),
				Result.CompileErrors.Num());

			for (const FShintCompileError& CE : Result.CompileErrors)
			{
				FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
				Item->RuleId         = TEXT("BUILD001");

				Item->Severity       = TEXT("critical");
				Item->Message        = CE.Code.IsEmpty()
					? CE.Message
					: FString::Printf(TEXT("[%s] %s"), *CE.Code, *CE.Message);
				Item->FilePath       = CE.FilePath;
				Item->FileName       = CE.FileName;
				Item->Line           = CE.Line;
				Item->Snippet        = TEXT("");
				Item->FixSuggestion  = TEXT("");
				Item->bIsAutoFixable = false;
				Item->bChecked       = false;
				Item->Category       = TEXT("Build");
				Item->OriginalIndex  = AllCodeItems.Num();
				AllCodeItems.Add(MoveTemp(Item));
			}
		}
		else if (Result.bHasCompileErrors && FixGeneration != ScanGeneration)
		{
			UE_LOG(LogShintTools, Verbose,
				TEXT("OnCodeFixComplete: build errors discarded — scan generation changed (fix=%u current=%u)"),
				FixGeneration, ScanGeneration);
		}

		ApplyCodeFilter();
		RefreshCodeStats();

		if (AllCodeItems.IsEmpty())
		{
			if (CodeEmptyText.IsValid())
				CodeEmptyText->SetText(LOCTEXT("CVAllFixed",
					"✓  All issues resolved — click 'Scan' again to do a full re-scan."));
			if (CodeEmptyState.IsValid())
				CodeEmptyState->SetVisibility(EVisibility::Visible);
		}
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("ApplyFix failed: %s"), *Result.ErrorMessage);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();
	RefreshApplyCodeLabel();
}

void SShintToolsPanel::OnAssetScanComplete(const FShintAssetScanResult& Result)
{
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Asset scan failed"), Result.ErrorMessage);
		SetAssetState(EModuleState::Error);
		return;
	}
	LastAssetResult = Result;
	PopulateAssetIssueList(Result);
	RefreshAssetStats();

	FShintAssistantContext::Publish(
		Result.AnalysisId, EShintAssistantModule::AssetNaming,
		FString::Printf(TEXT("Asset Naming — %d violation%s"),
			Result.InvalidAssets, Result.InvalidAssets == 1 ? TEXT("") : TEXT("s")));

	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintNamingScanComplete));
}

void SShintToolsPanel::OnAssetScanFromBPComplete(const FShintAssetScanResult& Result)
{

	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Blueprint asset scan failed"), Result.ErrorMessage);
		SetAssetState(EModuleState::Error);
		return;
	}
	LastAssetResult = Result;
	PopulateAssetIssueList(Result);

	CurrentAssetTypeFilter = EAssetTypeFilter::Blueprints;
	ApplyAssetFilter();
	RefreshAssetStats();
}

void SShintToolsPanel::OnBlueprintNamingScanComplete(const FShintValidateResult& Result)
{

	int32 NamingRouted = 0;
	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		if (Issue.RuleId != TEXT("BPB001")) continue;

		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath     = Issue.FilePath;
		Item->CurrentName   = FPaths::GetBaseFilename(Issue.FilePath);
		Item->SuggestedName = TEXT("BP_") + Item->CurrentName;
		Item->Reason        = Issue.Message;
		Item->AssetType     = TEXT("Blueprint");
		Item->bChecked      = true;
		Item->OriginalIndex = AllAssetItems.Num();
		AllAssetItems.Add(MoveTemp(Item));
		++NamingRouted;
	}

	SetAssetState(EModuleState::Done);

	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}
}

void SShintToolsPanel::OnAssetFixComplete(const FShintAssetFixResult& Result)
{
	SetAssetState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Verbose, TEXT("AssetFix: %d asset(s) renamed."), Result.AssetsRenamed);

		AllAssetItems.Reset();
		AssetIssueItems.Reset();
		if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
		RefreshAssetStats();
		RefreshApplyAssetLabel();

		++AssetFixesApplied;
		if (AssetEmptyText.IsValid())
			AssetEmptyText->SetText(LOCTEXT("ANBAllFixed",
				"✓  All violations resolved — click 'Scan' again to do a full re-scan."));
		if (AssetEmptyState.IsValid())
			AssetEmptyState->SetVisibility(EVisibility::Visible);
	}
	else
	{
		UE_LOG(LogShintTools, Error, TEXT("AssetFix failed: %s"), *Result.ErrorMessage);
	}
}

#undef LOCTEXT_NAMESPACE
