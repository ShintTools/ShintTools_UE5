// Copyright 2026 ShintTools. All Rights Reserved.
//
// Stateful panel logic that mutates the issue/asset backing stores or
// reads/writes module state — populate, filter, refresh-stats,
// quality-score helpers, plus the trivial Set*/Get* accessors. None of
// this builds Slate widgets; the only Slate API touched is the bound
// SListView::RequestListRefresh() / RebuildList() / visibility flag on
// the empty-state hosts.
//
// Split out of the main panel TU so the controller logic compiles
// independently from the section builders. The behaviour is verbatim:
// every counter, every fingerprint, every cap mirror what the original
// 3.5k-line file did.

#include "SShintToolsPanel.h"
#include "SShintToolsPanel_Private.h"
#include "ShintTools.h"
#include "ShintCoreClient.h"
#include "Core/ShintAssistantContext.h"
#include "SShintTopBar.h"  // EShintConnState — bridged from SetStatus()

#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Views/SListView.h"

#include "Algo/Count.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "SShintToolsPanel"

namespace
{
	/**
	 * Count the DISTINCT files referenced by a list of issue rows.
	 *
	 * This is the Unity client's rule, kept identical on purpose so both
	 * engines headline the same number for the same project: Unity's
	 * DynamicToolPanel keys a dictionary by each issue's path and shows its
	 * Count, i.e. a file with five issues still counts once.
	 *
	 * Templated on the row type + path member so the Code and Asset panels
	 * share one definition (their rows name the field differently). Lives in
	 * this TU only — an anon-namespace helper duplicated across panel TUs
	 * collides under a full unity build.
	 */
	template <typename RowType>
	int32 CountUniqueFiles(const TArray<TSharedPtr<RowType>>& Rows,
	                       FString RowType::* PathField)
	{
		TSet<FString> Seen;
		Seen.Reserve(Rows.Num());
		for (const TSharedPtr<RowType>& Row : Rows)
		{
			if (Row.IsValid())
			{
				Seen.Add((*Row).*PathField);
			}
		}
		return Seen.Num();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// HandleValidateResult
//
// Routes a /validate response into the panel: either merges with the existing
// issue set (preserving the other half of the C++/BP split) or fully replaces
// it. Bumps ScanGeneration on full replace so any in-flight async build check
// drops its compile errors instead of injecting them into a newer scan.
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::HandleValidateResult(const FShintValidateResult& Result, bool bMerge, bool bIsBPScan)
{
	if (!Result.bSuccess)
	{
		ShintShowErrorToast(TEXT("Code validation failed"), Result.ErrorMessage);
		SetCodeState(EModuleState::Error);
		return;
	}

	if (bMerge)
	{
		// Merge-by-scan-type. A C++ scan replaces only C++ issues (RuleId
		// prefixed CP/CB/CS/CM); a BP scan replaces only BP issues (BP*
		// prefix). Re-running either scan does NOT duplicate findings, and
		// the unified list keeps both kinds visible at once.
		auto IsBP = [](const FShintCodeIssue& I) {
			return I.RuleId.StartsWith(TEXT("BP"));
		};
		LastCodeResult.Issues.RemoveAll([&](const FShintCodeIssue& I)
		{
			return bIsBPScan ? IsBP(I) : !IsBP(I);
		});
		LastCodeResult.Issues.Append(Result.Issues);

		// Recompute counters from the merged set so they always match the list.
		LastCodeResult.bSuccess      = true;
		LastCodeResult.FilesScanned  = Result.FilesScanned;  // last scan's coverage
		LastCodeResult.TotalIssues   = LastCodeResult.Issues.Num();
		LastCodeResult.TotalErrors   = 0;
		LastCodeResult.TotalWarnings = 0;
		for (const FShintCodeIssue& I : LastCodeResult.Issues)
		{
			if (I.Severity == TEXT("error"))   ++LastCodeResult.TotalErrors;
			if (I.Severity == TEXT("warning")) ++LastCodeResult.TotalWarnings;
		}

		// Refresh the score from THIS scan's response. The merge path used to
		// drop the new quality_score on the floor, so a BP scan after a C++
		// scan kept showing the C++ score forever and the OverviewHero froze
		// on the first scan's value.
		if (Result.QualityScoreOverall >= 0.f)
		{
			LastCodeResult.QualityScoreOverall   = Result.QualityScoreOverall;
			LastCodeResult.bHasCategoryBreakdown = Result.bHasCategoryBreakdown;
			LastCodeResult.PerformanceScore      = Result.PerformanceScore;
			LastCodeResult.SecurityScore         = Result.SecurityScore;
			LastCodeResult.BestPracticesScore    = Result.BestPracticesScore;
			LastCodeResult.MaintainabilityScore  = Result.MaintainabilityScore;
			LastCodeResult.NamingScore           = Result.NamingScore;
		}
	}
	else
	{
		LastCodeResult = Result;
		// Bump generation so any in-flight async build check is discarded — its
		// BUILD001 errors belong to the previous set of files, not this fresh scan.
		++ScanGeneration;
	}

	SetCodeState(EModuleState::Done);
	PopulateCodeIssueList(LastCodeResult, bIsBPScan);
	RefreshCodeStats();

	// Hand the assistant something to ground answers in. Publishing here (and
	// not at request time) means the panel only ever points at an analysis the
	// server actually produced.
	FShintAssistantContext::Publish(
		LastCodeResult.AnalysisId,
		EShintAssistantModule::CodeValidator,
		FString::Printf(TEXT("Code Validator — %d issue%s"),
			LastCodeResult.TotalIssues,
			LastCodeResult.TotalIssues == 1 ? TEXT("") : TEXT("s")));

	// Surface the overall score the server returned inline. The
	// /metrics/score/latest round-trip used to fetch the per-category
	// breakdown was removed in 1.7.11 along with project_id from the
	// config schema; the breakdown now rides on the validate response as
	// `category_scores` when the connected core is new enough.
	if (LastCodeResult.QualityScoreOverall >= 0.f)
	{
		LastQualityScore = FShintQualityScoreSnapshot();
		LastQualityScore.bValid       = true;
		LastQualityScore.OverallScore = LastCodeResult.QualityScoreOverall;
		LastQualityScore.Errors       = LastCodeResult.TotalErrors;
		LastQualityScore.Warnings     = LastCodeResult.TotalWarnings;
		LastQualityScore.TotalIssues  = LastCodeResult.TotalIssues;
		LastQualityScore.FilesScanned = LastCodeResult.FilesScanned;
		if (LastCodeResult.bHasCategoryBreakdown)
		{
			LastQualityScore.PerformanceScore     = LastCodeResult.PerformanceScore;
			LastQualityScore.SecurityScore        = LastCodeResult.SecurityScore;
			LastQualityScore.BestPracticesScore   = LastCodeResult.BestPracticesScore;
			LastQualityScore.MaintainabilityScore = LastCodeResult.MaintainabilityScore;
			LastQualityScore.NamingScore          = LastCodeResult.NamingScore;
		}
		RefreshQualityScore();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Populate
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::PopulateCodeIssueList(const FShintValidateResult& Result, bool bIsBPScan)
{
	(void)bIsBPScan;   // classification is derived from FilePath / RuleId, not this flag
	const double PopStart = FPlatformTime::Seconds();
	// Clear visible list FIRST so Slate never references stale items during a paint tick.
	CodeIssueItems.Reset();
	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	AllCodeItems.Reset();
	AllCodeItems.Reserve(Result.Issues.Num());

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		const FShintCodeIssue& Src = Result.Issues[i];

		// Skip issues already fixed this session — fingerprint matches by
		// FilePath:Line:RuleId so an incremental BP re-scan does not re-show
		// the same issue.
		const FString Fingerprint = FString::Printf(
			TEXT("%s:%d:%s"), *Src.FilePath, Src.Line, *Src.RuleId);
		if (AppliedFixFingerprints.Contains(Fingerprint))
			continue;

		FShintIssueItemPtr Item = MakeShared<FShintIssueItem>();
		Item->RuleId          = Src.RuleId;
		Item->RuleName        = Src.RuleName;
		Item->RuleExplanation = Src.RuleExplanation;
		Item->Severity        = Src.Severity;
		Item->Message         = Src.Message;
		Item->FilePath        = Src.FilePath;
		Item->FileName        = FPaths::GetCleanFilename(Src.FilePath);
		Item->Line            = Src.Line;
		Item->Snippet         = Src.Snippet;
		Item->FixSuggestion   = Src.FixSuggestion;
		Item->bIsAutoFixable  = Src.bIsAutoFixable;
		Item->bChecked        = Src.bIsAutoFixable;

		// Classify each issue. Use the rule-id prefix as the primary signal —
		// BP* rules can only be produced by Blueprint scanning, C{P,B,S,M}*
		// rules can only be produced by C++ scanning — and fall back to the
		// path heuristic only when RuleId is empty. BUILD* (compile errors)
		// belong to C++ so the fix-routing logic that branches on bIsBlueprint
		// stays correct.
		const FString& Rid = Src.RuleId;
		const bool bRidIsBP = Rid.StartsWith(TEXT("BP"));
		const bool bRidIsCpp =
			Rid.StartsWith(TEXT("CP")) || Rid.StartsWith(TEXT("CB"))
		 || Rid.StartsWith(TEXT("CS")) || Rid.StartsWith(TEXT("CM"));
		if      (bRidIsBP)  Item->bIsBlueprint = true;
		else if (bRidIsCpp) Item->bIsBlueprint = false;
		else                Item->bIsBlueprint = Src.FilePath.StartsWith(TEXT("/Game/"));

		Item->OriginalIndex    = i;
		Item->Class            = Src.Class;
		Item->Category         = Src.Category;
		Item->Graph            = Src.Graph;
		Item->FileContent      = Src.FileContent;
		Item->ContextBefore    = Src.ContextBefore;
		Item->ContextAfter     = Src.ContextAfter;
		Item->ContextLineStart = Src.ContextLineStart;
		AllCodeItems.Add(MoveTemp(Item));
	}

	ApplyCodeFilter();

	const bool bCodeEmpty = AllCodeItems.IsEmpty();
	if (CodeEmptyState.IsValid())
		CodeEmptyState->SetVisibility(bCodeEmpty ? EVisibility::Visible : EVisibility::Collapsed);
	if (bCodeEmpty && CodeEmptyText.IsValid())
		CodeEmptyText->SetText(LOCTEXT("CVNoIssues", "✓  No issues found in your project."));

	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(!AllCodeItems.IsEmpty());
	if (SendCodeBtn.IsValid())  SendCodeBtn->SetEnabled(true);
	RefreshApplyCodeLabel();

	UE_LOG(LogShintTools, Verbose,
		TEXT("[BENCH] PopulateCodeIssueList: %.3f s, AllCodeItems=%d (raw issues=%d, fingerprints suppressed=%d), CodeIssueItems=%d after filter "
		     "[CodeType=%d Severity=%d Category=%d Fixable=%d]"),
		FPlatformTime::Seconds() - PopStart,
		AllCodeItems.Num(), Result.Issues.Num(),
		Result.Issues.Num() - AllCodeItems.Num(),
		CodeIssueItems.Num(),
		static_cast<int32>(CurrentCodeTypeFilter),
		static_cast<int32>(CurrentSeverityFilter),
		static_cast<int32>(CurrentCategoryFilter),
		static_cast<int32>(CurrentFilter));
}

void SShintToolsPanel::PopulateAssetIssueList(const FShintAssetScanResult& Result)
{
	const double PopStart = FPlatformTime::Seconds();
	AllAssetItems.Reset();
	AllAssetItems.Reserve(Result.Issues.Num());

	// Free-tier display cap. The free-tier promise is "show up to 500 issues
	// total". Server caps the SCAN at 500 assets to bound work, but each asset
	// can fire several rules — so the response can carry many more than 500
	// issue rows. Cap the DISPLAYED rows so the list never exceeds the
	// advertised limit and the banner count stays truthful. Only applied when
	// the server reports tier="free"; Indie returns the full set untouched.
	constexpr int32 FreeTierIssueCap = 500;
	const bool bApplyFreeCap = (Result.Tier == TEXT("free"));

	for (int32 i = 0; i < Result.Issues.Num(); ++i)
	{
		if (bApplyFreeCap && AllAssetItems.Num() >= FreeTierIssueCap)
			break;

		const FShintAssetIssue& Src = Result.Issues[i];
		FShintAssetItemPtr Item = MakeShared<FShintAssetItem>();
		Item->AssetPath     = Src.AssetPath;
		Item->CurrentName   = Src.CurrentName;
		Item->SuggestedName = Src.SuggestedName;
		Item->Reason        = Src.Reason;
		Item->AssetType     = Src.AssetType;
		Item->bChecked      = true;
		Item->OriginalIndex = i;
		AllAssetItems.Add(MoveTemp(Item));
	}

	ApplyAssetFilter();
	if (SendAssetBtn.IsValid()) SendAssetBtn->SetEnabled(true);

	// If the scan returned no violations, show a clear success message.
	if (AllAssetItems.IsEmpty() && AssetEmptyText.IsValid())
		AssetEmptyText->SetText(LOCTEXT("ANBNoIssues", "✓  No naming violations found."));

	UE_LOG(LogShintTools, Verbose,
		TEXT("[BENCH] PopulateAssetIssueList: %.3f s, %d items"),
		FPlatformTime::Seconds() - PopStart, AllAssetItems.Num());
}

// ─────────────────────────────────────────────────────────────────────────────
// Filters
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::ApplyCodeFilter()
{
	CodeIssueItems.Reset();
	CodeIssueItems.Reserve(AllCodeItems.Num());

	for (const FShintIssueItemPtr& Item : AllCodeItems)
	{
		// Compile errors always show regardless of active filters — they are critical.
		const bool bIsBuildError = (Item->RuleId == TEXT("BUILD001"));

		// Toolbar search — case-insensitive across message / file / rule id.
		if (!bIsBuildError && !CodeSearchText.IsEmpty()
			&& !Item->Message.Contains(CodeSearchText)
			&& !Item->FilePath.Contains(CodeSearchText)
			&& !Item->RuleId.Contains(CodeSearchText))
			continue;

		if (!bIsBuildError && CurrentCodeTypeFilter != ECodeTypeFilter::All)
		{
			if (CurrentCodeTypeFilter == ECodeTypeFilter::CppOnly        &&  Item->bIsBlueprint) continue;
			if (CurrentCodeTypeFilter == ECodeTypeFilter::BlueprintsOnly && !Item->bIsBlueprint) continue;
		}

		if (!bIsBuildError && CurrentFilter == EIssueFilter::FixableOnly && !Item->bIsAutoFixable)
			continue;

		if (!bIsBuildError && CurrentSeverityFilter != EIssueSeverityFilter::All)
		{
			const FString SevLower = Item->Severity.ToLower();
			bool bSevMatch = false;
			switch (CurrentSeverityFilter)
			{
			case EIssueSeverityFilter::Critical: bSevMatch = (SevLower == TEXT("critical")); break;
			case EIssueSeverityFilter::Error:    bSevMatch = (SevLower == TEXT("error"));    break;
			case EIssueSeverityFilter::Warning:  bSevMatch = (SevLower == TEXT("warning"));  break;
			case EIssueSeverityFilter::Info:     bSevMatch = (SevLower == TEXT("info"));     break;
			default: bSevMatch = true; break;
			}
			if (!bSevMatch) continue;
		}

		// Category fallback — when the server didn't populate `category`
		// (some BP and validator rules return it empty), fall back to the
		// RuleId prefix. Without this the filter dropped every uncategorised
		// issue and "Performance" silently emptied the panel.
		if (!bIsBuildError && CurrentCategoryFilter != EIssueCategoryFilter::All)
		{
			const FString  CatLower = Item->Category.ToLower();
			const FString& Rid      = Item->RuleId;
			bool bCatMatch = false;
			switch (CurrentCategoryFilter)
			{
			case EIssueCategoryFilter::Performance:
				bCatMatch = CatLower.Contains(TEXT("performance"))
				         || Rid.StartsWith(TEXT("CP")) || Rid.StartsWith(TEXT("BPP"));
				break;
			case EIssueCategoryFilter::BestPractices:
				bCatMatch = CatLower.Contains(TEXT("best")) || CatLower.Contains(TEXT("practice"))
				         || Rid.StartsWith(TEXT("CB")) || Rid.StartsWith(TEXT("BPB"));
				break;
			case EIssueCategoryFilter::Security:
				bCatMatch = CatLower.Contains(TEXT("security"))
				         || Rid.StartsWith(TEXT("CS")) || Rid.StartsWith(TEXT("BPS"));
				break;
			case EIssueCategoryFilter::Maintainability:
				bCatMatch = CatLower.Contains(TEXT("maintain"))
				         || Rid.StartsWith(TEXT("CM")) || Rid.StartsWith(TEXT("BPM"));
				break;
			default: bCatMatch = true; break;
			}
			if (!bCatMatch) continue;
		}

		CodeIssueItems.Add(Item);
	}

	if (CodeIssueListView.IsValid()) CodeIssueListView->RequestListRefresh();

	// Manage the empty-state surface from here too. Without this, a stale
	// filter (e.g. Severity="Error" + a BP scan whose findings are all
	// warnings) collapses the empty state hosted by PopulateCodeIssueList
	// AND empties CodeIssueItems — the user sees a blank panel with no
	// explanation. When AllCodeItems has rows but every one was masked,
	// surface a "no matches for current filters" message instead.
	if (CodeEmptyState.IsValid() && CodeEmptyText.IsValid())
	{
		if (CodeIssueItems.IsEmpty() && !AllCodeItems.IsEmpty())
		{
			CodeEmptyText->SetText(LOCTEXT("CVFilterMasked",
				"No issues match the current filters — reset Category / "
				"Severity / Fixable to see all results."));
			CodeEmptyState->SetVisibility(EVisibility::Visible);
		}
		else if (!CodeIssueItems.IsEmpty())
		{
			CodeEmptyState->SetVisibility(EVisibility::Collapsed);
		}
		// CodeIssueItems empty AND AllCodeItems empty falls through —
		// PopulateCodeIssueList owns that state ("Run a scan" / "No issues
		// found"); we don't second-guess it here.
	}
}

void SShintToolsPanel::ApplyAssetFilter()
{
	AssetIssueItems.Reset();
	AssetIssueItems.Reserve(AllAssetItems.Num());

	for (const FShintAssetItemPtr& Item : AllAssetItems)
	{
		// Toolbar search — case-insensitive across names / path.
		if (!AssetSearchText.IsEmpty()
			&& !Item->CurrentName.Contains(AssetSearchText)
			&& !Item->SuggestedName.Contains(AssetSearchText)
			&& !Item->AssetPath.Contains(AssetSearchText))
			continue;

		if (CurrentAssetTypeFilter != EAssetTypeFilter::All)
		{
			const FString TypeLower = Item->AssetType.ToLower();
			bool bTypeMatch = false;
			switch (CurrentAssetTypeFilter)
			{
			case EAssetTypeFilter::Materials:  bTypeMatch = TypeLower.Contains(TEXT("material"));  break;
			case EAssetTypeFilter::Textures:   bTypeMatch = TypeLower.Contains(TEXT("texture"));   break;
			case EAssetTypeFilter::Meshes:     bTypeMatch = TypeLower.Contains(TEXT("mesh"));      break;
			case EAssetTypeFilter::Blueprints: bTypeMatch = TypeLower.Contains(TEXT("blueprint")); break;
			case EAssetTypeFilter::VFX:        bTypeMatch = TypeLower.Contains(TEXT("niagara")) || TypeLower.Contains(TEXT("particle")); break;
			case EAssetTypeFilter::Audio:      bTypeMatch = TypeLower.Contains(TEXT("sound")) || TypeLower.Contains(TEXT("audio")); break;
			case EAssetTypeFilter::Animations: bTypeMatch = TypeLower.Contains(TEXT("anim"));      break;
			case EAssetTypeFilter::Data:       bTypeMatch = TypeLower.Contains(TEXT("data")) || TypeLower.Contains(TEXT("table")) || TypeLower.Contains(TEXT("curve")); break;
			default: bTypeMatch = true; break;
			}
			if (!bTypeMatch) continue;
		}
		AssetIssueItems.Add(Item);
	}

	if (AssetIssueListView.IsValid()) AssetIssueListView->RequestListRefresh();

	const bool bHasItems = !AssetIssueItems.IsEmpty();
	if (AssetEmptyState.IsValid())
		AssetEmptyState->SetVisibility(bHasItems ? EVisibility::Collapsed : EVisibility::Visible);
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(bHasItems);
	if (SendAssetBtn.IsValid())  SendAssetBtn->SetEnabled(!AllAssetItems.IsEmpty());
	RefreshApplyAssetLabel();
}

// ─────────────────────────────────────────────────────────────────────────────
// Stat refresh
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::RefreshCodeStats()
{
	// FILES counts the DISTINCT files that actually carry an issue — same
	// rule as the Unity client (DynamicToolPanel keys a dict by issue path
	// and shows its Count), so the two engines report the same number for
	// the same project. LastCodeResult.FilesScanned is a different quantity
	// (every file the Core looked at, issue or not) and made UE5 headline a
	// much larger number than Unity for an identical scan.
	if (CodeFiles_Label.IsValid())
		CodeFiles_Label->SetText(FText::FromString(
			FmtN(CountUniqueFiles(AllCodeItems, &FShintIssueItem::FilePath))));
	if (CodeErrors_Label.IsValid())   CodeErrors_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalErrors)));
	if (CodeWarnings_Label.IsValid()) CodeWarnings_Label->SetText(FText::FromString(FmtN(LastCodeResult.TotalWarnings)));
}

