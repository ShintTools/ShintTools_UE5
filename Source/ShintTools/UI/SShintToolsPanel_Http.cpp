// Copyright ShintTools. All Rights Reserved.
//
// HTTP completion callbacks — every On*Complete bound to a request
// fired by the panel.
//
// Health / scan paths:
//   OnHealthCheckComplete
//   OnProjectValidateComplete        → HandleValidateResult (in _State.cpp)
//   OnBlueprintValidateComplete      → routes BPB001 to the asset panel
//                                      then HandleValidateResult
//   OnAssetScanComplete              → chains a BP-naming validate
//   OnAssetScanFromBPComplete        → asset scan after Scan Blueprints
//   OnBlueprintNamingScanComplete    → BPB001 → asset panel only
//
// Apply paths:
//   OnCodeFixComplete                → success toast, fingerprint applied
//                                      fixes, inject BUILD001 errors when
//                                      the build check fires for the same
//                                      ScanGeneration
//   OnAssetFixComplete               → clear lists + "all resolved" empty
//                                      state
//
// Dashboard push paths:
//   OnCodeDashboardComplete          → button label transitions
//   OnAssetDashboardComplete         → button label transitions

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools/ShintTools.h"
#include "ShintCoreClient.h"
#include "ShintDashboardSync.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

#include "Containers/Ticker.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

// ─────────────────────────────────────────────────────────────────────────────
// Health / validate
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnHealthCheckComplete(const FShintRequestResult& Result)
{
	SetStatus(Result.bSuccess ? ECoreStatus::Online : ECoreStatus::Offline);
}

void SShintToolsPanel::OnProjectValidateComplete(const FShintValidateResult& Result)
{
	// Bug #36: this used to call HandleValidateResult with bMerge=false, which
	// wiped LastCodeResult on every C++ scan — so doing "Scan All BP" then
	// "Scan All Source" lost the BP findings and the user saw only what the
	// C++ scan happened to return (often a near-empty list). Switching to
	// bMerge=true makes the merge-by-prefix logic in HandleValidateResult
	// keep BP issues while replacing only the C++ half of the list.
	HandleValidateResult(Result, /*bMerge=*/true, /*bIsBPScan=*/false);
}