void SShintToolsPanel::RefreshAssetStats()
{
	// Drive counters from the backing store so stats reflect total, not the
	// filtered view. AssetTotal: count of UNIQUE assets (one per asset_path),
	// not issue rows — multiple issues on the same asset must not inflate the
	// headline number and break the free-tier 500-asset promise. AssetInvalid:
	// total flagged rows, kept as-is so the user can see "37 issues across 19
	// assets".
	const int32 UniqueAssets = CountUniqueFiles(AllAssetItems,
	                                            &FShintAssetItem::AssetPath);
	const int32 IssueRows    = AllAssetItems.Num();
	if (AssetTotal_Label.IsValid())   AssetTotal_Label->SetText(FText::FromString(FmtN(UniqueAssets)));
	if (AssetInvalid_Label.IsValid()) AssetInvalid_Label->SetText(FText::FromString(FmtN(IssueRows)));
	if (AssetTime_Label.IsValid())    AssetTime_Label->SetText(FText::FromString(
		FString::Printf(TEXT("%.2f"), LastAssetResult.ScanTimeSeconds)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Quality Score
//
// Color thresholds match the dashboard convention:
//   ≥ 90 → green   (healthy)
//   ≥ 70 → yellow  (needs attention)
//   < 70 → red     (poor quality)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	FLinearColor ScoreColor(float Score)
	{
		if (Score >= 90.f) return SShintToolsPanel::C_Green();
		if (Score >= 70.f) return SShintToolsPanel::C_Yellow();
		return SShintToolsPanel::C_Red();
	}
}

void SShintToolsPanel::RefreshQualityScore()
{
	if (CodeScore_Label.IsValid())
	{
		if (LastQualityScore.bValid)
		{
			CodeScore_Label->SetText(FText::FromString(
				FString::Printf(TEXT("%.0f"), LastQualityScore.OverallScore)));
			CodeScore_Label->SetColorAndOpacity(
				FSlateColor(ScoreColor(LastQualityScore.OverallScore)));
		}
		else
		{
			CodeScore_Label->SetText(FText::FromString(TEXT("—")));
			CodeScore_Label->SetColorAndOpacity(FSlateColor(C_Gray()));
		}
	}
}

void SShintToolsPanel::OnLatestScoreFetched(const FShintQualityScoreSnapshot& Snap)
{
	if (!Snap.bValid)
	{
		// 404 / older free server / empty project — keep the inline overall we
		// already painted from the scan response, just log for diagnostics.
		UE_LOG(LogShintTools, Verbose,
			TEXT("Slice B: /metrics/score/latest returned no score (%s)"),
			Snap.ErrorMessage.IsEmpty() ? TEXT("not found") : *Snap.ErrorMessage);
		return;
	}

	LastQualityScore = Snap;
	RefreshQualityScore();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply-button label refresh
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::RefreshApplyCodeLabel()
{
	const int32 N = Algo::CountIf(AllCodeItems,
		[](const FShintIssueItemPtr& P){ return P->bChecked; });
	if (ApplyCodeBtnLabel.IsValid())
		ApplyCodeBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("Fix all (%d)"), N)));
	if (ApplyCodeBtn.IsValid()) ApplyCodeBtn->SetEnabled(N > 0);
}

void SShintToolsPanel::RefreshApplyAssetLabel()
{
	const int32 N = Algo::CountIf(AssetIssueItems,
		[](const FShintAssetItemPtr& P){ return P->bChecked; });
	if (ApplyAssetBtnLabel.IsValid())
		ApplyAssetBtnLabel->SetText(FText::FromString(
			FString::Printf(TEXT("Fix all (%d)"), N)));
	if (ApplyAssetBtn.IsValid()) ApplyAssetBtn->SetEnabled(N > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// State setters + attribute getters
// ─────────────────────────────────────────────────────────────────────────────
void SShintToolsPanel::SetStatus(ECoreStatus S)
{
	StatusState = S;
	// Mirror onto the TopBar's parallel enum. The TopBar LED reads
	// CurrentConnStateIndex — without this bridge it stayed permanently in
	// the Unknown/grey state because nothing else wrote to that index.
	// ECoreStatus and EShintConnState are intentionally separate types so
	// each subsystem owns its vocabulary (the panel speaks "checking", the
	// TopBar speaks "connecting"); the bridge translates between them.
	EShintConnState Bridged = EShintConnState::Unknown;
	switch (S)
	{
	case ECoreStatus::Online:   Bridged = EShintConnState::Connected;    break;
	case ECoreStatus::Offline:  Bridged = EShintConnState::Disconnected; break;
	case ECoreStatus::Checking: Bridged = EShintConnState::Connecting;   break;
	case ECoreStatus::Unknown:
	default:                    Bridged = EShintConnState::Unknown;      break;
	}
	CurrentConnStateIndex = static_cast<int32>(Bridged);
	Invalidate(EInvalidateWidget::Paint);
}

void SShintToolsPanel::SetCodeState(EModuleState S)
{ CodeState = S; Invalidate(EInvalidateWidget::Paint); }

void SShintToolsPanel::SetAssetState(EModuleState S)
{ AssetState = S; Invalidate(EInvalidateWidget::Paint); }

FSlateColor SShintToolsPanel::GetStatusColor() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return FSlateColor(C_Green());
	case ECoreStatus::Offline:  return FSlateColor(C_Red());
	case ECoreStatus::Checking: return FSlateColor(C_Yellow());
	default:                    return FSlateColor(C_DimGray());
	}
}

FText SShintToolsPanel::GetStatusText() const
{
	switch (StatusState)
	{
	case ECoreStatus::Online:   return LOCTEXT("On",  "Online");
	case ECoreStatus::Offline:  return LOCTEXT("Off", "Offline");
	case ECoreStatus::Checking: return LOCTEXT("Chk", "Checking…");
	default:                    return LOCTEXT("Unk", "Not checked");
	}
}

TOptional<float> SShintToolsPanel::GetCodeProgress() const
{
	if (CodeState == EModuleState::Running) return TOptional<float>();
	if (CodeState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

TOptional<float> SShintToolsPanel::GetAssetProgress() const
{
	if (AssetState == EModuleState::Running) return TOptional<float>();
	if (AssetState == EModuleState::Done)    return TOptional<float>(1.f);
	return TOptional<float>(0.f);
}

#undef LOCTEXT_NAMESPACE