void SShintToolsPanel::OnBlueprintValidateComplete(const FShintValidateResult& Result)
{
	UE_LOG(LogShintTools, Log,
		TEXT("OnBlueprintValidateComplete: bSuccess=%d, %d issues from server (bBlueprintScanActive=%d)"),
		Result.bSuccess ? 1 : 0, Result.Issues.Num(), bBlueprintScanActive ? 1 : 0);

	// Separate naming issues (BPB001) → route to Asset Naming panel.
	FShintValidateResult QualityResult;
	QualityResult.bSuccess     = Result.bSuccess;
	QualityResult.FilesScanned = Result.FilesScanned;
	QualityResult.Issues.Reserve(Result.Issues.Num());

	int32 NamingRouted = 0;

	for (const FShintCodeIssue& Issue : Result.Issues)
	{
		// BPB001 (naming) → Asset Naming panel ONLY when triggered by the
		// legacy naming-chain path (bBlueprintScanActive=false). When the user
		// clicks "Scan All BP", all issues stay in the Code Validator — no
		// asset panel side-effect.
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

	if (NamingRouted > 0)
	{
		ApplyAssetFilter();
		RefreshAssetStats();
	}

	UE_LOG(LogShintTools, Log,
		TEXT("OnBlueprintValidateComplete: routed %d to asset panel, %d kept for code merge"),
		NamingRouted, QualityResult.Issues.Num());

	// T4 — Always merge into the unified code-validator panel so the user sees
	// C++ AND Blueprint findings in the same list. The Code-Type filter
	// (CppOnly / BlueprintsOnly) and the Category filter let them slice the
	// view; they don't need a destructive REPLACE on every BP scan.
	HandleValidateResult(QualityResult, /*bMerge=*/true, /*bIsBPScan=*/bBlueprintScanActive);
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply code fixes — completion
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnCodeFixComplete(const FShintFixResult& Result, uint32 FixGeneration)
{
	SetCodeState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log,
			TEXT("ApplyFix: %d fix(es) applied, %d skipped."),
			Result.TotalFixesApplied, Result.TotalFixesSkipped);

		// T3 — Surface a success toast so the user sees what just landed.
		// Without this, the panel only updated counters and the "click was
		// silent" perception drove repeat-clicks. Mention the kind of scan
		// (Blueprint vs C++) explicitly because BP fixes were the most
		// visually-quiet flow.
		{
			const TCHAR* Kind = bBlueprintScanActive ? TEXT("Blueprint") : TEXT("C++");
			FNotificationInfo Info(FText::FromString(
				FString::Printf(TEXT("✓  %d %s fix(es) applied"),
					Result.TotalFixesApplied, Kind)));
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

		// Record fingerprints so these issues are suppressed on any future
		// incremental re-scan within this session.
		for (const FShintCodeIssue& I : PendingCodeFixes)
		{
			AppliedFixFingerprints.Add(
				FString::Printf(TEXT("%s:%d:%s"), *I.FilePath, I.Line, *I.RuleId));
		}
		PendingCodeFixes.Empty();

		// Clear visible list FIRST so Slate never touches stale pointers.
		CodeIssueItems.Reset();
		if (CodeIssueListView.IsValid()) CodeIssueListView->RebuildList();

		// Now safe to remove from backing data.
		AllCodeItems.RemoveAll([](const FShintIssueItemPtr& I) {
			return I->bChecked && I->bIsAutoFixable
				&& (!I->FixSuggestion.IsEmpty() || I->bIsBlueprint);
		});

		// Inject compile errors from the incremental build check. Only inject
		// if no new scan has been triggered since the fix was applied. A
		// changed ScanGeneration means the user already launched a fresh scan
		// that wiped AllCodeItems — stale BUILD001 items must not be re-added.
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
				// Compile errors are always critical — they block compilation.
				// "critical" maps to the Critical filter chip in the panel.
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
			UE_LOG(LogShintTools, Log,
				TEXT("OnCodeFixComplete: build errors discarded — scan generation changed (fix=%u current=%u)"),
				FixGeneration, ScanGeneration);
		}

		// Re-populate visible list with updated data.
		ApplyCodeFilter();
		RefreshCodeStats();

		// If all items are now gone, show the "all resolved" state immediately.
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

// ─────────────────────────────────────────────────────────────────────────────
// Dashboard push — code
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnCodeDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendCodeBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendCodeBtnLabel->SetText(LOCTEXT("SendCodeOk", "✓  Sent!"));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		// Surface the truncated server message inline so the user sees WHY the
		// send failed without having to open the Output Log.
		FString Short = Result.ErrorMessage.IsEmpty()
			? FString(TEXT("Network or auth error"))
			: Result.ErrorMessage;
		if (Short.Len() > 60)
		{
			Short = Short.Left(57) + TEXT("…");
		}
		SendCodeBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✗  Send failed — %s"), *Short)));
		SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error,
			TEXT("Dashboard send failed: %s | response: %s"),
			*Result.ErrorMessage, *Result.ResponseBody);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (TSharedPtr<SShintToolsPanel> Pin = weak_this.Pin())
			{
				if (Pin->SendCodeBtnLabel.IsValid())
				{
					Pin->SendCodeBtnLabel->SetText(LOCTEXT("SendCodeRst", "↑  Send to Dashboard"));
					Pin->SendCodeBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset scan chain
// ─────────────────────────────────────────────────────────────────────────────
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

	// Chain a BP validation pass to pick up BPB001 (naming violations).
	// Results go ONLY to the asset naming panel — code validator is not touched.
	CoreClient->ValidateBlueprints(FPaths::ProjectContentDir(),
		FOnShintValidateComplete::CreateSP(this, &SShintToolsPanel::OnBlueprintNamingScanComplete));
}

void SShintToolsPanel::OnAssetScanFromBPComplete(const FShintAssetScanResult& Result)
{
	// Asset scan triggered by "Scan Blueprints":
	//   • populate the asset list from full scan results
	//   • auto-set filter to Blueprints so only BP naming violations are visible
	//   • do NOT chain another ValidateBlueprints call — BP code scan is already running
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
	// Extract only BPB001 (wrong/missing BP_ prefix) and add to asset panel.
	// Every other BP issue is silently discarded — code validator stays untouched.
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

// ─────────────────────────────────────────────────────────────────────────────
// Asset apply / dashboard
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::OnAssetFixComplete(const FShintAssetFixResult& Result)
{
	SetAssetState(EModuleState::Done);

	if (Result.bSuccess)
	{
		UE_LOG(LogShintTools, Log, TEXT("AssetFix: %d asset(s) renamed."), Result.AssetsRenamed);

		// Clear both backing store and visible list.
		AllAssetItems.Reset();
		AssetIssueItems.Reset();
		if (AssetIssueListView.IsValid()) AssetIssueListView->RebuildList();
		RefreshAssetStats();
		RefreshApplyAssetLabel();

		// Track that fixes were applied; show "all resolved" message.
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

void SShintToolsPanel::OnAssetDashboardComplete(const FShintWebDashboardResult& Result)
{
	if (!SendAssetBtnLabel.IsValid()) return;

	if (Result.bSuccess)
	{
		SendAssetBtnLabel->SetText(LOCTEXT("SendAssetOk", "✓  Sent!"));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Green()));
	}
	else
	{
		FString Short = Result.ErrorMessage.IsEmpty()
			? FString(TEXT("Network or auth error"))
			: Result.ErrorMessage;
		if (Short.Len() > 60)
		{
			Short = Short.Left(57) + TEXT("…");
		}
		SendAssetBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("✗  Send failed — %s"), *Short)));
		SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Red()));
		UE_LOG(LogShintTools, Error,
			TEXT("Dashboard send failed: %s | response: %s"),
			*Result.ErrorMessage, *Result.ResponseBody);
	}

	TWeakPtr<SShintToolsPanel> weak_this = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([weak_this](float) -> bool {
			if (const TSharedPtr<SShintToolsPanel> pin = weak_this.Pin())
			{
				if (pin->SendAssetBtnLabel.IsValid())
				{
					pin->SendAssetBtnLabel->SetText(LOCTEXT("SendAssetRst", "↑  Send to Dashboard"));
					pin->SendAssetBtnLabel->SetColorAndOpacity(FSlateColor(C_Blue()));
				}
			}
			return false;
		}), 3.f);
}

#undef LOCTEXT_NAMESPACE
